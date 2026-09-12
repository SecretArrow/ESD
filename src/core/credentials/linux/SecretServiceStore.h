#pragma once

#include <QObject>

// Secret Service (freedesktop keyring) client implemented over QtDBus.
// Enabled on Linux builds; every failure degrades gracefully to false so the
// credential manager can fall back to the encrypted-file container.
#ifdef Q_OS_LINUX

#include "../CredentialBackends.h"

#include <QDBusObjectPath>

namespace eclipse {

class SecretServiceStore : public ICredentialBackend
{
public:
    QString name() const override { return QStringLiteral("secret-service"); }
    bool available() override;
    bool store(const QString& key, const QString& secret) override;
    QString load(const QString& key) override;
    bool remove(const QString& key) override;

private:
    bool initSession();
    QDBusObjectPath defaultCollection();
    QString searchItem(const QString& key);   // returns item path or empty

    QDBusObjectPath m_session;
    bool m_sessionTried = false;
    bool m_broken = false;
};

} // namespace eclipse
#endif
