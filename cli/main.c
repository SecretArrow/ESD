/* Eclipse SSH - CLI companion: reuses the same core libraries as the GUI
 * (spec #52 - no duplicated SSH implementation).
 *
 *   eclipse-ssh-cli list
 *   eclipse-ssh-cli connect <profile> [-- command...]
 *   eclipse-ssh-cli exec <profile> <command>
 *   eclipse-ssh-cli upload <local> <profile:remote>
 *   eclipse-ssh-cli download <profile:remote> <local>
 *   eclipse-ssh-cli tunnel <profile> --local [bind:]port:host:port
 *   eclipse-ssh-cli pipe [--proxy SPEC] HOST PORT   (ProxyCommand helper for jump/proxy)
 */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "eclipse/repos.h"
#include "eclipse/security.h"
#include "eclipse/ssh.h"
#include "eclipse/sftp.h"
#include "eclipse/fwd.h"
#include "eclipse/proxy.h"
#include "eclipse/platform.h"

#include <gio/gio.h>
#include <sys/select.h>
#include <unistd.h>

static int g_exit_code = 0;

static void files_init(EcSessionStore* sessions, EcVault** vault, EcHostKeyStore** hk,
                       char* config_dir, size_t cap)
{
    char* d = ec_app_config_dir();
    snprintf(config_dir, cap, "%s", d ? d : ".");
    free(d);
    ec_mkdir_p(config_dir);
    char* p = ec_path_join(config_dir, "sessions.json");
    ec_sessions_init(sessions, p);
    ec_sessions_load(sessions);
    free(p);
    char* dd = ec_app_data_dir();
    char* dp = ec_path_join(dd ? dd : ".", "vault.json");
    *vault = ec_vault_open(dp);
    free(dp);
    dp = ec_path_join(dd ? dd : ".", "known_hosts");
    *hk = ec_hostkeys_open(dp);
    free(dp);
    free(dd);
}

static bool hostkey_auto(const EcServerKey* key, EcHostKeyStatus st, const char* host,
                         int port, void* user)
{
    (void)key; (void)user;
    if (st == EC_HK_UNKNOWN) {
        fprintf(stderr, "[eclipse-ssh] first connection to %s:%d - fingerprint %s accepted (TOFU)\n",
                host, port, key->fingerprint ? key->fingerprint : "?");
        return true;
    }
    fprintf(stderr, "[eclipse-ssh] REFUSING %s:%d: host key CHANGED (possible MITM)\n", host, port);
    return false;
}

static bool connect_profile(EcSession* s, EcVault* vault, EcHostKeyStore* hk, EcSshSession* ssh)
{
    EcSshParams p = { 0 };
    p.host = s->host;
    p.port = s->port;
    p.username = s->username;
    p.hostkeys = hk;
    p.timeout_ms = 15000;
    p.key_path = s->key_path[0] ? s->key_path : NULL;
    p.try_agent = s->auth_mode == EC_AUTH_AGENT || s->auth_mode == EC_AUTH_KEY_AGENT || s->use_agent_fallback;
    if (s->auth_mode == EC_AUTH_PASSWORD) {
        char *user = NULL, *pass = NULL;
        if (vault && ec_vault_get(vault, s->id, &user, &pass))
            p.password = pass;
        free(user);
        if (!p.password) {
            fprintf(stderr, "[eclipse-ssh] no stored password for '%s'\n", s->name);
            return false;
        }
    }
    char* err = NULL;
    bool ok = ec_ssh_connect(ssh, &p, hostkey_auto, NULL, &err);
    if (!ok) {
        fprintf(stderr, "[eclipse-ssh] connect failed: %s\n", err ? err : ec_ssh_last_error(ssh));
        free(err);
    }
    return ok;
}

static void cmd_list(EcSessionStore* st)
{
    for (size_t i = 0; i < st->sessions.len; i++) {
        EcSession* s = st->sessions.items[i];
        printf("%-24s %-28s %s@%s:%d\n", s->name, s->id, s->username, s->host, s->port);
    }
    printf("(%zu sessions)\n", st->sessions.len);
}

static void cmd_exec(EcSession* s, EcVault* vault, EcHostKeyStore* hk, const char* command)
{
    EcSshSession* ssh = ec_ssh_new();
    if (!connect_profile(s, vault, hk, ssh)) { g_exit_code = 2; ec_ssh_free(ssh); return; }
    EcStr out;
    ec_str_init(&out);
    char* err = NULL;
    int rc = ec_ssh_run_command(ssh, command, &out, &err);
    if (out.s) fputs(out.s, stdout);
    free(err);
    ec_str_free(&out);
    g_exit_code = rc < 0 ? 2 : rc;
    ec_ssh_free(ssh);
}

static void cmd_interactive(EcSession* s, EcVault* vault, EcHostKeyStore* hk)
{
    EcSshSession* ssh = ec_ssh_new();
    if (!connect_profile(s, vault, hk, ssh)) { g_exit_code = 2; ec_ssh_free(ssh); return; }
    EcSshChannel* ch = ec_ssh_open_pty(ssh, 80, 24, "xterm-256color", NULL);
    if (!ch) { fprintf(stderr, "pty open failed\n"); g_exit_code = 2; ec_ssh_free(ssh); return; }
    /* bridge stdin/stdout to the channel until EOF */
    for (;;) {
        char buf[4096];
        int n = ec_ssh_chan_read(ch, buf, sizeof buf, 0);
        if (n > 0) fwrite(buf, 1, (size_t)n, stdout);
        else if (n < 0) break;
        else {
            fflush(stdout);
            int c = fgetc(stdin);
            if (c == EOF) break;
            char cc = (char)c;
            ec_ssh_chan_write(ch, &cc, 1);
        }
    }
    ec_ssh_chan_free(ch);
    ec_ssh_free(ssh);
}

static void cmd_upload(EcSession* s, EcVault* vault, EcHostKeyStore* hk,
                       const char* local, const char* remote)
{
    EcSshSession* ssh = ec_ssh_new();
    if (!connect_profile(s, vault, hk, ssh)) { g_exit_code = 2; ec_ssh_free(ssh); return; }
    char* err = NULL;
    EcSftp* sf = ec_sftp_open(ssh, &err);
    if (!sf) { fprintf(stderr, "sftp failed: %s\n", err); g_exit_code = 2; ec_ssh_free(ssh); return; }
    EcTransfer t;
    memset(&t, 0, sizeof t);
    t.kind = EC_TR_UPLOAD;
    t.local_path = (char*)local;
    t.remote_path = (char*)remote;
    t.session = ssh;
    t.sftp = sf;
    bool ok = ec_sftp_upload_file(&t);
    if (!ok) fprintf(stderr, "upload failed: %s\n", t.error);
    g_exit_code = ok ? 0 : 2;
    ec_sftp_free(sf);
    ec_ssh_free(ssh);
}

static void cmd_download(EcSession* s, EcVault* vault, EcHostKeyStore* hk,
                         const char* remote, const char* local)
{
    EcSshSession* ssh = ec_ssh_new();
    if (!connect_profile(s, vault, hk, ssh)) { g_exit_code = 2; ec_ssh_free(ssh); return; }
    char* err = NULL;
    EcSftp* sf = ec_sftp_open(ssh, &err);
    if (!sf) { fprintf(stderr, "sftp failed: %s\n", err); g_exit_code = 2; ec_ssh_free(ssh); return; }
    EcTransfer t;
    memset(&t, 0, sizeof t);
    t.kind = EC_TR_DOWNLOAD;
    t.local_path = (char*)local;
    t.remote_path = (char*)remote;
    t.session = ssh;
    t.sftp = sf;
    bool ok = ec_sftp_download_file(&t);
    if (!ok) fprintf(stderr, "download failed: %s\n", t.error);
    g_exit_code = ok ? 0 : 2;
    ec_sftp_free(sf);
    ec_ssh_free(ssh);
}

static void cmd_tunnel(EcSession* s, EcVault* vault, EcHostKeyStore* hk, const char* spec)
{
    EcSshSession* ssh = ec_ssh_new();
    if (!connect_profile(s, vault, hk, ssh)) { g_exit_code = 2; ec_ssh_free(ssh); return; }
    EcFwdRule rule;
    if (!ec_fwd_parse_spec(spec, EC_FWD_LOCAL, &rule)) {
        fprintf(stderr, "bad tunnel spec: %s\n", spec);
        g_exit_code = 2;
        ec_ssh_free(ssh);
        return;
    }
    EcFwdManager* m = ec_fwd_new();
    ec_fwd_set_session(m, ssh);
    int idx = ec_fwd_add(m, &rule);
    char* err = NULL;
    if (!ec_fwd_start(m, idx, &err)) {
        fprintf(stderr, "tunnel failed: %s\n", err ? err : "?");
        g_exit_code = 2;
        free(err);
    } else {
        printf("tunnel active on %s:%d -> %s:%d (Ctrl+C to stop)\n",
               rule.bind_host, rule.bind_port, rule.target_host, rule.target_port);
        for (;;) sleep(1);
    }
    ec_fwd_free(m);
    ec_ssh_free(ssh);
}

/* pipe: bridge stdin/stdout through proxy/jump to HOST:PORT (ProxyCommand) */
static void cmd_pipe(int argc, char** argv)
{
    const char* proxy_spec = NULL;
    const char* host = NULL;
    int port = 22;
    for (int i = 0; i < argc; i++) {
        if (strcmp(argv[i], "--proxy") == 0 && i + 1 < argc)
            proxy_spec = argv[++i];
        else if (!host) host = argv[i];
        else port = atoi(argv[i]);
    }
    if (!host || !ec_valid_port(port)) {
        fprintf(stderr, "usage: pipe [--proxy SPEC] HOST PORT\n");
        g_exit_code = 2;
        return;
    }
    int fd;
    if (proxy_spec) {
        EcProxy px;
        if (!ec_proxy_parse(proxy_spec, &px)) {
            fprintf(stderr, "bad proxy spec: %s\n", proxy_spec);
            g_exit_code = 2;
            return;
        }
        char* err = NULL;
        fd = ec_proxy_dial(&px, host, port, 15000, &err);
        if (fd < 0) {
            fprintf(stderr, "proxy dial failed: %s\n", err ? err : "?");
            free(err);
            g_exit_code = 2;
            return;
        }
    } else {
        char port_s[16];
        snprintf(port_s, sizeof port_s, "%d", port);
        GError* ge = NULL;
        GSocketClient* c = g_socket_client_new();
        GSocketConnection* conn = g_socket_client_connect_to_host(c, host, (guint16)port, NULL, &ge);
        g_object_unref(c);
        if (!conn) {
            fprintf(stderr, "dial failed: %s\n", ge ? ge->message : "?");
            g_clear_error(&ge);
            g_exit_code = 2;
            return;
        }
        fd = dup(g_socket_get_fd(g_socket_connection_get_socket(conn)));
        g_object_unref(conn);
        if (fd < 0) { g_exit_code = 2; return; }
    }
    /* bridge stdin/stdout <-> fd until EOF */
    for (;;) {
        fd_set rf;
        FD_ZERO(&rf);
        FD_SET(0, &rf);
        FD_SET(fd, &rf);
        if (select(fd + 1, &rf, NULL, NULL, NULL) <= 0) break;
        char buf[8192];
        ssize_t n;
        if (FD_ISSET(0, &rf)) {
            n = read(0, buf, sizeof buf);
            if (n <= 0) break;
            ssize_t off = 0;
            while (off < n) {
                ssize_t w = write(fd, buf + off, (size_t)(n - off));
                if (w <= 0) goto out;
                off += w;
            }
        }
        if (FD_ISSET(fd, &rf)) {
            n = read(fd, buf, sizeof buf);
            if (n <= 0) break;
            fwrite(buf, 1, (size_t)n, stdout);
            fflush(stdout);
        }
    }
out:
    close(fd);
}

int main(int argc, char** argv)
{
    if (argc < 2) {
        fprintf(stderr,
            "eclipse-ssh-cli - Eclipse SSH companion\n"
            "usage:\n"
            "  eclipse-ssh-cli list\n"
            "  eclipse-ssh-cli exec <profile> <command>\n"
            "  eclipse-ssh-cli connect <profile>\n"
            "  eclipse-ssh-cli upload <local> <profile:remote>\n"
            "  eclipse-ssh-cli download <profile:remote> <local>\n"
            "  eclipse-ssh-cli tunnel <profile> --local [bind:]port:host:port\n"
            "  eclipse-ssh-cli pipe [--proxy SPEC] HOST PORT\n");
        return 2;
    }
    const char* cmd = argv[1];
    if (strcmp(cmd, "pipe") == 0) {
        cmd_pipe(argc - 2, argv + 2);
        return g_exit_code;
    }
    EcSessionStore sessions;
    EcVault* vault = NULL;
    EcHostKeyStore* hk = NULL;
    char config_dir[512];
    files_init(&sessions, &vault, &hk, config_dir, sizeof config_dir);

    if (strcmp(cmd, "list") == 0) {
        cmd_list(&sessions);
    } else if (strcmp(cmd, "exec") == 0 && argc >= 4) {
        EcSession* s = ec_sessions_find_name(&sessions, argv[2]);
        if (!s) { fprintf(stderr, "no profile '%s'\n", argv[2]); g_exit_code = 2; }
        else cmd_exec(s, vault, hk, argv[3]);
    } else if (strcmp(cmd, "connect") == 0 && argc >= 3) {
        EcSession* s = ec_sessions_find_name(&sessions, argv[2]);
        if (!s) { fprintf(stderr, "no profile '%s'\n", argv[2]); g_exit_code = 2; }
        else cmd_interactive(s, vault, hk);
    } else if (strcmp(cmd, "upload") == 0 && argc >= 4) {
        char* spec = ec_strdup(argv[3]);
        char* colon = strchr(spec, ':');
        if (colon) *colon = 0;
        EcSession* s = ec_sessions_find_name(&sessions, spec);
        if (!s || !colon) { fprintf(stderr, "usage: upload <local> <profile:remote>\n"); g_exit_code = 2; }
        else cmd_upload(s, vault, hk, argv[2], colon + 1);
        free(spec);
    } else if (strcmp(cmd, "download") == 0 && argc >= 4) {
        char* spec = ec_strdup(argv[2]);
        char* colon = strchr(spec, ':');
        if (colon) *colon = 0;
        EcSession* s = ec_sessions_find_name(&sessions, spec);
        if (!s || !colon) { fprintf(stderr, "usage: download <profile:remote> <local>\n"); g_exit_code = 2; }
        else cmd_download(s, vault, hk, colon + 1, argv[3]);
        free(spec);
    } else if (strcmp(cmd, "tunnel") == 0 && argc >= 4) {
        EcSession* s = ec_sessions_find_name(&sessions, argv[2]);
        if (!s) { fprintf(stderr, "no profile '%s'\n", argv[2]); g_exit_code = 2; }
        else cmd_tunnel(s, vault, hk, argv[3]);
    } else {
        fprintf(stderr, "unknown command '%s'\n", cmd);
        g_exit_code = 2;
    }
    ec_sessions_free(&sessions);
    ec_vault_free(vault);
    ec_hostkeys_free(hk);
    return g_exit_code;
}
