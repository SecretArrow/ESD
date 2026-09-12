#pragma once

// ---------------------------------------------------------------------------
// ArchiveSource.h - abstraction over a seekable-or-sequential byte source.
//
// Archive parsers never download or read whole files: they pull bytes on
// demand. Two concrete sources exist:
//
//   * LocalFileArchiveSource - a local file (QFile), fully seekable.
//   * SftpArchiveSource      - a remote file streamed over an existing
//                              ISftpSession; random-access reads go through
//                              seekFile+readFile behind a small LRU window
//                              cache (256 KiB) so that ZIP central-directory
//                              parsing costs only a handful of network reads.
//
// DecompressionStream wraps any ArchiveSource and yields decompressed bytes
// (gzip via zlib, bzip2/xz when compiled with ECLIPSE_HAVE_BZLIB /
// ECLIPSE_HAVE_LZMA). It is used by TarReader for .tar.gz/.tar.bz2/.tar.xz.
// ---------------------------------------------------------------------------

#include <QByteArray>
#include <QString>
#include <memory>

#include "../ssh/ISshEngine.h"

namespace eclipse {

enum class CompressionCodec { None, Gzip, Bzip2, Xz };

class ArchiveSource
{
public:
    ArchiveSource() = default;
    virtual ~ArchiveSource();
    ArchiveSource(const ArchiveSource&) = delete;
    ArchiveSource& operator=(const ArchiveSource&) = delete;

    // Total size in bytes; may return 0 when unknown (pure streams).
    virtual quint64 size() = 0;

    virtual bool isSeekable() = 0;

    // Random-access read; only meaningful when isSeekable() == true.
    // Reads exactly `len` bytes at `offset` unless the range extends past the
    // end of the stream (then fewer bytes are returned). A `len` of 0 may be
    // used to reposition sequential readers.
    virtual bool readAt(quint64 offset, int len, QByteArray* out, QString* err);

    // Sequential read: appends up to `maxLen` bytes to *out.
    // Returns bytes read (>= 0), 0 at end of stream, -1 on error.
    virtual int readNext(int maxLen, QByteArray* out, QString* err) = 0;

    // Rewinds to position 0. Needed to re-scan compressed tar streams for a
    // targeted extraction. Returns false when the storage cannot rewind.
    virtual bool rewind(QString* err);

    // Logical position of the sequential cursor (informational).
    quint64 position() const { return m_pos; }

protected:
    quint64 m_pos = 0;
};

// ---------------------------------------------------------------------------
class LocalFileArchiveSource final : public ArchiveSource
{
public:
    static std::unique_ptr<LocalFileArchiveSource> open(const QString& path, QString* err);
    ~LocalFileArchiveSource() override;

    quint64 size() override { return m_size; }
    bool isSeekable() override { return true; }
    bool readAt(quint64 offset, int len, QByteArray* out, QString* err) override;
    int readNext(int maxLen, QByteArray* out, QString* err) override;
    bool rewind(QString* err) override;

private:
    explicit LocalFileArchiveSource(std::unique_ptr<QFile> file, quint64 size);

    std::unique_ptr<QFile> m_file;
    quint64 m_size = 0;
};

// ---------------------------------------------------------------------------
class SftpArchiveSource final : public ArchiveSource
{
public:
    // Opens the remote file for reading; `sftp` is retained (shared) so the
    // handle stays valid for the lifetime of this object.
    static std::unique_ptr<SftpArchiveSource> open(std::shared_ptr<ISftpSession> sftp,
                                                   const QString& remotePath, QString* err);
    ~SftpArchiveSource() override;

    quint64 size() override { return m_size; }
    bool isSeekable() override { return true; }
    bool readAt(quint64 offset, int len, QByteArray* out, QString* err) override;
    int readNext(int maxLen, QByteArray* out, QString* err) override;
    bool rewind(QString* err) override;

private:
    SftpArchiveSource(std::shared_ptr<ISftpSession> sftp, ISftpSession::FileHandle handle,
                      quint64 size);

    bool blockAt(quint64 blockIndex, QByteArray* block, QString* err);
    void cacheInsert(quint64 blockIndex, const QByteArray& data);

    std::shared_ptr<ISftpSession> m_sftp;
    ISftpSession::FileHandle m_handle = nullptr;
    quint64 m_size = 0;

    // LRU window cache: 4 blocks x 64 KiB = 256 KiB. Front = most recent.
    struct CachedBlock
    {
        quint64 index = 0;
        QByteArray data;
    };
    static constexpr int kBlockBytes = 64 * 1024;
    static constexpr int kMaxCachedBlocks = 4;
    QList<CachedBlock> m_cache;
};

// ---------------------------------------------------------------------------
// DecompressionStream: wrapper around another ArchiveSource producing
// decompressed bytes. The wrapped source is BORROWED and must outlive this
// object. For Gzip/Bzip2/Xz the stream is strictly sequential (isSeekable()
// == false, size() == 0); for None it delegates everything.
// ---------------------------------------------------------------------------
class DecompressionStream final : public ArchiveSource
{
public:
    DecompressionStream(ArchiveSource* source, CompressionCodec codec,
                        const QByteArray& pendingPrefix = {});
    ~DecompressionStream() override;

    quint64 size() override;
    bool isSeekable() override;
    bool readAt(quint64 offset, int len, QByteArray* out, QString* err) override;
    int readNext(int maxLen, QByteArray* out, QString* err) override;
    bool rewind(QString* err) override;

    CompressionCodec codec() const { return m_codec; }

private:
    struct Ctx; // codec contexts live in the .cpp (keeps lib headers out here)

    bool start(QString* err);
    bool pump(QString* err);
    void teardown();

    ArchiveSource* m_src = nullptr;
    CompressionCodec m_codec = CompressionCodec::None;
    std::unique_ptr<Ctx> m_ctx;

    QByteArray m_prefix;   // bytes consumed from the source before wrapping
    int m_prefixPos = 0;
    QByteArray m_out;      // decoded bytes pending delivery
    bool m_started = false;
    bool m_finished = false;
    bool m_srcEof = false;
};

} // namespace eclipse
