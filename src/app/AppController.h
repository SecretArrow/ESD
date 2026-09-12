#pragma once

#include <QAbstractListModel>
#include <QObject>
#include <QQmlEngine>
#include <QVariantMap>
#include <QVector>

#include "../core/commands/CommandRegistry.h"
#include "../core/credentials/CredentialManager.h"
#include "../core/logging/Logger.h"
#include "../core/profiles/ProfileStore.h"
#include "../core/settings/Settings.h"
#include "../core/ThemeManager.h"
#include "../core/snippets/SnippetStore.h"
#include "../core/shortcuts/ShortcutManager.h"
#include "../ssh/Diagnostics.h"
#include "../ssh/ForwardManager.h"
#include "../ssh/SshSession.h"
#include "../transfer/TransferManager.h"

namespace eclipse {

// ---------------------------------------------------------------------------
// SessionManager - the open sessions (tabs) model for QML.
// ---------------------------------------------------------------------------
class SessionManager : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles {
        SessionRole = Qt::UserRole + 1, SessionIdRole, ProfileIdRole, NameRole,
        HostRole, PortRole, UserRole, StateRole, LatencyRole, UptimeRole, EngineRole, ConnectedRole
    };

    static SessionManager& instance();

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE QVariant get(int row) const;
    Q_INVOKABLE SshSession* sessionAt(int row) const;
    SshSession* sessionById(qint64 id) const;
    QVector<SshSession*> sessions() const { return m_sessions; }

    void addSession(SshSession* session);
    Q_INVOKABLE void closeSession(int row);

signals:
    void sessionStateChanged();
    void sessionListChanged();

public slots:
    void onSessionDataChanged();

private:
    SessionManager() = default;
    QVector<SshSession*> m_sessions;
};

// ---------------------------------------------------------------------------
// ProfileDraft - editable profile view for the connection editor dialog.
// ---------------------------------------------------------------------------
class ProfileDraft : public QObject
{
    Q_OBJECT
    Q_PROPERTY(qint64 id MEMBER id FINAL)
    Q_PROPERTY(QString name MEMBER name NOTIFY changed)
    Q_PROPERTY(QString group MEMBER group NOTIFY changed)
    Q_PROPERTY(QString tags MEMBER tags NOTIFY changed)
    Q_PROPERTY(bool favorite MEMBER favorite NOTIFY changed)
    Q_PROPERTY(QString host MEMBER host NOTIFY changed)
    Q_PROPERTY(int port MEMBER port NOTIFY changed)
    Q_PROPERTY(QString username MEMBER username NOTIFY changed)
    Q_PROPERTY(QString authMethod MEMBER authMethod NOTIFY changed)
    Q_PROPERTY(QString privateKeyPath MEMBER privateKeyPath NOTIFY changed)
    Q_PROPERTY(QString engine MEMBER engine NOTIFY changed)
    Q_PROPERTY(QString jumpHosts MEMBER jumpHosts NOTIFY changed)
    Q_PROPERTY(QString proxyType MEMBER proxyType NOTIFY changed)
    Q_PROPERTY(QString proxyHost MEMBER proxyHost NOTIFY changed)
    Q_PROPERTY(int proxyPort MEMBER proxyPort NOTIFY changed)
    Q_PROPERTY(QString proxyUser MEMBER proxyUser NOTIFY changed)
    Q_PROPERTY(int keepAliveSeconds MEMBER keepAliveSeconds NOTIFY changed)
    Q_PROPERTY(bool compression MEMBER compression NOTIFY changed)
    Q_PROPERTY(int connectTimeoutMs MEMBER connectTimeoutMs NOTIFY changed)
    Q_PROPERTY(bool allowKeyboardInteractive MEMBER allowKeyboardInteractive NOTIFY changed)
    Q_PROPERTY(bool autoReconnect MEMBER autoReconnect NOTIFY changed)
    Q_PROPERTY(QString startupCommands MEMBER startupCommands NOTIFY changed)
    Q_PROPERTY(QString environment MEMBER environment NOTIFY changed)
    Q_PROPERTY(QString sftpDefaultRemoteDir MEMBER sftpDefaultRemoteDir NOTIFY changed)
    Q_PROPERTY(QString sftpDefaultLocalDir MEMBER sftpDefaultLocalDir NOTIFY changed)
    Q_PROPERTY(QString transferProtocol MEMBER transferProtocol NOTIFY changed)
    Q_PROPERTY(QVariantList forwardingRules MEMBER forwardingRules NOTIFY changed)

    // Task 2-c: connection-layer fields
    Q_PROPERTY(QString connectionType MEMBER connectionType NOTIFY changed)
    Q_PROPERTY(QString kexAlgorithms MEMBER kexAlgorithms NOTIFY changed)
    Q_PROPERTY(bool x11Forward MEMBER x11Forward NOTIFY changed)
    Q_PROPERTY(int x11Screen MEMBER x11Screen NOTIFY changed)
    Q_PROPERTY(QString serialPort MEMBER serialPort NOTIFY changed)
    Q_PROPERTY(int serialBaud MEMBER serialBaud NOTIFY changed)
    Q_PROPERTY(int serialDataBits MEMBER serialDataBits NOTIFY changed)
    Q_PROPERTY(QString serialParity MEMBER serialParity NOTIFY changed)
    Q_PROPERTY(int serialStopBits MEMBER serialStopBits NOTIFY changed)
    Q_PROPERTY(int serialFlowControl MEMBER serialFlowControl NOTIFY changed)

public:
    explicit ProfileDraft(QObject* parent = nullptr) : QObject(parent) {}
    static ProfileDraft* fromProfile(const ConnectionProfile& p);
    ConnectionProfile toProfile() const;

signals:
    void changed();

public:
    qint64 id = 0;
    QString name, group, tags, host, username, authMethod = QStringLiteral("password");
    QString privateKeyPath, engine = QStringLiteral("auto"), jumpHosts;
    QString proxyType = QStringLiteral("none"), proxyHost, proxyUser;
    int proxyPort = 0, port = 22;
    int keepAliveSeconds = 15, connectTimeoutMs = 15000;
    bool favorite = false, compression = false, allowKeyboardInteractive = true;
    bool autoReconnect = true;
    QString startupCommands, environment, sftpDefaultRemoteDir, sftpDefaultLocalDir;
    QString transferProtocol = QStringLiteral("sftp");
    QVariantList forwardingRules;

    // Task 2-c additions (defaults mirror ConnectionProfile)
    QString connectionType = QStringLiteral("ssh");
    QString kexAlgorithms;
    bool x11Forward = false;
    int x11Screen = 0;
    QString serialPort;
    int serialBaud = 115200;
    int serialDataBits = 8;
    QString serialParity = QStringLiteral("none");
    int serialStopBits = 1;
    int serialFlowControl = 0;
};

// ---------------------------------------------------------------------------
// AppController - root object exposed to QML as "App".
// ---------------------------------------------------------------------------
class AppController : public QObject
{
    Q_OBJECT
    Q_PROPERTY(ProfileStore* profiles READ profiles CONSTANT)
    Q_PROPERTY(SessionManager* sessions READ sessions CONSTANT)
    Q_PROPERTY(Settings* settings READ settings CONSTANT)
    Q_PROPERTY(ShortcutManager* shortcuts READ shortcuts CONSTANT)
    Q_PROPERTY(CredentialManager* credentials READ credentials CONSTANT)
    Q_PROPERTY(ThemeManager* theme READ theme CONSTANT)
    Q_PROPERTY(QString version READ version CONSTANT)
    Q_PROPERTY(bool firstRunDone READ firstRunDone WRITE setFirstRunDone NOTIFY firstRunChanged)

public:
    static AppController* create(QQmlEngine* engine, QJSEngine* scriptEngine);
    static AppController& instance();

    ProfileStore* profiles() const { return &ProfileStore::instance(); }
    SessionManager* sessions() const { return &SessionManager::instance(); }
    Settings* settings() const { return &Settings::instance(); }
    ShortcutManager* shortcuts() const { return &ShortcutManager::instance(); }
    CredentialManager* credentials() const { return &CredentialManager::instance(); }
    ThemeManager* theme() const { return &ThemeManager::instance(); }
    QString version() const;
    bool firstRunDone() const;
    void setFirstRunDone(bool done);

    // ---- connections ------------------------------------------------------
    Q_INVOKABLE ProfileDraft* draftProfile(qint64 id);      // id 0 = new
    Q_INVOKABLE qint64 saveProfile(ProfileDraft* draft, const QString& password,
                                   const QString& passphrase, bool rememberSecrets);
    Q_INVOKABLE void deleteProfile(qint64 id);
    Q_INVOKABLE void duplicateProfile(qint64 id);
    Q_INVOKABLE void toggleFavorite(qint64 id);
    Q_INVOKABLE bool hasStoredSecret(qint64 id) const;
    Q_INVOKABLE bool needsPasswordPrompt(qint64 id) const;

    // ---- sessions ---------------------------------------------------------
    Q_INVOKABLE qint64 connectProfile(qint64 profileId, const QString& password = {},
                                      const QString& passphrase = {});
    Q_INVOKABLE qint64 quickConnect(const QVariantMap& params); // returns sessionId (0 = fail)
    Q_INVOKABLE void disconnectSession(qint64 sessionId);
    Q_INVOKABLE void openTerminalFor(qint64 sessionId);
    Q_INVOKABLE void openSftpFor(qint64 sessionId, const QVariantMap& panelState);
    Q_INVOKABLE SshSession* session(qint64 sessionId) const;

    // ---- console (Telnet/Serial) sessions ----------------------------------
    // Creates (and owns) a TelnetSession or SerialSession from a profile map
    // (either { profileId } for a stored profile or explicit fields such as
    // connectionType/host/port/serialPort/serialBaud/...). The returned
    // object exposes the console attachment contract: bytesReceived(const
    // QByteArray&), write(const QByteArray&), ptyResize(int,int).
    Q_INVOKABLE QObject* openConsoleSession(const QVariantMap& profile);
    Q_INVOKABLE void closeConsoleSession(qint64 sessionId);
    Q_INVOKABLE QObject* consoleSession(qint64 sessionId) const;

    // ---- import/export ----------------------------------------------------
    Q_INVOKABLE QString importOpenSshConfig(const QString& path);
    Q_INVOKABLE QString exportProfilesJson(const QString& path);
    Q_INVOKABLE QString importProfilesJson(const QString& path);
    Q_INVOKABLE QString backupProfiles(const QString& path);
    Q_INVOKABLE QString restoreBackup(const QString& path);

    // ---- import/sync (agent 2-b API contracts) -----------------------------
    // All return { ok: bool, imported: int, skipped: int, error: QString } and
    // degrade to an error map (never crash) when the backend is unavailable.
    Q_INVOKABLE QVariantMap importPuttySessions();
    Q_INVOKABLE QVariantMap exportProfileBundle(const QUrl& fileUrl, const QString& passphrase);
    Q_INVOKABLE QVariantMap importProfileBundle(const QUrl& fileUrl, const QString& passphrase);
    Q_INVOKABLE QVariantMap importOpenSshKnownHosts(const QUrl& fileUrl);

    // ---- diagnostics / tools ----------------------------------------------
    // ---- clipboard / archive / preview -------------------------------------
    Q_INVOKABLE void copyToClipboard(const QString& text);
    Q_INVOKABLE QVariantList queryCommands(const QString& text) const;
    Q_INVOKABLE QVariantList listArchive(qint64 sessionId, const QString& remotePath);
    Q_INVOKABLE QString extractArchive(qint64 sessionId, const QString& remotePath,
                                       const QStringList& entries, const QString& localDir);
    Q_INVOKABLE QVariantMap previewTextFile(qint64 sessionId, const QString& remotePath);
    Q_INVOKABLE void runDiagnostics(qint64 profileId, const QString& password);
    Q_INVOKABLE QVariantMap developerInfo() const;
    Q_INVOKABLE void quitApplication();

    // Global command execution (palette / shortcuts) - QML listens to
    // commandRequested(id) and opens the matching UI.
    Q_INVOKABLE void triggerCommand(const QString& id) { emit commandRequested(id); }

signals:
    void commandRequested(const QString& id);
    void firstRunChanged();
    void hostKeyNeeded(qint64 sessionId, const QVariantMap& keyInfo, bool isChanged);
    void authPromptNeeded(qint64 sessionId, const QVariantList& prompts);
    void credentialsNeeded(qint64 sessionId, const QString& host);
    void connected(qint64 sessionId);
    void authFailed(qint64 sessionId, const QString& friendly, const QString& technical, const QString& hint);
    void sessionDisconnected(qint64 sessionId, const QString& reason, bool byRequest);
    void reconnecting(qint64 sessionId, int attempt, int maxAttempts);
    void notify(const QString& title, const QString& body, bool isError);
    void diagnosticsStep(const QString& step, bool ok, const QString& detail, int ms);
    void diagnosticsKex(const QString& kex, bool postQuantumReady, const QString& note);
    void diagnosticsFinished(bool allOk);

private:
    explicit AppController(QObject* parent = nullptr);
    void wireSession(SshSession* session);
    static AppController* s_instance;

    // Console (Telnet/Serial) session registry; objects are parented to the
    // controller and deleteLater()'d via closeConsoleSession().
    QHash<qint64, QObject*> m_consoleSessions;
    qint64 m_nextConsoleSessionId = 1;
};

} // namespace eclipse
