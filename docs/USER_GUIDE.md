# Eclipse SSH - User guide

## First run
1. Click the **+** in the toolbar, fill host/port/user, choose auth.
2. Passwords are stored encrypted in the local vault. Key files point at your
   OpenSSH private key (ed25519/rsa/ecdsa).
3. First connection shows the server fingerprint - verify it, then
   *Trust and save*. Changed fingerprints are refused with a warning.

## Terminal
- Tabs per session; `Ctrl+Shift+C` / `Ctrl+Shift+V` copy/paste (middle-click
  pastes too). Scrollback: mouse wheel / scrollbar; type or scroll-down to
  return to the live view.
- `Ctrl+Shift+P` opens the command palette: run snippets or raw commands.

## SFTP / Transfers
- Toolbar **folder** icon opens the dual-pane view. Select files, then
  upload/download. The queue supports pause/resume/cancel/retry and shows
  progress per file.

## Tunnels
- CLI: `eclipse-ssh-cli tunnel <profile> --local 8080:localhost:80`.
- SOCKS5 dynamic proxies and remote forwards are managed per session.

## CLI
```
eclipse-ssh-cli list
eclipse-ssh-cli exec <profile> "uptime"
eclipse-ssh-cli upload ./file.tar.gz <profile>:/tmp/
eclipse-ssh-cli download <profile>:/var/log/app.log ./
```

## Portable mode
Create an empty `portable.flag` next to the executable; all data stays in the
program folder.

## Themes
Light/dark built-in. Custom themes are JSON files in the themes folder
(`colors.accent`, `terminal_bg`, ...). See an example in docs.
