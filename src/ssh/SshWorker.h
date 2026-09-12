#pragma once

#include <QElapsedTimer>
#include <QHash>
#include <QMutex>
#include <QObject>
#include <QTcpServer>
#include <QTimer>
#include <QWaitCondition>

#include <memory>

#include "../core/profiles/ConnectionProfile.h"
#include "ISshEngine.h"
#include "Types.h"

class QTcpSocket;

namespace eclipse {

enum class SessionState
{
    Idle,
    Resolving,
    Connecting,
    WaitingHostKey,
    Authenticating,
    Connected,
    Disconnecting,
    Disconnected,
    AuthFailed,
    HostKeyRejected,
    Error
};

QString sessionStateName(SessionState s);

// ---------------------------------------------------------------------------
// SshWorker - owns the SSH engine and lives on a dedicated thread.
// Every libssh/libssh2 call happens here; the UI thread only exchanges
// queued signals. The UI never blocks, even during handshakes and transfers.
// ---------------------------------------------------------------------------
class SshWorker : public QObject
{
    Q_OBJECT
public:
    explicit SshWorker(ConnectionProfile profile, QObject* parent = nullptr);
    ~SshWorker() override;

    const ConnectionProfile& profile() const { return m_profile; }

    // Called (thread-safe, before connect) to supply secrets for "ask" mode.
    void supplyCredentials(const QString& password, const QString& passphrase);
    std::shared_ptr<ISftpSession> createSftpSession(QString* err); // any thread

public slots:
    void startConnect();
    void hostKeyDecision(bool accepted, bool trustAndSave);
    void provideAuthAnswers(const QStringList& answers);
    void disconnectSession();
    void openTerminal(int cols, int rows);
    void channelWrite(int cid, const QByteArray& data);
    void channelResize(int cid, int cols, int rows);
    void channelClose(int cid);
    void runExec(const QByteArray& tag, const QString& command);
    void cancelExec(const QByteArray& tag);
    void startForward(const eclipse::ForwardRuleSpec& rule);
    void stopForward(const QString& ruleId);
    void pollTick();

signals:
    void stateChanged(int newState);
    void hostKeyPresented(const eclipse::HostKeyInfo& info, const QString& checkError);
    void authPromptReceived(const QStringList& prompts, const QVector<bool>& echoes);
    void connected(const eclipse::EngineInfo& info);
    void authFailed(const QString& friendly, const QString& technical, const QString& hint);
    void disconnected(const QString& reason, bool byRequest);
    void terminalOpened(int cid, int cols, int rows);
    void channelData(int cid, const QByteArray& data, bool isStderr);
    void channelClosed(int cid);
    void execOutput(const QByteArray& tag, const QByteArray& data, bool isStderr);
    void execFinished(const QByteArray& tag, int exitCode);
    void latencyChanged(int ms);
    void forwardStatusChanged(const QString& ruleId, const QString& status, const QString& error);

private:
    struct ChannelEntry
    {
        std::shared_ptr<IChannel> channel;
        enum Kind { Shell, Exec, Pipe } kind = Shell;
        QTcpSocket* socket = nullptr;      // Pipe: the local/accepted side
        bool handshakeDone = false;
        QByteArray socksBuffer;
        QString socksHost;
        int socksPort = 0;
        QString forwardRuleId;
        QByteArray execTag;
    };

    Outcome runAuthentication();
    bool startForwardInternal(const ForwardRuleSpec& rule, QString* err);
    void stopForwardInternal(const QString& ruleId);
    void teardown(const QString& reason, bool byRequest);
    void setState(SessionState s);
    void updateLatency();

    ConnectionProfile m_profile;
    std::unique_ptr<ISshEngine> m_engine;

    SessionState m_state = SessionState::Idle;
    QMutex m_decisionMutex;
    QWaitCondition m_hostKeyDecision;
    bool m_hostKeyAccepted = false;
    bool m_hostKeySaved = false;
    bool m_hostKeyDecided = false;
    QMutex m_promptMutex;
    QWaitCondition m_promptCond;
    QStringList m_promptAnswers;
    bool m_promptReady = false;

    QString m_password;      // runtime ("ask" mode); wiped after auth
    QString m_passphrase;

    QHash<int, ChannelEntry> m_channels;
    int m_nextChannelId = 1;
    QHash<QString, int> m_remoteListeners;               // ruleId -> marker entry id
    QHash<QString, QTcpServer*> m_forwardServers;        // local/dynamic ruleId -> server
    QHash<QString, ForwardRuleSpec> m_remoteRules;       // remote ruleId -> spec
    QTimer* m_pollTimer = nullptr;
    QTimer* m_keepAliveTimer = nullptr;
    std::unique_ptr<class JumpConnector> m_jump;
    bool m_byRequest = false;
};

} // namespace eclipse
