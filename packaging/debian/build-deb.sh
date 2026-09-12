#!/usr/bin/env bash
# Builds the .deb package (Debian/Ubuntu amd64).
set -euo pipefail
HERE=$(cd "$(dirname "$0")" && pwd)
ROOT=$(cd "$HERE/../.." && pwd)
VERSION=$(awk "/^project\\(EclipseSSH/{found=1} found && /VERSION/{print \$2; exit}" "$ROOT/CMakeLists.txt")
DIST="$ROOT/dist"
BUILD="${ECLIPSE_BUILD_DIR:-$ROOT/build/linux-release}"
[ -x "$BUILD/src/eclipse-ssh-desktop" ] || { echo "Build first: cmake --build --preset linux-release"; exit 1; }

STAGE="$DIST/debstage/eclipse-ssh-desktop_$VERSION-1_amd64"
rm -rf "$STAGE"
install -Dm755 "$BUILD/src/eclipse-ssh-desktop" "$STAGE/usr/bin/eclipse-ssh-desktop"
install -Dm644 "$ROOT/resources/appicon.png" "$STAGE/usr/share/icons/hicolor/256x256/apps/eclipse-ssh-desktop.png"
install -Dm644 "$ROOT/README.md" "$STAGE/usr/share/doc/eclipse-ssh-desktop/README.md"
install -Dm644 "$ROOT/LICENSE" "$STAGE/usr/share/doc/eclipse-ssh-desktop/copyright"
install -Dm644 "$ROOT/examples/profiles.example.json" "$STAGE/usr/share/doc/eclipse-ssh-desktop/examples/profiles.example.json"
mkdir -p "$STAGE/usr/share/applications"
cat > "$STAGE/usr/share/applications/eclipse-ssh-desktop.desktop" <<DESKTOP
[Desktop Entry]
Type=Application
Name=Eclipse SSH Desktop
GenericName=SSH/SFTP Client
Comment=Modern SSH, SFTP, terminal and tunneling client
Exec=eclipse-ssh-desktop
Icon=eclipse-ssh-desktop
Categories=Network;RemoteAccess;
Terminal=false
DESKTOP
mkdir -p "$STAGE/DEBIAN"
# The binary links libqt6serialport6 only when Qt6::SerialPort was found at
# configure time; detect instead of hard-coding so the Depends list always
# matches the actual linkage (a wrong Depends = broken install on user machines).
SERIAL_DEP=""
if ldd "$BUILD/src/eclipse-ssh-desktop" | grep -q libqt6serialport; then
  SERIAL_DEP="libqt6serialport6, "
  echo "serial support linked: adding libqt6serialport6 to Depends"
fi
cat > "$STAGE/DEBIAN/control" <<CTRL
Package: eclipse-ssh-desktop
Version: $VERSION-1
Section: net
Priority: optional
Architecture: amd64
Depends: $SERIAL_DEP libqt6core6t64, libqt6gui6, libqt6qml6, libqt6quick6, libqt6quickcontrols2-6, libqt6sql6, libqt6sql6-sqlite, libqt6network6, libqt6widgets6, libqt6dbus6, qml6-module-qtquick, qml6-module-qtquick-window, qml6-module-qtquick-controls, qml6-module-qtquick-layouts, qml6-module-qtquick-templates, qml6-module-qtquick-dialogs, qml6-module-qtqml, qml6-module-qtqml-workerscript, qml6-module-qt-labs-platform, libssh-4, libssh2-1, libvterm0, libssl3, zlib1g
Maintainer: Eclipse SSH Desktop Contributors <maintainers@eclipsessh.invalid>
Homepage: https://example.invalid/eclipse-ssh-desktop
Description: Modern native SSH/SFTP client
 Eclipse SSH Desktop combines SSH, SFTP/SCP, a full terminal, port
 forwarding, jump hosts and a session manager in one fast native app.
CTRL

mkdir -p "$DIST"
dpkg-deb --root-owner-group --build "$STAGE" "$DIST/eclipse-ssh-desktop_${VERSION}-1_amd64.deb"
echo "Packaged: $DIST/eclipse-ssh-desktop_${VERSION}-1_amd64.deb"
