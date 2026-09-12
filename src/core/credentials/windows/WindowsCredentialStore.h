#pragma once

// Windows Credential Manager backend (DPAPI-backed).
// Only compiled on Windows; declared unconditionally so project file lists
// stay stable across platforms.
#ifdef _WIN32

#include "../CredentialBackends.h"

namespace eclipse {

class WindowsCredentialStore : public ICredentialBackend
{
public:
    QString name() const override { return QStringLiteral("windows-credential-manager"); }
    bool available() override;
    bool store(const QString& key, const QString& secret) override;
    QString load(const QString& key) override;
    bool remove(const QString& key) override;
};

} // namespace eclipse
#endif
