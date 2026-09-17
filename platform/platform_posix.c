/* Eclipse SSH - POSIX/Linux platform implementation. */

#include "eclipse/platform.h"

#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <libgen.h>
#include <limits.h>
#include <signal.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#ifndef PATH_MAX
#define PATH_MAX 4096
#endif

FILE* ec_fopen(const char* path, const char* mode) { return path ? fopen(path, mode) : NULL; }

bool ec_dir_mkdir(const char* path)
{
    if (!path) return false;
    if (mkdir(path, 0700) == 0) return true;
    return errno == EEXIST;
}

bool ec_remove(const char* path) { return path && remove(path) == 0; }

bool ec_rename(const char* from, const char* to)
{
    if (!from || !to) return false;
    return rename(from, to) == 0;
}

bool ec_stat_exists(const char* path)
{
    if (!path) return false;
    struct stat st;
    return stat(path, &st) == 0;
}

bool ec_stat_size(const char* path, uint64_t* out)
{
    if (!path || !out) return false;
    struct stat st;
    if (stat(path, &st) != 0) return false;
    if (st.st_size < 0) return false;
    *out = (uint64_t)st.st_size;
    return true;
}

bool ec_set_file_mode_600(const char* path)
{
    return path && chmod(path, 0600) == 0;
}

static char* home_dir(void)
{
    const char* h = getenv("HOME");
    return h && *h ? ec_strdup(h) : NULL;
}

bool ec_portable_mode(void)
{
    char* dir = ec_executable_dir();
    if (!dir) return false;
    char* flag = ec_path_join(dir, "portable.flag");
    bool ok = ec_file_exists(flag);
    free(flag);
    free(dir);
    return ok;
}

char* ec_executable_dir(void)
{
    char buf[PATH_MAX];
    ssize_t n = readlink("/proc/self/exe", buf, sizeof buf - 1);
    if (n <= 0) return ec_strdup(".");
    buf[n] = '\0';
    return ec_path_dirname(buf);
}

static char* xdg_dir(const char* envvar, const char* default_rel)
{
    const char* xdg = getenv(envvar);
    if (xdg && *xdg && xdg[0] == '/') return ec_strdup(xdg);
    char* home = home_dir();
    if (!home) return NULL;
    char* p = ec_path_join(home, default_rel);
    free(home);
    return p;
}

char* ec_app_data_dir(void)
{
    if (ec_portable_mode()) return ec_executable_dir();
    char* base = xdg_dir("XDG_DATA_HOME", ".local/share");
    if (!base) return NULL;
    char* dir = ec_path_join(base, "eclipse-ssh");
    free(base);
    return dir;
}

char* ec_app_config_dir(void)
{
    if (ec_portable_mode()) return ec_executable_dir();
    char* base = xdg_dir("XDG_CONFIG_HOME", ".config");
    if (!base) return NULL;
    char* dir = ec_path_join(base, "eclipse-ssh");
    free(base);
    return dir;
}

char* ec_temp_dir(void)
{
    const char* t = getenv("TMPDIR");
    return ec_strdup((t && *t) ? t : "/tmp");
}

int ec_getpid(void) { return (int)getpid(); }

char* ec_username(void)
{
    const char* u = getenv("USER");
    if (!u || !*u) u = getenv("LOGNAME");
    return (u && *u) ? ec_strdup(u) : NULL;
}

/* ---- directory iteration ---- */
struct EcDirIter {
    DIR* dir;
};

bool ec_dir_iter_open(EcDirIter** it_out, const char* dir)
{
    *it_out = NULL;
    if (!dir) return false;
    DIR* d = opendir(dir);
    if (!d) return false;
    EcDirIter* it = malloc(sizeof(EcDirIter));
    if (!it) { closedir(d); return false; }
    it->dir = d;
    *it_out = it;
    return true;
}

char* ec_dir_iter_next(EcDirIter* it)
{
    if (!it || !it->dir) return NULL;
    struct dirent* e;
    do {
        e = readdir(it->dir);
        if (!e) return NULL;
    } while (strcmp(e->d_name, ".") == 0 || strcmp(e->d_name, "..") == 0);
    return ec_strdup(e->d_name);
}

void ec_dir_iter_close(EcDirIter* it)
{
    if (!it) return;
    if (it->dir) closedir(it->dir);
    free(it);
}

/* ---- single instance via abstract unix socket ---- */
int64_t ec_single_instance_probe(const char* lock_name)
{
    int fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return 0;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    addr.sun_path[0] = '\0'; /* abstract socket */
    snprintf(addr.sun_path + 1, sizeof addr.sun_path - 1, "eclipse-ssh/%s", lock_name ? lock_name : "gui");
    socklen_t alen = (socklen_t)(sizeof(addr.sun_family) + 1 + strlen(addr.sun_path + 1));
    if (connect(fd, (struct sockaddr*)&addr, alen) != 0) {
        close(fd);
        return 0;
    }
    const char* ping = "ping\n";
    (void)!write(fd, ping, 5);
    char buf[64] = { 0 };
    ssize_t n = read(fd, buf, sizeof buf - 1);
    close(fd);
    if (n <= 0) return 0;
    return 1; /* somebody answered */
}

static int g_lock_fd = -1;
static int g_srv_fd = -1;
static void (*g_poke_cb)(void*) = NULL;
static void* g_poke_user = NULL;

static void* server_thread(void* arg)
{
    (void)arg;
    while (g_srv_fd >= 0) {
        int c = accept(g_srv_fd, NULL, NULL);
        if (c < 0) break;
        char buf[32];
        (void)!read(c, buf, sizeof buf);
        (void)!write(c, "pong\n", 5);
        close(c);
        if (g_poke_cb) g_poke_cb(g_poke_user);
    }
    return NULL;
}

/* pthread via glib is avoided here; plain pthreads (available on Linux always) */
#include <pthread.h>

void ec_single_instance_bind(const char* lock_name, void (*on_poke)(void* user), void* user)
{
    g_poke_cb = on_poke;
    g_poke_user = user;
    g_srv_fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (g_srv_fd < 0) return;
    struct sockaddr_un addr;
    memset(&addr, 0, sizeof addr);
    addr.sun_family = AF_UNIX;
    addr.sun_path[0] = '\0';
    snprintf(addr.sun_path + 1, sizeof addr.sun_path - 1, "eclipse-ssh/%s", lock_name ? lock_name : "gui");
    socklen_t alen = (socklen_t)(sizeof(addr.sun_family) + 1 + strlen(addr.sun_path + 1));
    if (bind(g_srv_fd, (struct sockaddr*)&addr, alen) != 0) {
        close(g_srv_fd);
        g_srv_fd = -1;
        return;
    }
    if (listen(g_srv_fd, 2) != 0) {
        close(g_srv_fd);
        g_srv_fd = -1;
        return;
    }
    pthread_t th;
    if (pthread_create(&th, NULL, server_thread, NULL) == 0)
        pthread_detach(th);
}

void ec_open_url(const char* url)
{
    if (!url || !*url) return;
    char* cmd = NULL;
    if (asprintf(&cmd, "xdg-open '%s' >/dev/null 2>&1 &", url) > 0) {
        int rc = system(cmd);
        (void)rc;
        free(cmd);
    }
}

void ec_show_in_folder(const char* path)
{
    if (!path || !*path) return;
    char* dir = ec_path_dirname(path);
    char* cmd = NULL;
    if (asprintf(&cmd, "xdg-open '%s' >/dev/null 2>&1 &", dir) > 0) {
        int rc = system(cmd);
        (void)rc;
        free(cmd);
    }
    free(dir);
}
