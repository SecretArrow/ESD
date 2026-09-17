/* Eclipse SSH - session orchestration: connects/authenticates on a worker
 * thread (UI never blocks, spec rule #9), pumps the pty into callbacks,
 * runs keepalives and auto-reconnect. Marshalling to the GTK main loop is
 * done by the UI layer via g_idle_add from the event callbacks.
 */
#ifndef ECLIPSE_SESSION_H
#define ECLIPSE_SESSION_H

#include "eclipse/core.h"
#include "eclipse/ssh.h"
#include "eclipse/sftp.h"
#include "eclipse/fwd.h"
#include "eclipse/repos.h"
#include "eclipse/term.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct EcLiveSession EcLiveSession;

typedef enum {
    EC_LV_IDLE = 0,
    EC_LV_CONNECTING,
    EC_LV_AUTHENTICATING,
    EC_LV_CONNECTED,
    EC_LV_RECONNECTING,
    EC_LV_FAILED,
    EC_LV_CLOSED
} EcLiveState;

/* Events arrive on worker threads; UI must marshal to main loop. */
typedef struct {
    void* user;
    void (*state)(EcLiveSession* ls, EcLiveState st, const char* message, void* user);
    void (*data)(EcLiveSession* ls, const char* buf, size_t len, void* user);
    void (*eof)(EcLiveSession* ls, void* user);
    /* hostkey consent: return 1=accept temporarily, 2=accept+store, 0=reject */
    int (*hostkey_prompt)(EcLiveSession* ls, const EcServerKey* key, EcHostKeyStatus st,
                          const char* host, int port, void* user);
} EcLiveCallbacks;

struct EcLiveSession {
    EcSession profile;          /* copy */
    EcSshSession* ssh;
    EcSftp* sftp;
    EcFwdManager* fwd;
    EcTerm* term;               /* borrowed; before start */
    EcHostKeyStore* hostkeys;   /* borrowed global store */
    EcLiveCallbacks cb;
    EcLiveState state;
    char status[256];
    EcSshChannel* pty_channel;  /* guarded by lock */
    EcTransferQueue transfer_queue; /* owned; per-session SFTP queue */
    uint32_t session_epoch;     /* guards stale pumps across reconnects */
    bool auto_reconnect;
    int keepalive_interval;
    char* pending_password;     /* injected from vault; wiped after use */
    /* worker control */
    GMutex lock;
    GThread* connect_thread;
    GThread* pump_thread;
    GThread* keepalive_thread;
    volatile bool closing;
};

EcLiveSession* ec_live_new(const EcSession* profile, EcHostKeyStore* hostkeys);
void ec_live_set_callbacks(EcLiveSession* ls, const EcLiveCallbacks* cbs);
void ec_live_set_term(EcLiveSession* ls, EcTerm* term); /* borrowed; before start */
void ec_live_set_auto_reconnect(EcLiveSession* ls, bool enabled);
void ec_live_set_password(EcLiveSession* ls, const char* password);
/* Start async connect. Returns immediately. */
void ec_live_start(EcLiveSession* ls);
void ec_live_disconnect(EcLiveSession* ls);
void ec_live_free(EcLiveSession* ls);
/* Send user bytes to the pty (safe from UI thread). */
void ec_live_send(EcLiveSession* ls, const char* data, size_t len);
void ec_live_resize(EcLiveSession* ls, int cols, int rows);
/* Run the profile's init automation sequence (worker thread). Returns count sent. */
int ec_live_run_automation(EcLiveSession* ls);
/* Exec a single command on this session (blocks; call from worker contexts). */
char* ec_live_exec_sync(EcLiveSession* ls, const char* command, int* exit_code);

#ifdef __cplusplus
}
#endif
#endif /* ECLIPSE_SESSION_H */
