#pragma once

// ---------------------------------------------------------------------------
// Libssh2Engine - libssh2 (BSD-3) backend for Eclipse SSH Desktop.
//
// Design notes:
//  * The engine owns a Libssh2SessionState (shared with all channels / SFTP /
//    SCP session objects) which carries the LIBSSH2_SESSION, the socket fd and
//    a std::recursive_mutex guarding every libssh2 call.
//  * The session is kept in NON-BLOCKING mode at all times (blocking mode is
//    global per session and would stall the transfer threads). Every
//    blocking-looking operation is implemented as an EAGAIN retry loop with a
//    deadline (see retryEagain() in Libssh2Engine.cpp).
//  * ISftpSession/IScpSession methods may be called from transfer threads;
//    they lock the state mutex per call (chunk-level granularity).
//  * Host key verification (known_hosts) is performed by the CALLER (SshWorker)
//    - this backend only reports the server key through the hostKey callback.
//
// No Q_OBJECT here: backend classes are plain C++ (moc-free).
// ---------------------------------------------------------------------------

#include <memory>
#include <mutex>

#include <QMap>
#include <QString>

#include <libssh2.h>
#include <libssh2_sftp.h>

#include "../../ISshEngine.h"

namespace eclipse {

// Shared between engine and all session-derived objects ---------------------
struct Libssh2SessionState;
using Libssh2StatePtr = std::shared_ptr<Libssh2SessionState>;

// ---------------------------------------------------------------------------
// Libssh2Channel - IChannel over a LIBSSH2_CHANNEL.
// ---------------------------------------------------------------------------
class Libssh2Channel final : public IChannel
{
public:
    Libssh2Channel(Libssh2StatePtr state, LIBSSH2_CHANNEL* channel, ChannelKind kind);
    ~Libssh2Channel() override;

    bool isOpen() const override;
    bool isEof() const override;

    Outcome openShell(int cols, int rows, const QStringList& env) override;
    Outcome openExecChannel(const QString& command) override;

    // Non-blocking reads; return -1 on error, 0 when nothing available.
    int readStdout(char* buf, int len) override;
    int readStderr(char* buf, int len) override;
    int write(const char* buf, int len) override;

    void resizePty(int cols, int rows) override;
    void sendEof() override;
    void close() override;
    int exitStatus() override;

    // Backend-specific session setup, invoked by Libssh2Engine's open*Channel
    // helpers right after the channel is created (startShell OR startExec).
    bool startShell(int cols, int rows, const QMap<QString, QString>& env, QString* err);
    bool startExec(const QString& command, QString* err);

private:
    friend class Libssh2Engine;

    Libssh2StatePtr m_state;
    LIBSSH2_CHANNEL* m_channel = nullptr;
    ChannelKind m_kind = ChannelKind::Shell;
    bool m_closed = false;
    int m_cachedExitStatus = -1;
};

// ---------------------------------------------------------------------------
// Libssh2Sftp - ISftpSession over libssh2's SFTP subsystem.
// Every method locks the shared session state mutex per call.
// ---------------------------------------------------------------------------
class Libssh2Sftp final : public ISftpSession
{
public:
    Libssh2Sftp(Libssh2StatePtr state, LIBSSH2_SFTP* sftp);
    ~Libssh2Sftp() override;

    bool isValid() const override;

    QString canonicalize(const QString& path) override;
    QVector<SftpEntry> listDir(const QString& path, QString* err) override;
    bool stat(const QString& path, SftpAttrs* out, bool followLinks = true) override;
    bool mkdir(const QString& path, quint32 mode, QString* err) override;
    bool rmdir(const QString& path, QString* err) override;
    bool unlink(const QString& path, QString* err) override;
    bool rename(const QString& from, const QString& to, QString* err) override;
    bool setPermissions(const QString& path, quint32 mode, QString* err) override;
    bool setOwnership(const QString& path, quint32 uid, quint32 gid, QString* err) override;
    bool createSymlink(const QString& target, const QString& linkPath, QString* err) override;
    QString readLink(const QString& path) override;

    FileHandle openForRead(const QString& path, quint64* sizeOut, QString* err) override;
    FileHandle openForWrite(const QString& path, quint64 sizeHint, bool append,
                            quint32 mode, QString* err) override;
    int readFile(FileHandle, char* buf, int len, QString* err) override;
    int writeFile(FileHandle, const char* buf, int len, QString* err) override;
    bool seekFile(FileHandle, quint64 offset) override;
    void closeFile(FileHandle) override;

    bool truncate(const QString& path, QString* err) override;

private:
    Libssh2StatePtr m_state;
    LIBSSH2_SFTP* m_sftp = nullptr;
};

// ---------------------------------------------------------------------------
// Libssh2Scp - IScpSession over libssh2's SCP protocol.
// A single sendTo/recvFrom call runs one whole transfer while holding the
// session mutex (the SCP wire protocol is driven through a single channel;
// chunk-level unlocking is impossible without exposing the raw channel).
// For recursive downloads the SFTP subsystem is used internally (see note in
// Libssh2Engine.cpp).
// ---------------------------------------------------------------------------
class Libssh2Scp final : public IScpSession
{
public:
    explicit Libssh2Scp(Libssh2StatePtr state);
    ~Libssh2Scp() override;

    Outcome sendTo(const QString& localPath, const QString& remotePath,
                   bool recursive, ProgressFn progress) override;
    Outcome recvFrom(const QString& remotePath, const QString& localPath,
                     bool recursive, ProgressFn progress) override;

private:
    // All of these assume the state mutex is already held.
    Outcome sendFileLocked(const QString& localPath, const QString& remotePath,
                           const ProgressFn& progress);
    Outcome sendDirLocked(const QString& localPath, const QString& remotePath,
                          const ProgressFn& progress, quint64* filesSent);
    Outcome recvFileScpLocked(const QString& remotePath, const QString& localPath,
                              const ProgressFn& progress);
    Outcome recvDirLocked(const QString& remotePath, const QString& localPath,
                          const ProgressFn& progress, quint64* filesRecv);
    LIBSSH2_SFTP* ensureSftpLocked(QString* err);
    void shutdownSftpLocked();

    Libssh2StatePtr m_state;
    LIBSSH2_SFTP* m_sftp = nullptr; // lazily created for recursive transfers
};

// ---------------------------------------------------------------------------
// Libssh2Engine - ISshEngine implementation.
// ---------------------------------------------------------------------------
class Libssh2Engine final : public ISshEngine
{
public:
    Libssh2Engine();
    ~Libssh2Engine() override;

    QString backendName() const override;
    QString backendVersion() const override;

    Outcome connect(const QString& host, int port, int timeoutMs,
                    const std::function<void(const HostKeyInfo&)>& hostKeyCb = {}) override;
    Outcome connectOverFd(int fd, const QString& hostForLog, int port,
                          const std::function<void(const HostKeyInfo&)>& hostKeyCb = {}) override;

    HostKeyInfo serverHostKey() const override;
    QString serverBanner() const override;

    Outcome authenticate(const AuthParams& params,
                         const std::function<QStringList(const QStringList& prompts,
                                                         const QVector<bool>& echoes)>& promptCb = {}) override;

    bool isConnected() const override;
    void disconnect() override;

    std::unique_ptr<IChannel> openChannel(QString* err) override;
    std::unique_ptr<IChannel> openForwardChannel(const QString& destHost, int destPort, QString* err) override;
    std::unique_ptr<ISftpSession> openSftp(QString* err) override;
    std::unique_ptr<IScpSession> openScp(QString* err) override;

    bool remoteForwardListen(const QString& bindAddress, int port, int* boundPort, QString* err) override;
    std::unique_ptr<IChannel> acceptRemoteForward(int timeoutMs, QString* err) override;
    void remoteForwardCancel(const QString& bindAddress, int port) override;

    Outcome authenticateWithAgent(const QString& username) override;

    int sendKeepAlive() override;

    EngineInfo negotiatedInfo() const override;

    std::recursive_mutex& rawMutex() override;

    void setPreferredAuthOrder(bool agentFirst) override;

    // Backend extensions beyond ISshEngine (the generic interface only exposes
    // openChannel(), which produces an interactive shell channel). The worker
    // can use these through the concrete type when needed:
    std::unique_ptr<IChannel> openShellChannel(int cols, int rows,
                                               const QMap<QString, QString>& env, QString* err);
    std::unique_ptr<IChannel> openExecChannel(const QString& command, QString* err);
    std::unique_ptr<IChannel> openDirectTcpipChannel(const QString& host, int port, QString* err);

private:
    Outcome finishConnect(const QString& hostForLog, int port,
                          const std::function<void(const HostKeyInfo&)>& hostKeyCb,
                          int timeoutMs);
    Outcome authPasswordLocked(const QByteArray& user, const QString& password);
    Outcome authPublicKeyLocked(const AuthParams& params);
    Outcome authKeyboardInteractiveLocked(
        const QByteArray& user,
        const std::function<QStringList(const QStringList&, const QVector<bool>&)>& promptCb);
    Outcome authAgentLocked(const QByteArray& user);

    Libssh2StatePtr m_state;
    QString m_hostForLog;
    bool m_agentFirst = true;
};

} // namespace eclipse
