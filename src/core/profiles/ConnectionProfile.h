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

    // ssh | telnet | serial  (console types reuse the same profile record)
    QString connectionType = QStringLiteral("ssh");

    QString host;
    int port = 22;
    QString username;

    // password | publickey | agent | keyboardInteractive
    QString authMethod = QStringLiteral("password");
    QString privateKeyPath;        // for publickey
    bool useSshAgent = false;      // shortcut flag when authMethod == agent

    // auto | libssh | libssh2
    QString engine = QStringLiteral("auto");

    // Comma-separated KEX preference list ("" = engine default).
    // Example: "mlkem768x25519-sha256,sntrup761x25519@openssh.com,curve25519-sha256"
    QString kexAlgorithms;

    // X11 forwarding (SSH only). x11Screen = local display screen number used
    // on Windows where DISPLAY is absent (VcXsrv/X410 convention: TCP 6000+n).
    bool x11Forward = false;
    int x11Screen = 0;

    // Serial console (connectionType == "serial")
    QString serialPort;                 // e.g. "COM3" or "/dev/ttyUSB0"
    int serialBaud = 115200;
    int serialDataBits = 8;             // 7 | 8
    QString serialParity = QStringLiteral("none"); // none | even | odd
    int serialStopBits = 1;             // 1 | 2
    int serialFlowControl = 0;          // 0 none | 1 rtscts | 2 xonxoff

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
