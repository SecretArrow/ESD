#include "SshSession.h"

#include <QThread>

#include "../core/logging/Logger.h"
#include "../core/profiles/ProfileStore.h"
#include "../core/settings/Settings.h"
#include "ForwardManager.h"
#include "HostKeyManager.h"

namespace eclipse {

qint64 SshSession::s_nextId = 1;

SshSession::SshSession(ConnectionProfile profile, QObject* parent)
    : QObject(parent)
    , m_sessionId(s_nextId++)
    , m_profile(std::move(profile))
    , m_name(m_profile.name.isEmpty() ? m_profile.host : m_profile.name)
{
    m_reconnectTimer = new QTimer(this);
    m_reconnectTimer->setSingleShot(true);
    connect(m_reconnectTimer, &QTimer::timeout, this, &SshSession::tryReconnect);
    connect(this, &SshSession::terminalOpened, this, &SshSession::trackTerminal);
}

SshSession::~SshSession()
{
    if (m_workerThread) {
        m_workerThread->quit();
        m_workerThread->wait(5000);
    }
}

QString SshSession::stateName() const
{
    return sessionStateName(m_state);
}

quint64 SshSession::uptimeSec() const
{
    return m_state == SessionState::Connected && m_uptime.isValid()
               ? quint64(m_uptime.elapsed() / 1000)
               : 0;
}

void SshSession::connectTo(const QString& password, const QString& passphrase)
{
    m_userRequestedDisconnect = false;
    m_reconnectScheduled = false;
    m_reconnectAttempt = 0;
    m_pendingPassword = password;
    m_pendingPassphrase = passphrase;
    startWorker(password, passphrase);
}

void SshSession::startWorker(const QString& password, const QString& passphrase)
{
    if (m_workerThread) { // restart path
        m_workerThread->quit();
        m_workerThread->wait(5000);
        m_workerThread = nullptr;
        m_worker = nullptr;
    }

    m_worker = new SshWorker(m_profile);
    m_workerThread = new QThread(this);
    m_worker->moveToThread(m_workerThread);

    connect(m_workerThread, &QThread::started, m_worker, &SshWorker::startConnect);
    connect(m_worker, &SshWorker::stateChanged, this, &SshSession::onStateChanged, Qt::QueuedConnection);
    connect(m_worker, &SshWorker::hostKeyPresented, this, &SshSession::onHostKey, Qt::QueuedConnection);
    connect(m_worker, &SshWorker::authPromptReceived, this, &SshSession::onAuthPrompt, Qt::QueuedConnection);
    connect(m_worker, &SshWorker::connected, this, &SshSession::onConnected, Qt::QueuedConnection);
    connect(m_worker, &SshWorker::authFailed, this, &SshSession::onAuthFailed, Qt::QueuedConnection);
    connect(m_worker, &SshWorker::disconnected, this, &SshSession::onDisconnected, Qt::QueuedConnection);
    connect(m_worker, &SshWorker::latencyChanged, this, &SshSession::onLatency, Qt::QueuedConnection);
    connect(m_worker, &SshWorker::forwardStatusChanged, this, &SshSession::onForwardStatus, Qt::QueuedConnection);
    connect(m_worker, &SshWorker::terminalOpened, this, &SshSession::terminalOpened, Qt::QueuedConnection);
    connect(m_worker, &SshWorker::channelData, this, &SshSession::terminalData, Qt::QueuedConnection);
    connect(m_worker, &SshWorker::channelClosed, this, &SshSession::terminalClosed, Qt::QueuedConnection);
    connect(m_worker, &SshWorker::execOutput, this, &SshSession::execOutput, Qt::QueuedConnection);
    connect(m_worker, &SshWorker::execFinished, this, &SshSession::execFinished, Qt::QueuedConnection);

    if (!password.isEmpty() || !passphrase.isEmpty())
        m_worker->supplyCredentials(password, passphrase);

    m_workerThread->start();
}

void SshSession::disconnect()
{
    m_userRequestedDisconnect = true;
    m_reconnectTimer->stop();
    m_reconnectScheduled = false;
    if (m_worker)
        QMetaObject::invokeMethod(m_worker, &SshWorker::disconnectSession, Qt::QueuedConnection);
}

void SshSession::openTerminal(int cols, int rows)
{
    if (m_worker)
        QMetaObject::invokeMethod(m_worker, [&] { m_worker->openTerminal(cols, rows); }, Qt::QueuedConnection);
}

void SshSession::answerAuthPrompt(const QStringList& answers)
{
    if (m_worker)
        QMetaObject::invokeMethod(m_worker,
                                  [&] { m_worker->provideAuthAnswers(answers); },
                                  Qt::QueuedConnection);
}

void SshSession::provideRuntimePassword(const QString& password, const QString& passphrase)
{
    m_awaitingCredentials = false;
    m_pendingPassword = password;
    m_pendingPassphrase = passphrase;
    startWorker(password, passphrase);
}

void SshSession::decideHostKey(bool accepted, bool trustAndSave)
{
    if (m_worker)
        QMetaObject::invokeMethod(m_worker,
                                  [&] { m_worker->hostKeyDecision(accepted, trustAndSave); },
                                  Qt::QueuedConnection);
}

void SshSession::duplicateSession()
{
    // The UI layer clones the profile and opens a new tab; nothing to do here.
}

void SshSession::sendToTerminal(const QString& text)
{
    if (m_worker && m_lastTerminalCid >= 0) {
        const int cid = m_lastTerminalCid;
        const QByteArray bytes = text.toUtf8();
        QMetaObject::invokeMethod(m_worker,
                                  [w = m_worker, cid, bytes]() { w->channelWrite(cid, bytes); },
                                  Qt::QueuedConnection);
    }
}

std::shared_ptr<ISftpSession> SshSession::createSftpSession(QString* err)
{
    return m_worker ? m_worker->createSftpSession(err) : nullptr;
}

// ---------------------------------------------------------------------------
// Worker signal handlers
// ---------------------------------------------------------------------------
void SshSession::onStateChanged(int state)
{
    m_state = SessionState(state);
    emit stateChanged(stateName());
}

void SshSession::onHostKey(const HostKeyInfo& info, const QString&)
{
    const HostKeyCheckResult check = HostKeyManager().check(info);
    m_lastKeyChanged = (check == HostKeyCheckResult::Changed);
    if (check == HostKeyCheckResult::Known)
        return; // silently accept saved keys
    m_lastHostKeyMap = {
        { "host", info.host },
        { "port", info.port },
        { "keyType", info.keyType },
        { "sha256", info.sha256Fingerprint },
        { "md5", info.md5Fingerprint },
    };
    emit hostKeyNeeded(m_lastHostKeyMap, m_lastKeyChanged);
}

void SshSession::onAuthPrompt(const QStringList& prompts, const QVector<bool>& echoes)
{
    QVariantList list;
    for (int i = 0; i < prompts.size(); ++i) {
        list.append(QVariantMap {
            { "text", prompts.at(i) },
            { "echo", i < echoes.size() ? echoes.at(i) : true },
        });
    }
    emit authPromptNeeded(list);
}

void SshSession::trackTerminal(int cid)
{
    m_lastTerminalCid = cid;
}

void SshSession::onConnected(const EngineInfo& info)
{
    m_engineName = info.backend;
    m_uptime.start();
    m_reconnectAttempt = 0;
    ProfileStore::instance().touchLastUsed(m_profile.id);
    emit connectedInfoChanged(QVariantMap {
        { "backend", info.backend },
        { "version", info.version },
        { "kex", info.kex },
        { "hostKeyAlgo", info.hostKeyAlgo },
        { "cipherIn", info.cipherIn },
        { "cipherOut", info.cipherOut },
    });
    ForwardManager::instance().startAutoRules(m_worker);
    // Startup commands go to the first terminal when it opens (UI concern).
}

void SshSession::onAuthFailed(const QString& friendly, const QString& technical, const QString& hint)
{
    emit authFailed(friendly, technical, hint);
}

void SshSession::onDisconnected(const QString& reason, bool byRequest)
{
    m_state = SessionState::Disconnected;
    emit stateChanged(stateName());
    emit disconnected(reason, byRequest);

    const bool mayReconnect = !byRequest && m_profile.autoReconnect
                              && Settings::instance().autoReconnect();
    if (mayReconnect && !m_userRequestedDisconnect) {
        const int maxRetries = qMax(1, Settings::instance().reconnectRetries());
        if (m_reconnectAttempt < maxRetries) {
            ++m_reconnectAttempt;
            const int interval = qMin(30000, Settings::instance().reconnectBaseIntervalMs()
                                                * (1 << qMin(4, m_reconnectAttempt - 1)));
            m_reconnectScheduled = true;
            m_reconnectTimer->start(interval);
            emit reconnecting(m_reconnectAttempt, maxRetries);
            LOG_CONN(QStringLiteral("Reconnect %1/%2 scheduled in %3 ms (%4)")
                         .arg(m_reconnectAttempt).arg(maxRetries).arg(interval).arg(reason));
        }
    }
}

void SshSession::tryReconnect()
{
    if (!m_reconnectScheduled)
        return;
    m_reconnectScheduled = false;
    startWorker(m_pendingPassword, m_pendingPassphrase);
}

void SshSession::onLatency(int ms)
{
    m_latencyMs = ms;
    emit latencyChanged(ms);
}

void SshSession::onForwardStatus(const QString& ruleId, const QString& status, const QString& error)
{
    emit forwardStatusChanged(ruleId, status, error);
}

} // namespace eclipse
