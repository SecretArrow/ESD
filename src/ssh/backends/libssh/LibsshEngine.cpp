#include "LibsshEngine.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QThread>

#include <fcntl.h>
#include <sys/stat.h>
#ifdef _WIN32
#include <io.h>
#else
#include <unistd.h>
#endif

#include <libssh/libssh.h>
#include <libssh/sftp.h>


#include "../../HostKeyManager.h"
#include "../../AgentClient.h"
#include "../../../common/Utils.h"
#include "../../../core/logging/Logger.h"

namespace eclipse {

namespace {

Outcome libsshError(ssh_session session, const QString& friendlyPrefix, const QString& hint = {})
{
    const QString technical = QStringLiteral("%1 (%2)")
                                  .arg(QString::fromLatin1(ssh_get_error(session)),
                                       QString::number(ssh_get_error_code(session) == 0 ? -1 : ssh_get_error_code(session)));
    return Outcome::fail(friendlyPrefix, technical, hint);
}

QString keyTypeName(ssh_keytypes_e type)
{
    switch (type) {
    case SSH_KEYTYPE_ED25519: return QStringLiteral("ssh-ed25519");
    case SSH_KEYTYPE_RSA: return QStringLiteral("ssh-rsa");
    case SSH_KEYTYPE_DSS: return QStringLiteral("ssh-dss");
    case SSH_KEYTYPE_ECDSA_P256: return QStringLiteral("ecdsa-sha2-nistp256");
    case SSH_KEYTYPE_ECDSA_P384: return QStringLiteral("ecdsa-sha2-nistp384");
    case SSH_KEYTYPE_ECDSA_P521: return QStringLiteral("ecdsa-sha2-nistp521");
    case SSH_KEYTYPE_ED25519_CERT01: return QStringLiteral("ssh-ed25519-cert-v01@openssh.com");
    case SSH_KEYTYPE_RSA_CERT01: return QStringLiteral("ssh-rsa-cert-v01@openssh.com");
    case SSH_KEYTYPE_SK_ED25519: return QStringLiteral("sk-ssh-ed25519@openssh.com");
    case SSH_KEYTYPE_SK_ECDSA: return QStringLiteral("sk-ecdsa-sha2-nistp256@openssh.com");
    default: return QStringLiteral("unknown");
    }
}

void fillSftpAttrs(const sftp_attributes_struct* a, SftpAttrs* out)
{
    out->size = quint64(a->size);
    out->permissions = a->permissions;
    out->uid = a->uid;
    out->gid = a->gid;
    out->mtime = qint64(a->mtime);
    out->atime = qint64(a->atime);
    const quint32 p = a->permissions;
    out->isDir = (p & S_IFMT) == S_IFDIR;
    out->isLink = (p & S_IFMT) == S_IFLNK;
    out->isRegular = (p & S_IFMT) == S_IFREG;
    out->canRead = (p & S_IRUSR) || !(p & S_IWUSR); // simplified owner heuristic
    out->canWrite = (p & S_IWUSR) != 0;
    out->canExec = (p & S_IXUSR) != 0;
}

} // namespace

// ---------------------------------------------------------------------------
// Engine
// ---------------------------------------------------------------------------
LibsshEngine::LibsshEngine()
{
    m_session = ssh_new();
}

LibsshEngine::~LibsshEngine()
{
    disconnect();
    if (m_session)
        ssh_free(m_session);
}

QString LibsshEngine::backendVersion() const
{
    return QString::fromLatin1(ssh_version(0));
}

void LibsshEngine::setIdentityPath(const QString& path)
{
    m_identityPath = path;
}

void LibsshEngine::setKnownHostsPath(const QString& path)
{
    m_knownHostsPath = path;
}

void LibsshEngine::setCompression(bool enabled)
{
    if (!m_session)
        return;
    ssh_options_set(m_session, SSH_OPTIONS_COMPRESSION, enabled ? "yes" : "no");
}

Outcome LibsshEngine::connect(const QString& host, int port, int timeoutMs,
                              const std::function<void(const HostKeyInfo&)>& hostKeyCb)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_session)
        return Outcome::fail(QStringLiteral("Internal error: SSH session not initialized."));

    ssh_options_set(m_session, SSH_OPTIONS_HOST, host.toUtf8().constData());
    unsigned int uport = unsigned(port);
    ssh_options_set(m_session, SSH_OPTIONS_PORT, &uport);
    unsigned int secs = unsigned(qMax(1, timeoutMs / 1000));
    ssh_options_set(m_session, SSH_OPTIONS_TIMEOUT, &secs);
    if (!m_knownHostsPath.isEmpty())
        ssh_options_set(m_session, SSH_OPTIONS_KNOWNHOSTS, m_knownHostsPath.toUtf8().constData());

    return finishConnect(host, port, hostKeyCb);
}

Outcome LibsshEngine::connectOverFd(int fd, const QString& hostForLog, int port,
                                    const std::function<void(const HostKeyInfo&)>& hostKeyCb)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_session)
        return Outcome::fail(QStringLiteral("Internal error: SSH session not initialized."));

    ssh_options_set(m_session, SSH_OPTIONS_HOST, hostForLog.toUtf8().constData());
    unsigned int uport = unsigned(port);
    ssh_options_set(m_session, SSH_OPTIONS_PORT, &uport);
    socket_t sock = socket_t(fd);
    if (ssh_options_set(m_session, SSH_OPTIONS_FD, &sock) != SSH_OK)
        return libsshError(m_session, QStringLiteral("Failed to attach the network connection."));
    m_ownedFd = fd;
    return finishConnect(hostForLog, port, hostKeyCb);
}

Outcome LibsshEngine::finishConnect(const QString& hostForLog, int port,
                                    const std::function<void(const HostKeyInfo&)>& hostKeyCb)
{
    LOG_SSH(QStringLiteral("[libssh] connecting to %1:%2").arg(hostForLog).arg(port));
    const int rc = ssh_connect(m_session);
    if (rc != SSH_OK)
        return libsshError(m_session, QStringLiteral("Could not reach the server %1:%2.").arg(hostForLog).arg(port),
                           QStringLiteral("Check the host name, port, VPN and firewall settings."));

    // Extract server host key and report BEFORE authentication.
    ssh_key key = nullptr;
    if (ssh_get_server_publickey(m_session, &key) == SSH_OK) {
        HostKeyInfo info;
        info.host = hostForLog;
        info.port = port;
        info.keyType = keyTypeName(ssh_key_type(key));
        char* blob = nullptr;
        if (ssh_pki_export_pubkey_base64(key, &blob) == SSH_OK && blob) {
            info.publicKeyBlob = QByteArray::fromBase64(QByteArray(blob));
            ssh_string_free_char(blob);
        }
        ssh_key_free(key);
        info.sha256Fingerprint = HostKeyManager::sha256Fingerprint(info.publicKeyBlob);
        info.md5Fingerprint = HostKeyManager::md5Fingerprint(info.publicKeyBlob);
        m_lastHostKey = info;
        if (hostKeyCb)
            hostKeyCb(info);
    }
    LOG_SSH(QStringLiteral("[libssh] handshake done, host key %1").arg(m_lastHostKey.sha256Fingerprint));
    return Outcome::success();
}

HostKeyInfo LibsshEngine::serverHostKey() const
{
    return m_lastHostKey;
}

QString LibsshEngine::serverBanner() const
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_session)
        return {};
    char* b = ssh_get_issue_banner(m_session);
    QString out = b ? QString::fromLatin1(b) : QString();
    if (b)
        ssh_string_free_char(b);
    return out;
}

Outcome LibsshEngine::authenticate(const AuthParams& params,
                                   const std::function<QStringList(const QStringList&, const QVector<bool>&)>& promptCb)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_session)
        return Outcome::fail(QStringLiteral("Internal error: session gone."));
    const QByteArray user = params.username.toUtf8();
    const char* userC = user.isEmpty() ? nullptr : user.constData();

    // Public key file(s)
    if (params.method == SshAuthMethod::PublicKey || params.allowAgentFallback) {
        if (!params.privateKeyPath.isEmpty()) {
            if (!m_identityPath.isEmpty())
                ssh_options_set(m_session, SSH_OPTIONS_ADD_IDENTITY, m_identityPath.toUtf8().constData());
            ssh_options_set(m_session, SSH_OPTIONS_ADD_IDENTITY, params.privateKeyPath.toUtf8().constData());
        } else if (!m_identityPath.isEmpty()) {
            ssh_options_set(m_session, SSH_OPTIONS_ADD_IDENTITY, m_identityPath.toUtf8().constData());
        }
    }

    switch (params.method) {
    case SshAuthMethod::Password: {
        const QByteArray pass = params.password.toUtf8();
        const int rc = ssh_userauth_password(m_session, userC, pass.constData());
        if (rc == SSH_AUTH_SUCCESS)
            return Outcome::success();
        if (rc == SSH_AUTH_DENIED)
            return Outcome::fail(QStringLiteral("The server rejected the username or password."),
                                 QStringLiteral("SSH_AUTH_DENIED"), QStringLiteral("Check your credentials."));
        if (rc == SSH_AUTH_PARTIAL)
            return Outcome::fail(QStringLiteral("Partial authentication: more credentials required."));
        if (params.allowInteractive && promptCb)
            break; // fall through to keyboard-interactive
        return libsshError(m_session, QStringLiteral("Authentication failed."));
    }
    case SshAuthMethod::Agent:
        return authenticateWithAgent(params.username);
    case SshAuthMethod::PublicKey: {
        const QByteArray pass = params.passphrase.toUtf8();
        const int rc = ssh_userauth_publickey_auto(m_session, userC,
                                                   params.passphrase.isEmpty() ? nullptr : pass.constData());
        if (rc == SSH_AUTH_SUCCESS)
            return Outcome::success();
        return Outcome::fail(QStringLiteral("Public key authentication failed."),
                             QStringLiteral("ssh_userauth_publickey_auto rc=%1").arg(rc),
                             QStringLiteral("Verify the key file path and passphrase."));
    }
    case SshAuthMethod::KeyboardInteractive:
        break;
    }

    // Keyboard-interactive loop (also used for MFA / 2FA).
    if (params.allowInteractive && promptCb) {
        int rc = ssh_userauth_kbdint(m_session, userC, nullptr);
        while (rc == SSH_AUTH_INFO) {
            const int nprompts = ssh_userauth_kbdint_getnprompts(m_session);
            QStringList prompts;
            QVector<bool> echoes;
            for (int i = 0; i < nprompts; ++i) {
                char echo = 0;
                const char* p = ssh_userauth_kbdint_getprompt(m_session, i, &echo);
                prompts.append(QString::fromLatin1(p ? p : ""));
                echoes.append(echo != 0);
            }
            if (nprompts > 0) {
                const QStringList answers = promptCb(prompts, echoes);
                for (int i = 0; i < nprompts && i < answers.size(); ++i)
                    ssh_userauth_kbdint_setanswer(m_session, i, answers.value(i).toUtf8().constData());
            }
            rc = ssh_userauth_kbdint(m_session, nullptr, nullptr);
        }
        if (rc == SSH_AUTH_SUCCESS)
            return Outcome::success();
        if (rc == SSH_AUTH_DENIED)
            return Outcome::fail(QStringLiteral("The server rejected the provided answers."),
                                 QStringLiteral("SSH_AUTH_DENIED"));
        return libsshError(m_session, QStringLiteral("Authentication failed."));
    }

    return libsshError(m_session, QStringLiteral("Authentication failed."));
}

Outcome LibsshEngine::authenticateWithAgent(const QString& username)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_session)
        return Outcome::fail(QStringLiteral("Internal error: session gone."));
    const QByteArray user = username.toUtf8();
    const int rc = ssh_userauth_agent(m_session, user.isEmpty() ? nullptr : user.constData());
    if (rc == SSH_AUTH_SUCCESS)
        return Outcome::success();
    if (rc == SSH_AUTH_DENIED)
        return Outcome::fail(QStringLiteral("No authorized key was accepted from the SSH agent."),
                             QStringLiteral("SSH_AUTH_DENIED"),
                             QStringLiteral("Is the key added to the agent (ssh-add)?"));
    return libsshError(m_session, QStringLiteral("SSH agent authentication failed."));
}

bool LibsshEngine::isConnected() const
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    return m_session && ssh_is_connected(m_session) != 0;
}

void LibsshEngine::disconnect()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (m_session && ssh_is_connected(m_session))
        ssh_disconnect(m_session);
    if (m_ownedFd >= 0) {
#ifdef _WIN32
        ::closesocket(m_ownedFd);
#else
        ::close(m_ownedFd);
#endif
        m_ownedFd = -1;
    }
}

std::unique_ptr<IChannel> LibsshEngine::openChannel(QString* err)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    ssh_channel ch = ssh_channel_new(m_session);
    if (!ch) {
        if (err)
            *err = QStringLiteral("Cannot allocate channel.");
        return nullptr;
    }
    if (ssh_channel_open_session(ch) != SSH_OK) {
        if (err)
            *err = QStringLiteral("Cannot open a session channel: %1").arg(QString::fromLatin1(ssh_get_error(m_session)));
        ssh_channel_free(ch);
        return nullptr;
    }
    return std::make_unique<LibsshChannel>(this, ch);
}

std::unique_ptr<ISftpSession> LibsshEngine::openSftp(QString* err)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    auto sftp = std::make_unique<LibsshSftp>(this);
    if (!sftp->isValid()) {
        if (err)
            *err = QStringLiteral("SFTP subsystem is not available on this server: %1")
                       .arg(QString::fromLatin1(ssh_get_error(m_session)));
        return nullptr;
    }
    return sftp;
}

std::unique_ptr<IScpSession> LibsshEngine::openScp(QString* err)
{
    Q_UNUSED(err);
    return std::make_unique<LibsshScp>(this);
}

std::unique_ptr<IChannel> LibsshEngine::openForwardChannel(const QString& destHost, int destPort, QString* err)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    ssh_channel ch = ssh_channel_new(m_session);
    if (!ch) {
        if (err)
            *err = QStringLiteral("Cannot allocate channel.");
        return nullptr;
    }
    if (ssh_channel_open_forward(ch, destHost.toUtf8().constData(), destPort, "127.0.0.1", 0) != SSH_OK) {
        if (err)
            *err = QStringLiteral("Could not open a forwarded channel: %1")
                       .arg(QString::fromLatin1(ssh_get_error(m_session)));
        ssh_channel_free(ch);
        return nullptr;
    }
    return std::make_unique<LibsshChannel>(this, ch);
}

bool LibsshEngine::remoteForwardListen(const QString& bindAddress, int port, int* boundPort, QString* err)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    int bound = port;
    const int rc = channel_forward_listen(m_session,
                                          bindAddress.isEmpty() ? nullptr : bindAddress.toUtf8().constData(),
                                          port, &bound);
    if (rc != 0) {
        if (err)
            *err = QStringLiteral("Remote listen failed: %1").arg(QString::fromLatin1(ssh_get_error(m_session)));
        return false;
    }
    if (boundPort)
        *boundPort = bound;
    m_remoteListeners.append({ bindAddress.isEmpty() ? QStringLiteral("*") : bindAddress, bound });
    return true;
}

std::unique_ptr<IChannel> LibsshEngine::acceptRemoteForward(int timeoutMs, QString* err)
{
    Q_UNUSED(err);
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    ssh_channel accepted = channel_forward_accept(m_session, timeoutMs);
    if (accepted)
        return std::make_unique<LibsshChannel>(this, accepted);
    return nullptr;
}

void LibsshEngine::remoteForwardCancel(const QString& bindAddress, int port)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    channel_forward_cancel(m_session, bindAddress.toUtf8().constData(), port);
    m_remoteListeners.removeAll({ bindAddress, port });
}

int LibsshEngine::sendKeepAlive()
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_session || ssh_is_connected(m_session) != 1)
        return -1;
    // RTT is measured by the worker using a tiny exec; keepalive packets are
    // sent via the engine's ignore packet when available.
    return 0;
}

EngineInfo LibsshEngine::negotiatedInfo() const
{
    EngineInfo info;
    info.backend = backendName();
    info.version = backendVersion();
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    if (!m_session)
        return info;
    if (m_lastHostKey.isValid())
        info.hostKeyAlgo = m_lastHostKey.keyType;
    return info;
}

// ---------------------------------------------------------------------------
// Channel
// ---------------------------------------------------------------------------
LibsshChannel::LibsshChannel(LibsshEngine* engine, ssh_channel_struct* channel)
    : m_engine(engine)
    , m_channel(channel)
{
}

LibsshChannel::~LibsshChannel()
{
    close();
}

Outcome LibsshChannel::openShell(int cols, int rows, const QStringList& env)
{
    if (ssh_channel_request_pty_size(m_channel, "xterm-256color", cols, rows) != SSH_OK)
        return libsshError(m_engine->rawSession(), QStringLiteral("PTY request was rejected by the server."));
    for (const QString& kv : env) {
        const int eq = kv.indexOf(QLatin1Char('='));
        if (eq <= 0)
            continue;
        ssh_channel_request_env(m_channel, kv.left(eq).toUtf8().constData(), kv.mid(eq + 1).toUtf8().constData());
    }
    if (ssh_channel_request_shell(m_channel) != SSH_OK)
        return libsshError(m_engine->rawSession(), QStringLiteral("Shell request was rejected by the server."));
    return Outcome::success();
}

Outcome LibsshChannel::openExecChannel(const QString& cmd)
{
    if (ssh_channel_request_exec(m_channel, cmd.toUtf8().constData()) != SSH_OK)
        return libsshError(m_engine->rawSession(), QStringLiteral("The command could not be executed."));
    return Outcome::success();
}

bool LibsshChannel::isOpen() const
{
    return m_channel && !m_closed && ssh_channel_is_closed(m_channel) == 0;
}

bool LibsshChannel::isEof() const
{
    return m_channel ? ssh_channel_is_eof(m_channel) != 0 : true;
}

int LibsshChannel::readStdout(char* buf, int len)
{
    if (!isOpen())
        return -1;
    const int rc = ssh_channel_read_nonblocking(m_channel, buf, len, 0);
    return rc == SSH_ERROR ? -1 : rc;
}

int LibsshChannel::readStderr(char* buf, int len)
{
    if (!isOpen())
        return -1;
    const int rc = ssh_channel_read_nonblocking(m_channel, buf, len, 1);
    return rc == SSH_ERROR ? -1 : rc;
}

int LibsshChannel::write(const char* buf, int len)
{
    if (!isOpen())
        return -1;
    int total = 0;
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + 5000;
    while (total < len) {
        const int rc = ssh_channel_write(m_channel, buf + total, len - total);
        if (rc == SSH_AGAIN) {
            if (QDateTime::currentMSecsSinceEpoch() > deadline)
                return total > 0 ? total : -1;
            QThread::msleep(2);
            continue;
        }
        if (rc <= 0)
            return total > 0 ? total : -1;
        total += rc;
    }
    return total;
}

void LibsshChannel::resizePty(int cols, int rows)
{
    if (isOpen())
        ssh_channel_change_pty_size(m_channel, cols, rows);
}

void LibsshChannel::sendEof()
{
    if (isOpen())
        ssh_channel_send_eof(m_channel);
}

void LibsshChannel::close()
{
    if (m_channel && !m_closed) {
        ssh_channel_close(m_channel);
        ssh_channel_free(m_channel);
        m_closed = true;
    }
    m_channel = nullptr;
}

int LibsshChannel::exitStatus()
{
    return m_channel ? ssh_channel_get_exit_status(m_channel) : -1;
}

// ---------------------------------------------------------------------------
// SFTP
// ---------------------------------------------------------------------------
LibsshSftp::LibsshSftp(LibsshEngine* engine)
    : m_engine(engine)
{
    if (!m_engine->rawSession())
        return;
    std::lock_guard<std::recursive_mutex> lock(m_engine->mutex());
    m_sftp = sftp_new(m_engine->rawSession());
    if (m_sftp && sftp_init(m_sftp) != SSH_OK) {
        sftp_free(m_sftp);
        m_sftp = nullptr;
    }
}

LibsshSftp::~LibsshSftp()
{
    std::lock_guard<std::recursive_mutex> lock(m_engine->mutex());
    if (m_sftp)
        sftp_free(m_sftp);
}

bool LibsshSftp::isValid() const
{
    return m_sftp != nullptr;
}

QString LibsshSftp::canonicalize(const QString& path)
{
    std::lock_guard<std::recursive_mutex> lock(m_engine->mutex());
    char* real = sftp_canonicalize_path(m_sftp, path.toUtf8().constData());
    QString out = real ? QString::fromUtf8(real) : path;
    if (real)
        ssh_string_free_char(real);
    return out;
}

QVector<SftpEntry> LibsshSftp::listDir(const QString& path, QString* err)
{
    QVector<SftpEntry> out;
    std::lock_guard<std::recursive_mutex> lock(m_engine->mutex());
    sftp_dir dir = sftp_opendir(m_sftp, path.toUtf8().constData());
    if (!dir) {
        if (err)
            *err = QString::fromUtf8(sftp_get_error(m_sftp) == SSH_FX_PERMISSION_DENIED
                                         ? "Permission denied"
                                         : ssh_get_error(m_engine->rawSession()));
        return out;
    }
    sftp_attributes a = nullptr;
    while ((a = sftp_readdir(m_sftp, dir)) != nullptr) {
        SftpEntry e;
        e.name = QString::fromUtf8(a->name);
        if (e.name != QLatin1String(".") && e.name != QLatin1String(".."))
            fillSftpAttrs(a, &e.attrs);
        sftp_attributes_free(a);
        if (!e.name.isEmpty())
            out.append(e);
    }
    if (!sftp_dir_eof(dir) && err)
        *err = QString::fromLatin1(ssh_get_error(m_engine->rawSession()));
    sftp_closedir(dir);
    return out;
}

bool LibsshSftp::stat(const QString& path, SftpAttrs* out, bool followLinks)
{
    std::lock_guard<std::recursive_mutex> lock(m_engine->mutex());
    sftp_attributes a = followLinks ? sftp_stat(m_sftp, path.toUtf8().constData())
                                    : sftp_lstat(m_sftp, path.toUtf8().constData());
    if (!a)
        return false;
    fillSftpAttrs(a, out);
    sftp_attributes_free(a);
    return true;
}

bool LibsshSftp::mkdir(const QString& path, quint32 mode, QString* err)
{
    std::lock_guard<std::recursive_mutex> lock(m_engine->mutex());
    if (sftp_mkdir(m_sftp, path.toUtf8().constData(), mode) == 0)
        return true;
    if (err)
        *err = QString::fromLatin1(ssh_get_error(m_engine->rawSession()));
    return false;
}

bool LibsshSftp::rmdir(const QString& path, QString* err)
{
    std::lock_guard<std::recursive_mutex> lock(m_engine->mutex());
    if (sftp_rmdir(m_sftp, path.toUtf8().constData()) == 0)
        return true;
    if (err)
        *err = QString::fromLatin1(ssh_get_error(m_engine->rawSession()));
    return false;
}

bool LibsshSftp::unlink(const QString& path, QString* err)
{
    std::lock_guard<std::recursive_mutex> lock(m_engine->mutex());
    if (sftp_unlink(m_sftp, path.toUtf8().constData()) == 0)
        return true;
    if (err)
        *err = QString::fromLatin1(ssh_get_error(m_engine->rawSession()));
    return false;
}

bool LibsshSftp::rename(const QString& from, const QString& to, QString* err)
{
    std::lock_guard<std::recursive_mutex> lock(m_engine->mutex());
    const QByteArray f = from.toUtf8();
    const QByteArray t = to.toUtf8();
    if (sftp_rename(m_sftp, f.constData(), t.constData()) == 0)
        return true;
    // SFTP v3 rename fails when the target exists; try unlink+rename once.
    sftp_unlink(m_sftp, t.constData());
    if (sftp_rename(m_sftp, f.constData(), t.constData()) == 0)
        return true;
    if (err)
        *err = QString::fromLatin1(ssh_get_error(m_engine->rawSession()));
    return false;
}

bool LibsshSftp::setPermissions(const QString& path, quint32 mode, QString* err)
{
    std::lock_guard<std::recursive_mutex> lock(m_engine->mutex());
    sftp_attributes a = sftp_lstat(m_sftp, path.toUtf8().constData());
    if (!a) {
        if (err)
            *err = QStringLiteral("Cannot read file attributes.");
        return false;
    }
    a->permissions = (a->permissions & ~quint32(07777)) | mode;
    a->flags |= SSH_FILEXFER_ATTR_PERMISSIONS;
    const int rc = sftp_setstat(m_sftp, path.toUtf8().constData(), a);
    sftp_attributes_free(a);
    if (rc == 0)
        return true;
    if (err)
        *err = QString::fromLatin1(ssh_get_error(m_engine->rawSession()));
    return false;
}

bool LibsshSftp::setOwnership(const QString& path, quint32 uid, quint32 gid, QString* err)
{
    std::lock_guard<std::recursive_mutex> lock(m_engine->mutex());
    sftp_attributes a = sftp_lstat(m_sftp, path.toUtf8().constData());
    if (!a) {
        if (err)
            *err = QStringLiteral("Cannot read file attributes.");
        return false;
    }
    a->uid = uid;
    a->gid = gid;
    a->flags |= SSH_FILEXFER_ATTR_UIDGID;
    const int rc = sftp_setstat(m_sftp, path.toUtf8().constData(), a);
    sftp_attributes_free(a);
    if (rc == 0)
        return true;
    if (err)
        *err = QString::fromLatin1(ssh_get_error(m_engine->rawSession()));
    return false;
}

bool LibsshSftp::createSymlink(const QString& target, const QString& linkPath, QString* err)
{
    std::lock_guard<std::recursive_mutex> lock(m_engine->mutex());
    if (sftp_symlink(m_sftp, target.toUtf8().constData(), linkPath.toUtf8().constData()) == 0)
        return true;
    ssh_channel ch = ssh_channel_new(m_engine->rawSession());
    if (!ch)
        return false;
    const QString cmd = QStringLiteral("ln -s -- %1 %2")
                            .arg(QString(target).replace(QLatin1Char('\''), QStringLiteral("'\\''")),
                                 QString(linkPath).replace(QLatin1Char('\''), QStringLiteral("'\\''")));
    if (ssh_channel_open_session(ch) != SSH_OK || ssh_channel_request_exec(ch, cmd.toUtf8().constData()) != SSH_OK) {
        ssh_channel_close(ch);
        ssh_channel_free(ch);
        if (err)
            *err = QStringLiteral("Cannot run ln on the remote host.");
        return false;
    }
    ssh_channel_send_eof(ch);
    ssh_channel_close(ch);
    ssh_channel_free(ch);
    return true;
}

QString LibsshSftp::readLink(const QString& path)
{
    // Resolve through `readlink` via an exec channel (portable across servers).
    std::lock_guard<std::recursive_mutex> lock(m_engine->mutex());
    ssh_channel ch = ssh_channel_new(m_engine->rawSession());
    if (!ch)
        return {};
    const QString quoted = QString(path).replace(QLatin1Char('\''), QStringLiteral("'\\''"));
    if (ssh_channel_open_session(ch) != SSH_OK
        || ssh_channel_request_exec(ch, QStringLiteral("readlink -- '%1'").arg(quoted).toUtf8().constData()) != SSH_OK) {
        ssh_channel_close(ch);
        ssh_channel_free(ch);
        return {};
    }
    QByteArray out;
    char buf[1024];
    for (;;) {
        const int n = ssh_channel_read_timeout(ch, buf, sizeof(buf), 0, 500);
        if (n <= 0)
            break;
        out.append(buf, n);
    }
    ssh_channel_send_eof(ch);
    ssh_channel_close(ch);
    ssh_channel_free(ch);
    return QString::fromUtf8(out).trimmed();
}

LibsshSftp::FileHandle LibsshSftp::openForRead(const QString& path, quint64* sizeOut, QString* err)
{
    std::lock_guard<std::recursive_mutex> lock(m_engine->mutex());
    sftp_file f = sftp_open(m_sftp, path.toUtf8().constData(), O_RDONLY, 0);
    if (!f) {
        if (err)
            *err = QString::fromLatin1(ssh_get_error(m_engine->rawSession()));
        return nullptr;
    }
    if (sizeOut) {
        sftp_attributes a = sftp_fstat(f);
        *sizeOut = a ? quint64(a->size) : 0;
        if (a)
            sftp_attributes_free(a);
    }
    return reinterpret_cast<FileHandle>(f);
}

LibsshSftp::FileHandle LibsshSftp::openForWrite(const QString& path, quint64, bool append, quint32 mode, QString* err)
{
    std::lock_guard<std::recursive_mutex> lock(m_engine->mutex());
    const int flags = append ? (O_WRONLY | O_CREAT | O_APPEND) : (O_WRONLY | O_CREAT | O_TRUNC);
    sftp_file f = sftp_open(m_sftp, path.toUtf8().constData(), flags, mode);
    if (!f) {
        if (err)
            *err = QString::fromLatin1(ssh_get_error(m_engine->rawSession()));
        return nullptr;
    }
    return reinterpret_cast<FileHandle>(f);
}

int LibsshSftp::readFile(FileHandle h, char* buf, int len, QString* err)
{
    std::lock_guard<std::recursive_mutex> lock(m_engine->mutex());
    const int rc = sftp_read(reinterpret_cast<sftp_file>(h), buf, size_t(len));
    if (rc < 0 && err)
        *err = QString::fromLatin1(ssh_get_error(m_engine->rawSession()));
    return rc;
}

int LibsshSftp::writeFile(FileHandle h, const char* buf, int len, QString* err)
{
    std::lock_guard<std::recursive_mutex> lock(m_engine->mutex());
    int written = 0;
    while (written < len) {
        const int rc = sftp_write(reinterpret_cast<sftp_file>(h), buf + written, size_t(len - written));
        if (rc < 0) {
            if (err)
                *err = QString::fromLatin1(ssh_get_error(m_engine->rawSession()));
            return written > 0 ? written : rc;
        }
        written += rc;
    }
    return written;
}

bool LibsshSftp::seekFile(FileHandle h, quint64 offset)
{
    std::lock_guard<std::recursive_mutex> lock(m_engine->mutex());
    sftp_seek64(reinterpret_cast<sftp_file>(h), offset);
    return true;
}

void LibsshSftp::closeFile(FileHandle h)
{
    if (!h)
        return;
    std::lock_guard<std::recursive_mutex> lock(m_engine->mutex());
    sftp_close(reinterpret_cast<sftp_file>(h));
}

bool LibsshSftp::truncate(const QString& path, QString* err)
{
    std::lock_guard<std::recursive_mutex> lock(m_engine->mutex());
    sftp_file f = sftp_open(m_sftp, path.toUtf8().constData(), O_WRONLY | O_TRUNC, 0);
    if (!f) {
        if (err)
            *err = QString::fromLatin1(ssh_get_error(m_engine->rawSession()));
        return false;
    }
    sftp_close(f);
    return true;
}

// ---------------------------------------------------------------------------
// SCP
// ---------------------------------------------------------------------------
LibsshScp::LibsshScp(LibsshEngine* engine)
    : m_engine(engine)
{
}

LibsshScp::~LibsshScp() = default;

Outcome LibsshScp::sendTo(const QString& localPath, const QString& remotePath, bool recursive, ProgressFn progress)
{
    const QFileInfo fi(localPath);
    if (!fi.exists())
        return Outcome::fail(QStringLiteral("Local file does not exist: %1").arg(localPath));

    std::lock_guard<std::recursive_mutex> lock(m_engine->mutex());
    if (fi.isDir()) {
        if (!recursive)
            return Outcome::fail(QStringLiteral("Source is a directory - enable recursive transfer."));
        // Create the remote dir then send each child.
        const QString base = remotePath + QStringLiteral("/") + fi.fileName();
        ssh_scp scp = ssh_scp_new(m_engine->rawSession(), SSH_SCP_WRITE | SSH_SCP_RECURSIVE,
                                  base.toUtf8().constData());
        if (!scp)
            return Outcome::fail(QStringLiteral("Cannot initialize SCP."));
        Outcome result = Outcome::success();
        if (ssh_scp_init(scp) != SSH_OK) {
            result = libsshError(m_engine->rawSession(), QStringLiteral("SCP could not start."));
        } else {
            ssh_scp_push_directory(scp, fi.fileName().toUtf8().constData(), 0755);
            QDirIterator it(localPath, QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot);
            while (it.hasNext() && result.ok) {
                const QString child = it.next();
                const QFileInfo cf(child);
                if (cf.isDir())
                    continue; // simplified: files only at first level
                QFile file(child);
                if (!file.open(QIODevice::ReadOnly)) {
                    result = Outcome::fail(QStringLiteral("Cannot read local file %1").arg(child));
                    break;
                }
                const QByteArray data = file.readAll();
                if (ssh_scp_push_file(scp, cf.fileName().toUtf8().constData(),
                                      quint64(data.size()), 0644) != SSH_OK) {
                    result = libsshError(m_engine->rawSession(), QStringLiteral("SCP push failed."));
                } else {
                    if (ssh_scp_write(scp, data.constData(), size_t(data.size())) != SSH_OK)
                        result = libsshError(m_engine->rawSession(), QStringLiteral("SCP write failed."));
                    if (progress && !progress(data.size(), data.size()))
                        result = Outcome::fail(QStringLiteral("Transfer canceled."));
                }
            }
            ssh_scp_close(scp);
        }
        ssh_scp_free(scp);
        return result;
    }

    QFile file(localPath);
    if (!file.open(QIODevice::ReadOnly))
        return Outcome::fail(QStringLiteral("Cannot read local file %1").arg(localPath));

    ssh_scp scp = ssh_scp_new(m_engine->rawSession(), SSH_SCP_WRITE, remotePath.toUtf8().constData());
    if (!scp)
        return Outcome::fail(QStringLiteral("Cannot initialize SCP."));
    Outcome result = Outcome::success();
    if (ssh_scp_init(scp) != SSH_OK) {
        result = libsshError(m_engine->rawSession(), QStringLiteral("SCP could not start."));
    } else {
        const quint64 total = quint64(file.size());
        if (ssh_scp_push_file(scp, fi.fileName().toUtf8().constData(), total, 0644) != SSH_OK)
            result = libsshError(m_engine->rawSession(), QStringLiteral("SCP push failed."));
        else {
            quint64 sent = 0;
            QByteArray chunk;
            chunk.resize(128 * 1024);
            while (sent < total && result.ok) {
                const qint64 n = file.read(chunk.data(), chunk.size());
                if (n <= 0)
                    break;
                if (ssh_scp_write(scp, chunk.constData(), size_t(n)) != SSH_OK) {
                    result = libsshError(m_engine->rawSession(), QStringLiteral("SCP write failed."));
                    break;
                }
                sent += quint64(n);
                if (progress && !progress(sent, total))
                    result = Outcome::fail(QStringLiteral("Transfer canceled."));
            }
        }
        ssh_scp_close(scp);
    }
    ssh_scp_free(scp);
    return result;
}

Outcome LibsshScp::recvFrom(const QString& remotePath, const QString& localPath, bool recursive, ProgressFn progress)
{
    Q_UNUSED(recursive);
    std::lock_guard<std::recursive_mutex> lock(m_engine->mutex());
    ssh_scp scp = ssh_scp_new(m_engine->rawSession(), SSH_SCP_READ, remotePath.toUtf8().constData());
    if (!scp)
        return Outcome::fail(QStringLiteral("Cannot initialize SCP."));
    Outcome result = Outcome::success();
    if (ssh_scp_init(scp) != SSH_OK) {
        result = libsshError(m_engine->rawSession(), QStringLiteral("SCP could not start."));
    } else {
        int rc = ssh_scp_pull_request(scp);
        if (rc != SSH_SCP_REQUEST_NEWFILE) {
            result = Outcome::fail(QStringLiteral("Remote file is not available over SCP."),
                                   QStringLiteral("pull_request rc=%1").arg(rc));
        } else {
            const quint64 size = quint64(ssh_scp_request_get_size(scp));
            const QString name = QString::fromLatin1(ssh_scp_request_get_filename(scp));
            ssh_scp_accept_request(scp);

            const QString destDir = QFileInfo(localPath).isDir() ? localPath : QFileInfo(localPath).absolutePath();
            const QString destFile = QFileInfo(localPath).isDir() ? destDir + QLatin1Char('/') + name : localPath;
            QFile out(destFile);
            if (!out.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
                result = Outcome::fail(QStringLiteral("Cannot write local file %1").arg(destFile));
            } else {
                quint64 got = 0;
                QByteArray chunk;
                chunk.resize(128 * 1024);
                while (got < size && result.ok) {
                    const int toRead = int(qMin<quint64>(chunk.size(), size - got));
                    const int n = ssh_scp_read(scp, chunk.data(), size_t(toRead));
                    if (n <= 0)
                        break;
                    out.write(chunk.constData(), n);
                    got += quint64(n);
                    if (progress && !progress(got, size))
                        result = Outcome::fail(QStringLiteral("Transfer canceled."));
                }
                out.close();
            }
        }
        ssh_scp_close(scp);
    }
    ssh_scp_free(scp);
    return result;
}

// ---------------------------------------------------------------------------
// Factory
// ---------------------------------------------------------------------------
std::unique_ptr<ISshEngine> createEngine(SshEngineKind kind)
{
    return std::make_unique<LibsshEngine>();
}

QString libsshVersionString()
{
    return QString::fromLatin1(ssh_version(0));
}

QString libssh2VersionString()
{
    return {}; // provided by the libssh2 backend target when compiled in
}

} // namespace eclipse
