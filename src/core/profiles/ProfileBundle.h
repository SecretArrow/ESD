#pragma once

#include <QList>
#include <QString>

#include "ConnectionProfile.h"

namespace eclipse {

// ---------------------------------------------------------------------------
// ProfileBundle - portable, optionally AES-256-GCM encrypted bundle of
// connection profiles for device sync ("portable mode").
//
// File format:
//   plaintext:  JSON envelope
//               {"format":"eclipse-profile-bundle","version":1,"count":N,
//                "exportedAt":"<ISO-8601>","profiles":[...]}
//   encrypted:  base64( "EPB1" | salt[16] | iv[12] | ciphertext || tag[16] )
//     key       = PBKDF2-HMAC-SHA256(passphrase, salt, 120000 iters, 32 B)
//     cipher    = AES-256-GCM, random 16-byte salt / 12-byte IV per file,
//                 16-byte tag appended after the ciphertext and verified on
//                 import (wrong passphrase => tag mismatch => error)
//     The crypto mirrors the CredentialBackends encrypted-file fallback
//     (PKCS5_PBKDF2_HMAC + EVP_aes_256_gcm); iterations match (120000).
//
// Profile JSON inside "profiles" uses the same per-profile field mapping as
// ProfileStore::exportToJson/importFromJson (name / group / tags / favorite /
// host / port / username / authMethod / privateKeyPath / engine + "extra"
// object), so bundles interoperate with the existing JSON export. Profile ids
// are never exported; imported profiles get fresh identities when stored.
//
// An empty passphrase writes a plaintext JSON file. The passphrase itself is
// never logged (messages mention counts and paths only).
// ---------------------------------------------------------------------------
class ProfileBundle
{
public:
    // Writes a bundle to path. Returns false and fills errorOut on failure.
    static bool exportToFile(const QString& path, const QList<ConnectionProfile>& profiles,
                             const QString& passphrase, QString* errorOut = nullptr);

    // Reads a bundle from path. Returns the imported profiles (host-empty
    // entries are skipped) and appends a summary to errorOut; an empty list
    // with a non-empty errorOut means the file could not be read.
    static QList<ConnectionProfile> importFromFile(const QString& path, const QString& passphrase,
                                                   QString* errorOut = nullptr);
};

} // namespace eclipse
