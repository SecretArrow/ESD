#pragma once

#include <QHash>
#include <QObject>
#include <QString>

#include "CredentialBackends.h"

namespace eclipse {

// ---------------------------------------------------------------------------
// CredentialManager - single façade over all credential backends.
//
// Secrets are addressed by stable keys like:
//     profile:<id>:password
//     profile:<id>:passphrase
//     profile:<id>:proxy-password
//
// Storage modes (per Settings security/credentialStorage):
//   "os"      OS keyring (Credential Manager / Secret Service), falls back to
//             the encrypted-file container when the keyring is unavailable.
//   "session" memory only, wiped at exit.
//   "ask"     nothing stored; UI prompts on every connection.
// ---------------------------------------------------------------------------

class CredentialManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(int mode READ modeInt WRITE setModeInt)
public:
    static CredentialManager& instance();
    int modeInt() const { return int(mode()); }
    void setModeInt(int m) { setMode(m == 1 ? Mode::Session : m == 2 ? Mode::Ask : Mode::Os); }

    enum class Mode { Os, Session, Ask };

    Mode mode() const;
    void setMode(Mode m);

    // Effective backend used for persistent secrets ("os" or "encrypted-file").
    QString persistentBackendName();
    bool persistentBackendAvailable();

    void saveSecret(const QString& key, const QString& secret, bool persistent);
    QString loadSecret(const QString& key);
    void deleteSecret(const QString& key);
    bool hasSecret(const QString& key);

    // Wipes every session-only secret (called on shutdown).
    void wipeSessionSecrets();

    // Returns a user-readable description of where secrets are stored.
    Q_INVOKABLE QString storageDescription() const;

private:
    CredentialManager() = default;
    ICredentialBackend* persistent();

    SessionCredentialStore m_session;
    ICredentialBackend* m_platform = nullptr;
    bool m_platformTried = false;
};

} // namespace eclipse
