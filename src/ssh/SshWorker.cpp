#include "SshWorker.h"

#include <QHostAddress>
#include <QTcpSocket>
#include <QThread>

#include "../common/Utils.h"
#include "../core/credentials/CredentialManager.h"
#include "../core/logging/Logger.h"
#include "Bridge.h"
#include "backends/libssh/LibsshEngine.h"
#include "ForwardManager.h"
#include "HostKeyManager.h"
#include "JumpConnector.h"
#include "ProxyDialer.h"

namespace eclipse {

QString sessionStateName(SessionState s)
{
    switch (s) {
    case SessionState::Idle: return QStringLiteral("Idle");
    case SessionState::Resolving: return QStringLiteral("Resolving");
    case SessionState::Connecting: return QStringLiteral("Connecting");
    case SessionState::WaitingHostKey: return QStringLiteral("WaitingHostKey");
    case SessionState::Authenticating: return QStringLiteral("Authenticating");
    case SessionState::Connected: return QStringLiteral("Connected");
    case SessionState::Disconnecting: return QStringLiteral("Disconnecting");
    case SessionState::Disconnected: return QStringLiteral("Disconnected");
    case SessionState::AuthFailed: return QStringLiteral("AuthFailed");
    case SessionState::HostKeyRejected: return QStringLiteral("HostKeyRejected");
    case SessionState::Error: return QStringLiteral("Error");
    }
    return QStringLiteral("Unknown");
}

SshWorker::SshWorker(ConnectionProfile profile, QObject* parent)
    : QObject(parent)
    , m_profile(std::move(profile))
{
    m_pollTimer = new QTimer(this);
    m_pollTimer->setInterval(4);
    connect(m_pollTimer, &QTimer::timeout, this, &SshWorker::pollTick);

    m_keepAliveTimer = new QTimer(this);
    connect(m_keepAliveTimer, &QTimer::timeout, this, &SshWorker::updateLatency);
}

SshWorker::~SshWorker()
{
    m_byRequest = true;
    teardown(QStringLiteral("Worker destroyed"), true);
}

void SshWorker::supplyCredentials(const QString& password, const QString& passphrase)
{
    m_password = password;
    m_passphrase = passphrase;
}

void SshWorker::setState(SessionState s)
{
    if (m_state == s)
        return;
    m_state = s;
    emit stateChanged(int(s));
}

std::shared_ptr<ISftpSession> SshWorker::createSftpSession(QString* err)
{
    if (!m_engine)
        return nullptr;
    std::unique_ptr<ISftpSession> session = m_engine->openSftp(err);
    if (!session)
        return nullptr;
    return std::shared_ptr<ISftpSession>(std::move(session));
}

// ---------------------------------------------------------------------------
// Connect flow
// ---------------------------------------------------------------------------
void SshWorker::startConnect()
{
    setState(SessionState::Resolving);

    // 1. Engine selection
    SshEngineKind kind = SshEngineKind::Libssh;
    if (m_profile.engine == QLatin1String("libssh2"))
        kind = SshEngineKind::Libssh2;
    m_engine = createEngine(kind);
    if (!m_engine) {
        emit authFailed(QStringLiteral("No SSH engine is available for this profile."),
                        QStringLiteral("createEngine(%1) failed").arg(m_profile.engine), {});
        setState(SessionState::Error);
        return;
    }
#ifdef ECLIPSE_HAVE_LIBSSH
    if (kind == SshEngineKind::Libssh || kind == SshEngineKind::Auto) {
        if (auto* libssh = dynamic_cast<LibsshEngine*>(m_engine.get())) {
            libssh->setKnownHostsPath(HostKeyManager().path());
            libssh->setCompression(m_profile.compression);
            if (!m_profile.privateKeyPath.isEmpty() && m_profile.authMethod == QLatin1String("publickey"))
                libssh->setIdentityPath(utils::expandTildePath(m_profile.privateKeyPath));
        }
    }
#endif

    // 2. Establish transport (jump chain first, then engine connect)
    setState(SessionState::Connecting);
    QString err;
    int targetFd = -1;
    if (!m_profile.jumpHosts.isEmpty()) {
        m_jump = std::make_unique<JumpConnector>();
        if (!m_jump->establishChain(m_profile, targetFd, &err)) {
            emit authFailed(QStringLiteral("Could not establish the jump host chain."), err,
                            QStringLiteral("Connect to the bastion host once directly so its host key gets saved."));
            setState(SessionState::Error);
            emit disconnected(err, false);
            return;
        }
    }

    Outcome oc;
    if (targetFd >= 0) {
        oc = m_engine->connectOverFd(targetFd, m_profile.host, m_profile.port, {});
    } else {
        ProxyConfig proxy;
        proxy.type = m_profile.proxyType;
        proxy.host = m_profile.proxyHost;
        proxy.port = m_profile.proxyPort;
        proxy.user = m_profile.proxyUser;
        proxy.password = CredentialManager::instance().loadSecret(
            QStringLiteral("profile:%1:proxy-password").arg(m_profile.id));

        if (proxy.type.isEmpty() || proxy.type == QLatin1String("none")) {
            oc = m_engine->connect(m_profile.host, m_profile.port, m_profile.connectTimeoutMs, {});
        } else {
            int fd = -1;
            oc = dial(m_profile.host, m_profile.port, proxy, m_profile.connectTimeoutMs, &fd);
            if (oc.ok)
                oc = m_engine->connectOverFd(fd, m_profile.host, m_profile.port, {});
            else if (fd >= 0)
                closeFd(fd);
        }
    }
    if (!oc.ok) {
        m_jump.reset();
        emit authFailed(oc.friendly, oc.technical, oc.hint);
        setState(SessionState::Error);
        emit disconnected(oc.friendly, false);
        return;
    }

    // 3. Host key verification - NEVER auto-accepted.
    setState(SessionState::WaitingHostKey);
    {
        QMutexLocker lock(&m_decisionMutex);
        m_hostKeyDecided = false;
        m_hostKeyAccepted = false;
        m_hostKeySaved = false;
    }
    QString checkError;
    const HostKeyCheckResult check = HostKeyManager().check(m_engine->serverHostKey(), &checkError);
    emit hostKeyPresented(m_engine->serverHostKey(),
                          check == HostKeyCheckResult::Error ? checkError : QString());

    {
        QMutexLocker lock(&m_decisionMutex);
        if (!m_hostKeyDecided)
            m_hostKeyDecision.wait(&m_decisionMutex, 180000);
        if (!m_hostKeyDecided || !m_hostKeyAccepted) {
            setState(SessionState::HostKeyRejected);
            teardown(m_hostKeyDecided ? QStringLiteral("Host key rejected by user")
                                      : QStringLiteral("Host key dialog timed out"),
                     true);
            emit disconnected(QStringLiteral("Connection canceled: host key was not accepted."), true);
            return;
        }
        if (m_hostKeySaved)
            HostKeyManager().save(m_engine->serverHostKey());
    }

    // 4. Authentication
    setState(SessionState::Authenticating);
    const Outcome auth = runAuthentication();
    m_password = QString(); // wipe runtime secrets
    m_passphrase = QString();
    if (!auth.ok) {
        emit authFailed(auth.friendly, auth.technical, auth.hint);
        setState(SessionState::AuthFailed);
        emit disconnected(auth.friendly, false);
        return;
    }

    // 5. Connected: auto-start forwarding rules + keepalive
    setState(SessionState::Connected);
    emit connected(m_engine->negotiatedInfo());

    for (const ForwardRuleSpec& rule : m_profile.forwardingRules) {
        if (rule.enabled && rule.autoStart) {
            QString ferr;
            if (!startForwardInternal(rule, &ferr))
                emit forwardStatusChanged(rule.id, QStringLiteral("error"), ferr);
            else
                emit forwardStatusChanged(rule.id, QStringLiteral("active"), {});
        }
    }

    if (m_profile.keepAliveSeconds > 0) {
        m_keepAliveTimer->start(m_profile.keepAliveSeconds * 1000);
        updateLatency();
    }
    m_pollTimer->start();
    LOG_CONN(QStringLiteral("Session established to %1@%2:%3")
                 .arg(m_profile.username, m_profile.host, QString::number(m_profile.port)));
}

Outcome SshWorker::runAuthentication()
{
    AuthParams params;
    params.username = m_profile.username;
    params.allowInteractive = m_profile.allowKeyboardInteractive;
    params.allowAgentFallback = false;
    params.privateKeyPath = utils::expandTildePath(m_profile.privateKeyPath);
    params.passphrase = m_passphrase;

    CredentialManager& creds = CredentialManager::instance();
    const QString credBase = QStringLiteral("profile:%1").arg(m_profile.id);

    if (m_profile.authMethod == QLatin1String("publickey")) {
        params.method = SshAuthMethod::PublicKey;
        if (params.passphrase.isEmpty())
            params.passphrase = creds.loadSecret(credBase + QStringLiteral(":passphrase"));
        params.tryDefaultIdentities = params.privateKeyPath.isEmpty();
    } else if (m_profile.authMethod == QLatin1String("agent")) {
        params.method = SshAuthMethod::Agent;
    } else if (m_profile.authMethod == QLatin1String("keyboard-interactive")) {
        params.method = SshAuthMethod::KeyboardInteractive;
        params.password = m_password.isEmpty() ? creds.loadSecret(credBase + QStringLiteral(":password"))
                                               : m_password;
    } else {
        params.method = SshAuthMethod::Password;
        params.password = m_password.isEmpty() ? creds.loadSecret(credBase + QStringLiteral(":password"))
                                               : m_password;
    }

    return m_engine->authenticate(params,
                                  [this](const QStringList& prompts, const QVector<bool>& echoes) {
                                      // Surface prompts to the UI, then block for answers.
                                      QMutexLocker lock(&m_promptMutex);
                                      m_promptReady = false;
                                      m_promptAnswers.clear();
                                      emit authPromptReceived(prompts, echoes);
                                      m_promptCond.wait(&m_promptMutex, 180000);
                                      return m_promptAnswers;
                                  });
}

// ---------------------------------------------------------------------------
// Slots
// ---------------------------------------------------------------------------
void SshWorker::hostKeyDecision(bool accepted, bool trustAndSave)
{
    QMutexLocker lock(&m_decisionMutex);
    m_hostKeyAccepted = accepted;
    m_hostKeySaved = trustAndSave;
    m_hostKeyDecided = true;
    m_hostKeyDecision.wakeAll();
}

void SshWorker::provideAuthAnswers(const QStringList& answers)
{
    QMutexLocker lock(&m_promptMutex);
    m_promptAnswers = answers;
    m_promptReady = true;
    m_promptCond.wakeAll();
}

void SshWorker::disconnectSession()
{
    m_byRequest = true;
    teardown(QStringLiteral("Disconnected by user"), true);
}

void SshWorker::teardown(const QString& reason, bool byRequest)
{
    setState(SessionState::Disconnecting);
    if (m_pollTimer)
        m_pollTimer->stop();
    if (m_keepAliveTimer)
        m_keepAliveTimer->stop();

    for (auto it = m_channels.begin(); it != m_channels.end(); ++it) {
        if (it->socket)
            it->socket->deleteLater();
    }
    m_channels.clear();
    m_forwardServers.clear();
    m_remoteListeners.clear();

    if (m_engine) {
        m_engine->disconnect();
        m_engine.reset();
    }
    setState(SessionState::Disconnected);
    emit disconnected(reason, byRequest);
    LOG_CONN(QStringLiteral("Session torn down: %1").arg(reason));
}

void SshWorker::openTerminal(int cols, int rows)
{
    if (!m_engine || !m_engine->isConnected())
        return;
    QString err;
    std::unique_ptr<IChannel> channel = m_engine->openChannel(&err);
    if (!channel) {
        LOG_SSH_ERR(QStringLiteral("openTerminal: %1").arg(err));
        return;
    }
    const Outcome oc = channel->openShell(cols, rows, m_profile.environment);
    if (!oc.ok) {
        LOG_SSH_ERR(QStringLiteral("shell request failed: %1").arg(oc.friendly));
        return;
    }
    const int cid = m_nextChannelId++;
    ChannelEntry entry;
    entry.channel = std::shared_ptr<IChannel>(std::move(channel));
    entry.kind = ChannelEntry::Shell;
    m_channels.insert(cid, std::move(entry));
    emit terminalOpened(cid, cols, rows);
}

void SshWorker::channelWrite(int cid, const QByteArray& data)
{
    const auto it = m_channels.constFind(cid);
    if (it == m_channels.cend() || data.isEmpty())
        return;
    it->channel->write(data.constData(), data.size());
}

void SshWorker::channelResize(int cid, int cols, int rows)
{
    const auto it = m_channels.constFind(cid);
    if (it != m_channels.cend())
        it->channel->resizePty(cols, rows);
}

void SshWorker::channelClose(int cid)
{
    m_channels.remove(cid);
    emit channelClosed(cid);
}

void SshWorker::runExec(const QByteArray& tag, const QString& command)
{
    if (!m_engine || !m_engine->isConnected()) {
        emit execFinished(tag, -1);
        return;
    }
    QString err;
    std::unique_ptr<IChannel> channel = m_engine->openChannel(&err);
    if (!channel) {
        emit execFinished(tag, -1);
        return;
    }
    const Outcome oc = channel->openExecChannel(command);
    if (!oc.ok) {
        emit execFinished(tag, -1);
        return;
    }
    const int cid = m_nextChannelId++;
    ChannelEntry entry;
    entry.channel = std::shared_ptr<IChannel>(std::move(channel));
    entry.kind = ChannelEntry::Exec;
    entry.execTag = tag;
    m_channels.insert(cid, std::move(entry));
}

void SshWorker::cancelExec(const QByteArray& tag)
{
    for (auto it = m_channels.begin(); it != m_channels.end(); ++it) {
        if (it->execTag == tag) {
            m_channels.erase(it);
            break;
        }
    }
}

void SshWorker::startForward(const eclipse::ForwardRuleSpec& rule)
{
    QString err;
    if (startForwardInternal(rule, &err))
        emit forwardStatusChanged(rule.id, QStringLiteral("active"), {});
    else
        emit forwardStatusChanged(rule.id, QStringLiteral("error"), err);
}

void SshWorker::stopForward(const QString& ruleId)
{
    stopForwardInternal(ruleId);
    emit forwardStatusChanged(ruleId, QStringLiteral("stopped"), {});
}

bool SshWorker::startForwardInternal(const ForwardRuleSpec& rule, QString* err)
{
    if (!m_engine || !m_engine->isConnected()) {
        if (err)
            *err = QStringLiteral("Session is not connected.");
        return false;
    }
    LOG_TUNNEL(QStringLiteral("Starting %1 forward '%2'")
                   .arg(rule.type, rule.name.isEmpty() ? rule.id : rule.name));

    if (rule.type == QLatin1String("remote")) {
        int bound = 0;
        if (!m_engine->remoteForwardListen(rule.listenAddress, rule.listenPort, &bound, err))
            return false;
        m_remoteRules.insert(rule.id, rule);
        const int lid = m_nextChannelId++;
        ChannelEntry marker;
        marker.kind = ChannelEntry::Pipe;
        marker.forwardRuleId = rule.id;
        marker.handshakeDone = true; // marker only
        m_channels.insert(lid, std::move(marker));
        m_remoteListeners.insert(rule.id, lid);
        return true;
    }

    // local / dynamic: bind a TCP listener on this worker thread
    auto* server = new QTcpServer(this);
    if (!server->listen(QHostAddress(rule.listenAddress), quint16(rule.listenPort))) {
        if (err)
            *err = QStringLiteral("Cannot listen on %1:%2 - %3")
                       .arg(rule.listenAddress, QString::number(rule.listenPort), server->errorString());
        server->deleteLater();
        return false;
    }
    const quint16 bound = server->serverPort();
    connect(server, &QTcpServer::newConnection, this, [this, server, rule]() {
        while (QTcpSocket* client = server->nextPendingConnection()) {
            QString cerr;
            std::unique_ptr<IChannel> channel;
            if (rule.type == QLatin1String("local"))
                channel = m_engine->openForwardChannel(rule.destHost, rule.destPort, &cerr);
            else
                channel = m_engine->openChannel(&cerr); // destination known after SOCKS5
            if (!channel) {
                LOG_TUNNEL_ERR(QStringLiteral("forward channel: %1").arg(cerr));
                client->abort();
                client->deleteLater();
                continue;
            }
            const int cid = m_nextChannelId++;
            client->setParent(this);
            ChannelEntry entry;
            entry.kind = ChannelEntry::Pipe;
            entry.socket = client;
            entry.forwardRuleId = rule.id;
            entry.handshakeDone = (rule.type == QLatin1String("local"));
            entry.channel = std::shared_ptr<IChannel>(std::move(channel));
            m_channels.insert(cid, std::move(entry));
        }
    });
    m_forwardServers.insert(rule.id, server);
    LOG_TUNNEL(QStringLiteral("Listening on %1:%2 (%3)").arg(rule.listenAddress).arg(bound).arg(rule.type));
    return true;
}

void SshWorker::stopForwardInternal(const QString& ruleId)
{
    if (QTcpServer* server = m_forwardServers.take(ruleId)) {
        server->close();
        server->deleteLater();
    }
    if (const int lid = m_remoteListeners.take(ruleId); lid != 0)
        m_channels.remove(lid);
    for (auto it = m_channels.begin(); it != m_channels.end();) {
        if (it->forwardRuleId == ruleId) {
            if (it->socket)
                it->socket->deleteLater();
            it = m_channels.erase(it);
        } else {
            ++it;
        }
    }
    m_remoteRules.remove(ruleId);
}

// ---------------------------------------------------------------------------
// Poll loop - the heart of I/O multiplexing on the worker thread.
// ---------------------------------------------------------------------------
void SshWorker::pollTick()
{
    if (!m_engine || !m_engine->isConnected()) {
        if (m_state == SessionState::Connected) {
            teardown(QStringLiteral("Connection lost"), false);
            emit disconnected(QStringLiteral("Connection lost."), false);
        }
        return;
    }

    // Accept forwarded channels from remote listeners.
    if (!m_remoteListeners.isEmpty()) {
        QString aerr;
        std::unique_ptr<IChannel> accepted = m_engine->acceptRemoteForward(0, &aerr);
        if (accepted) {
            const QString ruleId = m_remoteListeners.constBegin().key();
            const ForwardRuleSpec rule = m_remoteRules.value(ruleId);
            auto* dest = new QTcpSocket(this);
            dest->connectToHost(rule.destHost, quint16(rule.destPort));
            const int cid = m_nextChannelId++;
            ChannelEntry entry;
            entry.kind = ChannelEntry::Pipe;
            entry.socket = dest;
            entry.forwardRuleId = ruleId;
            entry.handshakeDone = true;
            entry.channel = std::shared_ptr<IChannel>(std::move(accepted));
            m_channels.insert(cid, std::move(entry));
        }
    }

    for (auto it = m_channels.begin(); it != m_channels.end();) {
        ChannelEntry& entry = it.value();
        IChannel* ch = entry.channel.get();
        if (!ch) {
            it = m_channels.erase(it);
            continue;
        }

        // SOCKS5 handshake for dynamic forwards (client side)
        if (entry.socket && !entry.handshakeDone) {
            entry.socksBuffer.append(entry.socket->readAll());
            QByteArray reply;
            QString host;
            int port = 0;
            const bool done = ForwardManager::parseSocks5Step(&entry.socksBuffer, &host, &port, &reply);
            if (!done)
                continue;
            const bool okReq = !reply.isEmpty() && quint8(reply.at(1)) == 0x00;
            if (okReq) {
                // Swap the bare channel for a direct-tcpip channel to the target
                QString ferr;
                std::unique_ptr<IChannel> fwd = m_engine->openForwardChannel(host, port, &ferr);
                if (fwd) {
                    entry.channel = std::shared_ptr<IChannel>(std::move(fwd));
                    ch = entry.channel.get();
                    entry.socket->write(reply); // success reply
                    entry.socket->flush();
                    entry.handshakeDone = true;
                    entry.socksBuffer.clear();
                } else {
                    LOG_TUNNEL_ERR(QStringLiteral("SOCKS target failed: %1").arg(ferr));
                    static const unsigned char fail5[] = { 0x05, 0x01, 0x00, 0x01, 0, 0, 0, 0, 0, 0 };
                    entry.socket->write(reinterpret_cast<const char*>(fail5), 10);
                    entry.socket->flush();
                    it = m_channels.erase(it);
                    continue;
                }
            } else {
                // refusal reply already written
                it = m_channels.erase(it);
                continue;
            }
        }

        // Channel -> UI/socket
        char buf[32 * 1024];
        for (;;) {
            const int n = ch->readStdout(buf, sizeof(buf));
            if (n <= 0)
                break;
            const QByteArray data(buf, n);
            if (entry.kind == ChannelEntry::Shell)
                emit channelData(it.key(), data, false);
            else if (entry.kind == ChannelEntry::Exec)
                emit execOutput(entry.execTag, data, false);
            else if (entry.socket && entry.socket->state() == QAbstractSocket::ConnectedState) {
                entry.socket->write(data);
                entry.socket->flush();
            }
        }
        for (;;) {
            const int n = ch->readStderr(buf, sizeof(buf));
            if (n <= 0)
                break;
            const QByteArray data(buf, n);
            if (entry.kind == ChannelEntry::Shell)
                emit channelData(it.key(), data, true);
            else if (entry.kind == ChannelEntry::Exec)
                emit execOutput(entry.execTag, data, true);
            else if (entry.socket && entry.socket->state() == QAbstractSocket::ConnectedState)
                entry.socket->write(data);
        }

        // Socket -> channel
        if (entry.socket && entry.handshakeDone) {
            const QByteArray up = entry.socket->readAll();
            if (!up.isEmpty())
                ch->write(up.constData(), up.size());
        }

        const bool channelGone = !ch->isOpen();
        const bool execDone = entry.kind == ChannelEntry::Exec && ch->isEof();
        if (channelGone || execDone) {
            if (entry.kind == ChannelEntry::Exec)
                emit execFinished(entry.execTag, ch->exitStatus());
            emit channelClosed(it.key());
            if (entry.socket) {
                entry.socket->flush();
                entry.socket->disconnectFromHost();
            }
            it = m_channels.erase(it);
            continue;
        }
        ++it;
    }
}

void SshWorker::updateLatency()
{
    if (!m_engine || !m_engine->isConnected())
        return;
    QElapsedTimer timer;
    timer.start();
    QString err;
    std::unique_ptr<IChannel> channel = m_engine->openChannel(&err);
    if (!channel)
        return;
    if (!channel->openExecChannel(QStringLiteral(":")).ok)
        return;
    char buf[256];
    const qint64 deadline = QDateTime::currentMSecsSinceEpoch() + 3000;
    for (;;) {
        const int n = channel->readStdout(buf, sizeof(buf));
        if (n > 0) {
            emit latencyChanged(int(timer.elapsed()));
            break;
        }
        if (!channel->isOpen() || QDateTime::currentMSecsSinceEpoch() > deadline)
            break;
        QThread::msleep(4);
    }
}

} // namespace eclipse
