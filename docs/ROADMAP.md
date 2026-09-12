# Roadmap

This document tracks the capability gaps identified by the 2026 SSH-client
feature research, what shipped, and what is planned next. Items marked
**Shipped in v0.2.0** are implemented in this tree and built by CI; the rest
are planned in priority order.

## Shipped in v0.2.0

| Area | Feature |
|------|---------|
| Terminal | Find-in-buffer: full-text search across scrollback + screen, match highlighting, next/previous navigation, find bar UI (Ctrl+F) |
| Terminal | OSC 133 shell integration: prompt/command markers, "command finished (exit N)" notifications, jump-to-previous/next command |
| Terminal | Sixel graphics decoder (`SixelDecoder`) with inline image extraction from the terminal stream |
| Interop | PuTTY session import — Windows registry (live) and `.reg` export files (any OS) |
| Interop | OpenSSH `known_hosts` import — plain, `[host]:port`, comma lists, negated patterns, `|1|` hashed entries (HMAC-SHA1), `@cert-authority`/`@revoked` awareness |
| Profiles | Encrypted profile bundle export/import (AES-256-GCM, PBKDF2-HMAC-SHA256) for device-to-device transfer |
| Profiles | Portable sync folder: point profile storage at any synced directory (Dropbox/Nextcloud/network drive) |
| Connections | Telnet console sessions (RFC 854/855 minimal negotiation over TCP) |
| Connections | Serial console sessions via Qt6::SerialPort (baud/parity/data/stop/flow), compile-time stub when the module is absent |
| Connections | X11 forwarding plumbing and profile toggle |
| Security | Per-profile KEX algorithm preference (applied to both engines) |
| Security | Diagnostics report negotiated key exchange + post-quantum readiness flag with honest engine-limitation notes |
| UX | Import / Export hub dialog unifying PuTTY, known_hosts, and profile bundles |

## Planned

### P0 — Post-quantum key exchange (engine dependency)
`mlkem768x25519-sha256` is the default KEX of OpenSSH 10.0 (April 2025) and is
shipped by RHEL 10. Neither libssh (0.11.x) nor libssh2 (1.11.x) implements a
post-quantum KEX yet, so a native client cannot negotiate one today. Plan:
1. Track upstream: libssh PQC work (tracking issue references ML-KEM hybrids)
   and libssh2 proposals. Bump engine versions as soon as support lands —
   the dual-engine abstraction in this codebase makes the upgrade
   client-transparent.
2. Interim: the Diagnostics dialog reports the negotiated KEX and flags
   non-PQ connections; per-profile KEX preference lets users pin the strongest
   algorithms a server offers.
3. Long-term fallback: vendor `liboqs` behind a build option if upstream
   support slips past two release cycles.

### P1 — SSH certificates & zero-trust integration
- OpenSSH user certificates (`*.pub`-cert auth against a CA), short-lived
  cert issuance flows, and `AuthorizedPrincipalsFile` awareness.
- Integration surfaces for Teleport / Cloudflare Access / HashiCorp
  Boundary-style brokers (SSO login → short-lived cert → connect).

### P1 — Mosh / Eternal Terminal
Roaming UDP sessions for flaky or high-latency links. Requires a separate
transport layer alongside SSH; scoped after the engine-layer refactor that
X11/console work introduced.

### P2 — Terminal polish
- Full in-buffer sixel rendering (decoder + extraction shipped; painting
  images inline inside scrollback is next).
- Kitty graphics protocol.
- Shell-integration deep links (click-to-retry a failed command block).

### P2 — Sync evolution
- End-to-end encrypted cloud sync service (bundle export/import is the
  manual stopgap).
- Selective per-folder sync conflict resolution UI.
