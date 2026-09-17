/* Eclipse SSH - SSH engine on libssh. Blocking primitives; threading lives in
 * the session layer so the UI thread is never blocked (spec rule #9).
 * Security: host-key verification happens through EcHostKeyStore before auth;
 * there is no code path that skips verification (spec #31).
 */
#ifndef ECLIPSE_SSH_H
#define ECLIPSE_SSH_H

#include "eclipse/core.h"
#include "eclipse/security.h"
#include <libssh/libssh.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct EcSshSession EcSshSession;
typedef struct EcSshChannel EcSshChannel;

typedef struct {
    const char* host;
    int port;                 /* default 22 */
    int timeout_ms;           /* connect+banner timeout */
    const char* username;
    const char* password;     /* optional */
    const char* key_path;     /* optional private key */
    const char* key_passphrase;
    bool try_agent;           /* ssh-agent fallback */
    bool use_compression;
    EcHostKeyStore* hostkeys; /* borrowed; required */
} EcSshParams;

/* owned copies inside the session (never expose credentials via getters) */
typedef struct {
    char* host;
    char* username;
    char* password;
    char* key_path;
    char* key_passphrase;
    int port;
    int timeout_ms;
    bool try_agent;
    EcHostKeyStore* hostkeys;
} EcSshOwned;

typedef enum {
    EC_SSH_IDLE = 0,
    EC_SSH_CONNECTING,
    EC_SSH_HOSTKEY,
    EC_SSH_AUTHENTICATING,
    EC_SSH_CONNECTED,
    EC_SSH_CLOSED,
    EC_SSH_ERROR
} EcSshState;

/* Host key presented by server (filled during connect). */
typedef struct {
    char* type;        /* "ssh-ed25519", ... */
    char* blob_b64;    /* base64 public blob (known_hosts format) */
    char* fingerprint; /* SHA256:... */
} EcServerKey;

struct EcSshSession {
    ssh_session libssh;
    EcSshOwned owned;        /* owned copies */
    EcServerKey server_key;
    EcSshState state;
    char last_error[512];
    /* stats (spec #27) */
    uint64_t bytes_in;
    uint64_t bytes_out;
    int64_t connected_at_ms;
    int rtt_ms;              /* measured via keepalive replies */
};

struct EcSshChannel {
    ssh_channel ch;
    EcSshSession* session;
    bool eof_seen;
};

EcSshSession* ec_ssh_new(void);
void ec_ssh_free(EcSshSession* s);

/* Blocking connect + hostkey verify + auth. hostkey_accept_cb is invoked when
 * the key is unknown/changed: return true to accept (TOFU or user consent). */
typedef bool (*EcHostKeyAcceptCb)(const EcServerKey* key, EcHostKeyStatus status,
                                  const char* host, int port, void* user);
bool ec_ssh_connect(EcSshSession* s, const EcSshParams* p,
                    EcHostKeyAcceptCb hostkey_cb, void* cb_user, char** err);
bool ec_ssh_is_connected(const EcSshSession* s);
const char* ec_ssh_last_error(const EcSshSession* s);
void ec_ssh_disconnect(EcSshSession* s);
/* Send a global keepalive request; updates rtt estimate. */
void ec_ssh_send_keepalive(EcSshSession* s);

/* Channels */
EcSshChannel* ec_ssh_open_pty(EcSshSession* s, int cols, int rows, const char* term, char** err);
EcSshChannel* ec_ssh_open_exec(EcSshSession* s, const char* command, char** err);
void ec_ssh_chan_free(EcSshChannel* c);
/* read: >0 bytes, 0 = nothing now, <0 = eof/closed */
int ec_ssh_chan_read(EcSshChannel* c, char* buf, int len, int is_stderr);
int ec_ssh_chan_write(EcSshChannel* c, const char* buf, int len);
void ec_ssh_chan_resize(EcSshChannel* c, int cols, int rows);
bool ec_ssh_chan_eof(EcSshChannel* c);
int ec_ssh_chan_exit_status(EcSshChannel* c);
/* Wait until any of the session's channels has data or timeout_ms; returns true if data ready. */
bool ec_ssh_wait_readable(EcSshChannel* c, int timeout_ms);

/* exec helper: runs command, collects stdout, returns exit code (blocking) */
int ec_ssh_run_command(EcSshSession* s, const char* command, EcStr* out, char** err);

/* fingerprint helper shared with UI */
char* ec_ssh_key_fingerprint_b64(ssh_key key);

#ifdef __cplusplus
}
#endif
#endif /* ECLIPSE_SSH_H */
