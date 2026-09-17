/* Eclipse SSH - theme engine implementation. */
#include "eclipse/theme.h"
#include "eclipse/platform.h"
#include "eclipse/repos.h"

#include "cJSON.h"

static bool valid_color(const char* c)
{
    if (!c || c[0] != '#' || strlen(c) != 7) return false;
    for (int i = 1; i < 7; i++) {
        char ch = c[i];
        bool hex = (ch >= '0' && ch <= '9') || (ch >= 'a' && ch <= 'f') || (ch >= 'A' && ch <= 'F');
        if (!hex) return false;
    }
    return true;
}

static void theme_defaults_for_appearance(EcTheme* t)
{
    bool dark = strcmp(t->appearance, "dark") == 0;
    EcThemeColors* c = &t->c;
    if (dark) {
        snprintf(c->background, sizeof c->background, "#1b1e23");
        snprintf(c->foreground, sizeof c->foreground, "#e6e8eb");
        snprintf(c->primary, sizeof c->primary, "#4f8cff");
        snprintf(c->secondary, sizeof c->secondary, "#9aa4b2");
        snprintf(c->accent, sizeof c->accent, "#4f8cff");
        snprintf(c->border, sizeof c->border, "#2c313a");
        snprintf(c->panel, sizeof c->panel, "#22262d");
        snprintf(c->sidebar, sizeof c->sidebar, "#191c21");
        snprintf(c->terminal_bg, sizeof c->terminal_bg, "#14171c");
        snprintf(c->terminal_fg, sizeof c->terminal_fg, "#e6e8eb");
        snprintf(c->terminal_cursor, sizeof c->terminal_cursor, "#4f8cff");
        snprintf(c->selection, sizeof c->selection, "#31465f");
        snprintf(c->warning, sizeof c->warning, "#e5b567");
        snprintf(c->error, sizeof c->error, "#e5484d");
        snprintf(c->success, sizeof c->success, "#46a758");
        snprintf(c->info, sizeof c->info, "#00b0d8");
    } else {
        snprintf(c->background, sizeof c->background, "#f7f8fa");
        snprintf(c->foreground, sizeof c->foreground, "#1c2127");
        snprintf(c->primary, sizeof c->primary, "#2563eb");
        snprintf(c->secondary, sizeof c->secondary, "#5a6572");
        snprintf(c->accent, sizeof c->accent, "#2563eb");
        snprintf(c->border, sizeof c->border, "#dde1e6");
        snprintf(c->panel, sizeof c->panel, "#ffffff");
        snprintf(c->sidebar, sizeof c->sidebar, "#eef0f3");
        snprintf(c->terminal_bg, sizeof c->terminal_bg, "#ffffff");
        snprintf(c->terminal_fg, sizeof c->terminal_fg, "#1c2127");
        snprintf(c->terminal_cursor, sizeof c->terminal_cursor, "#2563eb");
        snprintf(c->selection, sizeof c->selection, "#cfe0f8");
        snprintf(c->warning, sizeof c->warning, "#9a6700");
        snprintf(c->error, sizeof c->error, "#c62f33");
        snprintf(c->success, sizeof c->success, "#1a7f37");
        snprintf(c->info, sizeof c->info, "#0969da");
    }
    snprintf(c->radius, sizeof c->radius, "8px");
}

void ec_theme_builtin_light(EcTheme* t)
{
    snprintf(t->name, sizeof t->name, "Light");
    snprintf(t->appearance, sizeof t->appearance, "light");
    theme_defaults_for_appearance(t);
}

void ec_theme_builtin_dark(EcTheme* t)
{
    snprintf(t->name, sizeof t->name, "Dark");
    snprintf(t->appearance, sizeof t->appearance, "dark");
    theme_defaults_for_appearance(t);
}

static void load_color(cJSON* obj, const char* key, char* dst, size_t cap, const char* fallback)
{
    cJSON* j = cJSON_GetObjectItem(obj, key);
    if (cJSON_IsString(j) && valid_color(j->valuestring))
        snprintf(dst, cap, "%s", j->valuestring);
    else if (fallback)
        snprintf(dst, cap, "%s", fallback);
}

bool ec_theme_load_json(const char* json, EcTheme* out)
{
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
    theme_defaults_for_appearance(out);
    if (cJSON_IsObject(colors)) {
        EcThemeColors* c = &out->c;
        load_color(colors, "background", c->background, sizeof c->background, NULL);
        load_color(colors, "foreground", c->foreground, sizeof c->foreground, NULL);
        load_color(colors, "primary", c->primary, sizeof c->primary, NULL);
        load_color(colors, "secondary", c->secondary, sizeof c->secondary, NULL);
        load_color(colors, "accent", c->accent, sizeof c->accent, NULL);
        load_color(colors, "border", c->border, sizeof c->border, NULL);
        load_color(colors, "panel", c->panel, sizeof c->panel, NULL);
        load_color(colors, "sidebar", c->sidebar, sizeof c->sidebar, NULL);
        load_color(colors, "terminal_bg", c->terminal_bg, sizeof c->terminal_bg, NULL);
        load_color(colors, "terminal_fg", c->terminal_fg, sizeof c->terminal_fg, NULL);
        load_color(colors, "terminal_cursor", c->terminal_cursor, sizeof c->terminal_cursor, NULL);
        load_color(colors, "selection", c->selection, sizeof c->selection, NULL);
        load_color(colors, "warning", c->warning, sizeof c->warning, NULL);
        load_color(colors, "error", c->error, sizeof c->error, NULL);
        load_color(colors, "success", c->success, sizeof c->success, NULL);
        load_color(colors, "info", c->info, sizeof c->info, NULL);
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
    cJSON_AddStringToObject(colors, "background", c->background);
    cJSON_AddStringToObject(colors, "foreground", c->foreground);
    cJSON_AddStringToObject(colors, "primary", c->primary);
    cJSON_AddStringToObject(colors, "secondary", c->secondary);
    cJSON_AddStringToObject(colors, "accent", c->accent);
    cJSON_AddStringToObject(colors, "border", c->border);
    cJSON_AddStringToObject(colors, "panel", c->panel);
    cJSON_AddStringToObject(colors, "sidebar", c->sidebar);
    cJSON_AddStringToObject(colors, "terminal_bg", c->terminal_bg);
    cJSON_AddStringToObject(colors, "terminal_fg", c->terminal_fg);
    cJSON_AddStringToObject(colors, "terminal_cursor", c->terminal_cursor);
    cJSON_AddStringToObject(colors, "selection", c->selection);
    cJSON_AddStringToObject(colors, "warning", c->warning);
    cJSON_AddStringToObject(colors, "error", c->error);
    cJSON_AddStringToObject(colors, "success", c->success);
    cJSON_AddStringToObject(colors, "info", c->info);
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
        "\n"
        "window { background: %s; color: %s; }\n"
        "listview, listbox { background: transparent; }\n"
        "row:selected { background: %s; color: %s; }\n"
        ".sidebar-box { background: %s; }\n"
        ".panel-box { background: %s; }\n"
        ".toolbar { background: %s; border-bottom: 1px solid %s; }\n"
        "button { border-radius: %s; }\n"
        ".statusbar { background: %s; border-top: 1px solid %s; color: %s; }\n"
        ".success-text { color: %s; }\n"
        ".warning-text { color: %s; }\n"
        ".error-text { color: %s; }\n"
        ".info-text { color: %s; }\n"
        ".muted { color: %s; }\n"
        ".accent-text { color: %s; }\n"
        ".session-dot { min-width: 10px; min-height: 10px; border-radius: 5px; }\n"
        ".palette-pop { background: %s; border: 1px solid %s; border-radius: %s; padding: 6px; }\n"
        ".transfer-bar { background: %s; }\n",
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
        c->background,      /* window */
        c->foreground,
        c->selection,       /* row:selected */
        c->foreground,
        c->sidebar,         /* sidebar-box */
        c->panel,           /* panel-box */
        c->sidebar,         /* toolbar */
        c->border,
        c->radius,          /* button */
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
        c->accent);         /* transfer-bar */
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
