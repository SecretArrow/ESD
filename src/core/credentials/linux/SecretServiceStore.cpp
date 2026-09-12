
#include "SecretServiceStore.h"

#include <QDBusArgument>
#include <QDBusInterface>
#include <QDBusReply>
#include <QVariantMap>

#include "common/Utils.h"
#include "core/logging/Logger.h"

namespace eclipse {

static const QLatin1String kService("org.freedesktop.secrets");
static const QLatin1String kServicePath("/org/freedesktop/secrets");
static const QLatin1String kServiceIface("org.freedesktop.Secret.Service");
static const QLatin1String kItemIface("org.freedesktop.Secret.Item");
static const QLatin1String kCollectionIface("org.freedesktop.Secret.Collection");
static const QLatin1String kDefaultAlias("default");

static QVariantMap keyAttrs(const QString& key)
{
    QVariantMap attrs;
    attrs.insert(QStringLiteral("xdg:schema"), QStringLiteral("com.eclipsessh.desktop"));
    attrs.insert(QStringLiteral("key"), key);
    return attrs;
}

bool SecretServiceStore::available()
{
    QDBusInterface svc(kService, kServicePath, kServiceIface, QDBusConnection::sessionBus());
    return svc.isValid();
}

bool SecretServiceStore::initSession()
{
    if (m_sessionTried)
        return m_session.path() != QLatin1String("/");
    m_sessionTried = true;
    QDBusInterface svc(kService, kServicePath, kServiceIface, QDBusConnection::sessionBus());
    if (!svc.isValid()) {
        m_broken = true;
        return false;
    }
    QDBusReply<QDBusVariant> reply =
        svc.call(QStringLiteral("OpenSession"), QStringLiteral("plain"),
                 QVariant::fromValue(QDBusVariant(QString())));
    if (!reply.isValid()) {
        m_broken = true;
        return false;
    }
    m_session = QDBusObjectPath(reply.value().variant().toString());
    return m_session.path() != QLatin1String("/");
}

QDBusObjectPath SecretServiceStore::defaultCollection()
{
    QDBusInterface svc(kService, kServicePath, kServiceIface, QDBusConnection::sessionBus());
    if (!svc.isValid())
        return QDBusObjectPath();
    QDBusReply<QDBusObjectPath> col = svc.call(QStringLiteral("ReadAlias"), kDefaultAlias);
    if (col.isValid() && col.value().path() != QLatin1String("/"))
        return col.value();

    // Fall back to the first collection registered on the service.
    QDBusInterface svcProps(kService, kServicePath, QLatin1String("org.freedesktop.DBus.Properties"),
                            QDBusConnection::sessionBus());
    QDBusReply<QDBusVariant> collections =
        svcProps.call(QStringLiteral("Get"), kServiceIface, QStringLiteral("Collections"));
    if (collections.isValid()) {
        QDBusArgument arg = collections.value().variant().value<QDBusArgument>();
        arg.beginArray();
        if (!arg.atEnd()) {
            QDBusObjectPath first;
            arg >> first;
            arg.endArray();
            return first;
        }
        arg.endArray();
    }
    return QDBusObjectPath();
}

QString SecretServiceStore::searchItem(const QString& key)
{
    QDBusInterface svc(kService, kServicePath, kServiceIface, QDBusConnection::sessionBus());
    if (!svc.isValid())
        return {};
    QDBusReply<QList<QDBusObjectPath>> found =
        svc.call(QStringLiteral("SearchItems"), keyAttrs(key));
    if (found.isValid() && !found.value().isEmpty())
        return found.value().first().path();
    return {};
}

bool SecretServiceStore::store(const QString& key, const QString& secret)
{
    if (m_broken || !initSession())
        return false;
    const QDBusObjectPath collection = defaultCollection();
    if (collection.path() == QLatin1String("/")) {
        m_broken = true;
        return false;
    }

    // Secret struct: (session, params, content type, value)
    QDBusArgument secretArg;
    secretArg.beginStructure();
    secretArg << m_session << QByteArray() << QStringLiteral("text/plain") << secret.toUtf8();
    secretArg.endStructure();

    const QString existing = searchItem(key);
    if (!existing.isEmpty()) {
        QDBusInterface item(kService, existing, kItemIface, QDBusConnection::sessionBus());
        if (item.isValid()) {
            item.call(QStringLiteral("SetSecret"),
                      QVariant::fromValue(secretArg));
            return true;
        }
    }

    QDBusInterface collectionIf(kService, collection.path(), kCollectionIface,
                                QDBusConnection::sessionBus());
    if (!collectionIf.isValid())
        return false;

    QVariantMap props;
    props.insert(QStringLiteral("org.freedesktop.Secret.Item.Label"),
                 QStringLiteral("Eclipse SSH: %1").arg(key));
    props.insert(QStringLiteral("org.freedesktop.Secret.Item.Attributes"), keyAttrs(key));

    // Rebuild the argument (it may have been consumed above).
    QDBusArgument secretArg2;
    secretArg2.beginStructure();
    secretArg2 << m_session << QByteArray() << QStringLiteral("text/plain") << secret.toUtf8();
    secretArg2.endStructure();

    QDBusReply<QDBusObjectPath> item =
        collectionIf.call(QStringLiteral("CreateItem"), props,
                          QVariant::fromValue(secretArg2), true);
    if (!item.isValid())
        LOG_WARN(QStringLiteral("SecretService CreateItem failed: %1").arg(item.error().message()));
    return item.isValid();
}

QString SecretServiceStore::load(const QString& key)
{
    if (m_broken || !initSession())
        return {};
    const QString itemPath = searchItem(key);
    if (itemPath.isEmpty())
        return {};
    QDBusInterface item(kService, itemPath, kItemIface, QDBusConnection::sessionBus());
    if (!item.isValid())
        return {};
    QDBusReply<QDBusVariant> sec =
        item.call(QStringLiteral("GetSecret"), QVariant::fromValue(m_session));
    if (!sec.isValid())
        return {};
    QDBusArgument arg = sec.value().variant().value<QDBusArgument>();
    QDBusObjectPath sessionPath;
    QByteArray params, value;
    QString contentType;
    arg.beginStructure();
    arg >> sessionPath >> params >> contentType >> value;
    arg.endStructure();
    return QString::fromUtf8(value);
}

bool SecretServiceStore::remove(const QString& key)
{
    const QString itemPath = searchItem(key);
    if (itemPath.isEmpty())
        return true;
    QDBusInterface item(kService, itemPath, kItemIface, QDBusConnection::sessionBus());
    if (!item.isValid())
        return false;
    item.call(QStringLiteral("Delete"));
    return true;
}

} // namespace eclipse
// end linux-only guard removed
