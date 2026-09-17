/* Eclipse SSH - port forwarding implementation.
 *
 * LOCAL  : accept TCP on bind -> ssh_channel_open_forward(session, target) -> pump
 * REMOTE : ssh_channel_open_reverse_listen(session, port) -> accept channel -> connect local TCP -> pump
 * DYNAMIC: SOCKS5 accept on bind -> parse CONNECT -> channel_open_forward -> pump
 */
#include "eclipse/fwd.h"
#include "eclipse/platform.h"

#include <gio/gio.h>

#ifdef _WIN32
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <errno.h>
#include <sys/socket.h>
#endif

#define FWD_BACKLOG 16

typedef struct {
    EcFwdRule rule;
    int listen_fd;      /* local/dynamic listener */
    GThread* thread;
    bool stop;
    EcSshSession* session;
} FwdRuntime;

struct EcFwdManager {
    GMutex lock;
    EcVec runtimes; /* FwdRuntime* */
    EcSshSession* session;
    /* remote-forward callback table: index -> accepted channel pump threads */
};

static void set_rerr(FwdRuntime* r, const char* msg)
{
    snprintf(r->rule.last_error, sizeof r->rule.last_error, "%s", msg);
    EC_LOGE("fwd", "rule %s:%d: %s", r->rule.bind_host, r->rule.bind_port, msg);
}

/* ---------------- socket helpers (via GSocket for portability) ---------------- */
static bool sock_recv_all(GSocket* s, void* buf, size_t len, GError** err)
{
    char* p = buf;
    size_t got = 0;
    while (got < len) {
        gssize n = g_socket_receive(s, p + got, len - got, NULL, err);
        if (n <= 0) return false;
        got += (size_t)n;
    }
    return true;
}

static bool sock_send_all(GSocket* s, const void* buf, size_t len, GError** err)
{
    const char* p = buf;
    size_t sent = 0;
    while (sent < len) {
        gssize n = g_socket_send(s, p + sent, len - sent, NULL, err);
        if (n <= 0) return false;
        sent += (size_t)n;
    }
    return true;
}

/* pump one bidirectional pair: local socket <-> ssh channel (2ms poll tick) */
typedef struct {
    GSocketConnection* conn;
    ssh_channel channel;
    volatile bool stop;
} FwdPair;

/* local socket -> channel pump uses polling on a nonblocking socket */
#ifdef _WIN32
typedef SOCKET ECRawSock;
#define EC_RECV(s,b,n) recv(s,b,n,0)
#define EC_SEND(s,b,n) send(s,b,(int)(n),0)
#else
typedef int ECRawSock;
#define EC_RECV(s,b,n) recv(s,b,n,0)
#define EC_SEND(s,b,n) send(s,b,(size_t)(n),0)
#endif

static gpointer pair_poll_thread(gpointer data)
{
    FwdPair* p = data;
    GSocket* sock = g_socket_connection_get_socket(p->conn);
    ECRawSock lfd = (ECRawSock)g_socket_get_fd(sock);
    char buf[16384];
    while (!p->stop) {
        /* drain local -> ssh (nonblocking reads on socket via MSG_DONTWAIT not portable here;
         * instead set socket nonblocking) */
        gssize n = (gssize)EC_RECV(lfd, buf, sizeof buf);
        if (n > 0) {
            ssize_t w = ssh_channel_write(p->channel, buf, (uint32_t)n);
            if (w <= 0) break;
        } else if (n == 0) {
            break; /* peer closed */
        } else {
#ifdef _WIN32
            int err = WSAGetLastError();
            if (err != WSAEWOULDBLOCK) break;
#else
            if (errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR) break;
#endif
        }
        /* drain ssh -> local */
        int m = ssh_channel_read_nonblocking(p->channel, buf, sizeof buf, 0);
        if (m > 0) {
            ssize_t off = 0;
            while (off < m) {
                ssize_t w = (ssize_t)EC_SEND(lfd, buf + off, m - off);
                if (w <= 0) {
#ifdef _WIN32
                    int err = WSAGetLastError();
                    if (err == WSAEWOULDBLOCK) { g_usleep(2000); continue; }
#else
                    if (errno == EAGAIN || errno == EWOULDBLOCK || errno == EINTR) { g_usleep(2000); continue; }
#endif
                    break;
                }
                off += w;
            }
        } else if (m < 0 && ssh_channel_is_eof(p->channel)) {
            break;
        }
        if (ssh_channel_is_closed(p->channel)) break;
        g_usleep(2000); /* 2ms poll tick - cheap and portable */
    }
    ssh_channel_close(p->channel);
    ssh_channel_free(p->channel);
    g_object_unref(p->conn);
    free(p);
    return NULL;
}

static void start_pair(GSocketConnection* conn, ssh_channel channel)
{
    FwdPair* p = malloc(sizeof(FwdPair));
    if (!p) { ssh_channel_free(channel); g_object_unref(conn); return; }
    p->conn = conn;
    p->channel = channel;
    p->stop = false;
    GSocket* sock = g_socket_connection_get_socket(conn);
    g_socket_set_blocking(sock, FALSE);
    GThread* th = g_thread_new("fwd-pair", pair_poll_thread, p);
    if (th) g_thread_unref(th);
    else { ssh_channel_free(channel); g_object_unref(conn); free(p); }
}

static GSocket* make_listener(int port, GError** err)
{
    GInetAddress* addr = g_inet_address_new_any(G_SOCKET_FAMILY_IPV4);
    GSocketAddress* sa = g_inet_socket_address_new(addr, (guint16)port);
    g_object_unref(addr);
    GSocket* srv = g_socket_new(G_SOCKET_FAMILY_IPV4, G_SOCKET_TYPE_STREAM, G_SOCKET_PROTOCOL_TCP, err);
    if (!srv) { g_object_unref(sa); return NULL; }
    if (!g_socket_bind(srv, sa, TRUE, err) || !g_socket_listen(srv, err)) {
        g_object_unref(srv);
        g_object_unref(sa);
        return NULL;
    }
    g_object_unref(sa);
    return srv;
}

/* ---------------- listener threads ---------------- */
static gpointer local_thread(gpointer data)
{
    FwdRuntime* r = data;
    GError* ge = NULL;
    GSocket* srv = make_listener(r->rule.bind_port, &ge);
    if (!srv) {
        set_rerr(r, ge ? ge->message : "local listen failed");
        g_clear_error(&ge);
        r->rule.running = false;
        return NULL;
    }
    r->rule.running = true;
    while (!r->stop) {
        GSocket* sock = g_socket_accept(srv, NULL, &ge);
        if (!sock) { g_clear_error(&ge); break; }
        if (!r->session || !ec_ssh_is_connected(r->session)) {
            g_object_unref(sock);
            continue;
        }
        GSocketConnection* conn = g_socket_connection_factory_create_connection(sock);
        g_object_unref(sock);
        ssh_channel ch = ssh_channel_new(r->session->libssh);
        if (!ch) { g_object_unref(conn); continue; }
        if (ssh_channel_open_forward(ch, r->rule.target_host, r->rule.target_port,
                                     "localhost", 0) != SSH_OK) {
            set_rerr(r, ssh_get_error(r->session->libssh));
            ssh_channel_free(ch);
            g_object_unref(conn);
            continue;
        }
        start_pair(conn, ch);
    }
    g_socket_close(srv, NULL);
    g_object_unref(srv);
    r->rule.running = false;
    return NULL;
}

static gpointer remote_thread(gpointer data)
{
    FwdRuntime* r = data;
    if (!r->session || !ec_ssh_is_connected(r->session)) {
        set_rerr(r, "not connected");
        return NULL;
    }
    const char* bind = (*r->rule.bind_host) ? r->rule.bind_host : "localhost";
    if (ssh_channel_listen_forward(r->session->libssh, bind, r->rule.bind_port, NULL) != SSH_OK) {
        set_rerr(r, ssh_get_error(r->session->libssh));
        return NULL;
    }
    r->rule.running = true;
    while (!r->stop) {
        char* originator = NULL;
        int originator_port = 0;
        int dport = 0;
        ssh_channel accepted = ssh_channel_open_forward_port(r->session->libssh, 500,
                                                             &dport, &originator, &originator_port);
        if (originator)
            ssh_string_free_char(originator);
        if (!accepted) continue;
        if (r->stop) { ssh_channel_free(accepted); break; }
        /* connect back to the local target */
        GSocketClient* client = g_socket_client_new();
        GError* ge = NULL;
        GSocketConnection* conn = g_socket_client_connect_to_host(client, r->rule.target_host,
                                                                  (guint16)r->rule.target_port, NULL, &ge);
        g_object_unref(client);
        if (!conn) {
            g_clear_error(&ge);
            ssh_channel_close(accepted);
            ssh_channel_free(accepted);
            continue;
        }
        start_pair(conn, accepted);
    }
    ssh_channel_cancel_forward(r->session->libssh, bind, r->rule.bind_port);
    r->rule.running = false;
    return NULL;
}

/* ---------------- SOCKS5 (RFC 1928) ---------------- */
static gpointer dynamic_thread(gpointer data)
{
    FwdRuntime* r = data;
    GError* ge = NULL;
    GSocket* srv = make_listener(r->rule.bind_port, &ge);
    if (!srv) {
        set_rerr(r, ge ? ge->message : "socks listen failed");
        g_clear_error(&ge);
        r->rule.running = false;
        return NULL;
    }
    r->rule.running = true;
    while (!r->stop) {
        GSocket* sock = g_socket_accept(srv, NULL, &ge);
        if (!sock) { g_clear_error(&ge); break; }
        if (!r->session || !ec_ssh_is_connected(r->session)) { g_object_unref(sock); continue; }
        g_socket_set_blocking(sock, TRUE);
        /* handshake: VER=5 NMETHODS=1 METHOD=0 (no-auth) */
        unsigned char hs[2];
        if (!sock_recv_all(sock, hs, 2, NULL) || hs[0] != 5) {
            g_object_unref(sock);
            continue;
        }
        unsigned char resp[2] = { 5, 0 };
        if (!sock_send_all(sock, resp, 2, NULL)) { g_object_unref(sock); continue; }
        /* request: VER CMD RSV ATYP DST.ADDR DST.PORT */
        unsigned char req[4];
        if (!sock_recv_all(sock, req, 4, NULL) || req[1] != 1 /*CONNECT*/) {
            g_object_unref(sock);
            continue;
        }
        char target[260] = { 0 };
        int tport = 0;
        if (req[3] == 1) { /* IPv4 */
            unsigned char a[4];
            unsigned char p[2];
            if (!sock_recv_all(sock, a, 4, NULL) || !sock_recv_all(sock, p, 2, NULL)) { g_object_unref(sock); continue; }
            snprintf(target, sizeof target, "%u.%u.%u.%u", a[0], a[1], a[2], a[3]);
            tport = (p[0] << 8) | p[1];
        } else if (req[3] == 3) { /* domain */
            unsigned char len;
            if (!sock_recv_all(sock, &len, 1, NULL)) { g_object_unref(sock); continue; }
            char dom[256] = { 0 };
            unsigned char p[2];
            if (!sock_recv_all(sock, dom, len, NULL) || !sock_recv_all(sock, p, 2, NULL)) { g_object_unref(sock); continue; }
            snprintf(target, sizeof target, "%s", dom);
            tport = (p[0] << 8) | p[1];
        } else { /* IPv6: read-and-reject in this hop */
            unsigned char a[16];
            unsigned char p[2];
            sock_recv_all(sock, a, 16, NULL);
            sock_recv_all(sock, p, 2, NULL);
            g_object_unref(sock);
            continue;
        }
        ssh_channel ch = ssh_channel_new(r->session->libssh);
        if (!ch) { g_object_unref(sock); continue; }
        if (ssh_channel_open_forward(ch, target, tport, "localhost", 0) != SSH_OK) {
            ssh_channel_free(ch);
            g_object_unref(sock);
            continue;
        }
        unsigned char okresp[10] = { 5, 0, 0, 1, 0, 0, 0, 0, 0, 0 };
        if (!sock_send_all(sock, okresp, 10, NULL)) {
            ssh_channel_free(ch);
            g_object_unref(sock);
            continue;
        }
        GSocketConnection* conn = g_socket_connection_factory_create_connection(sock);
        g_object_unref(sock);
        start_pair(conn, ch);
    }
    g_socket_close(srv, NULL);
    g_object_unref(srv);
    r->rule.running = false;
    return NULL;
}

/* ---------------- manager ---------------- */
EcFwdManager* ec_fwd_new(void)
{
    EcFwdManager* m = calloc(1, sizeof(EcFwdManager));
    if (m) {
        g_mutex_init(&m->lock);
        ec_vec_init(&m->runtimes);
    }
    return m;
}

void ec_fwd_set_session(EcFwdManager* m, EcSshSession* session)
{
    g_mutex_lock(&m->lock);
    m->session = session;
    for (size_t i = 0; i < m->runtimes.len; i++) {
        FwdRuntime* r = m->runtimes.items[i];
        r->session = session;
    }
    g_mutex_unlock(&m->lock);
}

void ec_fwd_free(EcFwdManager* m)
{
    if (!m) return;
    ec_fwd_stop_all(m);
    g_mutex_lock(&m->lock);
    for (size_t i = 0; i < m->runtimes.len; i++) free(m->runtimes.items[i]);
    ec_vec_free(&m->runtimes);
    g_mutex_unlock(&m->lock);
    g_mutex_clear(&m->lock);
    free(m);
}

int ec_fwd_add(EcFwdManager* m, const EcFwdRule* rule)
{
    FwdRuntime* r = calloc(1, sizeof(FwdRuntime));
    if (!r) return -1;
    r->rule = *rule;
    r->listen_fd = -1;
    g_mutex_lock(&m->lock);
    r->session = m->session;
    bool ok = ec_vec_push(&m->runtimes, r);
    int idx = ok ? (int)m->runtimes.len - 1 : -1;
    g_mutex_unlock(&m->lock);
    if (!ok) free(r);
    return idx;
}

bool ec_fwd_remove(EcFwdManager* m, int index)
{
    ec_fwd_stop(m, index);
    g_mutex_lock(&m->lock);
    bool ok = ec_vec_remove_at(&m->runtimes, (size_t)index);
    g_mutex_unlock(&m->lock);
    return ok;
}

bool ec_fwd_update(EcFwdManager* m, int index, const EcFwdRule* rule)
{
    g_mutex_lock(&m->lock);
    bool ok = false;
    if ((size_t)index < m->runtimes.len) {
        FwdRuntime* r = m->runtimes.items[index];
        if (!r->rule.running) {
            r->rule = *rule;
            ok = true;
        }
    }
    g_mutex_unlock(&m->lock);
    return ok;
}

size_t ec_fwd_count(EcFwdManager* m) { return m ? m->runtimes.len : 0; }

const EcFwdRule* ec_fwd_get(EcFwdManager* m, int index)
{
    if (!m || (size_t)index >= m->runtimes.len) return NULL;
    return &((FwdRuntime*)m->runtimes.items[index])->rule;
}

bool ec_fwd_start(EcFwdManager* m, int index, char** err)
{
    g_mutex_lock(&m->lock);
    if ((size_t)index >= m->runtimes.len) { g_mutex_unlock(&m->lock); return false; }
    FwdRuntime* r = m->runtimes.items[index];
    if (r->thread) { g_mutex_unlock(&m->lock); return true; }
    r->stop = false;
    r->session = m->session;
    GThreadFunc fn = r->rule.kind == EC_FWD_LOCAL ? local_thread
                   : r->rule.kind == EC_FWD_REMOTE ? remote_thread
                   : dynamic_thread;
    r->thread = g_thread_new("fwd-listener", fn, r);
    if (!r->thread) {
        if (err) *err = ec_strdup("Cannot start listener thread");
        g_mutex_unlock(&m->lock);
        return false;
    }
    g_mutex_unlock(&m->lock);
    return true;
}

bool ec_fwd_stop(EcFwdManager* m, int index)
{
    g_mutex_lock(&m->lock);
    if ((size_t)index >= m->runtimes.len) { g_mutex_unlock(&m->lock); return false; }
    FwdRuntime* r = m->runtimes.items[index];
    if (r->thread) {
        r->stop = true;
        /* unblock accept() by connecting to self */
        char port[16];
        snprintf(port, sizeof port, "%d", r->rule.bind_port);
        GSocketClient* c = g_socket_client_new();
        GSocketConnection* self = g_socket_client_connect_to_host(c, "127.0.0.1", (guint16)r->rule.bind_port, NULL, NULL);
        if (self) g_object_unref(self);
        g_object_unref(c);
        g_thread_join(r->thread);
        r->thread = NULL;
    }
    r->rule.running = false;
    g_mutex_unlock(&m->lock);
    return true;
}

void ec_fwd_stop_all(EcFwdManager* m)
{
    if (!m) return;
    size_t n = ec_fwd_count(m);
    for (int i = (int)n - 1; i >= 0; i--)
        ec_fwd_stop(m, i);
}

bool ec_fwd_parse_spec(const char* spec, EcFwdKind kind, EcFwdRule* out)
{
    if (!spec || !out) return false;
    memset(out, 0, sizeof *out);
    out->kind = kind;
    /* local/dynamic: [bind:]port[:target_host]:target_port  (dynamic: bind:port)
     * remote: [bind:]port:target_host:target_port */
    unsigned parts[8];
    char copy[512];
    snprintf(copy, sizeof copy, "%s", spec);
    char* fields[5] = { 0 };
    int nf = 0;
    char* save = NULL;
    for (char* tok = strtok_r(copy, ":", &save); tok && nf < 5; tok = strtok_r(NULL, ":", &save))
        fields[nf++] = tok;
    if (kind == EC_FWD_DYNAMIC) {
        if (nf == 1) { out->bind_port = atoi(fields[0]); snprintf(out->bind_host, sizeof out->bind_host, "127.0.0.1"); }
        else if (nf == 2) { snprintf(out->bind_host, sizeof out->bind_host, "%s", fields[0]); out->bind_port = atoi(fields[1]); }
        else return false;
        return ec_valid_port(out->bind_port);
    }
    if (nf == 2) { /* port:target_port (localhost default) */
        out->bind_port = atoi(fields[0]);
        snprintf(out->bind_host, sizeof out->bind_host, "127.0.0.1");
        snprintf(out->target_host, sizeof out->target_host, "localhost");
        out->target_port = atoi(fields[1]);
    } else if (nf == 3) { /* port:host:port */
        out->bind_port = atoi(fields[0]);
        snprintf(out->bind_host, sizeof out->bind_host, "127.0.0.1");
        snprintf(out->target_host, sizeof out->target_host, "%s", fields[1]);
        out->target_port = atoi(fields[2]);
    } else if (nf == 4) { /* bindhost:port:host:port */
        snprintf(out->bind_host, sizeof out->bind_host, "%s", fields[0]);
        out->bind_port = atoi(fields[1]);
        snprintf(out->target_host, sizeof out->target_host, "%s", fields[2]);
        out->target_port = atoi(fields[3]);
    } else {
        return false;
    }
    (void)parts;
    return ec_valid_port(out->bind_port) && ec_valid_port(out->target_port) && out->target_host[0];
}
