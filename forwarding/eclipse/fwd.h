/* Eclipse SSH - port forwarding manager: local (-L), remote (-R),
 * dynamic SOCKS5 (-D). Real tunnels over libssh channels; listeners run on
 * dedicated threads; each accepted connection is pumped on its own thread.
 */
#ifndef ECLIPSE_FWD_H
#define ECLIPSE_FWD_H

#include "eclipse/core.h"
#include "eclipse/ssh.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    EC_FWD_LOCAL = 0,    /* -L bind:port -> target_host:target_port via SSH */
    EC_FWD_REMOTE,       /* -R listen on SSH server -> connect back to local host:port */
    EC_FWD_DYNAMIC       /* -D SOCKS5 proxy on bind:port */
} EcFwdKind;

typedef struct {
    EcFwdKind kind;
    char bind_host[256];
    int bind_port;
    char target_host[256];
    int target_port;
    bool enabled;
    bool running;
    char last_error[256];
} EcFwdRule;

typedef struct EcFwdManager EcFwdManager;

EcFwdManager* ec_fwd_new(void);
void ec_fwd_free(EcFwdManager* m);
/* Attach the SSH session all rules tunnel through (may be swapped when reconnecting). */
void ec_fwd_set_session(EcFwdManager* m, EcSshSession* session);
/* Rule storage (works also while disconnected). */
int ec_fwd_add(EcFwdManager* m, const EcFwdRule* rule);      /* returns index */
bool ec_fwd_remove(EcFwdManager* m, int index);
bool ec_fwd_update(EcFwdManager* m, int index, const EcFwdRule* rule);
size_t ec_fwd_count(EcFwdManager* m);
const EcFwdRule* ec_fwd_get(EcFwdManager* m, int index);
/* Start/stop listeners of one rule. */
bool ec_fwd_start(EcFwdManager* m, int index, char** err);
bool ec_fwd_stop(EcFwdManager* m, int index);
void ec_fwd_stop_all(EcFwdManager* m);

/* Parse "8080:localhost:80" / "0.0.0.0:2222:10.0.0.5:22" */
bool ec_fwd_parse_spec(const char* spec, EcFwdKind kind, EcFwdRule* out);

#ifdef __cplusplus
}
#endif
#endif /* ECLIPSE_FWD_H */
