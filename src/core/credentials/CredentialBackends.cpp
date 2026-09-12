#include "CredentialBackends.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSet>

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include "../../common/Secure.h"
#include "../../common/Utils.h"
#include "../logging/Logger.h"

#ifdef Q_OS_WIN
#include "windows/WindowsCredentialStore.h"
#endif
#ifdef Q_OS_LINUX
#include "linux/SecretServiceStore.h"
#endif

namespace eclipse {

// ---------------------------------------------------------------------------
// EncryptedFileStore (AES-256-GCM)
// ---------------------------------------------------------------------------
bool EncryptedFileStore::available()
{
    return ensureKey();
}

QString EncryptedFileStore::containerPath() const
{
    return utils::dataDirectory() + QStringLiteral("/secrets.bin");
}

bool EncryptedFileStore::ensureKey()
{
    if (m_keyTried)
        return !m_key.isEmpty();
    m_keyTried = true;
    const QString dir = utils::dataDirectory();
    QDir().mkpath(dir);
    const QString keyPath = dir + QStringLiteral("/machine.key");
    QFile kf(keyPath);
    if (kf.exists() && kf.size() == 32) {
        if (kf.open(QIODevice::ReadOnly)) {
            m_key = kf.readAll();
            kf.close();
        }
    } else {
        unsigned char rnd[32];
        if (RAND_bytes(rnd, 32) != 1)
            return false;
        m_key = QByteArray(reinterpret_cast<char*>(rnd), 32);
        if (kf.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
            kf.write(m_key);
            kf.close();
            kf.setPermissions(QFile::ReadOwner | QFile::WriteOwner); // 0600
        } else {
            m_key.clear();
            return false;
        }
    }
    return !m_key.isEmpty();
}

QByteArray EncryptedFileStore::masterKey()
{
    if (!ensureKey())
        return {};
    // Stretch the machine key through PBKDF2-HMAC-SHA256 with an app salt so a
    // stolen machine.key alone is not trivially reusable with other containers.
    unsigned char derived[32];
    const QByteArray salt = QByteArrayLiteral("eclipse-ssh-desktop/secrets/v1");
    if (PKCS5_PBKDF2_HMAC(m_key.constData(), m_key.size(),
                          reinterpret_cast<const unsigned char*>(salt.constData()), salt.size(),
                          120000, EVP_sha256(), 32, derived) != 1)
        return {};
    QByteArray out(reinterpret_cast<char*>(derived), 32);
    secureZero(derived, sizeof(derived));
    return out;
}

QJsonDocument readContainer(const QString& path)
{
    QFile f(path);
    if (!f.exists() || !f.open(QIODevice::ReadOnly))
        return QJsonDocument(QJsonObject {});
    return QJsonDocument::fromJson(f.readAll());
}

bool writeContainer(const QString& path, const QJsonDocument& doc)
{
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate))
        return false;
    f.write(doc.toJson(QJsonDocument::Compact));
    f.setPermissions(QFile::ReadOwner | QFile::WriteOwner);
    return true;
}

bool EncryptedFileStore::store(const QString& key, const QString& secret)
{
    const QByteArray mk = masterKey();
    if (mk.isEmpty())
        return false;

    unsigned char iv[12];
    RAND_bytes(iv, 12);
    const QByteArray plain = secret.toUtf8();

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        return false;
    QByteArray ct(plain.size() + 16, 0);
    int len = 0, ctLen = 0;
    bool ok = EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr,
                                 reinterpret_cast<const unsigned char*>(mk.constData()), iv) == 1
              && EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char*>(ct.data()), &len,
                                   reinterpret_cast<const unsigned char*>(plain.constData()),
                                   plain.size()) == 1;
    ctLen = len;
    if (ok && EVP_EncryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(ct.data()) + len, &len) == 1)
        ctLen += len;
    unsigned char tag[16];
    if (ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag) == 1) {
        ct.resize(ctLen);
    } else {
        ok = false;
    }
    EVP_CIPHER_CTX_free(ctx);
    if (!ok)
        return false;

    QJsonObject entry;
    entry.insert(QStringLiteral("iv"), QString::fromLatin1(QByteArray(reinterpret_cast<char*>(iv), 12).toHex()));
    entry.insert(QStringLiteral("tag"), QString::fromLatin1(QByteArray(reinterpret_cast<char*>(tag), 16).toHex()));
    entry.insert(QStringLiteral("ct"), QString::fromLatin1(ct.toHex()));
    entry.insert(QStringLiteral("updated"), QDateTime::currentSecsSinceEpoch());

    const QString path = containerPath();
    QJsonObject root = readContainer(path).object();
    root.insert(key, entry);
    const bool saved = writeContainer(path, QJsonDocument(root));
    if (saved)
        LOG_APP(QStringLiteral("Credential stored in encrypted-file backend (%1)").arg(key));
    return saved;
}

QString EncryptedFileStore::load(const QString& key)
{
    const QByteArray mk = masterKey();
    if (mk.isEmpty())
        return {};
    const QJsonObject root = readContainer(containerPath()).object();
    const QJsonObject entry = root.value(key).toObject();
    if (entry.isEmpty())
        return {};
    const QByteArray iv = QByteArray::fromHex(entry.value(QStringLiteral("iv")).toString().toLatin1());
    const QByteArray tag = QByteArray::fromHex(entry.value(QStringLiteral("tag")).toString().toLatin1());
    QByteArray ct = QByteArray::fromHex(entry.value(QStringLiteral("ct")).toString().toLatin1());
    if (iv.size() != 12 || tag.size() != 16 || ct.isEmpty())
        return {};

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx)
        return {};
    QByteArray plain(ct.size(), 0);
    int len = 0, plainLen = 0;
    bool ok = EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr,
                                 reinterpret_cast<const unsigned char*>(mk.constData()),
                                 reinterpret_cast<const unsigned char*>(iv.constData())) == 1
              && EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char*>(plain.data()), &len,
                                   reinterpret_cast<const unsigned char*>(ct.constData()),
                                   ct.size()) == 1;
    plainLen = len;
    ok = ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16,
                                   reinterpret_cast<void*>(const_cast<char*>(tag.constData()))) == 1;
    if (ok && EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(plain.data()) + len, &len) == 1)
        plainLen += len;
    else
        ok = false;
    EVP_CIPHER_CTX_free(ctx);
    if (!ok)
        return {};
    plain.resize(plainLen);
    return QString::fromUtf8(plain);
}

bool EncryptedFileStore::remove(const QString& key)
{
    const QString path = containerPath();
    QJsonObject root = readContainer(path).object();
    if (!root.contains(key))
        return true;
    root.remove(key);
    return writeContainer(path, QJsonDocument(root));
}

// ---------------------------------------------------------------------------
// SessionCredentialStore
// ---------------------------------------------------------------------------
bool SessionCredentialStore::store(const QString& key, const QString& secret)
{
    m_map.insert(key, secret.toUtf8());
    return true;
}

QString SessionCredentialStore::load(const QString& key)
{
    return QString::fromUtf8(m_map.value(key));
}

bool SessionCredentialStore::remove(const QString& key)
{
    return m_map.remove(key) > 0;
}

// ---------------------------------------------------------------------------
ICredentialBackend* createPlatformCredentialBackend()
{
#ifdef Q_OS_WIN
    static WindowsCredentialStore winStore;
    if (winStore.available())
        return &winStore;
#endif
#ifdef Q_OS_LINUX
    static SecretServiceStore secretStore;
    if (secretStore.available())
        return &secretStore;
#endif
    return nullptr;
}

} // namespace eclipse
