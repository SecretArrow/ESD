/* Eclipse SSH - integration round-trip test against a real local sshd
 * (CI testbed) or skipped when no testbed env is present.
 * Env: ECD_SSHD_HOST, ECD_SSHD_PORT, ECD_SSHD_USER, ECD_SSHD_KEY
 */
#include "ec_minunit.h"
#include "eclipse/core.h"
#include "eclipse/security.h"
#include "eclipse/ssh.h"
#include "eclipse/sftp.h"
#include "eclipse/platform.h"

#include <stdio.h>
#include <stdlib.h>

int tests_run = 0;

static char* g_host = NULL;
static int g_port = 22;
static char* g_user = NULL;
static char* g_key = NULL;
static EcHostKeyStore* g_hk = NULL;
static EcSshSession* g_ssh = NULL;

static bool tofu_accept(const EcServerKey* key, EcHostKeyStatus st, const char* host,
                        int port, void* user)
{
    (void)key; (void)user;
    /* testbed only: trust on first use; changed keys still refused */
    return st == EC_HK_UNKNOWN;
}

MU_TEST_SUITE_SETUP()
{
    g_host = getenv("ECD_SSHD_HOST");
    g_port = getenv("ECD_SSHD_PORT") ? atoi(getenv("ECD_SSHD_PORT")) : 22;
    g_user = getenv("ECD_SSHD_USER");
    g_key = getenv("ECD_SSHD_KEY");
    if (!g_host || !g_user || !g_key) {
        printf("[SKIP] sshd testbed not configured (ECD_SSHD_* unset)\n");
        exit(77); /* ctest SKIPPED */
    }
    char* dir = ec_file_tmpname("/tmp/ec-rt-hk");
    remove(dir);
    mu_check(ec_mkdir_p(dir));
    char* p = ec_path_join(dir, "known_hosts");
    g_hk = ec_hostkeys_open(p);
    free(p);
    free(dir);
    g_ssh = ec_ssh_new();
}

MU_TEST_SUITE_TEARDOWN()
{
    if (g_ssh) ec_ssh_free(g_ssh);
    if (g_hk) ec_hostkeys_free(g_hk);
}

MU_TEST(test_connect_exec)
{
    EcSshParams p = { 0 };
    p.host = g_host;
    p.port = g_port;
    p.username = g_user;
    p.key_path = g_key;
    p.hostkeys = g_hk;
    p.timeout_ms = 15000;
    char* err = NULL;
    mu_check_msg(ec_ssh_connect(g_ssh, &p, tofu_accept, NULL, &err), err);
    free(err);
    mu_check(ec_ssh_is_connected(g_ssh));

    EcStr out;
    ec_str_init(&out);
    err = NULL;
    int rc = ec_ssh_run_command(g_ssh, "printf hello-esd", &out, &err);
    free(err);
    mu_check_int_eq(0, rc);
    mu_assert_string_eq("hello-esd", out.s);
    ec_str_free(&out);

    /* exit code propagation */
    err = NULL;
    ec_str_init(&out);
    rc = ec_ssh_run_command(g_ssh, "exit 7", &out, &err);
    free(err);
    mu_check_int_eq(7, rc);
    ec_str_free(&out);
}

MU_TEST(test_sftp_roundtrip)
{
    EcSshParams p = { 0 };
    p.host = g_host;
    p.port = g_port;
    p.username = g_user;
    p.key_path = g_key;
    p.hostkeys = g_hk;
    p.hostkeys = g_hk;
    char* err = NULL;
    mu_check_msg(ec_ssh_connect(g_ssh, &p, tofu_accept, NULL, &err), err);
    free(err);
    EcSftp* sf = ec_sftp_open(g_ssh, &err);
    mu_check_msg(sf != NULL, err);
    free(err);
    if (!sf) return;

    char* home = ec_sftp_canonicalize(sf, ".");
    mu_check(home && *home == '/');

    /* write via transfer helper, read back, compare */
    char* lpath = ec_file_tmpname("/tmp/ec-rt-payload");
    const char* payload = "eclipse-ssh round trip payload 0123456789\n";
    mu_check(ec_file_write_atomic(lpath, payload, strlen(payload)));

    char* rpath = ec_path_join(home, "ec-rt-payload.bin");
    EcTransfer t;
    memset(&t, 0, sizeof t);
    t.kind = EC_TR_UPLOAD;
    t.local_path = lpath;
    t.remote_path = rpath;
    t.session = g_ssh;
    t.sftp = sf;
    mu_check_msg(ec_sftp_upload_file(&t), t.error);

    char* ldown = ec_file_tmpname("/tmp/ec-rt-down");
    EcTransfer t2;
    memset(&t2, 0, sizeof t2);
    t2.kind = EC_TR_DOWNLOAD;
    t2.local_path = ldown;
    t2.remote_path = rpath;
    t2.session = g_ssh;
    t2.sftp = sf;
    mu_check_msg(ec_sftp_download_file(&t2), t2.error);
    mu_check_int_eq((int)strlen(payload), (int)t2.total);

    char* back = NULL;
    size_t blen = 0;
    mu_check(ec_file_read_all(ldown, &back, &blen));
    mu_check_int_eq((int)strlen(payload), (int)blen);
    mu_check(memcmp(payload, back, blen) == 0);
    free(back);

    mu_check(ec_sftp_unlink(sf, rpath));
    remove(lpath);
    remove(ldown);
    free(lpath);
    free(rpath);
    free(home);
    ec_sftp_free(sf);
}

MU_TEST(test_hostkey_change_rejected)
{
    /* a second store that knows a DIFFERENT key must refuse the testbed */
    char* dir = ec_file_tmpname("/tmp/ec-rt-hk2");
    remove(dir);
    mu_check(ec_mkdir_p(dir));
    char* p = ec_path_join(dir, "known_hosts");
    char* norm = ec_hostkeys_normalize_host(g_host, g_port);
    EcStr line;
    ec_str_init(&line);
    ec_str_printf(&line, "%s ssh-ed25519 AAAAFAKEFAKEFAKEFAKEFAKE=\n", norm);
    ec_file_write_atomic(p, line.s, line.len);
    ec_str_free(&line);
    free(norm);
    EcHostKeyStore* hk = ec_hostkeys_open(p);
    EcSshSession* s2 = ec_ssh_new();
    EcSshParams pr = { 0 };
    pr.host = g_host;
    pr.port = g_port;
    pr.username = g_user;
    pr.key_path = g_key;
    pr.hostkeys = hk;
    char* err = NULL;
    mu_check(!ec_ssh_connect(s2, &pr, tofu_accept, NULL, &err)); /* CHANGED -> reject */
    free(err);
    ec_ssh_free(s2);
    ec_hostkeys_free(hk);
    free(p);
    free(dir);
}

int main(void)
{
    MU_SUITE_CONFIGURE(&test_suite_setup, &test_suite_teardown);
    MU_RUN_TEST(test_connect_exec);
    MU_RUN_TEST(test_sftp_roundtrip);
    MU_RUN_TEST(test_hostkey_change_rejected);
    MU_REPORT_SUMMARY();
    return minunit_fail == 0 ? 0 : 1;
}
