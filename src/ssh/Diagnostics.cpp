#include "Diagnostics.h"

#include <QElapsedTimer>
#include <QHostInfo>
#include <QThread>

#include "../common/Utils.h"
#include "../core/credentials/CredentialManager.h"
#include "../core/logging/Logger.h"
#include "HostKeyManager.h"
#include "ProxyDialer.h"
#include "backends/libssh/LibsshEngine.h"

namespace eclipse {

Diagnostics::Diagnostics(ConnectionProfile profile, const QString& password, QObject* parent)
    : QObject(parent)
    , m_profile(std::move(profile))
    , m_password(password)
{
}

void Diagnostics::start()
{
    QElapsedTimer total;
    total.start();
    bool allOk = true;

    auto report = [this, &allOk](const QString& step, bool ok, const QString& detail) {
        if (!ok)
            allOk = false;
        emit stepFinished(step, ok, detail, 0);
    };

    // --- DNS ---------------------------------------------------------------
    QElapsedTimer t;
    t.start();
    const QHostInfo info = QHostInfo::fromName(m_profile.host);
    const int dnsMs = int(t.elapsed());
    const bool dnsOk = info.error() == QHostInfo::NoError && !info.addresses().isEmpty();
    emit stepFinished(QStringLiteral("DNS"), dnsOk,
                      dnsOk ? info.addresses().first().toString() : info.errorString(), dnsMs);
    if (!dnsOk) {
        emit finished(false, int(total.elapsed()));
        return;
    }

    // --- TCP ---------------------------------------------------------------
    t.restart();
    ProxyConfig proxy;
    proxy.type = m_profile.proxyType;
    proxy.host = m_profile.proxyHost;
    proxy.port = m_profile.proxyPort;
    proxy.user = m_profile.proxyUser;
    proxy.password = CredentialManager::instance().loadSecret(
        QStringLiteral("profile:%1:proxy-password").arg(m_profile.id));
    int fd = -1;
    const Outcome tcp = dial(m_profile.host, m_profile.port, proxy, 8000, &fd);
    emit stepFinished(QStringLiteral("TCP Connection"), tcp.ok,
                      tcp.ok ? QStringLiteral("Connected (%1 ms)").arg(t.elapsed())
                             : tcp.friendly,
                      int(t.elapsed()));
    if (!tcp.ok) {
        emit finished(false, int(total.elapsed()));
        return;
    }

    // --- SSH handshake -----------------------------------------------------
    t.restart();
    std::unique_ptr<ISshEngine> engine = createEngine(SshEngineKind::Libssh);
    const Outcome hs = engine->connectOverFd(fd, m_profile.host, m_profile.port, {});
    emit stepFinished(QStringLiteral("SSH Handshake"), hs.ok,
                      hs.ok ? QStringLiteral("Negotiated with %1 (%2)")
                                  .arg(engine->serverHostKey().keyType,
                                       QString::number(t.elapsed()) + QStringLiteral(" ms"))
                            : hs.friendly,
                      int(t.elapsed()));
    if (!hs.ok) {
        emit finished(false, int(total.elapsed()));
        return;
    }

    // --- Host key ----------------------------------------------------------
    t.restart();
    const HostKeyCheckResult check = HostKeyManager().check(engine->serverHostKey());
    const bool keyOk = check == HostKeyCheckResult::Known;
    emit stepFinished(QStringLiteral("Host Key"), keyOk,
                      check == HostKeyCheckResult::Known
                          ? engine->serverHostKey().sha256Fingerprint
                          : (check == HostKeyCheckResult::Changed
                                 ? QStringLiteral("Key has CHANGED since last visit")
                                 : QStringLiteral("Key is not in known_hosts")),
                      int(t.elapsed()));
    // Diagnostics continue even when the key is unknown (read-only check).

    // --- Authentication ----------------------------------------------------
    t.restart();
    AuthParams params;
    params.username = m_profile.username;
    params.method = SshAuthMethod::Password;
    params.password = m_password.isEmpty()
                          ? CredentialManager::instance().loadSecret(
                                QStringLiteral("profile:%1:password").arg(m_profile.id))
                          : m_password;
    if (params.password.isEmpty() && m_profile.authMethod == QLatin1String("publickey")) {
        params.method = SshAuthMethod::PublicKey;
        params.privateKeyPath = utils::expandTildePath(m_profile.privateKeyPath);
    } else if (params.password.isEmpty() && m_profile.authMethod == QLatin1String("agent")) {
        params.method = SshAuthMethod::Agent;
    }
    const Outcome auth = engine->authenticate(params);
    emit stepFinished(QStringLiteral("Authentication"), auth.ok,
                      auth.ok ? QStringLiteral("Authenticated as %1").arg(m_profile.username)
                              : auth.friendly,
                      int(t.elapsed()));
    if (!auth.ok) {
        emit finished(false, int(total.elapsed()));
        return;
    }

    // --- SFTP subsystem ----------------------------------------------------
    t.restart();
    QString err;
    std::unique_ptr<ISftpSession> sftp = engine->openSftp(&err);
    if (sftp)
        sftp->canonicalize(QStringLiteral("."));
    emit stepFinished(QStringLiteral("SFTP"), sftp != nullptr,
                      sftp ? QStringLiteral("Subsystem available") : err, int(t.elapsed()));

    emit finished(allOk, int(total.elapsed()));
    LOG_APP(QStringLiteral("Diagnostics for %1 finished in %2 ms").arg(m_profile.host).arg(total.elapsed()));
}

} // namespace eclipse
