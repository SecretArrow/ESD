#ifdef _WIN32

#include "WindowsCredentialStore.h"

#include <QString>

#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <wincred.h>

namespace eclipse {

static const wchar_t* kTargetPrefix = L"EclipseSSH:";

static std::wstring targetName(const QString& key)
{
    return std::wstring(kTargetPrefix) + key.toStdWString();
}

bool WindowsCredentialStore::available()
{
    return true;
}

bool WindowsCredentialStore::store(const QString& key, const QString& secret)
{
    const std::wstring target = targetName(key);
    const QByteArray utf8 = secret.toUtf8();

    CREDENTIALW cred;
    ZeroMemory(&cred, sizeof(cred));
    cred.Type = CRED_TYPE_GENERIC;
    cred.TargetName = const_cast<LPWSTR>(target.c_str());
    cred.CredentialBlobSize = ULONG(utf8.size());
    cred.CredentialBlob = reinterpret_cast<LPBYTE>(const_cast<char*>(utf8.constData()));
    cred.Persist = CRED_PERSIST_LOCAL_MACHINE;
    cred.UserName = const_cast<LPWSTR>(L"EclipseSSH");

    if (!CredWriteW(&cred, 0)) {
        // Update path: remove then rewrite when blob size changed
        CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0);
        return CredWriteW(&cred, 0) != 0;
    }
    return true;
}

QString WindowsCredentialStore::load(const QString& key)
{
    const std::wstring target = targetName(key);
    PCREDENTIALW pcred = nullptr;
    if (!CredReadW(target.c_str(), CRED_TYPE_GENERIC, 0, &pcred))
        return {};
    QString out;
    if (pcred->CredentialBlob && pcred->CredentialBlobSize > 0)
        out = QString::fromUtf8(reinterpret_cast<const char*>(pcred->CredentialBlob),
                                int(pcred->CredentialBlobSize));
    CredFree(pcred);
    return out;
}

bool WindowsCredentialStore::remove(const QString& key)
{
    const std::wstring target = targetName(key);
    return CredDeleteW(target.c_str(), CRED_TYPE_GENERIC, 0) != 0;
}

} // namespace eclipse
#endif
