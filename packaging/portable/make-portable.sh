#!/usr/bin/env bash
# Creates the portable Linux archive: binary + bundled Qt/SSH runtime + launcher.
# Works with any Qt 6 install (distro packages, user-space sysroot, CI runners).
# Override with env: ECLIPSE_BUILD_DIR, QT_PLUGIN_SRC, QT_QML_SRC
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
VERSION=$(awk "/^project\\(EclipseSSH/{found=1} found && /VERSION/{print \$2; exit}" "$ROOT/CMakeLists.txt")
DIST="$ROOT/dist"
BUILD="${ECLIPSE_BUILD_DIR:-$ROOT/build/linux-release}"
BIN="$BUILD/src/eclipse-ssh-desktop"
[ -x "$BIN" ] || { echo "Build first: cmake --build --preset linux-release"; exit 1; }

STAGE="$DIST/portable/eclipse-ssh-desktop-$VERSION"
rm -rf "$STAGE"
mkdir -p "$STAGE/bin" "$STAGE/lib" "$STAGE/lib/qt6/plugins" "$STAGE/lib/qt6/qml"
install -m755 "$BIN" "$STAGE/bin/"

# --- Locate Qt plugin / QML import sources -------------------------------
QT_PLUGIN_SRC="${QT_PLUGIN_SRC:-${QT_PLUGIN_PATH:-}}"
QT_QML_SRC="${QT_QML_SRC:-${QML_IMPORT_PATH:-}}"
QMAKE_BIN="${QMAKE:-$(command -v qmake6 2>/dev/null || true)}"
if [ -n "$QMAKE_BIN" ]; then
  [ -n "$QT_PLUGIN_SRC" ] || QT_PLUGIN_SRC=$("$QMAKE_BIN" -query QT_INSTALL_PLUGINS 2>/dev/null | tail -1)
  [ -n "$QT_QML_SRC" ]    || QT_QML_SRC=$("$QMAKE_BIN" -query QT_INSTALL_QML 2>/dev/null | tail -1)
fi
QT_PLUGIN_SRC="${QT_PLUGIN_SRC:-/usr/lib/x86_64-linux-gnu/qt6/plugins}"
QT_QML_SRC="${QT_QML_SRC:-/usr/lib/x86_64-linux-gnu/qt6/qml}"
echo "Qt plugins source: $QT_PLUGIN_SRC"
echo "Qt QML source:     $QT_QML_SRC"

# --- Copy required Qt plugin families -------------------------------------
for fam in platforms imageformats iconengines sqldrivers tls networkinformation \
           platforminputcontexts xcbglintegrations; do
  [ -d "$QT_PLUGIN_SRC/$fam" ] && cp -r "$QT_PLUGIN_SRC/$fam" "$STAGE/lib/qt6/plugins/" || true
done

# --- Copy required QML module trees ----------------------------------------
for mod in QtQuick QtQml QtCore Qt; do
  [ -d "$QT_QML_SRC/$mod" ] && cp -r "$QT_QML_SRC/$mod" "$STAGE/lib/qt6/qml/" || true
done
# Trim heavyweight QML bits we never import
rm -rf "$STAGE/lib/qt6/qml/QtQuick/"{Particles,3D,Scene3D,Shapes/DesignTools} 2>/dev/null || true

# --- Bundle every non-core shared library (recursive over the stage) -------
# Core glibc runtime stays on the host; everything else ships in lib/.
is_core() {
  case "$(basename "$1")" in
    ld-linux*|libc.so*|libm.so*|libpthread*|libdl*|librt.so*|libresolv*|libnss_*|libgcc_s.so*) return 0 ;;
    *) return 1 ;;
  esac
}
declare -A seen
resolve() { # resolve deps of all ELF files currently staged, repeat until fixpoint
  local pass=0 newlibs=1
  while [ "$newlibs" -gt 0 ] && [ "$pass" -lt 8 ]; do
    pass=$((pass+1)); newlibs=0
    while IFS= read -r -d '' elf; do
      while IFS= read -r so; do
        [ -n "$so" ] || continue
        base=$(basename "$so")
        is_core "$base" && continue
        [ -z "${seen[$base]:-}" ] || continue
        seen[$base]=1; newlibs=$((newlibs+1))
        cp -L "$so" "$STAGE/lib/"
      done < <(ldd "$elf" 2>/dev/null | grep -oE '/[^ ]+\.so[^ ]*' | sort -u || true)
    done < <(find "$STAGE" -type f \( -name '*.so*' -o -perm -111 \) -print0)
  done
}
resolve

# --- qt.conf next to the binary + launcher ---------------------------------
cat > "$STAGE/bin/qt.conf" <<QCONF
[Paths]
Prefix=..
Plugins=lib/qt6/plugins
Qml2Imports=lib/qt6/qml
Translations=lib/qt6/translations
QCONF
cat > "$STAGE/eclipse-ssh-desktop.sh" <<'LAUNCHER'
#!/usr/bin/env bash
DIR=$(cd "$(dirname "$0")" && pwd)
export LD_LIBRARY_PATH="$DIR/lib${LD_LIBRARY_PATH:+:$LD_LIBRARY_PATH}"
export QT_PLUGIN_PATH="$DIR/lib/qt6/plugins"
export QML_IMPORT_PATH="$DIR/lib/qt6/qml"
export QML2_IMPORT_PATH="$DIR/lib/qt6/qml"
exec "$DIR/bin/eclipse-ssh-desktop" "$@"
LAUNCHER
chmod +x "$STAGE/eclipse-ssh-desktop.sh"
cp "$ROOT/README.md" "$ROOT/LICENSE" "$STAGE/"

# --- Archive ----------------------------------------------------------------
mkdir -p "$DIST"
tar -C "$DIST/portable" -cJf "$DIST/eclipse-ssh-desktop-$VERSION-linux-x86_64.tar.xz" "$(basename "$STAGE")"
echo "Portable Linux: $DIST/eclipse-ssh-desktop-$VERSION-linux-x86_64.tar.xz"
echo
echo "Windows portable: run cmake --preset windows-release && cmake --build --preset windows-release,"
echo "then zip the exe together with Qt DLLs (windeployqt) into eclipse-ssh-desktop-$VERSION-win64.zip."
