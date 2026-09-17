/* Eclipse SSH - security implementation: vault (OpenSSL AES-256-GCM + PBKDF2),
 * host key store (OpenSSH known_hosts format), key manager (libssh pki).
 */
#include "eclipse/security.h"
#include "eclipse/platform.h"

#include <libssh/libssh.h>
#include <glib.h>

#include <openssl/core_names.h>
#include <openssl/evp.h>
#include <openssl/kdf.h>
#include <openssl/rand.h>
#include <openssl/sha.h>

#include "cJSON.h"

/* --------------------------------------------------------- base64 helpers */
static char* b64_encode(const uint8_t* data, size_t len)
{
    int need = 4 * ((int)((len + 2) / 3)) + 1;
    char* out = malloc((size_t)need);
    if (!out) return NULL;
    int n = EVP_EncodeBlock((unsigned char*)out, data, (int)len);
    if (n <= 0) { free(out); return NULL; }
    out[n] = '\0';
    return out;
}

static uint8_t* b64_decode(const char* text, size_t* len_out)
{
    if (!text) return NULL;
    int max = (int)(strlen(text) * 3 / 4) + 3;
    uint8_t* out = malloc((size_t)max);
    if (!out) return NULL;
    int n = EVP_DecodeBlock(out, (const unsigned char*)text, (int)strlen(text));
    if (n < 0) { free(out); return NULL; }
    size_t tl = strlen(text);
    if (tl >= 1 && text[tl - 1] == '=') n--;
    if (tl >= 2 && text[tl - 2] == '=') n--;
    if (n < 0) { free(out); return NULL; }
    *len_out = (size_t)n;
    return out;
}

/* PBKDF2-HMAC-SHA256 via the OpenSSL 3 EVP_KDF API (no deprecated call). */
static bool pbkdf2_sha256(const uint8_t* pass, size_t pass_len,
                          const uint8_t* salt, size_t salt_len,
                          unsigned iters, uint8_t out[32])
{
    EVP_KDF* kdf = EVP_KDF_fetch(NULL, "PBKDF2", NULL);
    if (!kdf) return false;
    EVP_KDF_CTX* ctx = EVP_KDF_CTX_new(kdf);
    EVP_KDF_free(kdf);
    if (!ctx) return false;
    OSSL_PARAM params[5];
    int iters_i = (int)iters;
    size_t keylen = 32;
    const char* digest = "SHA256";
    params[0] = OSSL_PARAM_construct_utf8_string(OSSL_KDF_PARAM_DIGEST, (char*)digest, 0);
    params[1] = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_SALT, (void*)salt, salt_len);
    params[2] = OSSL_PARAM_construct_octet_string(OSSL_KDF_PARAM_PASSWORD, (void*)pass, pass_len);
    params[3] = OSSL_PARAM_construct_int(OSSL_KDF_PARAM_ITER, &iters_i);
    params[4] = OSSL_PARAM_construct_end();
    bool ok = EVP_KDF_derive(ctx, out, keylen, params) == 1;
    EVP_KDF_CTX_free(ctx);
    return ok;
}

/* ------------------------------------------------------------------- vault */
struct EcVault {
    char* path;
    uint8_t nonce[12];
    cJSON* secrets; /* object of id -> {user, pass}; plaintext RAM only */
    bool dirty;
};

static char* load_or_create_master(const char* vault_path, uint8_t master[32])
{
    char* dir = ec_path_dirname(vault_path);
    if (!dir) return NULL;
    char* master_path = ec_path_join(dir, "vault.key");
    free(dir);
    if (!master_path) return NULL;
    char* enc = NULL;
    if (ec_file_read_all(master_path, &enc, NULL)) {
        size_t ml = 0;
        uint8_t* m = b64_decode(enc, &ml);
        free(enc);
        if (!m || ml != 32) {
            free(m);
            free(master_path);
            return NULL;
        }
        memcpy(master, m, 32);
        ec_secure_wipe(m, ml);
        free(m);
        return master_path;
    }
    if (RAND_bytes(master, 32) != 1) { free(master_path); return NULL; }
    char* s = b64_encode(master, 32);
    if (!s || !ec_file_write_atomic(master_path, s, strlen(s)) ||
        !ec_set_file_mode_600(master_path)) {
        ec_secure_wipe(master, 32);
        free(s);
        free(master_path);
        return NULL;
    }
    free(s);
    return master_path;
}

static bool vault_write_file(EcVault* v)
{
    uint8_t master[32];
    char* master_path = load_or_create_master(v->path, master);
    if (!master_path) return false;

    uint8_t salt[16];
    if (RAND_bytes(salt, sizeof salt) != 1) goto fail;
    if (RAND_bytes(v->nonce, sizeof v->nonce) != 1) goto fail;

    uint8_t key[32];
    if (!pbkdf2_sha256(master, 32, salt, sizeof salt, 200000, key)) goto fail;
    ec_secure_wipe(master, 32);

    char* plain = cJSON_PrintUnformatted(v->secrets);
    if (!plain) { ec_secure_wipe(key, 32); goto fail; }
    size_t plain_len = strlen(plain);

    EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
    uint8_t* ct = malloc(plain_len + 32);
    int len = 0, ct_len = 0;
    bool ok = ctx && ct &&
              EVP_EncryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) == 1 &&
              EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, NULL) == 1 &&
              EVP_EncryptInit_ex(ctx, NULL, NULL, key, v->nonce) == 1 &&
              EVP_EncryptUpdate(ctx, ct, &len, (const uint8_t*)plain, (int)plain_len) == 1;
    if (ok) { ct_len = len; ok = EVP_EncryptFinal_ex(ctx, ct + len, &len) == 1; ct_len += len; }
    uint8_t tag[16];
    if (ok) ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_GET_TAG, 16, tag) == 1;
    EVP_CIPHER_CTX_free(ctx);
    ec_secure_wipe(key, 32);
    ec_secure_wipe(plain, plain_len);
    free(plain);
    if (!ok) { free(ct); goto fail; }

    char *salt_s = b64_encode(salt, sizeof salt);
    char *nonce_s = b64_encode(v->nonce, sizeof v->nonce);
    char *ct_s = b64_encode(ct, (size_t)ct_len);
    char *tag_s = b64_encode(tag, sizeof tag);
    free(ct);
    if (!salt_s || !nonce_s || !ct_s || !tag_s) {
        free(salt_s); free(nonce_s); free(ct_s); free(tag_s);
        goto fail;
    }
    cJSON* root = cJSON_CreateObject();
    if (!root) { free(salt_s); free(nonce_s); free(ct_s); free(tag_s); goto fail; }
    cJSON_AddNumberToObject(root, "version", 1);
    cJSON* kdf = cJSON_CreateObject();
    cJSON_AddStringToObject(kdf, "algo", "pbkdf2-hmac-sha256");
    cJSON_AddNumberToObject(kdf, "iters", 200000);
    cJSON_AddStringToObject(kdf, "salt", salt_s);
    cJSON_AddStringToObject(kdf, "note", "master key in vault.key (0600); DPAPI wrap future work");
    cJSON_AddItemToObject(root, "kdf", kdf);
    cJSON_AddStringToObject(root, "nonce", nonce_s);
    cJSON_AddStringToObject(root, "ciphertext", ct_s);
    cJSON_AddStringToObject(root, "tag", tag_s);
    free(salt_s); free(nonce_s); free(ct_s); free(tag_s);

    char* out = cJSON_PrintUnformatted(root);
    cJSON_Delete(root);
    ok = out && ec_file_write_atomic(v->path, out, strlen(out)) && ec_set_file_mode_600(v->path);
    if (out) { ec_secure_wipe(out, strlen(out)); free(out); }
    free(master_path);
    return ok;

fail:
    ec_secure_wipe(master, 32);
    free(master_path);
    return false;
}

static bool vault_read_file(EcVault* v)
{
    char* text = NULL;
    if (!ec_file_read_all(v->path, &text, NULL)) return false;
    cJSON* root = cJSON_Parse(text);
    free(text);
    if (!root) return false;
    cJSON *kdf = cJSON_GetObjectItem(root, "kdf");
    cJSON *nonce_j = cJSON_GetObjectItem(root, "nonce");
    cJSON *ct_j = cJSON_GetObjectItem(root, "ciphertext");
    cJSON *tag_j = cJSON_GetObjectItem(root, "tag");
    cJSON *salt_j = kdf ? cJSON_GetObjectItem(kdf, "salt") : NULL;
    cJSON *iters_j = kdf ? cJSON_GetObjectItem(kdf, "iters") : NULL;
    bool ok = false;
    do {
        if (!cJSON_IsString(nonce_j) || !cJSON_IsString(ct_j) || !cJSON_IsString(tag_j) ||
            !cJSON_IsString(salt_j) || !cJSON_IsNumber(iters_j))
            break;
        size_t nonce_len = 0, ct_len = 0, tag_len = 0, salt_len = 0;
        uint8_t* nonce = b64_decode(nonce_j->valuestring, &nonce_len);
        uint8_t* ct = b64_decode(ct_j->valuestring, &ct_len);
        uint8_t* tag = b64_decode(tag_j->valuestring, &tag_len);
        uint8_t* salt = b64_decode(salt_j->valuestring, &salt_len);
        if (!nonce || !ct || !tag || !salt || nonce_len != 12 || tag_len != 16 || salt_len == 0) {
            free(nonce); free(ct); free(tag); free(salt);
            break;
        }
        uint8_t master[32];
        char* master_path = load_or_create_master(v->path, master);
        uint8_t key[32];
        if (!master_path ||
            !pbkdf2_sha256(master, 32, salt, salt_len, (unsigned)iters_j->valueint, key)) {
            free(nonce); free(ct); free(tag); free(salt);
            free(master_path);
            break;
        }
        ec_secure_wipe(master, 32);
        free(master_path);

        EVP_CIPHER_CTX* ctx = EVP_CIPHER_CTX_new();
        int len = 0, plen = 0;
        uint8_t* plain = malloc(ct_len + 1);
        ok = ctx && plain &&
             EVP_DecryptInit_ex(ctx, EVP_aes_256_gcm(), NULL, NULL, NULL) == 1 &&
             EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_IVLEN, 12, NULL) == 1 &&
             EVP_DecryptInit_ex(ctx, NULL, NULL, key, nonce) == 1 &&
             EVP_DecryptUpdate(ctx, plain, &len, ct, (int)ct_len) == 1;
        if (ok) {
            plen = len;
            ok = EVP_CIPHER_CTX_ctrl(ctx, EVP_CTRL_GCM_SET_TAG, 16, tag) == 1;
        }
        if (ok) ok = EVP_DecryptFinal_ex(ctx, plain + len, &len) == 1;
        if (ok) plen += len;
        EVP_CIPHER_CTX_free(ctx);
        ec_secure_wipe(key, 32);
        free(nonce); free(ct); free(tag); free(salt);
        if (!ok) { free(plain); break; }
        plain[plen] = '\0';
        cJSON* secrets = cJSON_Parse((const char*)plain);
        ec_secure_wipe(plain, (size_t)plen);
        free(plain);
        if (!secrets || !cJSON_IsObject(secrets)) {
            if (secrets) cJSON_Delete(secrets);
            ok = false;
            break;
        }
        cJSON_Delete(v->secrets);
        v->secrets = secrets;
        ok = true;
    } while (0);
    cJSON_Delete(root);
    return ok;
}

EcVault* ec_vault_open(const char* path)
{
    if (!path) return NULL;
    EcVault* v = calloc(1, sizeof(EcVault));
    if (!v) return NULL;
    v->path = ec_strdup(path);
    v->secrets = cJSON_CreateObject();
    if (!v->path || !v->secrets) { ec_vault_free(v); return NULL; }
    if (ec_file_exists(path)) {
        if (!vault_read_file(v)) {
            EC_LOGW("vault", "vault unreadable (corrupt or foreign) - starting empty");
            cJSON_Delete(v->secrets);
            v->secrets = cJSON_CreateObject();
            if (!v->secrets) { ec_vault_free(v); return NULL; }
        }
    } else {
        v->dirty = true;
        if (!ec_vault_save(v)) EC_LOGW("vault", "initial vault save failed");
    }
    return v;
}

void ec_vault_free(EcVault* v)
{
    if (!v) return;
    free(v->path);
    if (v->secrets) {
        char* dump = cJSON_PrintUnformatted(v->secrets);
        if (dump) { ec_secure_wipe(dump, strlen(dump)); free(dump); }
        cJSON_Delete(v->secrets);
    }
    free(v);
}

bool ec_vault_set(EcVault* v, const char* id, const char* username, const char* secret)
{
    if (!v || !id || !*id) return false;
    cJSON* item = cJSON_CreateObject();
    if (!item) return false;
    cJSON_AddStringToObject(item, "user", username ? username : "");
    cJSON_AddStringToObject(item, "pass", secret ? secret : "");
    cJSON* old = cJSON_DetachItemFromObjectCaseSensitive(v->secrets, id);
    if (old) cJSON_Delete(old);
    cJSON_AddItemToObject(v->secrets, id, item);
    v->dirty = true;
    return ec_vault_save(v);
}

bool ec_vault_get(EcVault* v, const char* id, char** username, char** secret)
{
    if (!v || !id) return false;
    cJSON* item = cJSON_GetObjectItemCaseSensitive(v->secrets, id);
    if (!cJSON_IsObject(item)) return false;
    cJSON* u = cJSON_GetObjectItemCaseSensitive(item, "user");
    cJSON* p = cJSON_GetObjectItemCaseSensitive(item, "pass");
    if (username) *username = cJSON_IsString(u) ? ec_strdup(u->valuestring) : NULL;
    if (secret) *secret = cJSON_IsString(p) ? ec_strdup(p->valuestring) : NULL;
    return true;
}

bool ec_vault_delete(EcVault* v, const char* id)
{
    if (!v || !id) return false;
    cJSON* old = cJSON_DetachItemFromObjectCaseSensitive(v->secrets, id);
    if (!old) return false;
    cJSON_Delete(old);
    v->dirty = true;
    return ec_vault_save(v);
}

bool ec_vault_has(EcVault* v, const char* id)
{
    return v && id && cJSON_GetObjectItemCaseSensitive(v->secrets, id) != NULL;
}

bool ec_vault_save(EcVault* v)
{
    if (!v || !v->dirty) return true;
    bool ok = vault_write_file(v);
    if (ok) v->dirty = false;
    return ok;
}

char** ec_vault_ids(EcVault* v, size_t* count_out)
{
    *count_out = 0;
    if (!v) return NULL;
    size_t n = (size_t)cJSON_GetArraySize(v->secrets);
    char** ids = calloc(n + 1, sizeof(char*));
    if (!ids) return NULL;
    size_t i = 0;
    cJSON* it;
    cJSON_ArrayForEach(it, v->secrets) {
        if (it->string) ids[i++] = ec_strdup(it->string);
    }
    *count_out = i;
    return ids;
}

/* -------------------------------------------------------- host key store */
struct EcHostKeyStore {
    char* path; /* known_hosts file */
};

char* ec_hostkeys_normalize_host(const char* host, int port)
{
    if (!host || !*host) return NULL;
    EcStr b;
    ec_str_init(&b);
    bool v6 = strchr(host, ':') != NULL;
    if (port == 22)
        ec_str_printf(&b, v6 ? "[%s]" : "%s", host);
    else
        ec_str_printf(&b, v6 ? "[%s]:%d" : "%s:%d", host, port);
    return ec_str_take(&b);
}

EcHostKeyStore* ec_hostkeys_open(const char* known_hosts_path)
{
    if (!known_hosts_path) return NULL;
    EcHostKeyStore* s = calloc(1, sizeof(EcHostKeyStore));
    if (!s) return NULL;
    s->path = ec_strdup(known_hosts_path);
    if (!s->path) { free(s); return NULL; }
    if (!ec_file_exists(s->path)) {
        char* dir = ec_path_dirname(s->path);
        if (dir) { ec_mkdir_p(dir); free(dir); }
        ec_file_write_atomic(s->path, "", 0);
        ec_set_file_mode_600(s->path);
    }
    return s;
}

void ec_hostkeys_free(EcHostKeyStore* s)
{
    if (!s) return;
    free(s->path);
    free(s);
}

static bool hk_line_matches(const char* line, const char* norm)
{
    size_t nl = strlen(norm);
    if (strncmp(line, norm, nl) != 0) return false;
    char next = line[nl];
    return next == ' ' || next == '\0' || next == ',';
}

EcHostKeyStatus ec_hostkeys_check(EcHostKeyStore* s, const char* host, int port,
                                  const char* key_type, const char* key_base64)
{
    if (!s || !host || !key_type || !key_base64) return EC_HK_ERROR;
    char* norm = ec_hostkeys_normalize_host(host, port);
    if (!norm) return EC_HK_ERROR;
    char* text = NULL;
    if (!ec_file_read_all(s->path, &text, NULL)) {
        free(norm);
        return EC_HK_UNKNOWN; /* empty store */
    }
    bool seen_host = false, seen_key = false;
    char* saveptr = NULL;
    char* dup = ec_strdup(text);
    free(text);
    if (!dup) { free(norm); return EC_HK_ERROR; }
    for (char* line = strtok_r(dup, "\n", &saveptr); line; line = strtok_r(NULL, "\n", &saveptr)) {
        if (hk_line_matches(line, norm)) {
            seen_host = true;
            char* sp = NULL;
            char* tok_host = strtok_r(line, " ", &sp);
            char* tok_type = tok_host ? strtok_r(NULL, " ", &sp) : NULL;
            char* tok_key = tok_type ? strtok_r(NULL, " ", &sp) : NULL;
            if (tok_type && tok_key &&
                strcmp(tok_type, key_type) == 0 && strcmp(tok_key, key_base64) == 0)
                seen_key = true;
        }
    }
    free(dup);
    free(norm);
    if (seen_key) return EC_HK_OK;
    if (seen_host) return EC_HK_CHANGED;
    return EC_HK_UNKNOWN;
}

bool ec_hostkeys_accept(EcHostKeyStore* s, const char* host, int port,
                        const char* key_type, const char* key_base64)
{
    if (!s || !host || !key_type || !key_base64) return false;
    ec_hostkeys_remove(s, host, port); /* replaced below */
    char* norm = ec_hostkeys_normalize_host(host, port);
    if (!norm) return false;
    char* text = NULL;
    if (!ec_file_read_all(s->path, &text, NULL)) text = ec_strdup("");
    EcStr b;
    ec_str_init(&b);
    if (text && *text) {
        ec_str_append(&b, text);
        if (text[strlen(text) - 1] != '\n') ec_str_append_ch(&b, '\n');
    }
    ec_str_printf(&b, "%s %s %s\n", norm, key_type, key_base64);
    bool ok = ec_file_write_atomic(s->path, b.s ? b.s : "", b.len) && ec_set_file_mode_600(s->path);
    ec_str_free(&b);
    free(text);
    free(norm);
    return ok;
}

bool ec_hostkeys_remove(EcHostKeyStore* s, const char* host, int port)
{
    if (!s || !host) return false;
    char* norm = ec_hostkeys_normalize_host(host, port);
    if (!norm) return false;
    char* text = NULL;
    if (!ec_file_read_all(s->path, &text, NULL)) { free(norm); return true; }
    EcStr b;
    ec_str_init(&b);
    char* saveptr = NULL;
    char* dup = ec_strdup(text);
    free(text);
    bool changed = false;
    if (dup) {
        for (char* line = strtok_r(dup, "\n", &saveptr); line; line = strtok_r(NULL, "\n", &saveptr)) {
            if (hk_line_matches(line, norm)) { changed = true; continue; }
            ec_str_append(&b, line);
            ec_str_append_ch(&b, '\n');
        }
        free(dup);
    }
    bool ok = true;
    if (changed) ok = ec_file_write_atomic(s->path, b.s ? b.s : "", b.len);
    ec_str_free(&b);
    free(norm);
    return ok;
}

/* ----------------------------------------------------------- key manager */
void ec_keyinfo_free(EcKeyInfo* k)
{
    if (!k) return;
    free(k->path);
    free(k->type);
    free(k->fingerprint);
    free(k->comment);
    free(k);
}

void ec_keyinfo_array_free(EcKeyInfo** keys, size_t n)
{
    if (!keys) return;
    for (size_t i = 0; i < n; i++) ec_keyinfo_free(keys[i]);
    free(keys);
}

/* SHA256:"base64" fingerprint of a key's public blob (libssh blob API + OpenSSL). */
static char* key_fingerprint(ssh_key key)
{
    char* blob_b64 = NULL;
    if (ssh_pki_export_pubkey_base64(key, &blob_b64) != SSH_OK || !blob_b64)
        return NULL;
    size_t blob_len = 0;
    uint8_t* blob = b64_decode(blob_b64, &blob_len);
    ssh_string_free_char(blob_b64);
    if (!blob) return NULL;
    uint8_t digest[32];
    SHA256(blob, blob_len, digest);
    free(blob);
    char* b = b64_encode(digest, 32);
    if (!b) return NULL;
    size_t bl = strlen(b);
    while (bl && b[bl - 1] == '=') b[--bl] = '\0';
    EcStr out;
    ec_str_init(&out);
    ec_str_printf(&out, "SHA256:%s", b);
    free(b);
    return ec_str_take(&out);
}

char* ec_keymgr_fingerprint(const char* private_path)
{
    if (!private_path) return NULL;
    ssh_key key = NULL;
    int rc = ssh_pki_import_privkey_file(private_path, NULL, NULL, NULL, &key);
    if (rc != SSH_OK) return NULL;
    char* fp = key_fingerprint(key);
    ssh_key_free(key);
    return fp;
}

bool ec_keymgr_validate(const char* private_path, const char* passphrase)
{
    if (!private_path) return false;
    ssh_key key = NULL;
    int rc = ssh_pki_import_privkey_file(private_path, passphrase ? passphrase : NULL, NULL, NULL, &key);
    if (rc != SSH_OK) return false;
    ssh_key_free(key);
    return true;
}

static const char* map_key_type(ssh_key k)
{
    switch (ssh_key_type(k)) {
    case SSH_KEYTYPE_ED25519: return "ssh-ed25519";
    case SSH_KEYTYPE_RSA:
    case SSH_KEYTYPE_RSA1: return "ssh-rsa";
    case SSH_KEYTYPE_ECDSA_P256:
    case SSH_KEYTYPE_ECDSA_P384:
    case SSH_KEYTYPE_ECDSA_P521:
    case SSH_KEYTYPE_ECDSA: return "ecdsa-sha2-nistp";
    case SSH_KEYTYPE_DSS: return "ssh-dss";
    default: return "unknown";
    }
}

EcKeyInfo** ec_keymgr_scan(const char* dir, size_t* count_out)
{
    *count_out = 0;
    if (!dir) return NULL;
    EcVec found;
    ec_vec_init(&found);
    EcDirIter* dit = NULL;
    if (ec_dir_iter_open(&dit, dir)) {
        char* name;
        while ((name = ec_dir_iter_next(dit)) != NULL) {
            char* full = ec_path_join(dir, name);
            free(name);
            if (!full) continue;
            bool skip = false;
            for (const char* c = full; *c; c++) {
                if (strstr(full, ".pub") == c) { skip = true; break; } /* cheap: skip *.pub */
            }
            if (!skip && ec_keymgr_validate(full, NULL)) {
                ssh_key key = NULL;
                if (ssh_pki_import_privkey_file(full, NULL, NULL, NULL, &key) == SSH_OK) {
                    EcKeyInfo* info = calloc(1, sizeof(EcKeyInfo));
                    if (info) {
                        info->path = ec_strdup(full);
                        info->type = ec_strdup(map_key_type(key));
                        info->fingerprint = key_fingerprint(key);
                        info->comment = NULL;
                        if (!ec_vec_push(&found, info)) ec_keyinfo_free(info);
                    }
                    ssh_key_free(key);
                }
            }
            free(full);
        }
        ec_dir_iter_close(dit);
    }
    if (found.len == 0) { ec_vec_free(&found); return NULL; }
    *count_out = found.len;
    return (EcKeyInfo**)found.items;
}

bool ec_keymgr_generate(const char* type_s, unsigned bits, const char* comment,
                        const char* passphrase, const char* out_private_path, char** err)
{
    enum ssh_keytypes_e type = SSH_KEYTYPE_UNKNOWN;
    if (!type_s) type = SSH_KEYTYPE_ED25519;
    else if (strcmp(type_s, "ed25519") == 0) type = SSH_KEYTYPE_ED25519;
    else if (strcmp(type_s, "rsa") == 0) type = SSH_KEYTYPE_RSA;
    else if (strcmp(type_s, "ecdsa") == 0) type = SSH_KEYTYPE_ECDSA_P256;
    else {
        if (err) *err = ec_strdup("Unknown key type");
        return false;
    }
    if (type == SSH_KEYTYPE_RSA && (bits == 0)) bits = 3072;
    ssh_key key = NULL;
    int rc = ssh_pki_generate(type, (int)bits, &key);
    if (rc != SSH_OK || !key) {
        if (err) *err = ec_strdup("Key generation failed");
        return false;
    }
    (void)comment;
    rc = ssh_pki_export_privkey_file(key, passphrase, NULL, NULL, out_private_path);
    if (rc != SSH_OK) {
        if (err) *err = ec_strdup("Could not write private key file");
        ssh_key_free(key);
        return false;
    }
    ec_set_file_mode_600(out_private_path);
    /* write <path>.pub from the same key */
    {
        char* pub_path = malloc(strlen(out_private_path) + 5);
        if (pub_path) {
            snprintf(pub_path, strlen(out_private_path) + 5, "%s.pub", out_private_path);
            char* blob_b64 = NULL;
            if (ssh_pki_export_pubkey_base64(key, &blob_b64) == SSH_OK && blob_b64) {
                EcStr b;
                ec_str_init(&b);
                ec_str_printf(&b, "%s %s\n", map_key_type(key), blob_b64);
                ec_file_write_atomic(pub_path, b.s ? b.s : "", b.len);
                ec_str_free(&b);
                ssh_string_free_char(blob_b64);
            }
            free(pub_path);
        }
    }
    ssh_key_free(key);
    return true;
}

bool ec_keymgr_import(const char* source_path, const char* dest_dir, char** err)
{
    if (!source_path || !dest_dir) return false;
    if (!ec_keymgr_validate(source_path, NULL)) {
        if (err) *err = ec_strdup("Not a readable OpenSSH private key (passphrase-protected keys need agent import)");
        return false;
    }
    char* base = ec_path_basename(source_path);
    if (!base) return false;
    char* dest = ec_path_join(dest_dir, base);
    free(base);
    if (!dest) return false;
    char* text = NULL;
    size_t len = 0;
    if (!ec_file_read_all(source_path, &text, &len)) {
        if (err) *err = ec_strdup("Cannot read source key");
        free(dest);
        return false;
    }
    bool ok = ec_file_write_atomic(dest, text, len) && ec_set_file_mode_600(dest);
    ec_secure_wipe(text, len);
    free(text);
    free(dest);
    if (!ok && err) *err = ec_strdup("Copy failed");
    return ok;
}

bool ec_keymgr_export_public(const char* private_path, const char* out_public_path, char** err)
{
    ssh_key key = NULL;
    if (ssh_pki_import_privkey_file(private_path, NULL, NULL, NULL, &key) != SSH_OK) {
        if (err) *err = ec_strdup("Cannot read private key");
        return false;
    }
    char* blob_b64 = NULL;
    if (ssh_pki_export_pubkey_base64(key, &blob_b64) != SSH_OK || !blob_b64) {
        if (err) *err = ec_strdup("Cannot export public blob");
        ssh_key_free(key);
        return false;
    }
    EcStr b;
    ec_str_init(&b);
    ec_str_printf(&b, "%s %s\n", map_key_type(key), blob_b64);
    ssh_string_free_char(blob_b64);
    ssh_key_free(key);
    bool ok = ec_file_write_atomic(out_public_path, b.s ? b.s : "", b.len);
    ec_str_free(&b);
    if (!ok && err) *err = ec_strdup("Write failed");
    return ok;
}
