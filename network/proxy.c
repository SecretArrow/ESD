/* Eclipse SSH - outbound proxy implementation (POSIX/Win32 sockets via glib). */
#include "eclipse/proxy.h"
#include "eclipse/platform.h"

#include <gio/gio.h>
#ifdef _WIN32
#include <windows.h>
#endif

bool ec_proxy_parse(const char* spec, EcProxy* out)
{
    if (!spec || !out) return false;
    memset(out, 0, sizeof *out);
    if (!*spec) { out->kind = EC_PROXY_NONE; return true; }
    EcProxyKind kind;
    const char* rest = NULL;
    if (g_str_has_prefix(spec, "socks5://")) { kind = EC_PROXY_SOCKS5; rest = spec + 9; }
    else if (g_str_has_prefix(spec, "socks4://")) { kind = EC_PROXY_SOCKS4; rest = spec + 9; }
    else if (g_str_has_prefix(spec, "http://")) { kind = EC_PROXY_HTTP; rest = spec + 7; }
    else return false;
    out->kind = kind;
    /* optional user:pass@ */
    const char* at = strrchr(rest, '@');
    if (at) {
        const char* colon = strchr(rest, ':');
        if (colon && colon < at) {
            size_t ul = (size_t)(colon - rest);
            if (ul >= sizeof out->user) return false;
            memcpy(out->user, rest, ul);
            size_t pl = (size_t)(at - colon - 1);
            if (pl >= sizeof out->pass) return false;
            memcpy(out->pass, colon + 1, pl);
        } else {
            size_t ul = (size_t)(at - rest);
            if (ul >= sizeof out->user) return false;
            memcpy(out->user, rest, ul);
        }
        rest = at + 1;
    }
    char* host = NULL;
    int port = 0;
    if (!ec_parse_hostport(rest, &host, &port, kind == EC_PROXY_HTTP ? 8080 : 1080))
        return false;
    snprintf(out->host, sizeof out->host, "%s", host);
    out->port = port;
    free(host);
    return out->host[0] && ec_valid_port(out->port);
}

int ec_proxy_dial(const EcProxy* px, const char* dest_host, int dest_port,
                  int timeout_ms, char** err)
{
    GError* ge = NULL;
    if (timeout_ms <= 0) timeout_ms = 15000;
    GSocketClient* client = g_socket_client_new();
    g_socket_client_set_timeout(client, timeout_ms / 1000 + 1);
    char target[300];
    snprintf(target, sizeof target, "%s:%d", px->host, px->port);
    GSocketConnection* conn = g_socket_client_connect_to_host(client, target, (guint16)px->port, NULL, &ge);
    g_object_unref(client);
    if (!conn) {
        if (err) *err = ec_strdup(ge ? ge->message : "proxy connect failed");
        g_clear_error(&ge);
        return -1;
    }
    GSocket* sock = g_socket_connection_get_socket(conn);
    char buf[512];
    bool ok = false;
    if (px->kind == EC_PROXY_SOCKS5) {
        bool have_auth = px->user[0] != '\0';
        unsigned char hs[4] = { 5, have_auth ? 2u : 1u, 0, have_auth ? 2u : 0u };
        size_t hslen = have_auth ? 4 : 3;
        ok = g_socket_send(sock, (gpointer)hs, hslen, NULL, &ge) == (gssize)hslen;
        if (ok) ok = g_socket_receive(sock, buf, 2, NULL, &ge) == 2 && (unsigned char)buf[0] == 5;
        if (ok && have_auth && (unsigned char)buf[1] == 2) {
            size_t ul = strlen(px->user), pl = strlen(px->pass);
            if (ul > 255 || pl > 255) ok = false;
            else {
                buf[0] = 1;
                buf[1] = (char)ul;
                memcpy(buf + 2, px->user, ul);
                buf[2 + ul] = (char)pl;
                memcpy(buf + 3 + ul, px->pass, pl);
                size_t total = 3 + ul + pl;
                ok = g_socket_send(sock, buf, total, NULL, &ge) == (gssize)total &&
                     g_socket_receive(sock, buf, 2, NULL, &ge) == 2 && buf[1] == 0;
            }
        }
        if (ok) {
            /* CONNECT request: VER=5 CMD=1 RSV=0 ATYP=3 domain */
            size_t dl = strlen(dest_host);
            if (dl > 255) ok = false;
            else {
                buf[0] = 5; buf[1] = 1; buf[2] = 0; buf[3] = 3;
                buf[4] = (char)dl;
                memcpy(buf + 5, dest_host, dl);
                buf[5 + dl] = (char)(dest_port >> 8);
                buf[6 + dl] = (char)(dest_port & 0xFF);
                size_t total = 7 + dl;
                ok = g_socket_send(sock, buf, total, NULL, &ge) == (gssize)total &&
                     g_socket_receive(sock, buf, 4, NULL, &ge) == 4 && buf[1] == 0;
                if (ok) {
                    unsigned char alen = (unsigned char)buf[3];
                    size_t skip = alen == 1 ? 4 : alen == 3 ? 1 + alen : 16;
                    size_t got = 0;
                    while (ok && got < skip + 2) {
                        gssize n = g_socket_receive(sock, buf, sizeof buf, NULL, &ge);
                        if (n <= 0) ok = false;
                        else got += (size_t)n;
                    }
                }
            }
        }
        if (!ok && err && ge) *err = ec_strdup(ge->message);
        else if (!ok && err) *err = ec_strdup("SOCKS5 handshake failed");
    } else if (px->kind == EC_PROXY_SOCKS4) {
        /* SOCKS4a: VER=4 CMD=1 DSTPORT DSTIP=(0,0,0,x) USERID NUL HOSTNAME NUL */
        size_t ul = strlen(px->user);
        if (ul > 255) ul = 255;
        size_t dl = strlen(dest_host);
        if (dl > 255) dl = 255;
        buf[0] = 4; buf[1] = 1;
        buf[2] = (char)(dest_port >> 8);
        buf[3] = (char)(dest_port & 0xFF);
        buf[4] = 0; buf[5] = 0; buf[6] = 0; buf[7] = 1; /* 0.0.0.1 => SOCKS4a */
        memcpy(buf + 8, px->user, ul);
        buf[8 + ul] = 0;
        memcpy(buf + 9 + ul, dest_host, dl);
        buf[9 + ul + dl] = 0;
        size_t total = 10 + ul + dl;
        ok = g_socket_send(sock, buf, total, NULL, &ge) == (gssize)total &&
             g_socket_receive(sock, buf, 8, NULL, &ge) == 8 && buf[1] == 90;
        if (!ok && err) *err = ec_strdup(ge ? ge->message : "SOCKS4 handshake failed");
    } else if (px->kind == EC_PROXY_HTTP) {
        int n = snprintf(buf, sizeof buf,
                         "CONNECT %s:%d HTTP/1.1\r\nHost: %s:%d\r\n"
                         "Proxy-Authorization: Basic %s\r\n\r\n",
                         dest_host, dest_port, dest_host, dest_port, px->user[0] ? "x" : "");
        /* NOTE: real Basic auth encoding done below when user present */
        if (px->user[0]) {
            /* base64(user:pass) minimal encoder */
            char up[280];
            int l = snprintf(up, sizeof up, "%s:%s", px->user, px->pass);
            static const char* b64t = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
            char b64[400];
            size_t bo = 0;
            for (int i = 0; i < l; i += 3) {
                unsigned v = (unsigned char)up[i] << 16;
                if (i + 1 < l) v |= (unsigned char)up[i + 1] << 8;
                if (i + 2 < l) v |= (unsigned char)up[i + 2];
                b64[bo++] = b64t[(v >> 18) & 63];
                b64[bo++] = b64t[(v >> 12) & 63];
                b64[bo++] = (i + 1 < l) ? b64t[(v >> 6) & 63] : '=';
                b64[bo++] = (i + 2 < l) ? b64t[v & 63] : '=';
            }
            b64[bo] = 0;
            n = snprintf(buf, sizeof buf,
                         "CONNECT %s:%d HTTP/1.1\r\nHost: %s:%d\r\nProxy-Authorization: Basic %s\r\n\r\n",
                         dest_host, dest_port, dest_host, dest_port, b64);
        }
        ok = n > 0 && g_socket_send(sock, buf, (size_t)n, NULL, &ge) == n;
        if (ok) {
            size_t got = 0;
            while (ok && got < sizeof buf - 1) {
                gssize r = g_socket_receive(sock, buf + got, 1, NULL, &ge);
                if (r <= 0) { ok = false; break; }
                got += (size_t)r;
                if (got >= 4 && memcmp(buf + got - 4, "\r\n\r\n", 4) == 0) break;
            }
            ok = ok && got > 12 && memcmp(buf, "HTTP/1.", 7) == 0 && strstr(buf, " 2") != NULL;
        }
        if (!ok && err) *err = ec_strdup(ge ? ge->message : "HTTP CONNECT failed");
    }
    g_clear_error(&ge);
    if (!ok) {
        g_object_unref(conn);
        return -1;
    }
    /* hand the fd to the caller; detach from GIO lifetime */
    int fd = g_socket_get_fd(sock);
    g_socket_set_blocking(sock, TRUE);
    /* keep descriptor valid after unref: dup it */
#ifdef _WIN32
    HANDLE dupfd = NULL;
    DuplicateHandle(GetCurrentProcess(), (HANDLE)(intptr_t)fd, GetCurrentProcess(), &dupfd, 0, FALSE, DUPLICATE_SAME_ACCESS);
    g_object_unref(conn);
    return (int)(intptr_t)dupfd; /* libssh accepts SOCKET handles on Windows */
#else
    int dupfd = dup(fd);
    g_object_unref(conn);
    return dupfd;
#endif
}
