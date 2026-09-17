/* Eclipse SSH - configuration repositories: settings, sessions, snippets,
 * shortcuts, themes. JSON on disk (cJSON), atomic writes, graceful recovery
 * from corrupt files (spec #71, #72 - structured files, no heavy DB).
 */
#ifndef ECLIPSE_REPOS_H
#define ECLIPSE_REPOS_H

#include "eclipse/core.h"
#include "eclipse/security.h"
#include "cJSON.h"

#ifdef __cplusplus
extern "C" {
#endif

/* load JSON from path; returns NULL if missing/corrupt (corrupt file is renamed *.bad) */
cJSON* ec_json_load(const char* path);
bool ec_json_save(const char* path, cJSON* root); /* pretty-printed, atomic */

/* ------------------------------------------------------------- app settings */
typedef struct {
    /* appearance */
    char theme[128];            /* "light" | "dark" | "system" | custom name */
    int density;                /* 0=compact 1=comfortable 2=large */
    int font_size;              /* terminal font pt */
    char font_name[128];
    /* behavior */
    bool confirm_paste;
    bool copy_on_select;
    int scrollback_lines;
    int keepalive_sec;          /* 0 = off */
    bool reconnect_auto;
    int max_transfers;
    uint64_t speed_limit_bps;   /* 0 = unlimited */
    /* shell */
    bool bracketed_paste;       /* default on */
    bool mouse_reporting;
    /* updates */
    bool check_updates;         /* default false (privacy) */
} EcSettings;

void ec_settings_defaults(EcSettings* s);
bool ec_settings_load(EcSettings* s, const char* path);
bool ec_settings_save(const EcSettings* s, const char* path);

/* ---------------------------------------------------------------- sessions */
typedef struct {
    char id[64];
    char name[128];
    char group[128];
    char color[16];             /* hex color tag (spec #59) */
    char host[256];
    int port;
    char username[128];
    /* auth: vault stores secrets under id; auth_mode selects flow */
    enum { EC_AUTH_PASSWORD = 0, EC_AUTH_KEY, EC_AUTH_AGENT, EC_AUTH_KEY_AGENT } auth_mode;
    char key_path[512];
    bool use_agent_fallback;
    /* network */
    char jump_id[64];           /* chain via another profile (spec #21) */
    char proxy_spec[256];       /* socks5://user:pass@h:p / http://... (spec #22) */
    char init_commands[2048];   /* automation sequence, \n separated (spec #23) */
    int init_delay_ms;
    bool keep_alive;
    /* notes */
    char notes[512];
} EcSession;

typedef struct {
    EcVec sessions;             /* EcSession* */
    char path[512];
} EcSessionStore;

void ec_sessions_init(EcSessionStore* st, const char* path);
void ec_sessions_free(EcSessionStore* st);
bool ec_sessions_load(EcSessionStore* st);
bool ec_sessions_save(EcSessionStore* st);
EcSession* ec_sessions_add(EcSessionStore* st, const char* name);
EcSession* ec_sessions_find(EcSessionStore* st, const char* id);
EcSession* ec_sessions_find_name(EcSessionStore* st, const char* name);
bool ec_sessions_remove(EcSessionStore* st, const char* id);
/* export: credentials excluded by default (spec #33); include_secrets uses vault */
bool ec_sessions_export(EcSessionStore* st, EcVault* vault, const char* path, bool include_secrets);
bool ec_sessions_import(EcSessionStore* st, EcVault* vault, const char* path, bool has_secrets);

/* ---------------------------------------------------------------- snippets */
typedef struct {
    char name[128];
    char command[1024];
    char tags[128];
} EcSnippet;

typedef struct {
    EcVec snippets;             /* EcSnippet* */
    char path[512];
} EcSnippetStore;

void ec_snippets_init(EcSnippetStore* st, const char* path);
void ec_snippets_free(EcSnippetStore* st);
bool ec_snippets_load(EcSnippetStore* st);
bool ec_snippets_save(EcSnippetStore* st);
EcSnippet* ec_snippets_add(EcSnippetStore* st, const char* name, const char* command);

/* --------------------------------------------------------------- shortcuts */
typedef struct {
    char action[64];
    char keys[64];
} EcShortcut;

typedef struct {
    EcVec shortcuts;            /* EcShortcut* */
    char path[512];
} EcShortcutStore;

void ec_shortcuts_init(EcShortcutStore* st, const char* path);
void ec_shortcuts_free(EcShortcutStore* st);
bool ec_shortcuts_load(EcShortcutStore* st);
bool ec_shortcuts_save(EcShortcutStore* st);
const char* ec_shortcuts_lookup(EcShortcutStore* st, const char* action, const char* fallback);

#ifdef __cplusplus
}
#endif
#endif /* ECLIPSE_REPOS_H */
