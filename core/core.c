/* Eclipse SSH - core implementation. See eclipse/core.h */
#include "eclipse/core.h"
#include "eclipse/platform.h"

#include <stdio.h>
#include <time.h>

/* ---------------------------------------------------------------- logging */
static EcLogLevel g_level = EC_LOG_INFO;
static EcLogSink g_sink = NULL;
static void* g_sink_user = NULL;

void ec_log_set_level(EcLogLevel lvl) { g_level = lvl; }
void ec_log_set_sink(EcLogSink sink, void* user) { g_sink = sink; g_sink_user = user; }

const char* ec_redact(const char* secret) { return secret ? "***" : "(none)"; }

static const char* lvl_name(EcLogLevel l)
{
    switch (l) {
    case EC_LOG_DEBUG: return "D";
    case EC_LOG_INFO: return "I";
    case EC_LOG_WARN: return "W";
    default: return "E";
    }
}

void ec_log(EcLogLevel lvl, const char* domain, const char* fmt, ...)
{
    if (lvl < g_level)
        return;
    char msg[2048];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(msg, sizeof msg, fmt, ap);
    va_end(ap);
    if (g_sink) {
        g_sink(lvl, domain ? domain : "app", msg, g_sink_user);
        return;
    }
    fprintf(stderr, "[%s] %s: %s\n", lvl_name(lvl), domain ? domain : "app", msg);
}

/* ------------------------------------------------------------ string buffer */
void ec_str_init(EcStr* b) { b->s = NULL; b->len = 0; b->cap = 0; }

void ec_str_free(EcStr* b)
{
    free(b->s);
    ec_str_init(b);
}

static bool str_reserve(EcStr* b, size_t extra)
{
    size_t need;
    if (!ec_add_u64(b->len, extra, &need) || !ec_add_u64(need, 1, &need))
        return false;
    if (need <= b->cap)
        return true;
    size_t cap = b->cap ? b->cap : 64;
    while (cap < need) {
        if (cap > SIZE_MAX / 2) return false;
        cap *= 2;
    }
    char* p = realloc(b->s, cap);
    if (!p) return false;
    b->s = p;
    b->cap = cap;
    if (b->len == 0) b->s[0] = '\0';
    return true;
}

int ec_str_append_n(EcStr* b, const char* text, size_t n)
{
    if (!text || n == 0) { if (b->cap == 0 && !str_reserve(b, 0)) return EC_NOMEM; return EC_OK; }
    if (!str_reserve(b, n)) return EC_NOMEM;
    memcpy(b->s + b->len, text, n);
    b->len += n;
    b->s[b->len] = '\0';
    return EC_OK;
}

int ec_str_append(EcStr* b, const char* text) { return ec_str_append_n(b, text, text ? strlen(text) : 0); }

int ec_str_append_ch(EcStr* b, char c) { return ec_str_append_n(b, &c, 1); }

int ec_str_printf(EcStr* b, const char* fmt, ...)
{
    va_list ap, ap2;
    va_start(ap, fmt);
    va_copy(ap2, ap);
    int n = vsnprintf(NULL, 0, fmt, ap);
    va_end(ap);
    if (n < 0) { va_end(ap2); return EC_ERR; }
    if (!str_reserve(b, (size_t)n)) { va_end(ap2); return EC_NOMEM; }
    vsnprintf(b->s + b->len, (size_t)n + 1, fmt, ap2);
    va_end(ap2);
    b->len += (size_t)n;
    return EC_OK;
}

char* ec_str_take(EcStr* b)
{
    char* s = b->s;
    if (!s) s = ec_strdup("");
    ec_str_init(b);
    return s;
}

/* ------------------------------------------------------------------- util */
char* ec_strdup(const char* s) { return s ? ec_strndup(s, strlen(s)) : NULL; }

char* ec_strndup(const char* s, size_t n)
{
    char* p = malloc(n + 1);
    if (!p) return NULL;
    memcpy(p, s, n);
    p[n] = '\0';
    return p;
}

char* ec_path_join(const char* a, const char* b)
{
    if (!a || !*a) return ec_strdup(b ? b : "");
    if (!b || !*b) return ec_strdup(a);
    EcStr s;
    ec_str_init(&s);
    ec_str_printf(&s, "%s%s%s", a, (a[strlen(a) - 1] == '/' || b[0] == '/') ? "" : "/", b);
    return ec_str_take(&s);
}

char* ec_path_dirname(const char* path)
{
    if (!path) return ec_strdup(".");
    const char* last = strrchr(path, '/');
    if (!last) return ec_strdup(".");
    if (last == path) return ec_strdup("/");
    return ec_strndup(path, (size_t)(last - path));
}

char* ec_path_basename(const char* path)
{
    if (!path || !*path) return ec_strdup(".");
    const char* last = strrchr(path, '/');
    return ec_strdup(last ? last + 1 : path);
}

bool ec_mkdir_p(const char* path)
{
    if (!path || !*path) return false;
    char* copy = ec_strdup(path);
    if (!copy) return false;
    for (char* p = copy + 1; *p; p++) {
        if (*p == '/') {
            *p = '\0';
            ec_dir_mkdir(copy); /* ignore exists-errors */
            *p = '/';
        }
    }
    bool ok = ec_dir_mkdir(copy);
    free(copy);
    return ok;
}

bool ec_file_read_all(const char* path, char** out, size_t* len_out)
{
    *out = NULL;
    if (len_out) *len_out = 0;
    FILE* f = ec_fopen(path, "rb");
    if (!f) return false;
    if (fseek(f, 0, SEEK_END) != 0) { fclose(f); return false; }
    long sz = ftell(f);
    if (sz < 0) { fclose(f); return false; }
    rewind(f);
    char* buf = malloc((size_t)sz + 1);
    if (!buf) { fclose(f); return false; }
    size_t rd = fread(buf, 1, (size_t)sz, f);
    fclose(f);
    if (rd != (size_t)sz) { free(buf); return false; }
    buf[sz] = '\0';
    *out = buf;
    if (len_out) *len_out = (size_t)sz;
    return true;
}

bool ec_file_write_atomic(const char* path, const char* data, size_t len)
{
    char* tmp = ec_file_tmpname(path);
    if (!tmp) return false;
    FILE* f = ec_fopen(tmp, "wb");
    if (!f) { free(tmp); return false; }
    size_t wr = len ? fwrite(data, 1, len, f) : 0;
    bool ok = (wr == len);
    if (ok) ok = (fflush(f) == 0);
    fclose(f);
    if (!ok) { ec_remove(tmp); free(tmp); return false; }
    ok = ec_rename(tmp, path);
    free(tmp);
    return ok;
}

bool ec_file_exists(const char* path)
{
    return ec_stat_exists(path);
}

bool ec_file_size(const char* path, uint64_t* out)
{
    return ec_stat_size(path, out);
}

char* ec_file_tmpname(const char* prefix)
{
    static uint32_t counter = 0;
    EcStr b;
    ec_str_init(&b);
    ec_str_printf(&b, "%s.tmp.%u.%u", prefix ? prefix : "/tmp/ec", (unsigned)ec_getpid(), (unsigned)(++counter));
    return ec_str_take(&b);
}

void* ec_memset_secure(void* p, int c, size_t n)
{
    volatile unsigned char* vp = (volatile unsigned char*)p;
    while (n--) *vp++ = (unsigned char)c;
    return p;
}

void ec_secure_wipe(void* p, size_t n) { if (p && n) ec_memset_secure(p, 0, n); }

int64_t ec_now_ms(void)
{
    struct timespec ts;
#if defined(CLOCK_MONOTONIC)
    clock_gettime(CLOCK_MONOTONIC, &ts);
#else
    clock_gettime(CLOCK_REALTIME, &ts);
#endif
    return (int64_t)ts.tv_sec * 1000 + ts.tv_nsec / 1000000;
}

void ec_human_size(uint64_t bytes, char* out, size_t outsz)
{
    static const char* units[] = { "B", "KiB", "MiB", "GiB", "TiB" };
    double v = (double)bytes;
    int u = 0;
    while (v >= 1024.0 && u < 4) { v /= 1024.0; u++; }
    if (u == 0) snprintf(out, outsz, "%u B", (unsigned)bytes);
    else snprintf(out, outsz, "%.1f %s", v, units[u]);
}

bool ec_mul_u64(uint64_t a, uint64_t b, uint64_t* out)
{
    if (a && b > UINT64_MAX / a) return false;
    *out = a * b;
    return true;
}

bool ec_add_u64(uint64_t a, uint64_t b, uint64_t* out)
{
    if (b > UINT64_MAX - a) return false;
    *out = a + b;
    return true;
}

uint64_t ec_hash_str(const char* s)
{
    uint64_t h = 1469598103934665603ULL;
    for (; s && *s; s++) { h ^= (unsigned char)*s; h *= 1099511628211ULL; }
    return h;
}

char* ec_hex_encode(const uint8_t* data, size_t len)
{
    static const char* hexd = "0123456789abcdef";
    char* out = malloc(len * 2 + 1);
    if (!out) return NULL;
    for (size_t i = 0; i < len; i++) {
        out[i * 2] = hexd[data[i] >> 4];
        out[i * 2 + 1] = hexd[data[i] & 0xF];
    }
    out[len * 2] = '\0';
    return out;
}

bool ec_parse_hostport(const char* spec, char** host, int* port, int default_port)
{
    *host = NULL;
    *port = default_port;
    if (!spec || !*spec) return false;
    const char* hp = spec;
    /* [v6]:port or host:port or host */
    const char* colon = NULL;
    if (*hp == '[') {
        const char* close = strchr(hp, ']');
        if (!close) return false;
        colon = close + 1;
        if (*colon != ':') colon = NULL;
    } else {
        const char* c1 = strchr(hp, ':');
        if (c1 && strchr(c1 + 1, ':') == NULL) colon = c1; /* single colon => host:port */
    }
    if (colon) {
        char* endp = NULL;
        long p = strtol(colon + 1, &endp, 10);
        if (!endp || *endp != '\0' || p <= 0 || p > 65535) return false;
        *port = (int)p;
        size_t hlen = (size_t)(colon - hp);
        /* strip brackets */
        if (hp[0] == '[' && hlen >= 2 && hp[hlen - 1] == ']') hlen -= 2, hp++;
        *host = ec_strndup(hp, hlen);
    } else {
        size_t hl = strlen(hp);
        if (hp[0] == '[' && hp[hl - 1] == ']') { hp++; hl -= 2; }
        *host = ec_strndup(hp, hl);
    }
    return *host && **host;
}

bool ec_valid_port(int port) { return port > 0 && port <= 65535; }

static char loc_lower(char c) { return (c >= 'A' && c <= 'Z') ? (char)(c + 32) : c; }

bool ec_glob_match(const char* pattern, const char* text)
{
    if (!pattern || !text) return false;
    /* iterative wildcard match with backtracking, O(n*m) worst case */
    const char* p = pattern;
    const char* t = text;
    const char* star_p = NULL;
    const char* star_t = NULL;
    while (*t) {
        if (*p == '*' ) {
            star_p = p++;
            star_t = t;
        } else if (*p == '?' || loc_lower(*p) == loc_lower(*t)) {
            p++;
            t++;
        } else if (star_p) {
            p = star_p + 1;
            t = ++star_t;
        } else {
            return false;
        }
    }
    while (*p == '*') p++;
    return *p == '\0';
}

/* ------------------------------------------------------------------- EcVec */
void ec_vec_init(EcVec* v) { v->items = NULL; v->len = 0; v->cap = 0; }

void ec_vec_free(EcVec* v) { free(v->items); ec_vec_init(v); }

void ec_vec_free_full(EcVec* v)
{
    for (size_t i = 0; i < v->len; i++) free(v->items[i]);
    ec_vec_free(v);
}

bool ec_vec_push(EcVec* v, void* item)
{
    if (v->len == v->cap) {
        size_t cap = v->cap ? v->cap * 2 : 8;
        void** p = realloc(v->items, cap * sizeof(void*));
        if (!p) return false;
        v->items = p;
        v->cap = cap;
    }
    v->items[v->len++] = item;
    return true;
}

bool ec_vec_remove_at(EcVec* v, size_t i)
{
    if (i >= v->len) return false;
    v->items[i] = v->items[--v->len];
    return true;
}

void* ec_vec_last(EcVec* v) { return v->len ? v->items[v->len - 1] : NULL; }
