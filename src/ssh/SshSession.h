#pragma once

#include <QElapsedTimer>
#include <QTimer>
#include <QVariantMap>

#include <QtQml/QJSValue>

#include "../core/profiles/ConnectionProfile.h"
#include "SshWorker.h"

namespace eclipse {

// ---------------------------------------------------------------------------
// SshSession - one user-visible connection (one tab). Lives on the UI thread;
// delegates all transport work to SshWorker on its own thread. Adds:
//   * state machine exposure to QML
//   * auto-reconnect with exponential backoff
//   * uptime / latency tracking
//   * credentials-ask flow
// ---------------------------------------------------------------------------
class SshSession : public QObject
{
    Q_OBJECT
    Q_PROPERTY(qint64 sessionId READ sessionId CONSTANT)
    Q_PROPERTY(qint64 profileId READ profileId CONSTANT)
    Q_PROPERTY(QString name READ name NOTIFY nameChanged)
    Q_PROPERTY(QString host READ host CONSTANT)
    Q_PROPERTY(int port READ port CONSTANT)
    Q_PROPERTY(QString username READ username CONSTANT)
    Q_PROPERTY(QString state READ stateName NOTIFY stateChanged)
    Q_PROPERTY(int latencyMs READ latencyMs NOTIFY latencyChanged)
    Q_PROPERTY(quint64 uptimeSec READ uptimeSec NOTIFY uptimeChanged)
    Q_PROPERTY(QString engineName READ engineName NOTIFY connectedInfoChanged)
    Q_PROPERTY(bool connected READ isConnected NOTIFY stateChanged)

public:
    explicit SshSession(ConnectionProfile profile, QObject* parent = nullptr);
    ~SshSession() override;

    qint64 sessionId() const { return m_sessionId; }
    qint64 profileId() const { return m_profile.id; }
    QString name() const { return m_name; }
    QString host() const { return m_profile.host; }
    int port() const { return m_profile.port; }
    QString username() const { return m_profile.username; }
    QString stateName() const;
    int latencyMs() const { return m_latencyMs; }
    quint64 uptimeSec() const;
    QString engineName() const { return m_engineName; }
    bool isConnected() const { return m_state == SessionState::Connected; }

    const ConnectionProfile& profile() const { return m_profile; }
    SshWorker* worker() const { return m_worker; }

    void connectTo(const QString& password, const QString& passphrase);
    Q_INVOKABLE void disconnect();
    Q_INVOKABLE void openTerminal(int cols = 80, int rows = 24);
    Q_INVOKABLE void answerAuthPrompt(const QStringList& answers);
    Q_INVOKABLE void provideRuntimePassword(const QString& password, const QString& passphrase);
    Q_INVOKABLE void decideHostKey(bool accepted, bool trustAndSave);
    Q_INVOKABLE void duplicateSession();
    Q_INVOKABLE void sendToTerminal(const QString& text);   // types into last terminal
    // QML one-shot exec: runs "command" on its own channel (concurrent calls
    // allowed; SshWorker::runExec opens a fresh channel per tag) and invokes
    // the JS callback once, on the main thread, with a single QVariantMap:
    // { ok: bool, exitCode: int, output: QString }.
    Q_INVOKABLE void runCommand(const QString& command, const QJSValue& callback);
    std::shared_ptr<ISftpSession> createSftpSession(QString* err);

signals:
    void nameChanged();
    void stateChanged(const QString& state);
    void hostKeyNeeded(const QVariantMap& keyInfo, bool isChangedKey);
    void authPromptNeeded(const QVariantList& prompts);
    void credentialsNeeded();
    void connectedInfoChanged(const QVariantMap& info);
    void authFailed(const QString& friendly, const QString& technical, const QString& hint);
    void disconnected(const QString& reason, bool byRequest);
    void reconnecting(int attempt, int maxAttempts);
    void terminalOpened(int cid, int cols, int rows);
    void terminalData(int cid, const QByteArray& data, bool isStderr);
    void terminalClosed(int cid);
    void latencyChanged(int ms);
    void uptimeChanged();
    void execOutput(const QByteArray& tag, const QByteArray& data, bool isStderr);
    void execFinished(const QByteArray& tag, int exitCode);
    void forwardStatusChanged(const QString& ruleId, const QString& status, const QString& error);

private slots:
    void trackTerminal(int cid);
    void onStateChanged(int state);
    void onHostKey(const eclipse::HostKeyInfo& info, const QString& checkError);
    void onAuthPrompt(const QStringList& prompts, const QVector<bool>& echoes);
    void onConnected(const eclipse::EngineInfo& info);
    void onAuthFailed(const QString& friendly, const QString& technical, const QString& hint);
    void onDisconnected(const QString& reason, bool byRequest);
    void onLatency(int ms);
    void onForwardStatus(const QString& ruleId, const QString& status, const QString& error);
    void tryReconnect();

private:
    void startWorker(const QString& password, const QString& passphrase);

    static qint64 s_nextId;
    qint64 m_sessionId = 0;
    ConnectionProfile m_profile;
    QString m_name;
    SshWorker* m_worker = nullptr;
    QThread* m_workerThread = nullptr;
    SessionState m_state = SessionState::Idle;
    int m_latencyMs = -1;
    QElapsedTimer m_uptime;
    QString m_engineName;

    QString m_pendingPassword;   // runtime only
    QString m_pendingPassphrase;
    bool m_awaitingCredentials = false;
    bool m_userRequestedDisconnect = false;

    // Host key state
    QVariantMap m_lastHostKeyMap;
    bool m_lastKeyChanged = false;

    int m_lastTerminalCid = -1;

    // runCommand() bookkeeping: one entry per outstanding exec, keyed by the
    // worker exec tag. Only touched on the main thread (QJSValue is not
    // thread-safe; execOutput/execFinished are delivered queued to us here).
    struct PendingExec
    {
        QJSValue callback;
        QByteArray output;
    };
    QHash<QByteArray, PendingExec> m_pendingExecs;
    qint64 m_execTagCounter = 0;

    // Reconnect
    QTimer* m_reconnectTimer = nullptr;
    int m_reconnectAttempt = 0;
    bool m_reconnectScheduled = false;
};

} // namespace eclipse
