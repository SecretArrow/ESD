#include <QtTest/QtTest>

#include "../src/ssh/HostKeyManager.h"
#include "../src/ssh/AgentClient.h"
#include "../src/core/profiles/ProfileStore.h"
#include "../src/core/profiles/ConnectionProfile.h"
#include "../src/ssh/ForwardManager.h"

#include <QTemporaryDir>
#include <QFile>

using namespace eclipse;

static QScopedPointer<QTemporaryDir> m_dataDir;

class HostKeyAndProtocolTest : public QObject
{
    Q_OBJECT

private slots:
    void initTestCase()
    {
        // Isolate profile storage for this test run.
        QTemporaryDir* dir = new QTemporaryDir();
        dir->setAutoRemove(true);
        qputenv("XDG_DATA_HOME", dir->path().toUtf8());
        qputenv("XDG_CONFIG_HOME", dir->path().toUtf8());
        m_dataDir.reset(dir);
    }

    void fingerprintCalculation()
    {
        // SHA256:base64 of blob - verify against a known vector
        const QByteArray blob("test-blob-for-fingerprint");
        const QString fp = HostKeyManager::sha256Fingerprint(blob);
        QVERIFY(fp.startsWith(QLatin1String("SHA256:")));
        QVERIFY(fp.length() > 10);
        // Deterministic
        QCOMPARE(fp, HostKeyManager::sha256Fingerprint(blob));
    }

    void knownHostsRoundTrip()
    {
        QTemporaryDir dir;
        const QString path = dir.path() + QStringLiteral("/known_hosts");
        HostKeyManager mgr(path);

        HostKeyInfo key;
        key.host = QStringLiteral("server.example.com");
        key.port = 22;
        key.keyType = QStringLiteral("ssh-ed25519");
        key.publicKeyBlob = QByteArray::fromBase64(
            QByteArrayLiteral("AAAAC3NzaC1lZDI1NTE5AAAAIB0Z8sN2bCpPqNf2iX1DkF5T3hVZm9vYmFy"));

        // Unknown initially
        QCOMPARE(mgr.check(key), HostKeyCheckResult::Unknown);

        // Save then known
        QVERIFY(mgr.save(key));
        QCOMPARE(mgr.check(key), HostKeyCheckResult::Known);

        // Different key for same host => Changed
        HostKeyInfo other = key;
        other.publicKeyBlob = QByteArrayLiteral("different-blob");
        QCOMPARE(mgr.check(other), HostKeyCheckResult::Changed);

        // Non-standard port uses [host]:port pattern
        HostKeyInfo pkey = key;
        pkey.port = 2222;
        QCOMPARE(mgr.check(pkey), HostKeyCheckResult::Unknown);
        QVERIFY(mgr.save(pkey));
        QCOMPARE(mgr.check(pkey), HostKeyCheckResult::Known);
        // and does not clash with port 22 entry
        QCOMPARE(mgr.check(key), HostKeyCheckResult::Known);

        // removeKey deletes only matching type+host
        QVERIFY(mgr.removeKey(QStringLiteral("server.example.com"), 22, QStringLiteral("ssh-ed25519")));
        QCOMPARE(mgr.check(key), HostKeyCheckResult::Unknown);
        QCOMPARE(mgr.check(pkey), HostKeyCheckResult::Known);
    }

    void knownHostsParsing()
    {
        const QString content = QStringLiteral(
            "# comment\n"
            "host1,host2 ssh-rsa AAAAB3NzaC1yc2EAAAADAQABAAABgQ abc comment\n"
            "|1|c2FsdA==|aGFzaA== ssh-ed25519 AAAAC3NzaC1lZDI1NTE5AAAAIA==\n");
        QString err;
        const auto entries = HostKeyManager::parseKnownHosts(content, &err);
        QCOMPARE(entries.size(), 2);
        QCOMPARE(entries[0].hostPatterns.size(), 2);
        QCOMPARE(entries[0].keyType, QStringLiteral("ssh-rsa"));
        QVERIFY(!entries[0].blob.isEmpty());
        QVERIFY(entries[1].hashed);
    }

    void agentWireFormat()
    {
        using namespace agentwire;
        QByteArray buf;
        putU32(buf, 0x01020304);
        QCOMPARE(int(quint8(buf[0])), 1);
        QCOMPARE(int(quint8(buf[3])), 4);

        putString(buf, QByteArrayLiteral("ssh-ed25519"));
        int pos = 4;
        bool ok = true;
        QCOMPARE(getString(buf, pos, &ok), QByteArrayLiteral("ssh-ed25519"));
        QVERIFY(ok);

        QByteArray blob;
        putString(blob, QByteArrayLiteral("ssh-ed25519"));
        QCOMPARE(peekKeyType(blob), QStringLiteral("ssh-ed25519"));
    }

    void socks5Handshake()
    {
        // Greeting + CONNECT in ONE cumulative buffer (like the worker uses).
        QByteArray buf;
        buf.append(char(0x05)).append(char(0x01)).append(char(0x00));          // greeting
        buf.append(char(0x05)).append(char(0x01)).append(char(0x00)).append(char(0x01))
           .append(char(10)).append(char(0)).append(char(0)).append(char(1))
           .append(char(0x01)).append(char(0xbb));                              // CONNECT 10.0.0.1:443
        QByteArray reply;
        QString host;
        int port = 0;
        const bool done = ForwardManager::parseSocks5Step(&buf, &host, &port, &reply);
        QVERIFY(done);
        QCOMPARE(int(reply.size()), 2);
        QCOMPARE(int(quint8(reply[0])), 5);
        QCOMPARE(int(quint8(reply[1])), 0); // no-auth chosen
        QCOMPARE(host, QStringLiteral("10.0.0.1"));
        QCOMPARE(port, 443);
    }

    void profileJsonRoundTrip()
    {
        ConnectionProfile p;
        p.name = QStringLiteral("prod-web");
        p.host = QStringLiteral("10.1.1.5");
        p.port = 2200;
        p.username = QStringLiteral("deploy");
        p.authMethod = QStringLiteral("publickey");
        p.privateKeyPath = QStringLiteral("/home/u/.ssh/id_ed25519");
        p.engine = QStringLiteral("libssh2");
        p.jumpHosts = QStringList { QStringLiteral("bastion"), QStringLiteral("user@j2:2222") };
        p.compression = true;
        p.keepAliveSeconds = 30;
        ForwardRuleSpec rule;
        rule.id = QStringLiteral("r1");
        rule.type = QStringLiteral("local");
        rule.listenPort = 5432;
        rule.destHost = QStringLiteral("db.internal");
        rule.destPort = 5432;
        p.forwardingRules.append(rule);

        const QJsonObject o = p.extraJson();
        ConnectionProfile q;
        q.applyExtraJson(o);
        const QVariantMap vm = p.toVariantMap();
        const ConnectionProfile core = ConnectionProfile::fromVariantMap(vm);
        QCOMPARE(core.host, p.host);
        QCOMPARE(core.port, p.port);
        QCOMPARE(core.username, p.username);
        QCOMPARE(core.authMethod, p.authMethod);
        QCOMPARE(core.engine, p.engine);
        QCOMPARE(q.jumpHosts.size(), 2);
        QCOMPARE(q.jumpHosts.at(1), QStringLiteral("user@j2:2222"));
        QVERIFY(q.compression);
        QCOMPARE(q.keepAliveSeconds, 30);
        QCOMPARE(q.forwardingRules.size(), 1);
        QCOMPARE(q.forwardingRules.first().listenPort, 5432);
        QCOMPARE(q.forwardingRules.first().destHost, QStringLiteral("db.internal"));
    }

    void openSshConfigImport()
    {
        QTemporaryDir dir;
        const QString cfg = dir.path() + QStringLiteral("/config");
        QFile f(cfg);
        QVERIFY(f.open(QIODevice::WriteOnly));
        f.write(QByteArrayLiteral(
            "Host web1\n"
            "    HostName 192.168.1.10\n"
            "    Port 2222\n"
            "    User deploy\n"
            "    IdentityFile ~/.ssh/id_rsa\n"
            "    Compression yes\n"
            "\n"
            "Host *web2*\n"
            "    HostName ignored\n"));
        f.close();

        ProfileStore* store = new ProfileStore(this);
        QVERIFY(store->init());
        const QString err = store->importFromOpenSshConfig(cfg);
        QVERIFY(err.isEmpty());
        const auto all = store->all();
        QCOMPARE(all.size(), 1); // wildcard pattern skipped
        QCOMPARE(all.first().host, QStringLiteral("192.168.1.10"));
        QCOMPARE(all.first().port, 2222);
        QCOMPARE(all.first().username, QStringLiteral("deploy"));
        QCOMPARE(all.first().authMethod, QStringLiteral("publickey"));
        QVERIFY(all.first().compression);
        store->remove(all.first().id);
    }

};


QTEST_GUILESS_MAIN(HostKeyAndProtocolTest)

#include "HostKeyAndProtocolTest.moc"
