/* Eclipse SSH - UI layer shared definitions (GTK4). */
#ifndef ECLIPSE_UI_H
#define ECLIPSE_UI_H

#include <gtk/gtk.h>
#include "eclipse/core.h"
#include "eclipse/repos.h"
#include "eclipse/security.h"
#include "eclipse/session.h"
#include "eclipse/theme.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct EcApp EcApp;
typedef struct EcTermPage EcTermPage;

struct EcApp {
    GtkApplication* gtk_app;
    GtkWindow* main_window;
    EcSettings settings;
    EcSessionStore sessions;
    EcSnippetStore snippets;
    EcShortcutStore shortcuts;
    EcVault* vault;
    EcHostKeyStore* hostkeys;
    EcTheme theme;              /* active theme */
    char config_dir[512];
    char data_dir[512];
    char themes_dir[512];
    char keys_dir[512];
    /* widgets (borrowed refs from main_window.c) */
    GtkWidget* sidebar_list;
    GtkWidget* stack;
    GtkWidget* notebook;        /* terminal tabs */
    GtkWidget* statusbar;
    GtkWidget* status_state;
    GtkWidget* status_conn;
    GtkWidget* welcome_page;
    EcVec live;                 /* EcTermPage* open pages */
};

/* main_window.c */
void ui_main_window_build(EcApp* app);
void ui_sidebar_refresh(EcApp* app);
void ui_status_update(EcApp* app, const char* state, const char* detail);

/* term_page.c */
EcTermPage* ui_term_page_new(EcApp* app, EcSession* profile);
GtkWidget* ui_term_page_widget(EcTermPage* page);
void ui_term_page_free(EcTermPage* page);
void ui_term_page_focus(EcTermPage* page);
void ui_term_page_run_snippet(EcTermPage* page, const char* command);

/* panes.c - split-pane terminal groups (one notebook tab = one group) */
void ui_pane_group_attach(EcApp* app, GtkWidget* container, EcTermPage* page);
void ui_pane_group_dispose(EcApp* app, GtkWidget* container);
void ui_split_pane(EcTermPage* page, bool vertical);
void ui_close_pane(EcTermPage* page);
void ui_close_tab_for_page(EcTermPage* page); /* close every pane in the tab */
void ui_note_focus(EcTermPage* page);
EcTermPage* ui_current_term_page(EcApp* app);

/* sftp_page.c */
void ui_sftp_page_build(EcApp* app); /* adds page to stack */

/* dialogs.c */
void ui_show_connect_dialog(EcApp* app, EcSession* existing);
void ui_show_hostkey_dialog(EcApp* app, const EcServerKey* key, EcHostKeyStatus st,
                            const char* host, int port, int* answer /* out: 0/1/2 */);
void ui_show_error(EcApp* app, const char* title, const char* message);
void ui_show_about(EcApp* app);
void ui_prompt_password(EcApp* app, const char* title, void (*done)(const char* pass, void*), void* user);
/* spec #13 paste confirmation: preview + explicit yes before cb runs */
void ui_confirm_paste(EcApp* app, const char* text,
                      void (*on_yes)(const char* text, void* user), void* user);

/* settings.c */
void ui_settings_page_build(EcApp* app);
void ui_settings_apply(EcApp* app); /* persist + retheme */

/* palette.c */
void ui_palette_toggle(EcApp* app);

/* theme apply */
void ui_apply_theme(EcApp* app);

#ifdef __cplusplus
}
#endif
#endif /* ECLIPSE_UI_H */
