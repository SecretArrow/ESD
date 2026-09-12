#pragma once

// ---------------------------------------------------------------------------
// ZipReader.h - ZIP reader that lists and extracts WITHOUT downloading the
// whole archive.
//
// Listing: reads only the tail of the file (EOCD + ZIP64 locator/record) and
// then the central directory via ranged reads. On a remote file behind
// SftpArchiveSource this costs a handful of small network reads.
//
// Extraction: seeks straight to the entry's local header, skips name+extra
// using the central-directory record, then streams the raw deflate (zlib,
// raw window, inflateInit2(-15)) or stored payload to disk while verifying
// the CRC32.
//
// Not supported (reported with friendly errors): encrypted entries,
// compression methods other than Store/Deflate, multi-disk archives.
// 7Z archives are handled by ArchiveService ("Coming Soon").
// ---------------------------------------------------------------------------

#include <QByteArray>
#include <QString>
#include <QVector>
#include <functional>

#include "ArchiveSource.h"

namespace eclipse {

struct ZipEntry
{
    QString name;               // '/'-separated, no leading '/'; dirs have no trailing '/'
    quint64 size = 0;           // uncompressed size
    quint64 compressedSize = 0;
    quint16 method = 0;         // 0 = store, 8 = deflate
    quint32 crc = 0;
    quint64 localHeaderOffset = 0;
    bool isDir = false;
    bool encrypted = false;
    qint64 mtimeSec = 0;        // DOS timestamp interpreted as UTC
    quint32 externalAttributes = 0;
};

class ZipReader
{
public:
    ZipReader() = default;
    ~ZipReader() = default;

    // `source` is borrowed and must outlive the reader. Listing requires a
    // seekable source (ZIP directories live at the end of the file).
    bool open(ArchiveSource* source, QString* err);

    const QVector<ZipEntry>& entries() const { return m_entries; }

    // Extracts one entry to a local file. `progress(done, uncompressedTotal)`
    // is called periodically; returning false cancels (the partial file is
    // removed). CRC32 is verified on extraction.
    bool extractEntry(const QString& name, const QString& localDest,
                      const std::function<bool(quint64, quint64)>& progress, QString* err);

    // Decompressed first bytes of an entry (no CRC verification, bounded).
    bool readTextPreview(const QString& name, int maxBytes, QByteArray* out,
                         QString* err = nullptr);

private:
    const ZipEntry* findEntry(const QString& name) const;
    bool extractData(const ZipEntry& e, const QString& localDest, QByteArray* preview,
                     int previewLimit, const std::function<bool(quint64, quint64)>& progress,
                     QString* err);

    ArchiveSource* m_src = nullptr;
    quint64 m_size = 0; // archive size (0 = unknown)
    QVector<ZipEntry> m_entries;
};

} // namespace eclipse
