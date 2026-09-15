#pragma once

#include <QList>
#include <QString>

#include "ConnectionProfile.h"

namespace eclipse {

// ---------------------------------------------------------------------------
// PuttyImporter - imports PuTTY saved sessions into ConnectionProfiles.
//
// Windows source: sessions live in the registry under
//   HKCU\Software\SimonTatham\PuTTY\Sessions\<SessionKey>
// where <SessionKey> is a URL-encoded name (e.g. "My%20Server"). Read via
// QSettings(QSettings::NativeFormat); names are decoded with
// QUrl::fromPercentEncoding().
//
// Registry value -> ConnectionProfile mapping (only existing fields used):
//   Session key (percent-decoded) -> name
//   HostName        -> host (a leading "user@" is split into username when
//                      UserName is absent, like PuTTY's quick sessions)
//   PortNumber      -> port (default 22)
//   UserName        -> username
//   PublicKeyFile   -> privateKeyPath (+ authMethod = "publickey")
//   ProxyMethod     -> proxyType/proxyHost/proxyPort/proxyUser (see below)
//   PingInterval + PingIntervalSecs -> keepAliveSeconds (only when > 0)
//   Compression     -> compression (0/1)
//
// Proxy values verified against the PuTTY sources (putty.h / settings.c):
//   * modern PuTTY (>= 0.76) writes ProxyMethod with the enum
//       PROXY_NONE=0, PROXY_SOCKS4=1, PROXY_SOCKS5=2, PROXY_HTTP=3
//     (PROXY_TELNET=4 / PROXY_CMD=5 / PROXY_FUZZ=6 are not mappable and are
//     treated as "none")
//   * older sessions carry the legacy "ProxyType" value which PuTTY's own
//     loader maps as: 0=none, 1=http, 2=socks (version decided by
//     ProxySOCKSVersion, default 5), 3=telnet, 4=cmd; that same mapping is
//     applied here. Proxy fields are only filled for non-"none" types.
//
// Only "ssh" sessions are imported ("Protocol" missing/empty counts as ssh,
// matching PuTTY's own default); telnet / serial / raw / rlogin sessions are
// counted as skipped.
//
// Error handling: never throws; a human-readable note is written to errorOut.
// ---------------------------------------------------------------------------
class PuttyImporter
{
public:
    // Windows registry import. On non-Windows platforms returns an empty list
    // and sets errorOut to "PuTTY registry is only present on Windows ..." so
    // callers can degrade gracefully instead of failing hard.
    static QList<ConnectionProfile> importFromWindowsRegistry(QString* errorOut = nullptr);

    // Bonus path for non-Windows users: parse a PuTTY "Registry Editor"
    // export (.reg file, UTF-16LE-with-BOM or ANSI/UTF-8 text). Sessions are
    // taken from [HKEY_...\Software\SimonTatham\PuTTY\Sessions\<Name>]
    // sections; dword and string (incl. hex(2) UTF-16LE) values are read,
    // other value kinds are ignored. Same mapping as the registry import.
    static QList<ConnectionProfile> importFromRegExportFile(const QString& path,
                                                            QString* errorOut = nullptr);
};

} // namespace eclipse
