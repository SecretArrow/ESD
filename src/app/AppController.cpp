#include "AppController.h"

#include <QClipboard>
#include <QThread>
#include <QGuiApplication>
#include <QtQml>
#include <EclipseVersion.h>

#include "../archive/Archive.h"
#include "../net/SerialSession.h"
#include "../net/TelnetSession.h"

#include <QApplication>
#include <QDateTime>
#include <QDesktopServices>
#include <QDir>
#include <QProcess>
#include <QSystemTrayIcon>
#include <QUrl>

#include "../common/Secure.h"
#include "../common/Utils.h"
#include "../core/Database.h"
#include "../core/profiles/ProfileStore.h"
#include "../ssh/HostKeyManager.h"
#include "../ssh/backends/libssh/LibsshEngine.h"
#include "../ssh/backends/libssh2/Libssh2Engine.h"

// Agent 2-b backend contracts (src/core/profiles/PuttyImporter.h,
// src/core/profiles/ProfileBundle.h). The files may not exist yet in every
// build; __has_include keeps the app compiling and the call sites degrade to
// an error map until the integrator lands the backend.
#if __has_include("../core/profiles/PuttyImporter.h")
#include "../core/profiles/PuttyImporter.h"
#define ECLIPSE_HAVE_PUTTY_IMPORTER 1
#endif
#if __has_include("../core/profiles/ProfileBundle.h")
#include "../core/profiles/ProfileBundle.h"
#define ECLIPSE_HAVE_PROFILE_BUNDLE 1
#endif

namespace eclipse {

// ---------------------------------------------------------------------------
// SessionManager
// ---------------------------------------------------------------------------
SessionManager& SessionManager::instance()
{
    static SessionManager m;
    return m;
}

int SessionManager::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_sessions.size();
}

QHash<int, QByteArray> SessionManager::roleNames() const
{
    return {
        { SessionRole, "session" },
        { SessionIdRole, "sessionId" },
        { ProfileIdRole, "profileId" },
        { NameRole, "name" },
        { HostRole, "host" },
        { PortRole, "port" },
        { UserRole, "username" },
        { StateRole, "state" },
        { LatencyRole, "latencyMs" },
        { UptimeRole, "uptimeSec" },
        { EngineRole, "engineName" },
        { ConnectedRole, "connected" },
    };
}

QVariant SessionManager::data(const QModelIndex& index, int role) const
{
    const int row = index.row();
    if (row < 0 || row >= m_sessions.size())
        return {};
    SshSession* s = m_sessions[row];
    switch (role) {
    case SessionRole: return QVariant::fromValue(s);
    case SessionIdRole: return s->sessionId();
    case ProfileIdRole: return s->profileId();
    case NameRole: return s->name();
    case HostRole: return s->host();
    case PortRole: return s->port();
    case UserRole: return s->username();
    case StateRole: return s->stateName();
    case LatencyRole: return s->latencyMs();
    case UptimeRole: return s->uptimeSec();
    case EngineRole: return s->engineName();
    case ConnectedRole: return s->isConnected();
    }
    return {};
}

QVariant SessionManager::get(int row) const
{
    QVariantMap m;
    if (row < 0 || row >= m_sessions.size())
        return m;
    for (int r = SessionRole; r <= ConnectedRole; ++r) {
        const auto roles = roleNames();
        m.insert(roles.value(r), data(index(row), r));
    }
    return m;
}

SshSession* SessionManager::sessionAt(int row) const
{
    return (row >= 0 && row < m_sessions.size()) ? m_sessions[row] : nullptr;
}

SshSession* SessionManager::sessionById(qint64 id) const
{
    for (SshSession* s : m_sessions)
        if (s->sessionId() == id)
            return s;
    return nullptr;
}

void SessionManager::addSession(SshSession* session)
{
    beginInsertRows({}, m_sessions.size(), m_sessions.size());
    m_sessions.append(session);
    endInsertRows();
    emit sessionListChanged();
}

void SessionManager::closeSession(int row)
{
    if (row < 0 || row >= m_sessions.size())
        return;
    SshSession* s = m_sessions[row];
    s->disconnect();
    ForwardManager::instance().unregisterWorker(s->sessionId());
    beginRemoveRows({}, row, row);
    m_sessions.remove(row);
    endRemoveRows();
    s->deleteLater();
    emit sessionListChanged();
}

void SessionManager::onSessionDataChanged()
{
    auto* s = sender();
    for (int i = 0; i < m_sessions.size(); ++i) {
        if (m_sessions[i] == s) {
            const QModelIndex idx = index(i);
            emit dataChanged(idx, idx);
            break;
        }
    }
    emit sessionStateChanged();
}

// ---------------------------------------------------------------------------
// ProfileDraft
// ---------------------------------------------------------------------------
ProfileDraft* ProfileDraft::fromProfile(const ConnectionProfile& p)
{
    auto* d = new ProfileDraft();
    d->id = p.id;
    d->name = p.name;
    d->group = p.group;
    d->tags = p.tags.join(QStringLiteral(", "));
    d->favorite = p.favorite;
    d->host = p.host;
    d->port = p.port;
    d->username = p.username;
    d->authMethod = p.authMethod;
    d->privateKeyPath = p.privateKeyPath;
    d->engine = p.engine;
    d->jumpHosts = p.jumpHosts.join(QStringLiteral(" -> "));
    d->proxyType = p.proxyType;
    d->proxyHost = p.proxyHost;
    d->proxyPort = p.proxyPort;
    d->proxyUser = p.proxyUser;
    d->keepAliveSeconds = p.keepAliveSeconds;
    d->compression = p.compression;
    d->connectTimeoutMs = p.connectTimeoutMs;
    d->allowKeyboardInteractive = p.allowKeyboardInteractive;
    d->autoReconnect = p.autoReconnect;
    d->startupCommands = p.terminal.startupCommands.join(QLatin1Char('\n'));
    d->environment = p.environment.join(QLatin1Char('\n'));
    d->sftpDefaultRemoteDir = p.sftpDefaultRemoteDir;
    d->sftpDefaultLocalDir = p.sftpDefaultLocalDir;
    d->transferProtocol = p.transferProtocol;
    d->connectionType = p.connectionType;
    d->kexAlgorithms = p.kexAlgorithms;
    d->x11Forward = p.x11Forward;
    d->x11Screen = p.x11Screen;
    d->serialPort = p.serialPort;
    d->serialBaud = p.serialBaud;
    d->serialDataBits = p.serialDataBits;
    d->serialParity = p.serialParity;
    d->serialStopBits = p.serialStopBits;
    d->serialFlowControl = p.serialFlowControl;
    for (const auto& r : p.forwardingRules) {
        d->forwardingRules.append(QVariantMap {
            { "id", r.id },
            { "name", r.name },
            { "type", r.type },
            { "listenAddress", r.listenAddress },
            { "listenPort", r.listenPort },
            { "destHost", r.destHost },
            { "destPort", r.destPort },
            { "autoStart", r.autoStart },
            { "enabled", r.enabled },
        });
    }
    return d;
}

ConnectionProfile ProfileDraft::toProfile() const
{
    ConnectionProfile p;
    p.id = id;
    p.name = name;
    p.group = group;
    for (const auto& t : tags.split(QLatin1Char(','), Qt::SkipEmptyParts))
        p.tags.append(t.trimmed());
    p.favorite = favorite;
    p.host = host.trimmed();
    p.port = port;
    p.username = username;
    p.authMethod = authMethod;
    p.privateKeyPath = privateKeyPath;
    p.engine = engine;
    p.jumpHosts.clear();
    for (const auto& j : jumpHosts.split(QStringLiteral("->"), Qt::SkipEmptyParts))
        p.jumpHosts.append(j.trimmed());
    p.proxyType = proxyType;
    p.proxyHost = proxyHost;
    p.proxyPort = proxyPort;
    p.proxyUser = proxyUser;
    p.keepAliveSeconds = keepAliveSeconds;
    p.compression = compression;
    p.connectTimeoutMs = connectTimeoutMs;
    p.allowKeyboardInteractive = allowKeyboardInteractive;
    p.autoReconnect = autoReconnect;
    p.terminal.startupCommands = startupCommands.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    p.environment = environment.split(QLatin1Char('\n'), Qt::SkipEmptyParts);
    p.sftpDefaultRemoteDir = sftpDefaultRemoteDir;
    p.sftpDefaultLocalDir = sftpDefaultLocalDir;
    p.transferProtocol = transferProtocol;
    p.connectionType = connectionType;
    p.kexAlgorithms = kexAlgorithms;
    p.x11Forward = x11Forward;
    p.x11Screen = x11Screen;
    p.serialPort = serialPort;
    p.serialBaud = serialBaud;
    p.serialDataBits = serialDataBits;
    p.serialParity = serialParity;
    p.serialStopBits = serialStopBits;
    p.serialFlowControl = serialFlowControl;
    p.forwardingRules.clear();
    for (const auto& v : forwardingRules) {
        const QVariantMap m = v.toMap();
        ForwardRuleSpec r;
        r.id = m.value(QStringLiteral("id")).toString();
        if (r.id.isEmpty())
            r.id = QString::number(QDateTime::currentMSecsSinceEpoch() + p.forwardingRules.size());
        r.name = m.value(QStringLiteral("name")).toString();
        r.type = m.value(QStringLiteral("type")).toString();
        if (r.type.isEmpty())
            r.type = QStringLiteral("local");
        r.listenAddress = m.value(QStringLiteral("listenAddress")).toString();
        if (r.listenAddress.isEmpty())
            r.listenAddress = QStringLiteral("127.0.0.1");
        r.listenPort = m.value(QStringLiteral("listenPort")).toInt();
        r.destHost = m.value(QStringLiteral("destHost")).toString();
        r.destPort = m.value(QStringLiteral("destPort")).toInt();
        r.autoStart = m.value(QStringLiteral("autoStart")).toBool();
        r.enabled = m.value(QStringLiteral("enabled")).toBool();
        p.forwardingRules.append(r);
    }
    return p;
}

// ---------------------------------------------------------------------------
// AppController
// ---------------------------------------------------------------------------
AppController* AppController::s_instance = nullptr;

AppController& AppController::instance()
{
    return *s_instance;
}

AppController* AppController::create(QQmlEngine*, QJSEngine*)
{
    s_instance = new AppController();
    return s_instance;
}

AppController::AppController(QObject* parent)
    : QObject(parent)
{
    // QML type registration for the console session classes (Task 2-c).
    // Same import URI used by the other app types (see main.cpp:
    // qmlRegisterType<TermItem>("Eclipse.Internal", 1, 0, ...)).
    qmlRegisterType<TelnetSession>("Eclipse.Internal", 1, 0, "TelnetSession");
    qmlRegisterType<SerialSession>("Eclipse.Internal", 1, 0, "SerialSession");

    Database::instance().open();
    ProfileStore::instance().init();
    SnippetStore::instance().init();

    connect(&Settings::instance(), &Settings::changed, this, [this](const QString& key) {
        if (key.startsWith(QStringLiteral("appearance/")))
            ThemeManager::instance().applyFromSettings();
        if (key == QStringLiteral("advanced/debugMode"))
            Logger::instance().enableDebugMode(Settings::instance().debugMode());
    });
    ThemeManager::instance().applyFromSettings();
    Logger::instance().enableDebugMode(Settings::instance().debugMode());
}

QString AppController::version() const
{
    return QStringLiteral(ECLIPSE_VERSION);
}

bool AppController::firstRunDone() const
{
    return Settings::instance().value(QStringLiteral("general/firstRunDone"), false).toBool();
}

void AppController::setFirstRunDone(bool done)
{
    Settings::instance().setValue(QStringLiteral("general/firstRunDone"), done);
    emit firstRunChanged();
}

// ---- connections -----------------------------------------------------------
ProfileDraft* AppController::draftProfile(qint64 id)
{
    if (id <= 0) {
        auto* d = ProfileDraft::fromProfile(ConnectionProfile {});
        return d;
    }
    return ProfileDraft::fromProfile(ProfileStore::instance().get(id));
}

qint64 AppController::saveProfile(ProfileDraft* draft, const QString& password,
                                  const QString& passphrase, bool rememberSecrets)
{
    ConnectionProfile p = draft->toProfile();
    qint64 outId;
    if (p.id > 0) {
        ProfileStore::instance().update(p);
        outId = p.id;
    } else {
        outId = ProfileStore::instance().add(p);
    }
    if (rememberSecrets && !password.isEmpty())
        CredentialManager::instance().saveSecret(QStringLiteral("profile:%1:password").arg(outId),
                                                 password, true);
    if (rememberSecrets && !passphrase.isEmpty())
        CredentialManager::instance().saveSecret(QStringLiteral("profile:%1:passphrase").arg(outId),
                                                 passphrase, true);
    if (rememberSecrets && !draft->proxyUser.isEmpty()) {
        // proxy password handled when provided through the same dialog field
    }
    return outId;
}

void AppController::deleteProfile(qint64 id)
{
    ProfileStore::instance().remove(id);
    CredentialManager::instance().deleteSecret(QStringLiteral("profile:%1:password").arg(id));
    CredentialManager::instance().deleteSecret(QStringLiteral("profile:%1:passphrase").arg(id));
}

void AppController::duplicateProfile(qint64 id)
{
    ProfileStore::instance().duplicate(id, {});
}

void AppController::toggleFavorite(qint64 id)
{
    ConnectionProfile p = ProfileStore::instance().get(id);
    p.favorite = !p.favorite;
    ProfileStore::instance().update(p);
}

bool AppController::hasStoredSecret(qint64 id) const
{
    return CredentialManager::instance().hasSecret(QStringLiteral("profile:%1:password").arg(id))
           || CredentialManager::instance().hasSecret(QStringLiteral("profile:%1:passphrase").arg(id));
}

bool AppController::needsPasswordPrompt(qint64 id) const
{
    const ConnectionProfile p = ProfileStore::instance().get(id);
    if (p.authMethod == QLatin1String("agent"))
        return false;
    return !hasStoredSecret(id);
}

// ---- sessions ---------------------------------------------------------------
qint64 AppController::connectProfile(qint64 profileId, const QString& password, const QString& passphrase)
{
    ConnectionProfile p = ProfileStore::instance().get(profileId);
    if (p.id == 0)
        return 0;
    auto* session = new SshSession(p, this);
    wireSession(session);
    SessionManager::instance().addSession(session);
    ForwardManager::instance().registerWorker(session->sessionId(), session->worker());
    session->connectTo(password, passphrase);
    return session->sessionId();
}

qint64 AppController::quickConnect(const QVariantMap& params)
{
    ConnectionProfile p;
    p.name = QStringLiteral("%1@%2").arg(params.value(QStringLiteral("username")).toString(),
                                         params.value(QStringLiteral("host")).toString());
    p.host = params.value(QStringLiteral("host")).toString();
    p.port = params.value(QStringLiteral("port")).toInt();
    if (p.port <= 0) p.port = 22;
    p.username = params.value(QStringLiteral("username")).toString();
    QString auth = params.value(QStringLiteral("authMethod")).toString();
    if (auth.isEmpty()) auth = QStringLiteral("password");
    p.authMethod = auth;
    if (params.value(QStringLiteral("saveAsProfile")).toBool()) {
        { const QString pn = params.value(QStringLiteral("profileName")).toString(); if (!pn.isEmpty()) p.name = pn; }
        const qint64 id = ProfileStore::instance().add(p);
        return connectProfile(id, params.value(QStringLiteral("password")).toString());
    }
    // ephemeral session
    auto* session = new SshSession(p, this);
    wireSession(session);
    SessionManager::instance().addSession(session);
    ForwardManager::instance().registerWorker(session->sessionId(), session->worker());
    session->connectTo(params.value(QStringLiteral("password")).toString(), {});
    return session->sessionId();
}

void AppController::disconnectSession(qint64 sessionId)
{
    if (SshSession* s = SessionManager::instance().sessionById(sessionId))
        s->disconnect();
}

void AppController::openTerminalFor(qint64 sessionId)
{
    if (SshSession* s = SessionManager::instance().sessionById(sessionId))
        s->openTerminal(80, 24);
}

void AppController::openSftpFor(qint64, const QVariantMap&)
{
    // The Files panel reads the session via App.session(); nothing to do here.
}

SshSession* AppController::session(qint64 sessionId) const
{
    return SessionManager::instance().sessionById(sessionId);
}

// ---- console (Telnet/Serial) sessions ---------------------------------------
QObject* AppController::openConsoleSession(const QVariantMap& map)
{
    // Either a stored profile id or explicit fields (quick console).
    ConnectionProfile p;
    const qint64 profileId = qlonglong(map.value(QStringLiteral("profileId")).toLongLong());
    if (profileId > 0)
        p = ProfileStore::instance().get(profileId);
    if (map.contains(QStringLiteral("connectionType")))
        p.connectionType = map.value(QStringLiteral("connectionType")).toString();
    if (map.contains(QStringLiteral("name")))
        p.name = map.value(QStringLiteral("name")).toString();
    if (map.contains(QStringLiteral("host")))
        p.host = map.value(QStringLiteral("host")).toString();
    if (map.contains(QStringLiteral("port")))
        p.port = map.value(QStringLiteral("port")).toInt();
    if (map.contains(QStringLiteral("serialPort")))
        p.serialPort = map.value(QStringLiteral("serialPort")).toString();
    if (map.contains(QStringLiteral("serialBaud")))
        p.serialBaud = map.value(QStringLiteral("serialBaud")).toInt();
    if (map.contains(QStringLiteral("serialDataBits")))
        p.serialDataBits = map.value(QStringLiteral("serialDataBits")).toInt();
    if (map.contains(QStringLiteral("serialParity")))
        p.serialParity = map.value(QStringLiteral("serialParity")).toString();
    if (map.contains(QStringLiteral("serialStopBits")))
        p.serialStopBits = map.value(QStringLiteral("serialStopBits")).toInt();
    if (map.contains(QStringLiteral("serialFlowControl")))
        p.serialFlowControl = map.value(QStringLiteral("serialFlowControl")).toInt();

    if (p.connectionType != QLatin1String("telnet") && p.connectionType != QLatin1String("serial")) {
        LOG_WARN(QStringLiteral("openConsoleSession: unsupported connectionType '%1'").arg(p.connectionType));
        return nullptr;
    }
    if (p.host.isEmpty() && p.serialPort.isEmpty()) {
        LOG_WARN(QStringLiteral("openConsoleSession: no host or serial device given"));
        return nullptr;
    }
    if (p.connectionType == QLatin1String("telnet") && p.port <= 0)
        p.port = 23;

    const qint64 sessionId = m_nextConsoleSessionId++;
    QObject* session = nullptr;
    if (p.connectionType == QLatin1String("telnet")) {
        auto* telnet = new TelnetSession(this);
        telnet->connectToProfile(QVariantMap {
            { "host", p.host },
            { "port", p.port },
            { "name", p.label() },
        });
        session = telnet;
    } else {
        auto* serial = new SerialSession(this);
        serial->connectToProfile(QVariantMap {
            { "serialPort", p.serialPort },
            { "serialBaud", p.serialBaud },
            { "serialDataBits", p.serialDataBits },
            { "serialParity", p.serialParity },
            { "serialStopBits", p.serialStopBits },
            { "serialFlowControl", p.serialFlowControl },
            { "name", p.label() },
        });
        session = serial;
    }

    m_consoleSessions.insert(sessionId, session);
    connect(session, &QObject::destroyed, this, [this, sessionId]() {
        m_consoleSessions.remove(sessionId);
    });
    LOG_CONN(QStringLiteral("Console session %1 opened (%2 %3)")
                 .arg(sessionId).arg(p.connectionType, p.label()));
    return session;
}

void AppController::closeConsoleSession(qint64 sessionId)
{
    if (QObject* s = m_consoleSessions.take(sessionId))
        s->deleteLater();
}

QObject* AppController::consoleSession(qint64 sessionId) const
{
    return m_consoleSessions.value(sessionId, nullptr);
}

void AppController::wireSession(SshSession* session)
{
    connect(session, &SshSession::hostKeyNeeded, this,
            [this](const QVariantMap& keyInfo, bool changed) {
                emit hostKeyNeeded(qobject_cast<SshSession*>(sender())->sessionId(), keyInfo, changed);
            });
    connect(session, &SshSession::authPromptNeeded, this, [this](const QVariantList& prompts) {
        emit authPromptNeeded(qobject_cast<SshSession*>(sender())->sessionId(), prompts);
    });
    connect(session, &SshSession::authFailed, this,
            [this](const QString& f, const QString& t, const QString& h) {
                SshSession* s = qobject_cast<SshSession*>(sender());
                emit authFailed(s->sessionId(), f, t, h);
                emit notify(QStringLiteral("Authentication failed"), QStringLiteral("%1: %2").arg(s->name(), f), true);
            });
    connect(session, &SshSession::disconnected, this,
            [this](const QString& reason, bool byRequest) {
                SshSession* s = qobject_cast<SshSession*>(sender());
                emit sessionDisconnected(s->sessionId(), reason, byRequest);
                emit notify(QStringLiteral("Session closed"),
                            QStringLiteral("%1: %2").arg(s->name(), reason), !byRequest);
            });
    connect(session, &SshSession::reconnecting, this,
            [this](int attempt, int max) {
                SshSession* s = qobject_cast<SshSession*>(sender());
                emit reconnecting(s->sessionId(), attempt, max);
            });
    SessionManager* smgr = &SessionManager::instance();
    connect(session, &SshSession::stateChanged, smgr,
            [smgr](const QString&) { smgr->onSessionDataChanged(); });
    connect(session, &SshSession::latencyChanged, smgr,
            [smgr](int) { smgr->onSessionDataChanged(); });
    connect(session, &SshSession::connectedInfoChanged, this, [this](const QVariantMap&) {
        SshSession* s = qobject_cast<SshSession*>(sender());
        emit connected(s->sessionId());
        emit notify(QStringLiteral("Connected"), QStringLiteral("%1 (%2@%3)").arg(s->name(), s->username(), s->host()), false);
    });
    const qint64 fsid = session->sessionId();
    connect(session, &SshSession::forwardStatusChanged, &ForwardManager::instance(),
            [fsid](const QString& ruleId, const QString& status, const QString& error) {
                ForwardManager::instance().onForwardStatus(fsid, ruleId, status, error);
            });
}

// ---- import/export -----------------------------------------------------------
QString AppController::importOpenSshConfig(const QString& path)
{
    return ProfileStore::instance().importFromOpenSshConfig(path);
}

QString AppController::exportProfilesJson(const QString& path)
{
    return ProfileStore::instance().exportToJson(path, true);
}

QString AppController::importProfilesJson(const QString& path)
{
    return ProfileStore::instance().importFromJson(path);
}

QString AppController::backupProfiles(const QString& path)
{
    return ProfileStore::instance().backupToFile(path);
}

QString AppController::restoreBackup(const QString& path)
{
    return ProfileStore::instance().restoreFromFile(path);
}

// ---- import/sync (agent 2-b API contracts) ------------------------------------
namespace {

QVariantMap importResult(bool ok, int imported, int skipped, const QString& error)
{
    return {
        { "ok", ok },
        { "imported", imported },
        { "skipped", skipped },
        { "error", error },
    };
}

} // namespace

QVariantMap AppController::importPuttySessions()
{
#ifdef ECLIPSE_HAVE_PUTTY_IMPORTER
    // Contract (agent 2-b): static QVector<ConnectionProfile>
    // PuttyImporter::importFromWindowsRegistry(QString* errorOut).
    QString err;
    const QVector<ConnectionProfile> imported = PuttyImporter::importFromWindowsRegistry(&err);
    int added = 0;
    int skipped = 0;
    for (const ConnectionProfile& p : imported) {
        if (p.host.isEmpty()) {
            ++skipped;
            continue;
        }
        if (ProfileStore::instance().add(p) > 0)
            ++added;
        else
            ++skipped;
    }
    LOG_APP(QStringLiteral("PuTTY import: %1 added, %2 skipped%3")
                .arg(added).arg(skipped)
                .arg(err.isEmpty() ? QString() : QStringLiteral(" - ") + err));
    return importResult(err.isEmpty(), added, skipped, err);
#else
    LOG_WARN(QStringLiteral("PuTTY import requested but PuttyImporter is not available yet"));
    return importResult(false, 0, 0,
                        QStringLiteral("PuTTY import backend is not available in this build (PuttyImporter missing)."));
#endif
}

QVariantMap AppController::exportProfileBundle(const QUrl& fileUrl, const QString& passphrase)
{
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
#ifdef ECLIPSE_HAVE_PROFILE_BUNDLE
    // Real API (agent 2-b): static bool ProfileBundle::exportToFile(
    //   const QString& path, const QList<ConnectionProfile>& profiles,
    //   const QString& passphrase, QString* errorOut);
    QString err;
    const bool ok = ProfileBundle::exportToFile(path, ProfileStore::instance().all(),
                                                passphrase, &err);
    return importResult(ok, ok ? ProfileStore::instance().count() : 0, 0, err);
#else
    LOG_WARN(QStringLiteral("Profile bundle export requested but ProfileBundle is not available yet"));
    return importResult(false, 0, 0,
                        QStringLiteral("Profile bundle backend is not available in this build (ProfileBundle missing)."));
#endif
}

QVariantMap AppController::importProfileBundle(const QUrl& fileUrl, const QString& passphrase)
{
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
#ifdef ECLIPSE_HAVE_PROFILE_BUNDLE
    // Real API (agent 2-b): static QList<ConnectionProfile>
    // ProfileBundle::importFromFile(const QString& path, const QString& passphrase,
    //                               QString* errorOut).
    QString err;
    const QVector<ConnectionProfile> imported = ProfileBundle::importFromFile(path, passphrase, &err);
    int added = 0;
    int skipped = 0;
    for (const ConnectionProfile& p : imported) {
        if (p.host.isEmpty()) {
            ++skipped;
            continue;
        }
        if (ProfileStore::instance().add(p) > 0)
            ++added;
        else
            ++skipped;
    }
    return importResult(err.isEmpty(), added, skipped, err);
#else
    LOG_WARN(QStringLiteral("Profile bundle import requested but ProfileBundle is not available yet"));
    return importResult(false, 0, 0,
                        QStringLiteral("Profile bundle backend is not available in this build (ProfileBundle missing)."));
#endif
}

QVariantMap AppController::importOpenSshKnownHosts(const QUrl& fileUrl)
{
    const QString path = fileUrl.isLocalFile() ? fileUrl.toLocalFile() : fileUrl.toString();
    // Contract (agent 2-b): int HostKeyManager::importOpenSshKnownHosts(
    //   const QString& openSshPath, QString* errorOut) on the existing
    // HostKeyManager class (src/ssh/HostKeyManager.h). INTEGRATION NOTE: this
    // call site requires agent 2-b's method to be present at compile time.
    QString err;
    const int imported = HostKeyManager().importOpenSshKnownHosts(path, &err);
    if (imported < 0 || !err.isEmpty())
        return importResult(false, 0, 0, err.isEmpty()
                                                ? QStringLiteral("Importing known_hosts entries failed.")
                                                : err);
    return importResult(true, imported, 0, {});
}

// ---- clipboard / archive / preview ---------------------------------------------
void AppController::copyToClipboard(const QString& text)
{
    QGuiApplication::clipboard()->setText(text);
}

QVariantList AppController::queryCommands(const QString& text) const
{
    QVariantList out;
    const auto cmds = CommandRegistry::instance().query(text);
    for (const auto& c : cmds)
        out.append(QVariantMap { { "id", c.id }, { "title", c.title }, { "category", c.category } });
    return out;
}

QVariantList AppController::listArchive(qint64 sessionId, const QString& remotePath)
{
    QVariantList out;
    SshSession* s = SessionManager::instance().sessionById(sessionId);
    if (!s)
        return out;
    QString err;
    std::shared_ptr<ISftpSession> sftp = s->createSftpSession(&err);
    if (!sftp)
        return out;
    QVector<ArchiveEntry> entries;
    const Outcome oc = ArchiveService::listRemote(sftp, remotePath, &entries, nullptr);
    if (!oc.ok) {
        out.append(QVariantMap { { "error", oc.friendly } });
        return out;
    }
    for (const auto& e : entries) {
        out.append(QVariantMap {
            { "path", e.path }, { "isDir", e.isDir },
            { "size", qint64(e.size) }, { "mtime", e.mtime } });
    }
    return out;
}

QString AppController::extractArchive(qint64 sessionId, const QString& remotePath,
                                      const QStringList& entries, const QString& localDir)
{
    SshSession* s = SessionManager::instance().sessionById(sessionId);
    if (!s)
        return QStringLiteral("No session.");
    QString err;
    std::shared_ptr<ISftpSession> sftp = s->createSftpSession(&err);
    if (!sftp)
        return err;
    const Outcome oc = ArchiveService::extractRemote(sftp, remotePath, entries, localDir);
    return oc.ok ? QString() : oc.friendly;
}

QVariantMap AppController::previewTextFile(qint64 sessionId, const QString& remotePath)
{
    SshSession* s = SessionManager::instance().sessionById(sessionId);
    if (!s)
        return { { "ok", false }, { "error", QStringLiteral("No session.") } };
    QString err;
    std::shared_ptr<ISftpSession> sftp = s->createSftpSession(&err);
    if (!sftp)
        return { { "ok", false }, { "error", err } };
    quint64 size = 0;
    auto h = sftp->openForRead(remotePath, &size, &err);
    if (!h)
        return { { "ok", false }, { "error", err } };
    const quint64 cap = 1 * 1024 * 1024;
    QByteArray data;
    data.resize(int(qMin<quint64>(cap, size ? size : cap)));
    const int n = sftp->readFile(h, data.data(), data.size(), &err);
    sftp->closeFile(h);
    if (n < 0)
        return { { "ok", false }, { "error", err } };
    data.resize(n);
    return { { "ok", true },
             { "content", QString::fromUtf8(data) },
             { "truncated", quint64(size) > quint64(n) },
             { "size", qint64(size) } };
}

// ---- diagnostics / tools ------------------------------------------------------
void AppController::runDiagnostics(qint64 profileId, const QString& password)
{
    ConnectionProfile p = ProfileStore::instance().get(profileId);
    auto* diag = new Diagnostics(p, password, this);
    QThread* thread = new QThread(this);
    diag->moveToThread(thread);
    connect(thread, &QThread::started, diag, &Diagnostics::start);
    connect(diag, &Diagnostics::stepFinished, this, &AppController::diagnosticsStep);
    connect(diag, &Diagnostics::keyExchangeInfo, this, &AppController::diagnosticsKex);
    connect(diag, &Diagnostics::finished, this, &AppController::diagnosticsFinished);
    connect(diag, &Diagnostics::finished, thread, &QThread::quit);
    connect(thread, &QThread::finished, diag, &QObject::deleteLater);
    connect(thread, &QThread::finished, thread, &QObject::deleteLater);
    thread->start();
}

QVariantMap AppController::developerInfo() const
{
    return {
        { "appVersion", version() },
        { "qtVersion", QString::fromLatin1(qVersion()) },
        { "libsshVersion", libsshVersionString() },
        { "libssh2Version", libssh2VersionString() },
        { "platform", QSysInfo::prettyProductName() },
        { "arch", QSysInfo::currentCpuArchitecture() },
        { "credentialStorage", CredentialManager::instance().storageDescription() },
        { "openSshKeyDir", utils::defaultKeyDirectory() },
        { "knownHostsPath", HostKeyManager().path() },
        { "configDir", utils::configDirectory() },
        { "dataDir", utils::dataDirectory() },
        { "updateChannel", Settings::instance().updateChannel() },
    };
}

void AppController::quitApplication()
{
    CredentialManager::instance().wipeSessionSecrets();
    QApplication::quit();
}

} // namespace eclipse
