# Eclipse SSH Desktop

[![CI](https://github.com/SecretArrow/ESD/actions/workflows/ci.yml/badge.svg)](https://github.com/SecretArrow/ESD/actions/workflows/ci.yml)
[![Release](https://github.com/SecretArrow/ESD/actions/workflows/release.yml/badge.svg)](https://github.com/SecretArrow/ESD/actions/workflows/release.yml)

**A modern, native SSH/SFTP client for Windows and Linux — simple by default, powerful when needed.**

Eclipse SSH Desktop combines SSH, SFTP, SCP, a full terminal, port forwarding, a session
manager, a dual-panel file manager and automation tools into one fast native application,
built with C++20 and Qt 6.

```
┌─────────────────────────────────────────────────────────────┐
│ [New] [Connect] [Terminal] [Files] [Tunnel] [Settings]      │
├──────────────┬──────────────────────────────────────────────┤
│ CONNECTIONS  │  Terminal │ Files │ Monitor                  │
│ ★ prod-web   │  (full VT/xterm terminal, splits, recording) │
│ ★ bastion    │                                              │
│ staging      │                                              │
├──────────────┴──────────────────────────────────────────────┤
│ Connected · 12 ms · libssh      |  transfers / notifications │
└─────────────────────────────────────────────────────────────┘
```

## Highlights

- **Dual SSH engines** — libssh (default) and libssh2 behind one abstraction; pick per profile.
- **Real terminal** — libvterm-based VT/xterm emulation: 16/256/truecolor, bold/italic/underline,
  scrollback with reflow, alternate screen (vim/htop/tmux work), bracketed paste, mouse
  reporting, split panes, session recording.
- **SFTP file manager** — dual panel (local ⇄ remote), upload/download with a full transfer
  manager (queue, pause/resume, retry, speed/ETA), rename/mkdir/chmod/symlink, multi-select,
  "Open Terminal Here" ⇄ "Open Folder" workflows.
- **Remote archive viewer** — list ZIP/TAR/TGZ/TAR.BZ2/TAR.XZ contents *without downloading the
  whole archive* (ZIP via central-directory tail reads; TAR via sequential stream). Extract
  single entries on demand.
- **Port forwarding** — local, remote and dynamic (SOCKS5) with a manager UI and auto-start rules.
- **Jump hosts / bastion chains** — declarative multi-hop tunnels (no manual `ssh -J`).
- **Security first** — OpenSSH `known_hosts` verification with SHA256 fingerprints and an explicit
  changed-key warning that cannot be bypassed by saving; credentials in the OS keyring
  (Windows Credential Manager / freedesktop Secret Service) with an AES-256-GCM fallback and a
  session-only mode; no plaintext secrets on disk and credential redaction in all logs.
- **Diagnostics** — step-by-step DNS → TCP → handshake → host key → auth → SFTP report with latency.
- **Command runner** — run one command on many servers at once; snippets with `{variables}`.
- **Keyboard-driven UX** — command palette (`Ctrl+Shift+P`), customizable shortcuts, command
  palette fuzzy search, light/dark/system themes, custom accent color.
- **Fast and native** — C++20, Qt 6 Quick UI, worker-thread SSH engine; the UI never blocks.

## Repository layout

```
src/            C++ sources (core, ssh, sftp, transfer, terminal, archive, app, ...)
qml/            Qt Quick UI (pages, dialogs, theme singleton)
tests/          Qt Test suites (host keys, agent wire format, SOCKS5, archives, profiles)
packaging/      Debian, AppImage and portable packaging scripts
docs/           ARCHITECTURE.md, SECURITY.md, BUILD.md, CONTRIBUTING.md
examples/       Example configuration
```

## Quick start (Linux)

```bash
# Debian/Ubuntu build dependencies
sudo apt install cmake ninja-build g++ \
     qt6-base-dev qt6-declarative-dev qt6-declarative-dev-tools libqt6sql6-sqlite \
     libgl1-mesa-dev libssh-dev libssh2-1-dev libvterm-dev \
     libssl-dev zlib1g-dev qml6-module-qtquick-controls qml6-module-qtquick-layouts

cmake --preset linux-release
cmake --build --preset linux-release
./build/linux-release/src/eclipse-ssh-desktop
```

Run the tests:

```bash
cd build/linux-release && ctest --output-on-failure
```

See [docs/BUILD.md](docs/BUILD.md) for Windows instructions and packaging.

## Documentation

- [docs/ARCHITECTURE.md](docs/ARCHITECTURE.md) — module map, threading model, engine abstraction
- [docs/SECURITY.md](docs/SECURITY.md) — credential storage, host key policy, logging redaction
- [docs/BUILD.md](docs/BUILD.md) — per-platform build & packaging guide
- [docs/CONTRIBUTING.md](docs/CONTRIBUTING.md) — coding rules, commit/PR process
- [docs/THIRD_PARTY_LICENSES/](docs/THIRD_PARTY_LICENSES/README.md) — dependency licenses

## Status & roadmap

Implemented today: connection manager (SQLite, groups/tags/search/import/export incl.
OpenSSH config), SSH connect with password/public-key/agent/keyboard-interactive (+MFA),
host key management, terminal with splits & recording, SFTP dual panel + transfer manager,
SCP transfers, local/remote/dynamic forwarding, jump chains, archive viewer, remote file
preview & editor workflow, command runner, snippets, server monitor, diagnostics, session
recording, system tray, notifications, first-run wizard, command palette, workspaces-ready
layout persistence.

On the roadmap (UI labels these *Coming Soon* until real): embedded RDP/VNC viewers,
7Z archives, plugin marketplace UI. The plugin interface itself already exists
(`src/plugins/`) and loads Qt metadata plugins from the application `plugins/` directory.

## License

MIT — see [LICENSE](LICENSE). Third-party licenses are documented in
[docs/THIRD_PARTY_LICENSES/](docs/THIRD_PARTY_LICENSES/README.md).
