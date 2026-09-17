/* Eclipse SSH - GUI entry point (GTK4). */
#include <gtk/gtk.h>
#include "eclipse/ui.h"
#include "eclipse/platform.h"

static EcApp g_app;

static void app_dirs_init(EcApp* app)
{
    char* d = ec_app_config_dir();
    snprintf(app->config_dir, sizeof app->config_dir, "%s", d ? d : ".");
    free(d);
    d = ec_app_data_dir();
    snprintf(app->data_dir, sizeof app->data_dir, "%s", d ? d : ".");
    free(d);
    ec_mkdir_p(app->config_dir);
    ec_mkdir_p(app->data_dir);
    char* t = ec_path_join(app->data_dir, "themes");
    snprintf(app->themes_dir, sizeof app->themes_dir, "%s", t);
    free(t);
    ec_mkdir_p(app->themes_dir);
    t = ec_path_join(app->data_dir, "keys");
    snprintf(app->keys_dir, sizeof app->keys_dir, "%s", t);
    free(t);
    ec_mkdir_p(app->keys_dir);
}

static void app_files_init(EcApp* app)
{
    char* p;
    p = ec_path_join(app->config_dir, "settings.json");
    ec_settings_load(&app->settings, p);
    free(p);
    p = ec_path_join(app->config_dir, "sessions.json");
    ec_sessions_init(&app->sessions, p);
    ec_sessions_load(&app->sessions);
    free(p);
    p = ec_path_join(app->config_dir, "snippets.json");
    ec_snippets_init(&app->snippets, p);
    ec_snippets_load(&app->snippets);
    free(p);
    p = ec_path_join(app->config_dir, "shortcuts.json");
    ec_shortcuts_init(&app->shortcuts, p);
    ec_shortcuts_load(&app->shortcuts);
    free(p);
    p = ec_path_join(app->data_dir, "vault.json");
    app->vault = ec_vault_open(p);
    free(p);
    p = ec_path_join(app->data_dir, "known_hosts");
    app->hostkeys = ec_hostkeys_open(p);
    free(p);
}

static void theme_init(EcApp* app)
{
    if (strcmp(app->settings.theme, "light") == 0)
        ec_theme_builtin_light(&app->theme);
    else
        ec_theme_builtin_dark(&app->theme);
    /* custom theme file lookup by name */
    size_t n = 0;
    char** files = ec_theme_list(app->themes_dir, &n);
    for (size_t i = 0; i < n; i++) {
        EcTheme t;
        if (ec_theme_load_file(files[i], &t) && strcmp(t.name, app->settings.theme) == 0)
            app->theme = t;
    }
    for (size_t i = 0; i < n; i++) free(files[i]);
    free(files);
}

static void on_activate(GtkApplication* gtk_app, gpointer user)
{
    EcApp* app = user;
    /* single instance handshake: 2nd instance exits 0 and pokes 1st */
    if (ec_single_instance_probe("gui")) {
        g_application_quit(G_APPLICATION(gtk_app));
        return;
    }
    const char* whoami = g_get_application_name();
    (void)whoami;
    app->gtk_app = gtk_app;
    app_dirs_init(app);
    app_files_init(app);
    theme_init(app);

    GtkWindow* win = GTK_WINDOW(gtk_application_window_new(gtk_app));
    app->main_window = win;
    ui_main_window_build(app);

    /* global accelerators (overridable via shortcuts.json later) */
    const char* accels_new[] = { "<Ctrl>n", NULL };
    const char* accels_palette[] = { "<Ctrl><Shift>p", NULL };
    gtk_application_set_accels_for_action(gtk_app, "app.new-session", accels_new);
    gtk_application_set_accels_for_action(gtk_app, "app.palette", accels_palette);

    /* first poke target */
    ec_single_instance_bind("gui", NULL, NULL);

    gtk_window_present(win);
}

static void action_new_session(GSimpleAction* a, GVariant* v, gpointer user)
{
    (void)a; (void)v;
    ui_show_connect_dialog((EcApp*)user, NULL);
}

static void action_palette(GSimpleAction* a, GVariant* v, gpointer user)
{
    (void)a; (void)v;
    ui_palette_toggle((EcApp*)user);
}

int main(int argc, char** argv)
{
    /* ssh:// deep links: eclipse-ssh ssh://user@host:port (spec #53) */
    for (int i = 1; i < argc; i++) {
        if (g_str_has_prefix(argv[i], "ssh://")) {
            /* pre-seed quick-connect profile name */
            g_setenv("EC_DEEP_LINK", argv[i], TRUE);
            break;
        }
    }
    memset(&g_app, 0, sizeof g_app);
    GtkApplication* gtk_app = gtk_application_new("io.github.SecretArrow.EclipseSSH", G_APPLICATION_NON_UNIQUE);
    static const GActionEntry actions[] = {
        { "new-session", action_new_session, NULL, NULL, NULL },
        { "palette", action_palette, NULL, NULL, NULL },
    };
    g_action_map_add_action_entries(G_ACTION_MAP(gtk_app), actions, G_N_ELEMENTS(actions), &g_app);
    g_signal_connect(gtk_app, "activate", G_CALLBACK(on_activate), &g_app);
    int rc = g_application_run(G_APPLICATION(gtk_app), argc, argv);
    g_object_unref(gtk_app);
    return rc;
}
