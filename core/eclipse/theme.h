/* Eclipse SSH - theme engine: JSON theme files mapped to a role table,
 * rendered to GTK CSS at runtime (spec #6). No hard-coded colors elsewhere.
 * Six built-in themes ship with tuned UI roles AND terminal ANSI-16 palettes;
 * custom JSON themes in the data dir override builtins by name.
 */
#ifndef ECLIPSE_THEME_H
#define ECLIPSE_THEME_H

#include "eclipse/core.h"

#ifdef __cplusplus
extern "C" {
#endif

#define EC_THEME_ANSI_N 16

typedef struct {
    char background[16];
    char foreground[16];
    char primary[16];
    char secondary[16];
    char accent[16];
    char border[16];
    char panel[16];
    char sidebar[16];
    char terminal_bg[16];
    char terminal_fg[16];
    char terminal_cursor[16];
    char selection[16];
    char warning[16];
    char error[16];
    char success[16];
    char info[16];
    char ansi[EC_THEME_ANSI_N][16];
    char radius[8];   /* px */
} EcThemeColors;

typedef struct {
    char name[128];
    char appearance[16]; /* "light" | "dark" */
    EcThemeColors c;
} EcTheme;

/* Built-in theme registry (settings key + display label). */
typedef struct {
    const char* key;    /* "light", "dark", "nord", "dracula", "solarized-dark", "monokai" */
    const char* label;  /* display name for the settings dropdown */
} EcThemeDef;

const EcThemeDef* ec_theme_builtins(size_t* count_out);
const char* ec_theme_key_to_label(const char* key);
const char* ec_theme_label_to_key(const char* label);

/* Built-in fallback themes. */
void ec_theme_builtin_light(EcTheme* t);
void ec_theme_builtin_dark(EcTheme* t);
bool ec_theme_builtin_by_name(const char* key, EcTheme* out);
/* Resolve a settings theme key: builtin, then custom JSON in themes_dir whose
 * "name" matches the key (missing dir/file -> builtin). Returns true when a
 * custom file was applied. */
bool ec_theme_resolve(const char* key, const char* themes_dir, EcTheme* out);
/* Load a theme from JSON file (missing keys keep builtin defaults for appearance). */
bool ec_theme_load_file(const char* path, EcTheme* out);
bool ec_theme_load_json(const char* json, EcTheme* out);
/* Save theme to JSON. */
bool ec_theme_save(const EcTheme* t, const char* path);
/* Generate GTK CSS applying this theme (caller frees). */
char* ec_theme_css(const EcTheme* t);
/* List theme files (*.json) in a directory into caller array. */
char** ec_theme_list(const char* dir, size_t* count_out);
/* Parse "#rrggbb" (or #rgb) into 0..255 components; returns false on malformed. */
bool ec_theme_parse_hex(const char* hex, uint8_t out[3]);

#ifdef __cplusplus
}
#endif
#endif /* ECLIPSE_THEME_H */
