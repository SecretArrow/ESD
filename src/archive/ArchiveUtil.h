#pragma once

// ---------------------------------------------------------------------------
// ArchiveUtil.h - internal helpers shared by the archive readers.
//
// ZERO-UNSAFE-PARSING RULES for this module:
//   * every fixed-width field is read through ByteView / readLeN() which
//     bounds-check against the buffer length before touching memory;
//   * every untrusted size/offset is clamped against the constants below
//     before it is used to allocate, seek or skip;
//   * entry names are capped at kMaxEntryName, skip distances at
//     kMaxSkipBytes (1 TiB) and entry counts at kMaxEntries.
//
// Error convention: readers report failures through a `QString* err` that
// contains "<friendly message>[ | <technical detail>]". ArchiveService
// splits the two parts into an Outcome (friendly -> Outcome::friendly,
// technical -> Outcome::technical).
// ---------------------------------------------------------------------------

#include <QByteArray>
#include <QDate>
#include <QDateTime>
#include <QString>

namespace eclipse {
namespace archiveutil {

// Safety limits (defence against corrupted / malicious archives).
inline constexpr quint64 kMaxSkipBytes     = 1ULL << 40;  // 1 TiB
inline constexpr quint64 kMaxEntries       = 5000000;
inline constexpr int    kMaxEntryName      = 4096;
inline constexpr int    kMaxLinkTarget     = 4096;
inline constexpr quint64 kMaxLongNameBytes = 64 * 1024;   // GNU 'L' / PAX path
inline constexpr int    kIoChunkBytes      = 256 * 1024;  // streaming chunk
inline constexpr int    kMaxPreviewBytes   = 8 * 1024 * 1024;

inline constexpr auto kErrSeparator = " | ";

// Bounds-checked little-endian readers. Return false when [off, off+width)
// does not fit into `len` bytes.
inline bool readLe16(const uchar* p, int len, int off, quint16* v)
{
    if (!p || off < 0 || len < 0 || off + 2 > len) return false;
    *v = quint16(p[off]) | (quint16(p[off + 1]) << 8);
    return true;
}

inline bool readLe32(const uchar* p, int len, int off, quint32* v)
{
    if (!p || off < 0 || len < 0 || off + 4 > len) return false;
    quint16 lo, hi;
    readLe16(p, len, off, &lo);
    readLe16(p, len, off + 2, &hi);
    *v = quint32(lo) | (quint32(hi) << 16);
    return true;
}

inline bool readLe64(const uchar* p, int len, int off, quint64* v)
{
    if (!p || off < 0 || len < 0 || off + 8 > len) return false;
    quint32 lo, hi;
    readLe32(p, len, off, &lo);
    readLe32(p, len, off + 4, &hi);
    *v = quint64(lo) | (quint64(hi) << 32);
    return true;
}

// ---------------------------------------------------------------------------
// ByteView: a bounds-checked window over an immutable byte buffer.
// ---------------------------------------------------------------------------
class ByteView
{
public:
    ByteView() = default;
    ByteView(const uchar* data, int len) : m_data(data), m_len(len) {}
    explicit ByteView(const QByteArray& b)
        : m_data(reinterpret_cast<const uchar*>(b.constData())), m_len(b.size()) {}

    int size() const { return m_len; }

    bool u16(int off, quint16* v) const { return readLe16(m_data, m_len, off, v); }
    bool u32(int off, quint32* v) const { return readLe32(m_data, m_len, off, v); }
    bool u64(int off, quint64* v) const { return readLe64(m_data, m_len, off, v); }

    // Fixed-width, NUL/space terminated text field (tar style).
    QByteArray field(int off, int width) const
    {
        if (!m_data || off < 0) return {};
        const int end = qMin(off + width, m_len);
        int n = end - off;
        if (n <= 0) return {};
        const char* p = reinterpret_cast<const char*>(m_data + off);
        int cut = 0;
        while (cut < n && p[cut] != '\0') ++cut;
        QByteArray out(p, cut);
        while (!out.isEmpty() && out.endsWith(' ')) out.chop(1);
        return out;
    }

private:
    const uchar* m_data = nullptr;
    int m_len = 0;
};

// ---------------------------------------------------------------------------
// tar number fields: NUL/space padded octal, or GNU base-256 when the high
// bit of the first byte is set. Overflow is rejected instead of wrapping.
// ---------------------------------------------------------------------------
inline bool parseTarNumber(const char* p, int len, quint64* out)
{
    if (!p || len <= 0 || !out) return false;
    const uchar* b = reinterpret_cast<const uchar*>(p);
    if ((b[0] & 0x80) != 0) {
        quint64 v = b[0] & 0x7F;
        for (int i = 1; i < len; ++i) {
            if (v > (kMaxSkipBytes >> 8)) return false;
            v = (v << 8) | b[i];
        }
        *out = v;
        return true;
    }
    quint64 v = 0;
    bool any = false;
    for (int i = 0; i < len; ++i) {
        const char c = p[i];
        if (c == ' ' || c == '\0') {
            if (any) break;
            continue;
        }
        if (c < '0' || c > '7') return false;
        any = true;
        if (v > (kMaxSkipBytes >> 3)) return false;
        v = (v << 3) | quint64(c - '0');
    }
    if (!any) return false;
    *out = v;
    return true;
}

inline quint64 roundUp512(quint64 v)
{
    return (v / 512 + (v % 512 ? 1 : 0)) * 512;
}

// ---------------------------------------------------------------------------
// DOS date/time (ZIP). Interpreted as UTC so that displayed timestamps are
// deterministic regardless of the viewer machine's time zone.
// ---------------------------------------------------------------------------
inline qint64 dosDateTimeToEpoch(quint16 dosDate, quint16 dosTime)
{
    if (dosDate == 0 && dosTime == 0) return 0;
    const int year   = 1980 + int((dosDate >> 9) & 0x7F);
    const int month  = int((dosDate >> 5) & 0x0F);
    const int day    = int(dosDate & 0x1F);
    const int hour   = int((dosTime >> 11) & 0x1F);
    const int minute = int((dosTime >> 5) & 0x3F);
    const int sec    = int(dosTime & 0x1F) * 2;
    const QDate d(year, month, day);
    const QTime t(hour, minute, sec);
    if (!d.isValid() || !t.isValid()) return 0;
    return QDateTime(d, t, Qt::UTC).toSecsSinceEpoch();
}

// ---------------------------------------------------------------------------
// Text decoding: strict UTF-8 when valid, Latin-1 as a lossless fallback.
// ---------------------------------------------------------------------------
inline bool isValidUtf8(const QByteArray& raw)
{
    const int n = raw.size();
    const uchar* p = reinterpret_cast<const uchar*>(raw.constData());
    int i = 0;
    while (i < n) {
        const uchar c = p[i];
        if (c < 0x80) { ++i; continue; }
        int need;
        quint32 cp;
        if ((c & 0xE0) == 0xC0) { need = 1; cp = c & 0x1F; }
        else if ((c & 0xF0) == 0xE0) { need = 2; cp = c & 0x0F; }
        else if ((c & 0xF8) == 0xF0) { need = 3; cp = c & 0x07; }
        else return false;
        if (n - i - 1 < need) return false;
        for (int k = 1; k <= need; ++k) {
            if ((p[i + k] & 0xC0) != 0x80) return false;
            cp = (cp << 6) | quint32(p[i + k] & 0x3F);
        }
        if ((need == 1 && cp < 0x80) || (need == 2 && cp < 0x800) ||
            (need == 3 && cp < 0x10000) || cp > 0x10FFFF ||
            (cp >= 0xD800 && cp <= 0xDFFF)) return false;
        i += need + 1;
    }
    return true;
}

inline QString decodeName(const QByteArray& raw)
{
    if (raw.isEmpty()) return {};
    if (isValidUtf8(raw)) return QString::fromUtf8(raw);
    return QString::fromLatin1(raw);
}

// Normalises an in-archive path for DISPLAY/LISTING: backslashes become
// slashes, leading slashes and "./" prefixes are dropped. Unsafe components
// are NOT rejected here (listing must never hide what an archive contains).
inline QString normalizeArchivePath(const QString& name)
{
    QString s = name;
    s.replace(u'\\', u'/');
    if (s.size() >= 2 && s[1] == u':') s.remove(0, 2);
    while (s.startsWith(u'/')) s.remove(0, 1);
    while (s.startsWith(u"./")) s.remove(0, 2);
    return s;
}

// Zip-slip protection for EXTRACTION: rejects ".." components, absolute
// paths and empty results. Returns false when the path is unsafe.
inline bool sanitizeArchivePath(const QString& name, QString* clean)
{
    QString s = name;
    s.replace(u'\\', u'/');
    if (s.size() >= 2 && s[1] == u':') s.remove(0, 2);
    while (s.startsWith(u'/')) s.remove(0, 1);
    QStringList parts;
    const QStringList segs = s.split(u'/', Qt::SkipEmptyParts);
    parts.reserve(segs.size());
    for (const QString& seg : segs) {
        if (seg == QLatin1String(".")) continue;
        if (seg == QLatin1String("..")) return false;
        parts << seg;
    }
    *clean = parts.join(u'/');
    return !clean->isEmpty();
}

// Error helper: composes "friendly | technical" into the err out-param.
inline void setError(QString* err, const QString& friendly, const QString& technical = {})
{
    if (!err) return;
    if (technical.isEmpty()) *err = friendly;
    else *err = friendly + QLatin1String(kErrSeparator) + technical;
}

inline QString corruptedError(const QString& detail)
{
    return QStringLiteral("This archive appears to be corrupted (%1).").arg(detail);
}

} // namespace archiveutil
} // namespace eclipse
