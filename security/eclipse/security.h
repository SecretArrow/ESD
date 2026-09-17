/* Eclipse SSH - security layer: credential vault, host key store, key manager.
 *
 * Security model (spec #73): the UI never touches crypto primitives; it calls
 * this layer only. Cryptography is delegated to OpenSSL (AES-256-GCM, PBKDF2),
 * never implemented here from scratch (spec rule #11).
 *
 * Vault file format (JSON, UTF-8):
 * {
 *   "version": 1,
 *   "kdf": { "algo": "pbkdf2-hmac-sha256", "iters": 200000, "salt": "<b64>" },
 *   "nonce": "<b64>",                      // 12-byte AES-GCM nonce
 *   "ciphertext": "<b64>",                 // inner JSON: {"secret_id": {"user":..,"pass":..}}
 *   "tag": "<b64>"                         // 16-byte GCM tag
 * }
 * The vault key is a random 32-byte master key stored with 0600 perms in the
 * config dir (keyfile model, same trust root as the OS user account); on
 * Windows DPAPI could wrap it later — the format anticipates a "wrap" field.
 */
#ifndef ECLIPSE_SECURITY_H
#define ECLIPSE_SECURITY_H

#include "eclipse/core.h"

#ifdef __cplusplus
extern "C" {
#endif

/* ----------------------------------------------------------- credential vault */
typedef struct EcVault EcVault;

/* Open (or create) the vault at path. Returns NULL on unrecoverable error. */
EcVault* ec_vault_open(const char* path);
void ec_vault_free(EcVault* v);
/* Store/replace a secret. Secrets are wiped from memory after encrypting. */
bool ec_vault_set(EcVault* v, const char* id, const char* username, const char* secret);
/* Fetch a secret; caller frees the returned username and secret. */
bool ec_vault_get(EcVault* v, const char* id, char** username, char** secret);
bool ec_vault_delete(EcVault* v, const char* id);
bool ec_vault_has(EcVault* v, const char* id);
/* Persist to disk (atomic, 0600). */
bool ec_vault_save(EcVault* v);
/* List stored secret ids (caller frees each + array). */
char** ec_vault_ids(EcVault* v, size_t* count_out);

/* ------------------------------------------------------------- host key store */
typedef enum {
    EC_HK_OK = 0,        /* known and matching */
    EC_HK_UNKNOWN,       /* not in known_hosts yet */
    EC_HK_CHANGED,       /* key mismatch - possible MITM */
    EC_HK_ERROR          /* storage/parse failure */
} EcHostKeyStatus;

typedef struct EcHostKeyStore EcHostKeyStore;
/* Wraps a libssh known_hosts file path. */
EcHostKeyStore* ec_hostkeys_open(const char* known_hosts_path);
void ec_hostkeys_free(EcHostKeyStore* s);
EcHostKeyStatus ec_hostkeys_check(EcHostKeyStore* s, const char* host, int port,
                                  const char* key_type, const char* key_base64);
bool ec_hostkeys_accept(EcHostKeyStore* s, const char* host, int port,
                        const char* key_type, const char* key_base64); /* TOFU write */
bool ec_hostkeys_remove(EcHostKeyStore* s, const char* host, int port);
/* [host]:port / host normalization matching OpenSSH known_hosts format. */
char* ec_hostkeys_normalize_host(const char* host, int port);

/* --------------------------------------------------------------- key manager */
typedef struct {
    char* path;          /* private key file */
    char* type;          /* ssh-ed25519, ssh-rsa, ... */
    char* fingerprint;   /* SHA256:... */
    char* comment;
} EcKeyInfo;

/* Enumerate keys in a directory (non-recursive; tries each file). */
EcKeyInfo** ec_keymgr_scan(const char* dir, size_t* count_out);
void ec_keyinfo_free(EcKeyInfo* k);
void ec_keyinfo_array_free(EcKeyInfo** keys, size_t n);
/* Generate a new keypair. type: "ed25519"|"rsa"|"ecdsa" (bits used for rsa). */
bool ec_keymgr_generate(const char* type, unsigned bits, const char* comment,
                        const char* passphrase, const char* out_private_path,
                        char** err);
/* Import OpenSSH private key file to store dir (copy), returns fingerprint. */
bool ec_keymgr_import(const char* source_path, const char* dest_dir, char** err);
/* Export public key of a private key file to path. */
bool ec_keymgr_export_public(const char* private_path, const char* out_public_path, char** err);
/* SHA256 fingerprint (base64, "SHA256:...") of a private key file. */
char* ec_keymgr_fingerprint(const char* private_path);
/* Validate a private key file (optionally with passphrase). */
bool ec_keymgr_validate(const char* private_path, const char* passphrase);

#ifdef __cplusplus
}
#endif
#endif /* ECLIPSE_SECURITY_H */
