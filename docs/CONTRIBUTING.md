# Contributing

## Ground rules

1. **No mock/fake implementations.** Features are either real or explicitly
   labelled *Coming Soon* in the UI.
2. **Security defaults are not negotiable**: no plaintext credentials, no
   auto-accepted host keys, no credential logging.
3. **UI thread never blocks** — all SSH/network work belongs to workers.
4. Keep modules separated: `core` (no UI), `ssh` (no Qt Widgets), `terminal`,
   `transfer`, `archive`; QML only binds to C++ APIs.

## Code style

- C++20, 4-space indent, braces on new lines, `QStringLiteral` for literals.
- Types in namespace `eclipse`; file names match class names.
- Use `Outcome` for user-facing errors (friendly message + technical detail).
- Log via `LOG_*` macros; never log secrets.

## Workflow

1. Fork, create a topic branch (`feat/…`, `fix/…`).
2. `cmake --build build/linux-release && ctest --test-dir build/linux-release`
   must pass (add tests for new logic — see `tests/`).
3. Keep commits atomic; follow `type(scope): summary` messages.
4. Update `docs/` when behaviour or architecture changes; add third-party
   license notes to `docs/THIRD_PARTY_LICENSES/`.

## Reporting bugs

Include: platform, Qt/engine versions (Settings → Advanced shows all versions),
redacted logs (`Logs → Copy`), and the diagnostics output
(`Ctrl+Shift+I`) — never credentials or private keys.
