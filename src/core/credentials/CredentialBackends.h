#pragma once

#include <QByteArray>
#include <QMap>
#include <QString>
#include <QStringList>

namespace eclipse {

// ---------------------------------------------------------------------------
// Credential backend implementations.
//
//  priority on Linux:  Secret Service (freedesktop) -> encrypted file (AES-GCM)
//  priority on Windows: Windows Credential Manager (DPAPI backed)
//
//  No plaintext secret is ever written to disk by this module.
//  The encrypted-file backend exists ONLY for systems without a keyring and
//  is clearly reported in the UI / SECURITY docs.
// ---------------------------------------------------------------------------

class ICredentialBackend
{
public:
    virtual ~ICredentialBackend() = default;
    virtual QString name() const = 0;
    virtual bool available() = 0;
    virtual bool store(const QString& key, const QString& secret) = 0;
    virtual QString load(const QString& key) = 0;   // empty on miss
    virtual bool remove(const QString& key) = 0;
};

// OS integration, per platform.
ICredentialBackend* createPlatformCredentialBackend();

// AES-256-GCM encrypted container (fallback when no OS keyring exists).
class EncryptedFileStore : public ICredentialBackend
{
public:
    QString name() const override { return QStringLiteral("encrypted-file"); }
    bool available() override;
    bool store(const QString& key, const QString& secret) override;
    QString load(const QString& key) override;
    bool remove(const QString& key) override;

private:
    bool ensureKey();
    QByteArray masterKey();
    QString containerPath() const;

    QByteArray m_key;
    bool m_keyTried = false;
};

// Session-only store: process memory, securely wiped at exit.
class SessionCredentialStore : public ICredentialBackend
{
public:
    QString name() const override { return QStringLiteral("session"); }
    bool available() override { return true; }
    bool store(const QString& key, const QString& secret) override;
    QString load(const QString& key) override;
    bool remove(const QString& key) override;

private:
    QMap<QString, QByteArray> m_map;
};

} // namespace eclipse
