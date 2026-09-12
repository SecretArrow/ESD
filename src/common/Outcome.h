#pragma once

#include <QString>

namespace eclipse {

// ---------------------------------------------------------------------------
// Outcome: user-facing error model.
// Every failure has a *friendly* explanation and optional *technical* details
// (shown in an expandable "Details" area, never as the primary message).
// ---------------------------------------------------------------------------

struct Outcome
{
    bool ok = true;
    QString friendly;    // "The remote server closed the connection."
    QString technical;   // "LIBSSH2_ERROR_SOCKET_SEND (-7)"
    QString hint;        // optional next-step hint

    static Outcome success() { return {}; }

    static Outcome fail(const QString& friendly, const QString& technical = {},
                        const QString& hint = {})
    {
        Outcome o;
        o.ok = false;
        o.friendly = friendly;
        o.technical = technical;
        o.hint = hint;
        return o;
    }
};

} // namespace eclipse
