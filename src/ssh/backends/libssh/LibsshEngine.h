#pragma once

#include <memory>
#include <mutex>

#include "../../ISshEngine.h"

struct ssh_session_struct;
struct ssh_channel_struct;
struct sftp_session_struct;
struct ssh_scp_struct;

namespace eclipse {

// ---------------------------------------------------------------------------
// LibsshEngine - SSH transport, channels, SFTP and SCP on top of libssh.
// Threading contract documented in ISshEngine.h; a recursive mutex serializes
// every access to the shared session (libssh is not thread-safe per session).
// ---------------------------------------------------------------------------

class LibsshEngine : public ISshEngine
{
public:
    LibsshEngine();
    ~LibsshEngine() override;

    QString backendName() const override { return QStringLiteral("libssh"); }
    QString backendVersion() const override;

    Outcome connect(const QString& host, int port, int timeoutMs,
                    const std::function<void(const HostKeyInfo&)>& hostKeyCb = {}) override;
    Outcome connectOverFd(int fd, const QString& hostForLog, int port,
                          const std::function<void(const HostKeyInfo&)>& hostKeyCb = {}) override;

    HostKeyInfo serverHostKey() const override;
    QString serverBanner() const override;

    Outcome authenticate(const AuthParams& params,
                         const std::function<QStringList(const QStringList&, const QVector<bool>&)>& promptCb = {}) override;
    Outcome authenticateWithAgent(const QString& username) override;

    bool isConnected() const override;
    void disconnect() override;

    std::unique_ptr<IChannel> openChannel(QString* err) override;
    std::unique_ptr<IChannel> openForwardChannel(const QString& destHost, int destPort, QString* err) override;
    std::unique_ptr<ISftpSession> openSftp(QString* err) override;
    std::unique_ptr<IScpSession> openScp(QString* err) override;

    bool remoteForwardListen(const QString& bindAddress, int port, int* boundPort, QString* err) override;
    std::unique_ptr<IChannel> acceptRemoteForward(int timeoutMs, QString* err) override;
    void remoteForwardCancel(const QString& bindAddress, int port) override;

    int sendKeepAlive() override;
    EngineInfo negotiatedInfo() const override;

    void setKexAlgorithms(const QString& list) override;
    std::unique_ptr<IChannel> acceptX11(int timeoutMs, QString* err) override;

    void setIdentityPath(const QString& path);
    void setKnownHostsPath(const QString& path);
    void setCompression(bool enabled);

    std::recursive_mutex& mutex() { return m_mutex; }
    std::recursive_mutex& rawMutex() override { return m_mutex; }
    ssh_session_struct* rawSession() const { return m_session; }

private:
    Outcome finishConnect(const QString& hostForLog, int port,
                          const std::function<void(const HostKeyInfo&)>& hostKeyCb);
    void registerX11Channel(ssh_channel_struct* shellChannel);

    ssh_session_struct* m_session = nullptr;
    QString m_identityPath;
    QString m_knownHostsPath;
    QString m_kexPreference;
    int m_ownedFd = -1;
    HostKeyInfo m_lastHostKey;
    QVector<QPair<QString, int>> m_remoteListeners;
    QVector<ssh_channel_struct*> m_x11RequestChannels; // shells with x11-req sent
    mutable std::recursive_mutex m_mutex;
};

class LibsshChannel : public IChannel
{
public:
    LibsshChannel(LibsshEngine* engine, ssh_channel_struct* channel);
    ~LibsshChannel() override;

    bool isOpen() const override;
    bool isEof() const override;
    Outcome openShell(int cols, int rows, const QStringList& env) override;
    Outcome openExecChannel(const QString& command) override;
    Outcome requestX11(int screenNumber, const QString& authCookie, QString* err) override;
    int readStdout(char* buf, int len) override;
    int readStderr(char* buf, int len) override;
    int write(const char* buf, int len) override;
    void resizePty(int cols, int rows) override;
    void sendEof() override;
    void close() override;
    int exitStatus() override;

private:
    friend class LibsshEngine;

    LibsshEngine* m_engine = nullptr;
    ssh_channel_struct* m_channel = nullptr;
    bool m_closed = false;
};

class LibsshSftp : public ISftpSession
{
public:
    explicit LibsshSftp(LibsshEngine* engine);
    ~LibsshSftp() override;

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
    FileHandle openForWrite(const QString& path, quint64 sizeHint, bool append, quint32 mode, QString* err) override;
    int readFile(FileHandle, char* buf, int len, QString* err) override;
    int writeFile(FileHandle, const char* buf, int len, QString* err) override;
    bool seekFile(FileHandle, quint64 offset) override;
    void closeFile(FileHandle) override;
    bool truncate(const QString& path, QString* err) override;

private:
    LibsshEngine* m_engine = nullptr;
    sftp_session_struct* m_sftp = nullptr;
};

class LibsshScp : public IScpSession
{
public:
    explicit LibsshScp(LibsshEngine* engine);
    ~LibsshScp() override;

    Outcome sendTo(const QString& localPath, const QString& remotePath, bool recursive, ProgressFn progress) override;
    Outcome recvFrom(const QString& remotePath, const QString& localPath, bool recursive, ProgressFn progress) override;

private:
    LibsshEngine* m_engine = nullptr;
};

} // namespace eclipse
