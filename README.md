# Eclipse SSH (ESC)

A professional, lightweight, native **C11** SSH/SFTP client for **Windows 10/11** and
**Ubuntu/Debian**. GTK4 UI, libssh transport, libvterm terminal, OpenSSL-backed
credential vault. No Electron, no browser runtime, no mocks: real SSH, real SFTP,
real tunneling.

```
| Section                 | Status |
|-------------------------|--------|
| SSH engine (libssh)     | done   |
| Auth: password / key / agent | done |
| Host-key verification (known_hosts, TOFU + change detection) | done |
| Terminal (libvterm: ANSI/VT, 256c, truecolor, UTF-8, scrollback, mouse) | done |
| Sessions, groups, color tags, search-ready store | done |
| SFTP + transfer queue (pause/resume/cancel/retry/rate-limit) | done |
| Port forwarding L / R / SOCKS5 dynamic | done |
| Proxy: SOCKS5 / SOCKS4a / HTTP CONNECT (ProxyCommand pipe) | done |
| Key manager (generate ed25519/rsa/ecdsa, import/export, fingerprint) | done |
| Themes: light/dark + JSON custom themes (import/export) | done |
| Command palette, snippets, startup automation | done |
| CLI companion (`eclipse-ssh-cli`) reusing the same core | done |
| Import/export sessions (secrets excluded by default) | done |
| Encrypted vault (AES-256-GCM + PBKDF2, OpenSSL) | done |
| Single-instance, portable mode, deep links (ssh://) | done |
| CI: Linux + Windows builds, unit + sshd round-trip, ASan/UBSan | done |
```

## Build

See [BUILD.md](docs/BUILD.md). Quick start (Ubuntu/Debian):

```bash
sudo apt install build-essential ninja-build pkg-config \
  libgtk-4-dev libssh-dev libvterm-dev libssl-dev
cmake -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
cmake --build build -j
./build/eclipse-ssh        # GUI
./build/eclipse-ssh-cli    # CLI companion
```

## Architecture

See [ARCHITECTURE.md](docs/ARCHITECTURE.md). Layout follows the spec:

```
core/ platform/ ssh/ sftp/ terminal/ forwarding/ network/ config/ security/ ui/ cli/ app/ tests/
```

## Testing

```bash
ctest --test-dir build --output-on-failure
```

Unit tests always run; the SSH/SFTP round-trip suite runs against a local sshd
when `ECD_SSHD_HOST/PORT/USER/KEY` are set (CI starts one automatically), and
skips otherwise.

## Security

See [SECURITY.md](docs/SECURITY.md). Credentials never touch disk unencrypted.
Host-key verification is mandatory; a changed host key is a hard error that
only an explicit user action can replace.

## License

MIT — see [LICENSE](LICENSE).
