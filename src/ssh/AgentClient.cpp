#include "AgentClient.h"

#include <QLocalSocket>

#ifdef Q_OS_WIN
#include <QStringList>
#endif

#include "../common/Utils.h"
#include "HostKeyManager.h"

namespace eclipse {

namespace agentwire {

void putU32(QByteArray& out, quint32 v)
{
    out.append(char((v >> 24) & 0xFF));
    out.append(char((v >> 16) & 0xFF));
    out.append(char((v >> 8) & 0xFF));
    out.append(char(v & 0xFF));
}

quint32 getU32(const QByteArray& buf, int& pos, bool* ok)
{
    if (!ok || pos + 4 > buf.size()) {
        if (ok)
            *ok = false;
        return 0;
    }
    const quint32 v = (quint32(quint8(buf[pos])) << 24)
                      | (quint32(quint8(buf[pos + 1])) << 16)
                      | (quint32(quint8(buf[pos + 2])) << 8)
                      | quint32(quint8(buf[pos + 3]));
    pos += 4;
    return v;
}

void putString(QByteArray& out, const QByteArray& v)
{
    putU32(out, quint32(v.size()));
    out.append(v);
}

QByteArray getString(const QByteArray& buf, int& pos, bool* ok)
{
    const quint32 len = getU32(buf, pos, ok);
    if (!ok || pos + int(len) > buf.size()) {
        if (ok)
            *ok = false;
        return {};
    }
    const QByteArray out = buf.mid(pos, int(len));
    pos += int(len);
    return out;
}

QString peekKeyType(const QByteArray& blob)
{
    int pos = 0;
    bool ok = true;
    const QByteArray type = getString(blob, pos, &ok);
    return ok ? QString::fromUtf8(type) : QString();
}

} // namespace agentwire

using namespace agentwire;

static QString agentSocketPath()
{
#ifdef Q_OS_WIN
    return QStringLiteral("\\\\.\\pipe\\openssh-ssh-agent");
#else
    const QString sock = qEnvironmentVariable("SSH_AUTH_SOCK");
    return sock;
#endif
}

bool AgentClient::isAgentAvailable()
{
#ifdef Q_OS_WIN
    // The Windows agent pipe exists only while the service runs.
    QLocalSocket probe;
    probe.connectToServer(agentSocketPath());
    const bool ok = probe.waitForConnected(300);
    probe.abort();
    return ok;
#else
    return !agentSocketPath().isEmpty();
#endif
}

AgentClient::~AgentClient()
{
    disconnectAgent();
}

bool AgentClient::connectAgent(QString* err)
{
    if (m_socket && m_socket->state() == QLocalSocket::ConnectedState)
        return true;
    disconnectAgent();
    const QString path = agentSocketPath();
    if (path.isEmpty()) {
        if (err)
            *err = QStringLiteral("SSH agent socket not configured (SSH_AUTH_SOCK empty).");
        return false;
    }
    m_socket = new QLocalSocket();
    m_socket->connectToServer(path);
    if (!m_socket->waitForConnected(2000)) {
        if (err)
            *err = QStringLiteral("Cannot connect to SSH agent at %1: %2")
                       .arg(path, m_socket->errorString());
        delete m_socket;
        m_socket = nullptr;
        return false;
    }
    return true;
}

void AgentClient::disconnectAgent()
{
    if (m_socket) {
        m_socket->disconnectFromServer();
        m_socket->deleteLater();
        m_socket = nullptr;
    }
}

bool AgentClient::request(unsigned char type, const QByteArray& payload, QByteArray* reply, QString* err)
{
    if (!connectAgent(err))
        return false;
    QByteArray packet;
    putU32(packet, 1 + payload.size());
    packet.append(char(type));
    packet.append(payload);

    if (m_socket->write(packet) != packet.size() || !m_socket->flush()) {
        if (err)
            *err = QStringLiteral("Failed writing to agent: %1").arg(m_socket->errorString());
        return false;
    }
    if (!m_socket->waitForReadyRead(5000)) {
        if (err)
            *err = QStringLiteral("Agent did not respond: %1").arg(m_socket->errorString());
        return false;
    }
    QByteArray all = m_socket->readAll();
    // Large replies (many identities) may require additional reads.
    while (all.size() >= 4) {
        const quint32 need = (quint32(quint8(all[0])) << 24)
                             | (quint32(quint8(all[1])) << 16)
                             | (quint32(quint8(all[2])) << 8)
                             | quint32(quint8(all[3]));
        if (all.size() >= int(need + 4))
            break;
        if (!m_socket->waitForReadyRead(5000))
            break;
        all.append(m_socket->readAll());
    }
    if (reply)
        *reply = all.mid(4); // strip length prefix
    return true;
}

bool AgentClient::listIdentities(QVector<AgentIdentity>* out, QString* err)
{
    QByteArray reply;
    if (!request(11, {}, &reply, err)) // SSH_AGENTC_REQUEST_IDENTITIES
        return false;
    if (reply.isEmpty() || quint8(reply[0]) != 12) { // SSH_AGENT_IDENTITIES_ANSWER
        if (err)
            *err = QStringLiteral("Unexpected agent response for identity request.");
        return false;
    }
    int pos = 1;
    bool ok = true;
    const quint32 count = getU32(reply, pos, &ok);
    if (!ok)
        return false;
    for (quint32 i = 0; i < count; ++i) {
        AgentIdentity id;
        id.publicKeyBlob = getString(reply, pos, &ok);
        const QByteArray comment = getString(reply, pos, &ok);
        if (!ok)
            break;
        id.comment = QString::fromUtf8(comment);
        id.keyType = peekKeyType(id.publicKeyBlob);
        id.fingerprint = HostKeyManager::sha256Fingerprint(id.publicKeyBlob);
        out->append(id);
    }
    return true;
}

bool AgentClient::sign(const QByteArray& blob, const QByteArray& data, int flags,
                       QByteArray* signatureOut, QString* err)
{
    QByteArray payload;
    putString(payload, blob);
    putString(payload, data);
    putU32(payload, quint32(flags));

    QByteArray reply;
    if (!request(13, payload, &reply, err)) // SSH_AGENTC_SIGN_REQUEST
        return false;
    if (reply.isEmpty() || quint8(reply[0]) == 5) { // SSH_AGENT_FAILURE
        if (err)
            *err = QStringLiteral("Agent refused to sign (key not authorized?).");
        return false;
    }
    if (quint8(reply[0]) != 14) { // SSH_AGENT_SIGN_RESPONSE
        if (err)
            *err = QStringLiteral("Unexpected agent sign response.");
        return false;
    }
    int pos = 1;
    bool ok = true;
    const QByteArray sig = getString(reply, pos, &ok);
    if (!ok || sig.isEmpty()) {
        if (err)
            *err = QStringLiteral("Malformed signature from agent.");
        return false;
    }
    *signatureOut = sig;
    return true;
}

} // namespace eclipse
