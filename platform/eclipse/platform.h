/* Eclipse SSH - platform abstraction: paths, files, secure storage hooks,
 * single-instance handshake, OS integration. Implementations per OS.
 */
#ifndef ECLIPSE_PLATFORM_H
#define ECLIPSE_PLATFORM_H

#include "eclipse/core.h"
#include <stdio.h>

#ifdef __cplusplus
extern "C" {
#endif

/* ---- filesystem shims (locale-safe on Win32 via _wfopen paths) ---- */
FILE* ec_fopen(const char* path, const char* mode);
bool ec_dir_mkdir(const char* path);          /* single level; false if exists/error */
bool ec_remove(const char* path);
bool ec_rename(const char* from, const char* to);
bool ec_stat_exists(const char* path);
bool ec_stat_size(const char* path, uint64_t* out);
bool ec_set_file_mode_600(const char* path);  /* restrict permissions where applicable */

/* ---- directories (UTF-8, no trailing slash) ---- */
/* Root for all app data. Honours portable mode: a file named "portable.flag"
 * next to the executable pins all data next to the binary (spec #54). */
char* ec_app_data_dir(void);        /* e.g. ~/.local/share/eclipse-ssh | %APPDATA%\eclipse-ssh | <exe dir> in portable mode */
char* ec_app_config_dir(void);      /* e.g. ~/.config/eclipse-ssh   | same as data dir in portable mode */
char* ec_executable_dir(void);      /* dir containing the running binary */
char* ec_temp_dir(void);
bool ec_portable_mode(void);

/* ---- process ---- */
/* Returns a handle id (>0) if another instance answered the ping, 0 otherwise.
 * Used by the GUI single-instance handshake (spec: 2nd instance exits 0 and pokes 1st). */
int64_t ec_single_instance_probe(const char* lock_name);
void ec_single_instance_bind(const char* lock_name, void (*on_poke)(void* user), void* user);
void ec_open_url(const char* url);  /* browser/mailto */
void ec_show_in_folder(const char* path);

/* ---- misc ---- */
int ec_getpid(void);
char* ec_username(void);            /* malloc'd current OS username, NULL on failure */

/* ---- directory iteration (names only, no ordering guarantee) ---- */
typedef struct EcDirIter EcDirIter;
bool ec_dir_iter_open(EcDirIter** it_out, const char* dir);
char* ec_dir_iter_next(EcDirIter* it);      /* malloc'd entry name; NULL at end */
void ec_dir_iter_close(EcDirIter* it);

#ifdef __cplusplus
}
#endif
#endif /* ECLIPSE_PLATFORM_H */
