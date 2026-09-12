# Eclipse SSH Desktop — Architecture

## Principles

1. **Security → Stability → UX → Performance → Features** (in that order).
2. Business logic never lives in QML; the UI binds to C++ models/services.
3. Network I/O never blocks the UI thread.
4. Every feature shown in the UI is implemented; unfinished ideas are labelled *Coming Soon*.

## Module map

```
src/
├── common/      Secure memory, Outcome (friendly+technical errors), utils
├── core/        Database(SQLite), Settings, ThemeManager, Logger(+LogModel),
│                profiles (ConnectionProfile, ProfileStore, OpenSSH import/export),
│                credentials (OS keyring / AES-GCM / session), shortcuts, commands,
│                snippets
├── ssh/         Engine abstraction + backends, host key manager, agent client,
│                worker/session orchestration, forwarding, jump chains, diagnostics
│   └── backends/
│       ├── libssh/    LibsshEngine/Channel/Sftp/Scp        (LGPL-2.1)
│       └── libssh2/   Libssh2Engine/Channel/Sftp/Scp       (BSD-3)
├── sftp/        RemoteFsModel / LocalFsModel (dual panel models)
├── transfer/    TransferManager + TransferJob (chunked streaming) + TransferModel
├── terminal/    VtEmulator (libvterm wrapper) + TermItem (QQuickPaintedItem)
├── archive/     ArchiveSource (local | SFTP ranged) + Zip/Tar readers + service
├── app/         AppController (QML root), SessionManager, ProfileDraft,
│                TrayController, main.cpp
└── platform/    (Windows: DPAPI/CredMan, named-pipe agent — compiled per-OS)
```

## Threading model

- **One `SshWorker` per session**, living on its own `QThread`. ALL engine calls
  (connect, auth, channels, forwards) happen there; the UI only exchanges queued
  signals. A 4 ms poll loop multiplexes channel I/O, forward pumps and SOCKS5
  handshakes.
- **Host key gate**: the worker emits `hostKeyPresented` and blocks on a
  `QWaitCondition` until the user accepts/rejects. Unknown keys are never accepted
  automatically; changed keys cannot be saved.
- **Keyboard-interactive / MFA**: prompts are surfaced to the UI the same way
  (worker blocks on a wait-condition until answers arrive).
- **Transfers** run on `QtConcurrent` threads with their **own SFTP session**
  (channel-level concurrency) and stream 64 KiB chunks — a 20 GB file never
  enters RAM. Transfer-thread SFTP calls are serialized against the worker
  through the engine's recursive mutex (chunk-granularity).
- **Jump chains** use `Bridge` (socket pair + two pump threads per hop) feeding
  the next engine through a direct-tcpip channel, so chained handshakes never
  need the worker's event loop.

## Engine abstraction

`ISshEngine` (connect / connectOverFd / authenticate / openChannel /
openForwardChannel / openSftp / openScp / remote forwards / agent auth /
negotiated info). Both backends implement it; the generic features (jump chains,
local+dynamic forwarding with SOCKS5, diagnostics, latency probes via exec-ping)
are written once against the interface. Engine choice is per profile
(`auto | libssh | libssh2`).

## Host key handling

`HostKeyManager` reads/writes OpenSSH `known_hosts` (plaintext + `|1|` HMAC-SHA1
hashed entries), computes SHA256/MD5 fingerprints like OpenSSH, and classifies
keys as Known / Unknown / Changed. The UI shows trust-once / trust-and-save for
unknown keys, and a hard warning dialog for changed keys where saving is disabled.

## Credentials

`CredentialManager` (facade) → per-OS backend:
- Windows: Credential Manager (DPAPI-protected, `CredWrite`/`CredRead`).
- Linux: freedesktop **Secret Service** over QtDBus (GNOME Keyring / KWallet);
  falls back to an **AES-256-GCM** container (`secrets.bin`, key stretched from a
  0600 machine key via PBKDF2-HMAC-SHA256) when no keyring is available.
- Session-only mode keeps secrets in memory and wipes them at exit.
Secrets are keyed `profile:<id>:password|passphrase` and never written to logs
(the logger scrubs `password=`, `BEGIN PRIVATE KEY` blocks, etc.).

## Terminal

`VtEmulator` wraps libvterm: screen callbacks stream into a snapshot API the
`TermItem` (QQuickPaintedItem) paints; scrollback is a deque of styled lines
(sb_pushline/sb_popline). Input goes Qt key → libvterm keyboard API → output
callback → worker `channelWrite`. Resize triggers pty resize; mouse reporting
and bracketed paste follow the remote's modes.

## Archive viewer

`ArchiveSource` abstracts "seekable or sequential bytes" — a local file or an
SFTP handle with a 256 KiB window cache. ZIP listing reads only the EOCD +
central directory (tail of the file); extraction streams one entry with raw
inflate + CRC verification. TAR(.gz/.bz2/.xz) is scanned sequentially; entries
are extracted by a targeted second pass. 7Z reports *Coming Soon*.

## QML layer

`App` (AppController) exposes ProfileStore / SessionManager / Settings / Theme /
Credentials to QML; pages are Dashboard / Session(Terminal|Files|Monitor) / Logs /
Settings. Dialogs: host key, auth prompts, quick connect, profile editor
(progressive disclosure), command palette, forwarding, command runner,
diagnostics, first-run wizard, about, transfer panel, archive viewer.
