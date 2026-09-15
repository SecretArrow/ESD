// Eclipse SSH Desktop - real SSH/SFTP round-trip integration test.
//
// Exercises the FULL connection pipeline against a live local sshd:
//   TCP connect -> key exchange -> host-key callback -> authentication
//   -> exec channel -> SFTP write/read/stat/list/rename/mkdir/unlink
//   -> keepalive -> disconnect, for BOTH backends (libssh + libssh2).
// Plus negative paths: bad key rejected, dead port fails with a friendly
// message (the user-facing "login feedback" guarantee).
//
// Requires a running sshd configured through the environment:
//   ESD_SSHD_PORT  - port of the test sshd on 127.0.0.1
//   ESD_SSHD_USER  - user allowed to log in with pubkey
//   ESD_SSHD_KEY   - path to the client private key (authorized on server)
// When these are unset (e.g. the Windows CI job) every test QSKIPs.

#include <QtTest/QtTest>

#include <QCoreApplication>
#include <QFile>
#include <QHostAddress>
#include <QProcess>
#include <QTemporaryDir>
#include <QThread>
#include <QTcpServer>
#include <QTimer>

#include "../src/ssh/ISshEngine.h"
#include "../src/ssh/Types.h"
#include "../src/common/Outcome.h"

#include <QRandomGenerator>

using namespace eclipse;

namespace {

QString envOr(const char* name, const QString& fallback = {})
{
    return qEnvironmentVariable(name, fallback);
}

bool sshdConfigured()
{
    return !envOr("ESD_SSHD_PORT").isEmpty()
        && !envOr("ESD_SSHD_USER").isEmpty()
        && !envOr("ESD_SSHD_KEY").isEmpty();
}

// Drain a channel's stdout until EOF or deadline; returns accumulated bytes.
QByteArray pumpStdout(IChannel* ch, int timeoutMs)
{
    QByteArray out;
    char buf[4096];
    QElapsedTimer timer;
    timer.start();
    int quietRounds = 0;
    while (timer.elapsed() < timeoutMs) {
        int n = ch->readStdout(buf, sizeof buf);
        if (n > 0) {
            out.append(buf, n);
            quietRounds = 0;
            continue;
        }
        if (n < 0)
            break;                       // channel error
        if (ch->isEof()) {
            // one extra pass to catch bytes already buffered
            QThread::msleep(20);
            n = ch->readStdout(buf, sizeof buf);
            if (n > 0) { out.append(buf, n); continue; }
            break;
        }
        if (++quietRounds > 400)
            break;
        QThread::msleep(20);
    }
    return out;
}

int waitExitStatus(IChannel* ch, int timeoutMs)
{
    QElapsedTimer timer;
    timer.start();
    while (timer.elapsed() < timeoutMs) {
        int st = ch->exitStatus();
        if (st >= 0)
            return st;
        QThread::msleep(30);
    }
    return -1;
}

AuthParams keyAuth()
{
    AuthParams p;
    p.username = envOr("ESD_SSHD_USER");
    p.method = SshAuthMethod::PublicKey;
    p.privateKeyPath = envOr("ESD_SSHD_KEY");
    p.passphrase.clear();
    p.tryDefaultIdentities = false;
    p.allowAgentFallback = false;
    return p;
}

QByteArray randomPayload(int size)
{
    QByteArray payload;
    payload.resize(size);
    quint32 seed = QRandomGenerator::global()->generate();
    QRandomGenerator rng(seed);
    for (int i = 0; i < size; ++i)
        payload[i] = char(rng.bounded(256));
    // deterministic tail marker
    payload.replace(size - 16, 16, "ECLIPSE-RT-END\n");
    return payload;
}

// A free TCP port (bind, read, close) for the dead-port negative test.
int freeTcpPort()
{
    QTcpServer srv;
    if (!srv.listen(QHostAddress::LocalHost))
        return -1;
    const int port = srv.serverPort();
    srv.close();
    return port;
}

} // namespace

class SshRoundTripTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        if (!sshdConfigured())
            QSKIP("ESD_SSHD_PORT/USER/KEY not set - no local sshd testbed on this runner");
    }

    void roundTrip_libssh()        { roundTrip(SshEngineKind::Libssh); }
    void roundTrip_libssh2()       { roundTrip(SshEngineKind::Libssh2); }

    void wrongKeyRejected()
    {
        // Freshly generated key pair NOT authorized on the server.
        QTemporaryDir tmp;
        QString badKey = tmp.path() + "/bad_key";
        QProcess gen;
        gen.start(QStringLiteral("ssh-keygen"),
                  {QStringLiteral("-q"), QStringLiteral("-t"), QStringLiteral("ed25519"),
                   QStringLiteral("-N"), QString(), QStringLiteral("-f"), badKey});
        if (!gen.waitForFinished(15000) || gen.exitStatus() != QProcess::NormalExit
            || gen.exitCode() != 0)
            QSKIP("ssh-keygen unavailable - skipping server-side rejection test");

        auto engine = createEngine(SshEngineKind::Libssh);
        QVERIFY(engine != nullptr);
        auto hk = engine->connect(QStringLiteral("127.0.0.1"),
                                  envOr("ESD_SSHD_PORT").toInt(), 10000);
        QVERIFY2(hk.ok, qPrintable(hk.friendly));

        AuthParams p = keyAuth();
        p.privateKeyPath = badKey;
        Outcome auth = engine->authenticate(p);
        QVERIFY2(!auth.ok, "authentication with unauthorized key MUST fail");
        QVERIFY2(!auth.friendly.isEmpty(), "failure must carry a friendly message");
        engine->disconnect();
    }

    void deadPortFailsFriendly()
    {
        const int port = freeTcpPort();
        if (port <= 0)
            QSKIP("no free TCP port available");

        auto engine = createEngine(SshEngineKind::Libssh);
        QVERIFY(engine != nullptr);
        Outcome o = engine->connect(QStringLiteral("127.0.0.1"), port, 5000);
        QVERIFY2(!o.ok, "connect to a dead port must fail");
        QVERIFY2(!o.friendly.isEmpty(), "dead port must produce a friendly error");
        QVERIFY2(!engine->isConnected(), "isConnected must stay false after failure");
    }

private:
    void roundTrip(SshEngineKind kind)
    {
        auto engine = createEngine(kind);
        QVERIFY2(engine != nullptr, "createEngine must return an engine");

        // --- connect + host key callback -----------------------------------
        HostKeyInfo hostKey;
        bool hostKeyCbFired = false;
        Outcome o = engine->connect(
            QStringLiteral("127.0.0.1"), envOr("ESD_SSHD_PORT").toInt(), 10000,
            [&](const HostKeyInfo& info) {
                hostKeyCbFired = true;
                hostKey = info;
            });
        QVERIFY2(o.ok, qPrintable(QStringLiteral("connect failed: %1 (%2)")
                                      .arg(o.friendly, o.technical)));
        QVERIFY(hostKeyCbFired);
        QVERIFY2(hostKey.isValid(), "host key callback must receive a valid key");
        QVERIFY2(hostKey.sha256Fingerprint.startsWith(QLatin1String("SHA256:")),
                 qPrintable(hostKey.sha256Fingerprint));
        QVERIFY(engine->isConnected());
        // NOTE: no serverBanner() assertion - the issue banner is optional;
        // a default sshd (Banner none) legitimately sends none.

        // --- authenticate (public key) --------------------------------------
        Outcome auth = engine->authenticate(keyAuth());
        QVERIFY2(auth.ok, qPrintable(QStringLiteral("pubkey auth failed: %1 (%2)")
                                         .arg(auth.friendly, auth.technical)));

        EngineInfo info = engine->negotiatedInfo();
        QVERIFY2(!info.kex.isEmpty(), "kex must be negotiated");

        // --- exec channel ----------------------------------------------------
        QString err;
        auto ch = engine->openChannel(&err);
        QVERIFY2(ch != nullptr, qPrintable(err));
        Outcome eo = ch->openExecChannel(QStringLiteral("printf 'eclipse-rt-%s\\n' OK"));
        QVERIFY2(eo.ok, qPrintable(eo.friendly));
        QByteArray out = pumpStdout(ch.get(), 15000);
        QVERIFY2(out.contains("eclipse-rt-OK"),
                 qPrintable(QStringLiteral("exec output mismatch: %1").arg(QString::fromUtf8(out))));
        QVERIFY2(waitExitStatus(ch.get(), 10000) == 0, "exec exit status must be 0");

        // --- SFTP round trip --------------------------------------------------
        auto sftp = engine->openSftp(&err);
        QVERIFY2(sftp != nullptr && sftp->isValid(), qPrintable(err));

        QString root = sftp->canonicalize(QStringLiteral("."));
        QVERIFY2(root.startsWith(QLatin1String("/")), qPrintable(root));

        const QByteArray payload = randomPayload(256 * 1024);
        const QString remoteFile = QStringLiteral("eclipse_rt_payload.bin");

        quint64 remoteSize = 0;
        QString werr;
        auto* fh = sftp->openForWrite(remoteFile, payload.size(), false, 0644, &werr);
        QVERIFY2(fh != nullptr, qPrintable(werr));
        qint64 written = 0;
        while (written < payload.size()) {
            int n = sftp->writeFile(fh, payload.constData() + written,
                                    int(payload.size() - written), &werr);
            if (n <= 0)
                break;
            written += n;
        }
        sftp->closeFile(fh);
        QVERIFY2(written == qint64(payload.size()),
                 qPrintable(QStringLiteral("SFTP upload stalled at %1/%2 bytes: %3")
                                .arg(written).arg(payload.size()).arg(werr)));

        // stat
        SftpAttrs attrs;
        QVERIFY2(sftp->stat(remoteFile, &attrs), "stat on uploaded file must succeed");
        QCOMPARE(qint64(attrs.size), qint64(payload.size()));

        // read back and compare
        QString rerr;
        auto* rh = sftp->openForRead(remoteFile, &remoteSize, &rerr);
        QVERIFY2(rh != nullptr, qPrintable(rerr));
        QCOMPARE(remoteSize, quint64(payload.size()));
        QByteArray readBack;
        char buf[32768];
        for (;;) {
            int n = sftp->readFile(rh, buf, sizeof buf, &rerr);
            if (n < 0) {
                sftp->closeFile(rh);
                QFAIL(qPrintable(rerr));
            }
            if (n == 0)
                break;
            readBack.append(buf, n);
        }
        sftp->closeFile(rh);
        QVERIFY2(readBack == payload, "SFTP download must round-trip byte-identical content");

        // listDir sees the file
        QString lerr;
        const QVector<SftpEntry> entries = sftp->listDir(QStringLiteral("."), &lerr);
        QVERIFY2(lerr.isEmpty(), qPrintable(lerr));
        bool seen = false;
        for (const auto& e : entries)
            if (e.name == remoteFile)
                seen = true;
        QVERIFY2(seen, "listDir must contain the uploaded file");

        // rename / mkdir / rmdir / unlink housekeeping round trip
        QString aerr;
        QVERIFY2(sftp->rename(remoteFile, remoteFile + QStringLiteral(".renamed"), &aerr),
                 qPrintable(aerr));
        QVERIFY2(sftp->unlink(remoteFile + QStringLiteral(".renamed"), &aerr), qPrintable(aerr));
        QVERIFY2(sftp->mkdir(QStringLiteral("eclipse_rt_dir"), 0755, &aerr), qPrintable(aerr));
        QVERIFY2(sftp->rmdir(QStringLiteral("eclipse_rt_dir"), &aerr), qPrintable(aerr));

        // --- keepalive + disconnect ------------------------------------------
        engine->sendKeepAlive();
        engine->disconnect();
        QVERIFY2(!engine->isConnected(), "isConnected must be false after disconnect");
    }
};

// GUI-less: pure Core+Network test, QCoreApplication avoids any QPA platform
// plugin (headless CI runners have no display).
QTEST_GUILESS_MAIN(SshRoundTripTest)
#include "SshRoundTripTest.moc"
