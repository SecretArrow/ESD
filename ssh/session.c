/* Eclipse SSH - session orchestration implementation.
 * The UI never blocks: connect/auth runs on a worker thread, the pty is read
 * by a pump thread, keepalive on its own. Events surface via callbacks that
 * the UI marshals to the GTK main loop with g_idle_add.
 */
#include "eclipse/session.h"
#include "eclipse/platform.h"

/* ---------------- hostkey consent trampoline ---------------- */
static bool hostkey_trampoline(const EcServerKey* key, EcHostKeyStatus st,
                               const char* host, int port, void* user)
{
    EcLiveSession* ls = user;
    if (!ls->cb.hostkey_prompt) return false;
    int answer = ls->cb.hostkey_prompt(ls, key, st, host, port, ls->cb.user);
    return answer == 1 || answer == 2; /* 2 == accept+store (engine stores) */
}

static gpointer connect_worker(gpointer data);
static gpointer pump_worker(gpointer data);
static gpointer keepalive_worker(gpointer data);

/* ---------------- public API ---------------- */
EcLiveSession* ec_live_new(const EcSession* profile, EcHostKeyStore* hostkeys)
{
    EcLiveSession* ls = calloc(1, sizeof(EcLiveSession));
    if (!ls) return NULL;
    ls->profile = *profile;
    ls->hostkeys = hostkeys;
    ls->ssh = ec_ssh_new();
    ls->fwd = ec_fwd_new();
    ls->state = EC_LV_IDLE;
    ls->auto_reconnect = true;
    ls->keepalive_interval = profile->keep_alive > 0 ? profile->keep_alive : 30;
    g_mutex_init(&ls->lock);
    ec_tq_init(&ls->transfer_queue);
    if (!ls->ssh || !ls->fwd) { ec_live_free(ls); return NULL; }
    return ls;
}

void ec_live_set_callbacks(EcLiveSession* ls, const EcLiveCallbacks* cbs) { ls->cb = *cbs; }

void ec_live_set_term(EcLiveSession* ls, EcTerm* term) { ls->term = term; }

void ec_live_set_auto_reconnect(EcLiveSession* ls, bool enabled)
{
    g_mutex_lock(&ls->lock);
    ls->auto_reconnect = enabled;
    g_mutex_unlock(&ls->lock);
}

void ec_live_set_password(EcLiveSession* ls, const char* password)
{
    g_mutex_lock(&ls->lock);
    if (ls->pending_password) {
        ec_secure_wipe(ls->pending_password, strlen(ls->pending_password));
        free(ls->pending_password);
        ls->pending_password = NULL;
    }
    ls->pending_password = password ? ec_strdup(password) : NULL;
    g_mutex_unlock(&ls->lock);
}

void ec_live_start(EcLiveSession* ls)
{
    g_mutex_lock(&ls->lock);
    if (ls->connect_thread || ls->closing) { g_mutex_unlock(&ls->lock); return; }
    ls->closing = false;
    g_mutex_unlock(&ls->lock);
    ls->connect_thread = g_thread_new("ec-connect", connect_worker, ls);
    if (ls->connect_thread) g_thread_unref(ls->connect_thread);
    else {
        ls->state = EC_LV_FAILED;
        snprintf(ls->status, sizeof ls->status, "Cannot start connect thread");
        ls->cb.state(ls, EC_LV_FAILED, ls->status, ls->cb.user);
    }
}

static void live_stop_threads(EcLiveSession* ls)
{
    g_mutex_lock(&ls->lock);
    ls->closing = true;
    ls->auto_reconnect = false;
    g_mutex_unlock(&ls->lock);
}

void ec_live_disconnect(EcLiveSession* ls)
{
    live_stop_threads(ls);
    ec_fwd_stop_all(ls->fwd);
    g_mutex_lock(&ls->lock);
    EcSshChannel* ch = ls->pty_channel;
    ls->pty_channel = NULL;
    g_mutex_unlock(&ls->lock);
    if (ch) ec_ssh_chan_free(ch);
    if (ls->ssh) ec_ssh_disconnect(ls->ssh);
    ls->state = EC_LV_CLOSED;
}

void ec_live_free(EcLiveSession* ls)
{
    if (!ls) return;
    live_stop_threads(ls);
    /* workers observe closing and exit on their own; wait bounded time */
    for (int waited = 0; waited < 2000; waited += 50) {
        g_mutex_lock(&ls->lock);
        bool busy = ls->connect_thread || ls->pump_thread || ls->keepalive_thread;
        g_mutex_unlock(&ls->lock);
        if (!busy) break;
        g_usleep(50000);
    }
    g_mutex_lock(&ls->lock);
    GThread *ct = ls->connect_thread, *pt = ls->pump_thread, *kt = ls->keepalive_thread;
    ls->connect_thread = ls->pump_thread = ls->keepalive_thread = NULL;
    g_mutex_unlock(&ls->lock);
    /* threads detached themselves; only clear refs here (they exit via closing flag) */
    (void)ct; (void)pt; (void)kt;
    if (ls->pty_channel) ec_ssh_chan_free(ls->pty_channel);
    ec_tq_free(&ls->transfer_queue);
    if (ls->sftp) ec_sftp_free(ls->sftp);
    if (ls->ssh) ec_ssh_free(ls->ssh);
    if (ls->fwd) ec_fwd_free(ls->fwd);
    if (ls->term) ec_term_free(ls->term);
    if (ls->pending_password) {
        ec_secure_wipe(ls->pending_password, strlen(ls->pending_password));
        free(ls->pending_password);
    }
    g_mutex_clear(&ls->lock);
    free(ls);
}

/* ---------------- connect worker ---------------- */
static gpointer connect_worker(gpointer data)
{
    EcLiveSession* ls = data;
    int attempt = 0;

    for (;;) {
        g_mutex_lock(&ls->lock);
        bool closing = ls->closing;
        g_mutex_unlock(&ls->lock);
        if (closing) break;

        if (attempt > 0) {
            g_mutex_lock(&ls->lock);
            bool reconnect = ls->auto_reconnect && !ls->closing;
            g_mutex_unlock(&ls->lock);
            if (!reconnect) break;
            ls->state = EC_LV_RECONNECTING;
            snprintf(ls->status, sizeof ls->status, "Reconnecting (attempt %d)...", attempt + 1);
            ls->cb.state(ls, EC_LV_RECONNECTING, ls->status, ls->cb.user);
            int backoff_s = attempt > 5 ? 5 : attempt;
            for (int w = 0; w < backoff_s * 5; w++) {
                g_mutex_lock(&ls->lock);
                bool stop = ls->closing;
                g_mutex_unlock(&ls->lock);
                if (stop) return NULL;
                g_usleep(200000);
            }
        }

        ls->state = attempt == 0 ? EC_LV_CONNECTING : EC_LV_RECONNECTING;
        snprintf(ls->status, sizeof ls->status, "Connecting to %.200s:%d...", ls->profile.host, ls->profile.port);
        ls->cb.state(ls, ls->state, ls->status, ls->cb.user);

        EcSshParams p = { 0 };
        p.host = ls->profile.host;
        p.port = ls->profile.port;
        p.username = ls->profile.username;
        p.timeout_ms = 15000;
        p.key_path = ls->profile.key_path[0] ? ls->profile.key_path : NULL;
        p.try_agent = ls->profile.auth_mode == EC_AUTH_AGENT || ls->profile.auth_mode == EC_AUTH_KEY_AGENT ||
                      ls->profile.use_agent_fallback;
        p.hostkeys = ls->hostkeys;
        p.use_compression = true;

        char* pass = NULL;
        g_mutex_lock(&ls->lock);
        pass = ls->pending_password ? ec_strdup(ls->pending_password) : NULL;
        g_mutex_unlock(&ls->lock);
        p.password = pass;

        char* err = NULL;
        bool ok = ec_ssh_connect(ls->ssh, &p, hostkey_trampoline, ls, &err);
        if (pass) { ec_secure_wipe(pass, strlen(pass)); free(pass); }
        free(err);

        if (ok) {
            ls->state = EC_LV_CONNECTED;
            snprintf(ls->status, sizeof ls->status, "Connected");
            attempt = 0;
            ls->cb.state(ls, EC_LV_CONNECTED, "Connected", ls->cb.user);

            ls->sftp = ec_sftp_open(ls->ssh, NULL);
            ec_fwd_set_session(ls->fwd, ls->ssh);

            /* open the pty if a terminal is attached */
            if (ls->term) {
                g_mutex_lock(&ls->lock);
                EcSshChannel* ch = ec_ssh_open_pty(ls->ssh, ls->term->cols, ls->term->rows,
                                                   "xterm-256color", NULL);
                ls->pty_channel = ch;
                ls->session_epoch++;
                bool start_pump = ch != NULL && !ls->pump_thread;
                g_mutex_unlock(&ls->lock);
                if (start_pump) {
                    ls->pump_thread = g_thread_new("ec-pump", pump_worker, ls);
                    if (ls->pump_thread) g_thread_unref(ls->pump_thread);
                }
            }
            if (ls->profile.keep_alive) {
                ls->keepalive_thread = g_thread_new("ec-keepalive", keepalive_worker, ls);
                if (ls->keepalive_thread) g_thread_unref(ls->keepalive_thread);
            }
            if (ls->profile.init_commands[0])
                ec_live_run_automation(ls);
            return NULL;
        }

        ls->state = EC_LV_FAILED;
        snprintf(ls->status, sizeof ls->status, "%s", err ? err : "Connect failed");
        g_mutex_lock(&ls->lock);
        bool retry = ls->auto_reconnect && !ls->closing;
        g_mutex_unlock(&ls->lock);
        ls->cb.state(ls, EC_LV_FAILED, ls->status, ls->cb.user);
        if (!retry) break;
        attempt++;
    }
    return NULL;
}

/* ---------------- pump worker ---------------- */
static gpointer pump_worker(gpointer data)
{
    EcLiveSession* ls = data;
    uint32_t epoch = ls->session_epoch;
    char buf[16384];
    for (;;) {
        g_mutex_lock(&ls->lock);
        bool closing = ls->closing;
        EcSshChannel* ch = ls->pty_channel;
        uint32_t cur_epoch = ls->session_epoch;
        g_mutex_unlock(&ls->lock);
        if (closing || !ch || cur_epoch != epoch) break;

        int n = ec_ssh_chan_read(ch, buf, sizeof buf, 0);
        if (n > 0) {
            if (ls->term) ec_term_input(ls->term, buf, (size_t)n);
            if (ls->cb.data) ls->cb.data(ls, buf, (size_t)n, ls->cb.user);
            continue;
        }
        if (n < 0 || ec_ssh_chan_eof(ch)) break;
        ec_ssh_wait_readable(ch, 15);
    }
    if (!ls->closing && ls->cb.eof)
        ls->cb.eof(ls, ls->cb.user);
    return NULL;
}

/* ---------------- keepalive worker ---------------- */
static gpointer keepalive_worker(gpointer data)
{
    EcLiveSession* ls = data;
    for (;;) {
        g_mutex_lock(&ls->lock);
        bool stop = ls->closing;
        int interval = ls->keepalive_interval;
        g_mutex_unlock(&ls->lock);
        if (stop) return NULL;
        if (ec_ssh_is_connected(ls->ssh))
            ec_ssh_send_keepalive(ls->ssh);
        for (int w = 0; w < interval * 5; w++) {
            g_mutex_lock(&ls->lock);
            bool s2 = ls->closing;
            g_mutex_unlock(&ls->lock);
            if (s2) return NULL;
            g_usleep(200000);
        }
    }
    return NULL;
}

/* ---------------- misc ---------------- */
void ec_live_send(EcLiveSession* ls, const char* data, size_t len)
{
    if (!ls || !data || !len) return;
    g_mutex_lock(&ls->lock);
    EcSshChannel* ch = ls->pty_channel;
    g_mutex_unlock(&ls->lock);
    if (ch) ec_ssh_chan_write(ch, data, (int)len);
}

void ec_live_resize(EcLiveSession* ls, int cols, int rows)
{
    if (!ls) return;
    g_mutex_lock(&ls->lock);
    EcSshChannel* ch = ls->pty_channel;
    g_mutex_unlock(&ls->lock);
    if (ch) ec_ssh_chan_resize(ch, cols, rows);
}

int ec_live_run_automation(EcLiveSession* ls)
{
    /* Commands are sent exactly as if typed so the user sees them (spec #23:
     * never execute dangerous sequences silently). */
    int sent = 0;
    const char* p = ls->profile.init_commands;
    while (p && *p) {
        const char* nl = strchr(p, '\n');
        size_t len = nl ? (size_t)(nl - p) : strlen(p);
        if (len) {
            ec_live_send(ls, p, len);
            ec_live_send(ls, "\r", 1);
            sent++;
            if (ls->profile.init_delay_ms > 0)
                g_usleep((gulong)ls->profile.init_delay_ms * 1000u);
        }
        p = nl ? nl + 1 : NULL;
    }
    return sent;
}

char* ec_live_exec_sync(EcLiveSession* ls, const char* command, int* exit_code)
{
    EcStr out;
    ec_str_init(&out);
    char* err = NULL;
    int rc = ec_ssh_run_command(ls->ssh, command, &out, &err);
    if (exit_code) *exit_code = rc;
    free(err);
    return ec_str_take(&out);
}
