#pragma once

#include "../common/Outcome.h"

class QTcpSocket;

namespace eclipse {

// ---------------------------------------------------------------------------
// ProxyDialer - raw TCP dialing through SOCKS4/5 and HTTP CONNECT proxies.
// Returns a plain connected socket fd which can then be handed to an SSH
// engine (connectOverFd).
// ---------------------------------------------------------------------------

struct ProxyConfig
{
    QString type;        // "none" | "socks4" | "socks5" | "http"
    QString host;
    int port = 0;
    QString user;
    QString password;
};

// Blocking; safe to call from worker threads only.
Outcome dial(const QString& host, int port, const ProxyConfig& proxy, int timeoutMs, int* fdOut);

} // namespace eclipse
