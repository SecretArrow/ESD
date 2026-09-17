/* Eclipse SSH - outbound proxy support (spec #22): SOCKS5, SOCKS4, HTTP
 * CONNECT. The GUI uses these via ProxyCommand-compatible workflow:
 *   <eclipse-ssh-cli> pipe --proxy socks5://... TARGET 22
 * which bridges stdin/stdout to the tunneled socket (libssh then speaks SSH
 * over it), matching OpenSSH ProxyCommand semantics without fd-ownership traps.
 */
#ifndef ECLIPSE_PROXY_H
#define ECLIPSE_PROXY_H

#include "eclipse/core.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum { EC_PROXY_NONE = 0, EC_PROXY_SOCKS5, EC_PROXY_SOCKS4, EC_PROXY_HTTP } EcProxyKind;

typedef struct {
    EcProxyKind kind;
    char host[256];
    int port;
    char user[128];
    char pass[128];
} EcProxy;

/* parse "socks5://user:pass@host:1080", "socks4://host:1080", "http://host:8080" */
bool ec_proxy_parse(const char* spec, EcProxy* out);

/* Dial dest_host:dest_port THROUGH proxy; blocks; returns connected socket fd
 * (caller owns, must close) or -1 with *err set. timeout_ms <= 0 => 15s. */
int ec_proxy_dial(const EcProxy* px, const char* dest_host, int dest_port,
                  int timeout_ms, char** err);

#ifdef __cplusplus
}
#endif
#endif /* ECLIPSE_PROXY_H */
