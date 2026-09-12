#!/usr/bin/env bash
# Full Windows runtime deployment.
#
# Problem this solves: windeployqt only copies Qt's own DLLs/plugins. The exe
# and libssh/libssh2 also import the MinGW runtime (libgcc_s_seh-1.dll,
# libstdc++-6.dll, libwinpthread-1.dll) and third-party DLLs (libssh.dll,
# libssh2.dll, libcrypto-3-x64.dll, libssl-3-x64.dll, zlib1.dll, ...). When
# those are missing the app fails on end-user machines with
# "code execution cannot proceed because ... was not found".
#
# Strategy: run windeployqt for Qt modules/plugins, then walk EVERY staged
# exe/dll with objdump, copy any import that lives in the MSYS2 prefix, and
# repeat until fixpoint. Finally verify that every import of every staged
# binary resolves either inside the bundle or in the system directory --
# and FAIL the CI job otherwise. This is a hard gate against shipping
# broken artifacts.
#
# Env: ECLIPSE_BUILD_DIR (default build/ci). Output: dist/win64/
set -euo pipefail

HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
BUILD="${ECLIPSE_BUILD_DIR:-$ROOT/build/ci}"
STAGE="$ROOT/dist/win64"
MINGW_BIN="/mingw64/bin"
SYS32="/c/Windows/System32"

rm -rf "$STAGE"
mkdir -p "$STAGE"

EXE=$(find "$BUILD" -name 'eclipse-ssh-desktop.exe' -type f | head -1)
[ -n "$EXE" ] || { echo "ERROR: eclipse-ssh-desktop.exe not found under $BUILD"; exit 1; }
cp "$EXE" "$STAGE/"
echo "Staged exe: $EXE"

# --- 1) Qt modules + plugins + QML -----------------------------------------
WDEP=$(command -v windeployqt6 || command -v windeployqt)
[ -n "$WDEP" ] || { echo "ERROR: windeployqt not found"; exit 1; }
echo "Using $WDEP"
(cd "$STAGE" && "$WDEP" --qmldir "$ROOT/qml" eclipse-ssh-desktop.exe)

# --- 2) Trim plugins we never use (and whose 3rd-party deps we can't ship) --
rm -rf "$STAGE/qmltooling"
rm -f "$STAGE"/sqldrivers/qsqlibase.dll "$STAGE"/sqldrivers/qsqlmysql.dll \
      "$STAGE"/sqldrivers/qsqlodbc.dll "$STAGE"/sqldrivers/qsqlpsql.dll
# Software-GL fallback for VMs/RDP without OpenGL 2.1 (optional, if present)
[ -e "$MINGW_BIN/opengl32sw.dll" ] && cp -n "$MINGW_BIN/opengl32sw.dll" "$STAGE/" || true

# --- 3) Recursive DLL closure (fixpoint over objdump imports) ---------------
# For every exe/dll in the bundle: parse "DLL Name:" imports; anything not
# already staged and not a system DLL gets copied from the MSYS2 prefix.
# Repeat until a full pass copies nothing new (transitive dependencies).
stage_or_system() { # $1 = dll file name (case-insensitive FS)
  [ -e "$STAGE/$1" ] && return 0
  [ -e "$SYS32/$1" ] && return 0
  return 1
}

pass=0
changed=1
while [ "$changed" -eq 1 ] && [ "$pass" -lt 10 ]; do
  pass=$((pass+1)); changed=0
  while IFS= read -r -d '' bin; do
    while IFS= read -r dll; do
      [ -n "$dll" ] || continue
      stage_or_system "$dll" && continue
      if [ -e "$MINGW_BIN/$dll" ]; then
        cp "$MINGW_BIN/$dll" "$STAGE/"
        echo "  [closure] added $dll (imported by ${bin#$ROOT/})"
        changed=1
      else
        echo "  [closure] WARNING: $dll imported by ${bin#$ROOT/} not in $MINGW_BIN"
      fi
    done < <(objdump -p "$bin" 2>/dev/null | awk '/[Dd][Ll][Ll] [Nn]ame:/{print $3}')
  done < <(find "$STAGE" -type f \( -name '*.dll' -o -name '*.exe' \) -print0)
done

# --- 4) Hard verification gate ----------------------------------------------
# Every import of every staged binary must resolve inside the bundle or the
# system directory. Anything else = the exact bug users reported. Fail CI.
FAIL=0
while IFS= read -r -d '' bin; do
  while IFS= read -r dll; do
    [ -n "$dll" ] || continue
    if ! stage_or_system "$dll"; then
      echo "UNRESOLVED: $bin imports $dll -- not bundled, not a system DLL"
      FAIL=1
    fi
  done < <(objdump -p "$bin" 2>/dev/null | awk '/[Dd][Ll][Ll] [Nn]ame:/{print $3}')
done < <(find "$STAGE" -type f \( -name '*.dll' -o -name '*.exe' \) -print0)

if [ "$FAIL" -ne 0 ]; then
  echo "ERROR: DLL closure incomplete -- refusing to package. See UNRESOLVED list above."
  exit 1
fi

echo
echo "Deployed bundle: $STAGE ($(find "$STAGE" -name '*.dll' | wc -l) DLLs, $(du -sh "$STAGE" | cut -f1))"
find "$STAGE" -maxdepth 1 -name '*.dll' -printf '  %f\n' | sort
echo "DLL closure verified: all imports resolve."
