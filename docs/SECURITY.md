# Security Policy & Model

## Priorities

**Security → Stability → UX → Performance → Features.**

## Credential storage

| Mode | Storage | Notes |
|---|---|---|
| OS (default) | Windows Credential Manager (DPAPI) / freedesktop Secret Service | OS-protected, per-user |
| Fallback | `secrets.bin` AES-256-GCM container, key = PBKDF2-HMAC-SHA256(120k) over a 0600 machine key | only when no keyring exists; reported in Settings |
| Session | RAM only, wiped at exit | nothing touches disk |
| Ask | not stored | prompted per connection |

- Passwords/passphrases are **never** stored in the profile database, config
  files, exports or backups. Backups explicitly contain metadata only.
- The fallback container is clearly labelled in the UI (`Settings → Security`).

## Host keys

- First connection: explicit **Trust once / Trust & Save / Cancel** — never implicit.
- Key change: hard warning (possible MITM); **saving a changed key is disabled**.
- `known_hosts` is OpenSSH-compatible (plaintext + `|1|` hashed entries) and shared
  with the system OpenSSH by default (`~/.ssh/known_hosts`).
- Jump-host hops enforce the same policy; an unknown bastion aborts with guidance.

## Logging

- All log messages pass a redaction filter (`password=`, `passphrase=`, tokens,
  `-----BEGIN … PRIVATE KEY-----` blocks → `[REDACTED]`).
- The engine and worker never log secrets; SSH debug output is opt-in (Debug mode).
- Log files live under the user data directory with rotating size caps.

## Transport

- Both engines negotiate modern OpenSSH defaults (curve25519-sha256, aes-gcm,
  chacha20-poly1305 where available). No legacy algorithm overrides are exposed.
- `connectOverFd` is used for proxy/jump paths so there is no plaintext hop.

## Reporting a vulnerability

Please open a private security advisory or contact the maintainers directly.
Do not open public issues for security reports.
