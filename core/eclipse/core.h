/* Eclipse SSH - core primitives: logging, dynamic strings, fs util, errors.
 * Part of the shared core; must stay platform-independent (C11, POSIX/Win32 via platform.h).
 */
#ifndef ECLIPSE_CORE_H
#define ECLIPSE_CORE_H

#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdbool.h>
#include <stdlib.h>
#include <string.h>

#ifdef __cplusplus
extern "C" {
#endif

#define EC_OK 0
#define EC_ERR (-1)
#define EC_NOMEM (-2)
#define EC_INVAL (-3)
#define EC_IO (-4)
#define EC_AUTH (-5)
#define EC_CANCELLED (-6)
#define EC_AGAIN (-7)

/* ---------------------------------------------------------------- logging */
typedef enum { EC_LOG_DEBUG = 0, EC_LOG_INFO, EC_LOG_WARN, EC_LOG_ERROR } EcLogLevel;

typedef void (*EcLogSink)(EcLogLevel lvl, const char* domain, const char* msg, void* user);

void ec_log_set_level(EcLogLevel lvl);
void ec_log_set_sink(EcLogSink sink, void* user); /* NULL sink => stderr */
void ec_log(EcLogLevel lvl, const char* domain, const char* fmt, ...);
#define EC_LOGD(dom, ...) ec_log(EC_LOG_DEBUG, dom, __VA_ARGS__)
#define EC_LOGI(dom, ...) ec_log(EC_LOG_INFO, dom, __VA_ARGS__)
#define EC_LOGW(dom, ...) ec_log(EC_LOG_WARN, dom, __VA_ARGS__)
#define EC_LOGE(dom, ...) ec_log(EC_LOG_ERROR, dom, __VA_ARGS__)
/* Never log credentials through the normal path; use this to redact. */
const char* ec_redact(const char* secret); /* returns "***" if non-NULL */

/* ------------------------------------------------------------ string buffer */
typedef struct {
    char* s;
    size_t len;
    size_t cap;
} EcStr;

void ec_str_init(EcStr* b);
void ec_str_free(EcStr* b);
int ec_str_printf(EcStr* b, const char* fmt, ...);
int ec_str_append(EcStr* b, const char* text);
int ec_str_append_n(EcStr* b, const char* text, size_t n);
int ec_str_append_ch(EcStr* b, char c);
char* ec_str_take(EcStr* b); /* hand ownership of NUL-terminated buffer to caller */

/* ------------------------------------------------------------------- util */
char* ec_strdup(const char* s);
char* ec_strndup(const char* s, size_t n);
char* ec_path_join(const char* a, const char* b);         /* malloc'd, '/'-normalized */
char* ec_path_dirname(const char* path);                  /* malloc'd */
char* ec_path_basename(const char* path);                 /* malloc'd */
bool ec_mkdir_p(const char* path);                        /* recursive, 0700 on new dirs */
bool ec_file_read_all(const char* path, char** out, size_t* len_out); /* NUL-terminated */
bool ec_file_write_atomic(const char* path, const char* data, size_t len); /* tmp+rename, 0600 */
bool ec_file_exists(const char* path);
bool ec_file_is_dir(const char* path);
bool ec_file_size(const char* path, uint64_t* out);
char* ec_file_tmpname(const char* prefix);                /* malloc'd */
void ec_secure_wipe(void* p, size_t n);
void* ec_memset_secure(void* p, int c, size_t n);

/* monotonic ms timestamp for timeouts/rate limiting */
int64_t ec_now_ms(void);
/* human size "1.2 MiB" into caller buffer */
void ec_human_size(uint64_t bytes, char* out, size_t outsz);
/* safe multiply/add with overflow check: returns false on overflow */
bool ec_mul_u64(uint64_t a, uint64_t b, uint64_t* out);
bool ec_add_u64(uint64_t a, uint64_t b, uint64_t* out);
/* FNV-1a for quick map keys */
uint64_t ec_hash_str(const char* s);
/* hex encode/decode helpers for fingerprints */
char* ec_hex_encode(const uint8_t* data, size_t len);
bool ec_parse_hostport(const char* spec, char** host, int* port, int default_port);
bool ec_valid_port(int port);
/* glob-ish match for filters: '*' any run, '?' single char; case-insensitive */
bool ec_glob_match(const char* pattern, const char* text);

/* simple dynamic pointer array */
typedef struct {
    void** items;
    size_t len;
    size_t cap;
} EcVec;

void ec_vec_init(EcVec* v);
void ec_vec_free(EcVec* v);                 /* frees array only */
void ec_vec_free_full(EcVec* v);            /* + free() each item */
bool ec_vec_push(EcVec* v, void* item);
bool ec_vec_remove_at(EcVec* v, size_t i);  /* unorders (swap-remove) */
void* ec_vec_last(EcVec* v);

#ifdef __cplusplus
}
#endif
#endif /* ECLIPSE_CORE_H */
