#include "ISshEngine.h"

// ---------------------------------------------------------------------------
// EngineFactory - single createEngine() definition with real backend dispatch.
//
// History: createEngine() used to live in the libssh backend and silently
// ignored the requested kind, so profiles configured for "libssh2" ran on
// libssh and the libssh2 backend was never instantiated. The factory now
// dispatches based on which backends were linked in (ECLIPSE_HAVE_LIBSSH /
// ECLIPSE_HAVE_LIBSSH2 compile definitions set from CMake).
// ---------------------------------------------------------------------------

#if defined(ECLIPSE_HAVE_LIBSSH)
#include <libssh/libssh.h>
#include "backends/libssh/LibsshEngine.h"
#endif

#if defined(ECLIPSE_HAVE_LIBSSH2)
#include <libssh2.h>
#include "backends/libssh2/Libssh2Engine.h"
#endif

namespace eclipse {

std::unique_ptr<ISshEngine> createEngine(SshEngineKind kind)
{
    switch (kind) {
#if defined(ECLIPSE_HAVE_LIBSSH2)
    case SshEngineKind::Libssh2:
        return std::make_unique<Libssh2Engine>();
#endif
#if defined(ECLIPSE_HAVE_LIBSSH)
    case SshEngineKind::Libssh:
    case SshEngineKind::Auto:
        break;
#endif
    }

    // Default: libssh backend when available, otherwise the requested
    // specific engine if only it was compiled in.
#if defined(ECLIPSE_HAVE_LIBSSH)
    return std::make_unique<LibsshEngine>();
#elif defined(ECLIPSE_HAVE_LIBSSH2)
    return std::make_unique<Libssh2Engine>();
#else
    return nullptr;
#endif
}

QString libsshVersionString()
{
#if defined(ECLIPSE_HAVE_LIBSSH)
    return QString::fromLatin1(ssh_version(0));
#else
    return {};
#endif
}

QString libssh2VersionString()
{
#if defined(ECLIPSE_HAVE_LIBSSH2)
    return QString::fromLatin1(libssh2_version(0));
#else
    return {};
#endif
}

} // namespace eclipse
