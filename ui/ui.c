/* Eclipse SSH - GTK4 UI: main window, sidebar, tabs, dialogs, SFTP page,
 * settings, command palette. One translation unit keeps cross-widget wiring
 * explicit; see eclipse/ui.h for the public surface.
 */
#include "eclipse/ui.h"
#include "eclipse/term_page.h"
#include "eclipse/platform.h"

#include <stdio.h>

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
}

/* ============================================================ statusbar */
static gboolean sb_idle(gpointer user)
{
    char** parts = user;
    gtk_label_set_text(GTK_LABEL(parts[0]), parts[1] ? parts[1] : "");
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

static void activate_session(EcApp* app, const char* id)
{
    EcSession* profile = ec_sessions_find(&app->sessions, id);
    if (!profile) return;
    /* one tab per session instance; reconnecting reuses the page if closed */
    EcTermPage* page = ui_term_page_new(app, profile);
    if (!page) return;
    ec_vec_push(&app->live, page);
    GtkWidget* w = ui_term_page_widget(page);
    gtk_notebook_append_page(GTK_NOTEBOOK(app->notebook), w, ui_term_page_tab_label(page));
    gtk_notebook_set_current_page(GTK_NOTEBOOK(app->notebook),
                                  gtk_notebook_page_num(GTK_NOTEBOOK(app->notebook), w));
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
    for (size_t i = 0; i < app->sessions.sessions.len; i++) {
        EcSession* s = app->sessions.sessions.items[i];
        GtkWidget* box = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
        GtkWidget* dot = gtk_label_new("");
        gtk_widget_add_css_class(dot, "session-dot");
        uint8_t r = 0x8a, g = 0x8a, b = 0x8a;
        sscanf(s->color, "#%02hhx%02hhx%02hhx", &r, &g, &b);
        char* css = g_strdup_printf("label.session-dot { background: #%02x%02x%02x; }", r, g, b);
        GtkCssProvider* prov = gtk_css_provider_new();
        gtk_css_provider_load_from_string(prov, css);
        gtk_style_context_add_provider(gtk_widget_get_style_context(dot),
                                       GTK_STYLE_PROVIDER(prov), GTK_STYLE_PROVIDER_PRIORITY_APPLICATION);
        g_object_unref(prov);
        g_free(css);
        char label[256];
        snprintf(label, sizeof label, "%s%s", s->name, s->group[0] ? "" : "");
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
}

/* ============================================================ host key dialog */
typedef struct {
    EcApp* app;
    EcServerKey key;
    EcHostKeyStatus st;
    char host[256];
    int port;
    int* answer;
    GtkDialog* dlg;
    GMainLoop* loop;
} HKCtx;

static void hk_respond(GtkDialog* dlg, int response, gpointer user)
{
    HKCtx* ctx = user;
    if (ctx->answer) {
        if (response == GTK_RESPONSE_YES)
            *ctx->answer = 2; /* accept + store */
        else if (response == GTK_RESPONSE_OK)
            *ctx->answer = 1; /* accept once */
        else
            *ctx->answer = 0;
    }
    (void)dlg;
    gtk_window_destroy(GTK_WINDOW(ctx->dlg));
    if (ctx->loop) g_main_loop_quit(ctx->loop);
}

static gboolean hk_show_idle(gpointer user)
{
    HKCtx* ctx = user;
    const char* kind = ctx->st == EC_HK_CHANGED
        ? "HOST KEY CHANGED - possible man-in-the-middle attack!"
        : "Unknown host - first connection";
    char fp[256] = "-";
    if (ctx->key.fingerprint) snprintf(fp, sizeof fp, "%s", ctx->key.fingerprint);
    char msg[1024];
    snprintf(msg, sizeof msg,
             "%s\n\nHost: %s:%d\nKey type: %s\nFingerprint: %s\n\nAccept this host key?",
             kind, ctx->host, ctx->port, ctx->key.type ? ctx->key.type : "?", fp);
    GtkWidget* dlg = gtk_message_dialog_new(ctx->app->main_window, GTK_DIALOG_MODAL,
                                            ctx->st == EC_HK_CHANGED ? GTK_MESSAGE_WARNING : GTK_MESSAGE_QUESTION,
                                            GTK_BUTTONS_NONE, "%s", msg);
    gtk_dialog_add_button(GTK_DIALOG(dlg), "Reject", GTK_RESPONSE_CANCEL);
    gtk_dialog_add_button(GTK_DIALOG(dlg), "Accept once", GTK_RESPONSE_OK);
    if (ctx->st != EC_HK_CHANGED)
        gtk_dialog_add_button(GTK_DIALOG(dlg), "Trust and save", GTK_RESPONSE_YES);
    ctx->dlg = GTK_DIALOG(dlg);
    g_signal_connect(dlg, "response", G_CALLBACK(hk_respond), ctx);
    gtk_widget_set_visible(dlg, TRUE);
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
    EcApp* app;
    char title[128];
    void (*done)(const char* pass, void* user);
    void* user;
} PwCtx;

static void pw_respond(GtkDialog* dlg, int response, gpointer user)
{
    PwCtx* ctx = user;
    const char* pass = NULL;
    if (response == GTK_RESPONSE_OK) {
        GtkEntry* e = g_object_get_data(G_OBJECT(dlg), "ec-entry");
        pass = gtk_editable_get_text(GTK_EDITABLE(e));
    }
    ctx->done(pass, ctx->user);
    gtk_window_destroy(GTK_WINDOW(dlg));
    g_free(ctx);
}

void ui_prompt_password(EcApp* app, const char* title, void (*done)(const char* pass, void* user), void* user)
{
    PwCtx* ctx = g_new0(PwCtx, 1);
    ctx->app = app;
    snprintf(ctx->title, sizeof ctx->title, "%s", title);
    ctx->done = done;
    ctx->user = user;
    GtkWidget* dlg = gtk_dialog_new_with_buttons(title, app->main_window, GTK_DIALOG_MODAL,
                                                 "_Cancel", GTK_RESPONSE_CANCEL,
                                                 "_OK", GTK_RESPONSE_OK, NULL);
    GtkWidget* area = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    GtkWidget* entry = gtk_password_entry_new();
    gtk_password_entry_set_show_peek_icon(GTK_PASSWORD_ENTRY(entry), TRUE); /* spec #32 */
    gtk_box_append(GTK_BOX(area), gtk_label_new("Password:"));
    gtk_box_append(GTK_BOX(area), entry);
    g_object_set_data(G_OBJECT(dlg), "ec-entry", entry);
    g_signal_connect(dlg, "response", G_CALLBACK(pw_respond), ctx);
    gtk_widget_set_visible(dlg, TRUE);
}

/* ============================================================ connect dialog */
static void connect_dialog_respond(GtkDialog* dlg, int response, gpointer user)
{
    EcApp* app = user;
    if (response == GTK_RESPONSE_OK) {
        EcSession* existing = g_object_get_data(G_OBJECT(dlg), "ec-existing");
        EcSession* s = existing ? existing : ec_sessions_add(&app->sessions, "New session");
        GtkEntry* e_name = g_object_get_data(G_OBJECT(dlg), "e-name");
        GtkEntry* e_host = g_object_get_data(G_OBJECT(dlg), "e-host");
        GtkEntry* e_port = g_object_get_data(G_OBJECT(dlg), "e-port");
        GtkEntry* e_user = g_object_get_data(G_OBJECT(dlg), "e-user");
        GtkComboBoxText* c_auth = g_object_get_data(G_OBJECT(dlg), "c-auth");
        GtkEntry* e_pass = g_object_get_data(G_OBJECT(dlg), "e-pass");
        GtkEntry* e_key = g_object_get_data(G_OBJECT(dlg), "e-key");
        GtkComboBoxText* c_color = g_object_get_data(G_OBJECT(dlg), "c-color");
        GtkTextView* t_init = g_object_get_data(G_OBJECT(dlg), "t-init");
        snprintf(s->name, sizeof s->name, "%s", gtk_editable_get_text(GTK_EDITABLE(e_name)));
        snprintf(s->host, sizeof s->host, "%s", gtk_editable_get_text(GTK_EDITABLE(e_host)));
        s->port = atoi(gtk_editable_get_text(GTK_EDITABLE(e_port)));
        if (!ec_valid_port(s->port)) s->port = 22;
        snprintf(s->username, sizeof s->username, "%s", gtk_editable_get_text(GTK_EDITABLE(e_user)));
        int am = gtk_combo_box_get_active(GTK_COMBO_BOX(c_auth));
        s->auth_mode = am == 0 ? EC_AUTH_PASSWORD : am == 1 ? EC_AUTH_KEY : EC_AUTH_AGENT;
        snprintf(s->key_path, sizeof s->key_path, "%s", gtk_editable_get_text(GTK_EDITABLE(e_key)));
        const char* color = gtk_combo_box_text_get_active_text(c_color);
        snprintf(s->color, sizeof s->color, "%s", color ? color : "#4f8cff");
        g_free((gpointer)color);
        GtkTextBuffer* tb = gtk_text_view_get_buffer(t_init);
        GtkTextIter a, b;
        gtk_text_buffer_get_bounds(tb, &a, &b);
        char* txt = gtk_text_buffer_get_text(tb, &a, &b, FALSE);
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
    }
    gtk_window_destroy(GTK_WINDOW(dlg));
}

void ui_show_connect_dialog(EcApp* app, EcSession* existing)
{
    GtkWidget* dlg = gtk_dialog_new_with_buttons(existing ? "Edit session" : "New session",
                                                 app->main_window, GTK_DIALOG_MODAL,
                                                 "_Cancel", GTK_RESPONSE_CANCEL,
                                                 "C_onnect", GTK_RESPONSE_OK, NULL);
    gtk_window_set_default_size(GTK_WINDOW(dlg), 460, 420);
    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 8);
    GtkWidget* area = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    gtk_box_append(GTK_BOX(area), grid);
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
    GtkWidget* c_auth = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(c_auth), "Password");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(c_auth), "Private key");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(c_auth), "Agent");
    gtk_combo_box_set_active(GTK_COMBO_BOX(c_auth), 0);
    GtkWidget* e_pass = gtk_password_entry_new();
    gtk_password_entry_set_show_peek_icon(GTK_PASSWORD_ENTRY(e_pass), TRUE);
    GtkWidget* e_key = gtk_entry_new();
    gtk_widget_set_tooltip_text(e_key, "Path to OpenSSH private key");
    GtkWidget* c_color = gtk_combo_box_text_new();
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(c_color), "#e5484d");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(c_color), "#e5b567");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(c_color), "#46a758");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(c_color), "#4f8cff");
    gtk_combo_box_text_append_text(GTK_COMBO_BOX_TEXT(c_color), "#9d59e5");
    gtk_combo_box_set_active(GTK_COMBO_BOX(c_color), 3);
    GtkWidget* scroll = gtk_scrolled_window_new();
    GtkWidget* t_init = gtk_text_view_new();
    gtk_text_view_set_wrap_mode(GTK_TEXT_VIEW(t_init), GTK_WRAP_WORD_CHAR);
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), t_init);
    gtk_widget_set_size_request(scroll, -1, 80);
    ADD_ROW("Name", e_name);
    ADD_ROW("Host", e_host);
    ADD_ROW("Port", e_port);
    ADD_ROW("User", e_user);
    ADD_ROW("Auth", c_auth);
    ADD_ROW("Password", e_pass);
    ADD_ROW("Key path", e_key);
    ADD_ROW("Color tag", c_color);
    ADD_ROW("Startup commands", scroll);
#undef ADD_ROW
    if (existing) {
        gtk_editable_set_text(GTK_EDITABLE(e_name), existing->name);
        gtk_editable_set_text(GTK_EDITABLE(e_host), existing->host);
        char port[16];
        snprintf(port, sizeof port, "%d", existing->port);
        gtk_editable_set_text(GTK_EDITABLE(e_port), port);
        gtk_editable_set_text(GTK_EDITABLE(e_user), existing->username);
        gtk_combo_box_set_active(GTK_COMBO_BOX(c_auth), (int)existing->auth_mode);
        gtk_editable_set_text(GTK_EDITABLE(e_key), existing->key_path);
        if (existing->color[0]) {
            GtkStringList* dummy = NULL;
            (void)dummy;
            /* select matching color if present */
            for (int i = 0; i < 5; i++) {
                const char* cand = gtk_combo_box_text_get_active_text(GTK_COMBO_BOX_TEXT(c_color));
                g_free((gpointer)cand);
                break;
            }
        }
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
    g_object_set_data(G_OBJECT(dlg), "ec-existing", existing);
    g_signal_connect(dlg, "response", G_CALLBACK(connect_dialog_respond), app);
    gtk_widget_set_visible(dlg, TRUE);
}

/* ============================================================ error/about */
void ui_show_error(EcApp* app, const char* title, const char* message)
{
    GtkWidget* dlg = gtk_message_dialog_new(app->main_window, GTK_DIALOG_MODAL,
                                            GTK_MESSAGE_ERROR, GTK_BUTTONS_CLOSE,
                                            "%s", message ? message : title);
    gtk_window_set_title(GTK_WINDOW(dlg), title);
    g_signal_connect(dlg, "response", G_CALLBACK(gtk_window_destroy), NULL);
    gtk_widget_set_visible(dlg, TRUE);
}

void ui_show_about(EcApp* app)
{
    gtk_show_about_dialog(app->main_window,
                          "program-name", "Eclipse SSH",
                          "version", "1.0.0",
                          "comments", "Native SSH/SFTP client for Windows and Linux",
                          "license-type", GTK_LICENSE_MIT_X11,
                          NULL);
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
    EcVec transfers;
} SftpPage;

static SftpPage* g_sftp; /* single SFTP page instance */

static void local_list(const char* dir, GtkWidget* view)
{
    GtkListBox* list = GTK_LIST_BOX(view);
    for (;;) {
        GtkListBoxRow* row = gtk_list_box_get_row_at_index(list, 0);
        if (!row) break;
        gtk_list_box_remove(list, GTK_WIDGET(row));
    }
    EcDirIter* it = NULL;
    if (!ec_dir_iter_open(&it, dir)) return;
    char* name;
    while ((name = ec_dir_iter_next(it)) != NULL) {
        GtkWidget* row = gtk_list_box_row_new();
        GtkWidget* lbl = gtk_label_new(name);
        gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), lbl);
        gtk_list_box_append(list, row);
        free(name);
    }
    ec_dir_iter_close(it);
}

static gboolean sftp_refresh_idle(gpointer user)
{
    SftpPage* pg = user;
    if (pg->live && pg->live->sftp && pg->remote_cwd[0]) {
        size_t n = 0;
        char* err = NULL;
        EcSftpEntry* entries = ec_sftp_list(pg->live->sftp, pg->remote_cwd, &n, &err);
        GtkListBox* list = GTK_LIST_BOX(pg->remote_view);
        for (;;) {
            GtkListBoxRow* row = gtk_list_box_get_row_at_index(list, 0);
            if (!row) break;
            gtk_list_box_remove(list, GTK_WIDGET(row));
        }
        if (entries) {
            for (size_t i = 0; i < n; i++) {
                GtkWidget* row = gtk_list_box_row_new();
                GtkWidget* lbl = gtk_label_new(entries[i].name);
                gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
                gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), lbl);
                gtk_list_box_append(list, row);
            }
            ec_sftp_entries_free(entries, n);
        }
        free(err);
    }
    return G_SOURCE_REMOVE;
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
    GtkWidget* bar = gtk_progress_bar_new();
    gtk_widget_add_css_class(bar, "transfer-bar");
    g_object_set_data(G_OBJECT(box), "bar", bar);
    gtk_box_append(GTK_BOX(box), lbl);
    gtk_box_append(GTK_BOX(box), bar);
    GtkWidget* row = gtk_list_box_row_new();
    gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), box);
    g_object_set_data(G_OBJECT(row), "ec-xfer", t);
    gtk_list_box_append(GTK_LIST_BOX(pg->queue_list), row);
}

static void on_sftp_upload_clicked(GtkButton* b, gpointer user)
{
    (void)b;
    SftpPage* pg = user;
    if (!pg->live || !pg->live->sftp) {
        ui_show_error(pg->app, "SFTP", "Connect a session first, then reopen the Transfers tab.");
        return;
    }
    /* enqueue selected local rows (single selection via focus row) */
    GtkListBoxRow* row = gtk_list_box_get_selected_row(GTK_LIST_BOX(pg->local_view));
    if (!row) return;
    GtkWidget* lbl = gtk_list_box_row_get_child(row);
    const char* name = gtk_label_get_text(GTK_LABEL(lbl));
    char* local = ec_path_join(pg->local_cwd, name);
    char* remote = ec_path_join(pg->remote_cwd, name);
    EcTransfer* t = ec_tq_add(&pg->live->transfer_queue, EC_TR_UPLOAD, pg->live->ssh, pg->live->sftp,
              local, remote, transfer_progress_cb, pg);
    if (t) tq_add_row(pg, t);
    free(local);
    free(remote);
}

static void on_sftp_download_clicked(GtkButton* b, gpointer user)
{
    (void)b;
    SftpPage* pg = user;
    if (!pg->live || !pg->live->sftp) return;
    GtkListBoxRow* row = gtk_list_box_get_selected_row(GTK_LIST_BOX(pg->remote_view));
    if (!row) return;
    GtkWidget* lbl = gtk_list_box_row_get_child(row);
    const char* name = gtk_label_get_text(GTK_LABEL(lbl));
    char* local = ec_path_join(pg->local_cwd, name);
    char* remote = ec_path_join(pg->remote_cwd, name);
    EcTransfer* t = ec_tq_add(&pg->live->transfer_queue, EC_TR_DOWNLOAD, pg->live->ssh, pg->live->sftp,
              local, remote, transfer_progress_cb, pg);
    if (t) tq_add_row(pg, t);
    free(local);
    free(remote);
}

/* transfers pump + queue UI tick */
static gboolean sftp_tick(gpointer user)
{
    SftpPage* pg = user;
    if (pg->live)
        ec_tq_pump(&pg->live->transfer_queue);
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
    gtk_box_append(GTK_BOX(toolbar), make_tool_button("go-up-symbolic", "Upload selected",
                     G_CALLBACK(on_sftp_upload_clicked), pg));
    gtk_box_append(GTK_BOX(toolbar), make_tool_button("go-down-symbolic", "Download selected",
                     G_CALLBACK(on_sftp_download_clicked), pg));
    gtk_box_append(GTK_BOX(root), toolbar);

    GtkWidget* paned = gtk_paned_new(GTK_ORIENTATION_HORIZONTAL);
    /* local pane */
    GtkWidget* local_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    pg->local_cwd_label = gtk_label_new(pg->local_cwd);
    gtk_label_set_xalign(GTK_LABEL(pg->local_cwd_label), 0.0);
    gtk_box_append(GTK_BOX(local_box), pg->local_cwd_label);
    GtkWidget* local_scroll = gtk_scrolled_window_new();
    pg->local_view = gtk_list_box_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(local_scroll), pg->local_view);
    gtk_widget_set_vexpand(local_scroll, TRUE);
    gtk_box_append(GTK_BOX(local_box), local_scroll);
    /* remote pane */
    GtkWidget* remote_box = gtk_box_new(GTK_ORIENTATION_VERTICAL, 4);
    pg->remote_cwd_label = gtk_label_new("(not connected)");
    gtk_label_set_xalign(GTK_LABEL(pg->remote_cwd_label), 0.0);
    gtk_box_append(GTK_BOX(remote_box), pg->remote_cwd_label);
    GtkWidget* remote_scroll = gtk_scrolled_window_new();
    pg->remote_view = gtk_list_box_new();
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
static void on_theme_selected(GtkDropDown* dd, GParamSpec* ps, gpointer user)
{
    (void)ps; (void)dd;
    EcApp* app = user;
    ui_settings_apply(app);
}

static void settings_toggle(GtkCheckButton* btn, gpointer user)
{
    (void)btn; (void)user;
    ui_settings_apply((EcApp*)user);
}

static void on_settings_back(GtkButton* b, gpointer user)
{
    (void)b;
    EcApp* app = user;
    gtk_stack_set_visible_child_name(GTK_STACK(app->stack), "welcome");
}

void ui_settings_apply(EcApp* app)
{
    /* persisted by the settings page controls before this is invoked */
    ec_settings_save(&app->settings, ec_path_join(app->config_dir, "settings.json"));
    ui_apply_theme(app);
}

void ui_settings_page_build(EcApp* app)
{
    GtkWidget* root = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_margin_top(root, 12);
    gtk_widget_set_margin_start(root, 12);
    gtk_widget_set_margin_end(root, 12);
    GtkWidget* hdr = gtk_box_new(GTK_ORIENTATION_HORIZONTAL, 8);
    GtkWidget* back = gtk_button_new_with_label("← Back");
    g_signal_connect(back, "clicked", G_CALLBACK(on_settings_back), app);
    gtk_box_append(GTK_BOX(hdr), back);
    gtk_box_append(GTK_BOX(hdr), gtk_label_new("Settings"));
    gtk_box_append(GTK_BOX(root), hdr);

    GtkWidget* grid = gtk_grid_new();
    gtk_grid_set_row_spacing(GTK_GRID(grid), 8);
    gtk_grid_set_column_spacing(GTK_GRID(grid), 12);
    int row = 0;
    GtkWidget* lbl;

    /* theme dropdown */
    GtkWidget* dd = gtk_drop_down_from_strings((const char* const[]){ "light", "dark", NULL });
    if (strcmp(app->settings.theme, "dark") == 0) gtk_drop_down_set_selected(GTK_DROP_DOWN(dd), 1);
    lbl = gtk_label_new("Theme");
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
    gtk_grid_attach(GTK_GRID(grid), lbl, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), dd, 1, row, 1, 1);
    g_signal_connect(dd, "notify::selected", G_CALLBACK(on_theme_selected), app);
    row++;

    /* font size */
    GtkWidget* spin = gtk_spin_button_new_with_range(6, 40, 1);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(spin), app->settings.font_size);
    g_object_set_data(G_OBJECT(spin), "kind", (gpointer)"font");
    lbl = gtk_label_new("Terminal font size");
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
    gtk_grid_attach(GTK_GRID(grid), lbl, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), spin, 1, row, 1, 1);
    row++;

    /* keepalive */
    GtkWidget* ka = gtk_spin_button_new_with_range(0, 600, 5);
    gtk_spin_button_set_value(GTK_SPIN_BUTTON(ka), app->settings.keepalive_sec);
    lbl = gtk_label_new("Keepalive seconds (0=off)");
    gtk_label_set_xalign(GTK_LABEL(lbl), 0.0);
    gtk_grid_attach(GTK_GRID(grid), lbl, 0, row, 1, 1);
    gtk_grid_attach(GTK_GRID(grid), ka, 1, row, 1, 1);
    row++;

    /* toggles */
    GtkWidget* t1 = gtk_check_button_new_with_label("Copy on select");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(t1), app->settings.copy_on_select);
    g_signal_connect(t1, "toggled", G_CALLBACK(settings_toggle), app);
    gtk_grid_attach(GTK_GRID(grid), t1, 0, row, 2, 1);
    row++;
    GtkWidget* t2 = gtk_check_button_new_with_label("Confirm paste");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(t2), app->settings.confirm_paste);
    g_signal_connect(t2, "toggled", G_CALLBACK(settings_toggle), app);
    gtk_grid_attach(GTK_GRID(grid), t2, 0, row, 2, 1);
    row++;
    GtkWidget* t3 = gtk_check_button_new_with_label("Check for updates (off = privacy default)");
    gtk_check_button_set_active(GTK_CHECK_BUTTON(t3), app->settings.check_updates);
    gtk_grid_attach(GTK_GRID(grid), t3, 0, row, 2, 1);
    row++;

    gtk_box_append(GTK_BOX(root), grid);
    gtk_stack_add_named(GTK_STACK(app->stack), root, "settings");
}

/* ============================================================ palette */
static void palette_execute(GtkEntry* entry, gpointer user)
{
    EcApp* app = user;
    const char* text = gtk_editable_get_text(GTK_EDITABLE(entry));
    /* 1) snippet names 2) raw commands */
    for (size_t i = 0; i < app->snippets.snippets.len; i++) {
        EcSnippet* sn = app->snippets.snippets.items[i];
        if (strcmp(sn->name, text) == 0) {
            guint n = gtk_notebook_get_n_pages(GTK_NOTEBOOK(app->notebook));
            int cur = gtk_notebook_get_current_page(GTK_NOTEBOOK(app->notebook));
            if (cur >= 0 && (guint)cur < n) {
                for (size_t j = 0; j < app->live.len; j++) {
                    EcTermPage* p = app->live.items[j];
                    if (ui_term_page_widget(p) ==
                        gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), cur)) {
                        ui_term_page_run_snippet(p, sn->command);
                        break;
                    }
                }
            }
            gtk_editable_set_text(GTK_EDITABLE(entry), "");
            return;
        }
    }
    guint n2 = gtk_notebook_get_n_pages(GTK_NOTEBOOK(app->notebook));
    int cur2 = gtk_notebook_get_current_page(GTK_NOTEBOOK(app->notebook));
    if (cur2 >= 0 && text && *text) {
        for (size_t j = 0; j < app->live.len; j++) {
            EcTermPage* p = app->live.items[j];
            if (ui_term_page_widget(p) ==
                gtk_notebook_get_nth_page(GTK_NOTEBOOK(app->notebook), cur2)) {
                ui_term_page_run_snippet(p, text);
                break;
            }
        }
        gtk_editable_set_text(GTK_EDITABLE(entry), "");
    }
}

void ui_palette_toggle(EcApp* app)
{
    GtkWidget* dlg = gtk_dialog_new_with_buttons("Command palette / run snippet",
                                                 app->main_window,
                                                 GTK_DIALOG_MODAL | GTK_DIALOG_DESTROY_WITH_PARENT,
                                                 "_Close", GTK_RESPONSE_CLOSE, NULL);
    GtkWidget* area = gtk_dialog_get_content_area(GTK_DIALOG(dlg));
    GtkWidget* entry = gtk_entry_new();
    gtk_entry_set_placeholder_text(GTK_ENTRY(entry), "Type a snippet name or a raw command…");
    gtk_box_append(GTK_BOX(area), entry);
    /* quick list */
    GtkWidget* list = gtk_list_box_new();
    for (size_t i = 0; i < app->snippets.snippets.len && i < 10; i++) {
        EcSnippet* sn = app->snippets.snippets.items[i];
        GtkWidget* row = gtk_list_box_row_new();
        gtk_list_box_row_set_child(GTK_LIST_BOX_ROW(row), gtk_label_new(sn->name));
        gtk_list_box_append(GTK_LIST_BOX(list), row);
    }
    GtkWidget* scroll = gtk_scrolled_window_new();
    gtk_scrolled_window_set_child(GTK_SCROLLED_WINDOW(scroll), list);
    gtk_widget_set_size_request(scroll, 420, 160);
    gtk_box_append(GTK_BOX(area), scroll);
    g_signal_connect(entry, "activate", G_CALLBACK(palette_execute), app);
    g_signal_connect(dlg, "response", G_CALLBACK(gtk_window_destroy), NULL);
    gtk_widget_set_visible(dlg, TRUE);
    gtk_widget_grab_focus(entry);
}

/* ============================================================ main window */
static void notebook_page_removed(GtkNotebook* nb, GtkWidget* child, guint page_num, gpointer user)
{
    (void)nb; (void)page_num;
    EcApp* app = user;
    for (size_t i = 0; i < app->live.len; i++) {
        EcTermPage* p = app->live.items[i];
        if (ui_term_page_widget(p) == child) {
            ec_vec_remove_at(&app->live, i);
            ui_term_page_free(p);
            break;
        }
    }
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
    app->welcome_page = gtk_box_new(GTK_ORIENTATION_VERTICAL, 10);
    gtk_widget_set_valign(app->welcome_page, GTK_ALIGN_CENTER);
    gtk_widget_set_halign(app->welcome_page, GTK_ALIGN_CENTER);
    GtkWidget* wl = gtk_label_new("Welcome to Eclipse SSH\n\nPick a session on the left, or create a new one.");
    gtk_label_set_justify(GTK_LABEL(wl), GTK_JUSTIFY_CENTER);
    gtk_box_append(GTK_BOX(app->welcome_page), wl);
    gtk_stack_add_named(GTK_STACK(app->stack), app->welcome_page, "welcome");

    /* terminal notebook */
    app->notebook = gtk_notebook_new();
    gtk_notebook_set_scrollable(GTK_NOTEBOOK(app->notebook), TRUE);
    gtk_stack_add_named(GTK_STACK(app->stack), app->notebook, "terminals");
    g_signal_connect(app->notebook, "page-removed", G_CALLBACK(notebook_page_removed), app);

    /* other pages */
    ui_sftp_page_build(app);
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
    ui_apply_theme(app);
}
