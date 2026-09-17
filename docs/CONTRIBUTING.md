# Contributing

- All CI must stay green: builds (Linux+Windows), unit tests, sshd round-trip,
  sanitizers. Warnings are errors.
- C11, one module per directory; shared logic stays platform-independent.
- Never weaken host-key verification or credential handling to make a fix
  easier (spec rule 31).
- Tests alongside features: pure-core unit tests for parsers/stores; round-trip
  tests against the local sshd testbed for network paths.
