/* Eclipse SSH - GTK4 UI: main window, sidebar, tabs, dialogs, SFTP page,
 * settings, command palette. One translation unit keeps cross-widget wiring
 * explicit; see eclipse/ui.h for the public surface.
 * Dialogs use libadwaita (AdwDialog family) - GtkDialog is deprecated.
 */
#include <adwaita.h>
#include "eclipse/ui.h"
#include "eclipse/term_page.h"
#include "eclipse/platform.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <ctype.h>

/* ============================================================ theme apply */
static guint g_css_provider_installed;

void ui_apply_theme(EcApp* app)
{
    GdkDisplay* disp = gdk_display_get_default();
    char* css = ec_theme_css(&app->theme);
    if (!css) return;
    GtkCssProvider* prov = gtk_css_provider_new();
    gtk_css_provider_load_from_string(prov, css);
    gtk_style_context_add_provider_for_display(disp, GTK_STYLE_PROVIDER(prov),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    g_object_unref(prov);
    free(css);
    g_css_provider_installed++;
    /* mirror our light/dark choice so AdwDialogs match the terminal theme */
    adw_style_manager_set_color_scheme(
        adw_style_manager_get_default(),
        strcmp(app->theme.appearance, "dark") == 0 ? ADW_COLOR_SCHEME_PREFER_DARK
                                                   : ADW_COLOR_SCHEME_PREFER_LIGHT);
    /* push the new ANSI palette + default colors into every live terminal */
    ui_term_page_apply_theme_all(app);
}

/* ============================================================ statusbar */
static gboolean sb_idle(gpointer user)
{
    char** parts = user;
    gtk_label_set_text(GTK_LABEL(parts[0]), parts[1] ? parts[1] : "");
    /* colorize the connection state (theme color classes) */
    const char* state = parts[1] ? parts[1] : "";
    const char* cls =
        g_strcmp0(state, "Connected") == 0 ? "success-text" :
        g_strcmp0(state, "Failed") == 0 ? "error-text" :
        g_strcmp0(state, "Connecting") == 0 ? "warning-text" :
        g_strcmp0(state, "Closed") == 0 ? "error-text" : "muted";
    const char* prev = (const char*)g_object_get_data(G_OBJECT(parts[0]), "ec-state-cls");
    if (g_strcmp0(prev, cls) != 0) {
        if (prev && *prev)
            gtk_widget_remove_css_class(GTK_WIDGET(parts[0]), prev);
        gtk_widget_add_css_class(GTK_WIDGET(parts[0]), cls);
        g_object_set_data_full(G_OBJECT(parts[0]), "ec-state-cls", g_strdup(cls), g_free);
    }
    gtk_label_set_text(GTK_LABEL(parts[2]), parts[3] ? parts[3] : "");
    free(parts);
    return G_SOURCE_REMOVE;
}

void ui_status_update(EcApp* app, const char* state, const char* detail)
{
    (void)app;
    char** parts = malloc(4 * sizeof(char*));
    parts[0] = (char*)app->status_state;
    parts[1] = g_strdup(state);
    parts[2] = (char*)app->status_conn;
    parts[3] = g_strdup(detail ? detail : "");
    g_idle_add(sb_idle, parts);
}

/* ============================================================ sessions */
typedef struct {
    EcApp* app;
    char session_id[64];
    gint64 row_at;
} SideRow;

/* ---- tab label: session color dot + name + close button ---- */
static void on_tab_close_clicked(GtkButton* b, gpointer user)
{
    (void)b;
    ui_close_tab_for_page((EcTermPage*)user);
}

static GtkWidget* build_tab_label(EcTermPage* page)
{
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 2);
    GtkWidget* dot = gtk_label_new("");
    gtk_widget_add_css_class(dot, "session-dot");
    gtk_widget_set_valign(dot, GTK_ALIGN_CENTER);
    uint8_t r = 0x8a, g = 0x8a, bl = 0x8a;
    sscanf(page->profile.color, "#%02hhx%02hhx%02hhx", &r, &g, &bl);
    char dotclass[32];
    snprintf(dotclass, sizeof dotclass, "sdot-%02x%02x%02x", r, g, bl);
    gtk_widget_add_css_class(dot, dotclass);
    GtkWidget* close = gtk_button_new_from_icon_name("window-close-symbolic");
    gtk_button_set_has_frame(GTK_BUTTON(close), FALSE);
    gtk_widget_set_tooltip_text(close, "Close tab");
    g_signal_connect(close, "clicked", G_CALLBACK(on_tab_close_clicked), page);
    gtk_box_append(GTK_BOX(box), dot);
    if (page->label) gtk_box_append(GTK_BOX(box), page->label);
    gtk_box_append(GTK_BOX(box), close);
    return box;
}

static void activate_session(EcApp* app, const char* id)
{
    EcSession* profile = ec_sessions_find(&app->sessions, id);
    if (!profile) return;
    /* one tab per session instance; reconnecting reuses the page if closed */
    EcTermPage* page = ui_term_page_new(app, profile);
    if (!page) return;
    ec_vec_push(&app->live, page);
    GtkWidget* w = ui_term_page_widget(page);
    /* tab container hosts the split-pane group (starts with a single pane) */
    GtkWidget* container = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 0);
    gtk_box_append(GTK_BOX(container), w);
    ui_pane_group_attach(app, container, page);
    gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook), container,
                             build_tab_label(page));
    int idx = gtk_notebook_page_num(GTK_NOTEBOOK(app->notebook), container);
    gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook), idx);
    gtk_notebook_set_tab_reorderable(GTK_NOTEBOOK(app->notebook), container, TRUE);
    ui_term_page_focus(page);
}

static void on_session_row_activate(GtkListBox* list, GtkListBoxRow* row, gpointer user)
{
    EcApp* app = user;
    SideRow* sr = g_object_get_data(G_OBJECT(row), "ec-siderow");
    (void)list;
    if (sr) activate_session(app, sr->session_id);
}

static void on_new_session_clicked(GtkButton* b, gpointer user)
{
    (void)b;
    ui_show_connect_dialog((EcApp*)user, NULL);
}

static void on_settings_clicked(GtkButton* b, gpointer user)
{
    (void)b;
    EcApp* app = user;
    gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "settings");
}

static void on_sftp_clicked(GtkButton* b, gpointer user)
{
    (void)b;
    EcApp* app = user;
    gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "sftp");
}

static void on_about_clicked(GtkButton* b, gpointer user)
{
    (void)b;
    ui_show_about((EcApp*)user);
}

static void on_palette_clicked(GtkButton* b, gpointer user)
{
    (void)b;
    ui_palette_toggle((EcApp*)user);
}

static void on_split_right_clicked(GtkButton* b, gpointer user)
{
    (void)b;
    EcApp* app = user;
    ui_split_pane(ui_current_term_page(app), false);
}

static void on_split_down_clicked(GtkButton* b, gpointer user)
{
    (void)b;
    EcApp* app = user;
    ui_split_pane(ui_current_term_page(app), true);
}

static void on_close_pane_clicked(GtkButton* b, gpointer user)
{
    (void)b;
    EcApp* app = user;
    ui_close_pane(ui_current_term_page(app));
}

static GtkWidget* make_tool_button(const char* icon, const char* tip, GCallback cb, gpointer user)
{
    GtkWidget* b = gtk_button_new_from_icon_name(icon);
    gtk_widget_set_tooltip_text(b, tip);
    gtk_button_set_has_frame(GTK_BUTTON(b), FALSE);
    if (cb) g_signal_connect(b, "clicked", cb, user);
    return b;
}

void ui_sidebar_refresh(EcApp* app)
{
    GtkListBox* list = GTK_LIST_BOX(app->sidebar_list);
    for (;;) {
        GtkListBoxRow* row = gtk_list_box_get_row_at_index(list, 0);
        if (!row) break;
        gtk_list_box_remove(list, GTK_WIDGET(row));
    }
    /* per-color rules for session dots go into ONE display-wide provider
     * (per-widget GtkStyleContext providers are deprecated since 4.10) */
    char css[1024];
    size_t off = 0;
    css[0] = 0;
    for (size_t i = 0; i < app->sessions.sessions.len; i++) {
        EcSession* s = app->sessions.sessions.items[i];
        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        GtkWidget* dot = gtk_label_new("");
        gtk_widget_add_css_class(dot, "session-dot");
        uint8_t r = 0x8a, g = 0x8a, b = 0x8a;
        sscanf(s->color, "#%02hhx%02hhx%02hhx", &r, &g, &b);
        char dotclass[32];
        snprintf(dotclass, sizeof dotclass, "sdot-%02x%02x%02x", r, g, b);
        gtk_widget_add_css_class(dot, dotclass);
        if (!off || !strstr(css, dotclass)) {
            int n = snprintf(css + off, sizeof css - off,
                             ".%s { background: #%02x%02x%02x; }\n", dotclass, r, g, b);
            if (n > 0 && off + (size_t)n < sizeof css) off += (size_t)n;
        }
        GtkWidget* name = gtk_label_new(s->name);
        gtk_label_set_xalign(GTK_LABEL(name), 0.0);
        gtk_widget_set_hexpand(name, TRUE);
        gtk_box_append(GTK_BOX(box), dot);
        gtk_box_append(GTK_BOX(box), name);
        GtkWidget* roww = gtk_list_box_row_new();
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(roww), box);
        SideRow* sr = g_new0(SideRow, 1);
        sr->app = app;
        snprintf(sr->session_id, sizeof sr->session_id, "%s", s->id);
        g_object_set_data_full(G_OBJECT(roww), "ec-siderow", sr, g_free);
        gtk_list_box_append(list, roww);
    }
    /* swap in a fresh display provider carrying the current dot colors */
    static GtkCssProvider* sdot_provider = NULL;
    GdkDisplay* disp = gdk_display_get_default();
    GtkCssProvider* prov = gtk_css_provider_new();
    gtk_css_provider_load_from_string(prov, css);
    gtk_style_context_add_provider_for_display(disp, GTK_STYLE_PROVIDER(prov),
                                               GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
    if (sdot_provider) {
        gtk_style_context_remove_provider_for_display(disp, GTK_STYLE_PROVIDER(sdot_provider));
        g_object_unref(sdot_provider);
    }
    sdot_provider = prov;
}

/* ============================================================ host key dialog */
typedef struct {
    EcApp* app;
    EcServerKey key;
    EcHostKeyStatus st;
    char host[256];
    int port;
    int* answer;
    GMainLoop* loop;
} HKCtx;

static void hk_respond(AdwAlertDialog* dlg, gchar* response, gpointer user)
{
    HKCtx* ctx = user;
    (void)dlg;
    if (ctx->answer) {
        if (strcmp(response, "save") == 0)
            *ctx->answer = 2; /* accept + store */
        else if (strcmp(response, "once") == 0)
            *ctx->answer = 1; /* accept once */
        else
            *ctx->answer = 0;
    }
    if (ctx->loop) g_main_loop_quit(ctx->loop);
}

static gboolean hk_show_idle(gpointer user)
{
    HKCtx* ctx = user;
    const char* heading = ctx->st == EC_HK_CHANGED
        ? "Host key changed!"
        : "Unknown host";
    const char* warn = ctx->st == EC_HK_CHANGED
        ? "Possible man-in-the-middle attack!"
        : "First connection to this server";
    char fp[256] = "-";
    if (ctx->key.fingerprint) snprintf(fp, sizeof fp, "%s", ctx->key.fingerprint);
    char body[1024];
    snprintf(body, sizeof body,
             "%s\n\nHost: %s:%d\nKey type: %s\nFingerprint: %s\n\nAccept this host key?",
             warn, ctx->host, ctx->port, ctx->key.type ? ctx->key.type : "?", fp);
    AdwAlertDialog* dlg = ADW_ALERT_DIALOG(adw_alert_dialog_new(heading, body));
    adw_alert_dialog_add_response(dlg, "reject", "Reject");
    adw_alert_dialog_add_response(dlg, "once", "Accept once");
    if (ctx->st != EC_HK_CHANGED) {
        adw_alert_dialog_add_response(dlg, "save", "Trust and save");
        adw_alert_dialog_set_response_appearance(dlg, "save", ADW_RESPONSE_SUGGESTED);
    } else {
        adw_alert_dialog_set_response_appearance(dlg, "reject", ADW_RESPONSE_DESTRUCTIVE);
    }
    adw_alert_dialog_set_default_response(dlg, "once");
    adw_alert_dialog_set_close_response(dlg, "reject");
    g_signal_connect(dlg, "response", G_CALLBACK(hk_respond), ctx);
    adw_dialog_present(ADW_DIALOG(dlg), GTK_WIDGET(ctx->app->main_window));
    return G_SOURCE_REMOVE;
}

void ui_show_hostkey_dialog(EcApp* app, const EcServerKey* key, EcHostKeyStatus st,
                            const char* host, int port, int* answer)
{
    /* Called from a worker thread. Marshal to the UI thread and BLOCK this
     * worker on a nested main loop until the user answers. */
    HKCtx* ctx = g_new0(HKCtx, 1);
    ctx->app = app;
    ctx->key.type = key->type ? g_strdup(key->type) : NULL;
    ctx->key.blob_b64 = key->blob_b64 ? g_strdup(key->blob_b64) : NULL;
    ctx->key.fingerprint = key->fingerprint ? g_strdup(key->fingerprint) : NULL;
    ctx->st = st;
    snprintf(ctx->host, sizeof ctx->host, "%s", host);
    ctx->port = port;
    ctx->answer = answer;
    *answer = 0;
    ctx->loop = g_main_loop_new(NULL, TRUE);
    g_idle_add(hk_show_idle, ctx);
    g_main_loop_run(ctx->loop);
    g_main_loop_unref(ctx->loop);
    g_free(ctx->key.type);
    g_free(ctx->key.blob_b64);
    g_free(ctx->key.fingerprint);
    g_free(ctx);
}

/* ============================================================ password prompt */
typedef struct {
    void (*done)(const char* pass, void* user);
    void* user;
    bool answered;
    GtkWidget* entry;
} PwCtx;

/* The PwCtx is owned by the dialog (g_object data, freed on finalize); the
 * "closed" path covers Esc/close attempts so the worker never blocks forever. */
static void pw_submit(AdwDialog* dlg, PwCtx* ctx)
{
    if (ctx->answered) return;
    const char* t = gtk_editable_get_text(GTK_EDITABLE(ctx->entry));
    char* pass = g_strdup(t ? t : "");
    ctx->answered = true;
    ctx->done(pass, ctx->user);
    g_free(pass);
    adw_dialog_force_close(dlg);
}

static void pw_ok(GtkButton* b, gpointer user)
{
    (void)b;
    AdwDialog* dlg = ADW_DIALOG(user);
    PwCtx* ctx = g_object_get_data(G_OBJECT(dlg), "ec-pwctx");
    if (ctx) pw_submit(dlg, ctx);
}

static void pw_entry_activate(GtkEntry* e, gpointer user)
{
    (void)e;
    AdwDialog* dlg = ADW_DIALOG(user);
    PwCtx* ctx = g_object_get_data(G_OBJECT(dlg), "ec-pwctx");
    if (ctx) pw_submit(dlg, ctx);
}

static void pw_cancel(GtkButton* b, gpointer user)
{
    (void)b;
    adw_dialog_force_close(ADW_DIALOG(user));
}

static void pw_closed(AdwDialog* dlg, gpointer user)
{
    (void)user;
    PwCtx* ctx = g_object_get_data(G_OBJECT(dlg), "ec-pwctx");
    if (ctx && !ctx->answered) {
        ctx->answered = true;
        ctx->done(NULL, ctx->user);
    }
}

void ui_prompt_password(EcApp* app, const char* title, void (*done)(const char* pass, void* user), void* user)
{
    AdwDialog* dlg = adw_dialog_new();
    adw_dialog_set_title(dlg, title ? title : "Password");
    adw_dialog_set_content_width(dlg, 400);
    PwCtx* ctx = g_new0(PwCtx, 1);
    ctx->done = done;
    ctx->user = user;

    GtkWidget* toolbar = adw_toolbar_view_new();
    GtkWidget* hb = adw_header_bar_new();
    GtkWidget* cancel = gtk_button_new_with_label("Cancel");
    GtkWidget* ok = gtk_button_new_with_label("OK");
    gtk_widget_add_css_class(ok, "suggested-action");
    g_signal_connect(cancel, "clicked", G_CALLBACK(pw_cancel), dlg);
    g_signal_connect(ok, "clicked", G_CALLBACK(pw_ok), dlg);
    adw_header_bar_pack_start(ADW_HEADER_BAR(hb), cancel);
    adw_header_bar_pack_end(ADW_HEADER_BAR(hb), ok);
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), hb);

    GtkWidget* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(content, 12);
    gtk_widget_set_margin_bottom(content, 24);
    gtk_widget_set_margin_start(content, 18);
    gtk_widget_set_margin_end(content, 18);
    GtkWidget* entry = gtk_password_entry_new();
    gtk_password_entry_set_show_peek_icon(GTK_PASSWORD_ENTRY(entry), TRUE); /* spec #32 */
    gtk_widget_set_hexpand(entry, TRUE);
    GtkWidget* lbl = gtk_label_new("Password:");
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
    gtk_box_append(GTK_BOX(content), lbl);
    gtk_box_append(GTK_BOX(content), entry);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar), content);

    ctx->entry = entry;
    g_object_set_data_full(G_OBJECT(dlg), "ec-pwctx", ctx, g_free);
    adw_dialog_set_child(dlg, toolbar);
    g_signal_connect(entry, "activate", G_CALLBACK(pw_entry_activate), dlg);
    g_signal_connect(dlg, "closed", G_CALLBACK(pw_closed), NULL);
    adw_dialog_present(dlg, GTK_WIDGET(app->main_window));
    gtk_widget_grab_focus(entry);
}

/* ============================================================ paste confirm (spec #13) */
typedef struct {
    char* text;
    void (*on_yes)(const char* text, void* user);
    void* user;
} PasteConfirmCtx;

static void cp_respond(AdwAlertDialog* dlg, gchar* response, gpointer user)
{
    (void)dlg;
    PasteConfirmCtx* ctx = user;
    if (strcmp(response, "paste") == 0 && ctx->on_yes)
        ctx->on_yes(ctx->text, ctx->user);
    g_free(ctx->text);
    g_free(ctx);
}

void ui_confirm_paste(EcApp* app, const char* text,
                      void (*on_yes)(const char* text, void* user), void* user)
{
    /* build an escaped preview of the payload (newlines visible!) */
    size_t n = strlen(text);
    size_t cap = n > 300 ? 300 : n;
    char preview[512];
    size_t off = 0;
    for (size_t i = 0; i < cap && off + 12 < sizeof preview; i++) {
        char ch = text[i];
        const char* rep = NULL;
        if (ch == '\n') rep = "\\n";
        else if (ch == '\r') rep = "\\r";
        else if (ch == '\t') rep = "\\t";
        if (rep) {
            int k = snprintf(preview + off, sizeof preview - off, "%s", rep);
            if (k > 0) off += (size_t)k;
        } else {
            preview[off++] = ch;
        }
    }
    preview[off] = 0;
    char body[768];
    snprintf(body, sizeof body, "This text will be sent to the remote shell:\n\n%s%s",
             preview, n > 300 ? "…" : "");
    AdwAlertDialog* dlg = ADW_ALERT_DIALOG(adw_alert_dialog_new("Paste into terminal?", body));
    adw_alert_dialog_add_response(dlg, "cancel", "Cancel");
    adw_alert_dialog_add_response(dlg, "paste", "Paste");
    adw_alert_dialog_set_response_appearance(dlg, "paste", ADW_RESPONSE_SUGGESTED);
    adw_alert_dialog_set_default_response(dlg, "cancel");
    adw_alert_dialog_set_close_response(dlg, "cancel");
    PasteConfirmCtx* ctx = g_new0(PasteConfirmCtx, 1);
    ctx->text = g_strdup(text);
    ctx->on_yes = on_yes;
    ctx->user = user;
    g_signal_connect(dlg, "response", G_CALLBACK(cp_respond), ctx);
    adw_dialog_present(ADW_DIALOG(dlg), GTK_WIDGET(app->main_window));
}

/* ============================================================ connect dialog */
static void connect_apply(GtkButton* b, gpointer user)
{
    (void)b;
    AdwDialog* dlg = ADW_DIALOG(user);
    EcApp* app = g_object_get_data(G_OBJECT(dlg), "ec-app");
    EcSession* existing = g_object_get_data(G_OBJECT(dlg), "ec-existing");
    EcSession* s = existing ? existing : ec_sessions_add(&app->sessions, "New session");
    GtkEntry* e_name = g_object_get_data(G_OBJECT(dlg), "e-name");
    GtkEntry* e_host = g_object_get_data(G_OBJECT(dlg), "e-host");
    GtkEntry* e_port = g_object_get_data(G_OBJECT(dlg), "e-port");
    GtkEntry* e_user = g_object_get_data(G_OBJECT(dlg), "e-user");
    GtkDropDown* c_auth = g_object_get_data(G_OBJECT(dlg), "c-auth");
    GtkEntry* e_pass = g_object_get_data(G_OBJECT(dlg), "e-pass");
    GtkEntry* e_key = g_object_get_data(G_OBJECT(dlg), "e-key");
    GtkDropDown* c_color = g_object_get_data(G_OBJECT(dlg), "c-color");
    GtkTextView* t_init = g_object_get_data(G_OBJECT(dlg), "t-init");
    snprintf(s->name, sizeof s->name, "%s", gtk_editable_get_text(GTK_EDITABLE(e_name)));
    snprintf(s->host, sizeof s->host, "%s", gtk_editable_get_text(GTK_EDITABLE(e_host)));
    s->port = atoi(gtk_editable_get_text(GTK_EDITABLE(e_port)));
    if (!ec_valid_port(s->port)) s->port = 22;
    snprintf(s->username, sizeof s->username, "%s", gtk_editable_get_text(GTK_EDITABLE(e_user)));
    int am = (int)gtk_drop_down_get_selected(c_auth);
    s->auth_mode = am == 0 ? EC_AUTH_PASSWORD : am == 1 ? EC_AUTH_KEY : EC_AUTH_AGENT;
    snprintf(s->key_path, sizeof s->key_path, "%s", gtk_editable_get_text(GTK_EDITABLE(e_key)));
    GtkStringObject* so = GTK_STRING_OBJECT(gtk_drop_down_get_selected_item(c_color));
    const char* color = so ? gtk_string_object_get_string(so) : NULL;
    snprintf(s->color, sizeof s->color, "%s", color ? color : "#4f8cff");
    GtkTextBuffer* tb = gtk_text_view_get_buffer(t_init);
    GtkTextIter a, b2;
    gtk_text_buffer_get_bounds(tb, &a, &b2);
    char* txt = gtk_text_buffer_get_text(tb, &a, &b2, FALSE);
    snprintf(s->init_commands, sizeof s->init_commands, "%s", txt ? txt : "");
    g_free(txt);
    /* persist secret to vault, never to session JSON (spec #10/#32) */
    if (s->auth_mode == EC_AUTH_PASSWORD) {
        const char* pass = gtk_editable_get_text(GTK_EDITABLE(e_pass));
        if (pass && *pass && app->vault)
            ec_vault_set(app->vault, s->id, s->username, pass);
    }
    ec_sessions_save(&app->sessions);
    ui_sidebar_refresh(app);
    if (!existing) activate_session(app, s->id);
    adw_dialog_force_close(dlg);
}

static void connect_cancel(GtkButton* b, gpointer user)
{
    (void)b;
    adw_dialog_force_close(ADW_DIALOG(user));
}

void ui_show_connect_dialog(EcApp* app, EcSession* existing)
{
    AdwDialog* dlg = adw_dialog_new();
    adw_dialog_set_title(dlg, existing ? "Edit session" : "New session");
    adw_dialog_set_content_width(dlg, 480);
    adw_dialog_set_content_height(dlg, 520);

    GtkWidget* toolbar = adw_toolbar_view_new();
    GtkWidget* hb = adw_header_bar_new();
    GtkWidget* cancel = gtk_button_new_with_label("Cancel");
    GtkWidget* ok = gtk_button_new_with_label("Connect");
    gtk_widget_add_css_class(ok, "suggested-action");
    g_signal_connect(cancel, "clicked", G_CALLBACK(connect_cancel), dlg);
    g_signal_connect(ok, "clicked", G_CALLBACK(connect_apply), dlg);
    adw_header_bar_pack_start(ADW_HEADER_BAR(hb), cancel);
    adw_header_bar_pack_end(ADW_HEADER_BAR(hb), ok);
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), hb);

    GtkWidget* scroll = gtk_scrolled_window_new();
    GtkWidget* grid = gtk_grid_new();
    int row = 0;
    GtkWidget* lbl;
#define ADD_ROW(label, w) (lbl = gtk_label_new(label), gtk_label_set_xalign(GTK_LABEL(lbl), 0.0), \
    gtk_grid_attach(GTK_GRID(grid), lbl, 0, row, 1, 1), \
    gtk_grid_attach(GTK_GRID(grid), w, 1, row, 1, 1), gtk_widget_set_hexpand(w, TRUE), row++)
    GtkWidget* e_name = gtk_entry_new();
    GtkWidget* e_host = gtk_entry_new();
    GtkWidget* e_port = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(e_port), "22");
    GtkWidget* e_user = gtk_entry_new();
    GtkWidget* c_auth = gtk_drop_down_new_from_strings(
        (const char* const[]){ "Password", "Private key", "Agent", NULL });
    gtk_drop_down_set_selected(GTK_DROP_DOWN(c_auth), 0);
    GtkWidget* e_pass = gtk_password_entry_new();
    gtk_password_entry_set_show_peek_icon(GTK_PASSWORD_ENTRY(e_pass), TRUE);
    GtkWidget* e_key = gtk_entry_new();
    gtk_widget_set_tooltip_text(e_key, "Path to OpenSSH private key");
    GtkWidget* c_color = gtk_drop_down_new_from_strings(
        (const char* const[]){ "#e5484d", "#e5b567", "#46a758", "#4f8cff", "#9d59e5", NULL });
    gtk_drop_down_set_selected(GTK_DROP_DOWN(c_color), 3);
    GtkWidget* scroll2 = gtk_scrolled_window_new();
    GtkWidget* t_init = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(t_init), GTK_WRAP_WORD_CHAR);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll2), t_init);
    gtk_widget_set_size_request(scroll2, -1, 80);
    ADD_ROW("Name", e_name);
    ADD_ROW("Host", e_host);
    ADD_ROW("Port", e_port);
    ADD_ROW("User", e_user);
    ADD_ROW("Auth", c_auth);
    ADD_ROW("Password", e_pass);
    ADD_ROW("Key path", e_key);
    ADD_ROW("Color tag", c_color);
    ADD_ROW("Startup commands", scroll2);
#undef ADD_ROW
    gtk_widget_set_margin_top(grid, 12);
    gtk_widget_set_margin_bottom(grid, 12);
    gtk_widget_set_margin_start(grid, 18);
    gtk_widget_set_margin_end(grid, 18);
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), grid);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar), scroll);
    adw_dialog_set_child(dlg, toolbar);
    if (existing) {
        gtk_editable_set_text(GTK_EDITABLE(e_name), existing->name);
        gtk_editable_set_text(GTK_EDITABLE(e_host), existing->host);
        char port[16];
        snprintf(port, sizeof port, "%d", existing->port);
        gtk_editable_set_text(GTK_EDITABLE(e_port), port);
        gtk_editable_set_text(GTK_EDITABLE(e_user), existing->username);
        gtk_drop_down_set_selected(GTK_DROP_DOWN(c_auth), (guint)existing->auth_mode);
        gtk_editable_set_text(GTK_EDITABLE(e_key), existing->key_path);
        GtkTextBuffer* tb = gtk_text_view_get_buffer(GTK_TEXT_VIEW(t_init));
        gtk_text_buffer_set_text(tb, existing->init_commands, -1);
    }
    g_object_set_data(G_OBJECT(dlg), "e-name", e_name);
    g_object_set_data(G_OBJECT(dlg), "e-host", e_host);
    g_object_set_data(G_OBJECT(dlg), "e-port", e_port);
    g_object_set_data(G_OBJECT(dlg), "e-user", e_user);
    g_object_set_data(G_OBJECT(dlg), "c-auth", c_auth);
    g_object_set_data(G_OBJECT(dlg), "e-pass", e_pass);
    g_object_set_data(G_OBJECT(dlg), "e-key", e_key);
    g_object_set_data(G_OBJECT(dlg), "c-color", c_color);
    g_object_set_data(G_OBJECT(dlg), "t-init", t_init);
    g_object_set_data(G_OBJECT(dlg), "ec-app", app);
    g_object_set_data(G_OBJECT(dlg), "ec-existing", existing);
    adw_dialog_present(dlg, GTK_WIDGET(app->main_window));
}

/* ============================================================ error/about */
static void err_response(AdwAlertDialog* dlg, gchar* response, gpointer user)
{
    (void)response; (void)user;
    adw_dialog_force_close(ADW_DIALOG(dlg));
}

void ui_show_error(EcApp* app, const char* title, const char* message)
{
    AdwAlertDialog* dlg = ADW_ALERT_DIALOG(adw_alert_dialog_new(title, message ? message : title));
    adw_alert_dialog_add_response(dlg, "close", "Close");
    adw_alert_dialog_set_default_response(dlg, "close");
    adw_alert_dialog_set_close_response(dlg, "close");
    g_signal_connect(dlg, "response", G_CALLBACK(err_response), NULL);
    adw_dialog_present(ADW_DIALOG(dlg), GTK_WIDGET(app->main_window));
}

void ui_show_about(EcApp* app)
{
    AdwDialog* dlg = ADW_DIALOG(adw_about_dialog_new());
    g_object_set(G_OBJECT(dlg),
                 "application-name", "Eclipse SSH",
                 "program-name", "Eclipse SSH",
                 "version", "1.1.0",
                 "comments", "Native SSH/SFTP client for Windows and Linux",
                 "license-type", GTK_LICENSE_MIT_X11,
                 "website", "https://github.com/SecretArrow/ESD",
                 NULL);
    adw_dialog_present(dlg, GTK_WIDGET(app->main_window));
}

/* ============================================================ SFTP page */
typedef struct {
    EcApp* app;
    EcLiveSession* live;   /* selected live session providing SFTP */
    GtkWidget* local_view;
    GtkWidget* remote_view;
    GtkWidget* remote_cwd_label;
    GtkWidget* local_cwd_label;
    GtkWidget* queue_list;
    char local_cwd[1024];
    char remote_cwd[1024];
    size_t prev_done;      /* finished transfers last tick (auto-refresh) */
    EcVec transfers;
} SftpPage;

static SftpPage* g_sftp; /* single SFTP page instance */

/* ---- browsable file rows (both panes) ---- */
typedef struct {
    char* name;            /* display name (dirs carry a trailing '/') */
    char* path;            /* full path used for navigation/uploads */
    bool is_dir;
    bool is_parent;        /* the ".." row */
    uint64_t size;
} SftpRowEnt;

static int row_cmp(const void* a, const void* b)
{
    /* qsort over EcVec of SftpRowEnt* : args are pointer-to-element */
    const SftpRowEnt* const* pa = a;
    const SftpRowEnt* const* pb = b;
    const SftpRowEnt* x = *pa;
    const SftpRowEnt* y = *pb;
    if (x->is_parent != y->is_parent) return x->is_parent ? -1 : 1;
    if (x->is_dir != y->is_dir) return x->is_dir ? -1 : 1;
    const char* s1 = x->name;
    const char* s2 = y->name;
    while (*s1 && *s2) {
        int c1 = tolower((unsigned char)*s1);
        int c2 = tolower((unsigned char)*s2);
        if (c1 != c2) return c1 - c2;
        s1++; s2++;
    }
    return (unsigned char)*s1 - (unsigned char)*s2;
}

/* heap-allocate one row entry (the vec stores pointers; stack copies would
 * dangle) */
static void push_row(EcVec* rows, const char* name, const char* path,
                     bool is_dir, bool is_parent, uint64_t size)
{
    SftpRowEnt* e = g_new0(SftpRowEnt, 1);
    e->name = g_strdup(name);
    e->path = g_strdup(path);
    e->is_dir = is_dir;
    e->is_parent = is_parent;
    e->size = size;
    ec_vec_push(rows, e);
}

static void free_row(SftpRowEnt* e)
{
    if (!e) return;
    g_free(e->name);
    g_free(e->path);
    g_free(e);
}

static GtkWidget* sftp_row_new(const SftpRowEnt* e)
{
    GtkWidget* row = gtk_list_box_row_new();
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    GtkWidget* icon = gtk_image_new_from_icon_name(
        e->is_dir ? "folder-symbolic" : "text-x-generic-symbolic");
    gtk_widget_add_css_class(icon, "muted");
    GtkWidget* lbl = gtk_label_new(e->name);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
    gtk_widget_set_hexpand(lbl, TRUE);
    gtk_label_set_ellipsize(GTK_LABEL(lbl), PANGO_ELLIPSIZE_MIDDLE);
    gtk_box_append(GTK_BOX(box), icon);
    gtk_box_append(GTK_BOX(box), lbl);
    if (!e->is_dir && e->size > 0) {
        char hs[32];
        ec_human_size(e->size, hs, sizeof hs);
        GtkWidget* sz = gtk_label_new(hs);
        gtk_label_set_xalign(GTK_LABEL(sz), 1.0);
        gtk_widget_add_css_class(sz, "muted");
        gtk_widget_set_margin_end(sz, 8);
        gtk_box_append(GTK_BOX(box), sz);
    }
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
    g_object_set_data_full(G_OBJECT(row), "ec-path", g_strdup(e->path), g_free);
    g_object_set_data(G_OBJECT(row), "ec-isdir", GINT_TO_POINTER(e->is_dir ? 1 : 0));
    return row;
}

static void list_clear(GtkWidget* view)
{
    GtkListBox* list = GTK_LIST_BOX(view);
    for (;;) {
        GtkListBoxRow* row = gtk_list_box_get_row_at_index(list, 0);
        if (!row) break;
        gtk_list_box_remove(list, GTK_WIDGET(row));
    }
}

static void local_list(const char* dir, GtkWidget* view)
{
    list_clear(view);
    GtkListBox* list = GTK_LIST_BOX(view);
    EcVec rows;
    ec_vec_init(&rows);
    /* parent navigation */
    char* parent = ec_path_dirname(dir);
    if (parent && strcmp(parent, dir) != 0)
        push_row(&rows, "..", parent, true, true, 0);
    free(parent);
    EcDirIter* it = NULL;
    if (ec_dir_iter_open(&it, dir)) {
        char* name;
        while ((name = ec_dir_iter_next(it)) != NULL) {
            if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) { free(name); continue; }
            char* full = ec_path_join(dir, name);
            bool isdir = full && ec_file_is_dir(full);
            char* disp = isdir ? g_strdup_printf("%s/", name) : g_strdup(name);
            uint64_t sz = 0;
            if (!isdir && full) (void)ec_file_size(full, &sz);
            push_row(&rows, disp, full, isdir, false, sz);
            g_free(disp);
            free(full);
            free(name);
        }
        ec_dir_iter_close(it);
    }
    if (rows.len > 1)
        qsort(rows.items, rows.len, sizeof(SftpRowEnt*), row_cmp);
    for (size_t i = 0; i < rows.len; i++)
        gtk_list_box_append(list, sftp_row_new(rows.items[i]));
    for (size_t i = 0; i < rows.len; i++)
        free_row(rows.items[i]);
    ec_vec_free(&rows);
}

static gboolean sftp_refresh_idle(gpointer user)
{
    SftpPage* pg = user;
    if (pg->live && pg->live->sftp && pg->remote_cwd[0]) {
        size_t n = 0;
        char* err = NULL;
        EcSftpEntry* entries = ec_sftp_list(pg->live->sftp, pg->remote_cwd, &n, &err);
        list_clear(pg->remote_view);
        GtkListBox* list = GTK_LIST_BOX(pg->remote_view);
        EcVec rows;
        ec_vec_init(&rows);
        char* parent = ec_path_dirname(pg->remote_cwd);
        if (parent && strcmp(parent, pg->remote_cwd) != 0)
            push_row(&rows, "..", parent, true, true, 0);
        free(parent);
        if (entries) {
            for (size_t i = 0; i < n; i++) {
                const char* nm = entries[i].name ? entries[i].name : "?";
                if (strcmp(nm, ".") == 0 || strcmp(nm, "..") == 0) continue;
                char* path = ec_path_join(pg->remote_cwd, nm);
                char* disp = g_strdup_printf(entries[i].is_dir ? "%s/" : "%s", nm);
                push_row(&rows, disp, path, entries[i].is_dir, false, entries[i].size);
                g_free(disp);
                free(path);
            }
            ec_sftp_entries_free(entries, n);
        }
        if (rows.len > 1)
            qsort(rows.items, rows.len, sizeof(SftpRowEnt*), row_cmp);
        for (size_t i = 0; i < rows.len; i++)
            gtk_list_box_append(list, sftp_row_new(rows.items[i]));
        for (size_t i = 0; i < rows.len; i++)
            free_row(rows.items[i]);
        ec_vec_free(&rows);
        free(err);
    }
    return G_SOURCE_REMOVE;
}

static void on_local_row_activated(GtkListBox* list, GtkListBoxRow* row, gpointer user)
{
    (void)list;
    SftpPage* pg = user;
    const char* path = (const char*)g_object_get_data(G_OBJECT(row), "ec-path");
    int isdir = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "ec-isdir"));
    if (!path || !isdir) return;
    snprintf(pg->local_cwd, sizeof pg->local_cwd, "%s", path);
    gtk_label_set_text(GTK_LABEL(pg->local_cwd_label), pg->local_cwd);
    local_list(pg->local_cwd, pg->local_view);
}

static void on_remote_row_activated(GtkListBox* list, GtkListBoxRow* row, gpointer user)
{
    (void)list;
    SftpPage* pg = user;
    const char* path = (const char*)g_object_get_data(G_OBJECT(row), "ec-path");
    int isdir = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "ec-isdir"));
    if (!path || !isdir) return;
    snprintf(pg->remote_cwd, sizeof pg->remote_cwd, "%s", path);
    gtk_label_set_text(GTK_LABEL(pg->remote_cwd_label), pg->remote_cwd);
    g_idle_add(sftp_refresh_idle, pg); /* blocking SFTP op off the first paint */
}

static EcLiveSession* first_connected_session(EcApp* app)
{
    for (size_t i = 0; i < app->live.len; i++) {
        EcTermPage* p = app->live.items[i];
        if (p->live && p->live->state == EC_LV_CONNECTED)
            return p->live;
    }
    return NULL;
}

static void sftp_set_session(SftpPage* pg)
{
    pg->live = first_connected_session(pg->app);
    if (pg->live && pg->live->sftp) {
        char* home = ec_sftp_canonicalize(pg->live->sftp, ".");
        snprintf(pg->remote_cwd, sizeof pg->remote_cwd, "%s", home ? home : "/");
        free(home);
        gtk_label_set_text(GTK_LABEL(pg->remote_cwd_label), pg->remote_cwd);
        g_idle_add(sftp_refresh_idle, pg);
    }
}

static void on_sftp_refresh_clicked(GtkButton* b, gpointer user)
{
    (void)b;
    SftpPage* pg = user;
    if (!pg->live) sftp_set_session(pg);
    else g_idle_add(sftp_refresh_idle, pg);
    local_list(pg->local_cwd, pg->local_view);
}

typedef struct { EcTransfer* t; SftpPage* pg; } TIdle;

static gboolean transfer_progress_idle(gpointer user)
{
    TIdle* m = user;
    /* update the first matching queue row label */
    GtkListBox* list = GTK_LIST_BOX(m->pg->queue_list);
    guint n = g_list_model_get_n_items(G_LIST_MODEL(list));
    for (guint i = 0; i < n; i++) {
        GtkListBoxRow* row = gtk_list_box_get_row_at_index(list, (int)i);
        if (row && g_object_get_data(G_OBJECT(row), "ec-xfer") == m->t) {
            GtkWidget* box = gtk_list_box_row_get_child(row);
            GtkWidget* bar = g_object_get_data(G_OBJECT(box), "bar");
            double frac = m->t->total ? (double)m->t->done / (double)m->t->total : 0;
            gtk_progress_bar_set_fraction(GTK_PROGRESS_BAR(bar), frac);
            GtkWidget* lbl = g_object_get_data(G_OBJECT(box), "lbl");
            if (lbl) {
                char desc[512], hs[32], tot[32];
                ec_human_size(m->t->done, hs, sizeof hs);
                ec_human_size(m->t->total, tot, sizeof tot);
                snprintf(desc, sizeof desc, "%s \xe2\x80\x94 %s / %s",
                         m->t->state == EC_TR_DONE ? "Done" :
                         m->t->state == EC_TR_ERROR ? "Failed" :
                         m->t->state == EC_TR_CANCELLED ? "Cancelled" : "Transferring",
                         hs, tot);
                gtk_label_set_text(GTK_LABEL(lbl), desc);
            }
            break;
        }
    }
    g_free(m);
    return G_SOURCE_REMOVE;
}

static void transfer_progress_cb(EcTransfer* t, void* user)
{
    SftpPage* pg = user;
    TIdle* m = g_new0(TIdle, 1);
    m->t = t;
    m->pg = pg;
    g_idle_add(transfer_progress_idle, m);
}

/* queue row helper used by transfers below */
static void tq_add_row(SftpPage* pg, EcTransfer* t)
{
    GtkWidget* box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 2);
    GtkWidget* lbl = gtk_label_new(t->remote_path);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(lbl), PANGO_ELLIPSIZE_MIDDLE);
    GtkWidget* bar = gtk_progress_bar_new();
    gtk_widget_add_css_class(bar, "transfer-bar");
    g_object_set_data(G_OBJECT(box), "bar", bar);
    g_object_set_data(G_OBJECT(box), "lbl", lbl);
    gtk_box_append(GTK_BOX(box), lbl);
    gtk_box_append(GTK_BOX(box), bar);
    GtkWidget* row = gtk_list_box_row_new();
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
    g_object_set_data(G_OBJECT(row), "ec-xfer", t);
    gtk_list_box_append(GTK_LIST_BOX(pg->queue_list), row);
}

/* enqueue the selected row of a pane (upload: local, download: remote) */
static void sftp_enqueue_selected(SftpPage* pg, GtkWidget* view, bool upload)
{
    if (!pg->live || !pg->live->sftp) {
        ui_show_error(pg->app, "SFTP", "Connect a session first, then reopen the Transfers tab.");
        return;
    }
    GtkListBoxRow* row = gtk_list_box_get_selected_row(GTK_LIST_BOX(view));
    if (!row) return;
    const char* path = (const char*)g_object_get_data(G_OBJECT(row), "ec-path");
    int isdir = GPOINTER_TO_INT(g_object_get_data(G_OBJECT(row), "ec-isdir"));
    if (!path || isdir) {
        ui_show_error(pg->app, "SFTP", "Select a file (directories cannot be queued).");
        return;
    }
    const char* base = strrchr(path, '/');
    base = base ? base + 1 : path;
    char* remote = upload ? ec_path_join(pg->remote_cwd, base) : g_strdup(path);
    char* local = upload ? g_strdup(path) : ec_path_join(pg->local_cwd, base);
    EcTransfer* t = ec_tq_add(&pg->live->transfer_queue,
                              upload ? EC_TR_UPLOAD : EC_TR_DOWNLOAD,
                              pg->live->ssh, pg->live->sftp,
                              local, remote, transfer_progress_cb, pg);
    if (t) tq_add_row(pg, t);
    free(local);
    free(remote);
}

static void on_sftp_upload_clicked(GtkButton* b, gpointer user)
{
    (void)b;
    SftpPage* pg = user;
    sftp_enqueue_selected(pg, pg->local_view, true);
}

static void on_sftp_download_clicked(GtkButton* b, gpointer user)
{
    (void)b;
    SftpPage* pg = user;
    sftp_enqueue_selected(pg, pg->remote_view, false);
}

/* transfers pump + queue UI tick; refreshes the remote pane when a
 * transfer reaches a terminal state so new files show up immediately */
static gboolean sftp_tick(gpointer user)
{
    SftpPage* pg = user;
    if (pg->live) {
        EcTransferQueue* q = &pg->live->transfer_queue;
        size_t done = 0;
        for (size_t i = 0; i < q->items.len; i++) {
            EcTransfer* t = q->items.items[i];
            if (t->state == EC_TR_DONE || t->state == EC_TR_ERROR ||
                t->state == EC_TR_CANCELLED)
                done++;
        }
        if (pg->prev_done > 0 && done > pg->prev_done && pg->remote_cwd[0])
            g_idle_add(sftp_refresh_idle, pg);
        pg->prev_done = done;
        ec_tq_pump(q);
    }
    return G_SOURCE_CONTINUE;
}

void ui_sftp_page_build(EcApp* app)
{
    SftpPage* pg = g_new0(SftpPage, 1);
    g_sftp = pg;
    pg->app = app;
    char* cwd = ec_temp_dir();
    snprintf(pg->local_cwd, sizeof pg->local_cwd, "%s", getenv("HOME") ? getenv("HOME") : (cwd ? cwd : "/"));
    free(cwd);

    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 6);
    gtk_widget_set_margin_top(root, 8);
    gtk_widget_set_margin_bottom(root, 8);
    gtk_widget_set_margin_start(root, 8);
    gtk_widget_set_margin_end(root, 8);

    GtkWidget* toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 6);
    gtk_box_append(GTK_BOX(toolbar), make_tool_button("view-refresh-symbolic", "Refresh",
                     G_CALLBACK(on_sftp_refresh_clicked), pg));
    gtk_box_append(GTK_BOX(toolbar), make_tool_button("go-up-symbolic", "Upload selected file",
                     G_CALLBACK(on_sftp_upload_clicked), pg));
    gtk_box_append(GTK_BOX(toolbar), make_tool_button("go-down-symbolic", "Download selected file",
                     G_CALLBACK(on_sftp_download_clicked), pg));
    GtkWidget* hint = gtk_label_new("Double-click a folder to open it; .. goes up.");
    gtk_label_set_xalign(GTK_LABEL(hint), 1.0);
    gtk_widget_add_css_class(hint, "muted");
    gtk_widget_set_hexpand(hint, TRUE);
    gtk_box_append(GTK_BOX(toolbar), hint);
    gtk_box_append(GTK_BOX(root), toolbar);

    GtkWidget* paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    /* local pane */
    GtkWidget* local_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    pg->local_cwd_label = gtk_label_new(pg->local_cwd);
    gtk_label_set_xalign(GTK_LABEL(pg->local_cwd_label), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(pg->local_cwd_label), PANGO_ELLIPSIZE_START);
    gtk_box_append(GTK_BOX(local_box), pg->local_cwd_label);
    GtkWidget* local_scroll = gtk_scrolled_window_new();
    pg->local_view = gtk_list_box_new();
    g_signal_connect(pg->local_view, "row-activated", G_CALLBACK(on_local_row_activated), pg);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(local_scroll), pg->local_view);
    gtk_widget_set_vexpand(local_scroll, TRUE);
    gtk_box_append(GTK_BOX(local_box), local_scroll);
    /* remote pane */
    GtkWidget* remote_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    pg->remote_cwd_label = gtk_label_new("(not connected)");
    gtk_label_set_xalign(GTK_LABEL(pg->remote_cwd_label), 0.0);
    gtk_label_set_ellipsize(GTK_LABEL(pg->remote_cwd_label), PANGO_ELLIPSIZE_START);
    gtk_box_append(GTK_BOX(remote_box), pg->remote_cwd_label);
    GtkWidget* remote_scroll = gtk_scrolled_window_new();
    pg->remote_view = gtk_list_box_new();
    g_signal_connect(pg->remote_view, "row-activated", G_CALLBACK(on_remote_row_activated), pg);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(remote_scroll), pg->remote_view);
    gtk_widget_set_vexpand(remote_scroll, TRUE);
    gtk_box_append(GTK_BOX(remote_box), remote_scroll);

    gtk_paned_set_start_child(GTK_PANED(paned), local_box);
    gtk_paned_set_end_child(GTK_PANED(paned), remote_box);
    gtk_paned_set_position(GTK_PANED(paned), 400);
    gtk_widget_set_vexpand(paned, TRUE);
    gtk_box_append(GTK_BOX(root), paned);

    /* queue */
    GtkWidget* qscroll = gtk_scrolled_window_new();
    pg->queue_list = gtk_list_box_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(qscroll), pg->queue_list);
    gtk_widget_set_size_request(qscroll, -1, 120);
    gtk_box_append(GTK_BOX(root), qscroll);

    gtk_stack_add_named(GTK_STACK(app->stack), root, "sftp");
    local_list(pg->local_cwd, pg->local_view);
    g_timeout_add(250, sftp_tick, pg);
}

/* ============================================================ settings page */
static char* settings_path(EcApp* app)
{
    return ec_path_join(app->config_dir, "settings.json");
}

static void settings_persist(EcApp* app)
{
    char* p = settings_path(app);
    ec_settings_save(&app->settings, p);
    free(p);
}

static void on_theme_selected(GtkDropDown* dd, GParamSpec* ps, gpointer user)
{
    (void)ps;
    EcApp* app = user;
    GtkStringObject* so = GTK_STRING_OBJECT(gtk_drop_down_get_selected_item(dd));
    if (!so) return;
    const char* key = ec_theme_label_to_key(gtk_string_object_get_string(so));
    if (strcmp(app->settings.theme, key) == 0) return;
    snprintf(app->settings.theme, sizeof app->settings.theme, "%s", key);
    settings_persist(app);
    /* rebuild the active theme (builtin, then custom file override) and
     * re-theme: CSS, Adwaita scheme, every live terminal palette */
    ec_theme_resolve(app->settings.theme, app->themes_dir, &app->theme);
    ui_apply_theme(app);
}

static void on_font_size_changed(GtkSpinButton* sp, gpointer user)
{
    EcApp* app = user;
    int v = gtk_spin_button_get_value_as_int(sp);
    if (v == app->settings.font_size) return;
    app->settings.font_size = v;
    ui_term_page_apply_font_all(app); /* live reflow of open terminals */
    settings_persist(app);
}

static void on_font_name_changed(GtkEntry* e, gpointer user)
{
    EcApp* app = user;
    const char* t = gtk_editable_get_text(GTK_EDITABLE(e));
    if (!t || !*t || strcmp(t, app->settings.font_name) == 0) return;
    snprintf(app->settings.font_name, sizeof app->settings.font_name, "%s", t);
    ui_term_page_apply_font_all(app);
    settings_persist(app);
}

static void on_keepalive_changed(GtkSpinButton* sp, gpointer user)
{
    EcApp* app = user;
    int v = gtk_spin_button_get_value_as_int(sp);
    if (v == app->settings.keepalive_sec) return;
    app->settings.keepalive_sec = v;
    settings_persist(app);
}

static void on_scrollback_changed(GtkSpinButton* sp, gpointer user)
{
    EcApp* app = user;
    int v = gtk_spin_button_get_value_as_int(sp);
    if (v == app->settings.scrollback_lines) return;
    app->settings.scrollback_lines = v;
    settings_persist(app); /* applies to newly opened terminals */
}

static void settings_toggle(GtkCheckButton* btn, gpointer user)
{
    EcApp* app = user;
    const char* kind = (const char*)g_object_get_data(G_OBJECT(btn), "ec-kind");
    if (!kind) return;
    gboolean active = gtk_check_button_get_active(btn);
    if (strcmp(kind, "copy") == 0)
        app->settings.copy_on_select = active != FALSE;
    else if (strcmp(kind, "paste") == 0)
        app->settings.confirm_paste = active != FALSE;
    else if (strcmp(kind, "updates") == 0)
        app->settings.check_updates = active != FALSE;
    else if (strcmp(kind, "reconnect") == 0)
        app->settings.reconnect_auto = active != FALSE;
    else
        return;
    settings_persist(app);
}

static void on_settings_back(GtkButton* b, gpointer user)
{
    (void)b;
    EcApp* app = user;
    gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "welcome");
}

void ui_settings_apply(EcApp* app)
{
    settings_persist(app);
    ec_theme_resolve(app->settings.theme, app->themes_dir, &app->theme);
    ui_apply_theme(app); /* CSS + Adwaita scheme + every live terminal */
}

static void settings_grid_label(GtkWidget* grid, int row, const char* text)
{
    GtkWidget* lbl = gtk_label_new(text);
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
    gtk_grid_attach(GTK_GRID(grid), lbl, 0, row, 1, 1);
}

void ui_settings_page_build(EcApp* app)
{
    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(root, 12);
    gtk_widget_set_margin_bottom(root, 12);
    gtk_widget_set_margin_start(root, 12);
    gtk_widget_set_margin_end(root, 12);
    GtkWidget* hdr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* back = gtk_button_new_with_label("\xe2\x86\x90 Back");
    g_signal_connect(back, "clicked", G_CALLBACK(on_settings_back), app);
    gtk_box_append(GTK_BOX(hdr), back);
    GtkWidget* title = gtk_label_new("Settings");
    gtk_widget_add_css_class(title, "title-4");
    gtk_box_append(GTK_BOX(hdr), title);
    gtk_box_append(GTK_BOX(root), hdr);

    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_widget_set_vexpand(scroll, TRUE);
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    gtk_widget_set_margin_top(grid, 12);
    int row = 0;

    /* --- appearance: theme dropdown (all builtins; custom files resolve too) --- */
    size_t ndefs = 0;
    const EcThemeDef* defs = ec_theme_builtins(&ndefs);
    const char* labels[16];
    size_t nl = 0;
    int cur_sel = -1;
    for (size_t i = 0; i < ndefs && nl < 15; i++) {
        labels[nl] = defs[i].label;
        if (strcmp(defs[i].key, app->settings.theme) == 0) cur_sel = (int)nl;
        nl++;
    }
    labels[nl] = NULL;
    GtkWidget* dd = gtk_drop_down_new_from_strings(labels);
    if (cur_sel >= 0)
        gtk_drop_down_set_selected(GTK_DROP_DOWN(dd), (guint)cur_sel);
    settings_grid_label(grid, row, "Theme");
    gtk_grid_attach(GTK_GRID(grid), dd, 1, row, 1, 1);
    gtk_widget_set_hexpand(dd, TRUE);
    g_signal_connect(dd, "notify::selected", G_CALLBACK(on_theme_selected), app);
    row++;

    /* --- terminal font --- */
    GtkWidget* fname = gtk_entry_new();
    gtk_editable_set_text(GTK_EDITABLE(fname), app->settings.font_name);
    gtk_widget_set_tooltip_text(fname, "Monospace family (e.g. monospace, JetBrains Mono)");
    settings_grid_label(grid, row, "Terminal font");
    gtk_grid_attach(GTK_GRID(grid), fname, 1, row, 1, 1);
    gtk_widget_set_hexpand(fname, TRUE);
    g_signal_connect(fname, "activate", G_CALLBACK(on_font_name_changed), app);
    g_signal_connect(fname, "changed",
                     G_CALLBACK(on_font_name_changed), app);
    row++;

    GtkWidget* fsize = gtk_spin_button_new_with_range(6, 40, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(fsize), app->settings.font_size);
    settings_grid_label(grid, row, "Terminal font size");
    gtk_grid_attach(GTK_GRID(grid), fsize, 1, row, 1, 1);
    g_signal_connect(fsize, "value-changed", G_CALLBACK(on_font_size_changed), app);
    row++;

    /* --- behavior --- */
    GtkWidget* sb = gtk_spin_button_new_with_range(100, 200000, 500);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(sb), app->settings.scrollback_lines);
    gtk_widget_set_tooltip_text(sb, "History lines kept per terminal (new terminals only)");
    settings_grid_label(grid, row, "Scrollback lines");
    gtk_grid_attach(GTK_GRID(grid), sb, 1, row, 1, 1);
    g_signal_connect(sb, "value-changed", G_CALLBACK(on_scrollback_changed), app);
    row++;

    GtkWidget* ka = gtk_spin_button_new_with_range(0, 600, 5);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ka), app->settings.keepalive_sec);
    settings_grid_label(grid, row, "Keepalive seconds (0=off)");
    gtk_grid_attach(GTK_GRID(grid), ka, 1, row, 1, 1);
    g_signal_connect(ka, "value-changed", G_CALLBACK(on_keepalive_changed), app);
    row++;

    GtkWidget* t1 = gtk_check_button_new_with_label("Copy on select");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(t1), app->settings.copy_on_select);
    g_object_set_data(G_OBJECT(t1), "ec-kind", (gpointer)"copy");
    g_signal_connect(t1, "toggled", G_CALLBACK(settings_toggle), app);
    gtk_grid_attach(GTK_GRID(grid), t1, 0, row, 2, 1);
    row++;
    GtkWidget* t2 = gtk_check_button_new_with_label("Confirm before pasting multiline/any text");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(t2), app->settings.confirm_paste);
    g_object_set_data(G_OBJECT(t2), "ec-kind", (gpointer)"paste");
    g_signal_connect(t2, "toggled", G_CALLBACK(settings_toggle), app);
    gtk_grid_attach(GTK_GRID(grid), t2, 0, row, 2, 1);
    row++;
    GtkWidget* t3 = gtk_check_button_new_with_label("Reconnect automatically after drops");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(t3), app->settings.reconnect_auto);
    g_object_set_data(G_OBJECT(t3), "ec-kind", (gpointer)"reconnect");
    g_signal_connect(t3, "toggled", G_CALLBACK(settings_toggle), app);
    gtk_grid_attach(GTK_GRID(grid), t3, 0, row, 2, 1);
    row++;
    GtkWidget* t4 = gtk_check_button_new_with_label("Check for updates (off = privacy default)");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(t4), app->settings.check_updates);
    g_object_set_data(G_OBJECT(t4), "ec-kind", (gpointer)"updates");
    g_signal_connect(t4, "toggled", G_CALLBACK(settings_toggle), app);
    gtk_grid_attach(GTK_GRID(grid), t4, 0, row, 2, 1);
    row++;

    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), grid);
    gtk_box_append(GTK_BOX(root), scroll);
    gtk_stack_add_named(GTK_STACK(app->stack), root, "settings");
}
/* ============================================================ palette */
static void palette_execute(GtkEntry* entry, gpointer user)
{
    AdwDialog* dlg = ADW_DIALOG(user);
    EcApp* app = g_object_get_data(G_OBJECT(dlg), "ec-app");
    const char* text = gtk_editable_get_text(GTK_EDITABLE(entry));
    EcTermPage* page = ui_current_term_page(app);
    if (!page) return;
    /* 1) snippet names 2) raw commands */
    for (size_t i = 0; i < app->snippets.snippets.len; i++) {
        EcSnippet* sn = app->snippets.snippets.items[i];
        if (strcmp(sn->name, text) == 0) {
            ui_term_page_run_snippet(page, sn->command);
            gtk_editable_set_text(GTK_EDITABLE(entry), "");
            adw_dialog_force_close(dlg);
            return;
        }
    }
    if (text && *text) {
        ui_term_page_run_snippet(page, text);
        gtk_editable_set_text(GTK_EDITABLE(entry), "");
        adw_dialog_force_close(dlg);
    }
}

static void palette_close(GtkButton* b, gpointer user)
{
    (void)b;
    adw_dialog_force_close(ADW_DIALOG(user));
}

/* case-insensitive substring match (portable; ASCII fold is enough here) */
static bool palette_match(const char* hay, const char* needle)
{
    if (!needle || !*needle) return true;
    if (!hay) return false;
    size_t nl = strlen(needle);
    size_t hl = strlen(hay);
    if (nl > hl) return false;
    for (size_t i = 0; i + nl <= hl; i++) {
        size_t j = 0;
        while (j < nl &&
               (tolower((unsigned char)hay[i + j]) == tolower((unsigned char)needle[j])))
            j++;
        if (j == nl) return true;
    }
    return false;
}

static void palette_populate(EcApp* app, GtkListBox* list, const char* filter)
{
    for (;;) {
        GtkListBoxRow* row = gtk_list_box_get_row_at_index(list, 0);
        if (!row) break;
        gtk_list_box_remove(list, GTK_WIDGET(row));
    }
    for (size_t i = 0; i < app->snippets.snippets.len; i++) {
        EcSnippet* sn = app->snippets.snippets.items[i];
        if (!palette_match(sn->name, filter) && !palette_match(sn->command, filter))
            continue;
        GtkWidget* roww = gtk_list_box_row_new();
        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        GtkWidget* lbl = gtk_label_new(sn->name);
        gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
        gtk_widget_set_hexpand(lbl, TRUE);
        GtkWidget* cmd = gtk_label_new(sn->command);
        gtk_label_set_xalign(GTK_LABEL(cmd), 1.0);
        gtk_label_set_ellipsize(GTK_LABEL(cmd), PANGO_ELLIPSIZE_END);
        gtk_widget_add_css_class(cmd, "muted");
        gtk_widget_add_css_class(cmd, "monospace");
        gtk_widget_set_size_request(cmd, 140, -1);
        gtk_box_append(GTK_BOX(box), lbl);
        gtk_box_append(GTK_BOX(box), cmd);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(roww), box);
        gtk_widget_set_tooltip_text(roww, sn->command);
        g_object_set_data_full(G_OBJECT(roww), "ec-snippet-name", g_strdup(sn->name), g_free);
        gtk_list_box_append(list, roww);
    }
}

static void on_palette_row_activate(GtkListBox* list, GtkListBoxRow* row, gpointer user)
{
    (void)list;
    AdwDialog* dlg = ADW_DIALOG(user);
    EcApp* app = g_object_get_data(G_OBJECT(dlg), "ec-app");
    GtkEntry* entry = g_object_get_data(G_OBJECT(dlg), "ec-entry");
    const char* name = (const char*)g_object_get_data(G_OBJECT(row), "ec-snippet-name");
    if (!app || !entry || !name) return;
    gtk_editable_set_text(GTK_EDITABLE(entry), name);
    palette_execute(entry, dlg);
}

static void on_palette_changed(GtkEntry* entry, gpointer user)
{
    AdwDialog* dlg = ADW_DIALOG(user);
    GtkListBox* list = GTK_LIST_BOX(g_object_get_data(G_OBJECT(dlg), "ec-list"));
    if (!list) return;
    palette_populate(g_object_get_data(G_OBJECT(dlg), "ec-app"), list,
                     gtk_editable_get_text(GTK_EDITABLE(entry)));
}

void ui_palette_toggle(EcApp* app)
{
    AdwDialog* dlg = adw_dialog_new();
    adw_dialog_set_title(dlg, "Command palette");
    adw_dialog_set_content_width(dlg, 520);
    adw_dialog_set_content_height(dlg, 380);
    g_object_set_data(G_OBJECT(dlg), "ec-app", app);

    GtkWidget* toolbar = adw_toolbar_view_new();
    GtkWidget* hb = adw_header_bar_new();
    GtkWidget* close = gtk_button_new_with_label("Close");
    g_signal_connect(close, "clicked", G_CALLBACK(palette_close), dlg);
    adw_header_bar_pack_end(ADW_HEADER_BAR(hb), close);
    adw_toolbar_view_add_top_bar(ADW_TOOLBAR_VIEW(toolbar), hb);

    GtkWidget* content = gtk_box_new(GTK_ORIENTATION_VERTICAL, 8);
    gtk_widget_set_margin_top(content, 12);
    gtk_widget_set_margin_bottom(content, 12);
    gtk_widget_set_margin_start(content, 12);
    gtk_widget_set_margin_end(content, 12);
    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Filter snippets or type a raw command…");
    gtk_box_append(GTK_BOX(content), entry);
    /* filterable snippet list */
    GtkWidget* list = gtk_list_box_new();
    palette_populate(app, GTK_LIST_BOX(list), NULL);
    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), list);
    gtk_widget_set_vexpand(scroll, TRUE);
    gtk_box_append(GTK_BOX(content), scroll);
    adw_toolbar_view_set_content(ADW_TOOLBAR_VIEW(toolbar), content);

    adw_dialog_set_child(dlg, toolbar);
    g_object_set_data(G_OBJECT(dlg), "ec-entry", entry);
    g_object_set_data(G_OBJECT(dlg), "ec-list", list);
    g_signal_connect(entry, "activate", G_CALLBACK(palette_execute), dlg);
    g_signal_connect(entry, "changed", G_CALLBACK(on_palette_changed), dlg);
    g_signal_connect(list, "row-activated", G_CALLBACK(on_palette_row_activate), dlg);
    adw_dialog_present(dlg, GTK_WIDGET(app->main_window));
    gtk_widget_grab_focus(entry);
}
/* ============================================================ main window */
static void notebook_page_removed(GtkNotebook* nb, GtkWidget* child, guint page_num, gpointer user)
{
    (void)nb; (void)page_num;
    /* free every pane in the tab's split group */
    ui_pane_group_dispose((EcApp*)user, child);
}

void ui_main_window_build(EcApp* app)
{
    GtkWindow* win = app->main_window;
    gtk_window_set_title(win, "Eclipse SSH");
    gtk_window_set_default_size(win, 1100, 720);

    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 0);
    gtk_window_set_child(win, root);

    /* --- toolbar --- */
    GtkWidget* toolbar = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 4);
    gtk_widget_add_css_class(toolbar, "toolbar");
    gtk_box_append(GTK_BOX(toolbar), gtk_label_new("  Eclipse SSH"));
    gtk_box_append(GTK_BOX(toolbar), make_tool_button("list-add-symbolic", "New session (Ctrl+N)",
                     G_CALLBACK(on_new_session_clicked), app));
    gtk_box_append(GTK_BOX(toolbar), make_tool_button("folder-remote-symbolic", "Transfers / SFTP",
                     G_CALLBACK(on_sftp_clicked), app));
    gtk_box_append(GTK_BOX(toolbar), make_tool_button("system-run-symbolic", "Command palette (Ctrl+Shift+P)",
                     G_CALLBACK(on_palette_clicked), app));
    gtk_box_append(GTK_BOX(toolbar), make_tool_button("tab-new-symbolic", "Split terminal right (Ctrl+Shift+E)",
                     G_CALLBACK(on_split_right_clicked), app));
    gtk_box_append(GTK_BOX(toolbar), make_tool_button("view-more-symbolic", "Split terminal down (Ctrl+Shift+O)",
                     G_CALLBACK(on_split_down_clicked), app));
    gtk_box_append(GTK_BOX(toolbar), make_tool_button("window-close-symbolic", "Close pane (Ctrl+Shift+W)",
                     G_CALLBACK(on_close_pane_clicked), app));
    GtkWidget* spacer = gtk_label_new("");
    gtk_widget_set_hexpand(spacer, TRUE);
    gtk_box_append(GTK_BOX(toolbar), spacer);
    gtk_box_append(GTK_BOX(toolbar), make_tool_button("help-about-symbolic", "About",
                     G_CALLBACK(on_about_clicked), app));
    gtk_box_append(GTK_BOX(toolbar), make_tool_button("emblem-system-symbolic", "Settings",
                     G_CALLBACK(on_settings_clicked), app));
    gtk_box_append(GTK_BOX(root), toolbar);

    /* --- body: sidebar + stack --- */
    GtkWidget* paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    GtkWidget* side_scroll = gtk_scrolled_window_new();
    app->sidebar_list = gtk_list_box_new();
    g_signal_connect(app->sidebar_list, "row-activated", G_CALLBACK(on_session_row_activate), app);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(side_scroll), app->sidebar_list);
    gtk_widget_add_css_class(side_scroll, "sidebar-box");
    gtk_widget_set_size_request(side_scroll, 220, -1);
    gtk_paned_set_start_child(GTK_PANED(paned), side_scroll);
    gtk_paned_set_position(GTK_PANED(paned), 230);

    app->stack = gtk_stack_new();
    gtk_paned_set_end_child(GTK_PANED(paned), app->stack);
    gtk_widget_set_vexpand(paned, TRUE);
    gtk_box_append(GTK_BOX(root), paned);

    /* welcome page */
    app->welcome_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 14);
    gtk_widget_set_valign(app->welcome_page, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(app->welcome_page, GTK_ALIGN_CENTER);
    gtk_widget_set_margin_bottom(app->welcome_page, 40);
    GtkWidget* wl = gtk_label_new("Welcome to Eclipse SSH\n\nPick a session on the left, or create a new one.");
    gtk_label_set_justify(GTK_LABEL(wl), GTK_JUSTIFY_CENTER);
    GtkWidget* wtitle = gtk_label_new(NULL);
    gtk_label_set_markup(GTK_LABEL(wtitle),
                         "<span size='x-large' weight='bold'>Eclipse SSH</span>");
    gtk_box_append(GTK_BOX(app->welcome_page), wtitle);
    gtk_box_append(GTK_BOX(app->welcome_page), wl);
    GtkWidget* wbtns = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    gtk_widget_set_halign(wbtns, GTK_ALIGN_CENTER);
    GtkWidget* wnew = gtk_button_new_with_label("New session");
    gtk_widget_add_css_class(wnew, "suggested-action");
    g_signal_connect(wnew, "clicked", G_CALLBACK(on_new_session_clicked), app);
    GtkWidget* wsftp = gtk_button_new_with_label("Transfers");
    g_signal_connect(wsftp, "clicked", G_CALLBACK(on_sftp_clicked), app);
    GtkWidget* wset = gtk_button_new_with_label("Settings");
    g_signal_connect(wset, "clicked", G_CALLBACK(on_settings_clicked), app);
    gtk_box_append(GTK_BOX(wbtns), wnew);
    gtk_box_append(GTK_BOX(wbtns), wsftp);
    gtk_box_append(GTK_BOX(wbtns), wset);
    gtk_box_append(GTK_BOX(app->welcome_page), wbtns);
    GtkWidget* whint = gtk_label_new("Ctrl+N new session  \xc2\xb7  Ctrl+Shift+P palette  \xc2\xb7  "
                                     "Ctrl+Shift+E/O split  \xc2\xb7  Ctrl+Shift+C/V copy/paste");
    gtk_widget_add_css_class(whint, "muted");
    gtk_label_set_justify(GTK_LABEL(whint), GTK_JUSTIFY_CENTER);
    gtk_box_append(GTK_BOX(app->welcome_page), whint);
    gtk_stack_add_named(GTK_STACK(app->stack), app->welcome_page, "welcome");

    /* terminal notebook */
    app->notebook = gtk_notebook_new();
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(app->notebook), TRUE);
    gtk_stack_add_named(GTK_STACK(app->stack), app->notebook, "terminals");
    g_signal_connect(app->notebook, "page-removed", G_CALLBACK(notebook_page_removed), app);

    /* other pages */
    fprintf(stderr, "[smoke] ui: sftp page begin\n"); fflush(stderr);
    ui_sftp_page_build(app);
    fprintf(stderr, "[smoke] ui: settings page begin\n"); fflush(stderr);
    ui_settings_page_build(app);

    /* --- status bar --- */
    GtkWidget* status = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 10);
    gtk_widget_add_css_class(status, "statusbar");
    app->status_state = gtk_label_new("Ready");
    app->status_conn = gtk_label_new("");
    gtk_label_set_xalign(GTK_LABEL(app->status_state), 0.0);
    gtk_widget_set_hexpand(app->status_state, TRUE);
    gtk_box_append(GTK_BOX(status), app->status_state);
    gtk_box_append(GTK_BOX(status), app->status_conn);
    app->statusbar = status;
    gtk_box_append(GTK_BOX(root), status);

    ui_sidebar_refresh(app);
    fprintf(stderr, "[smoke] ui: applying theme\n"); fflush(stderr);
    ui_apply_theme(app);
}
