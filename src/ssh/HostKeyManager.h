#pragma once

#include <QString>
#include <QStringList>
#include <QVector>

#include "Types.h"

namespace eclipse {

// ---------------------------------------------------------------------------
// HostKeyManager - OpenSSH-compatible known_hosts handling.
//
//  * reads/writes ~/.ssh/known_hosts (plus a manager-managed file for keys
//    saved with "trust & save" when the user prefers separation)
//  * understands hashed hostnames (|1| HMAC-SHA1 entries)
//  * SHA256 fingerprints computed the same way OpenSSH does
//  * never auto-accepts unknown or changed keys (security default)
// ---------------------------------------------------------------------------

enum class HostKeyCheckResult { Known, Unknown, Changed, Error };

class HostKeyManager
{
public:
    explicit HostKeyManager(QString knownHostsPath = {});

    const QString& path() const { return m_path; }

    HostKeyCheckResult check(const HostKeyInfo& key, QString* errorOut = nullptr) const;
    bool save(const HostKeyInfo& key);               // appends (dedup)
    bool removeKey(const QString& host, int port, const QString& keyType);

    // Parses any known_hosts content; used by import UI and tests.
    struct Entry
    {
        QStringList hostPatterns;   // after unbracketing ([host]:port)
        QString keyType;
        QByteArray blob;            // decoded public key blob
        QString comment;
        bool hashed = false;
    };
    static QVector<Entry> parseKnownHosts(const QString& content, QString* errorOut = nullptr);
    static QString encodeKnownHostsLine(const QString& host, int port, const HostKeyInfo& key);

    static QString hostPattern(const QString& host, int port);

    static QString sha256Fingerprint(const QByteArray& blob);
    static QString md5Fingerprint(const QByteArray& blob);
    static QString keyTypeForOid(const QString& sshName);
    static QString sshNameForKeyType(const QString& wireName);

    static bool isHashedEntry(const QString& hostField);

private:
    QString m_path;
    QString defaultPath() const;
};

} // namespace eclipse
