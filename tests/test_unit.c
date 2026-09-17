/* Eclipse SSH - unit tests (minunit): config, theme, vault, sessions,
 * forwarding parser, proxy parser, glob, host keys. Pure core, no network. */
#include "ec_minunit.h"
#include "eclipse/core.h"
#include "eclipse/repos.h"
#include "eclipse/security.h"
#include "eclipse/theme.h"
#include "eclipse/proxy.h"
#include "eclipse/fwd.h"
#include "eclipse/term.h"
#include "eclipse/platform.h"

#include <stdio.h>

int tests_run = 0;

MU_TEST(test_strbuf)
{
    EcStr b;
    ec_str_init(&b);
    mu_check(ec_str_printf(&b, "%s-%d", "a", 42) == EC_OK);
    mu_assert_string_eq("a-42", b.s);
    ec_str_append(&b, "/more");
    mu_assert_string_eq("a-42/more", b.s);
    ec_str_free(&b);
}

MU_TEST(test_glob)
{
    mu_check(ec_glob_match("*.log", "access.LOG"));
    mu_check(ec_glob_match("server?.conf", "server1.conf"));
    mu_check(!ec_glob_match("*.log", "access.txt"));
    mu_check(ec_glob_match("*", "anything"));
}

MU_TEST(test_hostport)
{
    char* host = NULL;
    int port = 0;
    mu_check(ec_parse_hostport("example.com:2222", &host, &port, 22));
    mu_assert_string_eq("example.com", host);
    mu_check_int_eq(2222, port);
    free(host);
    mu_check(ec_parse_hostport("[2001:db8::1]:22", &host, &port, 22));
    mu_assert_string_eq("2001:db8::1", host);
    mu_check_int_eq(22, port);
    free(host);
    mu_check(!ec_parse_hostport("host:bad", &host, &port, 22));
}

MU_TEST(test_fwd_parse)
{
    EcFwdRule r;
    mu_check(ec_fwd_parse_spec("8080:localhost:80", EC_FWD_LOCAL, &r));
    mu_check_int_eq(8080, r.bind_port);
    mu_check_int_eq(80, r.target_port);
    mu_assert_string_eq("localhost", r.target_host);
    mu_check(ec_fwd_parse_spec("0.0.0.0:2222:10.0.0.5:22", EC_FWD_LOCAL, &r));
    mu_assert_string_eq("0.0.0.0", r.bind_host);
    mu_check_int_eq(22, r.target_port); /* "10.0.0.5:22" -> host "10.0.0.5" port 22 */
    mu_assert_string_eq("10.0.0.5", r.target_host);
    mu_check(ec_fwd_parse_spec("1080", EC_FWD_DYNAMIC, &r));
    mu_check_int_eq(1080, r.bind_port);
    mu_check(!ec_fwd_parse_spec("bad", EC_FWD_LOCAL, &r));
}

MU_TEST(test_proxy_parse)
{
    EcProxy px;
    mu_check(ec_proxy_parse("socks5://user:secret@127.0.0.1:1080", &px));
    mu_check_int_eq(EC_PROXY_SOCKS5, px.kind);
    mu_assert_string_eq("user", px.user);
    mu_assert_string_eq("secret", px.pass);
    mu_assert_string_eq("127.0.0.1", px.host);
    mu_check_int_eq(1080, px.port);
    mu_check(ec_proxy_parse("http://proxy.corp:8080", &px));
    mu_check_int_eq(EC_PROXY_HTTP, px.kind);
    mu_check_int_eq(8080, px.port);
    mu_check(!ec_proxy_parse("ftp://x", &px));
}

MU_TEST(test_settings_roundtrip)
{
    char* dir = ec_file_tmpname("/tmp/ec-test-settings");
    remove(dir);
    mu_check(ec_mkdir_p(dir));
    char* p = ec_path_join(dir, "settings.json");
    EcSettings s;
    ec_settings_defaults(&s);
    s.font_size = 17;
    s.density = 2;
    s.copy_on_select = true;
    mu_check(ec_settings_save(&s, p));
    EcSettings t;
    mu_check(ec_settings_load(&t, p));
    mu_check_int_eq(17, t.font_size);
    mu_check_int_eq(2, t.density);
    mu_check(t.copy_on_select);
    /* corrupt file -> graceful recovery (defaults) */
    mu_check(ec_file_write_atomic(p, "{not json", 9));
    mu_check(ec_settings_load(&t, p));
    mu_check_int_eq(12, t.font_size);
    free(p);
    free(dir);
}

MU_TEST(test_sessions_store)
{
    char* dir = ec_file_tmpname("/tmp/ec-test-sessions");
    remove(dir);
    mu_check(ec_mkdir_p(dir));
    char* p = ec_path_join(dir, "sessions.json");
    EcSessionStore st;
    ec_sessions_init(&st, p);
    EcSession* s = ec_sessions_add(&st, "prod");
    snprintf(s->host, sizeof s->host, "prod.example.com");
    s->port = 2222;
    snprintf(s->color, sizeof s->color, "#e5484d");
    mu_check(ec_sessions_save(&st));
    EcSessionStore st2;
    ec_sessions_init(&st2, p);
    mu_check(ec_sessions_load(&st2));
    EcSession* f = ec_sessions_find(&st2, s->id);
    mu_check(f != NULL);
    mu_assert_string_eq("prod", f->name);
    mu_check_int_eq(2222, f->port);
    mu_assert_string_eq("#e5484d", f->color);
    mu_check(ec_sessions_remove(&st2, s->id));
    mu_check(ec_sessions_find(&st2, s->id) == NULL);
    ec_sessions_free(&st);
    ec_sessions_free(&st2);
    free(p);
    free(dir);
}

MU_TEST(test_theme)
{
    EcTheme t;
    ec_theme_builtin_dark(&t);
    mu_assert_string_eq("Dark", t.name);
    mu_check(ec_theme_load_json("{\"name\":\"Mine\",\"appearance\":\"light\","
                                "\"colors\":{\"accent\":\"#00ff00\",\"radius\":\"6px\"}}", &t));
    mu_assert_string_eq("Mine", t.name);
    mu_assert_string_eq("light", t.appearance);
    mu_assert_string_eq("#00ff00", t.c.accent);
    mu_assert_string_eq("6px", t.c.radius);
    /* invalid color keeps default */
    mu_check(ec_theme_load_json("{\"appearance\":\"dark\",\"colors\":{\"accent\":\"zzz\"}}", &t));
    mu_check(t.c.accent[0] == '#' && strlen(t.c.accent) == 7);
    char* css = ec_theme_css(&t);
    mu_check(css && strstr(css, "background"));
    free(css);
}

MU_TEST(test_vault)
{
    char* dir = ec_file_tmpname("/tmp/ec-test-vault");
    remove(dir);
    mu_check(ec_mkdir_p(dir));
    char* p = ec_path_join(dir, "vault.json");
    EcVault* v = ec_vault_open(p);
    mu_check(v != NULL);
    mu_check(ec_vault_set(v, "session-1", "root", "hunter2"));
    mu_check(ec_vault_has(v, "session-1"));
    /* wipe in-memory and re-open from disk */
    ec_vault_free(v);
    v = ec_vault_open(p);
    mu_check(v != NULL);
    char *user = NULL, *pass = NULL;
    mu_check(ec_vault_get(v, "session-1", &user, &pass));
    mu_assert_string_eq("root", user);
    mu_assert_string_eq("hunter2", pass);
    free(user);
    free(pass);
    mu_check(ec_vault_delete(v, "session-1"));
    mu_check(!ec_vault_has(v, "session-1"));
    ec_vault_free(v);
    free(p);
    free(dir);
}

MU_TEST(test_hostkeys)
{
    char* dir = ec_file_tmpname("/tmp/ec-test-hk");
    remove(dir);
    mu_check(ec_mkdir_p(dir));
    char* p = ec_path_join(dir, "known_hosts");
    EcHostKeyStore* s = ec_hostkeys_open(p);
    mu_check(s != NULL);
    const char* k1 = "AAAAC3NzaC1lZDI1NTE5AAAAIExampleKeyBlob1";
    const char* k2 = "AAAAC3NzaC1lZDI1NTE5AAAAIExampleKeyBlob2";
    mu_check_int_eq(EC_HK_UNKNOWN, ec_hostkeys_check(s, "h1", 22, "ssh-ed25519", k1));
    mu_check(ec_hostkeys_accept(s, "h1", 22, "ssh-ed25519", k1));
    mu_check_int_eq(EC_HK_OK, ec_hostkeys_check(s, "h1", 22, "ssh-ed25519", k1));
    mu_check_int_eq(EC_HK_CHANGED, ec_hostkeys_check(s, "h1", 22, "ssh-ed25519", k2));
    mu_check_int_eq(EC_HK_UNKNOWN, ec_hostkeys_check(s, "h2", 2222, "ssh-ed25519", k1));
    mu_check(ec_hostkeys_accept(s, "h1", 22, "ssh-ed25519", k2)); /* explicit change */
    mu_check_int_eq(EC_HK_OK, ec_hostkeys_check(s, "h1", 22, "ssh-ed25519", k2));
    mu_check(ec_hostkeys_remove(s, "h1", 22));
    mu_check_int_eq(EC_HK_UNKNOWN, ec_hostkeys_check(s, "h1", 22, "ssh-ed25519", k2));
    ec_hostkeys_free(s);
    free(p);
    free(dir);
}

MU_TEST(test_term_scrollback)
{
    EcTerm* t = ec_term_new(20, 5, 100);
    mu_check(t != NULL);
    const char* lines = "l1\r\nl2\r\nl3\r\nl4\r\nl5\r\nl6\r\nl7\r\n";
    ec_term_input(t, lines, strlen(lines));
    mu_check_int_eq(3, ec_term_scrollback_len(t)); /* 7 CRLF-terminated lines on 5 rows -> 3 pushed */
    ec_term_scroll_bottom(t);
    mu_check_int_eq(0, ec_term_view_offset(t));
    ec_term_scroll(t, 5);
    mu_check_int_eq(3, ec_term_view_offset(t)); /* capped at sb_len */
    ec_term_free(t);
}

int main(void)
{
    MU_RUN_TEST(test_strbuf);
    MU_RUN_TEST(test_glob);
    MU_RUN_TEST(test_hostport);
    MU_RUN_TEST(test_fwd_parse);
    MU_RUN_TEST(test_proxy_parse);
    MU_RUN_TEST(test_settings_roundtrip);
    MU_RUN_TEST(test_sessions_store);
    MU_RUN_TEST(test_theme);
    MU_RUN_TEST(test_vault);
    MU_RUN_TEST(test_hostkeys);
    MU_RUN_TEST(test_term_scrollback);
    MU_REPORT_SUMMARY();
    return minunit_fail == 0 ? 0 : 1;
}
