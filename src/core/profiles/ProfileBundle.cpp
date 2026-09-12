#include "ProfileBundle.h"

#include <QDateTime>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>

#include <cstring>

#include <openssl/evp.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include "../../common/Secure.h"
#include "../logging/Logger.h"

namespace eclipse {

namespace {

constexpr int kSaltBytes = 16;
constexpr int kIvBytes = 12;
constexpr int kTagBytes = 16;
constexpr int kKeyBytes = 32;
constexpr int kPbkdf2Iterations = 120000; // mirrors EncryptedFileStore
constexpr int kMagicLen = 4;
const char kMagic[kMagicLen + 1] = "EPB1";

void wipe(QByteArray& b)
{
    if (!b.isEmpty())
        secureZero(b.data(), size_t(b.size()));
    b.clear();
}

QByteArray deriveKey(const QString& passphrase, const QByteArray& salt)
{
    QByteArray key(kKeyBytes, 0);
    const QByteArray pass = passphrase.toUtf8();
    if (PKCS5_PBKDF2_HMAC(pass.constData(), pass.size(),
                          reinterpret_cast<const unsigned char*>(salt.constData()), salt.size(),
                          kPbkdf2Iterations, EVP_sha256(), kKeyBytes,
                          reinterpret_cast<unsigned char*>(key.data())) != 1) {
        wipe(key);
        return {};
    }
    return key;
}

// AES-256-GCM encrypt; blob layout: magic + salt + iv + ciphertext + tag.
QByteArray encryptPayload(const QByteArray& plain, const QString& passphrase, QString* errorOut)
{
    QByteArray salt(kSaltBytes, 0);
    QByteArray iv(kIvBytes, 0);
    if (RAND_bytes(reinterpret_cast<unsigned char*>(salt.data()), kSaltBytes) != 1
        || RAND_bytes(reinterpret_cast<unsigned char*>(iv.data()), kIvBytes) != 1) {
        if (errorOut)
            *errorOut = QStringLiteral("OpenSSL random generator failed");
        return {};
    }
    QByteArray key = deriveKey(passphrase, salt);
    if (key.isEmpty()) {
        if (errorOut)
            *errorOut = QStringLiteral("Key derivation failed");
        return {};
    }

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        wipe(key);
        if (errorOut)
            *errorOut = QStringLiteral("Cipher context allocation failed");
        return {};
    }
    QByteArray ct(plain.size() + kTagBytes, 0);
    int len = 0, ctLen = 0;
    bool ok = EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr,
                                 reinterpret_cast<const unsigned char*>(key.constData()),
                                 reinterpret_cast<const unsigned char*>(iv.constData())) == 1
              && EVP_EncryptUpdate(ctx, reinterpret_cast<unsigned char*>(ct.data()), &len,
                                   reinterpret_cast<const unsigned char*>(plain.constData()),
                                   plain.size()) == 1;
    ctLen = len;
    if (ok && EVP_EncryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(ct.data()) + len, &len) == 1)
        ctLen += len;
    unsigned char tag[kTagBytes];
    if (ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, kTagBytes, tag) == 1)
        ct.resize(ctLen);
    else
        ok = false;
    EVP_CIPHER_CTX_free(ctx);
    wipe(key);
    if (!ok) {
        if (errorOut)
            *errorOut = QStringLiteral("Encryption failed");
        return {};
    }

    QByteArray blob;
    blob.reserve(kMagicLen + kSaltBytes + kIvBytes + ct.size() + kTagBytes);
    blob.append(kMagic, kMagicLen);
    blob.append(salt);
    blob.append(iv);
    blob.append(ct);
    blob.append(reinterpret_cast<const char*>(tag), kTagBytes);
    return blob;
}

// AES-256-GCM decrypt with tag verification (wrong passphrase => empty).
QByteArray decryptPayload(const QByteArray& blob, const QString& passphrase, QString* errorOut)
{
    if (blob.size() < kMagicLen + kSaltBytes + kIvBytes + kTagBytes
        || std::memcmp(blob.constData(), kMagic, kMagicLen) != 0) {
        if (errorOut)
            *errorOut = QStringLiteral("Not an encrypted Eclipse profile bundle "
                                       "(missing EPB1 header)");
        return {};
    }
    const QByteArray salt = blob.mid(kMagicLen, kSaltBytes);
    const QByteArray iv = blob.mid(kMagicLen + kSaltBytes, kIvBytes);
    const QByteArray ct = blob.mid(kMagicLen + kSaltBytes + kIvBytes,
                                   blob.size() - kMagicLen - kSaltBytes - kIvBytes - kTagBytes);
    const QByteArray tag = blob.right(kTagBytes);

    QByteArray key = deriveKey(passphrase, salt);
    if (key.isEmpty()) {
        if (errorOut)
            *errorOut = QStringLiteral("Key derivation failed");
        return {};
    }

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    if (!ctx) {
        wipe(key);
        if (errorOut)
            *errorOut = QStringLiteral("Cipher context allocation failed");
        return {};
    }
    QByteArray plain(ct.size(), 0);
    int len = 0, plainLen = 0;
    bool ok = EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), nullptr,
                                 reinterpret_cast<const unsigned char*>(key.constData()),
                                 reinterpret_cast<const unsigned char*>(iv.constData())) == 1
              && EVP_DecryptUpdate(ctx, reinterpret_cast<unsigned char*>(plain.data()), &len,
                                   reinterpret_cast<const unsigned char*>(ct.constData()),
                                   ct.size()) == 1;
    plainLen = len;
    QByteArray tagCopy = tag; // EVP_CTRL_GCM_SET_TAG must point to writable memory
    ok = ok && EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, kTagBytes, tagCopy.data()) == 1;
    if (ok && EVP_DecryptFinal_ex(ctx, reinterpret_cast<unsigned char*>(plain.data()) + len, &len) == 1)
        plainLen += len;
    else
        ok = false;
    EVP_CIPHER_CTX_free(ctx);
    wipe(key);
    if (!ok) {
        if (errorOut)
            *errorOut = QStringLiteral("Wrong passphrase or corrupted bundle");
        return {};
    }
    plain.resize(plainLen);
    return plain;
}

// Per-profile JSON mapping - mirrors ProfileStore::exportToJson.
QJsonObject profileToJson(const ConnectionProfile& p)
{
    QJsonObject o;
    o.insert(QStringLiteral("name"), p.name);
    o.insert(QStringLiteral("group"), p.group);
    o.insert(QStringLiteral("tags"), QJsonArray::fromStringList(p.tags));
    o.insert(QStringLiteral("favorite"), p.favorite);
    o.insert(QStringLiteral("host"), p.host);
    o.insert(QStringLiteral("port"), p.port);
    o.insert(QStringLiteral("username"), p.username);
    o.insert(QStringLiteral("authMethod"), p.authMethod);
    o.insert(QStringLiteral("privateKeyPath"), p.privateKeyPath);
    o.insert(QStringLiteral("engine"), p.engine);
    o.insert(QStringLiteral("extra"), p.extraJson());
    return o;
}

// Per-profile JSON mapping - mirrors ProfileStore::importFromJson.
ConnectionProfile profileFromJson(const QJsonObject& o)
{
    ConnectionProfile p; // id stays 0: bundles never carry the target DB identity
    p.name = o.value(QStringLiteral("name")).toString();
    p.group = o.value(QStringLiteral("group")).toString();
    for (const auto& t : o.value(QStringLiteral("tags")).toArray())
        p.tags.append(t.toString());
    p.favorite = o.value(QStringLiteral("favorite")).toBool();
    p.host = o.value(QStringLiteral("host")).toString();
    p.port = o.value(QStringLiteral("port")).toInt(22);
    if (p.port <= 0)
        p.port = 22;
    p.username = o.value(QStringLiteral("username")).toString();
    p.authMethod = o.value(QStringLiteral("authMethod")).toString(QStringLiteral("password"));
    p.privateKeyPath = o.value(QStringLiteral("privateKeyPath")).toString();
    p.engine = o.value(QStringLiteral("engine")).toString(QStringLiteral("auto"));
    p.applyExtraJson(o.value(QStringLiteral("extra")).toObject());
    return p;
}

} // namespace

bool ProfileBundle::exportToFile(const QString& path, const QList<ConnectionProfile>& profiles,
                                 const QString& passphrase, QString* errorOut)
{
    QJsonArray arr;
    for (const auto& p : profiles)
        arr.append(profileToJson(p));

    QJsonObject root;
    root.insert(QStringLiteral("format"), QStringLiteral("eclipse-profile-bundle"));
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("count"), arr.size());
    root.insert(QStringLiteral("exportedAt"), QDateTime::currentDateTime().toString(Qt::ISODate));
    root.insert(QStringLiteral("profiles"), arr);
    const QByteArray json = QJsonDocument(root).toJson(QJsonDocument::Indented);

    QByteArray payload;
    if (passphrase.isEmpty()) {
        payload = json;
    } else {
        QString encErr;
        const QByteArray blob = encryptPayload(json, passphrase, &encErr);
        if (blob.isEmpty()) {
            if (errorOut)
                *errorOut = encErr;
            LOG_ERROR(QStringLiteral("Profile bundle export failed: %1").arg(encErr));
            return false;
        }
        payload = blob.toBase64() + QByteArrayLiteral("\n");
    }

    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Truncate)) {
        if (errorOut)
            *errorOut = QStringLiteral("Cannot write %1: %2").arg(path, f.errorString());
        return false;
    }
    f.write(payload);
    f.setPermissions(QFile::ReadOwner | QFile::WriteOwner); // 0600: may hold host names
    LOG_APP(QStringLiteral("Exported %1 profile(s) to bundle %2 (%3)")
                .arg(profiles.size()).arg(path)
                .arg(passphrase.isEmpty() ? QStringLiteral("plaintext")
                                          : QStringLiteral("encrypted")));
    return true;
}

QList<ConnectionProfile> ProfileBundle::importFromFile(const QString& path, const QString& passphrase,
                                                       QString* errorOut)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (errorOut)
            *errorOut = QStringLiteral("Cannot read %1: %2").arg(path, f.errorString());
        return {};
    }
    const QByteArray raw = f.readAll();

    // Plaintext JSON first; otherwise base64("EPB1" ...) encrypted blob.
    QJsonDocument doc = QJsonDocument::fromJson(raw);
    if (doc.isNull()) {
        const QByteArray blob = QByteArray::fromBase64(raw); // tolerant: skips whitespace
        const bool isEncryptedBundle = blob.size() > kMagicLen
                                       && std::memcmp(blob.constData(), kMagic, kMagicLen) == 0;
        if (isEncryptedBundle && passphrase.isEmpty()) {
            if (errorOut)
                *errorOut = QStringLiteral("This bundle is encrypted - enter its passphrase");
            return {};
        }
        if (!isEncryptedBundle && passphrase.isEmpty()) {
            if (errorOut)
                *errorOut = QStringLiteral("Not a profile bundle (invalid JSON and no EPB1 header)");
            return {};
        }
        QString decErr;
        const QByteArray plain = decryptPayload(blob, passphrase, &decErr);
        if (plain.isEmpty()) {
            if (errorOut)
                *errorOut = decErr;
            LOG_WARN(QStringLiteral("Profile bundle import failed: %1").arg(decErr));
            return {};
        }
        doc = QJsonDocument::fromJson(plain);
        if (doc.isNull()) {
            if (errorOut)
                *errorOut = QStringLiteral("Decrypted payload is not valid JSON");
            return {};
        }
    }

    const QJsonObject root = doc.object();
    if (root.value(QStringLiteral("format")).toString() != QLatin1String("eclipse-profile-bundle")) {
        if (errorOut)
            *errorOut = QStringLiteral("Not an Eclipse profile bundle (unexpected format field)");
        return {};
    }
    if (root.value(QStringLiteral("version")).toInt() != 1) {
        if (errorOut)
            *errorOut = QStringLiteral("Unsupported bundle version: %1")
                            .arg(root.value(QStringLiteral("version")).toInt());
        return {};
    }

    const QJsonArray arr = root.value(QStringLiteral("profiles")).toArray();
    const int declared = root.value(QStringLiteral("count")).toInt(-1);
    QList<ConnectionProfile> out;
    int skipped = 0;
    for (const auto& v : arr) {
        const ConnectionProfile p = profileFromJson(v.toObject());
        if (p.host.isEmpty()) {
            ++skipped;
            continue;
        }
        out.append(p);
    }

    if (errorOut) {
        QStringList notes;
        notes.append(QStringLiteral("%1 profile(s) in bundle").arg(arr.size()));
        if (skipped)
            notes.append(QStringLiteral("%1 skipped (missing host)").arg(skipped));
        if (declared >= 0 && declared != arr.size())
            notes.append(QStringLiteral("envelope count mismatch (declared %1)").arg(declared));
        if (!errorOut->isEmpty())
            *errorOut += QLatin1Char(' ');
        *errorOut += notes.join(QStringLiteral("; "));
    }
    LOG_APP(QStringLiteral("Bundle import from %1: %2 profiles imported, %3 skipped")
                .arg(path).arg(out.size()).arg(skipped));
    return out;
}

} // namespace eclipse
