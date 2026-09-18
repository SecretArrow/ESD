/* Eclipse SSH - theme engine implementation. */
#include "eclipse/theme.h"
#include "eclipse/platform.h"
#include "eclipse/repos.h"

#include "cJSON.h"

bool ec_theme_parse_hex(const char* hex, uint8_t out[3])
{
    if (!hex || hex[0] != '#' || strlen(hex) != 7) return false;
    for (int i = 1; i < 7; i++) {
        char ch = hex[i];
        bool hv = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
        if (!hv) return false;
    }
    unsigned v = 0;
    if (sscanf(hex + 1, "%06x", &v) != 1) return false;
    out[0] = (uint8_t)((v >> 16) & 0xff);
    out[1] = (uint8_t)((v >> 8) & 0xff);
    out[2] = (uint8_t)(v & 0xff);
    return true;
}

static bool valid_color(const char* c)
{
    uint8_t tmp[3];
    return ec_theme_parse_hex(c, tmp);
}

/* ------------------------------------------------------------ builtins */
typedef struct {
    const char* key;
    const char* label;
    const char* appearance;
    const char* col[16];    /* role colors, order matches load/save below */
    const char* ansi[EC_THEME_ANSI_N];
} EcBuiltin;

/* role order: background, foreground, primary, secondary, accent, border,
 *             panel, sidebar, terminal_bg, terminal_fg, terminal_cursor,
 *             selection, warning, error, success, info */
static const EcBuiltin g_builtins[] = {
    { "light", "Light", "light",
      { "#f7f8fa", "#1c2127", "#2563eb", "#5a6572", "#2563eb", "#dde1e6", "#ffffff",
        "#eef0f3", "#ffffff", "#1c2127", "#2563eb", "#cfe0f8", "#9a6700", "#c62f33",
        "#1a7f37", "#0969da" },
      { "#000000", "#cd3131", "#00bc00", "#949800", "#0451a5", "#bc05bc", "#0598bc",
        "#555555", "#666666", "#cd3131", "#14ce14", "#b5ba00", "#0451a5", "#bc05bc",
        "#0598bc", "#a5a5a5" } },
    { "dark", "Dark", "dark",
      { "#1b1e23", "#e6e8eb", "#4f8cff", "#9aa4b2", "#4f8cff", "#2c313a", "#22262d",
        "#191c21", "#14171c", "#e6e8eb", "#4f8cff", "#31465f", "#e5b567", "#e5484d",
        "#46a758", "#00b0d8" },
      { "#000000", "#cd3131", "#0dbc61", "#e5e512", "#2472c8", "#bc3fbc", "#11a8cd",
        "#e5e5e5", "#666666", "#f14c4c", "#23d18b", "#f5f543", "#3b8eea", "#d670d6",
        "#29b8db", "#ffffff" } },
    { "nord", "Nord", "dark",
      { "#2e3440", "#eceff4", "#88c0d0", "#a9b3c0", "#88c0d0", "#3b4252", "#3b4252",
        "#272c36", "#2e3440", "#d8dee9", "#88c0d0", "#434c5e", "#ebcb8b", "#bf616a",
        "#a3be8c", "#81a1c1" },
      { "#3b4252", "#bf616a", "#a3be8c", "#ebcb8b", "#81a1c1", "#b48ead", "#88c0d0",
        "#e5e9f0", "#4c566a", "#bf616a", "#a3be8c", "#ebcb8b", "#81a1c1", "#b48ead",
        "#8fbcbb", "#eceff4" } },
    { "dracula", "Dracula", "dark",
      { "#282a36", "#f8f8f2", "#bd93f9", "#9aa4b2", "#ff79c6", "#44475a", "#343746",
        "#21222c", "#282a36", "#f8f8f2", "#f8f8f0", "#44475a", "#f1fa8c", "#ff5555",
        "#50fa7b", "#8be9fd" },
      { "#21222c", "#ff5555", "#50fa7b", "#f1fa8c", "#bd93f9", "#ff79c6", "#8be9fd",
        "#f8f8f2", "#6272a4", "#ff6e6e", "#69ff94", "#ffffaa", "#d6acff", "#ff92df",
        "#a4ffff", "#ffffff" } },
    { "solarized-dark", "Solarized Dark", "dark",
      { "#002b36", "#eee8d5", "#268bd2", "#93a1a1", "#b58900", "#073642", "#073642",
        "#002028", "#002b36", "#eee8d5", "#93a1a1", "#0d4552", "#b58900", "#dc322f",
        "#859900", "#2aa198" },
      { "#002b36", "#dc322f", "#859900", "#b58900", "#268bd2", "#d33682", "#2aa198",
        "#eee8d5", "#073642", "#cb4b16", "#586e75", "#657b83", "#839496", "#6c71c4",
        "#93a1a1", "#fdf6e3" } },
    { "monokai", "Monokai", "dark",
      { "#272822", "#f8f8f2", "#a6e22e", "#a59f85", "#f92672", "#3e3d32", "#32322d",
        "#1f201b", "#272822", "#f8f8f2", "#f8f8f0", "#49483e", "#f4bf75", "#f92672",
        "#a6e22e", "#66d9ef" },
      { "#272822", "#f92672", "#a6e22e", "#f4bf75", "#66d9ef", "#ae81ff", "#a1efe4",
        "#f8f8f2", "#75715e", "#f92672", "#a6e22e", "#f4bf75", "#66d9ef", "#ae81ff",
        "#a1efe4", "#f8f8f2" } },
};

static const size_t g_builtin_n = sizeof g_builtins / sizeof g_builtins[0];

/* Settings-dropdown registry. Kept separate from EcBuiltin (whose first two
 * members do NOT alias EcThemeDef - EcBuiltin carries appearance between
 * label and the color table); tests cross-check the two lists. */
static const EcThemeDef g_deflist[] = {
    { "light", "Light" },
    { "dark", "Dark" },
    { "nord", "Nord" },
    { "dracula", "Dracula" },
    { "solarized-dark", "Solarized Dark" },
    { "monokai", "Monokai" },
};

const EcThemeDef* ec_theme_builtins(size_t* count_out)
{
    *count_out = sizeof g_deflist / sizeof g_deflist[0];
    return g_deflist;
}

const char* ec_theme_key_to_label(const char* key)
{
    if (!key) return "Dark";
    for (size_t i = 0; i < g_builtin_n; i++)
        if (strcmp(g_builtins[i].key, key) == 0) return g_builtins[i].label;
    return key;
}

const char* ec_theme_label_to_key(const char* label)
{
    if (!label) return "dark";
    for (size_t i = 0; i < g_builtin_n; i++)
        if (strcmp(g_builtins[i].label, label) == 0) return g_builtins[i].key;
    return label;
}

static void theme_fill(const EcBuiltin* b, EcTheme* t)
{
    snprintf(t->name, sizeof t->name, "%s", b->key);
    snprintf(t->appearance, sizeof t->appearance, "%s", b->appearance);
    EcThemeColors* c = &t->c;
    /* the 16 role fields are consecutive char[16] blocks at the head of
     * EcThemeColors (background..info); block i lives at offset i*16 */
    char* roles = (char*)c;
    for (size_t i = 0; i < 16; i++)
        snprintf(roles + i * 16, 16, "%s", b->col[i]);
    for (int i = 0; i < EC_THEME_ANSI_N; i++)
        snprintf(c->ansi[i], 16, "%s", b->ansi[i]);
    snprintf(c->radius, sizeof c->radius, "8px");
}

/* Appearance-based defaults (legacy/custom JSON): start from the matching
 * builtin's full color table, keep the caller's name/appearance fields. */
static void theme_defaults_for_appearance(EcTheme* t)
{
    EcTheme base;
    ec_theme_builtin_by_name(strcmp(t->appearance, "light") == 0 ? "light" : "dark", &base);
    t->c = base.c;
}

void ec_theme_builtin_light(EcTheme* t)
{
    for (size_t i = 0; i < g_builtin_n; i++)
        if (strcmp(g_builtins[i].key, "light") == 0) { theme_fill(&g_builtins[i], t); return; }
}

void ec_theme_builtin_dark(EcTheme* t)
{
    for (size_t i = 0; i < g_builtin_n; i++)
        if (strcmp(g_builtins[i].key, "dark") == 0) { theme_fill(&g_builtins[i], t); return; }
}

bool ec_theme_builtin_by_name(const char* key, EcTheme* out)
{
    if (!out) return false;
    if (!key || !*key) { ec_theme_builtin_dark(out); return false; }
    for (size_t i = 0; i < g_builtin_n; i++) {
        if (strcmp(g_builtins[i].key, key) == 0) {
            theme_fill(&g_builtins[i], out);
            return true;
        }
    }
    ec_theme_builtin_dark(out);
    return false;
}

bool ec_theme_resolve(const char* key, const char* themes_dir, EcTheme* out)
{
    if (!out) return false;
    ec_theme_builtin_by_name(key, out);
    if (!key || !*key || !themes_dir || !*themes_dir) return false;
    size_t n = 0;
    char** files = ec_theme_list(themes_dir, &n);
    bool custom = false;
    for (size_t i = 0; i < n; i++) {
        EcTheme t;
        if (ec_theme_load_file(files[i], &t) && strcmp(t.name, key) == 0) {
            *out = t;
            custom = true;
        }
        free(files[i]);
    }
    free(files);
    return custom;
}

/* ------------------------------------------------------------ JSON load */
static void load_color(cJSON* obj, const char* key, char* dst, size_t cap)
{
    cJSON* j = cJSON_GetObjectItem(obj, key);
    if (cJSON_IsString(j) && valid_color(j->valuestring))
        snprintf(dst, cap, "%s", j->valuestring);
}

bool ec_theme_load_json(const char* json, EcTheme* out)
{
    if (!json || !out) return false;
    cJSON* root = cJSON_Parse(json);
    if (!root) return false;
    cJSON* name_j = cJSON_GetObjectItem(root, "name");
    cJSON* app_j = cJSON_GetObjectItem(root, "appearance");
    cJSON* colors = cJSON_GetObjectItem(root, "colors");
    snprintf(out->name, sizeof out->name, "%s",
             (cJSON_IsString(name_j) && name_j->valuestring) ? name_j->valuestring : "Custom");
    snprintf(out->appearance, sizeof out->appearance, "%s",
             (cJSON_IsString(app_j) && app_j->valuestring) ? app_j->valuestring : "dark");
    if (strcmp(out->appearance, "light") != 0 && strcmp(out->appearance, "dark") != 0)
        snprintf(out->appearance, sizeof out->appearance, "dark");
    /* defaults from appearance keep legacy single-color themes working */
    {
        char keep_name[128];
        snprintf(keep_name, sizeof keep_name, "%s", out->name);
        theme_defaults_for_appearance(out);
        snprintf(out->name, sizeof out->name, "%s", keep_name);
    }
    if (cJSON_IsObject(colors)) {
        EcThemeColors* c = &out->c;
        load_color(colors, "background", c->background, sizeof c->background);
        load_color(colors, "foreground", c->foreground, sizeof c->foreground);
        load_color(colors, "primary", c->primary, sizeof c->primary);
        load_color(colors, "secondary", c->secondary, sizeof c->secondary);
        load_color(colors, "accent", c->accent, sizeof c->accent);
        load_color(colors, "border", c->border, sizeof c->border);
        load_color(colors, "panel", c->panel, sizeof c->panel);
        load_color(colors, "sidebar", c->sidebar, sizeof c->sidebar);
        load_color(colors, "terminal_bg", c->terminal_bg, sizeof c->terminal_bg);
        load_color(colors, "terminal_fg", c->terminal_fg, sizeof c->terminal_fg);
        load_color(colors, "terminal_cursor", c->terminal_cursor, sizeof c->terminal_cursor);
        load_color(colors, "selection", c->selection, sizeof c->selection);
        load_color(colors, "warning", c->warning, sizeof c->warning);
        load_color(colors, "error", c->error, sizeof c->error);
        load_color(colors, "success", c->success, sizeof c->success);
        load_color(colors, "info", c->info, sizeof c->info);
        cJSON* ansi = cJSON_GetObjectItem(colors, "ansi");
        if (cJSON_IsArray(ansi)) {
            int i = 0;
            cJSON* it = NULL;
            cJSON_ArrayForEach(it, ansi) {
                if (i >= EC_THEME_ANSI_N) break;
                if (cJSON_IsString(it) && valid_color(it->valuestring))
                    snprintf(c->ansi[i], 16, "%s", it->valuestring);
                i++;
            }
        }
        cJSON* r = cJSON_GetObjectItem(colors, "radius");
        if (cJSON_IsString(r) && r->valuestring && strlen(r->valuestring) < sizeof c->radius - 2)
            snprintf(c->radius, sizeof c->radius, "%s", r->valuestring);
    }
    cJSON_Delete(root);
    return true;
}

bool ec_theme_load_file(const char* path, EcTheme* out)
{
    char* json = NULL;
    if (!ec_file_read_all(path, &json, NULL)) return false;
    bool ok = ec_theme_load_json(json, out);
    free(json);
    return ok;
}

bool ec_theme_save(const EcTheme* t, const char* path)
{
    cJSON* root = cJSON_CreateObject();
    if (!root) return false;
    cJSON_AddStringToObject(root, "name", t->name);
    cJSON_AddStringToObject(root, "appearance", t->appearance);
    cJSON* colors = cJSON_AddObjectToObject(root, "colors");
    const EcThemeColors* c = &t->c;
    static const char* keys[16] = {
        "background", "foreground", "primary", "secondary", "accent", "border",
        "panel", "sidebar", "terminal_bg", "terminal_fg", "terminal_cursor",
        "selection", "warning", "error", "success", "info"
    };
    for (int i = 0; i < 16; i++)
        cJSON_AddStringToObject(colors, keys[i], ((const char*)c) + i * 16);
    cJSON* ansi = cJSON_AddArrayToObject(colors, "ansi");
    for (int i = 0; i < EC_THEME_ANSI_N; i++)
        cJSON_AddItemToArray(ansi, cJSON_CreateString(c->ansi[i]));
    cJSON_AddStringToObject(colors, "radius", c->radius);
    bool ok = ec_json_save(path, root);
    cJSON_Delete(root);
    return ok;
}

char* ec_theme_css(const EcTheme* t)
{
    const EcThemeColors* c = &t->c;
    EcStr b;
    ec_str_init(&b);
    ec_str_printf(&b,
        "/* generated from theme '%s' */\n"
        "@define-color accent_color %s;\n"
        "@define-color accent_bg_color %s;\n"
        "@define-color window_bg_color %s;\n"
        "@define-color window_fg_color %s;\n"
        "@define-color view_bg_color %s;\n"
        "@define-color view_fg_color %s;\n"
        "@define-color headerbar_bg_color %s;\n"
        "@define-color headerbar_fg_color %s;\n"
        "@define-color card_bg_color %s;\n"
        "@define-color borders %s;\n"
        "@define-color theme_selected_bg_color %s;\n"
        "@define-color theme_selected_fg_color %s;\n"
        "\n"
        "window { background: %s; color: %s; }\n"
        "listview, listbox { background: transparent; }\n"
        "listbox > row { border-radius: %s; }\n"
        "listbox > row:selected { background: %s; color: %s; }\n"
        "listbox > row:hover:not(:selected) { background: %s; }\n"
        ".sidebar-box { background: %s; }\n"
        ".sidebar-box listbox > row:hover:not(:selected) { background: %s; }\n"
        ".panel-box { background: %s; }\n"
        ".toolbar { background: %s; border-bottom: 1px solid %s; padding: 4px; }\n"
        "button { border-radius: %s; }\n"
        "notebook > header { background: %s; }\n"
        ".statusbar { background: %s; border-top: 1px solid %s; color: %s; padding: 3px 8px; }\n"
        ".success-text { color: %s; }\n"
        ".warning-text { color: %s; }\n"
        ".error-text { color: %s; }\n"
        ".info-text { color: %s; }\n"
        ".muted { color: %s; }\n"
        ".accent-text { color: %s; }\n"
        ".session-dot { min-width: 10px; min-height: 10px; border-radius: 5px; }\n"
        ".c-gray { background: #8a8a8a; }\n"
        ".c-red { background: #e5484d; }\n"
        ".c-amber { background: #e5b567; }\n"
        ".c-green { background: #46a758; }\n"
        ".c-blue { background: #4f8cff; }\n"
        ".c-purple { background: #9d59e5; }\n"
        ".palette-pop { background: %s; border: 1px solid %s; border-radius: %s; padding: 6px; }\n"
        ".transfer-bar > trough { background: %s; border-radius: 3px; min-height: 8px; }\n"
        ".transfer-bar > progress { background: %s; border-radius: 3px; min-height: 8px; }\n"
        ".terminal-frame { border-radius: %s; }\n"
        "scrollbar slider { background: %s; border-radius: 4px; }\n"
        "scrollbar slider:hover { background: %s; }\n",
        t->name,
        c->accent,          /* accent_color */
        c->accent,          /* accent_bg_color */
        c->background,      /* window_bg */
        c->foreground,      /* window_fg */
        c->panel,           /* view_bg */
        c->foreground,      /* view_fg */
        c->sidebar,         /* headerbar_bg */
        c->foreground,      /* headerbar_fg */
        c->panel,           /* card_bg */
        c->border,          /* borders */
        c->selection,       /* theme_selected_bg */
        c->foreground,      /* theme_selected_fg */
        c->background,      /* window */
        c->foreground,
        c->radius,          /* row radius */
        c->selection,       /* row:selected */
        c->foreground,
        c->selection,       /* row:hover */
        c->sidebar,         /* sidebar-box */
        c->panel,           /* sidebar row hover */
        c->panel,           /* panel-box */
        c->sidebar,         /* toolbar */
        c->border,
        c->radius,          /* button */
        c->sidebar,         /* notebook header */
        c->sidebar,         /* statusbar */
        c->border,
        c->secondary,
        c->success,         /* success-text */
        c->warning,         /* warning-text */
        c->error,           /* error-text */
        c->info,            /* info-text */
        c->secondary,       /* muted */
        c->accent,          /* accent-text */
        c->panel,           /* palette-pop */
        c->border,
        c->radius,
        c->border,          /* transfer trough */
        c->accent,          /* transfer progress */
        c->radius,          /* terminal-frame */
        c->border,          /* scrollbar slider */
        c->secondary);      /* scrollbar slider hover */
    return ec_str_take(&b);
}

char** ec_theme_list(const char* dir, size_t* count_out)
{
    *count_out = 0;
    if (!dir) return NULL;
    EcVec items;
    ec_vec_init(&items);
    EcDirIter* it = NULL;
    if (ec_dir_iter_open(&it, dir)) {
        char* name;
        while ((name = ec_dir_iter_next(it)) != NULL) {
            size_t l = strlen(name);
            if (l > 5 && strcmp(name + l - 5, ".json") == 0) {
                char* full = ec_path_join(dir, name);
                if (full) {
                    if (!ec_vec_push(&items, full)) free(full);
                }
            }
            free(name);
        }
        ec_dir_iter_close(it);
    }
    *count_out = items.len;
    return (char**)items.items;
}
