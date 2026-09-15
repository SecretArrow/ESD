// ---------------------------------------------------------------------------
// Libssh2Engine.cpp - libssh2 backend implementation (session, auth, channels,
// SFTP, SCP, forwarding).
//
// Threading model:
//   Libssh2SessionState::mutex (recursive) guards every libssh2 call. The
//   session runs in NON-BLOCKING mode for its whole lifetime; all operations
//   that "look blocking" are EAGAIN retry loops with a deadline (retryEagain /
//   retryEagainPtr below). Transfer-thread entry points (ISftpSession /
//   IScpSession) lock the mutex per call.
// ---------------------------------------------------------------------------

#include "Libssh2Engine.h"

#include "../../AgentClient.h"
#include "../../HostKeyManager.h"
#include "../../../common/Utils.h"
#include "../../../core/logging/Logger.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QHash>
#include <QSaveFile>

#include <atomic>
#include <chrono>
#include <cinttypes>
#include <cstdio>
#include <cstring>
#include <mutex>
#include <thread>
#include <sys/stat.h>
#include <ctime>

#ifndef _WIN32
#include <fcntl.h>
#include <netdb.h>
#include <netinet/in.h>
#include <poll.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <unistd.h>
#else
#define WIN32_LEAN_AND_MEAN
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <winsock2.h>
#include <ws2tcpip.h>
#endif

namespace eclipse {

// ===========================================================================
// Libssh2SessionState - shared engine state (defined early: the raw helpers
// in the anonymous namespace below reach into it).
// ===========================================================================

namespace {
// Forward declarations for the session state struct below.
constexpr int kDefaultOpTimeoutMs = 30000;
constexpr int kShortTimeoutMs = 5000;
qint64 nowMs();
void sleepRetry();
void closeSocket(libssh2_socket_t fd);

// Inbound X11 registry (defined below the engine section): libssh2's
// LIBSSH2_CALLBACK_X11 trampoline only receives the LIBSSH2_SESSION*, so a
// registry maps live sessions back to their owning engine. Entries are added
// in finishConnect() and removed as soon as the session is freed.
void x11RegistryRemove(LIBSSH2_SESSION* session);
} // namespace

struct Libssh2SessionState
{
    mutable std::recursive_mutex mutex;
    LIBSSH2_SESSION* session = nullptr;
    LIBSSH2_LISTENER* listener = nullptr;
    libssh2_socket_t fd = LIBSSH2_INVALID_SOCKET;
    bool ownsFd = false;
    bool connected = false;
    bool keepaliveConfigured = false;
    int opTimeoutMs = kDefaultOpTimeoutMs;
    int connId = 0;
    HostKeyInfo hostKey;
    QString banner;

    ~Libssh2SessionState() { closeNow(); }

    void closeNow()
    {
        std::lock_guard<std::recursive_mutex> lock(mutex);
        if (session) {
            libssh2_session_disconnect_ex(session, SSH_DISCONNECT_BY_APPLICATION,
                                          "Eclipse SSH Desktop", "");
            int rcf = 0;
            const qint64 deadline = nowMs() + kShortTimeoutMs;
            for (;;) {
                rcf = libssh2_session_free(session);
                if (rcf != LIBSSH2_ERROR_EAGAIN || nowMs() >= deadline)
                    break;
                sleepRetry();
            }
            if (rcf == LIBSSH2_ERROR_EAGAIN) {
                // Force teardown: no concurrent libssh2 call can be in flight
                // because we hold the state mutex.
                libssh2_session_set_blocking(session, 1);
                libssh2_session_free(session);
            }
            x11RegistryRemove(session); // drop the X11 callback registry entry
            session = nullptr;
        }
        listener = nullptr;
        if (fd != LIBSSH2_INVALID_SOCKET && ownsFd)
            closeSocket(fd);
        fd = LIBSSH2_INVALID_SOCKET;
        ownsFd = false;
        connected = false;
        keepaliveConfigured = false;
    }
};

namespace {

// X11 inbound registry: the LIBSSH2_CALLBACK_X11 trampoline only receives the
// LIBSSH2_SESSION*, so live sessions are mapped back to their owning engine.
// Entries are added in finishConnect() and removed the moment the session is
// freed (handshake failure or Libssh2SessionState::closeNow).
QMutex g_x11RegistryMutex;
QHash<LIBSSH2_SESSION*, Libssh2Engine*> g_x11Registry;

void x11RegistryAdd(LIBSSH2_SESSION* session, Libssh2Engine* engine)
{
    QMutexLocker lock(&g_x11RegistryMutex);
    g_x11Registry.insert(session, engine);
}

void x11RegistryRemove(LIBSSH2_SESSION* session)
{
    QMutexLocker lock(&g_x11RegistryMutex);
    g_x11Registry.remove(session);
}

Libssh2Engine* x11RegistryLookup(LIBSSH2_SESSION* session)
{
    QMutexLocker lock(&g_x11RegistryMutex);
    return g_x11Registry.value(session, nullptr);
}

// Signature: LIBSSH2_X11_OPEN_FUNC macro (libssh2.h:360) / LIBSSH2_CALLBACK_X11
// (libssh2.h:399). Fires on the thread that processes packets (the worker pump)
// while the session state mutex is held - therefore it only touches its own
// queue mutex, never libssh2 calls that re-enter the session.
void libssh2X11OpenTrampoline(LIBSSH2_SESSION* session, LIBSSH2_CHANNEL* channel,
                              const char* shost, int sport, void** abstract)
{
    Q_UNUSED(abstract);
    if (!channel)
        return;
    Libssh2Engine* engine = x11RegistryLookup(session);
    if (!engine) {
        libssh2_channel_free(channel);
        return;
    }
    LOG_SSH(QStringLiteral("libssh2 inbound X11 channel from %1:%2")
                .arg(shost ? QString::fromLatin1(shost) : QStringLiteral("?"))
                .arg(sport));
    engine->enqueueX11Channel(channel);
}

} // namespace

namespace {

using namespace std::chrono_literals;

constexpr int kRetrySleepMs = 2;          // EAGAIN polling interval
constexpr int kInteractiveTimeoutMs = 600000; // user answering kbd-int prompts
constexpr int kScpChunkSize = 32768;
constexpr int kNameBufSize = 1024;
std::atomic<int> g_nextConnId { 1 };

qint64 nowMs()
{
    return QDateTime::currentMSecsSinceEpoch();
}

void sleepRetry()
{
    std::this_thread::sleep_for(std::chrono::milliseconds(kRetrySleepMs));
}

// --- EAGAIN retry loops -----------------------------------------------------

// Retry an int/ssize_t-returning libssh2 call until it completes, errors or
// the deadline expires. Returns the last result (LIBSSH2_ERROR_EAGAIN means
// "timed out").
template <typename Fn>
auto retryEagain(Fn&& fn, qint64 deadlineMs) -> decltype(fn())
{
    for (;;) {
        auto rc = fn();
        if (rc != LIBSSH2_ERROR_EAGAIN)
            return rc;
        if (nowMs() >= deadlineMs)
            return rc;
        sleepRetry();
    }
}

// Retry a pointer-returning libssh2 call (channel open, sftp init, forward
// accept...). EAGAIN is signalled by nullptr + libssh2_session_last_errno().
template <typename Fn>
auto retryEagainPtr(Fn&& fn, qint64 deadlineMs, LIBSSH2_SESSION* session) -> decltype(fn())
{
    for (;;) {
        auto* p = fn();
        if (p)
            return p;
        if (!session || libssh2_session_last_errno(session) != LIBSSH2_ERROR_EAGAIN)
            return nullptr;
        if (nowMs() >= deadlineMs)
            return nullptr;
        sleepRetry();
    }
}

// --- error text -------------------------------------------------------------

QString libssh2ErrorText(LIBSSH2_SESSION* session, int rc)
{
    char* msg = nullptr;
    int len = 0;
    libssh2_session_last_error(session, &msg, &len, 0);
    const QString detail = msg ? QString::fromUtf8(msg, len) : QString();
    if (detail.isEmpty())
        return QStringLiteral("libssh2 error %1").arg(rc);
    return QStringLiteral("libssh2 error %1: %2").arg(rc).arg(detail);
}

QString friendlyTransportError(int rc)
{
    if (rc == LIBSSH2_ERROR_AUTHENTICATION_FAILED)
        return QStringLiteral("The server rejected the credentials.");
    if (rc == LIBSSH2_ERROR_SOCKET_DISCONNECT)
        return QStringLiteral("The remote server closed the connection.");
    if (rc == LIBSSH2_ERROR_TIMEOUT || rc == LIBSSH2_ERROR_EAGAIN)
        return QStringLiteral("The operation timed out.");
    if (rc == LIBSSH2_ERROR_KEX_FAILURE || rc == LIBSSH2_ERROR_KEY_EXCHANGE_FAILURE)
        return QStringLiteral("The key exchange with the server failed.");
    if (rc == LIBSSH2_ERROR_BANNER_RECV || rc == LIBSSH2_ERROR_SOCKET_NONE
        || rc == LIBSSH2_ERROR_SOCKET_SEND || rc == LIBSSH2_ERROR_SOCKET_RECV)
        return QStringLiteral("Could not communicate with the remote server.");
    return QStringLiteral("The SSH operation failed.");
}

QString sftpStatusText(unsigned long code)
{
    switch (code) {
    case 2: return QStringLiteral("no such file");
    case 3: return QStringLiteral("permission denied");
    case 4: return QStringLiteral("general failure");
    case 5: return QStringLiteral("bad message");
    case 6: return QStringLiteral("no connection");
    case 7: return QStringLiteral("connection lost");
    case 8: return QStringLiteral("operation unsupported");
    case 9: return QStringLiteral("invalid handle");
    case 10: return QStringLiteral("no such path");
    case 11: return QStringLiteral("file already exists");
    case 12: return QStringLiteral("write protected");
    default: return QStringLiteral("status %1").arg(code);
    }
}

QString sftpErrorText(LIBSSH2_SESSION* session, LIBSSH2_SFTP* sftp, int rc)
{
    if (rc == LIBSSH2_ERROR_SFTP_PROTOCOL && sftp)
        return QStringLiteral("SFTP error: %1").arg(sftpStatusText(libssh2_sftp_last_error(sftp)));
    return libssh2ErrorText(session, rc);
}

Outcome failOp(LIBSSH2_SESSION* session, int rc, const QString& what)
{
    return Outcome::fail(what, libssh2ErrorText(session, rc), {});
}

// --- raw socket helpers (POSIX primary, Winsock guarded) ---------------------

bool setNonBlocking(libssh2_socket_t fd, QString* err)
{
#ifdef _WIN32
    u_long mode = 1;
    if (ioctlsocket(fd, FIONBIO, &mode) != 0) {
        if (err) *err = QStringLiteral("ioctlsocket(FIONBIO) failed");
        return false;
    }
#else
    const int flags = ::fcntl(fd, F_GETFL, 0);
    if (flags < 0 || ::fcntl(fd, F_SETFL, flags | O_NONBLOCK) < 0) {
        if (err) *err = QStringLiteral("fcntl(O_NONBLOCK) failed: %1").arg(std::strerror(errno));
        return false;
    }
#endif
    return true;
}

void closeSocket(libssh2_socket_t fd)
{
#ifdef _WIN32
    ::closesocket(fd);
#else
    ::close(fd);
#endif
}

// Waits until the socket becomes writable; >0 ready, 0 timeout, <0 error.
int pollWritable(libssh2_socket_t fd, qint64 timeoutMs)
{
    if (timeoutMs < 0)
        timeoutMs = 0;
#ifdef _WIN32
    fd_set wset;
    FD_ZERO(&wset);
    FD_SET(fd, &wset);
    timeval tv;
    tv.tv_sec = long(timeoutMs / 1000);
    tv.tv_usec = long((timeoutMs % 1000) * 1000);
    return ::select(0, nullptr, &wset, nullptr, &tv);
#else
    pollfd p;
    p.fd = int(fd);
    p.events = POLLOUT;
    p.revents = 0;
    return ::poll(&p, 1, int(timeoutMs));
#endif
}

std::once_flag g_wsaInit;

// Blocking-with-deadline TCP connect; the returned fd stays NON-BLOCKING
// (required by the session's non-blocking mode).
libssh2_socket_t tcpConnect(const QString& host, int port, int timeoutMs, QString* err)
{
#ifdef _WIN32
    std::call_once(g_wsaInit, [] {
        WSADATA data;
        ::WSAStartup(MAKEWORD(2, 2), &data);
    });
#endif

    const QByteArray hostB = host.toUtf8();
    char portStr[16];
    std::snprintf(portStr, sizeof(portStr), "%d", port);

    addrinfo hints;
    std::memset(&hints, 0, sizeof(hints));
    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;
    hints.ai_protocol = IPPROTO_TCP;

    addrinfo* list = nullptr;
    const int grc = ::getaddrinfo(hostB.constData(), portStr, &hints, &list);
    if (grc != 0 || !list) {
        if (err)
            *err = QStringLiteral("Could not resolve host '%1'.").arg(host);
        return LIBSSH2_INVALID_SOCKET;
    }

    const qint64 deadline = nowMs() + timeoutMs;
    libssh2_socket_t fd = LIBSSH2_INVALID_SOCKET;
    QString lastErr = QStringLiteral("no address answered");

    for (addrinfo* ai = list; ai; ai = ai->ai_next) {
        if (nowMs() >= deadline) {
            lastErr = QStringLiteral("connect timed out");
            break;
        }
        libssh2_socket_t s = ::socket(ai->ai_family, ai->ai_socktype, ai->ai_protocol);
        if (s == LIBSSH2_INVALID_SOCKET)
            continue;
        if (!setNonBlocking(s, &lastErr)) {
            closeSocket(s);
            continue;
        }
        const int cr = ::connect(s, ai->ai_addr, socklen_t(ai->ai_addrlen));
        bool inProgress = (cr == 0);
        if (cr != 0) {
#ifdef _WIN32
            inProgress = (WSAGetLastError() == WSAEWOULDBLOCK);
#else
            inProgress = (errno == EINPROGRESS);
#endif
        }
        if (!inProgress) {
#ifndef _WIN32
            lastErr = QStringLiteral("connect: %1").arg(std::strerror(errno));
#endif
            closeSocket(s);
            continue;
        }
        const int pr = pollWritable(s, deadline - nowMs());
        if (pr <= 0) {
            lastErr = (pr == 0) ? QStringLiteral("connect timed out")
                                : QStringLiteral("poll failed");
            closeSocket(s);
            continue;
        }
        int soerr = 0;
        socklen_t slen = sizeof(soerr);
        if (::getsockopt(s, SOL_SOCKET, SO_ERROR, reinterpret_cast<char*>(&soerr), &slen) != 0
            || soerr != 0) {
#ifndef _WIN32
            lastErr = QStringLiteral("connect: %1").arg(std::strerror(soerr));
#endif
            closeSocket(s);
            continue;
        }
        fd = s; // still non-blocking on purpose
        break;
    }
    ::freeaddrinfo(list);

    if (fd == LIBSSH2_INVALID_SOCKET && err)
        *err = lastErr;
    return fd;
}

// --- host key ----------------------------------------------------------------

QString hostKeyTypeString(int type)
{
    switch (type) {
    case LIBSSH2_HOSTKEY_TYPE_ED25519: return QStringLiteral("ssh-ed25519");
    case LIBSSH2_HOSTKEY_TYPE_RSA: return QStringLiteral("ssh-rsa");
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_256: return QStringLiteral("ecdsa-sha2-nistp256");
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_384: return QStringLiteral("ecdsa-sha2-nistp384");
    case LIBSSH2_HOSTKEY_TYPE_ECDSA_521: return QStringLiteral("ecdsa-sha2-nistp521");
    case LIBSSH2_HOSTKEY_TYPE_DSS: return QStringLiteral("ssh-dss");
    default: return QString();
    }
}

// --- authentication callbacks -------------------------------------------------

struct KbdIntCtx
{
    LIBSSH2_SESSION* session = nullptr;
    const std::function<QStringList(const QStringList&, const QVector<bool>&)>* promptCb = nullptr;
    qint64* deadline = nullptr;
};

// Memory returned from these callbacks is released by libssh2 with its own
// allocator pair. Our session is created with libssh2_session_init_ex(NULL,
// NULL, NULL, NULL), i.e. the default malloc/free pair, so allocating with
// std::malloc here is correctly paired with libssh2's LIBSSH2_FREE.
void* sessionAlloc(size_t n)
{
    return std::malloc(n ? n : 1);
}

// LIBSSH2_USERAUTH_KBDINT_RESPONSE_FUNC
void kbdintResponseCb(const char* name, int name_len, const char* instruction,
                      int instruction_len, int num_prompts,
                      const LIBSSH2_USERAUTH_KBDINT_PROMPT* prompts,
                      LIBSSH2_USERAUTH_KBDINT_RESPONSE* responses, void** abstract)
{
    Q_UNUSED(name);
    Q_UNUSED(name_len);
    Q_UNUSED(instruction);
    Q_UNUSED(instruction_len);
    // libssh2 passes &session->abstract; we park our context there for the
    // duration of the call (see authKeyboardInteractiveLocked()).
    auto* ctx = static_cast<KbdIntCtx*>(abstract ? *abstract : nullptr);
    if (!ctx || !ctx->promptCb || !*ctx->promptCb)
        return;

    QStringList promptTexts;
    QVector<bool> echoes;
    promptTexts.reserve(num_prompts);
    for (int i = 0; i < num_prompts; ++i) {
        promptTexts.append(QString::fromUtf8(reinterpret_cast<const char*>(prompts[i].text),
                                             int(prompts[i].length)));
        echoes.append(prompts[i].echo != 0);
    }

    // Synchronous, blocking call into the application (dialog). Answer count
    // may differ from prompt count; missing answers are sent empty.
    const QStringList answers = (*ctx->promptCb)(promptTexts, echoes);

    for (int i = 0; i < num_prompts; ++i) {
        const QByteArray answer = (i < answers.size() ? answers.at(i) : QString()).toUtf8();
        const size_t allocLen = size_t(answer.size()) + 1; // libssh2 frees with LIBSSH2_FREE
        auto* text = static_cast<char*>(sessionAlloc(allocLen));
        if (text) {
            std::memcpy(text, answer.constData(), size_t(answer.size()));
            text[answer.size()] = '\0';
        }
        responses[i].text = text;
        responses[i].length = text ? unsigned(answer.size()) : 0;
    }

    // User thinking time must not eat the network deadline of the retry loop.
    if (ctx->deadline)
        *ctx->deadline = nowMs() + kInteractiveTimeoutMs;
}

struct AgentSignCtx
{
    LIBSSH2_SESSION* session = nullptr;
    AgentClient* agent = nullptr;
    QByteArray blob;
    QString err;
};

// LIBSSH2_USERAUTH_PUBLICKEY_SIGN_FUNC - signs the session-bound challenge
// through the SSH agent. The signature buffer is allocated with LIBSSH2_ALLOC
// (libssh2 releases it with LIBSSH2_FREE).
int agentSignCb(LIBSSH2_SESSION* session, unsigned char** sig, size_t* sig_len,
                const unsigned char* data, size_t data_len, void** abstract)
{
    auto* ctx = static_cast<AgentSignCtx*>(abstract ? *abstract : nullptr);
    if (!ctx || !ctx->agent || !ctx->session)
        return -1;

    // flags: 0 = auto; 2 = RSA/SHA-512 (rsa-sha2-512) for "ssh-rsa" blobs.
    const int flags = (agentwire::peekKeyType(ctx->blob) == QLatin1String("ssh-rsa")) ? 2 : 0;

    QByteArray signature;
    const QByteArray challenge = QByteArray::fromRawData(reinterpret_cast<const char*>(data),
                                                         int(data_len));
    if (!ctx->agent->sign(ctx->blob, challenge, flags, &signature, &ctx->err)
        || signature.isEmpty())
        return -1;

    auto* buf = static_cast<unsigned char*>(sessionAlloc(size_t(signature.size())));
    if (!buf)
        return -1;
    std::memcpy(buf, signature.constData(), size_t(signature.size()));
    *sig = buf;
    *sig_len = size_t(signature.size());
    return 0;
}

// --- misc ----------------------------------------------------------------------

SftpAttrs mapAttrs(const QString& name, const LIBSSH2_SFTP_ATTRIBUTES& a)
{
    SftpAttrs out;
    out.name = name;
    if (a.flags & LIBSSH2_SFTP_ATTR_SIZE)
        out.size = quint64(a.filesize);
    const unsigned long perms = (a.flags & LIBSSH2_SFTP_ATTR_PERMISSIONS) ? a.permissions : 0;
    out.permissions = quint32(perms);
    if (a.flags & LIBSSH2_SFTP_ATTR_UIDGID) {
        out.uid = quint32(a.uid);
        out.gid = quint32(a.gid);
    }
    if (a.flags & LIBSSH2_SFTP_ATTR_ACMODTIME) {
        out.atime = qint64(a.atime);
        out.mtime = qint64(a.mtime);
    }
    out.isDir = LIBSSH2_SFTP_S_ISDIR(perms) != 0;
    out.isLink = LIBSSH2_SFTP_S_ISLNK(perms) != 0;
    out.isRegular = LIBSSH2_SFTP_S_ISREG(perms) != 0;
    out.canRead = (perms & LIBSSH2_SFTP_S_IRUSR) != 0;
    out.canWrite = (perms & LIBSSH2_SFTP_S_IWUSR) != 0;
    out.canExec = (perms & LIBSSH2_SFTP_S_IXUSR) != 0;
    return out;
}

quint32 localFileMode(const QFileInfo& fi)
{
    // QFile::Permissions uses POSIX octal bit values.
    return quint32(fi.permissions()) & 07777u;
}

// --- raw SFTP helpers (the caller must hold the session-state mutex) --------

bool sftpStatRaw(LIBSSH2_SESSION* session, LIBSSH2_SFTP* sftp, int opTimeoutMs,
                 const QString& path, bool followLinks, SftpAttrs* out)
{
    if (!session || !sftp)
        return false;
    const QByteArray p = path.toUtf8();
    LIBSSH2_SFTP_ATTRIBUTES attrs;
    const int rc = retryEagain([&] {
        return libssh2_sftp_stat_ex(sftp, p.constData(), unsigned(p.size()),
                                    followLinks ? LIBSSH2_SFTP_STAT : LIBSSH2_SFTP_LSTAT,
                                    &attrs);
    }, nowMs() + opTimeoutMs);
    if (rc != 0)
        return false;
    if (out) {
        QString name = QFileInfo(path).fileName();
        if (name.isEmpty())
            name = path;
        *out = mapAttrs(name, attrs);
    }
    return true;
}

bool sftpListRaw(LIBSSH2_SESSION* session, LIBSSH2_SFTP* sftp, int opTimeoutMs,
                 const QString& path, QVector<SftpEntry>* out, QString* err)
{
    if (!session || !sftp) {
        if (err) *err = QStringLiteral("SFTP session is not open.");
        return false;
    }
    out->clear();
    const QByteArray p = path.toUtf8();
    const qint64 deadline = nowMs() + opTimeoutMs;

    LIBSSH2_SFTP_HANDLE* dir = retryEagainPtr([&] {
        return libssh2_sftp_open_ex(sftp, p.constData(), unsigned(p.size()),
                                    0, 0, LIBSSH2_SFTP_OPENDIR);
    }, deadline, session);
    if (!dir) {
        if (err)
            *err = sftpErrorText(session, sftp, libssh2_session_last_errno(session));
        return false;
    }

    char name[kNameBufSize];
    LIBSSH2_SFTP_ATTRIBUTES attrs;
    for (;;) {
        std::memset(&attrs, 0, sizeof(attrs));
        name[0] = '\0';
        const int rc = retryEagain([&] {
            return libssh2_sftp_readdir_ex(dir, name, sizeof(name), nullptr, 0, &attrs);
        }, deadline);
        if (rc <= 0)
            break; // 0 = end of listing, <0 = error (both stop the loop)
        const QString nm = QString::fromUtf8(name, rc);
        if (nm == QLatin1String(".") || nm == QLatin1String(".."))
            continue;
        SftpEntry entry;
        entry.name = nm;
        entry.attrs = mapAttrs(nm, attrs);
        out->append(entry);
    }

    retryEagain([&] { return libssh2_sftp_close_handle(dir); },
                nowMs() + kShortTimeoutMs);
    return true;
}

bool sftpMkdirRaw(LIBSSH2_SESSION* session, LIBSSH2_SFTP* sftp, int opTimeoutMs,
                  const QString& path, quint32 mode, QString* err)
{
    if (!session || !sftp) {
        if (err) *err = QStringLiteral("SFTP session is not open.");
        return false;
    }
    const QByteArray p = path.toUtf8();
    const int rc = retryEagain([&] {
        return libssh2_sftp_mkdir_ex(sftp, p.constData(), unsigned(p.size()),
                                     int(mode));
    }, nowMs() + opTimeoutMs);
    if (rc == 0)
        return true;
    if (err)
        *err = sftpErrorText(session, sftp, rc);
    return false;
}

// Fetches one remote file to a local path over SFTP (atomic via QSaveFile).
// The caller must hold the session-state mutex.
Outcome sftpFetchFileRaw(const Libssh2StatePtr& state, LIBSSH2_SFTP* sftp,
                         const QString& remotePath, const QString& localPath,
                         const std::function<bool(quint64, quint64)>& progress)
{
    auto st = state;
    LIBSSH2_SESSION* session = st->session;
    if (!session || !sftp)
        return Outcome::fail(QStringLiteral("SFTP session is not open."));

    const QByteArray rp = remotePath.toUtf8();
    LIBSSH2_SFTP_HANDLE* h = retryEagainPtr([&] {
        return libssh2_sftp_open_ex(sftp, rp.constData(), unsigned(rp.size()),
                                    LIBSSH2_FXF_READ, 0, LIBSSH2_SFTP_OPENFILE);
    }, nowMs() + st->opTimeoutMs, session);
    if (!h)
        return Outcome::fail(QStringLiteral("Cannot open remote file '%1'.").arg(remotePath),
                             sftpErrorText(session, sftp, libssh2_session_last_errno(session)), {});

    quint64 size = 0;
    LIBSSH2_SFTP_ATTRIBUTES attrs;
    std::memset(&attrs, 0, sizeof(attrs));
    // Non-blocking session: fstat needs the EAGAIN pump, a bare call returns
    // EAGAIN on the first invocation and the size would read as 0.
    const int src = retryEagain([&] { return libssh2_sftp_fstat_ex(h, &attrs, 0); },
                                nowMs() + st->opTimeoutMs);
    if (src == 0 && (attrs.flags & LIBSSH2_SFTP_ATTR_SIZE))
        size = quint64(attrs.filesize);

    QSaveFile out(localPath);
    if (!out.open(QIODevice::WriteOnly)) {
        retryEagain([&] { return libssh2_sftp_close_handle(h); }, nowMs() + kShortTimeoutMs);
        return Outcome::fail(QStringLiteral("Cannot write local file '%1'.").arg(localPath),
                             out.errorString(), {});
    }

    Outcome result = Outcome::success();
    quint64 received = 0;
    char buf[kScpChunkSize];
    qint64 deadline = nowMs() + st->opTimeoutMs;
    for (;;) {
        const ssize_t n = retryEagain([&] {
            return libssh2_sftp_read(h, buf, sizeof(buf));
        }, deadline);
        if (n == LIBSSH2_ERROR_EAGAIN) {
            result = Outcome::fail(QStringLiteral("The download of '%1' timed out.").arg(remotePath));
            break;
        }
        if (n < 0) {
            result = Outcome::fail(QStringLiteral("The download of '%1' failed.").arg(remotePath),
                                   sftpErrorText(session, sftp, int(n)), {});
            break;
        }
        if (n == 0)
            break; // EOF
        if (out.write(buf, int(n)) != n) {
            result = Outcome::fail(QStringLiteral("Cannot write local file '%1'.").arg(localPath),
                                   out.errorString(), {});
            break;
        }
        received += quint64(n);
        deadline = nowMs() + st->opTimeoutMs; // progress resets the deadline
        if (progress && !progress(received, size ? size : received)) {
            result = Outcome::fail(QStringLiteral("Download of '%1' cancelled.").arg(remotePath));
            break;
        }
    }

    retryEagain([&] { return libssh2_sftp_close_handle(h); }, nowMs() + kShortTimeoutMs);
    if (result.ok && !out.commit())
        result = Outcome::fail(QStringLiteral("Cannot save local file '%1'.").arg(localPath),
                               out.errorString(), {});
    if (!result.ok)
        out.cancelWriting();
    return result;
}

QString localReadError(const QString& path)
{
    return QStringLiteral("Cannot read local file '%1'.").arg(path);
}

} // namespace


// ===========================================================================
// Libssh2Channel
// ===========================================================================

Libssh2Channel::Libssh2Channel(Libssh2StatePtr state, LIBSSH2_CHANNEL* channel, ChannelKind kind)
    : m_state(std::move(state))
    , m_channel(channel)
    , m_kind(kind)
{
}

Outcome Libssh2Channel::openShell(int cols, int rows, const QStringList& env)
{
    QMap<QString, QString> envMap;
    for (const QString& kv : env) {
        const int eq = kv.indexOf(QLatin1Char('='));
        if (eq > 0)
            envMap.insert(kv.left(eq), kv.mid(eq + 1));
    }
    QString err;
    if (!startShell(cols, rows, envMap, &err))
        return Outcome::fail(err.isEmpty() ? QStringLiteral("Shell request was rejected by the server.") : err,
                             QStringLiteral("libssh2 channel shell"));
    return Outcome::success();
}

Outcome Libssh2Channel::openExecChannel(const QString& command)
{
    QString err;
    if (!startExec(command, &err))
        return Outcome::fail(err.isEmpty() ? QStringLiteral("The command could not be executed.") : err,
                             QStringLiteral("libssh2 channel exec"));
    return Outcome::success();
}

Libssh2Channel::~Libssh2Channel()
{
    close();
}

bool Libssh2Channel::isOpen() const
{
    auto st = m_state;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    return m_channel != nullptr && !m_closed && st->connected;
}

bool Libssh2Channel::isEof() const
{
    auto st = m_state;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    if (m_closed || !m_channel || !st->session)
        return true; // a freed session also freed this channel
    return libssh2_channel_eof(m_channel) != 0;
}

int Libssh2Channel::readStdout(char* buf, int len)
{
    if (len <= 0)
        return 0;
    auto st = m_state;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    if (!m_channel || m_closed || !st->session)
        return -1;
    const ssize_t rc = libssh2_channel_read_ex(m_channel, 0, buf, size_t(len));
    if (rc == LIBSSH2_ERROR_EAGAIN)
        return 0; // nothing available right now
    if (rc < 0)
        return -1;
    return int(rc);
}

int Libssh2Channel::readStderr(char* buf, int len)
{
    if (len <= 0)
        return 0;
    auto st = m_state;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    if (!m_channel || m_closed || !st->session)
        return -1;
    const ssize_t rc = libssh2_channel_read_ex(m_channel, SSH_EXTENDED_DATA_STDERR,
                                               buf, size_t(len));
    if (rc == LIBSSH2_ERROR_EAGAIN)
        return 0;
    if (rc < 0)
        return -1;
    return int(rc);
}

int Libssh2Channel::write(const char* buf, int len)
{
    if (len <= 0)
        return 0;
    auto st = m_state;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    if (!m_channel || m_closed || !st->session)
        return -1;

    qint64 deadline = nowMs() + st->opTimeoutMs;
    int total = 0;
    while (total < len) {
        const ssize_t rc = libssh2_channel_write_ex(m_channel, 0, buf + total,
                                                    size_t(len - total));
        if (rc == LIBSSH2_ERROR_EAGAIN) {
            if (nowMs() >= deadline)
                break;
            sleepRetry();
            continue;
        }
        if (rc < 0) {
            if (total == 0)
                return -1;
            break; // partial write then error: report what was accepted
        }
        total += int(rc);
        deadline = nowMs() + st->opTimeoutMs; // progress resets the deadline
        if (rc == 0)
            break;
    }
    return total;
}

void Libssh2Channel::resizePty(int cols, int rows)
{
    auto st = m_state;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    if (!m_channel || m_closed || !st->session)
        return;
    retryEagain([&] {
        return libssh2_channel_request_pty_size_ex(m_channel, cols, rows, 0, 0);
    }, nowMs() + kShortTimeoutMs);
}

void Libssh2Channel::sendEof()
{
    auto st = m_state;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    if (!m_channel || m_closed || !st->session)
        return;
    retryEagain([&] { return libssh2_channel_send_eof(m_channel); },
                nowMs() + kShortTimeoutMs);
}

void Libssh2Channel::close()
{
    auto st = m_state;
    if (!st)
        return;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    if (!m_channel)
        return;
    if (!st->session) {
        // libssh2_session_free() already destroyed this channel; the pointer
        // is dangling and must simply be dropped.
        m_channel = nullptr;
        m_closed = true;
        return;
    }
    if (!m_closed) {
        retryEagain([&] { return libssh2_channel_close(m_channel); },
                    nowMs() + kShortTimeoutMs);
        m_cachedExitStatus = libssh2_channel_get_exit_status(m_channel);
        m_closed = true;
    }
    // free() may report EAGAIN in non-blocking mode; force with a short retry.
    int rcf = 0;
    const qint64 deadline = nowMs() + kShortTimeoutMs;
    for (;;) {
        rcf = libssh2_channel_free(m_channel);
        if (rcf != LIBSSH2_ERROR_EAGAIN || nowMs() >= deadline)
            break;
        sleepRetry();
    }
    if (rcf == LIBSSH2_ERROR_EAGAIN) {
        // Blocking mode makes free() finish immediately; we hold the state
        // mutex, so no other thread can be inside libssh2 right now.
        libssh2_session_set_blocking(st->session, 1);
        libssh2_channel_free(m_channel);
        libssh2_session_set_blocking(st->session, 0); // back to non-blocking
    }
    m_channel = nullptr;
}

int Libssh2Channel::exitStatus()
{
    auto st = m_state;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    if (m_channel && !m_closed && st->session)
        m_cachedExitStatus = libssh2_channel_get_exit_status(m_channel);
    return m_cachedExitStatus;
}

bool Libssh2Channel::startShell(int cols, int rows, const QMap<QString, QString>& env,
                                QString* err)
{
    auto st = m_state;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    if (!m_channel || !st->session) {
        if (err)
            *err = QStringLiteral("Channel is not open.");
        return false;
    }
    const qint64 deadline = nowMs() + st->opTimeoutMs;

    // 1. PTY (xterm-256color, caller-provided size, no modes).
    static const char kTerm[] = "xterm-256color";
    int rc = retryEagain([&] {
        return libssh2_channel_request_pty_ex(m_channel, kTerm, unsigned(sizeof(kTerm) - 1),
                                              nullptr, 0, cols, rows, 0, 0);
    }, deadline);
    if (rc != 0) {
        if (err)
            *err = libssh2ErrorText(st->session, rc);
        LOG_SSH_ERR(QStringLiteral("libssh2[%1] pty request failed").arg(st->connId));
        return false;
    }

    // 2. Environment variables (best effort, errors ignored).
    for (auto it = env.constBegin(); it != env.constEnd(); ++it) {
        const QByteArray var = it.key().toUtf8();
        const QByteArray val = it.value().toUtf8();
        retryEagain([&] {
            return libssh2_channel_setenv_ex(m_channel, var.constData(),
                                             unsigned(var.size()), val.constData(),
                                             unsigned(val.size()));
        }, deadline);
    }

    // 3. Start the shell.
    rc = retryEagain([&] { return libssh2_channel_shell(m_channel); }, deadline);
    if (rc != 0) {
        if (err)
            *err = libssh2ErrorText(st->session, rc);
        LOG_SSH_ERR(QStringLiteral("libssh2[%1] shell request failed").arg(st->connId));
        return false;
    }
    LOG_SSH(QStringLiteral("libssh2[%1] shell channel started (%2x%3)")
                .arg(st->connId).arg(cols).arg(rows));
    return true;
}

bool Libssh2Channel::startExec(const QString& command, QString* err)
{
    auto st = m_state;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    if (!m_channel || !st->session) {
        if (err)
            *err = QStringLiteral("Channel is not open.");
        return false;
    }
    const QByteArray cmd = command.toUtf8();
    const int rc = retryEagain([&] {
        return libssh2_channel_exec(m_channel, cmd.constData());
    }, nowMs() + st->opTimeoutMs);
    if (rc != 0) {
        if (err)
            *err = libssh2ErrorText(st->session, rc);
        return false;
    }
    return true;
}

Outcome Libssh2Channel::requestX11(int screenNumber, const QString& authCookie, QString* err)
{
    auto st = m_state;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    if (!m_channel || !st->session) {
        if (err)
            *err = QStringLiteral("Channel is not open.");
        return Outcome::fail(QStringLiteral("Channel is not open."));
    }
    // libssh2_channel_x11_req_ex (libssh2.h:908); single_connection=0 keeps
    // forwarding alive for the session. The fake cookie is rewritten by the
    // worker on the local display connection (see SshWorker pump).
    const QByteArray cookie = authCookie.toLatin1();
    const int rc = retryEagain([&] {
        return libssh2_channel_x11_req_ex(m_channel, 0, "MIT-MAGIC-COOKIE-1",
                                          cookie.constData(), screenNumber);
    }, nowMs() + st->opTimeoutMs);
    if (rc != 0) {
        if (err)
            *err = libssh2ErrorText(st->session, rc);
        return Outcome::fail(QStringLiteral("The server rejected the X11 forwarding request."),
                             libssh2ErrorText(st->session, rc));
    }
    return Outcome::success();
}

// ===========================================================================
// Libssh2Sftp
// ===========================================================================

Libssh2Sftp::Libssh2Sftp(Libssh2StatePtr state, LIBSSH2_SFTP* sftp)
    : m_state(std::move(state))
    , m_sftp(sftp)
{
}

Libssh2Sftp::~Libssh2Sftp()
{
    auto st = m_state;
    if (!st)
        return;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    if (m_sftp) {
        if (st->session) {
            retryEagain([&] { return libssh2_sftp_shutdown(m_sftp); },
                        nowMs() + kShortTimeoutMs);
        }
        // else: the session was freed and took the SFTP structure with it.
        m_sftp = nullptr;
    }
}

bool Libssh2Sftp::isValid() const
{
    auto st = m_state;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    return m_sftp != nullptr && st->session != nullptr;
}

QString Libssh2Sftp::canonicalize(const QString& path)
{
    auto st = m_state;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    if (!m_sftp || !st->session)
        return path;
    const QByteArray p = path.toUtf8();
    char buf[kNameBufSize];
    const int rc = retryEagain([&] {
        return libssh2_sftp_symlink_ex(m_sftp, p.constData(), unsigned(p.size()),
                                       buf, sizeof(buf), LIBSSH2_SFTP_REALPATH);
    }, nowMs() + st->opTimeoutMs);
    if (rc <= 0)
        return path; // graceful fallback
    return QString::fromUtf8(buf, rc);
}

QVector<SftpEntry> Libssh2Sftp::listDir(const QString& path, QString* err)
{
    auto st = m_state;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    QVector<SftpEntry> out;
    if (!m_sftp || !st->session) {
        if (err)
            *err = QStringLiteral("SFTP session is not open.");
        return out;
    }
    sftpListRaw(st->session, m_sftp, st->opTimeoutMs, path, &out, err);
    return out;
}

bool Libssh2Sftp::stat(const QString& path, SftpAttrs* out, bool followLinks)
{
    auto st = m_state;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    return sftpStatRaw(st->session, m_sftp, st->opTimeoutMs, path, followLinks, out);
}

bool Libssh2Sftp::mkdir(const QString& path, quint32 mode, QString* err)
{
    auto st = m_state;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    if (!m_sftp || !st->session) {
        if (err)
            *err = QStringLiteral("SFTP session is not open.");
        return false;
    }
    const QByteArray p = path.toUtf8();
    const int rc = retryEagain([&] {
        return libssh2_sftp_mkdir_ex(m_sftp, p.constData(), unsigned(p.size()),
                                     mode != 0 ? int(mode) : 0755);
    }, nowMs() + st->opTimeoutMs);
    if (rc == 0)
        return true;
    if (err)
        *err = sftpErrorText(st->session, m_sftp, rc);
    return false;
}

bool Libssh2Sftp::rmdir(const QString& path, QString* err)
{
    auto st = m_state;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    if (!m_sftp || !st->session) {
        if (err)
            *err = QStringLiteral("SFTP session is not open.");
        return false;
    }
    const QByteArray p = path.toUtf8();
    const int rc = retryEagain([&] {
        return libssh2_sftp_rmdir_ex(m_sftp, p.constData(), unsigned(p.size()));
    }, nowMs() + st->opTimeoutMs);
    if (rc == 0)
        return true;
    if (err)
        *err = sftpErrorText(st->session, m_sftp, rc);
    return false;
}

bool Libssh2Sftp::unlink(const QString& path, QString* err)
{
    auto st = m_state;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    if (!m_sftp || !st->session) {
        if (err)
            *err = QStringLiteral("SFTP session is not open.");
        return false;
    }
    const QByteArray p = path.toUtf8();
    const int rc = retryEagain([&] {
        return libssh2_sftp_unlink_ex(m_sftp, p.constData(), unsigned(p.size()));
    }, nowMs() + st->opTimeoutMs);
    if (rc == 0)
        return true;
    if (err)
        *err = sftpErrorText(st->session, m_sftp, rc);
    return false;
}

bool Libssh2Sftp::rename(const QString& from, const QString& to, QString* err)
{
    auto st = m_state;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    if (!m_sftp || !st->session) {
        if (err)
            *err = QStringLiteral("SFTP session is not open.");
        return false;
    }
    const QByteArray fb = from.toUtf8();
    const QByteArray tb = to.toUtf8();
    const qint64 deadline = nowMs() + st->opTimeoutMs;

    int rc = retryEagain([&] {
        return libssh2_sftp_rename_ex(m_sftp, fb.constData(), unsigned(fb.size()),
                                      tb.constData(), unsigned(tb.size()), 0);
    }, deadline);
    if (rc != 0) {
        // If the target exists, remove it and retry once.
        SftpAttrs targetAttrs;
        if (stat(to, &targetAttrs, false)) {
            retryEagain([&] {
                return libssh2_sftp_unlink_ex(m_sftp, tb.constData(), unsigned(tb.size()));
            }, deadline);
            rc = retryEagain([&] {
                return libssh2_sftp_rename_ex(m_sftp, fb.constData(), unsigned(fb.size()),
                                              tb.constData(), unsigned(tb.size()), 0);
            }, deadline);
        }
    }
    if (rc == 0)
        return true;
    if (err)
        *err = sftpErrorText(st->session, m_sftp, rc);
    return false;
}

bool Libssh2Sftp::setPermissions(const QString& path, quint32 mode, QString* err)
{
    auto st = m_state;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    if (!m_sftp || !st->session) {
        if (err)
            *err = QStringLiteral("SFTP session is not open.");
        return false;
    }
    const QByteArray p = path.toUtf8();
    const qint64 deadline = nowMs() + st->opTimeoutMs;

    LIBSSH2_SFTP_ATTRIBUTES attrs;
    int rc = retryEagain([&] {
        return libssh2_sftp_stat_ex(m_sftp, p.constData(), unsigned(p.size()),
                                    LIBSSH2_SFTP_LSTAT, &attrs);
    }, deadline);
    if (rc != 0) {
        if (err)
            *err = sftpErrorText(st->session, m_sftp, rc);
        return false;
    }
    // Preserve the type bits, replace the permission bits.
    attrs.permissions = (attrs.permissions & ~07777ul) | static_cast<unsigned long>(mode) & 07777ul;
    attrs.flags |= LIBSSH2_SFTP_ATTR_PERMISSIONS;
    rc = retryEagain([&] {
        return libssh2_sftp_stat_ex(m_sftp, p.constData(), unsigned(p.size()),
                                    LIBSSH2_SFTP_SETSTAT, &attrs);
    }, deadline);
    if (rc == 0)
        return true;
    if (err)
        *err = sftpErrorText(st->session, m_sftp, rc);
    return false;
}

bool Libssh2Sftp::setOwnership(const QString& path, quint32 uid, quint32 gid, QString* err)
{
    auto st = m_state;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    if (!m_sftp || !st->session) {
        if (err)
            *err = QStringLiteral("SFTP session is not open.");
        return false;
    }
    const QByteArray p = path.toUtf8();
    const qint64 deadline = nowMs() + st->opTimeoutMs;

    LIBSSH2_SFTP_ATTRIBUTES attrs;
    int rc = retryEagain([&] {
        return libssh2_sftp_stat_ex(m_sftp, p.constData(), unsigned(p.size()),
                                    LIBSSH2_SFTP_LSTAT, &attrs);
    }, deadline);
    if (rc != 0) {
        if (err)
            *err = sftpErrorText(st->session, m_sftp, rc);
        return false;
    }
    attrs.uid = uid;
    attrs.gid = gid;
    attrs.flags |= LIBSSH2_SFTP_ATTR_UIDGID;
    rc = retryEagain([&] {
        return libssh2_sftp_stat_ex(m_sftp, p.constData(), unsigned(p.size()),
                                    LIBSSH2_SFTP_SETSTAT, &attrs);
    }, deadline);
    if (rc == 0)
        return true;
    if (err)
        *err = sftpErrorText(st->session, m_sftp, rc);
    return false;
}

bool Libssh2Sftp::createSymlink(const QString& target, const QString& linkPath, QString* err)
{
    auto st = m_state;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    if (!m_sftp || !st->session) {
        if (err)
            *err = QStringLiteral("SFTP session is not open.");
        return false;
    }
    const QByteArray tb = target.toUtf8();
    QByteArray lb = linkPath.toUtf8();
    const int rc = retryEagain([&] {
        return libssh2_sftp_symlink_ex(m_sftp, tb.constData(), unsigned(tb.size()),
                                       lb.data(), unsigned(lb.size()),
                                       LIBSSH2_SFTP_SYMLINK);
    }, nowMs() + st->opTimeoutMs);
    if (rc == 0)
        return true;
    if (err)
        *err = sftpErrorText(st->session, m_sftp, rc);
    return false;
}

QString Libssh2Sftp::readLink(const QString& path)
{
    auto st = m_state;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    if (!m_sftp || !st->session)
        return {};
    const QByteArray p = path.toUtf8();
    char buf[kNameBufSize];
    const int rc = retryEagain([&] {
        return libssh2_sftp_symlink_ex(m_sftp, p.constData(), unsigned(p.size()),
                                       buf, sizeof(buf), LIBSSH2_SFTP_READLINK);
    }, nowMs() + st->opTimeoutMs);
    if (rc <= 0)
        return {};
    return QString::fromUtf8(buf, rc);
}

ISftpSession::FileHandle Libssh2Sftp::openForRead(const QString& path, quint64* sizeOut,
                                                  QString* err)
{
    auto st = m_state;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    if (!m_sftp || !st->session) {
        if (err)
            *err = QStringLiteral("SFTP session is not open.");
        return nullptr;
    }
    const QByteArray p = path.toUtf8();
    LIBSSH2_SFTP_HANDLE* h = retryEagainPtr([&] {
        return libssh2_sftp_open_ex(m_sftp, p.constData(), unsigned(p.size()),
                                    LIBSSH2_FXF_READ, 0, LIBSSH2_SFTP_OPENFILE);
    }, nowMs() + st->opTimeoutMs, st->session);
    if (!h) {
        if (err)
            *err = sftpErrorText(st->session, m_sftp,
                                 libssh2_session_last_errno(st->session));
        return nullptr;
    }
    if (sizeOut) {
        LIBSSH2_SFTP_ATTRIBUTES attrs;
        std::memset(&attrs, 0, sizeof(attrs));
        // Non-blocking session: a bare fstat returns EAGAIN on first call,
        // which silently reported size 0 for freshly opened files.
        const int frc = retryEagain([&] { return libssh2_sftp_fstat_ex(h, &attrs, 0); },
                                    nowMs() + st->opTimeoutMs);
        if (frc == 0 && (attrs.flags & LIBSSH2_SFTP_ATTR_SIZE))
            *sizeOut = quint64(attrs.filesize);
        else
            *sizeOut = 0;
    }
    return h;
}

ISftpSession::FileHandle Libssh2Sftp::openForWrite(const QString& path, quint64 /*sizeHint*/,
                                                   bool append, quint32 mode, QString* err)
{
    // sizeHint: SFTP v3 has no pre-allocation; the hint is ignored.
    auto st = m_state;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    if (!m_sftp || !st->session) {
        if (err)
            *err = QStringLiteral("SFTP session is not open.");
        return nullptr;
    }
    const QByteArray p = path.toUtf8();
    const unsigned long flags = LIBSSH2_FXF_WRITE | LIBSSH2_FXF_CREAT
                                | (append ? LIBSSH2_FXF_APPEND : LIBSSH2_FXF_TRUNC);
    LIBSSH2_SFTP_HANDLE* h = retryEagainPtr([&] {
        return libssh2_sftp_open_ex(m_sftp, p.constData(), unsigned(p.size()),
                                    flags, mode != 0 ? long(mode) : 0644L,
                                    LIBSSH2_SFTP_OPENFILE);
    }, nowMs() + st->opTimeoutMs, st->session);
    if (!h) {
        if (err)
            *err = sftpErrorText(st->session, m_sftp,
                                 libssh2_session_last_errno(st->session));
        return nullptr;
    }
    return h;
}

int Libssh2Sftp::readFile(FileHandle fh, char* buf, int len, QString* err)
{
    auto st = m_state;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    auto* h = static_cast<LIBSSH2_SFTP_HANDLE*>(fh);
    if (!m_sftp || !st->session || !h) {
        if (err)
            *err = QStringLiteral("SFTP session or file handle is not open.");
        return -1;
    }
    const qint64 deadline = nowMs() + st->opTimeoutMs;
    const ssize_t rc = retryEagain([&] {
        return libssh2_sftp_read(h, buf, size_t(len));
    }, deadline);
    if (rc == LIBSSH2_ERROR_EAGAIN) {
        if (err)
            *err = QStringLiteral("Timed out reading from the remote file.");
        return -1;
    }
    if (rc < 0) {
        if (err)
            *err = sftpErrorText(st->session, m_sftp, int(rc));
        return -1;
    }
    return int(rc); // 0 = EOF; short reads are normal
}

int Libssh2Sftp::writeFile(FileHandle fh, const char* buf, int len, QString* err)
{
    auto st = m_state;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    auto* h = static_cast<LIBSSH2_SFTP_HANDLE*>(fh);
    if (!m_sftp || !st->session || !h) {
        if (err)
            *err = QStringLiteral("SFTP session or file handle is not open.");
        return -1;
    }
    qint64 deadline = nowMs() + st->opTimeoutMs;
    int total = 0;
    while (total < len) {
        const ssize_t rc = libssh2_sftp_write(h, buf + total, size_t(len - total));
        if (rc == LIBSSH2_ERROR_EAGAIN) {
            if (nowMs() >= deadline)
                break;
            sleepRetry();
            continue;
        }
        if (rc < 0) {
            if (err)
                *err = sftpErrorText(st->session, m_sftp, int(rc));
            return total > 0 ? total : -1; // partial progress is reported back
        }
        total += int(rc);
        deadline = nowMs() + st->opTimeoutMs; // progress resets the deadline
        if (rc == 0)
            break;
    }
    return total;
}

bool Libssh2Sftp::seekFile(FileHandle fh, quint64 offset)
{
    auto st = m_state;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    auto* h = static_cast<LIBSSH2_SFTP_HANDLE*>(fh);
    if (!h || !st->session)
        return false;
    libssh2_sftp_seek64(h, offset);
    return true;
}

void Libssh2Sftp::closeFile(FileHandle fh)
{
    auto st = m_state;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    auto* h = static_cast<LIBSSH2_SFTP_HANDLE*>(fh);
    if (!h)
        return;
    retryEagain([&] { return libssh2_sftp_close_handle(h); },
                nowMs() + kShortTimeoutMs);
}

bool Libssh2Sftp::truncate(const QString& path, QString* err)
{
    auto st = m_state;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    if (!m_sftp || !st->session) {
        if (err)
            *err = QStringLiteral("SFTP session is not open.");
        return false;
    }
    const QByteArray p = path.toUtf8();
    LIBSSH2_SFTP_HANDLE* h = retryEagainPtr([&] {
        return libssh2_sftp_open_ex(m_sftp, p.constData(), unsigned(p.size()),
                                    LIBSSH2_FXF_WRITE | LIBSSH2_FXF_TRUNC, 0,
                                    LIBSSH2_SFTP_OPENFILE);
    }, nowMs() + st->opTimeoutMs, st->session);
    if (!h) {
        if (err)
            *err = sftpErrorText(st->session, m_sftp,
                                 libssh2_session_last_errno(st->session));
        return false;
    }
    retryEagain([&] { return libssh2_sftp_close_handle(h); },
                nowMs() + kShortTimeoutMs);
    return true;
}

// ===========================================================================
// Libssh2Scp
// ===========================================================================

Libssh2Scp::Libssh2Scp(Libssh2StatePtr state)
    : m_state(std::move(state))
{
}

Libssh2Scp::~Libssh2Scp()
{
    auto st = m_state;
    if (!st)
        return;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    if (m_sftp && st->session)
        shutdownSftpLocked();
    else
        m_sftp = nullptr; // session free took the SFTP structure with it
}

LIBSSH2_SFTP* Libssh2Scp::ensureSftpLocked(QString* err)
{
    auto st = m_state;
    if (!st->session)
        return nullptr;
    if (m_sftp)
        return m_sftp;
    m_sftp = retryEagainPtr([&] { return libssh2_sftp_init(st->session); },
                            nowMs() + st->opTimeoutMs, st->session);
    if (!m_sftp && err)
        *err = libssh2ErrorText(st->session, LIBSSH2_ERROR_SFTP_PROTOCOL);
    return m_sftp;
}

void Libssh2Scp::shutdownSftpLocked()
{
    if (!m_sftp)
        return;
    retryEagain([&] { return libssh2_sftp_shutdown(m_sftp); },
                nowMs() + kShortTimeoutMs);
    m_sftp = nullptr;
}

Outcome Libssh2Scp::sendTo(const QString& localPath, const QString& remotePath,
                           bool recursive, ProgressFn progress)
{
    auto st = m_state;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    if (!st->session || !st->connected)
        return Outcome::fail(QStringLiteral("Not connected."));
    if (recursive) {
        quint64 filesSent = 0;
        return sendDirLocked(localPath, remotePath, progress, &filesSent);
    }
    return sendFileLocked(localPath, remotePath, progress);
}

Outcome Libssh2Scp::sendFileLocked(const QString& localPath, const QString& remotePath,
                                   const ProgressFn& progress)
{
    auto st = m_state;
    const QFileInfo fi(localPath);
    if (!fi.isFile() && !fi.isSymLink())
        return Outcome::fail(QStringLiteral("Local file '%1' does not exist.").arg(localPath));

    QFile in(localPath);
    if (!in.open(QIODevice::ReadOnly))
        return Outcome::fail(localReadError(localPath),
                             in.errorString(),
                             QStringLiteral("Check the file permissions."));

    const qint64 size = fi.isSymLink() ? in.size() : fi.size();
    const int mode = int(localFileMode(fi));
    const time_t mtime = time_t(fi.lastModified().toSecsSinceEpoch());
    const time_t atime = time_t(fi.lastRead().toSecsSinceEpoch());

    const QByteArray rp = remotePath.toUtf8();
    LIBSSH2_CHANNEL* chan = retryEagainPtr([&] {
        return libssh2_scp_send64(st->session, rp.constData(), mode,
                                  libssh2_int64_t(size), mtime, atime);
    }, nowMs() + st->opTimeoutMs, st->session);
    if (!chan) {
        return Outcome::fail(QStringLiteral("The server refused the upload of '%1'.").arg(remotePath),
                             libssh2ErrorText(st->session,
                                              libssh2_session_last_errno(st->session)),
                             QStringLiteral("Check the destination permissions and disk space."));
    }

    Outcome result = Outcome::success();
    quint64 sent = 0;
    char buf[kScpChunkSize];
    qint64 deadline = nowMs() + st->opTimeoutMs;

    while (sent < quint64(size)) {
        const qint64 n = in.read(buf, kScpChunkSize);
        if (n <= 0) {
            result = Outcome::fail(localReadError(localPath), in.errorString(), {});
            break;
        }
        qint64 off = 0;
        bool writeFailed = false;
        while (off < n) {
            const ssize_t w = libssh2_channel_write_ex(chan, 0, buf + off, size_t(n - off));
            if (w == LIBSSH2_ERROR_EAGAIN) {
                if (nowMs() >= deadline) {
                    result = Outcome::fail(QStringLiteral("The upload of '%1' timed out.").arg(remotePath),
                                           QStringLiteral("channel write stalled"), {});
                    writeFailed = true;
                    break;
                }
                sleepRetry();
                continue;
            }
            if (w < 0) {
                result = Outcome::fail(QStringLiteral("The upload of '%1' failed.").arg(remotePath),
                                       libssh2ErrorText(st->session, int(w)), {});
                writeFailed = true;
                break;
            }
            off += w;
            sent += quint64(w);
            deadline = nowMs() + st->opTimeoutMs; // progress resets the deadline
        }
        if (writeFailed)
            break;
        if (progress && !progress(sent, quint64(size))) {
            result = Outcome::fail(QStringLiteral("Upload of '%1' cancelled.").arg(remotePath));
            break;
        }
    }

    // Teardown (best effort): EOF, wait for remote close, free.
    retryEagain([&] { return libssh2_channel_send_eof(chan); }, nowMs() + kShortTimeoutMs);
    retryEagain([&] { return libssh2_channel_close(chan); }, nowMs() + kShortTimeoutMs);
    retryEagain([&] { return libssh2_channel_free(chan); }, nowMs() + kShortTimeoutMs);

    if (!result.ok)
        LOG_SSH_ERR(QStringLiteral("libssh2[%1] scp send failed").arg(st->connId));
    return result;
}

Outcome Libssh2Scp::sendDirLocked(const QString& localPath, const QString& remotePath,
                                  const ProgressFn& progress, quint64* filesSent)
{
    auto st = m_state;
    if (!ensureSftpLocked(nullptr))
        return Outcome::fail(QStringLiteral("Could not start SFTP for the recursive upload."));

    // Plan the whole tree first: remote directories are created on the way,
    // files (with sizes) are queued so progress can be cumulative.
    struct Item { QString local; QString remote; qint64 size; };
    QVector<Item> items;
    quint64 totalBytes = 0;
    QString perr;

    std::function<bool(const QString&, const QString&)> flatten =
        [&](const QString& ldir, const QString& rdir) -> bool {
        const QFileInfo fi(ldir);
        if (!fi.exists()) {
            perr = QStringLiteral("Local path '%1' does not exist.").arg(ldir);
            return false;
        }
        if (fi.isDir()) {
            SftpAttrs remoteAttrs;
            if (!sftpStatRaw(st->session, m_sftp, st->opTimeoutMs, rdir, false, &remoteAttrs)) {
                const quint32 mode = localFileMode(fi) | 0700u;
                if (!sftpMkdirRaw(st->session, m_sftp, st->opTimeoutMs, rdir, mode, &perr)) {
                    perr = QStringLiteral("Could not create remote directory '%1': %2").arg(rdir, perr);
                    return false;
                }
            } else if (!remoteAttrs.isDir) {
                perr = QStringLiteral("Remote path '%1' exists and is not a directory.").arg(rdir);
                return false;
            }
            const QFileInfoList entries = QDir(ldir).entryInfoList(
                QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::NoSymLinks
                | QDir::Hidden | QDir::System);
            for (const QFileInfo& e : entries) {
                const QString remoteChild = rdir + QLatin1Char('/') + e.fileName();
                if (!flatten(e.absoluteFilePath(), remoteChild))
                    return false;
            }
            return true;
        }
        Item item;
        item.local = fi.absoluteFilePath();
        item.remote = rdir;
        item.size = fi.size();
        totalBytes += quint64(item.size);
        items.append(item);
        return true;
    };

    if (!flatten(localPath, remotePath))
        return Outcome::fail(perr.isEmpty() ? QStringLiteral("Upload planning failed.") : perr);

    quint64 doneBytes = 0;
    for (const Item& item : items) {
        Outcome r = sendFileLocked(item.local, item.remote, ProgressFn());
        if (!r.ok)
            return r;
        doneBytes += quint64(item.size);
        *filesSent += 1;
        if (progress && !progress(doneBytes, totalBytes))
            return Outcome::fail(QStringLiteral("Upload cancelled."));
    }
    LOG_SSH(QStringLiteral("libssh2[%1] scp recursive send finished (%2 files)")
                .arg(st->connId).arg(*filesSent));
    return Outcome::success();
}

Outcome Libssh2Scp::recvFrom(const QString& remotePath, const QString& localPath,
                             bool recursive, ProgressFn progress)
{
    auto st = m_state;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    if (!st->session || !st->connected)
        return Outcome::fail(QStringLiteral("Not connected."));
    if (recursive) {
        quint64 filesRecv = 0;
        return recvDirLocked(remotePath, localPath, progress, &filesRecv);
    }
    return recvFileScpLocked(remotePath, localPath, progress);
}

Outcome Libssh2Scp::recvFileScpLocked(const QString& remotePath, const QString& localPath,
                                      const ProgressFn& progress)
{
    auto st = m_state;
    const QByteArray rp = remotePath.toUtf8();
    libssh2_struct_stat sb;
    std::memset(&sb, 0, sizeof(sb));

    LIBSSH2_CHANNEL* chan = retryEagainPtr([&] {
        return libssh2_scp_recv2(st->session, rp.constData(), &sb);
    }, nowMs() + st->opTimeoutMs, st->session);
    if (!chan) {
        const int err = libssh2_session_last_errno(st->session);
        if (err == LIBSSH2_ERROR_EAGAIN)
            return Outcome::fail(QStringLiteral("The download of '%1' timed out.").arg(remotePath));
        return Outcome::fail(QStringLiteral("The server refused the download of '%1'.").arg(remotePath),
                             libssh2ErrorText(st->session, err),
                             QStringLiteral("Check that the file exists and is readable."));
    }

    if (LIBSSH2_SFTP_S_ISDIR(sb.st_mode)) {
        retryEagain([&] { return libssh2_channel_close(chan); }, nowMs() + kShortTimeoutMs);
        retryEagain([&] { return libssh2_channel_free(chan); }, nowMs() + kShortTimeoutMs);
        return Outcome::fail(QStringLiteral("'%1' is a directory; use recursive download.").arg(remotePath),
                             {}, {});
    }

    const quint64 size = quint64(sb.st_size);
    QSaveFile out(localPath);
    if (!out.open(QIODevice::WriteOnly)) {
        retryEagain([&] { return libssh2_channel_close(chan); }, nowMs() + kShortTimeoutMs);
        retryEagain([&] { return libssh2_channel_free(chan); }, nowMs() + kShortTimeoutMs);
        return Outcome::fail(QStringLiteral("Cannot write local file '%1'.").arg(localPath),
                             out.errorString(), {});
    }

    Outcome result = Outcome::success();
    quint64 received = 0;
    char buf[kScpChunkSize];
    qint64 deadline = nowMs() + st->opTimeoutMs;

    while (received < size) {
        const ssize_t n = libssh2_channel_read_ex(chan, 0, buf, kScpChunkSize);
        if (n == LIBSSH2_ERROR_EAGAIN) {
            if (nowMs() >= deadline) {
                result = Outcome::fail(QStringLiteral("The download of '%1' timed out.").arg(remotePath));
                break;
            }
            sleepRetry();
            continue;
        }
        if (n < 0) {
            result = Outcome::fail(QStringLiteral("The download of '%1' failed.").arg(remotePath),
                                   libssh2ErrorText(st->session, int(n)), {});
            break;
        }
        if (n == 0) {
            result = Outcome::fail(QStringLiteral("The connection closed before '%1' was complete.").arg(remotePath));
            break;
        }
        if (out.write(buf, int(n)) != n) {
            result = Outcome::fail(QStringLiteral("Cannot write local file '%1'.").arg(localPath),
                                   out.errorString(), {});
            break;
        }
        received += quint64(n);
        deadline = nowMs() + st->opTimeoutMs; // progress resets the deadline
        if (progress && !progress(received, size)) {
            result = Outcome::fail(QStringLiteral("Download of '%1' cancelled.").arg(remotePath));
            break;
        }
    }

    if (result.ok) {
        if (!out.commit())
            result = Outcome::fail(QStringLiteral("Cannot save local file '%1'.").arg(localPath),
                                   out.errorString(), {});
    } else {
        out.cancelWriting();
    }

    retryEagain([&] { return libssh2_channel_close(chan); }, nowMs() + kShortTimeoutMs);
    retryEagain([&] { return libssh2_channel_free(chan); }, nowMs() + kShortTimeoutMs);
    return result;
}

// Recursive download. SIMPLIFICATION (documented): the classic SCP protocol's
// directory listing ("D" records) is fragile and not standardized; for
// recursive downloads we walk the remote tree over SFTP and fetch each file's
// payload through the SFTP read channel instead of scp_recv2.
Outcome Libssh2Scp::recvDirLocked(const QString& remotePath, const QString& localPath,
                                  const ProgressFn& progress, quint64* filesRecv)
{
    auto st = m_state;
    LIBSSH2_SFTP* sftp = ensureSftpLocked(nullptr);
    if (!sftp)
        return Outcome::fail(QStringLiteral("Could not start SFTP for the recursive download."));

    SftpAttrs attrs;
    if (!sftpStatRaw(st->session, sftp, st->opTimeoutMs, remotePath, false, &attrs))
        return Outcome::fail(QStringLiteral("Remote path '%1' does not exist.").arg(remotePath));

    if (!attrs.isDir) {
        // Single file reached through the recursion: fetch via SFTP.
        Outcome r = sftpFetchFileRaw(m_state, sftp, remotePath, localPath,
                                     progress ? progress : ProgressFn());
        if (r.ok)
            *filesRecv += 1;
        return r;
    }

    // Plan the whole remote tree first.
    struct Item { QString remote; QString local; quint64 size; };
    QVector<Item> items;
    quint64 totalBytes = 0;
    QString perr;

    std::function<bool(const QString&, const QString&)> flatten =
        [&](const QString& rdir, const QString& ldir) -> bool {
        if (!QDir().mkpath(ldir)) {
            perr = QStringLiteral("Cannot create local directory '%1'.").arg(ldir);
            return false;
        }
        QVector<SftpEntry> entries;
        if (!sftpListRaw(st->session, sftp, st->opTimeoutMs, rdir, &entries, &perr))
            return false;
        for (const SftpEntry& e : entries) {
            const QString remoteChild = rdir + QLatin1Char('/') + e.name;
            const QString localChild = ldir + QLatin1Char('/') + e.name;
            if (e.attrs.isDir) {
                if (!flatten(remoteChild, localChild))
                    return false;
            } else {
                Item item;
                item.remote = remoteChild;
                item.local = localChild;
                item.size = e.attrs.size;
                totalBytes += item.size;
                items.append(item);
            }
        }
        return true;
    };

    if (!flatten(remotePath, localPath))
        return Outcome::fail(perr.isEmpty() ? QStringLiteral("Download planning failed.") : perr);

    quint64 doneBytes = 0;
    for (const Item& item : items) {
        Outcome r = sftpFetchFileRaw(m_state, sftp, item.remote, item.local, ProgressFn());
        if (!r.ok)
            return r;
        doneBytes += item.size;
        *filesRecv += 1;
        if (progress && !progress(doneBytes, totalBytes))
            return Outcome::fail(QStringLiteral("Download cancelled."));
    }
    LOG_SSH(QStringLiteral("libssh2[%1] scp recursive recv finished (%2 files)")
                .arg(st->connId).arg(*filesRecv));
    return Outcome::success();
}

// ===========================================================================
// Libssh2Engine
// ===========================================================================

Libssh2Engine::Libssh2Engine()
    : m_state(std::make_shared<Libssh2SessionState>())
{
    static std::once_flag libssh2Init;
    std::call_once(libssh2Init, [] { libssh2_init(0); });
    m_state->connId = g_nextConnId.fetch_add(1);
}

Libssh2Engine::~Libssh2Engine()
{
    m_state->closeNow();
}

QString Libssh2Engine::backendName() const
{
    return QStringLiteral("libssh2");
}

QString Libssh2Engine::backendVersion() const
{
    const char* v = libssh2_version(0);
    return v ? QString::fromLatin1(v) : QString();
}

Outcome Libssh2Engine::connect(const QString& host, int port, int timeoutMs,
                               const std::function<void(const HostKeyInfo&)>& hostKeyCb)
{
    auto st = m_state;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    if (st->session)
        return Outcome::fail(QStringLiteral("Already connected."),
                             QStringLiteral("connect() called twice on the same engine"), {});

    m_hostForLog = host;
    QString err;
    const libssh2_socket_t fd = tcpConnect(host, port, timeoutMs > 0 ? timeoutMs : kDefaultOpTimeoutMs, &err);
    if (fd == LIBSSH2_INVALID_SOCKET) {
        return Outcome::fail(QStringLiteral("Could not reach %1:%2.").arg(host).arg(port),
                             err, QStringLiteral("Check the host name, port and network."));
    }
    st->fd = fd;
    st->ownsFd = true;
    return finishConnect(host, port, hostKeyCb, timeoutMs);
}

Outcome Libssh2Engine::connectOverFd(int fd, const QString& hostForLog, int port,
                                     const std::function<void(const HostKeyInfo&)>& hostKeyCb)
{
    auto st = m_state;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    if (st->session)
        return Outcome::fail(QStringLiteral("Already connected."),
                             QStringLiteral("connectOverFd() called twice on the same engine"), {});
    if (fd < 0)
        return Outcome::fail(QStringLiteral("Invalid socket provided for the connection."));

    m_hostForLog = hostForLog;
    // The session runs non-blocking, so the socket must be non-blocking too.
    QString sockErr;
    if (!setNonBlocking(libssh2_socket_t(fd), &sockErr)) {
        return Outcome::fail(QStringLiteral("Could not configure the provided socket."),
                             sockErr, {});
    }
    st->fd = libssh2_socket_t(fd);
    st->ownsFd = false; // caller keeps ownership
    return finishConnect(hostForLog, port, hostKeyCb, kDefaultOpTimeoutMs);
}

Outcome Libssh2Engine::finishConnect(const QString& hostForLog, int port,
                                     const std::function<void(const HostKeyInfo&)>& hostKeyCb,
                                     int timeoutMs)
{
    auto st = m_state;
    // assumes st->mutex held and st->fd set
    const int effTimeout = timeoutMs > 0 ? timeoutMs : kDefaultOpTimeoutMs;

    LIBSSH2_SESSION* session = libssh2_session_init_ex(nullptr, nullptr, nullptr, nullptr);
    if (!session)
        return Outcome::fail(QStringLiteral("Could not initialize the SSH engine (out of memory)."));

    libssh2_session_flag(session, LIBSSH2_FLAG_SIGPIPE, 0);
    libssh2_session_set_blocking(session, 0); // non-blocking for the whole lifetime
    libssh2_session_set_timeout(session, long(effTimeout));
    libssh2_session_set_read_timeout(session, long(effTimeout));

    // KEX preference: libssh2_session_method_pref (libssh2.h:690) must run
    // BEFORE the handshake. Best effort - a rejected list is logged, the
    // connection proceeds with the engine defaults.
    if (!m_kexPreference.isEmpty()) {
        const QByteArray kexPref = m_kexPreference.toUtf8();
        if (libssh2_session_method_pref(session, LIBSSH2_METHOD_KEX, kexPref.constData()) != 0)
            LOG_SSH_ERR(QStringLiteral("libssh2[%1] KEX preference rejected: %2")
                            .arg(st->connId).arg(m_kexPreference));
        else
            LOG_SSH(QStringLiteral("libssh2[%1] KEX preference set: %2")
                        .arg(st->connId).arg(m_kexPreference));
    }

    // X11: libssh2 delivers inbound X11 channels through LIBSSH2_CALLBACK_X11
    // (libssh2.h:399). Register the trampoline and map session -> engine.
    libssh2_session_callback_set(session, LIBSSH2_CALLBACK_X11,
                                 reinterpret_cast<void*>(&libssh2X11OpenTrampoline));
    x11RegistryAdd(session, this);

    const qint64 deadline = nowMs() + effTimeout;
    const int rc = retryEagain([&] {
        return libssh2_session_handshake(session, st->fd);
    }, deadline);
    if (rc != 0) {
        const Outcome fail = Outcome::fail(
            (rc == LIBSSH2_ERROR_EAGAIN)
                ? QStringLiteral("The SSH handshake with %1:%2 timed out.").arg(hostForLog).arg(port)
                : QStringLiteral("The SSH handshake with %1:%2 failed.").arg(hostForLog).arg(port),
            libssh2ErrorText(session, rc),
            QStringLiteral("Verify that the server is an SSH server and that the port is correct."));
        x11RegistryRemove(session);
        libssh2_session_free(session);
        if (st->ownsFd && st->fd != LIBSSH2_INVALID_SOCKET) {
            closeSocket(st->fd);
            st->fd = LIBSSH2_INVALID_SOCKET;
            st->ownsFd = false;
        }
        LOG_SSH_ERR(QStringLiteral("libssh2[%1] handshake failed with %2:%3")
                        .arg(st->connId).arg(hostForLog).arg(port));
        return fail;
    }

    // Server host key (reported to the caller; verification is the caller's job).
    size_t keyLen = 0;
    int keyType = 0;
    const char* keyBlob = libssh2_session_hostkey(session, &keyLen, &keyType);
    HostKeyInfo info;
    info.host = hostForLog;
    info.port = port;
    if (keyBlob && keyLen > 0) {
        info.publicKeyBlob = QByteArray(keyBlob, int(keyLen));
        info.keyType = hostKeyTypeString(keyType);
        info.sha256Fingerprint = HostKeyManager::sha256Fingerprint(info.publicKeyBlob);
        info.md5Fingerprint = HostKeyManager::md5Fingerprint(info.publicKeyBlob);
    }
    const char* banner = libssh2_session_banner_get(session);
    if (banner)
        st->banner = QString::fromLatin1(banner);

    st->session = session;
    st->connected = true;
    st->hostKey = info;
    st->opTimeoutMs = qBound(1000, effTimeout, 120000);
    libssh2_keepalive_config(session, 1, 15); // enable transport-level keepalives
    st->keepaliveConfigured = true;

    LOG_SSH(QStringLiteral("libssh2[%1] handshake complete with %2:%3 (host key %4, %5)")
                .arg(st->connId).arg(hostForLog).arg(port)
                .arg(info.keyType, info.sha256Fingerprint));

    if (hostKeyCb)
        hostKeyCb(info); // AFTER handshake, BEFORE authentication
    return Outcome::success();
}

HostKeyInfo Libssh2Engine::serverHostKey() const
{
    auto st = m_state;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    return st->hostKey;
}

QString Libssh2Engine::serverBanner() const
{
    auto st = m_state;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    return st->banner;
}

// ---------------------------------------------------------------------------
// Authentication
// ---------------------------------------------------------------------------

Outcome Libssh2Engine::authPasswordLocked(const QByteArray& user, const QString& password)
{
    auto st = m_state;
    const QByteArray pw = password.toUtf8();
    const int rc = retryEagain([&] {
        return libssh2_userauth_password_ex(st->session, user.constData(),
                                            unsigned(user.size()), pw.constData(),
                                            unsigned(pw.size()), nullptr);
    }, nowMs() + st->opTimeoutMs);
    if (rc == 0)
        return Outcome::success();
    return Outcome::fail(
        (rc == LIBSSH2_ERROR_AUTHENTICATION_FAILED)
            ? QStringLiteral("The server rejected the username or password.")
            : friendlyTransportError(rc),
        libssh2ErrorText(st->session, rc),
        QStringLiteral("Check the credentials in the connection profile."));
}

Outcome Libssh2Engine::authPublicKeyLocked(const AuthParams& params)
{
    auto st = m_state;
    const QByteArray user = params.username.toUtf8();

    QStringList keyPaths;
    if (!params.privateKeyPath.isEmpty()) {
        keyPaths.append(params.privateKeyPath);
    } else {
        // No explicit key: try the default identities (spec order).
        const QString dir = utils::defaultKeyDirectory();
        const QStringList defaults = { QStringLiteral("id_ed25519"),
                                       QStringLiteral("id_rsa"),
                                       QStringLiteral("id_ecdsa") };
        for (const QString& name : defaults) {
            const QString candidate = dir + QLatin1Char('/') + name;
            if (QFileInfo::exists(candidate))
                keyPaths.append(candidate);
        }
    }
    if (keyPaths.isEmpty())
        return Outcome::fail(QStringLiteral("No private key is available for authentication."),
                             QStringLiteral("no key file configured or found"),
                             QStringLiteral("Select a key file or enable agent authentication."));

    for (const QString& path : keyPaths) {
        const QByteArray keyB = QFile::encodeName(path);
        const QByteArray pass = params.passphrase.toUtf8();
        const int rc = retryEagain([&] {
            return libssh2_userauth_publickey_fromfile_ex(
                st->session, user.constData(), unsigned(user.size()),
                nullptr, keyB.constData(), pass.constData());
        }, nowMs() + st->opTimeoutMs);
        if (rc == 0) {
            LOG_AUTH(QStringLiteral("libssh2[%1] public key auth succeeded")
                         .arg(st->connId)); // never log the key path contents
            return Outcome::success();
        }
        LOG_DEBUG(QStringLiteral("libssh2[%1] key '%2' rejected (rc=%3)")
                      .arg(st->connId).arg(path).arg(rc));
    }
    return Outcome::fail(QStringLiteral("The server rejected the key file authentication."),
                         libssh2ErrorText(st->session,
                                          libssh2_session_last_errno(st->session)),
                         QStringLiteral("The key may need a passphrase or may not be authorized."));
}

Outcome Libssh2Engine::authKeyboardInteractiveLocked(
    const QByteArray& user,
    const std::function<QStringList(const QStringList&, const QVector<bool>&)>& promptCb)
{
    auto st = m_state;
    if (!promptCb)
        return Outcome::fail(QStringLiteral("Keyboard-interactive authentication requires user input."),
                             QStringLiteral("no prompt callback installed"), {});
    qint64 deadline = nowMs() + kInteractiveTimeoutMs;
    KbdIntCtx ctx;
    ctx.session = st->session;
    ctx.promptCb = &promptCb;
    ctx.deadline = &deadline;
    void* abstract = &ctx;
    const int rc = retryEagain([&] {
        return libssh2_userauth_keyboard_interactive_ex(
            st->session, user.constData(), unsigned(user.size()), &kbdintResponseCb);
    }, deadline);
    if (rc == 0)
        return Outcome::success();
    return Outcome::fail(
        (rc == LIBSSH2_ERROR_AUTHENTICATION_FAILED)
            ? QStringLiteral("The server rejected the interactive responses.")
            : friendlyTransportError(rc),
        libssh2ErrorText(st->session, rc), {});
}

Outcome Libssh2Engine::authAgentLocked(const QByteArray& user)
{
    auto st = m_state;
    AgentClient agent;
    QString aerr;
    if (!agent.connectAgent(&aerr))
        return Outcome::fail(QStringLiteral("The SSH agent is not reachable."), aerr,
                             QStringLiteral("Start ssh-agent / the OpenSSH Authentication Agent service."));

    QVector<AgentIdentity> identities;
    if (!agent.listIdentities(&identities, &aerr) || identities.isEmpty())
        return Outcome::fail(QStringLiteral("The SSH agent holds no usable identities."), aerr, {});

    QString lastTechnical;
    for (const AgentIdentity& id : identities) {
        AgentSignCtx ctx;
        ctx.session = st->session;
        ctx.agent = &agent;
        ctx.blob = id.publicKeyBlob;
        void* abstract = &ctx;
        const int rc = retryEagain([&] {
            return libssh2_userauth_publickey(
                st->session, user.constData(),
                reinterpret_cast<const unsigned char*>(id.publicKeyBlob.constData()),
                size_t(id.publicKeyBlob.size()), &agentSignCb, &abstract);
        }, nowMs() + st->opTimeoutMs);
        if (rc == 0) {
            LOG_AUTH(QStringLiteral("libssh2[%1] agent auth succeeded (key %2)")
                         .arg(st->connId).arg(id.fingerprint));
            return Outcome::success();
        }
        lastTechnical = libssh2ErrorText(st->session, rc);
        LOG_DEBUG(QStringLiteral("libssh2[%1] agent key %2 rejected (rc=%3)")
                      .arg(st->connId).arg(id.fingerprint).arg(rc));
    }
    return Outcome::fail(QStringLiteral("The server rejected every key offered by the SSH agent."),
                         lastTechnical, {});
}

Outcome Libssh2Engine::authenticateWithAgent(const QString& username)
{
    auto st = m_state;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    if (!st->session)
        return Outcome::fail(QStringLiteral("Not connected."));
    return authAgentLocked(username.toUtf8());
}

Outcome Libssh2Engine::authenticate(
    const AuthParams& params,
    const std::function<QStringList(const QStringList& prompts, const QVector<bool>& echoes)>& promptCb)
{
    auto st = m_state;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    if (!st->session || !st->connected)
        return Outcome::fail(QStringLiteral("Not connected."));

    const QByteArray user = params.username.toUtf8();
    Outcome result;

    switch (params.method) {
    case SshAuthMethod::Agent:
        result = authAgentLocked(user);
        if (!result.ok && params.allowAgentFallback && !params.password.isEmpty())
            result = authPasswordLocked(user, params.password);
        break;
    case SshAuthMethod::Password:
        result = authPasswordLocked(user, params.password);
        if (!result.ok && params.allowInteractive && promptCb)
            result = authKeyboardInteractiveLocked(user, promptCb);
        break;
    case SshAuthMethod::PublicKey:
        result = authPublicKeyLocked(params);
        if (!result.ok && params.allowAgentFallback)
            result = authAgentLocked(user);
        break;
    case SshAuthMethod::KeyboardInteractive:
        result = authKeyboardInteractiveLocked(user, promptCb);
        break;
    }

    // Only the outcome is logged - never the credentials themselves.
    if (result.ok)
        LOG_AUTH(QStringLiteral("libssh2[%1] authenticated as '%2' via %3")
                     .arg(st->connId).arg(params.username).arg(authMethodToString(params.method)));
    else
        LOG_AUTH_ERR(QStringLiteral("libssh2[%1] authentication failed (%2)")
                         .arg(st->connId).arg(authMethodToString(params.method)));
    return result;
}

// ---------------------------------------------------------------------------
// Channels / sessions
// ---------------------------------------------------------------------------

bool Libssh2Engine::isConnected() const
{
    auto st = m_state;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    return st->session != nullptr && st->connected;
}

void Libssh2Engine::disconnect()
{
    auto st = m_state;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    if (st->session)
        LOG_SSH(QStringLiteral("libssh2[%1] disconnecting").arg(st->connId));
    st->closeNow();
}

std::unique_ptr<IChannel> Libssh2Engine::openChannel(QString* err)
{
    // ISshEngine contract: openChannel returns an UNSTARTED session channel;
    // the caller selects the channel type via IChannel::openShell() or
    // openExecChannel() (see SshWorker::runTerminal/runExec). Auto-starting a
    // shell here made the follow-up openExecChannel fail with
    // LIBSSH2_ERROR (-39 "channel can not be reused").
    auto st = m_state;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    if (!st->session || !st->connected) {
        if (err)
            *err = QStringLiteral("Not connected.");
        return nullptr;
    }
    LIBSSH2_CHANNEL* chan = retryEagainPtr([&] {
        return libssh2_channel_open_session(st->session);
    }, nowMs() + st->opTimeoutMs, st->session);
    if (!chan) {
        if (err)
            *err = libssh2ErrorText(st->session, libssh2_session_last_errno(st->session));
        return nullptr;
    }
    return std::make_unique<Libssh2Channel>(m_state, chan, ChannelKind::Shell);
}

std::unique_ptr<IChannel> Libssh2Engine::openShellChannel(int cols, int rows,
                                                          const QMap<QString, QString>& env,
                                                          QString* err)
{
    auto st = m_state;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    if (!st->session || !st->connected) {
        if (err)
            *err = QStringLiteral("Not connected.");
        return nullptr;
    }
    LIBSSH2_CHANNEL* chan = retryEagainPtr([&] {
        return libssh2_channel_open_session(st->session);
    }, nowMs() + st->opTimeoutMs, st->session);
    if (!chan) {
        if (err)
            *err = libssh2ErrorText(st->session, libssh2_session_last_errno(st->session));
        return nullptr;
    }
    auto channel = std::make_unique<Libssh2Channel>(m_state, chan, ChannelKind::Shell);
    if (!channel->startShell(cols, rows, env, err)) {
        channel->close();
        return nullptr;
    }
    return channel;
}

std::unique_ptr<IChannel> Libssh2Engine::openExecChannel(const QString& command, QString* err)
{
    auto st = m_state;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    if (!st->session || !st->connected) {
        if (err)
            *err = QStringLiteral("Not connected.");
        return nullptr;
    }
    LIBSSH2_CHANNEL* chan = retryEagainPtr([&] {
        return libssh2_channel_open_session(st->session);
    }, nowMs() + st->opTimeoutMs, st->session);
    if (!chan) {
        if (err)
            *err = libssh2ErrorText(st->session, libssh2_session_last_errno(st->session));
        return nullptr;
    }
    auto channel = std::make_unique<Libssh2Channel>(m_state, chan, ChannelKind::Exec);
    if (!channel->startExec(command, err)) {
        channel->close();
        return nullptr;
    }
    return channel;
}

std::unique_ptr<IChannel> Libssh2Engine::openDirectTcpipChannel(const QString& host, int port,
                                                                QString* err)
{
    auto st = m_state;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    if (!st->session || !st->connected) {
        if (err)
            *err = QStringLiteral("Not connected.");
        return nullptr;
    }
    const QByteArray hb = host.toUtf8();
    LIBSSH2_CHANNEL* chan = retryEagainPtr([&] {
        return libssh2_channel_direct_tcpip_ex(st->session, hb.constData(), port,
                                               "127.0.0.1", 22);
    }, nowMs() + st->opTimeoutMs, st->session);
    if (!chan) {
        if (err)
            *err = libssh2ErrorText(st->session, libssh2_session_last_errno(st->session));
        return nullptr;
    }
    LOG_SSH(QStringLiteral("libssh2[%1] direct-tcpip to %2:%3").arg(st->connId).arg(host).arg(port));
    return std::make_unique<Libssh2Channel>(m_state, chan, ChannelKind::DirectTcpip);
}

std::unique_ptr<ISftpSession> Libssh2Engine::openSftp(QString* err)
{
    auto st = m_state;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    if (!st->session || !st->connected) {
        if (err)
            *err = QStringLiteral("Not connected.");
        return nullptr;
    }
    LIBSSH2_SFTP* sftp = retryEagainPtr([&] {
        return libssh2_sftp_init(st->session);
    }, nowMs() + st->opTimeoutMs, st->session);
    if (!sftp) {
        if (err)
            *err = libssh2ErrorText(st->session, libssh2_session_last_errno(st->session));
        return nullptr;
    }
    LOG_SSH(QStringLiteral("libssh2[%1] sftp session opened").arg(st->connId));
    return std::make_unique<Libssh2Sftp>(m_state, sftp);
}

std::unique_ptr<IScpSession> Libssh2Engine::openScp(QString* err)
{
    auto st = m_state;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    if (!st->session || !st->connected) {
        if (err)
            *err = QStringLiteral("Not connected.");
        return nullptr;
    }
    return std::make_unique<Libssh2Scp>(m_state);
}

// ---------------------------------------------------------------------------
// Remote (reverse) forwarding
// ---------------------------------------------------------------------------

bool Libssh2Engine::remoteForwardListen(const QString& bindAddress, int port,
                                        int* boundPort, QString* err)
{
    auto st = m_state;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    if (!st->session || !st->connected) {
        if (err)
            *err = QStringLiteral("Not connected.");
        return false;
    }
    if (st->listener) {
        // Only one listener per engine in this interface; replace politely.
        libssh2_channel_forward_cancel(st->listener);
        st->listener = nullptr;
    }
    const QByteArray ba = bindAddress.toUtf8();
    int bound = port;
    LIBSSH2_LISTENER* listener = retryEagainPtr([&] {
        return libssh2_channel_forward_listen_ex(st->session,
                                                 ba.isEmpty() ? nullptr : ba.constData(),
                                                 port, &bound, 8);
    }, nowMs() + st->opTimeoutMs, st->session);
    if (!listener) {
        if (err)
            *err = libssh2ErrorText(st->session, libssh2_session_last_errno(st->session));
        return false;
    }
    st->listener = listener;
    if (boundPort)
        *boundPort = bound;
    LOG_SSH(QStringLiteral("libssh2[%1] remote forward listening on port %2")
                .arg(st->connId).arg(bound));
    return true;
}

std::unique_ptr<IChannel> Libssh2Engine::acceptRemoteForward(int timeoutMs, QString* err)
{
    auto st = m_state;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    if (!st->listener) {
        if (err)
            *err = QStringLiteral("No remote forwarding listener is active.");
        return nullptr;
    }
    const qint64 deadline = nowMs() + (timeoutMs > 0 ? timeoutMs : kDefaultOpTimeoutMs);
    LIBSSH2_CHANNEL* chan = retryEagainPtr([&] {
        return libssh2_channel_forward_accept(st->listener);
    }, deadline, st->session);
    if (!chan) {
        if (err)
            *err = (libssh2_session_last_errno(st->session) == LIBSSH2_ERROR_EAGAIN)
                       ? QStringLiteral("No incoming forwarded connection within the timeout.")
                       : libssh2ErrorText(st->session, libssh2_session_last_errno(st->session));
        return nullptr;
    }
    LOG_SSH(QStringLiteral("libssh2[%1] accepted forwarded connection").arg(st->connId));
    return std::make_unique<Libssh2Channel>(m_state, chan, ChannelKind::RemoteForwardAccepted);
}

void Libssh2Engine::remoteForwardCancel(const QString& bindAddress, int port)
{
    Q_UNUSED(bindAddress);
    Q_UNUSED(port); // the engine tracks its single listener internally
    auto st = m_state;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    if (!st->listener)
        return;
    retryEagain([&] { return libssh2_channel_forward_cancel(st->listener); },
                nowMs() + kShortTimeoutMs);
    st->listener = nullptr;
    LOG_SSH(QStringLiteral("libssh2[%1] remote forward cancelled").arg(st->connId));
}

// ---------------------------------------------------------------------------
// Keepalive / info
// ---------------------------------------------------------------------------

int Libssh2Engine::sendKeepAlive()
{
    auto st = m_state;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    if (!st->session || !st->connected)
        return -1;
    int secondsToNext = 0;
    const int rc = libssh2_keepalive_send(st->session, &secondsToNext);
    if (rc != 0)
        LOG_DEBUG(QStringLiteral("libssh2[%1] keepalive send failed (rc=%2)")
                      .arg(st->connId).arg(rc));
    // RTT is measured by the worker (exec latency probe), never here.
    return -1;
}



std::recursive_mutex& Libssh2Engine::rawMutex()
{
    return m_state->mutex;
}

std::unique_ptr<IChannel> Libssh2Engine::openForwardChannel(const QString& destHost, int destPort, QString* err)
{
    return openDirectTcpipChannel(destHost, destPort, err);
}

EngineInfo Libssh2Engine::negotiatedInfo() const
{
    auto st = m_state;
    std::lock_guard<std::recursive_mutex> lock(st->mutex);
    EngineInfo info;
    info.backend = QStringLiteral("libssh2");
    info.version = backendVersion();
    if (!st->session)
        return info;
    const auto method = [&](int type) {
        const char* m = libssh2_session_methods(st->session, type);
        return m ? QString::fromLatin1(m) : QString();
    };
    info.kex = method(LIBSSH2_METHOD_KEX);
    info.cipherIn = method(LIBSSH2_METHOD_CRYPT_CS);
    info.cipherOut = method(LIBSSH2_METHOD_CRYPT_SC);
    info.macIn = method(LIBSSH2_METHOD_MAC_CS);
    info.macOut = method(LIBSSH2_METHOD_MAC_SC);
    info.hostKeyAlgo = method(LIBSSH2_METHOD_HOSTKEY);
    info.compression = method(LIBSSH2_METHOD_COMP_CS) + QLatin1Char('/')
                       + method(LIBSSH2_METHOD_COMP_SC);
    return info;
}

void Libssh2Engine::setPreferredAuthOrder(bool agentFirst)
{
    m_agentFirst = agentFirst; // recorded; explicit authenticate() drives the order
}

// ---------------------------------------------------------------------------
// KEX preference + X11 forwarding
// ---------------------------------------------------------------------------
void Libssh2Engine::setKexAlgorithms(const QString& list)
{
    const QString trimmed = list.trimmed();
    if (trimmed.isEmpty())
        return;
    m_kexPreference = trimmed; // applied in finishConnect() before the handshake
}

void Libssh2Engine::enqueueX11Channel(LIBSSH2_CHANNEL* channel)
{
    if (!channel)
        return;
    QMutexLocker lock(&m_x11Mutex);
    m_x11Pending.append(channel);
}

std::unique_ptr<IChannel> Libssh2Engine::acceptX11(int timeoutMs, QString* err)
{
    Q_UNUSED(timeoutMs);
    Q_UNUSED(err);
    LIBSSH2_CHANNEL* chan = nullptr;
    {
        QMutexLocker lock(&m_x11Mutex);
        if (!m_x11Pending.isEmpty())
            chan = m_x11Pending.takeFirst();
    }
    if (!chan)
        return nullptr;
    LOG_SSH(QStringLiteral("libssh2[%1] accepted inbound X11 channel").arg(m_state->connId));
    return std::make_unique<Libssh2Channel>(m_state, chan, ChannelKind::X11);
}

} // namespace eclipse
