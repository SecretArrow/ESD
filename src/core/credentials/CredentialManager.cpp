#include "CredentialManager.h"

#include "../logging/Logger.h"
#include "../settings/Settings.h"

namespace eclipse {

CredentialManager& CredentialManager::instance()
{
    static CredentialManager m;
    return m;
}

CredentialManager::Mode CredentialManager::mode() const
{
    const QString s = Settings::instance().credentialStorage();
    if (s == QLatin1String("session"))
        return Mode::Session;
    if (s == QLatin1String("ask"))
        return Mode::Ask;
    return Mode::Os;
}

void CredentialManager::setMode(Mode m)
{
    Settings::instance().setValue(QStringLiteral("security/credentialStorage"),
                                  m == Mode::Session ? QStringLiteral("session")
                                  : m == Mode::Ask   ? QStringLiteral("ask")
                                                     : QStringLiteral("os"));
}

ICredentialBackend* CredentialManager::persistent()
{
    if (!m_platformTried) {
        m_platformTried = true;
        m_platform = createPlatformCredentialBackend();
        if (m_platform)
            LOG_APP(QStringLiteral("Credential backend: %1").arg(m_platform->name()));
    }
    return m_platform;
}

bool CredentialManager::persistentBackendAvailable()
{
    return persistent() != nullptr;
}

QString CredentialManager::persistentBackendName()
{
    ICredentialBackend* b = persistent();
    return b ? b->name() : QStringLiteral("encrypted-file");
}

void CredentialManager::saveSecret(const QString& key, const QString& secret, bool persistent_)
{
    if (secret.isEmpty()) {
        deleteSecret(key);
        return;
    }
    if (!persistent_) {
        m_session.store(key, secret);
        return;
    }
    if (mode() == Mode::Session) {
        m_session.store(key, secret);
        return;
    }
    ICredentialBackend* b = persistent();
    if (b && b->store(key, secret))
        return;
    // Fallback: AES-GCM container (never plaintext).
    static EncryptedFileStore fileStore;
    fileStore.store(key, secret);
}

QString CredentialManager::loadSecret(const QString& key)
{
    QString v = m_session.load(key);
    if (!v.isEmpty())
        return v;
    if (ICredentialBackend* b = persistent()) {
        v = b->load(key);
        if (!v.isEmpty())
            return v;
    }
    static EncryptedFileStore fileStore;
    return fileStore.load(key);
}

void CredentialManager::deleteSecret(const QString& key)
{
    m_session.remove(key);
    if (ICredentialBackend* b = persistent())
        b->remove(key);
    static EncryptedFileStore fileStore;
    fileStore.remove(key);
}

bool CredentialManager::hasSecret(const QString& key)
{
    return !loadSecret(key).isEmpty();
}

void CredentialManager::wipeSessionSecrets()
{
    m_session = SessionCredentialStore {};
    LOG_APP(QStringLiteral("Session credentials wiped."));
}

QString CredentialManager::storageDescription() const
{
    const CredentialManager* self = const_cast<CredentialManager*>(this);
    const_cast<CredentialManager*>(self)->persistent();
    if (mode() == Mode::Session)
        return QStringLiteral("Memory only (session) - wiped when the application exits");
    if (self->m_platform)
        return QStringLiteral("OS secure storage: %1").arg(self->m_platform->name());
    return QStringLiteral("Encrypted file container (AES-256-GCM) - OS keyring unavailable");
}

} // namespace eclipse
