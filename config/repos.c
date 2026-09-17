/* Eclipse SSH - configuration repositories implementation. */
#include "eclipse/repos.h"
#include "eclipse/platform.h"

#include <unistd.h>

/* ------------------------------------------------------------- JSON helpers */
cJSON* ec_json_load(const char* path)
{
    if (!path) return NULL;
    char* text = NULL;
    if (!ec_file_read_all(path, &text, NULL)) return NULL;
    cJSON* root = cJSON_Parse(text);
    if (!root) {
        /* graceful recovery: keep the corrupt file aside for inspection */
        char* bad = malloc(strlen(path) + 5);
        if (bad) {
            snprintf(bad, strlen(path) + 5, "%s.bad", path);
            ec_rename(path, bad);
            free(bad);
        }
        EC_LOGW("config", "corrupt config %s moved aside", path);
    }
    free(text);
    return root;
}

bool ec_json_save(const char* path, cJSON* root)
{
    if (!path || !root) return false;
    char* text = cJSON_Print(root); /* pretty */
    if (!text) return false;
    bool ok = ec_file_write_atomic(path, text, strlen(text));
    free(text);
    return ok;
}

static void copy_json_str(char* dst, size_t cap, const cJSON* item)
{
    if (cJSON_IsString(item) && item->valuestring)
        snprintf(dst, cap, "%s", item->valuestring);
}

/* ------------------------------------------------------------- app settings */
void ec_settings_defaults(EcSettings* s)
{
    memset(s, 0, sizeof *s);
    snprintf(s->theme, sizeof s->theme, "system");
    s->density = 1;
    s->font_size = 12;
    snprintf(s->font_name, sizeof s->font_name, "monospace");
    s->confirm_paste = false;
    s->copy_on_select = false;
    s->scrollback_lines = 5000;
    s->keepalive_sec = 30;
    s->reconnect_auto = true;
    s->max_transfers = 3;
    s->speed_limit_bps = 0;
    s->bracketed_paste = true;
    s->mouse_reporting = true;
    s->check_updates = false;
}

bool ec_settings_load(EcSettings* s, const char* path)
{
    ec_settings_defaults(s);
    cJSON* root = ec_json_load(path);
    if (!root) return true; /* missing or corrupt: defaults already applied (graceful recovery) */
    copy_json_str(s->theme, sizeof s->theme, cJSON_GetObjectItem(root, "theme"));
    cJSON* j;
    if ((j = cJSON_GetObjectItem(root, "density")) && cJSON_IsNumber(j)) s->density = j->valueint;
    if ((j = cJSON_GetObjectItem(root, "font_size")) && cJSON_IsNumber(j)) s->font_size = j->valueint;
    copy_json_str(s->font_name, sizeof s->font_name, cJSON_GetObjectItem(root, "font_name"));
    if ((j = cJSON_GetObjectItem(root, "confirm_paste")) && cJSON_IsBool(j)) s->confirm_paste = cJSON_IsTrue(j);
    if ((j = cJSON_GetObjectItem(root, "copy_on_select")) && cJSON_IsBool(j)) s->copy_on_select = cJSON_IsTrue(j);
    if ((j = cJSON_GetObjectItem(root, "scrollback_lines")) && cJSON_IsNumber(j)) s->scrollback_lines = j->valueint;
    if ((j = cJSON_GetObjectItem(root, "keepalive_sec")) && cJSON_IsNumber(j)) s->keepalive_sec = j->valueint;
    if ((j = cJSON_GetObjectItem(root, "reconnect_auto")) && cJSON_IsBool(j)) s->reconnect_auto = cJSON_IsTrue(j);
    if ((j = cJSON_GetObjectItem(root, "max_transfers")) && cJSON_IsNumber(j)) s->max_transfers = j->valueint;
    if ((j = cJSON_GetObjectItem(root, "speed_limit_bps")) && cJSON_IsNumber(j) && j->valuedouble >= 0)
        s->speed_limit_bps = (uint64_t)j->valuedouble;
    if ((j = cJSON_GetObjectItem(root, "bracketed_paste")) && cJSON_IsBool(j)) s->bracketed_paste = cJSON_IsTrue(j);
    if ((j = cJSON_GetObjectItem(root, "mouse_reporting")) && cJSON_IsBool(j)) s->mouse_reporting = cJSON_IsTrue(j);
    if ((j = cJSON_GetObjectItem(root, "check_updates")) && cJSON_IsBool(j)) s->check_updates = cJSON_IsTrue(j);
    cJSON_Delete(root);
    /* validation */
    if (s->density < 0 || s->density > 2) s->density = 1;
    if (s->font_size < 6 || s->font_size > 72) s->font_size = 12;
    if (s->scrollback_lines < 100) s->scrollback_lines = 100;
    if (s->scrollback_lines > 500000) s->scrollback_lines = 500000;
    if (s->max_transfers < 1 || s->max_transfers > 16) s->max_transfers = 3;
    return true;
}

bool ec_settings_save(const EcSettings* s, const char* path)
{
    cJSON* root = cJSON_CreateObject();
    if (!root) return false;
    cJSON_AddStringToObject(root, "theme", s->theme);
    cJSON_AddNumberToObject(root, "density", s->density);
    cJSON_AddNumberToObject(root, "font_size", s->font_size);
    cJSON_AddStringToObject(root, "font_name", s->font_name);
    cJSON_AddBoolToObject(root, "confirm_paste", s->confirm_paste);
    cJSON_AddBoolToObject(root, "copy_on_select", s->copy_on_select);
    cJSON_AddNumberToObject(root, "scrollback_lines", s->scrollback_lines);
    cJSON_AddNumberToObject(root, "keepalive_sec", s->keepalive_sec);
    cJSON_AddBoolToObject(root, "reconnect_auto", s->reconnect_auto);
    cJSON_AddNumberToObject(root, "max_transfers", s->max_transfers);
    cJSON_AddNumberToObject(root, "speed_limit_bps", (double)s->speed_limit_bps);
    cJSON_AddBoolToObject(root, "bracketed_paste", s->bracketed_paste);
    cJSON_AddBoolToObject(root, "mouse_reporting", s->mouse_reporting);
    cJSON_AddBoolToObject(root, "check_updates", s->check_updates);
    bool ok = ec_json_save(path, root);
    cJSON_Delete(root);
    return ok;
}

/* ---------------------------------------------------------------- sessions */
static EcSession* session_from_json(const cJSON* obj)
{
    EcSession* sess = calloc(1, sizeof(EcSession));
    if (!sess) return NULL;
    copy_json_str(sess->id, sizeof sess->id, cJSON_GetObjectItem(obj, "id"));
    copy_json_str(sess->name, sizeof sess->name, cJSON_GetObjectItem(obj, "name"));
    copy_json_str(sess->group, sizeof sess->group, cJSON_GetObjectItem(obj, "group"));
    copy_json_str(sess->color, sizeof sess->color, cJSON_GetObjectItem(obj, "color"));
    copy_json_str(sess->host, sizeof sess->host, cJSON_GetObjectItem(obj, "host"));
    cJSON* j;
    if ((j = cJSON_GetObjectItem(obj, "port")) && cJSON_IsNumber(j)) sess->port = j->valueint;
    copy_json_str(sess->username, sizeof sess->username, cJSON_GetObjectItem(obj, "username"));
    if ((j = cJSON_GetObjectItem(obj, "auth_mode")) && cJSON_IsNumber(j)) sess->auth_mode = j->valueint;
    copy_json_str(sess->key_path, sizeof sess->key_path, cJSON_GetObjectItem(obj, "key_path"));
    if ((j = cJSON_GetObjectItem(obj, "use_agent_fallback")) && cJSON_IsBool(j)) sess->use_agent_fallback = cJSON_IsTrue(j);
    copy_json_str(sess->jump_id, sizeof sess->jump_id, cJSON_GetObjectItem(obj, "jump_id"));
    copy_json_str(sess->proxy_spec, sizeof sess->proxy_spec, cJSON_GetObjectItem(obj, "proxy_spec"));
    copy_json_str(sess->init_commands, sizeof sess->init_commands, cJSON_GetObjectItem(obj, "init_commands"));
    if ((j = cJSON_GetObjectItem(obj, "init_delay_ms")) && cJSON_IsNumber(j)) sess->init_delay_ms = j->valueint;
    if ((j = cJSON_GetObjectItem(obj, "keep_alive")) && cJSON_IsBool(j)) sess->keep_alive = cJSON_IsTrue(j);
    copy_json_str(sess->notes, sizeof sess->notes, cJSON_GetObjectItem(obj, "notes"));
    return sess;
}

static cJSON* session_to_json(const EcSession* sess, bool include_secrets, EcVault* vault)
{
    cJSON* obj = cJSON_CreateObject();
    if (!obj) return NULL;
    cJSON_AddStringToObject(obj, "id", sess->id);
    cJSON_AddStringToObject(obj, "name", sess->name);
    cJSON_AddStringToObject(obj, "group", sess->group);
    cJSON_AddStringToObject(obj, "color", sess->color);
    cJSON_AddStringToObject(obj, "host", sess->host);
    cJSON_AddNumberToObject(obj, "port", sess->port);
    cJSON_AddStringToObject(obj, "username", sess->username);
    cJSON_AddNumberToObject(obj, "auth_mode", sess->auth_mode);
    if (include_secrets && vault) {
        char *user = NULL, *pass = NULL;
        if (ec_vault_get(vault, sess->id, &user, &pass)) {
            cJSON_AddStringToObject(obj, "secret_user", user ? user : "");
            cJSON_AddStringToObject(obj, "secret_pass", pass ? pass : "");
            free(user);
            free(pass);
        }
    }
    cJSON_AddStringToObject(obj, "key_path", sess->key_path);
    cJSON_AddBoolToObject(obj, "use_agent_fallback", sess->use_agent_fallback);
    cJSON_AddStringToObject(obj, "jump_id", sess->jump_id);
    cJSON_AddStringToObject(obj, "proxy_spec", sess->proxy_spec);
    cJSON_AddStringToObject(obj, "init_commands", sess->init_commands);
    cJSON_AddNumberToObject(obj, "init_delay_ms", sess->init_delay_ms);
    cJSON_AddBoolToObject(obj, "keep_alive", sess->keep_alive);
    cJSON_AddStringToObject(obj, "notes", sess->notes);
    return obj;
}

void ec_sessions_init(EcSessionStore* st, const char* path)
{
    ec_vec_init(&st->sessions);
    snprintf(st->path, sizeof st->path, "%s", path ? path : "");
}

void ec_sessions_free(EcSessionStore* st)
{
    ec_vec_free_full(&st->sessions);
}

bool ec_sessions_load(EcSessionStore* st)
{
    cJSON* root = ec_json_load(st->path);
    if (!root) return false;
    cJSON* arr = cJSON_GetObjectItem(root, "sessions");
    if (cJSON_IsArray(arr)) {
        cJSON* it;
        cJSON_ArrayForEach(it, arr) {
            EcSession* s = session_from_json(it);
            if (s && !ec_vec_push(&st->sessions, s)) free(s);
        }
    }
    cJSON_Delete(root);
    return true;
}

bool ec_sessions_save(EcSessionStore* st)
{
    cJSON* root = cJSON_CreateObject();
    cJSON* arr = cJSON_AddArrayToObject(root, "sessions");
    if (!root || !arr) { cJSON_Delete(root); return false; }
    for (size_t i = 0; i < st->sessions.len; i++) {
        cJSON* obj = session_to_json(st->sessions.items[i], false, NULL);
        if (obj) cJSON_AddItemToArray(arr, obj);
    }
    bool ok = ec_json_save(st->path, root);
    cJSON_Delete(root);
    return ok;
}

static void gen_id(char* out, size_t cap)
{
    uint64_t h = ec_hash_str("") ^ (uint64_t)ec_now_ms() ^ ((uint64_t)getpid() << 32);
    snprintf(out, cap, "s-%08x-%04x", (unsigned)(h & 0xFFFFFFFF), (unsigned)((h >> 40) & 0xFFFF));
}

EcSession* ec_sessions_add(EcSessionStore* st, const char* name)
{
    EcSession* s = calloc(1, sizeof(EcSession));
    if (!s) return NULL;
    gen_id(s->id, sizeof s->id);
    snprintf(s->name, sizeof s->name, "%s", name ? name : "New session");
    s->port = 22;
    if (!ec_vec_push(&st->sessions, s)) { free(s); return NULL; }
    return s;
}

EcSession* ec_sessions_find(EcSessionStore* st, const char* id)
{
    for (size_t i = 0; i < st->sessions.len; i++) {
        EcSession* s = st->sessions.items[i];
        if (strcmp(s->id, id) == 0) return s;
    }
    return NULL;
}

EcSession* ec_sessions_find_name(EcSessionStore* st, const char* name)
{
    for (size_t i = 0; i < st->sessions.len; i++) {
        EcSession* s = st->sessions.items[i];
        if (strcmp(s->name, name) == 0) return s;
    }
    return NULL;
}

bool ec_sessions_remove(EcSessionStore* st, const char* id)
{
    for (size_t i = 0; i < st->sessions.len; i++) {
        EcSession* s = st->sessions.items[i];
        if (strcmp(s->id, id) == 0) {
            ec_vec_remove_at(&st->sessions, i);
            free(s);
            return true;
        }
    }
    return false;
}

bool ec_sessions_export(EcSessionStore* st, EcVault* vault, const char* path, bool include_secrets)
{
    cJSON* root = cJSON_CreateObject();
    cJSON* arr = cJSON_AddArrayToObject(root, "sessions");
    cJSON_AddStringToObject(root, "app", "eclipse-ssh");
    cJSON_AddNumberToObject(root, "format", 1);
    if (!root || !arr) { cJSON_Delete(root); return false; }
    for (size_t i = 0; i < st->sessions.len; i++) {
        cJSON* obj = session_to_json(st->sessions.items[i], include_secrets, vault);
        if (obj) cJSON_AddItemToArray(arr, obj);
    }
    bool ok = ec_json_save(path, root);
    cJSON_Delete(root);
    return ok;
}

bool ec_sessions_import(EcSessionStore* st, EcVault* vault, const char* path, bool has_secrets)
{
    cJSON* root = ec_json_load(path);
    if (!root) return false;
    cJSON* arr = cJSON_GetObjectItem(root, "sessions");
    bool any = false;
    if (cJSON_IsArray(arr)) {
        cJSON* it;
        cJSON_ArrayForEach(it, arr) {
            EcSession* s = session_from_json(it);
            if (!s) continue;
            if (!s->id[0]) gen_id(s->id, sizeof s->id);
            if (has_secrets && vault) {
                cJSON* su = cJSON_GetObjectItem(it, "secret_user");
                cJSON* sp = cJSON_GetObjectItem(it, "secret_pass");
                if (cJSON_IsString(su) || cJSON_IsString(sp))
                    ec_vault_set(vault, s->id,
                                 cJSON_IsString(su) ? su->valuestring : "",
                                 cJSON_IsString(sp) ? sp->valuestring : "");
            }
            /* de-duplicate by id: replace existing */
            ec_sessions_remove(st, s->id);
            if (ec_vec_push(&st->sessions, s)) any = true;
            else free(s);
        }
    }
    cJSON_Delete(root);
    return any;
}

/* ---------------------------------------------------------------- snippets */
void ec_snippets_init(EcSnippetStore* st, const char* path)
{
    ec_vec_init(&st->snippets);
    snprintf(st->path, sizeof st->path, "%s", path ? path : "");
}

void ec_snippets_free(EcSnippetStore* st) { ec_vec_free_full(&st->snippets); }

bool ec_snippets_load(EcSnippetStore* st)
{
    cJSON* root = ec_json_load(st->path);
    if (!root) return false;
    cJSON* arr = cJSON_GetObjectItem(root, "snippets");
    if (cJSON_IsArray(arr)) {
        cJSON* it;
        cJSON_ArrayForEach(it, arr) {
            EcSnippet* sn = calloc(1, sizeof(EcSnippet));
            if (!sn) continue;
            copy_json_str(sn->name, sizeof sn->name, cJSON_GetObjectItem(it, "name"));
            copy_json_str(sn->command, sizeof sn->command, cJSON_GetObjectItem(it, "command"));
            copy_json_str(sn->tags, sizeof sn->tags, cJSON_GetObjectItem(it, "tags"));
            if (!ec_vec_push(&st->snippets, sn)) free(sn);
        }
    }
    cJSON_Delete(root);
    return true;
}

bool ec_snippets_save(EcSnippetStore* st)
{
    cJSON* root = cJSON_CreateObject();
    cJSON* arr = cJSON_AddArrayToObject(root, "snippets");
    if (!root || !arr) { cJSON_Delete(root); return false; }
    for (size_t i = 0; i < st->snippets.len; i++) {
        EcSnippet* sn = st->snippets.items[i];
        cJSON* obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "name", sn->name);
        cJSON_AddStringToObject(obj, "command", sn->command);
        cJSON_AddStringToObject(obj, "tags", sn->tags);
        cJSON_AddItemToArray(arr, obj);
    }
    bool ok = ec_json_save(st->path, root);
    cJSON_Delete(root);
    return ok;
}

EcSnippet* ec_snippets_add(EcSnippetStore* st, const char* name, const char* command)
{
    EcSnippet* sn = calloc(1, sizeof(EcSnippet));
    if (!sn) return NULL;
    snprintf(sn->name, sizeof sn->name, "%s", name ? name : "snippet");
    snprintf(sn->command, sizeof sn->command, "%s", command ? command : "");
    if (!ec_vec_push(&st->snippets, sn)) { free(sn); return NULL; }
    return sn;
}

/* --------------------------------------------------------------- shortcuts */
void ec_shortcuts_init(EcShortcutStore* st, const char* path)
{
    ec_vec_init(&st->shortcuts);
    snprintf(st->path, sizeof st->path, "%s", path ? path : "");
}

void ec_shortcuts_free(EcShortcutStore* st) { ec_vec_free_full(&st->shortcuts); }

bool ec_shortcuts_load(EcShortcutStore* st)
{
    cJSON* root = ec_json_load(st->path);
    if (!root) return false;
    cJSON* arr = cJSON_GetObjectItem(root, "shortcuts");
    if (cJSON_IsArray(arr)) {
        cJSON* it;
        cJSON_ArrayForEach(it, arr) {
            EcShortcut* sc = calloc(1, sizeof(EcShortcut));
            if (!sc) continue;
            copy_json_str(sc->action, sizeof sc->action, cJSON_GetObjectItem(it, "action"));
            copy_json_str(sc->keys, sizeof sc->keys, cJSON_GetObjectItem(it, "keys"));
            if (!ec_vec_push(&st->shortcuts, sc)) free(sc);
        }
    }
    cJSON_Delete(root);
    return true;
}

bool ec_shortcuts_save(EcShortcutStore* st)
{
    cJSON* root = cJSON_CreateObject();
    cJSON* arr = cJSON_AddArrayToObject(root, "shortcuts");
    if (!root || !arr) { cJSON_Delete(root); return false; }
    for (size_t i = 0; i < st->shortcuts.len; i++) {
        EcShortcut* sc = st->shortcuts.items[i];
        cJSON* obj = cJSON_CreateObject();
        cJSON_AddStringToObject(obj, "action", sc->action);
        cJSON_AddStringToObject(obj, "keys", sc->keys);
        cJSON_AddItemToArray(arr, obj);
    }
    bool ok = ec_json_save(st->path, root);
    cJSON_Delete(root);
    return ok;
}

const char* ec_shortcuts_lookup(EcShortcutStore* st, const char* action, const char* fallback)
{
    for (size_t i = 0; i < st->shortcuts.len; i++) {
        EcShortcut* sc = st->shortcuts.items[i];
        if (strcmp(sc->action, action) == 0 && sc->keys[0]) return sc->keys;
    }
    return fallback;
}
