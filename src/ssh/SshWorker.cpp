#include "SshWorker.h"

#include <QHostAddress>
#include <QHostInfo>
#include <QLocalSocket>
#include <QRandomGenerator>
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

// ---------------------------------------------------------------------------
// X11 helpers (anonymous namespace)
// ---------------------------------------------------------------------------
namespace {

constexpr int kMaxX11Channels = 8;
constexpr int kMaxX11PreambleBytes = 64 * 1024; // never stall on a bogus stream

inline int pad4(int n) { return (4 - (n & 3)) & 3; }

// Parsed DISPLAY value: "[host]:display[.screen]", "unix:n", "/path:n", "n".
struct X11DisplayTarget
{
    bool valid = false;
    bool local = true;
    QString host;
    int display = 0;
    int screen = 0;
};

X11DisplayTarget parseX11Display(const QString& value)
{
    X11DisplayTarget t;
    const QString s = value.trimmed();
    if (s.isEmpty())
        return t;
    const int colon = s.lastIndexOf(QLatin1Char(':'));
    if (colon < 0) {
        // Bare number: treat as local display n.
        bool ok = false;
        const int n = s.toInt(&ok);
        if (!ok || n < 0)
            return t;
        t.valid = true;
        t.display = n;
        return t;
    }
    QString hostPart = s.left(colon);
    QString numPart = s.mid(colon + 1);
    const int dot = numPart.indexOf(QLatin1Char('.'));
    if (dot >= 0) {
        t.screen = numPart.mid(dot + 1).toInt();
        numPart = numPart.left(dot);
    }
    bool ok = false;
    const int n = numPart.toInt(&ok);
    if (!ok || n < 0)
        return t;
    t.display = n;
    t.valid = true;
    if (!hostPart.isEmpty() && hostPart.at(0) == QLatin1Char('/')) {
        t.local = true; // macOS launchd-style socket path
        t.host.clear();
        return t;
    }
    t.local = hostPart.isEmpty() || hostPart.compare(QLatin1String("unix"), Qt::CaseInsensitive) == 0
              || hostPart.compare(QLatin1String("localhost"), Qt::CaseInsensitive) == 0;
    t.host = t.local ? QString() : hostPart;
    return t;
}

// Scan an Xauthority file for the MIT-MAGIC-COOKIE-1 of one display.
// Record format: u16 family, u16 addrLen, addr, u16 numLen, number,
// u16 nameLen, name, u16 dataLen, data - all big-endian (network order).
QByteArray loadXauthorityCookie(const QString& path, int displayNumber)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return {};
    const QByteArray all = f.readAll();
    const QByteArray displayStr = QByteArray::number(displayNumber);
    const QByteArray hostName = QHostInfo::localHostName().toUtf8();

    auto u16 = [&all](int p, bool* ok) -> int {
        if (p < 0 || p + 2 > all.size()) {
            *ok = false;
            return 0;
        }
        *ok = true;
        return (quint8(all.at(p)) << 8) | quint8(all.at(p + 1));
    };

    int pos = 0;
    while (pos + 2 <= all.size()) {
        bool ok = false;
        const int family = u16(pos, &ok);
        if (!ok) break;
        pos += 2;
        const int addrLen = u16(pos, &ok); if (!ok) break;
        pos += 2;
        if (pos + addrLen > all.size()) break;
        const QByteArray addr = all.mid(pos, addrLen);
        pos += addrLen;
        const int numLen = u16(pos, &ok); if (!ok) break;
        pos += 2;
        if (pos + numLen > all.size()) break;
        const QByteArray num = all.mid(pos, numLen);
        pos += numLen;
        const int nameLen = u16(pos, &ok); if (!ok) break;
        pos += 2;
        if (pos + nameLen > all.size()) break;
        const QByteArray name = all.mid(pos, nameLen);
        pos += nameLen;
        const int dataLen = u16(pos, &ok); if (!ok) break;
        pos += 2;
        if (pos + dataLen > all.size()) break;
        const QByteArray data = all.mid(pos, dataLen);
        pos += dataLen;

        if (name != QByteArrayLiteral("MIT-MAGIC-COOKIE-1") || data.isEmpty())
            continue;
        const bool numberMatch = num == displayStr;
        const bool hostMatch = family == 65535 /*FamilyWild*/
                               || (family == 256 /*FamilyLocal*/ && !hostName.isEmpty() && addr == hostName);
        if (numberMatch && hostMatch)
            return data;
    }
    return {};
}

} // namespace

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

    // KEX preference (best effort; engines log rejections, never abort).
    if (!m_profile.kexAlgorithms.isEmpty())
        m_engine->setKexAlgorithms(m_profile.kexAlgorithms);

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
        if (it->lsocket)
            it->lsocket->deleteLater();
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
    if (m_profile.x11Forward) {
        ensureX11Cookies();
        QString xerr;
        const Outcome x11 = channel->requestX11(m_profile.x11Screen, m_x11FakeCookie, &xerr);
        if (!x11.ok)
            LOG_SSH_ERR(QStringLiteral("X11 forwarding request failed: %1")
                            .arg(x11.technical.isEmpty() ? x11.friendly : x11.technical));
        else
            LOG_SSH(QStringLiteral("X11 forwarding requested (screen %1)").arg(m_profile.x11Screen));
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

    // Accept inbound X11 channels (profile.x11Forward; the engines return
    // nullptr quickly when nothing is pending or X11 was never requested).
    pollX11();

    for (auto it = m_channels.begin(); it != m_channels.end();) {
        ChannelEntry& entry = it.value();
        IChannel* ch = entry.channel.get();
        if (!ch) {
            it = m_channels.erase(it);
            continue;
        }

        // Locally-dead X11 channels (display connection error/lost): reap.
        if (entry.dead) {
            if (entry.socket) {
                entry.socket->abort();
                entry.socket->deleteLater();
            }
            if (entry.lsocket) {
                entry.lsocket->abort();
                entry.lsocket->deleteLater();
            }
            emit channelClosed(it.key());
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
            else if (entry.isX11)
                writeX11Data(entry, data); // cookie rewrite on the setup packet
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
        if (entry.handshakeDone) {
            QByteArray up;
            if (entry.socket)
                up = entry.socket->readAll();
            else if (entry.lsocket)
                up = entry.lsocket->readAll();
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
            if (entry.lsocket) {
                entry.lsocket->flush();
                entry.lsocket->disconnectFromServer();
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

// ---------------------------------------------------------------------------
// X11 forwarding
// ---------------------------------------------------------------------------
void SshWorker::ensureX11Cookies()
{
    if (m_x11FakeCookie.isEmpty()) {
        // 16 ASCII hex characters: transmitted verbatim in the x11-req cookie
        // string and returned verbatim as the auth data of the first X11
        // setup packet, which makes the local rewrite a fixed-size replace.
        static const char kHex[] = "0123456789abcdef";
        QString cookie;
        cookie.reserve(16);
        for (int i = 0; i < 16; ++i)
            cookie.append(QLatin1Char(kHex[QRandomGenerator::system()->bounded(16)]));
        m_x11FakeCookie = cookie;
    }
    if (!m_x11CookieResolved) {
        m_x11CookieResolved = true;
        const X11DisplayTarget target = parseX11Display(qEnvironmentVariable("DISPLAY"));
        const int display = target.valid ? target.display : qMax(0, m_profile.x11Screen);
        QString xauth = qEnvironmentVariable("XAUTHORITY");
        if (xauth.isEmpty())
            xauth = utils::expandTildePath(QStringLiteral("~/.Xauthority"));
        m_x11RealCookie = loadXauthorityCookie(xauth, display);
        if (m_x11RealCookie.isEmpty())
            LOG_SSH(QStringLiteral("X11: no MIT-MAGIC-COOKIE-1 entry for display :%1 in %2 - "
                                   "remote X clients may be rejected unless access control is "
                                   "disabled on the local display (e.g. xhost +SI:localuser:$USER)")
                        .arg(display).arg(xauth));
    }
}

void SshWorker::pollX11()
{
    if (!m_profile.x11Forward || !m_engine || !m_engine->isConnected())
        return;
    int active = 0;
    for (const ChannelEntry& e : m_channels)
        if (e.isX11)
            ++active;
    if (active >= kMaxX11Channels)
        return;

    QString xerr;
    std::unique_ptr<IChannel> x11 = m_engine->acceptX11(0, &xerr);
    if (!x11)
        return;

    ensureX11Cookies();
    const int cid = m_nextChannelId++;
    ChannelEntry entry;
    entry.kind = ChannelEntry::Pipe;
    entry.isX11 = true;
    entry.handshakeDone = true; // socket -> channel pump is active immediately
    entry.channel = std::shared_ptr<IChannel>(std::move(x11));
    if (!openLocalX11Socket(cid, entry)) {
        // Local display unreachable: log + close the remote channel, never crash.
        LOG_SSH_ERR(QStringLiteral("X11 channel %1 closed: local display connection failed").arg(cid));
        entry.channel->close();
        emit channelClosed(cid);
        return;
    }
    m_channels.insert(cid, std::move(entry));
    LOG_SSH(QStringLiteral("X11 channel %1: forwarding to the local display").arg(cid));
}

bool SshWorker::openLocalX11Socket(int cid, ChannelEntry& entry)
{
    const X11DisplayTarget target = parseX11Display(qEnvironmentVariable("DISPLAY"));
    const int display = target.valid ? target.display : qMax(0, m_profile.x11Screen);

    // Failure/teardown marker: only flags the entry; the pump reaps it on its
    // next tick (never erases from m_channels inside a socket signal).
    auto markDead = [this, cid](const QString& why) {
        auto it = m_channels.find(cid);
        if (it == m_channels.end())
            return;
        LOG_SSH_ERR(QStringLiteral("X11 channel %1 dropped: %2").arg(cid).arg(why));
        it->dead = true;
        if (it->channel)
            it->channel->close();
    };

    if (target.valid && !target.local) {
        auto* tcp = new QTcpSocket(this);
        tcp->connectToHost(target.host, quint16(6000 + display));
        QObject::connect(tcp, &QTcpSocket::errorOccurred, this, [markDead](QAbstractSocket::SocketError e) {
            markDead(QStringLiteral("display TCP error (%1)").arg(int(e)));
        });
        QObject::connect(tcp, &QTcpSocket::disconnected, this, [markDead]() {
            markDead(QStringLiteral("display connection closed"));
        });
        entry.socket = tcp;
        return true;
    }

#ifdef _WIN32
    // No Unix-domain X11 sockets on Windows: VcXsrv/X410 listen on TCP
    // 127.0.0.1:6000+<display> (profile.x11Screen is the display number).
    Q_UNUSED(markDead);
    auto* tcp = new QTcpSocket(this);
    tcp->connectToHost(QStringLiteral("127.0.0.1"), quint16(6000 + display));
    QObject::connect(tcp, &QTcpSocket::errorOccurred, this, [cid](QAbstractSocket::SocketError e) {
        LOG_SSH_ERR(QStringLiteral("X11 channel %1: local display error (%2)").arg(cid).arg(int(e)));
    });
    entry.socket = tcp;
    return true;
#else
    auto* local = new QLocalSocket(this);
    local->connectToServer(QStringLiteral("/tmp/.X11-unix/X%1").arg(display));
    QObject::connect(local, &QLocalSocket::errorOccurred, this, [markDead](QLocalSocket::LocalSocketError e) {
        markDead(QStringLiteral("display socket error (%1)").arg(int(e)));
    });
    QObject::connect(local, &QLocalSocket::disconnected, this, [markDead]() {
        markDead(QStringLiteral("display connection closed"));
    });
    entry.lsocket = local;
    return true;
#endif
}

bool SshWorker::x11LocalConnected(const ChannelEntry& entry)
{
    if (entry.socket)
        return entry.socket->state() == QAbstractSocket::ConnectedState;
    if (entry.lsocket)
        return entry.lsocket->state() == QLocalSocket::ConnectedState;
    return false;
}

void SshWorker::writeX11Raw(ChannelEntry& entry, const QByteArray& data)
{
    if (data.isEmpty())
        return;
    if (entry.socket) {
        entry.socket->write(data);
        entry.socket->flush();
    } else if (entry.lsocket) {
        entry.lsocket->write(data);
        entry.lsocket->flush();
    }
}

void SshWorker::writeX11Data(ChannelEntry& entry, const QByteArray& data)
{
    if (!entry.x11RewriteDone) {
        entry.x11Preamble += data;
        tryFlushX11Preamble(entry);
        return;
    }
    writeX11Raw(entry, data);
}

void SshWorker::tryFlushX11Preamble(ChannelEntry& entry)
{
    const QByteArray& p = entry.x11Preamble;
    if (p.isEmpty() || !x11LocalConnected(entry))
        return;

    // X11 initial setup request (client -> server): byte order, 2x u16
    // protocol version, u16 auth-protocol length, u16 auth-data length,
    // u16 unused, then the strings, each padded to 4 bytes.
    if (p.at(0) != 'l' && p.at(0) != 'B') {
        writeX11Raw(entry, p); // not a setup packet - pass through untouched
        entry.x11RewriteDone = true;
        entry.x11Preamble.clear();
        return;
    }
    if (p.size() < 12)
        return;
    const bool little = p.at(0) == 'l';
    auto u16 = [&p, little](int off) -> int {
        return little ? (quint8(p.at(off)) | (quint8(p.at(off + 1)) << 8))
                      : ((quint8(p.at(off)) << 8) | quint8(p.at(off + 1)));
    };
    const int protoLen = u16(6);
    const int dataLen = u16(8);
    if (protoLen < 0 || dataLen < 0 || protoLen > 4096 || dataLen > 4096) {
        writeX11Raw(entry, p); // implausible lengths - pass through untouched
        entry.x11RewriteDone = true;
        entry.x11Preamble.clear();
        return;
    }
    const int need = 12 + pad4(protoLen) + pad4(dataLen);
    if (p.size() < need) {
        if (p.size() > kMaxX11PreambleBytes) { // safety valve: never stall
            writeX11Raw(entry, p);
            entry.x11RewriteDone = true;
            entry.x11Preamble.clear();
        }
        return;
    }

    const QByteArray protoName = p.mid(12, protoLen);
    const int dataOffset = 12 + protoLen + pad4(protoLen);
    const QByteArray authData = p.mid(dataOffset, dataLen);
    if (protoName == QByteArrayLiteral("MIT-MAGIC-COOKIE-1")
        && !m_x11RealCookie.isEmpty()
        && dataLen == m_x11RealCookie.size()
        && authData == m_x11FakeCookie.toLatin1()) {
        // Swap the fake cookie (sent in x11-req) for the real local one.
        QByteArray out = p;
        out.replace(dataOffset, dataLen, m_x11RealCookie);
        writeX11Raw(entry, out);
        LOG_SSH(QStringLiteral("X11: rewrote fake auth cookie to the real local cookie"));
    } else {
        writeX11Raw(entry, p);
    }
    entry.x11RewriteDone = true;
    entry.x11Preamble.clear();
}

} // namespace eclipse
