#pragma once

#include <atomic>
#include <functional>
#include <thread>

namespace eclipse {

// ---------------------------------------------------------------------------
// Bridge - connects two arbitrary duplex streams via background pump threads.
// Used by jump-host chains: the target engine gets one end of a socket pair,
// while pump threads shuttle bytes between the other end and a forwarded
// channel on the bastion.
// ---------------------------------------------------------------------------

// Creates a connected bidirectional fd pair (AF_UNIX socketpair on POSIX,
// loopback TCP pair on Windows). Returns {-1,-1} on failure.
std::pair<int, int> makeSocketPair();

void closeFd(int fd);

// Pipes bytes from `fd` into writeFn and from readFn into `fd` until EOF,
// error, or stop. Runs two threads. Returns immediately; joins on destruction.
class Bridge
{
public:
    using ReadFn = std::function<int(char* buf, int len)>;
    using WriteFn = std::function<int(const char* buf, int len)>;

    Bridge(int fdA, ReadFn readFromChannel, WriteFn writeToChannel);
    ~Bridge();

private:
    void pumpFdToChannel();
    void pumpChannelToFd();

    int m_fd;
    ReadFn m_readFromChannel;
    WriteFn m_writeToChannel;
    std::atomic<bool> m_stop{ false };
    std::thread m_threadFdToChannel;
    std::thread m_threadChannelToFd;
};

} // namespace eclipse
