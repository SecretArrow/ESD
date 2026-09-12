#pragma once

#include <QObject>
#include <QVariantList>
#include <QVariantMap>

#include "../core/profiles/ConnectionProfile.h"

namespace eclipse {

// ---------------------------------------------------------------------------
// Diagnostics - step-by-step connection diagnosis:
//
//   DNS -> TCP -> SSH Handshake -> Host key -> Authentication -> SFTP
//
// Each step reports latency and a friendly failure reason. Runs on its own
// thread; safe for UI progress display.
// ---------------------------------------------------------------------------
class Diagnostics : public QObject
{
    Q_OBJECT
public:
    explicit Diagnostics(ConnectionProfile profile, const QString& password, QObject* parent = nullptr);

    Q_INVOKABLE void start();

signals:
    void stepFinished(const QString& step, bool ok, const QString& detail, int ms);
    void finished(bool allOk, int totalMs);

private:
    ConnectionProfile m_profile;
    QString m_password;
};

} // namespace eclipse
