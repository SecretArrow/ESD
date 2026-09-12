#include "ConnectionProfile.h"

#include <QJsonArray>
#include <QJsonDocument>
#include <QUrl>

namespace eclipse {

namespace {
int toIntD(const QVariant& v, int def)
{
    bool ok = false;
    const int r = v.toInt(&ok);
    return ok ? r : def;
}
qint64 toI64D(const QVariant& v, qint64 def = 0)
{
    bool ok = false;
    const qint64 r = v.toLongLong(&ok);
    return ok ? r : def;
}
QString toStrD(const QVariant& v, const QString& def)
{
    const QString s = v.toString();
    return s.isEmpty() ? def : s;
}
} // namespace

QJsonObject ForwardRuleSpec::toJson() const
{
    return {
        { "id", id },
        { "name", name },
        { "type", type },
        { "listenAddress", listenAddress },
        { "listenPort", listenPort },
        { "destHost", destHost },
        { "destPort", destPort },
        { "autoStart", autoStart },
        { "enabled", enabled },
    };
}

ForwardRuleSpec ForwardRuleSpec::fromJson(const QJsonObject& o)
{
    ForwardRuleSpec r;
    r.id = o.value(QStringLiteral("id")).toString();
    r.name = o.value(QStringLiteral("name")).toString();
    r.type = o.value(QStringLiteral("type")).toString(QStringLiteral("local"));
    r.listenAddress = o.value(QStringLiteral("listenAddress")).toString(QStringLiteral("127.0.0.1"));
    r.listenPort = o.value(QStringLiteral("listenPort")).toInt();
    r.destHost = o.value(QStringLiteral("destHost")).toString();
    r.destPort = o.value(QStringLiteral("destPort")).toInt();
    r.autoStart = o.value(QStringLiteral("autoStart")).toBool();
    r.enabled = o.value(QStringLiteral("enabled")).toBool(true);
    return r;
}

QVariantMap ConnectionProfile::toVariantMap() const
{
    return {
        { "id", id },
        { "name", name },
        { "group", group },
        { "tags", tags },
        { "favorite", favorite },
        { "host", host },
        { "port", port },
        { "username", username },
        { "authMethod", authMethod },
        { "privateKeyPath", privateKeyPath },
        { "engine", engine },
        { "lastUsedMs", lastUsedMs },
    };
}

ConnectionProfile ConnectionProfile::fromVariantMap(const QVariantMap& m)
{
    ConnectionProfile p;
    p.id = toI64D(m.value(QStringLiteral("id")));
    p.name = m.value(QStringLiteral("name")).toString();
    p.group = m.value(QStringLiteral("group")).toString();
    p.tags = m.value(QStringLiteral("tags")).toStringList();
    p.favorite = m.value(QStringLiteral("favorite")).toBool();
    p.host = m.value(QStringLiteral("host")).toString();
    p.port = toIntD(m.value(QStringLiteral("port")), 22);
    p.username = m.value(QStringLiteral("username")).toString();
    p.authMethod = toStrD(m.value(QStringLiteral("authMethod")), QStringLiteral("password"));
    p.privateKeyPath = m.value(QStringLiteral("privateKeyPath")).toString();
    p.engine = toStrD(m.value(QStringLiteral("engine")), QStringLiteral("auto"));
    p.lastUsedMs = toI64D(m.value(QStringLiteral("lastUsedMs")));
    return p;
}

QJsonObject ConnectionProfile::extraJson() const
{
    QJsonArray rules;
    for (const auto& r : forwardingRules)
        rules.append(r.toJson());

    QJsonObject o;
    o.insert(QStringLiteral("proxyType"), proxyType);
    o.insert(QStringLiteral("proxyHost"), proxyHost);
    o.insert(QStringLiteral("proxyPort"), proxyPort);
    o.insert(QStringLiteral("proxyUser"), proxyUser);
    o.insert(QStringLiteral("jumpHosts"), QJsonArray::fromStringList(jumpHosts));
    o.insert(QStringLiteral("keepAliveSeconds"), keepAliveSeconds);
    o.insert(QStringLiteral("compression"), compression);
    o.insert(QStringLiteral("connectTimeoutMs"), connectTimeoutMs);
    o.insert(QStringLiteral("allowKeyboardInteractive"), allowKeyboardInteractive);
    o.insert(QStringLiteral("autoReconnect"), autoReconnect);
    o.insert(QStringLiteral("termFontFamily"), terminal.fontFamily);
    o.insert(QStringLiteral("termFontSize"), terminal.fontSize);
    o.insert(QStringLiteral("termColorScheme"), terminal.colorScheme);
    o.insert(QStringLiteral("startupCommands"), QJsonArray::fromStringList(terminal.startupCommands));
    o.insert(QStringLiteral("encoding"), terminal.encoding);
    o.insert(QStringLiteral("sftpDefaultRemoteDir"), sftpDefaultRemoteDir);
    o.insert(QStringLiteral("sftpDefaultLocalDir"), sftpDefaultLocalDir);
    o.insert(QStringLiteral("transferProtocol"), transferProtocol);
    o.insert(QStringLiteral("forwardingRules"), rules);
    o.insert(QStringLiteral("environment"), QJsonArray::fromStringList(environment));
    o.insert(QStringLiteral("createdMs"), createdMs);
    return o;
}

void ConnectionProfile::applyExtraJson(const QJsonObject& o)
{
    proxyType = toStrD(o.value(QStringLiteral("proxyType")), QStringLiteral("none"));
    proxyHost = o.value(QStringLiteral("proxyHost")).toString();
    proxyPort = o.value(QStringLiteral("proxyPort")).toInt();
    proxyUser = o.value(QStringLiteral("proxyUser")).toString();
    const QJsonArray jumps = o.value(QStringLiteral("jumpHosts")).toArray();
    jumpHosts.clear();
    for (const auto& j : jumps)
        jumpHosts.append(j.toString());
    keepAliveSeconds = o.value(QStringLiteral("keepAliveSeconds")).toInt();
    if (keepAliveSeconds <= 0)
        keepAliveSeconds = 15;
    compression = o.value(QStringLiteral("compression")).toBool();
    connectTimeoutMs = o.value(QStringLiteral("connectTimeoutMs")).toInt();
    if (connectTimeoutMs <= 0)
        connectTimeoutMs = 15000;
    allowKeyboardInteractive = o.value(QStringLiteral("allowKeyboardInteractive")).toBool(true);
    autoReconnect = o.value(QStringLiteral("autoReconnect")).toBool(true);
    terminal.fontFamily = o.value(QStringLiteral("termFontFamily")).toString();
    terminal.fontSize = o.value(QStringLiteral("termFontSize")).toInt();
    terminal.colorScheme = o.value(QStringLiteral("termColorScheme")).toString();
    terminal.startupCommands.clear();
    for (const auto& c : o.value(QStringLiteral("startupCommands")).toArray())
        terminal.startupCommands.append(c.toString());
    terminal.encoding = toStrD(o.value(QStringLiteral("encoding")), QStringLiteral("UTF-8"));
    sftpDefaultRemoteDir = o.value(QStringLiteral("sftpDefaultRemoteDir")).toString();
    sftpDefaultLocalDir = o.value(QStringLiteral("sftpDefaultLocalDir")).toString();
    transferProtocol = toStrD(o.value(QStringLiteral("transferProtocol")), QStringLiteral("sftp"));
    forwardingRules.clear();
    for (const auto& r : o.value(QStringLiteral("forwardingRules")).toArray())
        forwardingRules.append(ForwardRuleSpec::fromJson(r.toObject()));
    environment.clear();
    for (const auto& e : o.value(QStringLiteral("environment")).toArray())
        environment.append(e.toString());
    createdMs = static_cast<qint64>(o.value(QStringLiteral("createdMs")).toDouble());
}

QString ConnectionProfile::effectiveJumpChain() const
{
    return jumpHosts.join(QStringLiteral(" -> "));
}

} // namespace eclipse
