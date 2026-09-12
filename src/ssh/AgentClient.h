#pragma once

#include <QString>
#include <QVector>

#include "Types.h"

class QLocalSocket;

namespace eclipse {

// ---------------------------------------------------------------------------
// AgentClient - raw SSH agent protocol client (RFC draft-miller-ssh-agent).
//
// Speaks directly to:
//   * Linux: $SSH_AUTH_SOCK (unix socket)
//   * Windows: OpenSSH agent named pipe \\.\pipe\openssh-ssh-agent
//
// Used for listing identities (UI) and signing requests (libssh2 backend).
// Private key material NEVER passes through this client - only public blobs
// and opaque signatures.
// ---------------------------------------------------------------------------

struct AgentIdentity
{
    QByteArray publicKeyBlob;
    QString comment;
    QString keyType; // derived from blob
    QString fingerprint;
};

class AgentClient
{
public:
    static bool isAgentAvailable();

    AgentClient() = default;
    ~AgentClient();

    bool connectAgent(QString* err = nullptr);
    void disconnectAgent();

    bool listIdentities(QVector<AgentIdentity>* out, QString* err = nullptr);

    // Requests an agent signature over `data` with the identity `blob`.
    // flags: 0 auto, 1 = RSA/SHA256, 2 = RSA/SHA512
    bool sign(const QByteArray& blob, const QByteArray& data, int flags, QByteArray* signatureOut, QString* err = nullptr);

private:
    bool request(unsigned char type, const QByteArray& payload, QByteArray* reply, QString* err);
    QLocalSocket* m_socket = nullptr;
};

// Wire-format helpers shared with engines ---------------------------------
namespace agentwire {

void putU32(QByteArray& out, quint32 v);
quint32 getU32(const QByteArray& buf, int& pos, bool* ok);
void putString(QByteArray& out, const QByteArray& v);
QByteArray getString(const QByteArray& buf, int& pos, bool* ok);
QString peekKeyType(const QByteArray& blob);

} // namespace agentwire
} // namespace eclipse
