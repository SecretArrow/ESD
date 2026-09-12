#!/usr/bin/env bash
# Builds a universal Linux AppImage using linuxdeploy + Qt plugin delivery.
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
VERSION=$(awk "/^project\\(EclipseSSH/{found=1} found && /VERSION/{print \$2; exit}" "$ROOT/CMakeLists.txt")
DIST="$ROOT/dist"
BUILD="${ECLIPSE_BUILD_DIR:-$ROOT/build/linux-release}"
APPDIR="$DIST/AppDir"
[ -x "$BUILD/src/eclipse-ssh-desktop" ] || { echo "Build first."; exit 1; }
command -v linuxdeploy >/dev/null || {
  echo "linuxdeploy required: https://github.com/linuxdeploy/linuxdeploy/releases"
  echo "  curl -LO https://github.com/linuxdeploy/linuxdeploy/releases/download/continuous/linuxdeploy-x86_64.AppImage"
  exit 1
}
rm -rf "$APPDIR"
install -Dm755 "$BUILD/src/eclipse-ssh-desktop" "$APPDIR/usr/bin/eclipse-ssh-desktop"
install -Dm644 "$ROOT/resources/appicon.png" "$APPDIR/usr/share/icons/hicolor/256x256/apps/eclipse-ssh-desktop.png"
mkdir -p "$APPDIR/usr/share/applications"
cat > "$APPDIR/usr/share/applications/eclipse-ssh-desktop.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=Eclipse SSH Desktop
Exec=eclipse-ssh-desktop
Icon=eclipse-ssh-desktop
Categories=Network;RemoteAccess;
Terminal=false
DESKTOP

export QMAKE="${QMAKE:-$(command -v qmake6 || command -v qmake)}"
[ -n "$QMAKE" ] || { echo "qmake6 not found - required by linuxdeploy-plugin-qt"; exit 1; }
export QML_SOURCES_PATHS="$ROOT/qml"
# NOTE: linuxdeploy-plugin-qt reads EXTRA_QT_PLUGINS (not *_MODULES).
# sql = sqlite driver; imageformats = extra format plugins beyond defaults.
export EXTRA_QT_PLUGINS="sql;imageformats"
linuxdeploy --appdir "$APPDIR" --plugin qt --output appimage \
    --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/eclipse-ssh-desktop.png" \
    --desktop-file "$APPDIR/usr/share/applications/eclipse-ssh-desktop.desktop" \
    --outdir "$DIST"
echo "AppImage: $DIST/Eclipse-SSH-Desktop-$VERSION-x86_64.AppImage"
