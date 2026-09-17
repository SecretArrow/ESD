# Architecture

Eclipse SSH is C11 with strict module boundaries. The UI never touches crypto
primitives or libssh directly; it goes through the session orchestration layer.

```
ui/ (GTK4 + libadwaita)  dialogs (AdwDialog family), terminal widget (libvterm
                         render), split-pane groups, SFTP panes
  |
ssh/session.c         worker threads: connect, pty pump, keepalive, reconnect
  |          \
ssh/ssh_engine.c   sftp/  transfers.c   forwarding/fwd.c   network/proxy.c
  |                     (libssh SFTP + queue)  (L/R/SOCKS5)     (SOCKS4/5, HTTP CONNECT)
security/  vault (AES-256-GCM), hostkey store, key manager (libssh pki)
config/    repositories (sessions, snippets, shortcuts, themes) - JSON via cJSON
core/      log, strbuf, fs util, theme engine
platform/  POSIX / Win32 shims: paths, files, single-instance, dir iteration
```

Key contracts:
- **Non-blocking UI**: all network I/O happens on glib worker threads; results
  marshal to the GTK main loop via `g_idle_add`.
- **Host-key verification** (`security/hostkeys`): known_hosts read/write in
  OpenSSH format; `UNKNOWN` triggers TOFU consent, `CHANGED` is refused unless
  explicitly replaced by the user.
- **Terminal scrollback**: host-managed via libvterm `sb_pushline/sb_popline`
  callbacks into a ring buffer; view offset composes history+screen rows.
- **Transfers**: queue with worker threads, token-bucket rate limiting,
  pause/cancel/retry; progress marshals to UI idles.
- **Jump hosts / proxies**: implemented through a ProxyCommand-compatible pipe
  (`eclipse-ssh-cli pipe [--proxy SPEC] HOST PORT`) so libssh speaks SSH over an
  externally bridged stream - OpenSSH semantics without fd-ownership traps.
