#include "HostKeyManager.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSet>
#include <QTextStream>

#include <openssl/hmac.h>
#include <openssl/sha.h>

#include "../common/Utils.h"
#include "../core/logging/Logger.h"

namespace eclipse {

HostKeyManager::HostKeyManager(QString knownHostsPath)
    : m_path(knownHostsPath.isEmpty() ? defaultPath() : std::move(knownHostsPath))
{
}

QString HostKeyManager::defaultPath() const
{
    return utils::defaultKeyDirectory() + QStringLiteral("/known_hosts");
}

QString HostKeyManager::hostPattern(const QString& host, int port)
{
    if (port == 22)
        return host;
    return QStringLiteral("[%1]:%2").arg(host).arg(port);
}

QString HostKeyManager::sha256Fingerprint(const QByteArray& blob)
{
    const QByteArray hash = QCryptographicHash::hash(blob, QCryptographicHash::Sha256);
    return utils::fingerprintText(hash);
}

QString HostKeyManager::md5Fingerprint(const QByteArray& blob)
{
    const QByteArray hash = QCryptographicHash::hash(blob, QCryptographicHash::Md5);
    QStringList parts;
    for (unsigned char c : hash)
        parts.append(QString::number(c, 16).rightJustified(2, QLatin1Char('0')));
    return parts.join(QLatin1Char(':'));
}

QString HostKeyManager::keyTypeForOid(const QString& sshName)
{
    return sshName; // wire name and display name match ("ssh-ed25519", ...)
}

QString HostKeyManager::sshNameForKeyType(const QString& wireName)
{
    return wireName;
}

bool HostKeyManager::isHashedEntry(const QString& hostField)
{
    return hostField.startsWith(QLatin1String("|1|"));
}

static QByteArray hmacSha1(const QByteArray& key, const QByteArray& data)
{
    unsigned int len = SHA_DIGEST_LENGTH;
    QByteArray out(SHA_DIGEST_LENGTH, 0);
    HMAC(EVP_sha1(), key.constData(), key.size(),
         reinterpret_cast<const unsigned char*>(data.constData()), size_t(data.size()),
         reinterpret_cast<unsigned char*>(out.data()), &len);
    return out;
}

static QByteArray decodeBase64Url(const QByteArray& input)
{
    QByteArray b64 = input;
    b64.replace('-', '+');
    b64.replace('_', '/');
    while (b64.size() % 4)
        b64.append('=');
    return QByteArray::fromBase64(b64);
}

static bool hashedEntryMatches(const QString& hostField, const QString& host)
{
    // |1|saltB64|hashB64|
    const QStringList parts = hostField.split(QLatin1Char('|'));
    if (parts.size() < 4)
        return false;
    const QByteArray salt = decodeBase64Url(parts.at(2).toLatin1());
    const QByteArray hash = decodeBase64Url(parts.at(3).toLatin1());
    if (salt.isEmpty() || hash.isEmpty())
        return false;
    const QByteArray computed = hmacSha1(salt, host.toUtf8());
    return computed == hash;
}

// Decodes a positive known_hosts pattern into (host, port). "[host]:port"
// uses the OpenSSH non-standard-port convention, everything else is port 22.
static bool decodeHostPattern(const QString& pattern, QString* hostOut, int* portOut)
{
    if (pattern.isEmpty() || pattern.startsWith(QLatin1Char('!')) || isHashedEntry(pattern))
        return false;
    if (pattern.startsWith(QLatin1Char('['))) {
        const int close = pattern.indexOf(QLatin1Char(']'));
        if (close < 1 || close + 2 >= pattern.size() || pattern.at(close + 1) != QLatin1Char(':'))
            return false;
        bool ok = false;
        const int port = pattern.mid(close + 2).toInt(&ok);
        if (!ok || port <= 0 || port > 65535)
            return false;
        *hostOut = pattern.mid(1, close - 1);
        *portOut = port;
        return true;
    }
    *hostOut = pattern;
    *portOut = 22;
    return true;
}

QVector<HostKeyManager::Entry> HostKeyManager::parseKnownHosts(const QString& content, QString* errorOut)
{
    QVector<Entry> out;
    const QStringList lines = content.split(QLatin1Char('\n'));
    int lineNo = 0;
    for (const QString& rawLine : lines) {
        ++lineNo;
        const QString line = rawLine.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        QStringList fields = line.split(QChar::fromLatin1(' '), Qt::SkipEmptyParts);
        // Some entries have an optional marker field (@cert-authority etc.)
        if (!fields.isEmpty() && fields.first().startsWith(QLatin1Char('@')))
            fields.removeFirst();
        if (fields.size() < 3) {
            if (errorOut)
                *errorOut = QStringLiteral("Malformed known_hosts line %1").arg(lineNo);
            continue;
        }
        Entry e;
        e.hostPatterns = fields[0].split(QLatin1Char(','), Qt::SkipEmptyParts);
        e.keyType = fields[1];
        e.blob = QByteArray::fromBase64(fields[2].toLatin1());
        if (fields.size() > 3)
            e.comment = fields[3];
        e.hashed = std::any_of(e.hostPatterns.cbegin(), e.hostPatterns.cend(),
                               [](const QString& h) { return isHashedEntry(h); });
        if (e.blob.isEmpty()) {
            if (errorOut)
                *errorOut = QStringLiteral("Invalid base64 key on line %1").arg(lineNo);
            continue;
        }
        out.append(e);
    }
    return out;
}

QString HostKeyManager::encodeKnownHostsLine(const QString& host, int port, const HostKeyInfo& key)
{
    const QString pattern = hostPattern(host, port);
    return QStringLiteral("%1 %2 %3")
        .arg(pattern, key.keyType, QString::fromLatin1(key.publicKeyBlob.toBase64()));
}

HostKeyCheckResult HostKeyManager::check(const HostKeyInfo& key, QString* errorOut) const
{
    QFile f(m_path);
    if (!f.exists())
        return HostKeyCheckResult::Unknown;
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorOut)
            *errorOut = QStringLiteral("Cannot read %1: %2").arg(m_path, f.errorString());
        return HostKeyCheckResult::Error;
    }
    const QString content = QString::fromUtf8(f.readAll());
    const auto entries = parseKnownHosts(content, errorOut);

    const QString plainPattern = hostPattern(key.host, key.port);
    bool foundOtherKey = false;
    for (const auto& e : entries) {
        bool hostMatch = false;
        for (const QString& pattern : e.hostPatterns) {
            if (isHashedEntry(pattern)) {
                if (hashedEntryMatches(pattern, key.host))
                    hostMatch = true;
            } else if (pattern == plainPattern) {
                hostMatch = true;
            }
        }
        if (!hostMatch)
            continue;
        if (e.blob == key.publicKeyBlob)
            return HostKeyCheckResult::Known;
        foundOtherKey = true;
    }
    return foundOtherKey ? HostKeyCheckResult::Changed : HostKeyCheckResult::Unknown;
}

bool HostKeyManager::save(const HostKeyInfo& key)
{
    QDir().mkpath(QFileInfo(m_path).absolutePath());
    QFile f(m_path);
    if (!f.open(QIODevice::ReadWrite | QIODevice::Text | QIODevice::Append))
        return false;
    // Dedup: if exact line exists, done.
    const QString line = encodeKnownHostsLine(key.host, key.port, key);
    {
        const QString existing = QString::fromUtf8(f.readAll());
        f.seek(0);
        if (existing.contains(line + QLatin1Char('\n')) || existing.trimmed() == line.trimmed())
            return true;
    }
    f.seek(f.size());
    f.write(line.toUtf8() + QByteArrayLiteral("\n"));
    f.setPermissions(QFile::ReadOwner | QFile::WriteOwner); // 0600 like OpenSSH
    return true;
}

bool HostKeyManager::removeKey(const QString& host, int port, const QString& keyType)
{
    QFile f(m_path);
    if (!f.exists())
        return true;
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text))
        return false;
    const QString pattern = hostPattern(host, port);
    QStringList keep;
    const QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
    bool changed = false;
    for (const QString& line : lines) {
        const auto fields = line.split(QChar::fromLatin1(' '), Qt::SkipEmptyParts);
        if (fields.size() >= 3 && fields[0] == pattern && fields[1] == keyType) {
            changed = true;
            continue;
        }
        keep.append(line);
    }
    f.close();
    if (!changed)
        return true;
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate | QIODevice::Text))
        return false;
    f.write(keep.join(QLatin1Char('\n')).toUtf8());
    return true;
}

// ---------------------------------------------------------------------------
// OpenSSH known_hosts import -------------------------------------------------
QVector<HostKeyManager::OpenSshHostEntry> HostKeyManager::parseOpenSshLine(const QByteArray& line)
{
    QVector<OpenSshHostEntry> out;
    const QByteArray trimmed = line.trimmed();
    if (trimmed.isEmpty() || trimmed.startsWith('#'))
        return out;
    const QStringList fields = QString::fromUtf8(trimmed).split(QChar::fromLatin1(' '), Qt::SkipEmptyParts);
    if (fields.size() < 3)
        return out;

    OpenSshHostEntry e;
    int idx = 0;
    if (fields.at(0).startsWith(QLatin1Char('@'))) {
        if (fields.at(0) != QLatin1String("@cert-authority") && fields.at(0) != QLatin1String("@revoked"))
            return out; // unknown marker: treat line as malformed
        e.marker = true;
        idx = 1;
    }
    if (fields.size() < idx + 3)
        return out;

    for (const QString& p : fields.at(idx).split(QLatin1Char(','), Qt::SkipEmptyParts)) {
        if (p.startsWith(QLatin1Char('!'))) {
            if (p.size() > 1)
                e.negatedPatterns.append(p.mid(1));
        } else if (!p.isEmpty()) {
            e.hostPatterns.append(p);
        }
    }
    e.keyType = fields.at(idx + 1);
    e.blob = QByteArray::fromBase64(fields.at(idx + 2).toLatin1());
    if (fields.size() > idx + 3)
        e.comment = fields.mid(idx + 3).join(QLatin1Char(' '));
    for (const QString& p : e.hostPatterns) {
        if (isHashedEntry(p)) {
            e.hashed = true;
            break;
        }
    }

    if (e.keyType.isEmpty() || e.blob.isEmpty()
        || (e.hostPatterns.isEmpty() && e.negatedPatterns.isEmpty()))
        return out;
    out.append(e);
    return out;
}

int HostKeyManager::importOpenSshKnownHosts(const QString& openSshPath, QString* errorOut)
{
    QFile f(openSshPath);
    if (!f.open(QIODevice::ReadOnly | QIODevice::Text)) {
        if (errorOut)
            *errorOut += QStringLiteral("Cannot read %1: %2\n").arg(openSshPath, f.errorString());
        LOG_SSH_ERR(QStringLiteral("known_hosts import: cannot read %1").arg(openSshPath));
        return 0;
    }

    QVector<OpenSshHostEntry> entries;
    int malformed = 0;
    const QList<QByteArray> lines = f.readAll().split('\n');
    for (const QByteArray& raw : lines) {
        const QByteArray trimmed = raw.trimmed();
        if (trimmed.isEmpty() || trimmed.startsWith('#'))
            continue;
        const auto parsed = parseOpenSshLine(trimmed);
        if (parsed.isEmpty())
            ++malformed;
        else
            entries.append(parsed.first());
    }

    // Candidate hostnames for hashed-entry matching: a |1| hash cannot be
    // reversed, so hashed entries can only be imported for hosts this
    // manager already stores (parsed out of its own known_hosts file).
    struct HostPort
    {
        QString host;
        int port;
    };
    QVector<HostPort> candidates;
    {
        QSet<QString> seen;
        QFile own(m_path);
        if (own.open(QIODevice::ReadOnly | QIODevice::Text)) {
            const QStringList ownLines = QString::fromUtf8(own.readAll()).split(QLatin1Char('\n'));
            for (const QString& ol : ownLines) {
                const QStringList fields = ol.trimmed().split(QChar::fromLatin1(' '), Qt::SkipEmptyParts);
                if (fields.size() < 3 || fields.at(0).startsWith(QLatin1Char('@')))
                    continue;
                for (const QString& pattern : fields.at(0).split(QLatin1Char(','), Qt::SkipEmptyParts)) {
                    HostPort hp;
                    if (!decodeHostPattern(pattern, &hp.host, &hp.port))
                        continue;
                    const QString id = hp.host + QLatin1Char(':') + QString::number(hp.port);
                    if (seen.contains(id))
                        continue;
                    seen.insert(id);
                    candidates.append(hp);
                }
            }
        }
    }

    int imported = 0;
    int alreadyPresent = 0;
    int skippedMarkers = 0;
    int skippedNegated = 0;
    int hashedMatched = 0;
    auto importOne = [&](const QString& host, int port, const OpenSshHostEntry& e) {
        HostKeyInfo key;
        key.host = host;
        key.port = port;
        key.keyType = e.keyType;
        key.publicKeyBlob = e.blob;
        if (check(key) == HostKeyCheckResult::Known) {
            ++alreadyPresent;
            return;
        }
        if (save(key))
            ++imported;
    };

    for (const OpenSshHostEntry& e : entries) {
        if (e.marker) {
            ++skippedMarkers; // CA/revoked markers are out of scope for import
            continue;
        }
        if (e.hostPatterns.isEmpty()) {
            ++skippedNegated; // line consisted of negated patterns only
            continue;
        }
        // Plain (unhashed) patterns: import for every host.
        for (const QString& pattern : e.hostPatterns) {
            if (isHashedEntry(pattern))
                continue; // hashed patterns matched below
            QString host;
            int port = 22;
            if (!decodeHostPattern(pattern, &host, &port)) {
                ++malformed;
                continue;
            }
            importOne(host, port, e);
        }
        // Hashed patterns: match against the hosts we already know.
        if (e.hashed) {
            for (const QString& pattern : e.hostPatterns) {
                if (!isHashedEntry(pattern))
                    continue;
                for (const HostPort& hp : candidates) {
                    if (hashedEntryMatches(pattern, hp.host)) {
                        importOne(hp.host, hp.port, e);
                        ++hashedMatched;
                        break;
                    }
                }
            }
        }
    }

    if (errorOut) {
        QStringList notes;
        notes.append(QStringLiteral("Imported %1 new, %2 already present")
                         .arg(imported).arg(alreadyPresent));
        if (skippedMarkers)
            notes.append(QStringLiteral("%1 @marker line(s) skipped").arg(skippedMarkers));
        if (skippedNegated)
            notes.append(QStringLiteral("%1 negated-only line(s) skipped").arg(skippedNegated));
        if (malformed)
            notes.append(QStringLiteral("%1 malformed line(s) skipped").arg(malformed));
        if (hashedMatched)
            notes.append(QStringLiteral("%1 hashed entr(y/ies) matched to known hosts").arg(hashedMatched));
        if (!errorOut->isEmpty())
            *errorOut += QLatin1Char(' ');
        *errorOut += notes.join(QStringLiteral("; "));
    }
    LOG_SSH(QStringLiteral("known_hosts import from %1: %2 imported, %3 already present, %4 skipped")
                .arg(openSshPath).arg(imported).arg(alreadyPresent)
                .arg(skippedMarkers + skippedNegated + malformed));
    return imported;
}

} // namespace eclipse
