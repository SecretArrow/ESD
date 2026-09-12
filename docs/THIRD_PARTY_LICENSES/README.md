# Third-party licenses

Eclipse SSH Desktop is MIT licensed (see repository `LICENSE`). It links the
following third-party libraries at runtime; each remains under its own license.

| Library | License | Used for | Link mode |
|---|---|---|---|
| Qt 6 (Core/Gui/Qml/Quick/QuickControls2/Sql/Network/Concurrent/Widgets/DBus) | LGPL-3.0 | application framework | dynamic |
| libssh | LGPL-2.1 | SSH transport backend (default) | dynamic |
| libssh2 | BSD-3-Clause | SSH transport backend (alternative) | dynamic |
| libvterm | MIT | terminal emulation | dynamic |
| OpenSSL | Apache-2.0 | TLS/crypto primitives, AES-GCM credential container | dynamic |
| zlib | zlib license | ZIP/deflate + gzip streams | dynamic |
| bzip2 | BSD-style (bzip2 2010) | tar.bz2 archives (optional) | dynamic |
| xz / liblzma | 0BSD / Public-domain | tar.xz archives (optional) | dynamic |
| Duktape | MIT | transitive via libproxy (Linux only) | dynamic |

## Notes for distributors

- Qt is used through its LGPL-3.0 terms: ship the Qt libraries dynamically and
  provide the corresponding source availability notice to recipients.
- `libssh` is LGPL-2.1 — dynamic linking keeps the application MIT-compatible.
- Static linking of LGPL components would change the obligations above and is
  not used by the shipped configuration.
- Full license texts: see the upstream projects; the Debian packages installed
  by `scripts/setup-deps.sh` also install the texts under
  `/usr/share/doc/<package>/copyright`.
