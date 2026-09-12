#include "PuttyImporter.h"

#include <QFile>
#include <QHash>
#include <QSettings>
#include <QUrl>

#include "../logging/Logger.h"

namespace eclipse {

namespace {

// Raw PuTTY session values (strings + dwords), independent of the source
// (live registry or .reg export file).
struct PuttyValues
{
    QHash<QString, QString> strings;
    QHash<QString, int> ints;
};

QString strValue(const PuttyValues& v, const QString& key)
{
    return v.strings.value(key);
}

int intValue(const PuttyValues& v, const QString& key, int def)
{
    return v.ints.contains(key) ? v.ints.value(key) : def;
}

// Builds a profile from decoded session values. Returns an empty host when
// the session must be skipped (non-SSH protocol or no host set).
ConnectionProfile buildProfile(const QString& sessionName, const PuttyValues& v)
{
    ConnectionProfile p;

    // Only SSH sessions are imported ("default" = missing Protocol value,
    // which PuTTY itself treats as ssh).
    const QString protocol = strValue(v, QStringLiteral("Protocol")).toLower();
    if (!protocol.isEmpty() && protocol != QLatin1String("ssh") && protocol != QLatin1String("default"))
        return {};

    p.name = sessionName;
    p.host = strValue(v, QStringLiteral("HostName"));
    if (p.host.isEmpty())
        return {};
    p.port = intValue(v, QStringLiteral("PortNumber"), 22);
    if (p.port <= 0 || p.port > 65535)
        p.port = 22;
    p.username = strValue(v, QStringLiteral("UserName"));
    // Quick sessions often store "user@host" in HostName.
    const int at = p.host.indexOf(QLatin1Char('@'));
    if (at > 0 && p.username.isEmpty()) {
        p.username = p.host.left(at);
        p.host = p.host.mid(at + 1);
    }

    const QString keyFile = strValue(v, QStringLiteral("PublicKeyFile"));
    if (!keyFile.isEmpty()) {
        p.privateKeyPath = keyFile;
        p.authMethod = QStringLiteral("publickey");
    }

    // Proxy mapping, verified against the PuTTY sources (see header docs).
    QString proxyType = QStringLiteral("none");
    if (v.ints.contains(QStringLiteral("ProxyMethod"))) {
        switch (intValue(v, QStringLiteral("ProxyMethod"), 0)) {
        case 1:  proxyType = QStringLiteral("socks4"); break;
        case 2:  proxyType = QStringLiteral("socks5"); break;
        case 3:  proxyType = QStringLiteral("http"); break;
        default: proxyType = QStringLiteral("none"); break; // 0 / telnet / cmd
        }
    } else {
        const int legacy = intValue(v, QStringLiteral("ProxyType"), 0);
        if (legacy == 1)
            proxyType = QStringLiteral("http");
        else if (legacy == 3 || legacy == 4)
            proxyType = QStringLiteral("none"); // telnet / cmd: not mappable
        else if (legacy != 0)
            proxyType = intValue(v, QStringLiteral("ProxySOCKSVersion"), 5) == 4
                            ? QStringLiteral("socks4")
                            : QStringLiteral("socks5");
    }
    if (proxyType != QLatin1String("none")) {
        p.proxyType = proxyType;
        p.proxyHost = strValue(v, QStringLiteral("ProxyHost"));
        p.proxyPort = intValue(v, QStringLiteral("ProxyPort"), 80);
        p.proxyUser = strValue(v, QStringLiteral("ProxyUsername"));
    }

    // Keepalives: PingInterval (minutes) + PingIntervalSecs; total 0 = off.
    const int pingTotal = intValue(v, QStringLiteral("PingInterval"), 0) * 60
                          + intValue(v, QStringLiteral("PingIntervalSecs"), 0);
    if (pingTotal > 0 && pingTotal <= 86400)
        p.keepAliveSeconds = pingTotal;

    p.compression = intValue(v, QStringLiteral("Compression"), 0) != 0;
    return p;
}

#ifdef Q_OS_WIN
PuttyValues readRegistrySession(QSettings& putty, const QString& group)
{
    PuttyValues v;
    const QString base = group + QLatin1Char('/');
    for (const QString& key : { QStringLiteral("HostName"), QStringLiteral("Protocol"),
                                QStringLiteral("UserName"), QStringLiteral("PublicKeyFile"),
                                QStringLiteral("ProxyHost"), QStringLiteral("ProxyUsername") }) {
        if (putty.contains(base + key))
            v.strings.insert(key, putty.value(base + key).toString());
    }
    for (const QString& key : { QStringLiteral("PortNumber"), QStringLiteral("ProxyMethod"),
                                QStringLiteral("ProxyType"), QStringLiteral("ProxySOCKSVersion"),
                                QStringLiteral("PingInterval"), QStringLiteral("PingIntervalSecs"),
                                QStringLiteral("Compression") }) {
        if (putty.contains(base + key))
            v.ints.insert(key, putty.value(base + key).toInt());
    }
    return v;
}
#endif

// --- .reg export file parsing (portable import for non-Windows users) ------

QString unescapeRegString(const QString& s)
{
    QString out;
    out.reserve(s.size());
    for (int i = 0; i < s.size(); ++i) {
        const QChar c = s.at(i);
        if (c == QLatin1Char('\\') && i + 1 < s.size()) {
            const QChar n = s.at(++i);
            if (n == QLatin1Char('"') || n == QLatin1Char('\\'))
                out.append(n);
            else
                out.append(c).append(n); // keep unknown escapes verbatim
        } else {
            out.append(c);
        }
    }
    return out;
}

QByteArray parseRegHex(const QString& payload)
{
    QByteArray out;
    const QStringList parts = payload.split(QLatin1Char(','));
    for (const QString& part : parts) {
        bool ok = false;
        const uint byte = part.toUInt(&ok, 16);
        if (!ok)
            break;
        out.append(char(byte));
        if (out.size() > 64 * 1024)
            break; // sanity cap
    }
    return out;
}

void applyRegValue(PuttyValues& v, const QString& name, const QString& payload)
{
    if (name.isEmpty() || payload == QLatin1String("-"))
        return; // default value or deleted marker: not used by PuTTY sessions
    if (payload.startsWith(QLatin1Char('"'))) {
        QString body = payload.mid(1);
        if (body.endsWith(QLatin1Char('"')))
            body.chop(1);
        v.strings.insert(name, unescapeRegString(body));
        return;
    }
    if (payload.startsWith(QLatin1String("dword:"))) {
        bool ok = false;
        const qulonglong n = payload.mid(6).toULongLong(&ok, 16);
        if (ok)
            v.ints.insert(name, int(n));
        return;
    }
    if (payload.startsWith(QLatin1String("hex(2):"))) {
        // REG_EXPAND_SZ: UTF-16LE bytes (used e.g. for paths with variables)
        const QByteArray bytes = parseRegHex(payload.mid(7));
        QString s = QString::fromUtf16(reinterpret_cast<const char16_t*>(bytes.constData()),
                                       bytes.size() / 2);
        while (s.endsWith(QChar(u'\0')))
            s.chop(1);
        v.strings.insert(name, s);
        return;
    }
    // hex: (REG_BINARY) / hex(7): (REG_MULTI_SZ) / hex(b) - not needed.
}

} // namespace

QList<ConnectionProfile> PuttyImporter::importFromWindowsRegistry(QString* errorOut)
{
#ifdef Q_OS_WIN
    QSettings putty(QStringLiteral("HKEY_CURRENT_USER\\Software\\SimonTatham\\PuTTY\\Sessions"),
                    QSettings::NativeFormat);
    const QStringList groups = putty.childGroups();
    QList<ConnectionProfile> out;
    int skipped = 0;
    for (const QString& group : groups) {
        if (group.isEmpty())
            continue;
        const PuttyValues v = readRegistrySession(putty, group);
        const QString sessionName = QUrl::fromPercentEncoding(group.toUtf8());
        const ConnectionProfile p = buildProfile(sessionName, v);
        if (p.host.isEmpty())
            ++skipped; // non-SSH protocol or empty host
        else
            out.append(p);
    }
    LOG_APP(QStringLiteral("PuTTY import: %1 session(s) imported, %2 skipped")
                .arg(out.size()).arg(skipped));
    return out;
#else
    LOG_APP(QStringLiteral("PuTTY import requested on non-Windows platform"));
    if (errorOut)
        *errorOut = QStringLiteral("PuTTY registry is only present on Windows. Export your "
                                   "sessions to a .reg file on Windows and use the .reg "
                                   "import instead (PuttyImporter::importFromRegExportFile).");
    return {};
#endif
}

QList<ConnectionProfile> PuttyImporter::importFromRegExportFile(const QString& path, QString* errorOut)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly)) {
        if (errorOut)
            *errorOut = QStringLiteral("Cannot read %1: %2").arg(path, f.errorString());
        return {};
    }
    const QByteArray raw = f.readAll();

    // regedit writes UTF-16LE with a BOM ("Windows Registry Editor Version
    // 5.00"); old REGEDIT4 exports are plain ANSI/UTF-8.
    QString text;
    if (raw.size() >= 2 && uchar(raw.at(0)) == 0xFF && uchar(raw.at(1)) == 0xFE)
        text = QString::fromUtf16(reinterpret_cast<const char16_t*>(raw.constData() + 2),
                                  (raw.size() - 2) / 2);
    else
        text = QString::fromUtf8(raw);

    QList<ConnectionProfile> out;
    int skipped = 0;
    bool pendingValue = false;
    QString pendingName;
    QString pendingPayload;
    PuttyValues v;
    QString sessionName;

    auto flushValue = [&]() {
        if (pendingValue)
            applyRegValue(v, pendingName, pendingPayload);
        pendingValue = false;
        pendingName.clear();
        pendingPayload.clear();
    };
    auto flushSession = [&]() {
        flushValue();
        if (!sessionName.isEmpty()) {
            const ConnectionProfile p =
                buildProfile(QUrl::fromPercentEncoding(sessionName.toUtf8()), v);
            if (p.host.isEmpty())
                ++skipped;
            else
                out.append(p);
        }
        sessionName.clear();
        v = PuttyValues {};
    };

    const QStringList lines = text.split(QLatin1Char('\n'));
    for (const QString& rawLine : lines) {
        const QString line = rawLine.trimmed();
        if (line.isEmpty())
            continue;

        // Hex-data continuation of a wrapped multi-line value.
        if (pendingValue && !line.startsWith(QLatin1Char('['))
            && !line.startsWith(QLatin1Char('"'))) {
            pendingPayload += line;
            if (pendingPayload.endsWith(QLatin1Char('\\')))
                pendingPayload.chop(1); // more lines follow
            else
                flushValue();
            continue;
        }

        if (line.startsWith(QLatin1Char('['))) {
            // Section header: [HKEY_...\Software\SimonTatham\PuTTY\Sessions\Name]
            flushSession();
            const int close = line.lastIndexOf(QLatin1Char(']'));
            const QString section = line.mid(1, close > 0 ? close - 1 : -1);
            const QString lower = section.toLower();
            const QLatin1String marker("\\software\\simontatham\\putty\\sessions\\");
            const int cut = lower.lastIndexOf(marker);
            if (cut >= 0) {
                sessionName = section.mid(cut + marker.size());
                if (sessionName.isEmpty())
                    LOG_WARN(QStringLiteral("PuTTY .reg import: unnamed session section %1")
                                 .arg(section));
            }
            continue;
        }

        // Value line: "Name"=payload
        const int eq = line.indexOf(QLatin1String("\"="));
        if (eq < 0)
            continue;
        flushValue();
        pendingName = unescapeRegString(line.mid(1, eq - 1));
        pendingPayload = line.mid(eq + 2);
        pendingValue = true;
        if (pendingPayload.endsWith(QLatin1Char('\\')))
            pendingPayload.chop(1);
        else
            flushValue();
    }
    flushSession();

    LOG_APP(QStringLiteral("PuTTY .reg import from %1: %2 session(s) imported, %3 skipped")
                .arg(path).arg(out.size()).arg(skipped));
    return out;
}

} // namespace eclipse
