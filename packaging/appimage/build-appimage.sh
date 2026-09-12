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

export QML_SOURCES_PATHS="$ROOT/qml"
export EXTRA_QT_MODULES="qml;quick;quickcontrols2;sql;network;concurrent;widgets;dbus"
linuxdeploy --appdir "$APPDIR" --plugin qt --output appimage \
    --icon-file "$APPDIR/usr/share/icons/hicolor/256x256/apps/eclipse-ssh-desktop.png" \
    --desktop-file "$APPDIR/usr/share/applications/eclipse-ssh-desktop.desktop" \
    --outdir "$DIST"
echo "AppImage: $DIST/Eclipse-SSH-Desktop-$VERSION-x86_64.AppImage"
