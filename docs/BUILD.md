# Building Eclipse SSH

## Dependencies

| Component | Ubuntu/Debian package | MSYS2 (MINGW64) package |
|---|---|---|
| Build | build-essential, ninja-build, cmake, pkg-config | mingw-w64-x86_64-gcc, -cmake, -ninja |
| UI (GTK4) | libgtk-4-dev | mingw-w64-x86_64-gtk4 |
| SSH | libssh-dev | mingw-w64-x86_64-libssh |
| Terminal | libvterm-dev | mingw-w64-x86_64-libvterm |
| Crypto | libssl-dev | mingw-w64-x86_64-openssl |

## Linux

```bash
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j"$(nproc)"
```

Targets: `eclipse-ssh` (GUI), `eclipse-ssh-cli` (CLI), `test_unit`, `test_roundtrip`.

Options:
- `-DECLIPSE_BUILD_UI=OFF` - core + CLI only (no GTK needed)
- `-DECLIPSE_ASAN=ON` - ASan+UBSan (use with Debug)
- `-DECLIPSE_WERROR=ON` - warnings as errors (default in CI)

## Windows (MSYS2 MinGW64)

```bash
pacman -S mingw-w64-x86_64-gtk4 mingw-w64-x86_64-libssh mingw-w64-x86_64-libvterm \
  mingw-w64-x86_64-openssl mingw-w64-x86_64-cmake mingw-w64-x86_64-ninja
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
```

## Packaging

`cpack -G TGZ` / `cpack -G DEB` on Linux; portable zip via `dist/` on Windows.

## Testing

```bash
ctest --test-dir build --output-on-failure
```

The `ssh_roundtrip` test runs against a local sshd when `ECD_SSHD_HOST`,
`ECD_SSHD_PORT`, `ECD_SSHD_USER`, `ECD_SSHD_KEY` are set (pubkey auth, sftp
subsystem enabled); otherwise it skips. CI starts a real sshd on every run.
