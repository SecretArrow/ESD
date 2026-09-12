#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

namespace eclipse {

enum class SshEngineKind { Auto, Libssh, Libssh2 };

enum class SshAuthMethod { Password, PublicKey, Agent, KeyboardInteractive };

inline QString authMethodToString(SshAuthMethod m)
{
    switch (m) {
    case SshAuthMethod::Password: return QStringLiteral("password");
    case SshAuthMethod::PublicKey: return QStringLiteral("publickey");
    case SshAuthMethod::Agent: return QStringLiteral("agent");
    case SshAuthMethod::KeyboardInteractive: return QStringLiteral("keyboard-interactive");
    }
    return QStringLiteral("password");
}

struct AuthParams
{
    QString username;
    SshAuthMethod method = SshAuthMethod::Password;

    // Password auth
    QString password;

    // PublicKey auth
    QString privateKeyPath;
    QString passphrase;
    bool tryDefaultIdentities = false;

    // Agent auth (key selection handled internally)
    bool allowAgentFallback = true;

    // Keyboard-interactive: prompts are surfaced asynchronously
    bool allowInteractive = true;
};

struct HostKeyInfo
{
    QString host;
    int port = 22;
    QString keyType;          // "ssh-ed25519", "rsa-sha2-512", ...
    QByteArray publicKeyBlob; // raw wire blob (base64 body of known_hosts)
    QString sha256Fingerprint; // "SHA256:...."
    QString md5Fingerprint;    // legacy display "aa:bb:.."

    bool isValid() const { return !publicKeyBlob.isEmpty(); }
};

struct EngineInfo
{
    QString backend;      // "libssh" | "libssh2"
    QString version;      // library version
    QString kex;
    QString cipherIn;
    QString cipherOut;
    QString macIn;
    QString macOut;
    QString hostKeyAlgo;
    QString compression;
};

struct SftpAttrs
{
    QString name;
    quint64 size = 0;
    quint32 permissions = 0;
    quint32 uid = 0;
    quint32 gid = 0;
    qint64 mtime = 0;
    qint64 atime = 0;
    bool isDir = false;
    bool isLink = false;
    bool isRegular = false;
    bool canRead = true;
    bool canWrite = true;
    bool canExec = false;
};

struct SftpEntry
{
    QString name;
    SftpAttrs attrs;
};

enum class ChannelKind { Shell, Exec, DirectTcpip, RemoteForwardAccepted, X11 };

// Opaque channel identifier used by the worker pump.
using ChannelId = int;

constexpr ChannelId kInvalidChannelId = -1;

} // namespace eclipse
