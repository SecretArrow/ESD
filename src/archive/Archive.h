#pragma once

#include <QByteArray>
#include <QFile>
#include <QHash>
#include <QString>
#include <QVector>

#include "../common/Outcome.h"
#include "../ssh/ISshEngine.h"

namespace eclipse {

// ---------------------------------------------------------------------------
// Archive viewer core.
//
//  * ZipReader: parses the ZIP central directory using TAIL reads only
//    (remote listing does NOT download the whole file). Extraction streams
//    the requested entry via ranged reads + raw-deflate (zlib).
//  * TarReader: sequential 512-byte header scan; gz stream decompression via
//    zlib; bz2/lzma optional at build time (ECLIPSE_HAVE_BZLIB / LZMA).
//
// All byte sources implement ArchiveSource so local files and remote SFTP
// handles share one code path.
// ---------------------------------------------------------------------------

class ArchiveSource
{
public:
    virtual ~ArchiveSource() = default;
    virtual bool isSeekable() const = 0;
    virtual quint64 totalSize() const = 0;      // 0 = unknown
    virtual bool readAt(quint64 offset, int len, QByteArray* out, QString* err) = 0;
    virtual int readNext(int maxLen, QByteArray* out, QString* err) = 0; // sequential
    virtual bool rewind(QString* err) = 0;
};

class LocalFileArchiveSource : public ArchiveSource
{
public:
    explicit LocalFileArchiveSource(const QString& path);
    bool isOpen() const { return m_file.isOpen(); }
    bool isSeekable() const override { return true; }
    quint64 totalSize() const override;
    bool readAt(quint64 offset, int len, QByteArray* out, QString* err) override;
    int readNext(int maxLen, QByteArray* out, QString* err) override;
    bool rewind(QString* err) override;

private:
    QFile m_file;
};

// Reads remote archives through SFTP without downloading them entirely.
class SftpArchiveSource : public ArchiveSource
{
public:
    SftpArchiveSource(std::shared_ptr<ISftpSession> sftp, const QString& remotePath);
    ~SftpArchiveSource() override;
    bool isValid() const { return m_handle != nullptr; }
    bool isSeekable() const override { return true; }
    quint64 totalSize() const override { return m_size; }
    bool readAt(quint64 offset, int len, QByteArray* out, QString* err) override;
    int readNext(int maxLen, QByteArray* out, QString* err) override;
    bool rewind(QString* err) override;

private:
    static constexpr int kWindow = 256 * 1024;
    void fillWindow(quint64 offset);

    std::shared_ptr<ISftpSession> m_sftp;
    ISftpSession::FileHandle m_handle = nullptr;
    quint64 m_size = 0;
    quint64 m_seqPos = 0;
    // window cache for random access
    QByteArray m_window;
    quint64 m_windowOffset = 0;
    bool m_windowValid = false;
};

struct ArchiveEntry
{
    QString path;
    bool isDir = false;
    quint64 size = 0;
    quint64 compressedSize = 0;
    qint64 mtime = 0;
};

class ZipReader
{
public:
    bool open(ArchiveSource* source, QString* err);
    const QVector<ArchiveEntry>& entries() const { return m_entries; }
    bool extractEntry(ArchiveSource* source, const QString& entryPath,
                      const QString& localDest, QString* err);
    bool readTextPreview(ArchiveSource* source, const QString& entryPath,
                         int maxBytes, QByteArray* out, QString* err);

private:
    ArchiveSource* m_source = nullptr;
    QVector<ArchiveEntry> m_entries;
    QVector<quint64> m_localHeaderOffsets;
    QVector<quint16> m_methods;
    quint64 m_eocdOffset = 0;
};

class TarReader
{
public:
    explicit TarReader(bool compressed);
    ~TarReader();
    bool open(ArchiveSource* source, QString* err);
    const QVector<ArchiveEntry>& entries() const { return m_entries; }
    // Sequential extract; requires a fresh source (rewinds internally).
    bool extractEntry(const QString& entryPath, const QString& localDest, QString* err);
    bool readTextPreview(const QString& entryPath, int maxBytes, QByteArray* out, QString* err);

private:
    bool scanAll(QString* err);
    bool ensureStream();
    int streamRead(char* buf, int len, QString* err);

    ArchiveSource* m_source = nullptr;
    bool m_compressed = false;
    void* m_zStream = nullptr; // opaque zlib state
    int m_zAlgo = 0;           // 0 none, 1 gz, 2 bz2, 3 xz
    bool m_streamDone = false;
    bool m_streamStarted = false;
    QByteArray m_inBuffer;
    QVector<ArchiveEntry> m_entries;
    QHash<QString, quint64> m_offsets; // uncompressed-tar offsets for seek
};

class ArchiveService
{
public:
    enum class Kind { Zip, Tar, TarGz, Tgz, TarBz2, TarXz, SevenZip, Unknown };

    static Kind detect(const QString& fileName);
    static QString kindName(Kind k);

    // Open listing (zip: tail reads; tar: sequential scan)
    static Outcome listRemote(std::shared_ptr<ISftpSession> sftp, const QString& remotePath,
                              QVector<ArchiveEntry>* entries, QString* engineNote);
    static Outcome listLocal(const QString& path, QVector<ArchiveEntry>* entries);

    // Extract selected entries (remote or local) into localDir.
    static Outcome extractRemote(std::shared_ptr<ISftpSession> sftp, const QString& remotePath,
                                 const QStringList& entryPaths, const QString& localDir);
    static Outcome extractLocal(const QString& archivePath, const QStringList& entryPaths,
                                const QString& localDir);
};

} // namespace eclipse
