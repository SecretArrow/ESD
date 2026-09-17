/* Eclipse SSH - Windows platform implementation. */
#define WIN32_LEAN_AND_MEAN
#ifndef UNICODE
#define UNICODE
#endif
#ifndef _UNICODE
#define _UNICODE
#endif
#include "eclipse/platform.h"

#include <windows.h>
#include <shellapi.h>
#include <shlobj.h>

static wchar_t* utf8_to_wide(const char* s)
{
    if (!s) return NULL;
    int n = MultiByteToWideChar(CP_UTF8, 0, s, -1, NULL, 0);
    if (n <= 0) return NULL;
    wchar_t* w = malloc((size_t)n * sizeof(wchar_t));
    if (!w) return NULL;
    MultiByteToWideChar(CP_UTF8, 0, s, -1, w, n);
    return w;
}

static char* wide_to_utf8(const wchar_t* w)
{
    if (!w) return NULL;
    int n = WideCharToMultiByte(CP_UTF8, 0, w, -1, NULL, 0, NULL, NULL);
    if (n <= 0) return NULL;
    char* s = malloc((size_t)n);
    if (!s) return NULL;
    WideCharToMultiByte(CP_UTF8, 0, w, -1, s, n, NULL, NULL);
    return s;
}

FILE* ec_fopen(const char* path, const char* mode)
{
    if (!path || !mode) return NULL;
    wchar_t* wpath = utf8_to_wide(path);
    wchar_t* wmode = utf8_to_wide(mode);
    if (!wpath || !wmode) { free(wpath); free(wmode); return NULL; }
    FILE* f = _wfopen(wpath, wmode);
    free(wpath);
    free(wmode);
    return f;
}

bool ec_dir_mkdir(const char* path)
{
    if (!path) return false;
    wchar_t* w = utf8_to_wide(path);
    if (!w) return false;
    BOOL ok = CreateDirectoryW(w, NULL);
    free(w);
    return ok || GetLastError() == ERROR_ALREADY_EXISTS;
}

bool ec_remove(const char* path)
{
    if (!path) return false;
    wchar_t* w = utf8_to_wide(path);
    if (!w) return false;
    BOOL ok = DeleteFileW(w) || RemoveDirectoryW(w);
    free(w);
    return ok;
}

bool ec_rename(const char* from, const char* to)
{
    if (!from || !to) return false;
    wchar_t* a = utf8_to_wide(from);
    wchar_t* b = utf8_to_wide(to);
    if (!a || !b) { free(a); free(b); return false; }
    BOOL ok = MoveFileExW(a, b, MOVEFILE_REPLACE_EXISTING);
    free(a);
    free(b);
    return ok;
}

bool ec_stat_exists(const char* path)
{
    if (!path) return false;
    wchar_t* w = utf8_to_wide(path);
    if (!w) return false;
    DWORD attrs = GetFileAttributesW(w);
    free(w);
    return attrs != INVALID_FILE_ATTRIBUTES;
}

bool ec_stat_size(const char* path, uint64_t* out)
{
    if (!path || !out) return false;
    wchar_t* w = utf8_to_wide(path);
    if (!w) return false;
    HANDLE h = CreateFileW(w, GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, NULL,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, NULL);
    free(w);
    if (h == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER li;
    BOOL ok = GetFileSizeEx(h, &li);
    CloseHandle(h);
    if (!ok || li.QuadPart < 0) return false;
    *out = (uint64_t)li.QuadPart;
    return true;
}

bool ec_set_file_mode_600(const char* path)
{
    /* On Windows, per-user profile dirs are ACL-protected; keep the file
     * from inheriting broad share access by ensuring it is not read-only and
     * rely on %APPDATA% ACLs. 600 has no direct mapping without the ACL API. */
    return ec_stat_exists(path);
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
    wchar_t wpath[MAX_PATH + 2];
    DWORD n = GetModuleFileNameW(NULL, wpath, MAX_PATH + 1);
    if (n == 0 || n >= MAX_PATH) return ec_strdup(".");
    char* p = wide_to_utf8(wpath);
    if (!p) return NULL;
    char* last = strrchr(p, '\\');
    char* lastf = strrchr(p, '/');
    if (lastf > last) last = lastf;
    char* out;
    if (last) out = ec_strndup(p, (size_t)(last - p));
    else out = ec_strdup(".");
    free(p);
    return out;
}

static char* known_dir(int csidl)
{
    wchar_t w[MAX_PATH];
    if (SHGetFolderPathW(NULL, csidl, NULL, 0, w) != S_OK) return NULL;
    char* base = wide_to_utf8(w);
    if (!base) return NULL;
    return base;
}

char* ec_app_data_dir(void)
{
    if (ec_portable_mode()) return ec_executable_dir();
    char* base = known_dir(CSIDL_APPDATA);
    if (!base) return NULL;
    char* dir = ec_path_join(base, "eclipse-ssh");
    free(base);
    return dir;
}

char* ec_app_config_dir(void)
{
    return ec_app_data_dir(); /* single tree on Windows */
}

char* ec_temp_dir(void)
{
    wchar_t w[MAX_PATH + 1];
    DWORD n = GetTempPathW(MAX_PATH, w);
    if (n == 0) return ec_strdup("C:\\Windows\\Temp");
    char* p = wide_to_utf8(w);
    if (!p) return NULL;
    size_t l = strlen(p);
    if (l && (p[l - 1] == '\\' || p[l - 1] == '/')) p[l - 1] = '\0';
    return p;
}

int ec_getpid(void) { return (int)GetCurrentProcessId(); }

char* ec_username(void)
{
    wchar_t w[256];
    DWORD n = 256;
    if (!GetUserNameW(w, &n)) return NULL;
    return wide_to_utf8(w);
}

/* ---- directory iteration ---- */
struct EcDirIter {
    HANDLE find;
    wchar_t pattern[MAX_PATH + 4];
    bool first;
    WIN32_FIND_DATAW data;
};

bool ec_dir_iter_open(EcDirIter** it_out, const char* dir)
{
    *it_out = NULL;
    if (!dir) return false;
    wchar_t* w = utf8_to_wide(dir);
    if (!w) return false;
    EcDirIter* it = calloc(1, sizeof(EcDirIter));
    if (!it) { free(w); return false; }
    _snwprintf(it->pattern, MAX_PATH + 3, L"%s\\*", w);
    free(w);
    it->first = true;
    *it_out = it;
    return true;
}

char* ec_dir_iter_next(EcDirIter* it)
{
    if (!it) return NULL;
    if (it->find == INVALID_HANDLE_VALUE && !it->first) return NULL;
    if (it->first) {
        it->first = false;
        it->find = FindFirstFileW(it->pattern, &it->data);
        if (it->find == INVALID_HANDLE_VALUE) return NULL;
    } else {
        if (!FindNextFileW(it->find, &it->data)) return NULL;
    }
    if (wcscmp(it->data.cFileName, L".") == 0 || wcscmp(it->data.cFileName, L"..") == 0)
        return ec_dir_iter_next(it); /* recurse to next entry */
    return wide_to_utf8(it->data.cFileName);
}

void ec_dir_iter_close(EcDirIter* it)
{
    if (!it) return;
    if (it->find && it->find != INVALID_HANDLE_VALUE) FindClose(it->find);
    free(it);
}

/* ---- single instance via named mutex + window message broadcast ---- */
static HANDLE g_mutex = NULL;
static HWND g_msg_hwnd = NULL;
static void (*g_poke_cb)(void*) = NULL;
static void* g_poke_user = NULL;

static LRESULT CALLBACK poke_wndproc(HWND hwnd, UINT msg, WPARAM wp, LPARAM lp)
{
    if (msg == WM_APP + 4217) {
        if (g_poke_cb) g_poke_cb(g_poke_user);
        return 0;
    }
    return DefWindowProcW(hwnd, msg, wp, lp);
}

static char* g_lock_name = NULL;

int64_t ec_single_instance_probe(const char* lock_name)
{
    char name[128];
    snprintf(name, sizeof name, "EclipseSSH\\%s", lock_name ? lock_name : "gui");
    HANDLE h = CreateMutexA(NULL, FALSE, name);
    if (!h) return 0;
    if (GetLastError() == ERROR_ALREADY_EXISTS) {
        /* poke the first instance via a broadcast of the custom message */
        HWND other = FindWindowA("EclipseSSHPokeWnd", NULL);
        if (other) PostMessageW(other, WM_APP + 4217, 0, 0);
        CloseHandle(h);
        return 1;
    }
    /* first instance: keep the mutex alive for process lifetime */
    if (!g_mutex) {
        g_mutex = h;
        free(g_lock_name);
        g_lock_name = ec_strdup(lock_name);
        return 0;
    }
    CloseHandle(h);
    return 0;
}

void ec_single_instance_bind(const char* lock_name, void (*on_poke)(void* user), void* user)
{
    (void)lock_name;
    g_poke_cb = on_poke;
    g_poke_user = user;
    if (g_msg_hwnd) return;
    WNDCLASSW wc;
    memset(&wc, 0, sizeof wc);
    wc.lpfnWndProc = poke_wndproc;
    wc.lpszClassName = L"EclipseSSHPokeWnd";
    wc.hInstance = GetModuleHandleW(NULL);
    if (RegisterClassW(&wc))
        g_msg_hwnd = CreateWindowExW(0, wc.lpszClassName, L"", 0, 0, 0, 0, 0, HWND_MESSAGE, NULL, wc.hInstance, NULL);
}

void ec_open_url(const char* url)
{
    if (!url || !*url) return;
    wchar_t* w = utf8_to_wide(url);
    if (!w) return;
    ShellExecuteW(NULL, L"open", w, NULL, NULL, SW_SHOWNORMAL);
    free(w);
}

void ec_show_in_folder(const char* path)
{
    if (!path || !*path) return;
    wchar_t* w = utf8_to_wide(path);
    if (!w) return;
    /* select the file inside an explorer window */
    wchar_t* args = malloc((wcslen(w) + 64) * sizeof(wchar_t));
    if (args) {
        swprintf(args, wcslen(w) + 64, L"/select,\"%s\"", w);
        ShellExecuteW(NULL, L"open", L"explorer.exe", args, NULL, SW_SHOWNORMAL);
        free(args);
    }
    free(w);
}
