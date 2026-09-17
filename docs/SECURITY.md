# Security model

Priorities (spec #84): Security, Stability, Correctness, Performance, UX.

- **Credential storage**: AES-256-GCM (OpenSSL) with a PBKDF2-HMAC-SHA256
  stretched per-install master key (config dir, 0600). Plaintext secrets exist
  only in process memory and are wiped with `ecure_wipe` after use. No getter
  exposes credentials; exports exclude secrets by default (spec #33).
- **Host keys**: verification is mandatory on every connect. There is no code
  path that skips it. `CHANGED` keys are a hard failure (possible MITM) and
  require explicit user replacement.
- **Crypto**: delegated to OpenSSL and libssh; nothing implemented here.
- **Memory hygiene**: `-Wall -Wextra -Wshadow -Wconversion -Werror`; ASan+UBSan
  run in CI on every push; atomic config writes (tmp+rename); sensitive log
  redaction helper (`ec_redact`) - credentials never enter logs.
- **Terminal paste**: bracketed-paste mode support and optional paste
  confirmation guard against shell injection through the clipboard.
