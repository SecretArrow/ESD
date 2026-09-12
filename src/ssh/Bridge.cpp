#include "Bridge.h"

#include <QThread>

#ifdef Q_OS_WIN
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#else
#include <arpa/inet.h>
#include <netinet/in.h>
#include <sys/socket.h>
#include <unistd.h>
#endif

namespace eclipse {

std::pair<int, int> makeSocketPair()
{
#ifdef Q_OS_WIN
    SOCKET listener = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (listener == INVALID_SOCKET)
        return { -1, -1 };
    sockaddr_in addr {};
    addr.sin_family = AF_INET;
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    addr.sin_port = 0;
    if (::bind(listener, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::closesocket(listener);
        return { -1, -1 };
    }
    socklen_t alen = sizeof(addr);
    ::getsockname(listener, reinterpret_cast<sockaddr*>(&addr), &alen);
    if (::listen(listener, 1) != 0) {
        ::closesocket(listener);
        return { -1, -1 };
    }
    SOCKET a = ::socket(AF_INET, SOCK_STREAM, IPPROTO_TCP);
    if (a == INVALID_SOCKET || ::connect(a, reinterpret_cast<sockaddr*>(&addr), sizeof(addr)) != 0) {
        ::closesocket(listener);
        if (a != INVALID_SOCKET)
            ::closesocket(a);
        return { -1, -1 };
    }
    SOCKET b = ::accept(listener, nullptr, nullptr);
    ::closesocket(listener);
    if (b == INVALID_SOCKET) {
        ::closesocket(a);
        return { -1, -1 };
    }
    return { int(a), int(b) };
#else
    int sv[2];
    if (::socketpair(AF_UNIX, SOCK_STREAM, 0, sv) != 0)
        return { -1, -1 };
    return { sv[0], sv[1] };
#endif
}

void closeFd(int fd)
{
    if (fd < 0)
        return;
#ifdef Q_OS_WIN
    ::closesocket(SOCKET(fd));
#else
    ::close(fd);
#endif
}

Bridge::Bridge(int fdA, ReadFn readFromChannel, WriteFn writeToChannel)
    : m_fd(fdA)
    , m_readFromChannel(std::move(readFromChannel))
    , m_writeToChannel(std::move(writeToChannel))
{
    m_threadFdToChannel = std::thread([this] { pumpFdToChannel(); });
    m_threadChannelToFd = std::thread([this] { pumpChannelToFd(); });
}

Bridge::~Bridge()
{
    m_stop = true;
    closeFd(m_fd);
    m_fd = -1;
    if (m_threadFdToChannel.joinable())
        m_threadFdToChannel.join();
    if (m_threadChannelToFd.joinable())
        m_threadChannelToFd.join();
}

void Bridge::pumpFdToChannel()
{
    char buf[32 * 1024];
    while (!m_stop) {
        const int n = int(recv(m_fd, buf, sizeof(buf), 0));
        if (n <= 0)
            break;
        if (m_writeToChannel(buf, n) <= 0)
            break;
    }
    m_stop = true;
    closeFd(m_fd);
}

void Bridge::pumpChannelToFd()
{
    char buf[32 * 1024];
    while (!m_stop) {
        const int n = m_readFromChannel(buf, sizeof(buf));
        if (n < 0)
            break;
        if (n == 0) {
            QThread::msleep(2);
            continue;
        }
        if (send(m_fd, buf, n, 0) <= 0)
            break;
    }
    m_stop = true;
    closeFd(m_fd);
}

} // namespace eclipse
