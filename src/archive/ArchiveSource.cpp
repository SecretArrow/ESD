#include "ArchiveSource.h"

#include <QFile>
#include <QFileInfo>
#include <QDir>

#include <zlib.h>
#if defined(ECLIPSE_HAVE_BZLIB)
#include <bzlib.h>
#endif
#if defined(ECLIPSE_HAVE_LZMA)
#include <lzma.h>
#endif

#include "ArchiveUtil.h"

namespace eclipse {

using archiveutil::kIoChunkBytes;
using archiveutil::setError;

// ---------------------------------------------------------------------------
// ArchiveSource (base)
// ---------------------------------------------------------------------------

ArchiveSource::~ArchiveSource() = default;

bool ArchiveSource::readAt(quint64, int, QByteArray* out, QString* err)
{
    out->clear();
    setError(err, QStringLiteral("This data source does not support random access reads."),
             QStringLiteral("ArchiveSource::readAt() default implementation"));
    return false;
}

bool ArchiveSource::rewind(QString* err)
{
    setError(err, QStringLiteral("This data source cannot be rewound."),
             QStringLiteral("ArchiveSource::rewind() default implementation"));
    return false;
}

// ---------------------------------------------------------------------------
// LocalFileArchiveSource
// ---------------------------------------------------------------------------

std::unique_ptr<LocalFileArchiveSource> LocalFileArchiveSource::open(const QString& path,
                                                                     QString* err)
{
    auto file = std::make_unique<QFile>(path);
    if (!file->open(QIODevice::ReadOnly)) {
        setError(err, QStringLiteral("Could not open the local file '%1'.").arg(path),
                 file->errorString());
        return nullptr;
    }
    const qint64 sz = file->size();
    if (sz < 0) {
        setError(err, QStringLiteral("Could not determine the size of '%1'.").arg(path),
                 file->errorString());
        return nullptr;
    }
    return std::unique_ptr<LocalFileArchiveSource>(
        new LocalFileArchiveSource(std::move(file), quint64(sz)));
}

LocalFileArchiveSource::LocalFileArchiveSource(std::unique_ptr<QFile> file, quint64 size)
    : m_file(std::move(file)), m_size(size)
{
}

LocalFileArchiveSource::~LocalFileArchiveSource() = default;

bool LocalFileArchiveSource::readAt(quint64 offset, int len, QByteArray* out, QString* err)
{
    out->clear();
    if (len < 0) {
        setError(err, QStringLiteral("Invalid read length."));
        return false;
    }
    if (len == 0) {
        m_pos = qMin(offset, m_size);
        return true;
    }
    if (offset >= m_size) {
        m_pos = m_size;
        return true; // EOF
    }
    const int want = int(qMin<quint64>(len, m_size - offset));
    if (!m_file->seek(qint64(offset))) {
        setError(err, QStringLiteral("Could not reposition inside the local file."),
                 m_file->errorString());
        return false;
    }
    QByteArray buf(want, Qt::Uninitialized);
    int got = 0;
    while (got < want) {
        const qint64 n = m_file->read(buf.data() + got, want - got);
        if (n < 0) {
            setError(err, QStringLiteral("Reading the local file failed."), m_file->errorString());
            return false;
        }
        if (n == 0) break;
        got += int(n);
    }
    if (got < want) {
        setError(err,
                 archiveutil::corruptedError(
                     QStringLiteral("the file ended unexpectedly at offset %1").arg(offset + got)),
                 QStringLiteral("wanted %1 bytes, got %2").arg(want).arg(got));
        return false;
    }
    *out = buf;
    m_pos = offset + quint64(got);
    return true;
}

int LocalFileArchiveSource::readNext(int maxLen, QByteArray* out, QString* err)
{
    out->clear();
    if (maxLen <= 0) return 0;
    if (m_pos >= m_size) return 0;
    QByteArray chunk;
    const int want = int(qMin<quint64>(maxLen, m_size - m_pos));
    if (!readAt(m_pos, want, &chunk, err)) return -1;
    if (chunk.isEmpty()) return 0;
    *out = chunk;
    return chunk.size();
}

bool LocalFileArchiveSource::rewind(QString* err)
{
    if (!m_file->seek(0)) {
        setError(err, QStringLiteral("Could not rewind the local file."), m_file->errorString());
        return false;
    }
    m_pos = 0;
    return true;
}

// ---------------------------------------------------------------------------
// SftpArchiveSource
// ---------------------------------------------------------------------------

std::unique_ptr<SftpArchiveSource> SftpArchiveSource::open(std::shared_ptr<ISftpSession> sftp,
                                                           const QString& remotePath,
                                                           QString* err)
{
    if (!sftp || !sftp->isValid()) {
        setError(err, QStringLiteral("The SFTP session is not available."),
                 QStringLiteral("SftpArchiveSource::open() called without a valid session"));
        return nullptr;
    }
    quint64 size = 0;
    ISftpSession::FileHandle handle = sftp->openForRead(remotePath, &size, err);
    if (!handle) {
        const QString detail = err ? *err : QString();
        setError(err, QStringLiteral("Could not open the remote file '%1' for reading.").arg(remotePath),
                 detail);
        return nullptr;
    }
    // Wrap in a unique_ptr immediately so the handle is closed if the
    // constructor below ever throws (it cannot today, but stay safe).
    return std::unique_ptr<SftpArchiveSource>(new SftpArchiveSource(std::move(sftp), handle, size));
}

SftpArchiveSource::SftpArchiveSource(std::shared_ptr<ISftpSession> sftp,
                                     ISftpSession::FileHandle handle, quint64 size)
    : m_sftp(std::move(sftp)), m_handle(handle), m_size(size)
{
}

SftpArchiveSource::~SftpArchiveSource()
{
    if (m_handle && m_sftp) m_sftp->closeFile(m_handle);
}

void SftpArchiveSource::cacheInsert(quint64 blockIndex, const QByteArray& data)
{
    while (m_cache.size() >= kMaxCachedBlocks) m_cache.removeLast();
    m_cache.prepend({blockIndex, data});
}

bool SftpArchiveSource::blockAt(quint64 blockIndex, QByteArray* block, QString* err)
{
    for (int i = 0; i < m_cache.size(); ++i) {
        if (m_cache[i].index == blockIndex) {
            *block = m_cache[i].data;
            if (i != 0) m_cache.move(i, 0); // LRU touch
            return true;
        }
    }

    const quint64 blockStart = blockIndex * quint64(kBlockBytes);
    if (!m_sftp->seekFile(m_handle, blockStart)) {
        setError(err, QStringLiteral("The remote file could not be repositioned (seek failed)."),
                 QStringLiteral("seekFile(offset=%1) failed").arg(blockStart));
        return false;
    }
    QByteArray data(kBlockBytes, Qt::Uninitialized);
    int got = 0;
    while (got < kBlockBytes) {
        const int chunk = qMin(kBlockBytes - got, 32768);
        const int n = m_sftp->readFile(m_handle, data.data() + got, chunk, err);
        if (n < 0) {
            setError(err, QStringLiteral("Reading from the remote file failed."),
                     err ? *err : QString());
            return false;
        }
        if (n == 0) break; // EOF
        got += n;
    }
    if (got < kBlockBytes) data.resize(got);
    *block = data;
    cacheInsert(blockIndex, data);
    return true;
}

bool SftpArchiveSource::readAt(quint64 offset, int len, QByteArray* out, QString* err)
{
    out->clear();
    if (len < 0) {
        setError(err, QStringLiteral("Invalid read length."));
        return false;
    }
    if (len == 0) {
        m_pos = qMin(offset, m_size > 0 ? m_size : offset);
        return true;
    }
    if (m_size > 0 && offset >= m_size) {
        m_pos = m_size;
        return true; // EOF
    }
    const quint64 want = (m_size > 0) ? qMin<quint64>(len, m_size - offset) : quint64(len);
    QByteArray result(int(want), Qt::Uninitialized);
    int done = 0;
    quint64 blockIndex = offset / quint64(kBlockBytes);
    while (done < int(want)) {
        QByteArray block;
        if (!blockAt(blockIndex, &block, err)) return false;
        if (block.isEmpty()) break; // EOF (unknown-size stream)
        const quint64 blockStart = blockIndex * quint64(kBlockBytes);
        const int blockOffset = int(offset + quint64(done) - blockStart);
        if (blockOffset >= block.size()) break;
        const int take = qMin(int(want) - done, block.size() - blockOffset);
        memcpy(result.data() + done, block.constData() + blockOffset, size_t(take));
        done += take;
        ++blockIndex;
    }
    if (done < int(want)) result.resize(done);
    *out = result;
    m_pos = offset + quint64(done);
    return true;
}

int SftpArchiveSource::readNext(int maxLen, QByteArray* out, QString* err)
{
    out->clear();
    if (maxLen <= 0) return 0;
    if (m_size > 0 && m_pos >= m_size) return 0;
    const quint64 want = (m_size > 0) ? qMin<quint64>(maxLen, m_size - m_pos) : quint64(maxLen);
    QByteArray chunk;
    if (!readAt(m_pos, int(want), &chunk, err)) return -1;
    if (chunk.isEmpty()) return 0;
    *out = chunk;
    return chunk.size();
}

bool SftpArchiveSource::rewind(QString* err)
{
    if (!m_sftp->seekFile(m_handle, 0)) {
        setError(err, QStringLiteral("The remote file could not be repositioned to its start."));
        return false;
    }
    m_pos = 0;
    return true;
}

// ---------------------------------------------------------------------------
// DecompressionStream
// ---------------------------------------------------------------------------

struct DecompressionStream::Ctx
{
    // gzip / zlib
    z_stream z{};
    bool zInit = false;
#if defined(ECLIPSE_HAVE_BZLIB)
    bz_stream bz{};
    bool bzInit = false;
#endif
#if defined(ECLIPSE_HAVE_LZMA)
    lzma_stream lz{};
    bool lzInit = false;
#endif
    QByteArray inBuf; // current compressed input chunk (keeps next_in alive)
};

DecompressionStream::DecompressionStream(ArchiveSource* source, CompressionCodec codec,
                                         const QByteArray& pendingPrefix)
    : m_src(source), m_codec(codec), m_prefix(pendingPrefix)
{
}

DecompressionStream::~DecompressionStream()
{
    teardown();
}

void DecompressionStream::teardown()
{
    if (!m_ctx || !m_started) return;
    if (m_codec == CompressionCodec::Gzip && m_ctx->zInit) {
        inflateEnd(&m_ctx->z);
        m_ctx->zInit = false;
    }
#if defined(ECLIPSE_HAVE_BZLIB)
    if (m_codec == CompressionCodec::Bzip2 && m_ctx->bzInit) {
        BZ2_bzDecompressEnd(&m_ctx->bz);
        m_ctx->bzInit = false;
    }
#endif
#if defined(ECLIPSE_HAVE_LZMA)
    if (m_codec == CompressionCodec::Xz && m_ctx->lzInit) {
        lzma_end(&m_ctx->lz);
        m_ctx->lzInit = false;
    }
#endif
    m_started = false;
}

bool DecompressionStream::start(QString* err)
{
    if (m_started) return true;
    if (!m_ctx) m_ctx = std::make_unique<Ctx>();

    if (m_codec == CompressionCodec::None) {
        m_started = true;
        return true;
    }

#if !defined(ECLIPSE_HAVE_BZLIB)
    if (m_codec == CompressionCodec::Bzip2) {
        setError(err,
                 QStringLiteral("This build of Eclipse does not include bzip2 support, so this archive cannot be opened."),
                 QStringLiteral("compiled without ECLIPSE_HAVE_BZLIB"));
        return false;
    }
#endif
#if !defined(ECLIPSE_HAVE_LZMA)
    if (m_codec == CompressionCodec::Xz) {
        setError(err,
                 QStringLiteral("This build of Eclipse does not include xz support, so this archive cannot be opened."),
                 QStringLiteral("compiled without ECLIPSE_HAVE_LZMA"));
        return false;
    }
#endif

    if (m_codec == CompressionCodec::Gzip) {
        memset(&m_ctx->z, 0, sizeof(m_ctx->z));
        // 15+32: auto-detect gzip or zlib wrapper; raw inflate is NOT wanted here.
        if (inflateInit2(&m_ctx->z, 15 + 32) != Z_OK) {
            setError(err, QStringLiteral("The gzip decompressor could not be initialised."),
                     QStringLiteral("inflateInit2 failed (rc=%1)").arg(m_ctx->z.msg ? m_ctx->z.msg : "?"));
            return false;
        }
        m_ctx->zInit = true;
    }
#if defined(ECLIPSE_HAVE_BZLIB)
    else if (m_codec == CompressionCodec::Bzip2) {
        memset(&m_ctx->bz, 0, sizeof(m_ctx->bz));
        if (BZ2_bzDecompressInit(&m_ctx->bz, 0, 0) != BZ_OK) {
            setError(err, QStringLiteral("The bzip2 decompressor could not be initialised."),
                     QStringLiteral("BZ2_bzDecompressInit failed"));
            return false;
        }
        m_ctx->bzInit = true;
    }
#endif
#if defined(ECLIPSE_HAVE_LZMA)
    else if (m_codec == CompressionCodec::Xz) {
        memset(&m_ctx->lz, 0, sizeof(m_ctx->lz));
        if (lzma_stream_decoder(&m_ctx->lz, UINT64_MAX, 0) != LZMA_OK) {
            setError(err, QStringLiteral("The xz decompressor could not be initialised."),
                     QStringLiteral("lzma_stream_decoder failed"));
            return false;
        }
        m_ctx->lzInit = true;
    }
#endif

    m_started = true;
    return true;
}

bool DecompressionStream::pump(QString* err)
{
    if (!m_ctx) return false;
    constexpr int kOutChunk = 64 * 1024;

    while (m_out.isEmpty() && !m_finished) {
        // 1. Ensure the decompressor has input; refill from the source.
        bool haveInput = false;
        if (m_codec == CompressionCodec::Gzip && m_ctx->z.avail_in > 0) haveInput = true;
#if defined(ECLIPSE_HAVE_BZLIB)
        if (m_codec == CompressionCodec::Bzip2 && m_ctx->bz.avail_in > 0) haveInput = true;
#endif
#if defined(ECLIPSE_HAVE_LZMA)
        if (m_codec == CompressionCodec::Xz && m_ctx->lz.avail_in > 0) haveInput = true;
#endif

        if (!haveInput) {
            QByteArray chunk;
            int n = 0;
            if (m_prefixPos < m_prefix.size()) {
                n = qMin(kIoChunkBytes, m_prefix.size() - m_prefixPos);
                chunk = m_prefix.mid(m_prefixPos, n);
                m_prefixPos += n;
            } else if (!m_srcEof) {
                n = m_src->readNext(kIoChunkBytes, &chunk, err);
                if (n < 0) return false;
                if (n == 0) m_srcEof = true;
            }
            if (n > 0) {
                m_ctx->inBuf = chunk.left(n);
                const Bytef* in = reinterpret_cast<const Bytef*>(m_ctx->inBuf.constData());
                if (m_codec == CompressionCodec::Gzip) {
                    m_ctx->z.next_in = const_cast<Bytef*>(in);
                    m_ctx->z.avail_in = uInt(n);
                }
#if defined(ECLIPSE_HAVE_BZLIB)
                else if (m_codec == CompressionCodec::Bzip2) {
                    m_ctx->bz.next_in = const_cast<char*>(m_ctx->inBuf.constData());
                    m_ctx->bz.avail_in = unsigned(n);
                }
#endif
#if defined(ECLIPSE_HAVE_LZMA)
                else if (m_codec == CompressionCodec::Xz) {
                    m_ctx->lz.next_in = reinterpret_cast<const uint8_t*>(m_ctx->inBuf.constData());
                    m_ctx->lz.avail_in = size_t(n);
                }
#endif
                haveInput = true;
            }
        }

        if (!haveInput) {
            if (m_finished) break;
            setError(err,
                     QStringLiteral("The archive ended unexpectedly in the middle of a compressed stream."),
                     QStringLiteral("truncated compressed data (codec=%1)").arg(int(m_codec)));
            return false;
        }

        // 2. Decompress one output chunk.
        char outBuf[kOutChunk];
        size_t produced = 0;
        bool progressMade = false;

        if (m_codec == CompressionCodec::Gzip) {
            m_ctx->z.next_out = reinterpret_cast<Bytef*>(outBuf);
            m_ctx->z.avail_out = uInt(kOutChunk);
            const int before_in = int(m_ctx->z.avail_in);
            const int rc = inflate(&m_ctx->z, Z_NO_FLUSH);
            produced = size_t(kOutChunk - m_ctx->z.avail_out);
            if (rc == Z_STREAM_END) m_finished = true;
            else if (rc == Z_BUF_ERROR && m_ctx->z.avail_in == 0) { /* need more input */ }
            else if (rc != Z_OK) {
                setError(err,
                         QStringLiteral("The archive appears to be corrupted (compressed data is invalid)."),
                         QStringLiteral("inflate rc=%1 msg=%2").arg(rc).arg(m_ctx->z.msg ? m_ctx->z.msg : "-"));
                return false;
            }
            progressMade = (produced > 0) || (int(m_ctx->z.avail_in) != before_in) || m_finished;
        }
#if defined(ECLIPSE_HAVE_BZLIB)
        else if (m_codec == CompressionCodec::Bzip2) {
            m_ctx->bz.next_out = outBuf;
            m_ctx->bz.avail_out = unsigned(kOutChunk);
            const int rc = BZ2_bzDecompress(&m_ctx->bz);
            produced = size_t(kOutChunk - m_ctx->bz.avail_out);
            if (rc == BZ_STREAM_END) m_finished = true;
            else if (rc != BZ_OK) {
                setError(err,
                         QStringLiteral("The archive appears to be corrupted (bzip2 data is invalid)."),
                         QStringLiteral("BZ2_bzDecompress rc=%1").arg(rc));
                return false;
            }
            progressMade = (produced > 0) || m_finished;
        }
#endif
#if defined(ECLIPSE_HAVE_LZMA)
        else if (m_codec == CompressionCodec::Xz) {
            static const size_t kOutCap = size_t(kOutChunk);
            m_ctx->lz.next_out = reinterpret_cast<uint8_t*>(outBuf);
            m_ctx->lz.avail_out = kOutCap;
            const lzma_ret rc = lzma_code(&m_ctx->lz, LZMA_RUN);
            produced = kOutCap - m_ctx->lz.avail_out;
            if (rc == LZMA_STREAM_END) m_finished = true;
            else if (rc != LZMA_OK) {
                setError(err,
                         QStringLiteral("The archive appears to be corrupted (xz data is invalid)."),
                         QStringLiteral("lzma_code rc=%1").arg(int(rc)));
                return false;
            }
            progressMade = (produced > 0) || m_finished;
        }
#endif

        if (produced > 0) m_out.append(outBuf, int(produced));

        // 3. Guard against a stalled stream (no output, no input consumed).
        if (!progressMade && !m_finished && m_out.isEmpty()) {
            setError(err,
                     QStringLiteral("The archive appears to be corrupted (decompression stalled)."),
                     QStringLiteral("no progress in decompression (codec=%1)").arg(int(m_codec)));
            return false;
        }
    }
    return true;
}

quint64 DecompressionStream::size()
{
    return (m_codec == CompressionCodec::None) ? m_src->size() : 0;
}

bool DecompressionStream::isSeekable()
{
    return (m_codec == CompressionCodec::None) && m_src->isSeekable();
}

bool DecompressionStream::readAt(quint64 offset, int len, QByteArray* out, QString* err)
{
    if (m_codec == CompressionCodec::None) {
        const bool ok = m_src->readAt(offset, len, out, err);
        if (ok) m_pos = m_src->position();
        return ok;
    }
    out->clear();
    setError(err, QStringLiteral("Compressed archives do not support random access reads."),
             QStringLiteral("DecompressionStream::readAt on compressed stream"));
    return false;
}

int DecompressionStream::readNext(int maxLen, QByteArray* out, QString* err)
{
    out->clear();
    if (maxLen <= 0) return 0;

    // Bytes consumed from the source before this stream was created.
    if (m_prefixPos < m_prefix.size()) {
        const int n = qMin(maxLen, m_prefix.size() - m_prefixPos);
        out->append(m_prefix.constData() + m_prefixPos, n);
        m_prefixPos += n;
        m_pos += quint64(n);
        return n;
    }

    if (m_codec == CompressionCodec::None) {
        const int n = m_src->readNext(maxLen, out, err);
        if (n > 0) m_pos += quint64(n);
        return n;
    }

    if (!start(err)) return -1;
    if (m_out.isEmpty() && !m_finished) {
        if (!pump(err)) return -1;
    }
    if (m_out.isEmpty()) return 0; // finished and drained

    const int n = qMin(maxLen, m_out.size());
    out->append(m_out.constData(), n);
    m_out.remove(0, n);
    m_pos += quint64(n);
    return n;
}

bool DecompressionStream::rewind(QString* err)
{
    // Reset the underlying storage and all decoder state so the stream can
    // be re-scanned from its beginning.
    if (!m_src->rewind(err)) return false;
    teardown();
    m_ctx.reset();
    m_out.clear();
    m_prefixPos = 0;
    m_finished = false;
    m_srcEof = false;
    m_started = false;
    m_pos = 0;
    return true;
}

} // namespace eclipse
