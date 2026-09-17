/* Eclipse SSH - SSH engine implementation (libssh). */
#include "eclipse/ssh.h"

#include <stdio.h>
#include <glib.h>
#include <openssl/sha.h>

/* map ssh_key_type to known_hosts type string */
static const char* keytype_str(ssh_key k)
{
    switch (ssh_key_type(k)) {
    case SSH_KEYTYPE_ED25519: return "ssh-ed25519";
    case SSH_KEYTYPE_RSA:
    case SSH_KEYTYPE_RSA1: return "ssh-rsa";
    case SSH_KEYTYPE_ECDSA_P256: return "ecdsa-sha2-nistp256";
    case SSH_KEYTYPE_ECDSA_P384: return "ecdsa-sha2-nistp384";
    case SSH_KEYTYPE_ECDSA_P521: return "ecdsa-sha2-nistp521";
    case SSH_KEYTYPE_DSS: return "ssh-dss";
    default: return "ssh-unknown";
    }
}

char* ec_ssh_key_fingerprint_b64(ssh_key key)
{
    char* blob_b64 = NULL;
    if (ssh_pki_export_pubkey_base64(key, &blob_b64) != SSH_OK || !blob_b64)
        return NULL;
    /* decode blob to digest SHA256 */
    int max = (int)(strlen(blob_b64) * 3 / 4) + 3;
    uint8_t* blob = malloc((size_t)max);
    if (!blob) { ssh_string_free_char(blob_b64); return NULL; }
    size_t blob_len = 0;
    /* decode base64 inline (small local table; EVP not linked in this unit) */
    {
        static const char* tb64 = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
        int8_t rev[256];
        memset(rev, -1, sizeof rev);
        for (int i = 0; i < 64; i++) rev[(unsigned char)tb64[i]] = (int8_t)i;
        uint32_t acc = 0;
        int nbits = 0;
        for (const char* p = blob_b64; *p && *p != '='; p++) {
            int8_t v = rev[(unsigned char)*p];
            if (v < 0) continue;
            acc = (acc << 6) | (uint32_t)(uint8_t)v;
            nbits += 6;
            if (nbits >= 8) {
                nbits -= 8;
                blob[blob_len++] = (uint8_t)((acc >> nbits) & 0xFF);
            }
        }
    }
    ssh_string_free_char(blob_b64);
    uint8_t digest[32];
    SHA256(blob, blob_len, digest);
    free(blob);
    /* base64-encode digest (RFC 4648, with '=' padding stripped by caller) */
    static const char* enc = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    char out[64];
    size_t o = 0;
    for (size_t i = 0; i < 32; i += 3) {
        uint32_t v = (uint32_t)digest[i] << 16;
        if (i + 1 < 32) v |= (uint32_t)digest[i + 1] << 8;
        if (i + 2 < 32) v |= digest[i + 2];
        out[o++] = enc[(v >> 18) & 63];
        out[o++] = enc[(v >> 12) & 63];
        out[o++] = (i + 1 < 32) ? enc[(v >> 6) & 63] : '=';
        out[o++] = (i + 2 < 32) ? enc[v & 63] : '=';
    }
    out[o] = '\0';
    while (o && out[o - 1] == '=') out[--o] = '\0';
    EcStr b;
    ec_str_init(&b);
    ec_str_printf(&b, "SHA256:%s", out);
    return ec_str_take(&b);
}

static void set_error(EcSshSession* s, const char* msg)
{
    snprintf(s->last_error, sizeof s->last_error, "%s", msg ? msg : "unknown error");
    EC_LOGE("ssh", "%s", s->last_error);
}

EcSshSession* ec_ssh_new(void)
{
    EcSshSession* s = calloc(1, sizeof(EcSshSession));
    if (!s) return NULL;
    s->libssh = ssh_new();
    if (!s->libssh) { free(s); return NULL; }
    s->state = EC_SSH_IDLE;
    return s;
}

static void free_params_in(EcSshSession* s)
{
    free(s->owned.host);
    free(s->owned.username);
    free(s->owned.password);
    free(s->owned.key_path);
    free(s->owned.key_passphrase);
    memset(&s->owned, 0, sizeof s->owned);
    free(s->server_key.type);
    free(s->server_key.blob_b64);
    free(s->server_key.fingerprint);
    memset(&s->server_key, 0, sizeof s->server_key);
}

void ec_ssh_free(EcSshSession* s)
{
    if (!s) return;
    ec_ssh_disconnect(s);
    free_params_in(s);
    free(s);
}

bool ec_ssh_is_connected(const EcSshSession* s)
{
    return s && s->libssh && ssh_is_connected(s->libssh) == 1 && s->state == EC_SSH_CONNECTED;
}

const char* ec_ssh_last_error(const EcSshSession* s) { return s ? s->last_error : "null session"; }

void ec_ssh_disconnect(EcSshSession* s)
{
    if (!s) return;
    if (s->libssh && ssh_is_connected(s->libssh) == 1)
        ssh_disconnect(s->libssh);
    ssh_free(s->libssh);
    s->libssh = ssh_new();
    s->state = EC_SSH_IDLE;
    s->bytes_in = s->bytes_out = 0;
}

void ec_ssh_send_keepalive(EcSshSession* s)
{
    if (!s || !s->libssh || ssh_is_connected(s->libssh) != 1) return;
    int rc = ssh_send_ignore(s->libssh, "eclipse-ssh-keepalive");
    (void)rc;
}

bool ec_ssh_connect(EcSshSession* s, const EcSshParams* p,
                    EcHostKeyAcceptCb hostkey_cb, void* cb_user, char** err)
{
    if (!s || !p || !p->host || !p->username || !p->hostkeys) {
        if (err) *err = ec_strdup("Invalid connect parameters");
        return false;
    }
    free_params_in(s);
    s->owned.host = ec_strdup(p->host);
    s->owned.username = ec_strdup(p->username);
    if (p->password && *p->password) s->owned.password = ec_strdup(p->password);
    if (p->key_path && *p->key_path) s->owned.key_path = ec_strdup(p->key_path);
    if (p->key_passphrase && *p->key_passphrase) s->owned.key_passphrase = ec_strdup(p->key_passphrase);
    s->owned.port = p->port ? p->port : 22;
    s->owned.timeout_ms = p->timeout_ms ? p->timeout_ms : 15000;
    s->owned.try_agent = p->try_agent;
    s->owned.hostkeys = p->hostkeys;

    ssh_session ses = s->libssh;
    ssh_options_set(ses, SSH_OPTIONS_HOST, p->host);
    int port = s->owned.port;
    ssh_options_set(ses, SSH_OPTIONS_PORT, &port);
    ssh_options_set(ses, SSH_OPTIONS_USER, p->username);
    int timeout = s->owned.timeout_ms / 1000;
    ssh_options_set(ses, SSH_OPTIONS_TIMEOUT, &timeout);
    int no_delay = 1;
    ssh_options_set(ses, SSH_OPTIONS_NODELAY, &no_delay);
    ssh_options_set(ses, SSH_OPTIONS_COMPRESSION, p->use_compression ? "yes" : "no");
    ssh_options_set(ses, SSH_OPTIONS_KEY_EXCHANGE, NULL); /* defaults */

    s->state = EC_SSH_CONNECTING;
    if (ssh_connect(ses) != SSH_OK) {
        const char* e = ssh_get_error(ses);
        if (err) *err = ec_strdup(e ? e : "Connection failed");
        set_error(s, e);
        s->state = EC_SSH_ERROR;
        return false;
    }

    /* ---- host key verification (mandatory) ---- */
    s->state = EC_SSH_HOSTKEY;
    ssh_key srv = NULL;
    if (ssh_get_server_publickey(ses, &srv) != SSH_OK || !srv) {
        if (err) *err = ec_strdup("Could not read server host key");
        s->state = EC_SSH_ERROR;
        return false;
    }
    char* blob_b64 = NULL;
    if (ssh_pki_export_pubkey_base64(srv, &blob_b64) != SSH_OK || !blob_b64) {
        ssh_key_free(srv);
        if (err) *err = ec_strdup("Could not serialize server host key");
        s->state = EC_SSH_ERROR;
        return false;
    }
    free(s->server_key.type);
    free(s->server_key.blob_b64);
    free(s->server_key.fingerprint);
    s->server_key.type = ec_strdup(keytype_str(srv));
    s->server_key.blob_b64 = blob_b64;
    s->server_key.fingerprint = ec_ssh_key_fingerprint_b64(srv);
    ssh_key_free(srv);

    EcHostKeyStatus st = ec_hostkeys_check(p->hostkeys, p->host, port,
                                           s->server_key.type, blob_b64);
    bool accept = (st == EC_HK_OK);
    if (!accept) {
        if (hostkey_cb)
            accept = hostkey_cb(&s->server_key, st, p->host, port, cb_user);
        if (accept) {
            if (!ec_hostkeys_accept(p->hostkeys, p->host, port, s->server_key.type, blob_b64)) {
                if (err) *err = ec_strdup("Could not persist accepted host key");
                s->state = EC_SSH_ERROR;
                return false;
            }
        } else {
            if (err)
                *err = ec_strdup(st == EC_HK_CHANGED
                                     ? "Host key CHANGED - connection rejected (possible MITM)"
                                     : "Host key rejected by user");
            set_error(s, *err);
            ssh_disconnect(ses);
            s->state = EC_SSH_ERROR;
            return false;
        }
    }

    /* ---- authentication ---- */
    s->state = EC_SSH_AUTHENTICATING;
    int rc = SSH_AUTH_ERROR;
    if (s->owned.key_path) {
        ssh_key key = NULL;
        rc = ssh_pki_import_privkey_file(s->owned.key_path,
                                         s->owned.key_passphrase ? s->owned.key_passphrase : NULL,
                                         NULL, NULL, &key);
        if (rc != SSH_OK) {
            if (err) *err = ec_strdup("Could not read private key (wrong passphrase?)");
            s->state = EC_SSH_ERROR;
            return false;
        }
        rc = ssh_userauth_publickey(ses, s->owned.username, key);
        ssh_key_free(key);
    } else if (s->owned.password) {
        rc = ssh_userauth_password(ses, s->owned.username, s->owned.password);
    }
    if (rc != SSH_AUTH_SUCCESS && s->owned.try_agent)
        rc = ssh_userauth_agent(ses, s->owned.username);
    if (rc != SSH_AUTH_SUCCESS && s->owned.password)
        rc = ssh_userauth_kbdint(ses, s->owned.username, NULL);
    if (rc != SSH_AUTH_SUCCESS) {
        if (err) *err = ec_strdup("Authentication failed");
        set_error(s, "Authentication failed");
        ssh_disconnect(ses);
        s->state = EC_SSH_ERROR;
        return false;
    }
    s->state = EC_SSH_CONNECTED;
    s->connected_at_ms = ec_now_ms();
    return true;
}

/* -------------------------------------------------------------- channels */
EcSshChannel* ec_ssh_open_pty(EcSshSession* s, int cols, int rows, const char* term, char** err)
{
    if (!ec_ssh_is_connected(s)) {
        if (err) *err = ec_strdup("Not connected");
        return NULL;
    }
    ssh_channel ch = ssh_channel_new(s->libssh);
    if (!ch) { if (err) *err = ec_strdup("Out of memory"); return NULL; }
    if (ssh_channel_open_session(ch) != SSH_OK) {
        const char* e = ssh_get_error(s->libssh);
        if (err) *err = ec_strdup(e ? e : "Cannot open channel");
        ssh_channel_free(ch);
        return NULL;
    }
    if (ssh_channel_request_pty_size(ch, term ? term : "xterm-256color", cols, rows) != SSH_OK) {
        EC_LOGW("ssh", "pty request failed: %s", ssh_get_error(s->libssh));
    }
    if (ssh_channel_request_shell(ch) != SSH_OK) {
        const char* e = ssh_get_error(s->libssh);
        if (err) *err = ec_strdup(e ? e : "Shell request rejected");
        ssh_channel_free(ch);
        return NULL;
    }
    EcSshChannel* c = calloc(1, sizeof(EcSshChannel));
    if (!c) { ssh_channel_free(ch); return NULL; }
    c->ch = ch;
    c->session = s;
    return c;
}

EcSshChannel* ec_ssh_open_exec(EcSshSession* s, const char* command, char** err)
{
    if (!ec_ssh_is_connected(s)) {
        if (err) *err = ec_strdup("Not connected");
        return NULL;
    }
    ssh_channel ch = ssh_channel_new(s->libssh);
    if (!ch) { if (err) *err = ec_strdup("Out of memory"); return NULL; }
    if (ssh_channel_open_session(ch) != SSH_OK) {
        if (err) *err = ec_strdup(ssh_get_error(s->libssh));
        ssh_channel_free(ch);
        return NULL;
    }
    if (ssh_channel_request_exec(ch, command) != SSH_OK) {
        if (err) *err = ec_strdup(ssh_get_error(s->libssh));
        ssh_channel_free(ch);
        return NULL;
    }
    EcSshChannel* c = calloc(1, sizeof(EcSshChannel));
    if (!c) { ssh_channel_free(ch); return NULL; }
    c->ch = ch;
    c->session = s;
    return c;
}

void ec_ssh_chan_free(EcSshChannel* c)
{
    if (!c) return;
    if (c->ch) {
        ssh_channel_close(c->ch);
        ssh_channel_free(c->ch);
    }
    free(c);
}

int ec_ssh_chan_read(EcSshChannel* c, char* buf, int len, int is_stderr)
{
    if (!c || !c->ch) return -1;
    int n = ssh_channel_read_nonblocking(c->ch, buf, (uint32_t)len, is_stderr);
    if (n > 0) {
        if (is_stderr == 0) c->session->bytes_in += (uint64_t)n;
        return n;
    }
    if (n == 0)
        return ssh_channel_is_eof(c->ch) ? -1 : 0;
    return -1;
}

int ec_ssh_chan_write(EcSshChannel* c, const char* buf, int len)
{
    if (!c || !c->ch || len <= 0) return -1;
    int written = 0;
    while (written < len) {
        int n = ssh_channel_write(c->ch, buf + written, (uint32_t)(len - written));
        if (n <= 0) break;
        written += n;
        c->session->bytes_out += (uint64_t)n;
    }
    return written;
}

void ec_ssh_chan_resize(EcSshChannel* c, int cols, int rows)
{
    if (c && c->ch)
        ssh_channel_change_pty_size(c->ch, cols, rows);
}

bool ec_ssh_chan_eof(EcSshChannel* c)
{
    return !c || !c->ch || ssh_channel_is_eof(c->ch) != 0 || ssh_channel_is_closed(c->ch) != 0;
}

int ec_ssh_chan_exit_status(EcSshChannel* c)
{
    if (!c || !c->ch) return -1;
#if defined(LIBSSH_VERSION_MAJOR) && (LIBSSH_VERSION_MAJOR > 0 || LIBSSH_VERSION_MINOR >= 11)
    uint32_t code = 0;
    char* sig = NULL;
    int core = 0;
    if (ssh_channel_get_exit_state(c->ch, &code, &sig, &core) != SSH_OK) return -1;
    if (sig) ssh_string_free_char(sig);
    return (int)code;
#else
    return ssh_channel_get_exit_status(c->ch);
#endif
}

bool ec_ssh_wait_readable(EcSshChannel* c, int timeout_ms)
{
    /* small cooperative wait: the read pump polls with nonblocking reads;
     * this helper just parks the thread briefly (glib, portable). */
    if (timeout_ms > 0) g_usleep((gulong)timeout_ms * 1000u);
    return c && c->ch && ssh_channel_is_closed(c->ch) == 0 && ssh_channel_is_eof(c->ch) == 0;
}

int ec_ssh_run_command(EcSshSession* s, const char* command, EcStr* out, char** err)
{
    EcSshChannel* c = ec_ssh_open_exec(s, command, err);
    if (!c) return -1;
    char buf[4096];
    for (;;) {
        int n = ec_ssh_chan_read(c, buf, sizeof buf, 0);
        if (n > 0) ec_str_append_n(out, buf, (size_t)n);
        else if (n < 0) break;
        else {
            int e = ec_ssh_chan_read(c, buf, sizeof buf, 1);
            if (e > 0) ec_str_append_n(out, buf, (size_t)e);
            else if (e < 0) break;
            else ec_ssh_wait_readable(c, 50);
        }
    }
    int status = ec_ssh_chan_exit_status(c);
    ec_ssh_chan_free(c);
    return status;
}
