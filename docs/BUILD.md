# Building Eclipse SSH Desktop

## Requirements

| Tool / library | Version | Notes |
|---|---|---|
| CMake | >= 3.24 | Ninja generator recommended |
| C++ compiler | GCC >= 11 / Clang >= 14 / MSVC 2022 | C++20 required |
| Qt 6 | >= 6.5 | Core, Gui, Qml, Quick, QuickControls2, Sql, Network, Concurrent, Widgets, DBus (Linux) |
| libssh | >= 0.10 | default SSH backend |
| libssh2 | >= 1.11 | alternative SSH backend |
| libvterm | >= 0.3 | terminal emulation |
| OpenSSL | >= 3.0 | crypto, AES-GCM credential container |
| zlib / bzip2 / xz | any | archive module (bz2/xz optional, auto-detected) |

## Linux (Debian / Ubuntu)

```bash
sudo apt install cmake ninja-build g++ \
    qt6-base-dev qt6-base-private-dev qt6-declarative-dev qt6-declarative-dev-tools \
    libqt6sql6-sqlite libgl1-mesa-dev \
    libssh-dev libssh2-1-dev libvterm-dev libssl-dev zlib1g-dev \
    qml6-module-qtquick-controls qml6-module-qtquick-layouts \
    qml6-module-qtquick-templates qml6-module-qtquick-dialogs \
    qml6-module-qtqml-workerscript qml6-module-qt-labs-platform

cmake --preset linux-release
cmake --build --preset linux-release
ctest --test-dir build/linux-release --output-on-failure
./build/linux-release/src/eclipse-ssh-desktop
```

### User-space build (no root, any distro)

`scripts/setup-deps.sh` downloads official Debian packages matching the running
release and extracts them into `tools/sysroot` (no root needed):

```bash
bash scripts/setup-deps.sh
source scripts/deps-env.sh     # sets CMAKE_PREFIX_PATH, LD_LIBRARY_PATH, ...
cmake --preset linux-release && cmake --build --preset linux-release
```

## Windows 10/11 (MSVC + vcpkg or MSYS2)

### vcpkg (recommended for CI)

```powershell
vcpkg install --triplet x64-windows ^
    qtbase qtdeclarative libssh libssh2 libvterm openssl zlib
cmake --preset windows-release -DCMAKE_TOOLCHAIN_FILE=%VCPKG_ROOT%/scripts/buildsystems/vcpkg.cmake
cmake --build --preset windows-release
```

### MSYS2

```bash
pacman -S mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja \
          mingw-w64-x86_64-qt6-base mingw-w64-x86_64-qt6-declarative \
          mingw-w64-x86_64-libssh mingw-w64-x86_64-libssh2 \
          mingw-w64-x86_64-libvterm mingw-w64-x86_64-openssl
cmake --preset windows-release
cmake --build --preset windows-release
```

The Windows backend uses the OpenSSH agent named pipe (`\\.\pipe\openssh-ssh-agent`)
and Windows Credential Manager (DPAPI) for secrets.

## CMake options

| Option | Default | Description |
|---|---|---|
| `ECLIPSE_WITH_LIBSSH` | ON | build the libssh backend |
| `ECLIPSE_WITH_LIBSSH2` | ON | build the libssh2 backend |
| `ECLIPSE_BUILD_TESTS` | ON | build + register ctest suites |
| `ECLIPSE_WARNINGS_AS_ERRORS` | OFF | `-Werror` / `/WX` |
| `ECLIPSE_DEVELOPER_MODE` | ON | extra diagnostics UI |

## Packaging

```bash
# Debian package
packaging/debian/build-deb.sh            # -> dist/eclipse-ssh-desktop_<ver>_amd64.deb

# AppImage (requires linuxdeploy in PATH)
packaging/appimage/build-appimage.sh     # -> dist/Eclipse-SSH-Desktop-x86_64.AppImage

# Portable archives (Windows zip + Linux tar.xz)
packaging/portable/make-portable.sh      # -> dist/
```

See [ARCHITECTURE.md](ARCHITECTURE.md) for how the pieces fit together and
[SECURITY.md](SECURITY.md) for the security model.
