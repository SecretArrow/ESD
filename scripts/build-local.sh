#!/usr/bin/env bash
# Local (no-GTK) syntax + unit build helper for Eclipse SSH.
# Compiles every non-UI module with the sysroot libssh/libvterm + system OpenSSL.
set -u
cd "$(dirname "$0")/.."
SYSROOT=${SYSROOT:-/home/z/my-project/tools/sysroot/usr}
INC="-Icore -Iplatform -Isecurity -Issh -Isftp -Iterminal -Iforwarding -Iconfig -Inetwork -Ivendor -Ivendor/cjson $(pkg-config --cflags glib-2.0 gio-2.0) -I$SYSROOT/include -I$SYSROOT/include/libssh -I/usr/include/openssl"
CFLAGS="-pthread -std=c11 -Wall -Wextra -Wno-unused-parameter -Wno-deprecated-declarations -Werror -D_GNU_SOURCE -D_DEFAULT_SOURCE"
SRCS="network/proxy.c ssh/session.c terminal/term_core.c core/core.c core/theme.c platform/platform_posix.c security/security.c ssh/ssh_engine.c sftp/sftp.c forwarding/fwd.c config/repos.c vendor/cjson/cJSON.c"
FAIL=0
for src in $SRCS; do
  echo "CC  $src"
  gcc $CFLAGS $INC -fsyntax-only "$src" || FAIL=1
done
exit $FAIL
