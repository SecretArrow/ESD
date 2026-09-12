#pragma once

#include <QHash>
#include <QObject>
#include <QTcpServer>

#include "../core/profiles/ConnectionProfile.h"
#include "SshWorker.h"

namespace eclipse {

// ---------------------------------------------------------------------------
// ForwardManager - UI-facing port forwarding coordinator.
//
// Rules live in each profile (persisted JSON). At runtime, rules are started
// on the session's worker thread (SshWorker::startForward). This manager
// tracks runtime status per session and exposes it to QML, including a
// SOCKS5 handshake parser used by dynamic (SOCKS5) forwards.
// ---------------------------------------------------------------------------
class ForwardManager : public QObject
{
    Q_OBJECT
public:
    static ForwardManager& instance();

    struct RuntimeStatus
    {
        QString status;  // "active" | "stopped" | "error"
        QString error;
    };

    // SessionManager registers/unregisters live workers.
    void registerWorker(qint64 sessionId, SshWorker* worker);
    void unregisterWorker(qint64 sessionId);
    SshWorker* workerFor(qint64 sessionId) const;

    // Starts every rule with autoStart on the given worker (called after connect).
    void startAutoRules(SshWorker* worker);

    Q_INVOKABLE void startRule(qint64 sessionId, const QString& ruleId);
    Q_INVOKABLE void stopRule(qint64 sessionId, const QString& ruleId);
    Q_INVOKABLE QString ruleStatus(qint64 sessionId, const QString& ruleId) const;

    // SOCKS5 state machine for dynamic forwards.
    // Returns true when the handshake is complete; `reply` contains bytes to
    // send back to the client (empty reply = need more input). On success,
    // host/port contain the requested target.
    static bool parseSocks5Step(QByteArray* buffer, QString* host, int* port, QByteArray* reply);

public slots:
    void onForwardStatus(qint64 sessionId, const QString& ruleId, const QString& status, const QString& error);

signals:
    void statusChanged();

private:
    ForwardManager() = default;
    QHash<qint64, SshWorker*> m_workers;
    QHash<qint64, QHash<QString, RuntimeStatus>> m_status; // sessionId -> ruleId -> status
};

} // namespace eclipse
