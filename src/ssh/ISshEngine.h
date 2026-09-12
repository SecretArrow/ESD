#pragma once

#include <functional>
#include <memory>
#include <mutex>

#include <QByteArray>
#include <QString>

#include "Types.h"
#include "../common/Outcome.h"

namespace eclipse {

// ---------------------------------------------------------------------------
// ISshEngine - transport-agnostic SSH engine abstraction.
//
// Two concrete backends ship with Eclipse:
//   * LibsshEngine  (libssh, LGPL-2.1) - default
//   * Libssh2Engine (libssh2, BSD-3)   - alternative
//
// Threading contract: all non-const calls happen on the owning worker thread
// EXCEPT ISftpSession/IScpSession methods used by transfer threads, which are
// serialized through the engine-internal recursive mutex.
// ---------------------------------------------------------------------------

class IChannel
{
public:
    virtual ~IChannel() = default;
    virtual bool isOpen() const = 0;
    virtual bool isEof() const = 0;

    // Channel setup (call once after openChannel):
    virtual Outcome openShell(int cols, int rows, const QStringList& env) = 0;
    virtual Outcome openExecChannel(const QString& command) = 0;

    // Non-blocking reads; return -1 on error, 0 when nothing available.
    virtual int readStdout(char* buf, int len) = 0;
    virtual int readStderr(char* buf, int len) = 0;
    virtual int write(const char* buf, int len) = 0;

    virtual void resizePty(int cols, int rows) = 0;
    virtual void sendEof() = 0;
    virtual void close() = 0;
    virtual int exitStatus() = 0;
};

class ISftpSession
{
public:
    virtual ~ISftpSession() = default;
    virtual bool isValid() const = 0;

    virtual QString canonicalize(const QString& path) = 0;
    virtual QVector<SftpEntry> listDir(const QString& path, QString* err) = 0;
    virtual bool stat(const QString& path, SftpAttrs* out, bool followLinks = true) = 0;
    virtual bool mkdir(const QString& path, quint32 mode, QString* err) = 0;
    virtual bool rmdir(const QString& path, QString* err) = 0;
    virtual bool unlink(const QString& path, QString* err) = 0;
    virtual bool rename(const QString& from, const QString& to, QString* err) = 0;
    virtual bool setPermissions(const QString& path, quint32 mode, QString* err) = 0;
    virtual bool setOwnership(const QString& path, quint32 uid, quint32 gid, QString* err) = 0;
    virtual bool createSymlink(const QString& target, const QString& linkPath, QString* err) = 0;
    virtual QString readLink(const QString& path) = 0;

    // Streaming file I/O (used by transfer manager).
    // Returns opaque handle, 0 on failure.
    using FileHandle = void*;
    virtual FileHandle openForRead(const QString& path, quint64* sizeOut, QString* err) = 0;
    virtual FileHandle openForWrite(const QString& path, quint64 sizeHint, bool append, quint32 mode, QString* err) = 0;
    virtual int readFile(FileHandle, char* buf, int len, QString* err) = 0;
    virtual int writeFile(FileHandle, const char* buf, int len, QString* err) = 0;
    virtual bool seekFile(FileHandle, quint64 offset) = 0;
    virtual void closeFile(FileHandle) = 0;

    // Truncation for resume-from-zero restarts.
    virtual bool truncate(const QString& path, QString* err) = 0;
};

class IScpSession
{
public:
    virtual ~IScpSession() = default;
    using ProgressFn = std::function<bool(quint64 transferred, quint64 total)>;

    virtual Outcome sendTo(const QString& localPath, const QString& remotePath, bool recursive, ProgressFn progress) = 0;
    virtual Outcome recvFrom(const QString& remotePath, const QString& localPath, bool recursive, ProgressFn progress) = 0;
};

class ISshEngine
{
public:
    virtual ~ISshEngine() = default;

    virtual QString backendName() const = 0;
    virtual QString backendVersion() const = 0;

    // TCP connect + banner exchange + key exchange (handshake).
    // hostKey callback (if set) is invoked once the server key is known,
    // BEFORE authentication, so the caller can verify/ask the user.
    virtual Outcome connect(const QString& host, int port, int timeoutMs,
                            const std::function<void(const HostKeyInfo&)>& hostKeyCb = {}) = 0;

    // Connect over an ALREADY-OPENED socket fd (used for proxy & jump chains).
    virtual Outcome connectOverFd(int fd, const QString& hostForLog, int port,
                                  const std::function<void(const HostKeyInfo&)>& hostKeyCb = {}) = 0;

    virtual HostKeyInfo serverHostKey() const = 0;
    virtual QString serverBanner() const = 0;

    virtual Outcome authenticate(const AuthParams& params,
                                 // For keyboard-interactive / password-after-kbdint:
                                 const std::function<QStringList(const QStringList& prompts, const QVector<bool>& echoes)>& promptCb = {}) = 0;

    virtual bool isConnected() const = 0;
    virtual void disconnect() = 0;

    virtual std::unique_ptr<IChannel> openChannel(QString* err) = 0;
    // Channel pre-connected to destHost:destPort via direct-tcpip (forwards/jump).
    virtual std::unique_ptr<IChannel> openForwardChannel(const QString& destHost, int destPort, QString* err) = 0;
    virtual std::unique_ptr<ISftpSession> openSftp(QString* err) = 0;
    virtual std::unique_ptr<IScpSession> openScp(QString* err) = 0;

    // Remote (reverse) forwarding. Returns the actually-bound port via out param.
    virtual bool remoteForwardListen(const QString& bindAddress, int port, int* boundPort, QString* err) = 0;
    virtual std::unique_ptr<IChannel> acceptRemoteForward(int timeoutMs, QString* err) = 0;
    virtual void remoteForwardCancel(const QString& bindAddress, int port) = 0;

    // Public key authentication via SSH agent.
    virtual Outcome authenticateWithAgent(const QString& username) = 0;

    // Keepalive: send a transport-level ignore packet; returns RTT ms or -1.
    virtual int sendKeepAlive() = 0;

    virtual EngineInfo negotiatedInfo() const = 0;

    // Serializes all transport access (jump bridges pump from other threads).
    virtual std::recursive_mutex& rawMutex() = 0;

    // Sets environment variables for future channels (best effort).
    virtual void setPreferredAuthOrder(bool agentFirst) { Q_UNUSED(agentFirst); }
};

// Factory
std::unique_ptr<ISshEngine> createEngine(SshEngineKind kind);
QString libsshVersionString();
QString libssh2VersionString();

} // namespace eclipse
