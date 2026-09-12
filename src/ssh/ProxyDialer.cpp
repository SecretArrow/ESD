#include "ProxyDialer.h"

#include <QDateTime>
#include <QHostAddress>
#include <QHostInfo>

#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <fcntl.h>
#include <poll.h>
#include <netdb.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <unistd.h>
#endif

#include "../core/logging/Logger.h"
#include "Bridge.h"

namespace eclipse {

namespace {

bool sendAll(int fd, const char* data, int len)
{
    int sent = 0;
    while (sent < len) {
        const int n = int(send(fd, data + sent, len - sent, 0));
        if (n <= 0)
            return false;
        sent += n;
    }
    return true;
}

// Reads exactly len bytes or gives up after timeoutMs total.
bool recvAll(int fd, char* out, int len, int timeoutMs)
{
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
    int got = 0;
    while (got < len) {
        if (QDateTime::currentMSecsSinceEpoch() > deadline)
            return false;
        const int n = int(recv(fd, out + got, len - got, 0));
        if (n <= 0)
            return false;
        got += n;
    }
    return true;
}

quint32 resolveV4(const QString& host, bool* ok)
{
    *ok = false;
    const QHostAddress direct(host);
    if (!direct.isNull() && direct.protocol() == QAbstractSocket::IPv4Protocol) {
        *ok = true;
        return direct.toIPv4Address();
    }
    const QHostInfo info = QHostInfo::fromName(host);
    if (info.error() != QHostInfo::NoError || info.addresses().isEmpty())
        return 0;
    for (const QHostAddress& a : info.addresses()) {
        if (a.protocol() == QAbstractSocket::IPv4Protocol) {
            *ok = true;
            return a.toIPv4Address();
        }
    }
    return 0;
}

Outcome proxyHandshake(int fd, const QString& host, quint16 port, const ProxyConfig& proxy, int timeoutMs)
{
    if (proxy.type == QLatin1String("http")) {
        // HTTP CONNECT
        QString req = QStringLiteral("CONNECT %1:%2 HTTP/1.1\r\nHost: %1:%2\r\n").arg(host).arg(port);
        if (!proxy.user.isEmpty()) {
            const QByteArray cred = QByteArray(proxy.user.toUtf8() + QByteArrayLiteral(":")
                                               + proxy.password.toUtf8()).toBase64();
            req += QStringLiteral("Proxy-Authorization: Basic %1\r\n").arg(QString::fromLatin1(cred));
        }
        req += QStringLiteral("\r\n");
        if (!sendAll(fd, req.toUtf8().constData(), int(req.toUtf8().size())))
            return Outcome::fail(QStringLiteral("Could not send the CONNECT request to the proxy."),
                                 QStringLiteral("send failed"));
        char buf[1024] = {};
        // Read until end of headers
        QByteArray response;
        const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + timeoutMs;
        while (!response.contains(QByteArrayLiteral("\r\n\r\n"))) {
            if (QDateTime::currentMSecsSinceEpoch() > deadline)
                return Outcome::fail(QStringLiteral("The proxy did not respond in time."));
            const int n = int(recv(fd, buf, sizeof(buf) - 1, 0));
            if (n <= 0)
                return Outcome::fail(QStringLiteral("The proxy closed the connection."));
            response.append(buf, n);
        }
        if (!response.startsWith(QByteArrayLiteral("HTTP/1."))
            || response.mid(9, 3) != QByteArrayLiteral("200")) {
            const int sp = response.indexOf(' ');
            const QString code = response.mid(sp + 1, 3);
            return Outcome::fail(QStringLiteral("The proxy refused the connection (HTTP %1).").arg(code),
                                 QString::fromLatin1(response.left(120)));
        }
        return Outcome::success();
    }

    if (proxy.type == QLatin1String("socks5")) {
        // Greeting: ver=5, methods; offer no-auth and user/pass
        const unsigned char greet[] = { 0x05, 0x02, 0x00, 0x02 };
        if (!sendAll(fd, reinterpret_cast<const char*>(greet), 4))
            return Outcome::fail(QStringLiteral("Could not greet the SOCKS5 proxy."));
        unsigned char resp[2] = {};
        if (!recvAll(fd, reinterpret_cast<char*>(resp), 2, timeoutMs))
            return Outcome::fail(QStringLiteral("The SOCKS5 proxy did not respond."));
        if (resp[0] != 0x05)
            return Outcome::fail(QStringLiteral("The proxy does not speak SOCKS5."), QStringLiteral("ver=%1").arg(resp[0]));
        if (resp[1] == 0x02) {
            // username/password subnegotiation
            const QByteArray u = proxy.user.toUtf8();
            const QByteArray p = proxy.password.toUtf8();
            QByteArray auth;
            auth.append(char(0x01));
            auth.append(char(u.size()));
            auth.append(u);
            auth.append(char(p.size()));
            auth.append(p);
            if (!sendAll(fd, auth.constData(), int(auth.size())))
                return Outcome::fail(QStringLiteral("Could not send proxy credentials."));
            unsigned char aresp[2] = {};
            if (!recvAll(fd, reinterpret_cast<char*>(aresp), 2, timeoutMs) || aresp[1] != 0x00)
                return Outcome::fail(QStringLiteral("The proxy rejected the username or password."));
        } else if (resp[1] != 0x00) {
            return Outcome::fail(QStringLiteral("The proxy requires an unsupported authentication method."),
                                 QStringLiteral("method=%1").arg(resp[1]));
        }
        // CONNECT request: try hostname first (ATYP=3), fallback not needed
        QByteArray req;
        req.append(char(0x05));
        req.append(char(0x01)); // CONNECT
        req.append(char(0x00)); // reserved
        const QByteArray h = host.toUtf8();
        req.append(char(0x03));
        req.append(char(h.size()));
        req.append(h);
        req.append(char((port >> 8) & 0xFF));
        req.append(char(port & 0xFF));
        if (!sendAll(fd, req.constData(), int(req.size())))
            return Outcome::fail(QStringLiteral("Could not send the CONNECT request."));
        unsigned char head[4] = {};
        if (!recvAll(fd, reinterpret_cast<char*>(head), 4, timeoutMs))
            return Outcome::fail(QStringLiteral("The SOCKS5 proxy did not respond to CONNECT."));
        if (head[1] != 0x00) {
            const char* reason = head[1] == 0x01 ? "general failure"
                               : head[1] == 0x02 ? "connection not allowed"
                               : head[1] == 0x03 ? "network unreachable"
                               : head[1] == 0x04 ? "host unreachable"
                               : head[1] == 0x05 ? "connection refused"
                               : head[1] == 0x06 ? "TTL expired"
                               : "command not supported";
            return Outcome::fail(QStringLiteral("The proxy could not reach the destination (%1).").arg(reason),
                                 QStringLiteral("SOCKS5 reply=%1").arg(head[1]));
        }
        // Skip bound address
        if (head[3] == 0x01) {
            char skip[6];
            recvAll(fd, skip, 6, timeoutMs);
        } else if (head[3] == 0x03) {
            unsigned char l = 0;
            recvAll(fd, reinterpret_cast<char*>(&l), 1, timeoutMs);
            char skip[256];
            recvAll(fd, skip, l, timeoutMs);
        } else if (head[3] == 0x04) {
            char skip[18];
            recvAll(fd, skip, 18, timeoutMs);
        }
        return Outcome::success();
    }

    if (proxy.type == QLatin1String("socks4")) {
        bool ok = false;
        const quint32 ip = resolveV4(host, &ok);
        if (!ok)
            return Outcome::fail(QStringLiteral("SOCKS4 requires an IP address or resolvable host name."));
        QByteArray req;
        req.append(char(0x04));
        req.append(char(0x01));
        req.append(char((port >> 8) & 0xFF));
        req.append(char(port & 0xFF));
        const quint32 ipn = htonl(ip);
        req.append(reinterpret_cast<const char*>(&ipn), 4);
        req.append(proxy.user.toUtf8());
        req.append(char(0x00));
        if (!sendAll(fd, req.constData(), int(req.size())))
            return Outcome::fail(QStringLiteral("Could not send the SOCKS4 request."));
        unsigned char resp[8] = {};
        if (!recvAll(fd, reinterpret_cast<char*>(resp), 8, timeoutMs))
            return Outcome::fail(QStringLiteral("The SOCKS4 proxy did not respond."));
        if (resp[1] != 0x5A)
            return Outcome::fail(QStringLiteral("The SOCKS4 proxy refused the connection."),
                                 QStringLiteral("reply=%1").arg(resp[1]));
        return Outcome::success();
    }

    return Outcome::fail(QStringLiteral("Unsupported proxy type: %1").arg(proxy.type));
}

} // namespace

Outcome dial(const QString& host, int port, const ProxyConfig& proxy, int timeoutMs, int* fdOut)
{
#ifdef _WIN32
    WSADATA wsa;
    WSAStartup(MAKEWORD(2, 2), &wsa);
#endif

    if (fdOut)
        *fdOut = -1;

    // 1. TCP connect to the proxy (or straight to the host when no proxy).
    const QString connectHost = (proxy.type == QLatin1String("none") || proxy.type.isEmpty()) ? host : proxy.host;
    const int connectPort = (proxy.type == QLatin1String("none") || proxy.type.isEmpty()) ? port : proxy.port;

    addrinfo hints {};
    hints.ai_family = AF_INET;
    hints.ai_socktype = SOCK_STREAM;
    addrinfo* result = nullptr;
    const int rc = getaddrinfo(connectHost.toUtf8().constData(),
                               QByteArray::number(connectPort).constData(), &hints, &result);
    if (rc != 0 || !result)
        return Outcome::fail(QStringLiteral("Could not resolve %1.").arg(connectHost),
#ifdef _WIN32
                             // gai_strerror maps to the wchar_t variant when UNICODE is
                             // defined (Qt6 builds); use the ANSI variant explicitly.
                             QStringLiteral("getaddrinfo: %1").arg(QString::fromLatin1(gai_strerrorA(rc))),
#else
                             QStringLiteral("getaddrinfo: %1").arg(gai_strerror(rc)),
#endif
                             QStringLiteral("Check the host name and your DNS settings."));

    int fd = -1;
    Outcome out = Outcome::success();
    for (addrinfo* ai = result; ai; ai = ai->ai_next) {
        fd = int(::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol));
        if (fd < 0)
            continue;
        // non-blocking connect with poll
#ifdef _WIN32
        u_long mode = 1;
        ioctlsocket(SOCKET(fd), FIONBIO, &mode);
#else
        const int flags = fcntl(fd, F_GETFL, 0);
        fcntl(fd, F_SETFL, flags | O_NONBLOCK);
#endif
        const int crc = ::connect(fd, ai->ai_addr, int(ai->ai_addrlen));
        bool connected = crc == 0;
        if (!connected) {
#ifdef _WIN32
            const int err = WSAGetLastError();
            connected = err == WSAEWOULDBLOCK;
#else
            connected = errno == EINPROGRESS;
#endif
            if (connected) {
#ifdef _WIN32
                // pollfd::fd is SOCKET (unsigned) on Windows; a plain int is a
                // narrowing conversion (hard error in brace-init).
                pollfd p { SOCKET(fd), POLLOUT, 0 };
#else
                pollfd p { fd, POLLOUT, 0 };
#endif
#ifdef _WIN32
                const int prc = WSAPoll(&p, 1, timeoutMs);
#else
                const int prc = ::poll(&p, 1, timeoutMs);
#endif
                int soerr = 0;
                socklen_t slen = sizeof(soerr);
                getsockopt(fd, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soerr), &slen);
                connected = prc > 0 && soerr == 0;
            }
        }
        if (connected)
            break;
        closeFd(fd);
        fd = -1;
    }
    freeaddrinfo(result);

    if (fd < 0) {
        return Outcome::fail(QStringLiteral("TCP connection to %1:%2 failed.").arg(connectHost).arg(connectPort),
                             QStringLiteral("connect() timed out after %1 ms").arg(timeoutMs),
                             QStringLiteral("Check the address, port, VPN and firewall."));
    }

    // 2. Proxy handshake when needed.
    if (proxy.type != QLatin1String("none") && !proxy.type.isEmpty()) {
        out = proxyHandshake(fd, host, quint16(port), proxy, timeoutMs);
        if (!out.ok) {
            closeFd(fd);
            return out;
        }
    }

    if (fdOut)
        *fdOut = fd;
    return out;
}

} // namespace eclipse
