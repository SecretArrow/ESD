#include "HostKeyManager.h"

#include <QCryptographicHash>
#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QTextStream>

#include <openssl/hmac.h>
#include <openssl/sha.h>

#include "../common/Utils.h"

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

} // namespace eclipse
