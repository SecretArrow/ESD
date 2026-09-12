#pragma once

#include <QJsonObject>
#include <QStringList>
#include <QVariantMap>

namespace eclipse {

// ---------------------------------------------------------------------------
// ConnectionProfile - the complete description of one server connection.
// Secrets (passwords / passphrases) are NEVER stored here; they live in the
// CredentialManager keyed by profile id.
// ---------------------------------------------------------------------------

struct ForwardRuleSpec
{
    QString id;
    QString name;
    QString type;        // "local" | "remote" | "dynamic"
    QString listenAddress = QStringLiteral("127.0.0.1");
    int listenPort = 0;
    QString destHost;
    int destPort = 0;
    bool autoStart = false;
    bool enabled = true;

    QJsonObject toJson() const;
    static ForwardRuleSpec fromJson(const QJsonObject& o);
};

struct TerminalProfileOverrides
{
    QString fontFamily;    // empty = global setting
    int fontSize = 0;      // 0 = global setting
    QString colorScheme;   // empty = global
    QStringList startupCommands;
    QString encoding = QStringLiteral("UTF-8");
};

struct ConnectionProfile
{
    qint64 id = 0;                 // 0 = not persisted yet
    QString name;
    QString group;
    QStringList tags;
    bool favorite = false;

    QString host;
    int port = 22;
    QString username;

    // password | publickey | agent | keyboardInteractive
    QString authMethod = QStringLiteral("password");
    QString privateKeyPath;        // for publickey
    bool useSshAgent = false;      // shortcut flag when authMethod == agent

    // auto | libssh | libssh2
    QString engine = QStringLiteral("auto");

    QStringList jumpHosts;         // ["user@bastion:22", ...] ordered chain

    // proxy
    QString proxyType = QStringLiteral("none");   // none | socks4 | socks5 | http
    QString proxyHost;
    int proxyPort = 0;
    QString proxyUser;

    int keepAliveSeconds = 15;
    bool compression = false;
    int connectTimeoutMs = 15000;
    bool allowKeyboardInteractive = true;
    bool autoReconnect = true;

    TerminalProfileOverrides terminal;
    QString sftpDefaultRemoteDir;
    QString sftpDefaultLocalDir;
    QString transferProtocol = QStringLiteral("sftp");  // sftp | scp
    QVector<ForwardRuleSpec> forwardingRules;
    QStringList environment;       // ["KEY=value", ...]

    qint64 createdMs = 0;
    qint64 lastUsedMs = 0;

    QVariantMap toVariantMap() const;
    static ConnectionProfile fromVariantMap(const QVariantMap& m);
    QJsonObject extraJson() const;             // non-searchable extended fields
    void applyExtraJson(const QJsonObject& o);

    QString label() const { return name.isEmpty() ? host : name; }
    QString effectiveJumpChain() const;
};

} // namespace eclipse
