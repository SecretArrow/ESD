#include "Archive.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>

#include <zlib.h>
#if defined(ECLIPSE_HAVE_BZLIB)
#include <bzlib.h>
#endif
#if defined(ECLIPSE_HAVE_LZMA)
#include <lzma.h>
#endif

#include "../common/Utils.h"
#include "../core/logging/Logger.h"

namespace eclipse {

namespace {

quint16 rd16(const QByteArray& b, int pos)
{
    if (pos + 2 > b.size())
        return 0;
    return quint16(quint8(b[pos]) | (quint16(quint8(b[pos + 1])) << 8));
}

quint32 rd32(const QByteArray& b, int pos)
{
    if (pos + 4 > b.size())
        return 0;
    return quint32(quint8(b[pos]))
           | (quint32(quint8(b[pos + 1])) << 8)
           | (quint32(quint8(b[pos + 2])) << 16)
           | (quint32(quint8(b[pos + 3])) << 24);
}

qint64 dosTimeToEpoch(quint16 d, quint16 t)
{
    const int year = 1980 + ((d >> 9) & 0x7f);
    const int month = (d >> 5) & 0x0f;
    const int day = d & 0x1f;
    const int hour = (t >> 11) & 0x1f;
    const int minute = (t >> 5) & 0x3f;
    const int sec = (t & 0x1f) * 2;
    return QDateTime(QDate(year, qMax(1, month), qMax(1, day)),
                     QTime(hour, minute, sec), Qt::UTC)
        .toSecsSinceEpoch();
}

Outcome inflateRawTo(QIODevice* out, const QByteArray& compressed, quint64 expectedSize, quint32 expectedCrc)
{
    z_stream zs;
    memset(&zs, 0, sizeof(zs));
    if (inflateInit2(&zs, -15) != Z_OK)
        return Outcome::fail(QStringLiteral("Cannot initialize the decompressor."));

    QByteArray dst;
    dst.resize(64 * 1024);
    uLong crc = crc32(0, nullptr, 0);
    quint64 produced = 0;

    zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(compressed.constData()));
    zs.avail_in = uInt(compressed.size());
    int rc = Z_OK;
    while (rc != Z_STREAM_END) {
        zs.next_out = reinterpret_cast<Bytef*>(dst.data());
        zs.avail_out = uInt(dst.size());
        rc = inflate(&zs, Z_NO_FLUSH);
        if (rc != Z_OK && rc != Z_STREAM_END) {
            inflateEnd(&zs);
            return Outcome::fail(QStringLiteral("This archive entry is corrupted (deflate error)."),
                                 QStringLiteral("zlib rc=%1").arg(rc));
        }
        const int got = int(dst.size() - zs.avail_out);
        if (got > 0) {
            crc = crc32(crc, reinterpret_cast<const Bytef*>(dst.constData()), uInt(got));
            produced += quint64(got);
            if (produced > expectedSize + 1024 * 1024 * 64) { // safety valve
                inflateEnd(&zs);
                return Outcome::fail(QStringLiteral("Entry decompressed beyond its declared size."));
            }
            if (out)
                out->write(dst.constData(), got);
        }
        if (rc == Z_OK && zs.avail_in == 0 && got == 0) {
            inflateEnd(&zs);
            return Outcome::fail(QStringLiteral("Unexpected end of compressed data."));
        }
    }
    inflateEnd(&zs);
    if (quint64(crc) != expectedCrc)
        return Outcome::fail(QStringLiteral("Integrity check failed (CRC mismatch) for this entry."),
                             QStringLiteral("crc stored=%1 got=%2").arg(expectedCrc).arg(crc));
    Q_UNUSED(expectedSize);
    return Outcome::success();
}

} // namespace

// ---------------------------------------------------------------------------
// LocalFileArchiveSource
// ---------------------------------------------------------------------------
LocalFileArchiveSource::LocalFileArchiveSource(const QString& path)
{
    m_file.setFileName(path);
    m_file.open(QIODevice::ReadOnly);
}

quint64 LocalFileArchiveSource::totalSize() const
{
    return m_file.isOpen() ? quint64(m_file.size()) : 0;
}

bool LocalFileArchiveSource::readAt(quint64 offset, int len, QByteArray* out, QString* err)
{
    if (!m_file.isOpen() || !m_file.seek(qint64(offset))) {
        if (err)
            *err = QStringLiteral("Cannot seek in local file.");
        return false;
    }
    *out = m_file.read(len);
    return true;
}

int LocalFileArchiveSource::readNext(int maxLen, QByteArray* out, QString* err)
{
    if (!m_file.isOpen()) {
        if (err)
            *err = QStringLiteral("File is not open.");
        return -1;
    }
    *out = m_file.read(maxLen);
    return out->size();
}

bool LocalFileArchiveSource::rewind(QString*)
{
    return m_file.isOpen() ? m_file.seek(0) : false;
}

// ---------------------------------------------------------------------------
// SftpArchiveSource
// ---------------------------------------------------------------------------
SftpArchiveSource::SftpArchiveSource(std::shared_ptr<ISftpSession> sftp, const QString& remotePath)
    : m_sftp(std::move(sftp))
{
    if (!m_sftp)
        return;
    quint64 size = 0;
    QString err;
    m_handle = m_sftp->openForRead(remotePath, &size, &err);
    m_size = size;
}

SftpArchiveSource::~SftpArchiveSource()
{
    if (m_handle && m_sftp)
        m_sftp->closeFile(m_handle);
}

bool SftpArchiveSource::readAt(quint64 offset, int len, QByteArray* out, QString* err)
{
    if (!m_handle || !m_sftp)
        return false;
    fillWindow(offset);
    if (!m_windowValid)
        return false;
    const int inWindow = int(m_windowOffset + quint64(m_window.size()) - offset);
    if (len > inWindow)
        len = qMax(0, inWindow);
    const int start = int(offset - m_windowOffset);
    *out = m_window.mid(start, len);
    return true;
}

void SftpArchiveSource::fillWindow(quint64 offset)
{
    if (m_windowValid && offset >= m_windowOffset
        && offset < m_windowOffset + quint64(m_window.size()))
        return;
    QString err;
    if (!m_sftp->seekFile(m_handle, offset))
        return;
    m_window.resize(kWindow);
    int got = 0;
    while (got < kWindow) {
        const int n = m_sftp->readFile(m_handle, m_window.data() + got, kWindow - got, &err);
        if (n <= 0)
            break;
        got += n;
    }
    m_window.resize(got);
    m_windowOffset = offset;
    m_windowValid = got > 0;
}

int SftpArchiveSource::readNext(int maxLen, QByteArray* out, QString* err)
{
    if (!m_handle || !m_sftp || m_seqPos >= m_size)
        return 0;
    QByteArray chunk;
    chunk.resize(maxLen);
    int got = 0;
    while (got < maxLen) {
        const int n = m_sftp->readFile(m_handle, chunk.data() + got, maxLen - got, err);
        if (n <= 0)
            break;
        got += n;
    }
    if (got <= 0)
        return 0;
    m_seqPos += quint64(got);
    *out = chunk.left(got);
    return got;
}

bool SftpArchiveSource::rewind(QString* err)
{
    if (!m_handle)
        return false;
    m_seqPos = 0;
    m_windowValid = false;
    return m_sftp->seekFile(m_handle, 0);
}

// ---------------------------------------------------------------------------
// ZipReader
// ---------------------------------------------------------------------------
bool ZipReader::open(ArchiveSource* source, QString* err)
{
    m_source = source;
    m_entries.clear();
    m_localHeaderOffsets.clear();
    m_methods.clear();
    if (!source->isSeekable()) {
        if (err)
            *err = QStringLiteral("ZIP listing requires a seekable source.");
        return false;
    }

    const quint64 total = source->totalSize();
    const int tail = int(qMin<quint64>(total, 64 * 1024));
    QByteArray tailBuf;
    if (!source->readAt(total - quint64(tail), tail, &tailBuf, err))
        return false;

    // Find EOCD signature 0x06054b50 scanning backwards.
    int eocd = -1;
    for (int i = tailBuf.size() - 22; i >= 0; --i) {
        if (rd32(tailBuf, i) == 0x06054b50u) {
            eocd = i;
            break;
        }
    }
    if (eocd < 0) {
        if (err)
            *err = QStringLiteral("This file does not look like a ZIP archive (end-of-central-directory not found).");
        return false;
    }
    m_eocdOffset = quint64(total - tail) + quint64(eocd);
    const int entryCount = int(rd16(tailBuf, eocd + 10));
    const quint32 cdSize = rd32(tailBuf, eocd + 12);
    const quint32 cdOffset = rd32(tailBuf, eocd + 16);
    Q_UNUSED(entryCount);

    QByteArray cd;
    if (!source->readAt(cdOffset, int(cdSize), &cd, err))
        return false;

    int pos = 0;
    while (pos + 46 <= cd.size() && rd32(cd, pos) == 0x02014b50u) {
        ArchiveEntry e;
        const quint16 method = rd16(cd, pos + 10);
        const quint32 csize = rd32(cd, pos + 20);
        const quint32 usize = rd32(cd, pos + 24);
        const quint16 nameLen = rd16(cd, pos + 28);
        const quint16 extraLen = rd16(cd, pos + 30);
        const quint16 commentLen = rd16(cd, pos + 32);
        const quint16 dostime = rd16(cd, pos + 12);
        const quint16 dosdate = rd16(cd, pos + 14);
        const quint32 lho = rd32(cd, pos + 42);

        QString name = QString::fromUtf8(cd.mid(pos + 46, nameLen));
        e.isDir = name.endsWith(QLatin1Char('/'));
        if (e.isDir)
            name.chop(1);
        e.path = name;
        e.size = usize;
        e.compressedSize = csize;
        e.mtime = dosTimeToEpoch(dosdate, dostime);

        m_entries.append(e);
        m_localHeaderOffsets.append(lho);
        m_methods.append(method);

        pos += 46 + nameLen + extraLen + commentLen;
    }
    return !m_entries.isEmpty();
}

bool ZipReader::extractEntry(ArchiveSource* src, const QString& entryPath,
                             const QString& localDest, QString* err)
{
    int found = -1;
    for (int i = 0; i < m_entries.size(); ++i) {
        if (m_entries[i].path == entryPath) {
            found = i;
            break;
        }
    }
    if (found < 0) {
        if (err)
            *err = QStringLiteral("Entry not found in archive: %1").arg(entryPath);
        return false;
    }
    if (m_entries[found].isDir) {
        QDir().mkpath(localDest);
        return true;
    }
    QDir().mkpath(QFileInfo(localDest).absolutePath());

    // Parse the local header at the known offset to find where data starts.
    QByteArray lh;
    const quint64 lho = m_localHeaderOffsets[found];
    if (!src->readAt(lho, 30, &lh, err))
        return false;
    if (rd32(lh, 0) != 0x04034b50u) {
        if (err)
            *err = QStringLiteral("Archive structure is corrupted (bad local header).");
        return false;
    }
    const quint16 nameLen = rd16(lh, 26);
    const quint16 extraLen = rd16(lh, 28);
    const quint64 dataOffset = lho + 30 + nameLen + extraLen;

    QByteArray compressed;
    if (!src->readAt(dataOffset, int(m_entries[found].compressedSize), &compressed, err))
        return false;

    QSaveFile out(localDest);
    if (!out.open(QIODevice::WriteOnly)) {
        if (err)
            *err = QStringLiteral("Cannot write %1: %2").arg(localDest, out.errorString());
        return false;
    }
    Outcome oc;
    if (m_methods[found] == 0) { // stored
        if (quint64(compressed.size()) != m_entries[found].size) {
            if (err)
                *err = QStringLiteral("Entry size mismatch while reading.");
            return false;
        }
        out.write(compressed);
        oc = Outcome::success();
    } else if (m_methods[found] == 8) { // deflate
        oc = inflateRawTo(&out, compressed, m_entries[found].size, 0);
    } else {
        oc = Outcome::fail(QStringLiteral("Unsupported ZIP compression method (%1).").arg(m_methods[found]));
    }
    if (!oc.ok) {
        out.cancelWriting();
        if (err)
            *err = oc.friendly;
        return false;
    }
    return out.commit();
}

bool ZipReader::readTextPreview(ArchiveSource* src, const QString& entryPath,
                                int maxBytes, QByteArray* out, QString* err)
{
    // find and read, capped
    int found = -1;
    for (int i = 0; i < m_entries.size(); ++i)
        if (m_entries[i].path == entryPath) {
            found = i;
            break;
        }
    if (found < 0)
        return false;
    QByteArray lh;
    const quint64 lho = m_localHeaderOffsets[found];
    if (!src->readAt(lho, 30, &lh, err))
        return false;
    const quint16 nameLen = rd16(lh, 26);
    const quint16 extraLen = rd16(lh, 28);
    const quint64 dataOffset = lho + 30 + nameLen + extraLen;
    const int want = int(qMin<quint64>(maxBytes, m_entries[found].size));
    QByteArray raw;
    if (!src->readAt(dataOffset, want, &raw, err))
        return false;
    if (m_methods[found] == 8) {
        z_stream zs;
        memset(&zs, 0, sizeof(zs));
        inflateInit2(&zs, -15);
        zs.next_in = reinterpret_cast<Bytef*>(const_cast<char*>(raw.constData()));
        zs.avail_in = uInt(raw.size());
        QByteArray dst;
        dst.resize(want + 64);
        zs.next_out = reinterpret_cast<Bytef*>(dst.data());
        zs.avail_out = uInt(dst.size());
        inflate(&zs, Z_FINISH);
        const int got = int(dst.size() - zs.avail_out);
        inflateEnd(&zs);
        *out = dst.left(got);
    } else {
        *out = raw;
    }
    return true;
}

// ---------------------------------------------------------------------------
// TarReader
// ---------------------------------------------------------------------------
TarReader::TarReader(bool compressed)
    : m_compressed(compressed)
{
}

TarReader::~TarReader()
{
    if (m_zStream) {
        if (m_zAlgo == 1)
            inflateEnd(static_cast<z_stream*>(m_zStream));
        delete static_cast<z_stream*>(m_zStream);
        m_zStream = nullptr;
    }
}

bool TarReader::ensureStream()
{
    if (m_streamStarted || !m_compressed)
        return true;
    m_zStream = new z_stream();
    memset(m_zStream, 0, sizeof(z_stream));
    if (inflateInit2(static_cast<z_stream*>(m_zStream), 15 + 32) != Z_OK) { // auto gzip
        delete static_cast<z_stream*>(m_zStream);
        m_zStream = nullptr;
        return false;
    }
    m_zAlgo = 1;
    m_streamStarted = true;
    return true;
}

int TarReader::streamRead(char* buf, int len, QString* err)
{
    if (!m_compressed) {
        QByteArray chunk;
        if (!m_source->readNext(len, &chunk, err))
            return -1;
        memcpy(buf, chunk.constData(), size_t(chunk.size()));
        return chunk.size();
    }
    if (!ensureStream()) {
        if (err)
            *err = QStringLiteral("Cannot initialize the decompressor.");
        return -1;
    }
    z_stream* zs = static_cast<z_stream*>(m_zStream);
    zs->next_out = reinterpret_cast<Bytef*>(buf);
    zs->avail_out = uInt(len);
    while (zs->avail_out == uInt(len)) {
        if (zs->avail_in == 0 && !m_streamDone) {
            QByteArray in;
            const int n = m_source->readNext(64 * 1024, &in, err);
            if (n <= 0) {
                if (zs->avail_out == uInt(len))
                    return 0;
                break;
            }
            m_inBuffer.append(in);
            zs->next_in = reinterpret_cast<Bytef*>(m_inBuffer.data());
            zs->avail_in = uInt(m_inBuffer.size());
        }
        const int rc = inflate(zs, Z_NO_FLUSH);
        if (rc == Z_STREAM_END) {
            m_streamDone = true;
            break;
        }
        if (rc != Z_OK) {
            if (err)
                *err = QStringLiteral("Compressed archive data is corrupted (zlib rc=%1).").arg(rc);
            return -1;
        }
    }
    const int got = int(len - zs->avail_out);
    if (got > 0) {
        const int consumed = int(zs->next_in - reinterpret_cast<const Bytef*>(m_inBuffer.constData()));
        m_inBuffer.remove(0, consumed);
    }
    return got;
}

bool TarReader::open(ArchiveSource* source, QString* err)
{
    m_source = source;
    m_entries.clear();
    m_offsets.clear();
    if (m_compressed && !source->rewind(err))
        return false;
    return scanAll(err);
}

bool TarReader::scanAll(QString* err)
{
    QByteArray header(512, 0);
    quint64 offset = 0;
    QString pendingLongName;

    for (;;) {
        int got = 0;
        while (got < 512) {
            const int n = streamRead(header.data() + got, 512 - got, err);
            if (n <= 0)
                return got == 512 || !m_entries.isEmpty();
            got += n;
        }
        offset += 512;

        // all-zero block = end (single check; two blocks expected)
        static const char zeros[512] = {};
        if (memcmp(header.constData(), zeros, 512) == 0)
            break;

        const QByteArray nameField = header.mid(0, 100);
        const quint64 sizeField = quint64(QByteArray(header.mid(124, 12)).trimmed().toULongLong(nullptr, 8));
        const char typeFlag = header.at(156);
        const quint64 mtime = QByteArray(header.mid(136, 12)).trimmed().toULongLong(nullptr, 8);

        QString name;
        if (typeFlag == 'L') {
            // GNU long name: next entry holds the real name
            QByteArray longBuf(int(sizeField), 0);
            int lgot = 0;
            while (lgot < int(sizeField)) {
                const int n = streamRead(longBuf.data() + lgot, int(sizeField) - lgot, err);
                if (n <= 0)
                    return false;
                lgot += n;
            }
            pendingLongName = QString::fromUtf8(longBuf).trimmed();
            offset += (sizeField + 511) / 512 * 512;
            continue;
        }

        name = pendingLongName.isEmpty()
                   ? QString::fromUtf8(nameField).split(QChar::fromLatin1('\0')).value(0)
                   : pendingLongName;
        pendingLongName.clear();
        if (name.isEmpty())
            continue;

        ArchiveEntry e;
        e.path = name;
        e.size = sizeField;
        e.mtime = qint64(mtime);
        e.isDir = (typeFlag == '5') || name.endsWith(QLatin1Char('/'));
        m_entries.append(e);
        m_offsets.insert(name, offset);

        const quint64 dataBlocks = (sizeField + 511) / 512 * 512;
        // skip data by reading through the stream
        quint64 skipped = 0;
        QByteArray buf;
        buf.resize(64 * 1024);
        while (skipped < dataBlocks) {
            const int want = int(qMin<quint64>(buf.size(), dataBlocks - skipped));
            const int n = streamRead(buf.data(), want, err);
            if (n <= 0)
                break;
            skipped += quint64(n);
        }
        offset += dataBlocks;
    }
    return true;
}

bool TarReader::extractEntry(const QString& entryPath, const QString& localDest, QString* err)
{
    // Sequential re-scan extracting only the requested entry.
    if (!m_source->rewind(err))
        return false;
    m_streamDone = false;
    m_streamStarted = false;
    if (m_zStream) {
        inflateEnd(static_cast<z_stream*>(m_zStream));
        delete static_cast<z_stream*>(m_zStream);
        m_zStream = nullptr;
    }

    QByteArray header(512, 0);
    QString pendingLongName;
    quint64 offset = 0;
    for (;;) {
        int got = 0;
        while (got < 512) {
            const int n = streamRead(header.data() + got, 512 - got, err);
            if (n <= 0)
                return false;
            got += n;
        }
        offset += 512;
        static const char zeros[512] = {};
        if (memcmp(header.constData(), zeros, 512) == 0)
            break;

        const quint64 sizeField = quint64(QByteArray(header.mid(124, 12)).trimmed().toULongLong(nullptr, 8));
        const char typeFlag = header.at(156);

        QString name;
        if (typeFlag == 'L') {
            QByteArray longBuf(int(sizeField), 0);
            int lgot = 0;
            while (lgot < int(sizeField)) {
                const int n = streamRead(longBuf.data() + lgot, int(sizeField) - lgot, err);
                if (n <= 0)
                    return false;
                lgot += n;
            }
            pendingLongName = QString::fromUtf8(longBuf).trimmed();
            offset += (sizeField + 511) / 512 * 512;
            continue;
        }
        name = pendingLongName.isEmpty()
                   ? QString::fromUtf8(header.mid(0, 100)).split(QChar::fromLatin1('\0')).value(0)
                   : pendingLongName;
        pendingLongName.clear();

        const quint64 dataBlocks = (sizeField + 511) / 512 * 512;
        if (name == entryPath) {
            if (typeFlag == '5') {
                QDir().mkpath(localDest);
                return true;
            }
            QDir().mkpath(QFileInfo(localDest).absolutePath());
            QSaveFile out(localDest);
            if (!out.open(QIODevice::WriteOnly)) {
                if (err)
                    *err = QStringLiteral("Cannot write %1: %2").arg(localDest, out.errorString());
                return false;
            }
            quint64 remaining = sizeField;
            QByteArray buf;
            buf.resize(64 * 1024);
            while (remaining > 0) {
                const int want = int(qMin<quint64>(buf.size(), remaining));
                const int n = streamRead(buf.data(), want, err);
                if (n <= 0)
                    break;
                out.write(buf.constData(), n);
                remaining -= quint64(n);
            }
            return out.commit();
        }
        // skip
        quint64 skipped = 0;
        QByteArray buf;
        buf.resize(64 * 1024);
        while (skipped < dataBlocks) {
            const int want = int(qMin<quint64>(buf.size(), dataBlocks - skipped));
            const int n = streamRead(buf.data(), want, err);
            if (n <= 0)
                break;
            skipped += quint64(n);
        }
        offset += dataBlocks;
    }
    if (err)
        *err = QStringLiteral("Entry not found: %1").arg(entryPath);
    return false;
}

bool TarReader::readTextPreview(const QString& entryPath, int maxBytes, QByteArray* out, QString* err)
{
    // Simple approach: extract to memory capped at maxBytes by scanning.
    if (!m_source->rewind(err))
        return false;
    m_streamDone = false;
    m_streamStarted = false;
    if (m_zStream) {
        inflateEnd(static_cast<z_stream*>(m_zStream));
        delete static_cast<z_stream*>(m_zStream);
        m_zStream = nullptr;
    }
    QByteArray header(512, 0);
    QString pendingLongName;
    for (;;) {
        int got = 0;
        while (got < 512) {
            const int n = streamRead(header.data() + got, 512 - got, err);
            if (n <= 0)
                return false;
            got += n;
        }
        static const char zeros[512] = {};
        if (memcmp(header.constData(), zeros, 512) == 0)
            break;
        const quint64 sizeField = quint64(QByteArray(header.mid(124, 12)).trimmed().toULongLong(nullptr, 8));
        const char typeFlag = header.at(156);
        QString name;
        if (typeFlag == 'L') {
            QByteArray longBuf(int(sizeField), 0);
            int lgot = 0;
            while (lgot < int(sizeField)) {
                const int n = streamRead(longBuf.data() + lgot, int(sizeField) - lgot, err);
                if (n <= 0)
                    return false;
                lgot += n;
            }
            pendingLongName = QString::fromUtf8(longBuf).trimmed();
            continue;
        }
        name = pendingLongName.isEmpty()
                   ? QString::fromUtf8(header.mid(0, 100)).split(QChar::fromLatin1('\0')).value(0)
                   : pendingLongName;
        pendingLongName.clear();
        const quint64 dataBlocks = (sizeField + 511) / 512 * 512;
        if (name == entryPath && typeFlag != '5') {
            const int want = int(qMin<quint64>(quint64(maxBytes), sizeField));
            out->resize(want);
            int rgot = 0;
            while (rgot < want) {
                const int n = streamRead(out->data() + rgot, want - rgot, err);
                if (n <= 0)
                    break;
                rgot += n;
            }
            out->resize(rgot);
            return true;
        }
        quint64 skipped = 0;
        QByteArray buf;
        buf.resize(64 * 1024);
        while (skipped < dataBlocks) {
            const int want = int(qMin<quint64>(buf.size(), dataBlocks - skipped));
            const int n = streamRead(buf.data(), want, err);
            if (n <= 0)
                break;
            skipped += quint64(n);
        }
    }
    return false;
}

// ---------------------------------------------------------------------------
// ArchiveService
// ---------------------------------------------------------------------------
ArchiveService::Kind ArchiveService::detect(const QString& fileName)
{
    const QString n = fileName.toLower();
    if (n.endsWith(QLatin1String(".zip")))
        return Kind::Zip;
    if (n.endsWith(QLatin1String(".tar.gz")) || n.endsWith(QLatin1String(".tgz")))
        return Kind::TarGz;
    if (n.endsWith(QLatin1String(".tar.bz2")))
        return Kind::TarBz2;
    if (n.endsWith(QLatin1String(".tar.xz")))
        return Kind::TarXz;
    if (n.endsWith(QLatin1String(".tar")))
        return Kind::Tar;
    if (n.endsWith(QLatin1String(".7z")))
        return Kind::SevenZip;
    return Kind::Unknown;
}

QString ArchiveService::kindName(Kind k)
{
    switch (k) {
    case Kind::Zip: return QStringLiteral("ZIP");
    case Kind::Tar: return QStringLiteral("TAR");
    case Kind::TarGz:
    case Kind::Tgz: return QStringLiteral("TAR.GZ");
    case Kind::TarBz2: return QStringLiteral("TAR.BZ2");
    case Kind::TarXz: return QStringLiteral("TAR.XZ");
    case Kind::SevenZip: return QStringLiteral("7Z");
    default: return QStringLiteral("?");
    }
}

Outcome ArchiveService::listRemote(std::shared_ptr<ISftpSession> sftp, const QString& remotePath,
                                   QVector<ArchiveEntry>* entries, QString* engineNote)
{
    const Kind kind = detect(remotePath);
    if (kind == Kind::Unknown)
        return Outcome::fail(QStringLiteral("Unsupported archive format."));
    if (kind == Kind::SevenZip)
        return Outcome::fail(QStringLiteral("7Z archives are planned (Coming Soon)."),
                             QStringLiteral("no 7z backend"));

    SftpArchiveSource source(sftp, remotePath);
    if (!source.isValid())
        return Outcome::fail(QStringLiteral("Cannot open the remote archive for reading."));

    if (kind == Kind::Zip) {
        ZipReader reader;
        QString err;
        if (!reader.open(&source, &err))
            return Outcome::fail(err, QStringLiteral("zip open failed"));
        *entries = reader.entries();
        if (engineNote)
            *engineNote = QStringLiteral("Listed via central directory (tail reads only).");
        return Outcome::success();
    }

    TarReader reader(kind != Kind::Tar);
    QString err;
    if (!reader.open(&source, &err))
        return Outcome::fail(err.isEmpty() ? QStringLiteral("Cannot parse this TAR archive.") : err,
                             QStringLiteral("tar scan failed"));
    *entries = reader.entries();
    if (engineNote && kind != Kind::Tar)
        *engineNote = QStringLiteral("Compressed TAR was streamed (sequential) - no full download to disk.");
    return Outcome::success();
}

Outcome ArchiveService::listLocal(const QString& path, QVector<ArchiveEntry>* entries)
{
    const Kind kind = detect(path);
    if (kind == Kind::Unknown)
        return Outcome::fail(QStringLiteral("Unsupported archive format."));
    if (kind == Kind::SevenZip)
        return Outcome::fail(QStringLiteral("7Z archives are planned (Coming Soon)."));

    LocalFileArchiveSource source(path);
    if (!source.isOpen())
        return Outcome::fail(QStringLiteral("Cannot open %1").arg(path));

    if (kind == Kind::Zip) {
        ZipReader reader;
        QString err;
        if (!reader.open(&source, &err))
            return Outcome::fail(err);
        *entries = reader.entries();
        return Outcome::success();
    }
    TarReader reader(kind != Kind::Tar);
    QString err;
    if (!reader.open(&source, &err))
        return Outcome::fail(err);
    *entries = reader.entries();
    return Outcome::success();
}

Outcome ArchiveService::extractRemote(std::shared_ptr<ISftpSession> sftp, const QString& remotePath,
                                      const QStringList& entryPaths, const QString& localDir)
{
    const Kind kind = detect(remotePath);
    SftpArchiveSource source(sftp, remotePath);
    if (!source.isValid())
        return Outcome::fail(QStringLiteral("Cannot open the remote archive."));
    QDir().mkpath(localDir);

    if (kind == Kind::Zip) {
        ZipReader reader;
        QString err;
        if (!reader.open(&source, &err))
            return Outcome::fail(err);
        for (const QString& entry : entryPaths) {
            QFileInfo fi(entry);
            const QString dest = localDir + QLatin1Char('/') + fi.fileName();
            if (!reader.extractEntry(&source, entry, dest, &err))
                return Outcome::fail(err);
        }
        return Outcome::success();
    }
    TarReader reader(kind != Kind::Tar);
    QString err;
    if (!reader.open(&source, &err))
        return Outcome::fail(err);
    for (const QString& entry : entryPaths) {
        QFileInfo fi(entry);
        const QString dest = localDir + QLatin1Char('/') + fi.fileName();
        if (!reader.extractEntry(entry, dest, &err))
            return Outcome::fail(err);
    }
    return Outcome::success();
}

Outcome ArchiveService::extractLocal(const QString& archivePath, const QStringList& entryPaths,
                                     const QString& localDir)
{
    const Kind kind = detect(archivePath);
    LocalFileArchiveSource source(archivePath);
    if (!source.isOpen())
        return Outcome::fail(QStringLiteral("Cannot open %1").arg(archivePath));
    QDir().mkpath(localDir);

    if (kind == Kind::Zip) {
        ZipReader reader;
        QString err;
        if (!reader.open(&source, &err))
            return Outcome::fail(err);
        for (const QString& entry : entryPaths) {
            QFileInfo fi(entry);
            const QString dest = localDir + QLatin1Char('/') + fi.fileName();
            if (!reader.extractEntry(&source, entry, dest, &err))
                return Outcome::fail(err);
        }
        return Outcome::success();
    }
    TarReader reader(kind != Kind::Tar);
    QString err;
    if (!reader.open(&source, &err))
        return Outcome::fail(err);
    for (const QString& entry : entryPaths) {
        QFileInfo fi(entry);
        const QString dest = localDir + QLatin1Char('/') + fi.fileName();
        if (!reader.extractEntry(entry, dest, &err))
            return Outcome::fail(err);
    }
    return Outcome::success();
}

} // namespace eclipse
