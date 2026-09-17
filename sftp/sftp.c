/* Eclipse SSH - SFTP engine + transfer queue implementation. */
#include "eclipse/sftp.h"
#include "eclipse/platform.h"

#include <fcntl.h>

#define SFTP_CHUNK 32768

struct EcSftp {
    sftp_session sftp;
    EcSshSession* session;
    char last_error[256];
};

static void set_serr(EcSftp* sf, const char* fallback)
{
    const char* e = ssh_get_error(sf->session->libssh);
    snprintf(sf->last_error, sizeof sf->last_error, "%s", (e && *e) ? e : (fallback ? fallback : "SFTP error"));
    EC_LOGE("sftp", "%s", sf->last_error);
}

const char* ec_sftp_last_error(EcSftp* sf) { return sf ? sf->last_error : "null"; }

EcSftp* ec_sftp_open(EcSshSession* s, char** err)
{
    if (!ec_ssh_is_connected(s)) {
        if (err) *err = ec_strdup("Not connected");
        return NULL;
    }
    sftp_session ses = sftp_new(s->libssh);
    if (!ses) {
        if (err) *err = ec_strdup(ssh_get_error(s->libssh));
        return NULL;
    }
    if (sftp_init(ses) != SSH_OK) {
        if (err) *err = ec_strdup(ssh_get_error(s->libssh));
        sftp_free(ses);
        return NULL;
    }
    EcSftp* sf = calloc(1, sizeof(EcSftp));
    if (!sf) { sftp_free(ses); return NULL; }
    sf->sftp = ses;
    sf->session = s;
    return sf;
}

void ec_sftp_free(EcSftp* sf)
{
    if (!sf) return;
    if (sf->sftp) sftp_free(sf->sftp);
    free(sf);
}

char* ec_sftp_canonicalize(EcSftp* sf, const char* path)
{
    if (!sf || !path) return NULL;
    char* rp = sftp_canonicalize_path(sf->sftp, path);
    if (!rp) {
        set_serr(sf, "canonicalize failed");
        return ec_strdup(path); /* graceful fallback */
    }
    char* out = ec_strdup(rp);
    ssh_string_free_char(rp);
    return out;
}

void ec_sftp_entries_free(EcSftpEntry* entries, size_t n)
{
    if (!entries) return;
    for (size_t i = 0; i < n; i++) {
        free(entries[i].name);
        free(entries[i].longname);
    }
    free(entries);
}

EcSftpEntry* ec_sftp_list(EcSftp* sf, const char* dir, size_t* count_out, char** err)
{
    *count_out = 0;
    if (!sf) return NULL;
    sftp_dir d = sftp_opendir(sf->sftp, dir);
    if (!d) {
        set_serr(sf, "opendir failed");
        if (err) *err = ec_strdup(sf->last_error);
        return NULL;
    }
    EcVec items;
    ec_vec_init(&items);
    sftp_attributes attr;
    while ((attr = sftp_readdir(sf->sftp, d)) != NULL) {
        const char* nm = attr->name;
        if (strcmp(nm, ".") != 0 && strcmp(nm, "..") != 0) {
            EcSftpEntry* e = calloc(1, sizeof(EcSftpEntry));
            if (e) {
                e->name = ec_strdup(nm);
                e->longname = attr->longname ? ec_strdup(attr->longname) : NULL;
                e->size = (uint64_t)attr->size;
                e->permissions = attr->permissions;
                e->uid = attr->uid;
                e->gid = attr->gid;
                e->mtime = (int64_t)attr->mtime;
                e->is_dir = attr->type == SSH_FILEXFER_TYPE_DIRECTORY;
                e->is_symlink = attr->type == SSH_FILEXFER_TYPE_SYMLINK;
                if (!ec_vec_push(&items, e)) ec_sftp_entries_free((EcSftpEntry*)items.items, items.len), ec_vec_free(&items);
            }
        }
        sftp_attributes_free(attr);
    }
    if (!sftp_dir_eof(d)) {
        set_serr(sf, "readdir failed");
        sftp_closedir(d);
        ec_sftp_entries_free((EcSftpEntry*)items.items, items.len);
        ec_vec_free(&items);
        return NULL;
    }
    sftp_closedir(d);
    *count_out = items.len;
    return (EcSftpEntry*)items.items;
}

bool ec_sftp_mkdir(EcSftp* sf, const char* path)
{
    int rc = sftp_mkdir(sf->sftp, path, 0755);
    if (rc != SSH_OK) set_serr(sf, "mkdir failed");
    return rc == SSH_OK;
}

bool ec_sftp_rmdir(EcSftp* sf, const char* path)
{
    int rc = sftp_rmdir(sf->sftp, path);
    if (rc != SSH_OK) set_serr(sf, "rmdir failed");
    return rc == SSH_OK;
}

bool ec_sftp_unlink(EcSftp* sf, const char* path)
{
    int rc = sftp_unlink(sf->sftp, path);
    if (rc != SSH_OK) set_serr(sf, "unlink failed");
    return rc == SSH_OK;
}

bool ec_sftp_rename(EcSftp* sf, const char* from, const char* to)
{
    int rc = sftp_rename(sf->sftp, from, to);
    if (rc != SSH_OK) set_serr(sf, "rename failed");
    return rc == SSH_OK;
}

bool ec_sftp_exists(EcSftp* sf, const char* path)
{
    sftp_attributes a = sftp_stat(sf->sftp, path);
    if (!a) return false;
    sftp_attributes_free(a);
    return true;
}

bool ec_sftp_isdir(EcSftp* sf, const char* path, bool* out)
{
    sftp_attributes a = sftp_stat(sf->sftp, path);
    if (!a) return false;
    *out = a->type == SSH_FILEXFER_TYPE_DIRECTORY;
    sftp_attributes_free(a);
    return true;
}

bool ec_sftp_stat(EcSftp* sf, const char* path, EcSftpEntry* out)
{
    sftp_attributes a = sftp_stat(sf->sftp, path);
    if (!a) { set_serr(sf, "stat failed"); return false; }
    memset(out, 0, sizeof *out);
    out->name = ec_path_basename(path);
    out->size = (uint64_t)a->size;
    out->permissions = a->permissions;
    out->uid = a->uid;
    out->gid = a->gid;
    out->mtime = (int64_t)a->mtime;
    out->is_dir = a->type == SSH_FILEXFER_TYPE_DIRECTORY;
    out->is_symlink = a->type == SSH_FILEXFER_TYPE_SYMLINK;
    sftp_attributes_free(a);
    return true;
}

bool ec_sftp_chmod(EcSftp* sf, const char* path, uint32_t mode)
{
    int rc = sftp_chmod(sf->sftp, path, mode);
    if (rc != SSH_OK) set_serr(sf, "chmod failed");
    return rc == SSH_OK;
}

/* ------------------------------------------------------ file transfers */
static bool wait_while_paused(EcTransfer* t)
{
    bool announced = false;
    for (;;) {
        g_mutex_lock(&t->lock);
        bool cancelled = t->cancel_req;
        bool paused = t->pause_req;
        g_mutex_unlock(&t->lock);
        if (cancelled) return false;
        if (!paused) return true;
        if (!announced) { t->state = EC_TR_PAUSED; announced = true; }
        g_usleep(100000);
    }
}

/* token-bucket rate limiter */
static void rate_limit_wait(EcTransfer* t, uint64_t limit_bps, uint64_t bytes_this_call, int64_t* last_ms, double* tokens)
{
    if (limit_bps == 0 || *last_ms == 0) {
        *last_ms = ec_now_ms();
        return;
    }
    int64_t now = ec_now_ms();
    double delta_s = (double)(now - *last_ms) / 1000.0;
    *last_ms = now;
    if (delta_s <= 0) return;
    *tokens += delta_s * (double)limit_bps;
    double max_tokens = (double)limit_bps; /* 1s burst ceiling */
    if (*tokens > max_tokens) *tokens = max_tokens;
    *tokens -= (double)bytes_this_call;
    if (*tokens < 0) {
        double deficit_s = -(*tokens) / (double)limit_bps;
        int ms = (int)(deficit_s * 1000.0);
        if (ms > 0) g_usleep((guint64)ms * 1000);
    }
}

static bool copy_chunks(FILE* local, sftp_file remote, bool upload, EcTransfer* t)
{
    char buf[SFTP_CHUNK];
    int64_t last_ms = ec_now_ms();
    double tokens = 0.0;
    for (;;) {
        if (!wait_while_paused(t)) return false;
        if (upload) {
            size_t n = fread(buf, 1, sizeof buf, local);
            if (n == 0) return feof(local) != 0;
            size_t off = 0;
            while (off < n) {
                ssize_t w = sftp_write(remote, buf + off, n - off);
                if (w <= 0) {
                    snprintf(t->error, sizeof t->error, "sftp_write failed");
                    return false;
                }
                off += (size_t)w;
                t->done += (uint64_t)w;
                if (t->progress_cb) t->progress_cb(t, t->user);
            }
            rate_limit_wait(t, t->speed_limit_bps, n, &last_ms, &tokens);
        } else {
            ssize_t n = sftp_read(remote, buf, sizeof buf);
            if (n < 0) {
                snprintf(t->error, sizeof t->error, "sftp_read failed");
                return false;
            }
            if (n == 0) return true;
            size_t off = 0;
            while (off < (size_t)n) {
                size_t w = fwrite(buf + off, 1, (size_t)n - off, local);
                if (w == 0) {
                    snprintf(t->error, sizeof t->error, "local write failed");
                    return false;
                }
                off += w;
                t->done += w;
                if (t->progress_cb) t->progress_cb(t, t->user);
            }
            rate_limit_wait(t, t->speed_limit_bps, (size_t)n, &last_ms, &tokens);
        }
    }
}

bool ec_sftp_upload_file(EcTransfer* t)
{
    t->state = EC_TR_RUNNING;
    if (!ec_file_size(t->local_path, &t->total)) t->total = 0;
    FILE* local = ec_fopen(t->local_path, "rb");
    if (!local) {
        snprintf(t->error, sizeof t->error, "Cannot open local file: %s", t->local_path);
        t->state = EC_TR_ERROR;
        return false;
    }
    sftp_file rf = sftp_open(t->sftp->sftp, t->remote_path, O_WRONLY | O_CREAT | O_TRUNC, 0644);
    if (!rf) {
        snprintf(t->error, sizeof t->error, "Cannot open remote file: %s (%s)", t->remote_path, ec_sftp_last_error(t->sftp));
        t->state = EC_TR_ERROR;
        fclose(local);
        return false;
    }
    bool ok = copy_chunks(local, rf, true, t);
    sftp_close(rf);
    fclose(local);
    t->state = ok ? EC_TR_DONE : EC_TR_ERROR;
    return ok;
}

bool ec_sftp_download_file(EcTransfer* t)
{
    t->state = EC_TR_RUNNING;
    sftp_attributes a = sftp_stat(t->sftp->sftp, t->remote_path);
    t->total = a ? (uint64_t)a->size : 0;
    if (a) sftp_attributes_free(a);
    sftp_file rf = sftp_open(t->sftp->sftp, t->remote_path, O_RDONLY, 0);
    if (!rf) {
        snprintf(t->error, sizeof t->error, "Cannot open remote file: %s (%s)", t->remote_path, ec_sftp_last_error(t->sftp));
        t->state = EC_TR_ERROR;
        return false;
    }
    FILE* local = ec_fopen(t->local_path, "wb");
    if (!local) {
        snprintf(t->error, sizeof t->error, "Cannot write local file: %s", t->local_path);
        t->state = EC_TR_ERROR;
        sftp_close(rf);
        return false;
    }
    bool ok = copy_chunks(local, rf, false, t);
    fclose(local);
    sftp_close(rf);
    t->state = ok ? EC_TR_DONE : EC_TR_ERROR;
    return ok;
}

/* --------------------------------------------------------- transfer queue */
typedef struct {
    EcTransfer* t;
    EcTransferQueue* q;
} TQJob;

void ec_tq_init(EcTransferQueue* q)
{
    ec_vec_init(&q->items);
    g_mutex_init(&q->lock);
    q->max_concurrent = 3;
    q->speed_limit_bps = 0;
}

static void tq_free_item(EcTransfer* t)
{
    g_mutex_clear(&t->lock);
    free(t->local_path);
    free(t->remote_path);
    free(t);
}

void ec_tq_free(EcTransferQueue* q)
{
    g_mutex_lock(&q->lock);
    for (size_t i = 0; i < q->items.len; i++)
        tq_free_item(q->items.items[i]);
    ec_vec_free(&q->items);
    g_mutex_unlock(&q->lock);
    g_mutex_clear(&q->lock);
}

void ec_tq_set_limits(EcTransferQueue* q, int max_concurrent, uint64_t speed_limit_bps)
{
    g_mutex_lock(&q->lock);
    q->max_concurrent = max_concurrent > 0 ? max_concurrent : 1;
    q->speed_limit_bps = speed_limit_bps;
    g_mutex_unlock(&q->lock);
}

static gpointer tq_thread(gpointer data)
{
    TQJob* job = data;
    EcTransfer* t = job->t;
    bool ok = t->kind == EC_TR_UPLOAD ? ec_sftp_upload_file(t) : ec_sftp_download_file(t);
    if (!ok && t->state != EC_TR_CANCELLED) t->state = EC_TR_ERROR;
    t->finished_ms = ec_now_ms();
    if (t->progress_cb) t->progress_cb(t, t->user);
    /* apply queue speed limit for next runs via settings; free job */
    free(job);
    return NULL;
}

EcTransfer* ec_tq_add(EcTransferQueue* q, EcTransferKind kind, EcSshSession* session,
                      EcSftp* sftp, const char* local_path, const char* remote_path,
                      EcTransferProgressCb cb, void* user)
{
    EcTransfer* t = calloc(1, sizeof(EcTransfer));
    if (!t) return NULL;
    t->kind = kind;
    t->state = EC_TR_PENDING;
    t->local_path = ec_strdup(local_path);
    t->remote_path = ec_strdup(remote_path);
    t->session = session;
    t->sftp = sftp;
    t->progress_cb = cb;
    t->user = user;
    t->speed_limit_bps = q->speed_limit_bps;
    g_mutex_init(&t->lock);
    g_mutex_lock(&q->lock);
    bool ok = ec_vec_push(&q->items, t);
    g_mutex_unlock(&q->lock);
    if (!ok) {
        tq_free_item(t);
        return NULL;
    }
    if (q->on_queue_changed) q->on_queue_changed(q->user);
    return t;
}

void ec_tq_pause(EcTransferQueue* q, EcTransfer* t)
{
    (void)q;
    g_mutex_lock(&t->lock);
    if (t->state == EC_TR_PENDING || t->state == EC_TR_RUNNING) t->pause_req = true;
    g_mutex_unlock(&t->lock);
}

void ec_tq_resume(EcTransferQueue* q, EcTransfer* t)
{
    (void)q;
    g_mutex_lock(&t->lock);
    t->pause_req = false;
    if (t->state == EC_TR_PAUSED) t->state = EC_TR_PENDING;
    g_mutex_unlock(&t->lock);
}

void ec_tq_cancel(EcTransferQueue* q, EcTransfer* t)
{
    (void)q;
    g_mutex_lock(&t->lock);
    t->cancel_req = true;
    t->pause_req = false;
    g_mutex_unlock(&t->lock);
}

void ec_tq_retry(EcTransferQueue* q, EcTransfer* t)
{
    g_mutex_lock(&t->lock);
    if (t->state == EC_TR_ERROR || t->state == EC_TR_CANCELLED) {
        t->done = 0;
        t->error[0] = '\0';
        t->cancel_req = false;
        t->pause_req = false;
        t->state = EC_TR_PENDING;
    }
    g_mutex_unlock(&t->lock);
    (void)q;
}

void ec_tq_pump(EcTransferQueue* q)
{
    g_mutex_lock(&q->lock);
    int running = 0;
    for (size_t i = 0; i < q->items.len; i++) {
        EcTransfer* t = q->items.items[i];
        if (t->state == EC_TR_RUNNING) running++;
    }
    for (size_t i = 0; i < q->items.len && running < q->max_concurrent; i++) {
        EcTransfer* t = q->items.items[i];
        if (t->state != EC_TR_PENDING) continue;
        t->started_ms = ec_now_ms();
        TQJob* job = malloc(sizeof(TQJob));
        if (!job) continue;
        job->t = t;
        job->q = q;
        GThread* th = g_thread_new("ec-transfer", tq_thread, job);
        if (th) {
            g_thread_unref(th);
            running++;
        } else {
            free(job);
            snprintf(t->error, sizeof t->error, "Cannot start transfer thread");
            t->state = EC_TR_ERROR;
        }
    }
    g_mutex_unlock(&q->lock);
}
