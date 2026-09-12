#include "JumpConnector.h"

#include <QThread>

#include "../core/credentials/CredentialManager.h"
#include "../core/logging/Logger.h"
#include "../core/profiles/ProfileStore.h"
#include "HostKeyManager.h"
#include "../common/Utils.h"
#include "backends/libssh/LibsshEngine.h"
#include "ISshEngine.h"
#include "ProxyDialer.h"

namespace eclipse {

JumpConnector::~JumpConnector()
{
    m_bridges.clear(); // Bridge dtors close their fds and join pump threads
}

bool JumpConnector::establishChain(const ConnectionProfile& profile, int& outFd, QString* err)
{
    outFd = -1;
    HostKeyManager hostKeys;

    // Parse the chain: each entry is a profile name ("profile:Name" or just
    // the profile name) or a quick spec "user@host:port" (agent auth).
    struct JumpSpec
    {
        QString user;
        QString host;
        int port = 22;
        qint64 profileId = 0;
        QString label;
    };
    QVector<JumpSpec> chain;
    for (const QString& raw : profile.jumpHosts) {
        QString entry = raw.trimmed();
        if (entry.isEmpty())
            continue;
        JumpSpec spec;
        if (entry.startsWith(QLatin1String("profile:"), Qt::CaseInsensitive))
            entry = entry.mid(8);
        // Try to resolve as profile name first
        for (const ConnectionProfile& p : ProfileStore::instance().all()) {
            if (p.name.compare(entry, Qt::CaseInsensitive) == 0
                || p.name.compare(entry.mid(entry.lastIndexOf(QLatin1Char('@')) + 1), Qt::CaseInsensitive) == 0) {
                spec.profileId = p.id;
                spec.user = p.username;
                spec.host = p.host;
                spec.port = p.port;
                spec.label = p.name;
                break;
            }
        }
        if (spec.profileId == 0) {
            // user@host:port
            QString rest = entry;
            const int at = rest.lastIndexOf(QLatin1Char('@'));
            if (at >= 0) {
                spec.user = rest.left(at);
                rest = rest.mid(at + 1);
            }
            const int colon = rest.lastIndexOf(QLatin1Char(':'));
            if (colon >= 0) {
                spec.port = rest.mid(colon + 1).toInt();
                if (spec.port <= 0)
                    spec.port = 22;
                rest = rest.left(colon);
            }
            spec.host = rest;
            spec.label = entry;
        }
        if (spec.host.isEmpty()) {
            if (err)
                *err = QStringLiteral("Invalid jump host entry: %1").arg(raw);
            return false;
        }
        chain.append(spec);
    }

    const HostKeyInfo targetKeyPlaceholder;
    int currentFd = -1;

    for (int i = 0; i < chain.size(); ++i) {
        const JumpSpec& hop = chain[i];
        const bool isLast = (i == chain.size() - 1);
        const QString destHost = isLast ? profile.host : chain[i + 1].host;
        const int destPort = isLast ? profile.port : chain[i + 1].port;

        // 1. Connect + authenticate to this hop
        std::unique_ptr<ISshEngine> engine = createEngine(SshEngineKind::Libssh);
#ifdef ECLIPSE_HAVE_LIBSSH
        if (auto* libssh = dynamic_cast<LibsshEngine*>(engine.get()))
            libssh->setKnownHostsPath(hostKeys.path());
#endif
        Outcome oc = engine->connect(hop.host, hop.port, profile.connectTimeoutMs, {});
        if (!oc.ok) {
            if (err)
                *err = QStringLiteral("Bastion %1: %2").arg(hop.label, oc.friendly);
            return false;
        }

        // 2. Host key policy for hops: must already be known & unchanged.
        const HostKeyInfo key = engine->serverHostKey();
        const HostKeyCheckResult check = hostKeys.check(key);
        if (check == HostKeyCheckResult::Changed) {
            if (err)
                *err = QStringLiteral("The host key of bastion %1 has CHANGED. Aborting for your safety.")
                           .arg(hop.host);
            return false;
        }
        if (check == HostKeyCheckResult::Unknown) {
            if (err)
                *err = QStringLiteral("Bastion %1 is not in known_hosts yet.\n"
                                      "Connect to it directly once and save its host key.")
                           .arg(hop.host);
            return false;
        }

        // 3. Authenticate
        AuthParams params;
        params.username = hop.user;
        if (hop.profileId != 0) {
            const ConnectionProfile hopProfile = ProfileStore::instance().get(hop.profileId);
            params.username = hopProfile.username.isEmpty() ? hop.user : hopProfile.username;
            const QString base = QStringLiteral("profile:%1").arg(hopProfile.id);
            if (hopProfile.authMethod == QLatin1String("publickey")) {
                params.method = SshAuthMethod::PublicKey;
                params.privateKeyPath = utils::expandTildePath(hopProfile.privateKeyPath);
                params.passphrase = CredentialManager::instance().loadSecret(base + QStringLiteral(":passphrase"));
            } else if (hopProfile.authMethod == QLatin1String("agent")) {
                params.method = SshAuthMethod::Agent;
            } else {
                params.method = SshAuthMethod::Password;
                params.password = CredentialManager::instance().loadSecret(base + QStringLiteral(":password"));
            }
        } else {
            params.method = SshAuthMethod::Agent;
        }
        const Outcome auth = engine->authenticate(params);
        if (!auth.ok) {
            if (err)
                *err = QStringLiteral("Bastion %1: %2").arg(hop.label, auth.friendly);
            return false;
        }
        LOG_CONN(QStringLiteral("Jump hop %1 (%2) authenticated").arg(i + 1).arg(hop.label));

        // 4. Bridge to the next destination through direct-tcpip
        QString cerr;
        std::unique_ptr<IChannel> channel = engine->openForwardChannel(destHost, destPort, &cerr);
        if (!channel) {
            if (err)
                *err = QStringLiteral("Bastion %1 refused the tunnel to %2:%3 - %4")
                           .arg(hop.label, destHost, QString::number(destPort), cerr);
            return false;
        }

        auto [fdA, fdB] = makeSocketPair();
        if (fdA < 0 || fdB < 0) {
            if (err)
                *err = QStringLiteral("Failed to create the local tunnel bridge.");
            return false;
        }

        // Move the channel into the bridge closures (keep engine alive too).
        auto enginePtr = std::shared_ptr<ISshEngine>(engine.release());
        auto channelPtr = std::shared_ptr<IChannel>(channel.release());
        m_bridges.push_back(std::make_unique<Bridge>(
            fdA,
            [enginePtr, channelPtr](char* buf, int len) -> int {
                std::lock_guard<std::recursive_mutex> lock(enginePtr->rawMutex());
                return channelPtr->readStdout(buf, len);
            },
            [enginePtr, channelPtr](const char* buf, int len) -> int {
                std::lock_guard<std::recursive_mutex> lock(enginePtr->rawMutex());
                return channelPtr->write(buf, len);
            }));
        m_hopLabels.push_back(hop.label);
        currentFd = fdB;
    }

    outFd = currentFd;
    return outFd >= 0;
}

} // namespace eclipse
