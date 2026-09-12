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

    // OpenSSH known_hosts import --------------------------------------------
    // One parsed line of an OpenSSH known_hosts file. Marker lines
    // (@cert-authority / @revoked) keep marker=true and are skipped by the
    // importer; negated patterns ("!host") are moved out of hostPatterns so
    // importing code only sees positive patterns.
    struct OpenSshHostEntry
    {
        QStringList hostPatterns;    // positive patterns: "host", "[host]:port", "|1|salt|hash"
        QStringList negatedPatterns; // "!"-negated patterns ("!" stripped), informational
        QString keyType;             // e.g. "ssh-ed25519"
        QByteArray blob;             // decoded public key blob (base64 field)
        QString comment;
        bool hashed = false;         // any |1|salt|hash pattern present
        bool marker = false;         // @cert-authority / @revoked line
    };

    // Parses a single known_hosts line; returns 0 entries (comment / empty /
    // malformed) or 1 entry. Kept static so unit tests can call it directly.
    static QVector<OpenSshHostEntry> parseOpenSshLine(const QByteArray& line);

    // Imports an OpenSSH known_hosts file into this manager's own store.
    //  * plain entries are imported for every positive host pattern
    //  * hashed (|1|) entries are imported only for hosts already present in
    //    this manager's store (a hash cannot be reversed; HMAC-SHA1(salt)
    //    is matched against each stored hostname)
    //  * duplicates (same host/port/type/blob) are not re-imported
    // Returns the number of newly imported keys and appends a human-readable
    // summary (already-present / skipped / malformed counts) to errorOut.
    int importOpenSshKnownHosts(const QString& openSshPath, QString* errorOut = nullptr);

private:
    QString m_path;
    QString defaultPath() const;
};

} // namespace eclipse
