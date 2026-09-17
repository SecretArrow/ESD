/* Eclipse SSH - SFTP engine + transfer queue on libssh.
 * All operations are blocking; transfers run on worker threads (spec rule #9).
 */
#ifndef ECLIPSE_SFTP_H
#define ECLIPSE_SFTP_H

#include "eclipse/core.h"
#include "eclipse/ssh.h"
#include <glib.h>
#include <libssh/sftp.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef struct EcSftp EcSftp;

typedef struct {
    char* name;
    char* longname;
    uint64_t size;
    uint32_t permissions;
    uint32_t uid;
    uint32_t gid;
    int64_t mtime;
    bool is_dir;
    bool is_symlink;
} EcSftpEntry;

EcSftp* ec_sftp_open(EcSshSession* s, char** err);
void ec_sftp_free(EcSftp* sf);
char* ec_sftp_canonicalize(EcSftp* sf, const char* path);
void ec_sftp_entries_free(EcSftpEntry* entries, size_t n);
EcSftpEntry* ec_sftp_list(EcSftp* sf, const char* dir, size_t* count_out, char** err);
bool ec_sftp_mkdir(EcSftp* sf, const char* path);
bool ec_sftp_rmdir(EcSftp* sf, const char* path);
bool ec_sftp_unlink(EcSftp* sf, const char* path);
bool ec_sftp_rename(EcSftp* sf, const char* from, const char* to);
bool ec_sftp_exists(EcSftp* sf, const char* path);
bool ec_sftp_isdir(EcSftp* sf, const char* path, bool* out);
bool ec_sftp_stat(EcSftp* sf, const char* path, EcSftpEntry* out); /* name field = basename */
bool ec_sftp_chmod(EcSftp* sf, const char* path, uint32_t mode);
const char* ec_sftp_last_error(EcSftp* sf);

/* ----------------------------------------------------------- transfer queue */
typedef enum {
    EC_TR_PENDING = 0,
    EC_TR_RUNNING,
    EC_TR_PAUSED,
    EC_TR_DONE,
    EC_TR_ERROR,
    EC_TR_CANCELLED
} EcTransferState;

typedef struct EcTransfer EcTransfer;

/* invoked on transfer thread; must marshal to UI thread */
typedef void (*EcTransferProgressCb)(EcTransfer* t, void* user);

typedef enum { EC_TR_UPLOAD = 0, EC_TR_DOWNLOAD } EcTransferKind;

struct EcTransfer {
    EcTransferKind kind;
    EcTransferState state;
    char* local_path;
    char* remote_path;
    uint64_t total;       /* bytes */
    uint64_t done;
    int64_t started_ms;
    int64_t finished_ms;
    char error[256];
    /* control (guarded by mutex) */
    GMutex lock;
    bool pause_req;
    bool cancel_req;
    uint64_t speed_limit_bps; /* snapshot from queue at add time */
    EcSshSession* session; /* borrowed */
    EcSftp* sftp;          /* borrowed */
    EcTransferProgressCb progress_cb;
    void* user;
};

typedef struct {
    EcVec items;            /* EcTransfer* */
    GMutex lock;
    int max_concurrent;
    uint64_t speed_limit_bps; /* 0 = unlimited */
    void (*on_queue_changed)(void* user); /* marshal from threads */
    void* user;
} EcTransferQueue;

void ec_tq_init(EcTransferQueue* q);
void ec_tq_free(EcTransferQueue* q); /* frees finished + pending items */
void ec_tq_set_limits(EcTransferQueue* q, int max_concurrent, uint64_t speed_limit_bps);
EcTransfer* ec_tq_add(EcTransferQueue* q, EcTransferKind kind, EcSshSession* session,
                      EcSftp* sftp, const char* local_path, const char* remote_path,
                      EcTransferProgressCb cb, void* user);
void ec_tq_pause(EcTransferQueue* q, EcTransfer* t);
void ec_tq_resume(EcTransferQueue* q, EcTransfer* t);
void ec_tq_cancel(EcTransferQueue* q, EcTransfer* t);
void ec_tq_retry(EcTransferQueue* q, EcTransfer* t);
/* pump: starts queued items up to max_concurrent; call periodically from UI loop */
void ec_tq_pump(EcTransferQueue* q);

/* blocking single-file primitives used by the queue (also usable directly) */
bool ec_sftp_upload_file(EcTransfer* t);
bool ec_sftp_download_file(EcTransfer* t);

#ifdef __cplusplus
}
#endif
#endif /* ECLIPSE_SFTP_H */
