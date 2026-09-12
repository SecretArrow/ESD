#include "ForwardManager.h"

#include <QTimer>

#include "../core/logging/Logger.h"

namespace eclipse {

ForwardManager& ForwardManager::instance()
{
    static ForwardManager m;
    return m;
}

void ForwardManager::registerWorker(qint64 sessionId, SshWorker* worker)
{
    m_workers.insert(sessionId, worker);
}

void ForwardManager::unregisterWorker(qint64 sessionId)
{
    m_workers.remove(sessionId);
}

SshWorker* ForwardManager::workerFor(qint64 sessionId) const
{
    return m_workers.value(sessionId);
}

void ForwardManager::startAutoRules(SshWorker* worker)
{
    if (!worker)
        return;
    for (const ForwardRuleSpec& rule : worker->profile().forwardingRules) {
        if (rule.enabled && rule.autoStart)
            worker->startForward(rule); // queued to the worker thread
    }
}

void ForwardManager::startRule(qint64 sessionId, const QString& ruleId)
{
    SshWorker* w = m_workers.value(sessionId);
    if (!w)
        return;
    for (const ForwardRuleSpec& rule : w->profile().forwardingRules) {
        if (rule.id == ruleId) {
            w->startForward(rule);
            return;
        }
    }
}

void ForwardManager::stopRule(qint64 sessionId, const QString& ruleId)
{
    if (SshWorker* w = m_workers.value(sessionId))
        w->stopForward(ruleId);
}

QString ForwardManager::ruleStatus(qint64 sessionId, const QString& ruleId) const
{
    return m_status.value(sessionId).value(ruleId).status;
}

void ForwardManager::onForwardStatus(qint64 sessionId, const QString& ruleId,
                                     const QString& status, const QString& error)
{
    m_status[sessionId].insert(ruleId, { status, error });
    emit statusChanged();
}

// ---------------------------------------------------------------------------
// SOCKS5 (RFC 1928) server-side handshake, CONNECT only.
// Returns false when more bytes are needed and reply is empty; when the
// handshake fails, reply carries the failure response.
// ---------------------------------------------------------------------------
bool ForwardManager::parseSocks5Step(QByteArray* buffer, QString* host, int* port, QByteArray* reply)
{
    reply->clear();

    // 1. Method negotiation: VER(1) NMETHODS(1) METHODS(1..255)
    if (buffer->size() < 2)
        return false;
    const unsigned char ver = quint8((*buffer)[0]);
    if (ver != 0x05) {
        reply->append(char(0x05));
        reply->append(char(0xFF)); // no acceptable methods
        return true;
    }
    const int nmethods = quint8((*buffer)[1]);
    if (buffer->size() < 2 + nmethods)
        return false;
    const bool hasNoAuth = std::any_of(buffer->constBegin() + 2, buffer->constBegin() + 2 + nmethods,
                                       [](char m) { return quint8(m) == 0x00; });
    buffer->remove(0, 2 + nmethods);

    if (!hasNoAuth) {
        reply->append(char(0x05));
        reply->append(char(0xFF));
        return true;
    }
    reply->append(char(0x05));
    reply->append(char(0x00)); // choose no-auth

    // 2. Request: VER(1) CMD(1) RSV(1) ATYP(1) ADDR BNDPORT(2)
    if (buffer->size() < 5)
        return false;
    const unsigned char cmd = quint8((*buffer)[1]);
    const unsigned char atyp = quint8((*buffer)[3]);
    int addrLen = 0;
    if (atyp == 0x01)
        addrLen = 4;
    else if (atyp == 0x03)
        addrLen = -1; // length-prefixed
    else if (atyp == 0x04)
        addrLen = 16;
    else {
        reply->append(char(0x05));
        reply->append(char(0x08)); // address type not supported
        return true;
    }

    if (addrLen < 0) {
        if (buffer->size() < 5)
            return false;
        addrLen = quint8((*buffer)[4]);
        if (buffer->size() < 4 + 1 + addrLen + 2)
            return false;
    } else if (buffer->size() < 4 + addrLen + 2) {
        return false;
    }

    if (cmd != 0x01) { // only CONNECT
        reply->clear();
        reply->append(char(0x05));
        reply->append(char(0x07)); // command not supported
        return true;
    }

    QByteArray address;
    if (atyp == 0x01) {
        const QHostAddress ip(quint32((quint8((*buffer)[4]) << 24)
                                      | (quint8((*buffer)[5]) << 16)
                                      | (quint8((*buffer)[6]) << 8)
                                      | quint8((*buffer)[7])));
        *host = ip.toString();
    } else if (atyp == 0x03) {
        *host = QString::fromUtf8(buffer->mid(5, addrLen));
    } else {
        // IPv6: format properly
        QHostAddress ip6;
        Q_IPV6ADDR v6;
        memcpy(&v6, buffer->constData() + 5, 16);
        ip6.setAddress(v6);
        *host = ip6.toString();
    }
    const int p = 4 + (atyp == 0x03 ? 1 + addrLen : addrLen);
    *port = (quint8((*buffer)[p]) << 8) | quint8((*buffer)[p + 1]);
    buffer->remove(0, p + 2);
    return true;
}

} // namespace eclipse
