#include "ProfileStore.h"

#include <QDateTime>
#include <QDir>
#include <QFile>
#include <QJsonArray>
#include <QJsonDocument>
#include <QJsonObject>
#include <QSqlError>
#include <QSqlQuery>

#include "../../common/Utils.h"
#include "../Database.h"
#include "../logging/Logger.h"

namespace eclipse {

ProfileStore& ProfileStore::instance()
{
    static ProfileStore store;
    return store;
}

ProfileStore::ProfileStore(QObject* parent)
    : QObject(parent)
{
}

bool ProfileStore::init()
{
    return Database::instance().open();
}

ConnectionProfile ProfileStore::rowToProfile(QSqlQuery& q) const
{
    ConnectionProfile p;
    p.id = q.value(0).toLongLong();
    p.name = q.value(1).toString();
    p.group = q.value(2).toString();
    p.tags = QJsonDocument::fromJson(q.value(3).toString().toUtf8()).toVariant().toStringList();
    p.favorite = q.value(4).toBool();
    p.host = q.value(5).toString();
    p.port = q.value(6).toInt();
    if (p.port <= 0)
        p.port = 22;
    p.username = q.value(7).toString();
    p.authMethod = q.value(8).toString();
    if (p.authMethod.isEmpty())
        p.authMethod = QStringLiteral("password");
    p.engine = q.value(9).toString();
    if (p.engine.isEmpty())
        p.engine = QStringLiteral("auto");
    p.applyExtraJson(QJsonDocument::fromJson(q.value(10).toString().toUtf8()).object());
    p.createdMs = q.value(11).toLongLong();
    p.lastUsedMs = q.value(12).toLongLong();
    return p;
}

QVector<ConnectionProfile> ProfileStore::all() const
{
    QVector<ConnectionProfile> out;
    QSqlQuery q(Database::instance().handle());
    q.prepare(QStringLiteral(
        "SELECT id,name,group_name,tags,favorite,host,port,username,auth_method,engine,"
        "settings_json,created_ms,last_used_ms FROM profiles ORDER BY name COLLATE NOCASE"));
    if (!q.exec())
        return out;
    while (q.next())
        out.append(rowToProfile(q));
    return out;
}

ConnectionProfile ProfileStore::get(qint64 id) const
{
    QSqlQuery q(Database::instance().handle());
    q.prepare(QStringLiteral(
        "SELECT id,name,group_name,tags,favorite,host,port,username,auth_method,engine,"
        "settings_json,created_ms,last_used_ms FROM profiles WHERE id=?"));
    q.addBindValue(id);
    q.exec();
    if (q.next())
        return rowToProfile(q);
    return {};
}

qint64 ProfileStore::add(const ConnectionProfile& p)
{
    QSqlQuery q(Database::instance().handle());
    q.prepare(QStringLiteral(
        "INSERT INTO profiles(name,group_name,tags,favorite,host,port,username,auth_method,"
        "engine,settings_json,created_ms,last_used_ms) VALUES(?,?,?,?,?,?,?,?,?,?,?,?)"));
    q.addBindValue(p.name.isEmpty() ? p.host : p.name);
    q.addBindValue(p.group);
    q.addBindValue(QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(p.tags)).toJson(QJsonDocument::Compact)));
    q.addBindValue(p.favorite ? 1 : 0);
    q.addBindValue(p.host);
    q.addBindValue(p.port);
    q.addBindValue(p.username);
    q.addBindValue(p.authMethod);
    q.addBindValue(p.engine);
    q.addBindValue(QString::fromUtf8(QJsonDocument(p.extraJson()).toJson(QJsonDocument::Compact)));
    const qint64 now = QDateTime::currentMSecsSinceEpoch();
    q.addBindValue(p.createdMs ? p.createdMs : now);
    q.addBindValue(p.lastUsedMs);
    if (!q.exec()) {
        LOG_ERROR(QStringLiteral("profile add failed: %1").arg(q.lastError().text()));
        return 0;
    }
    const qint64 id = q.lastInsertId().toLongLong();
    emit profileAdded(id);
    return id;
}

bool ProfileStore::update(const ConnectionProfile& p)
{
    QSqlQuery q(Database::instance().handle());
    q.prepare(QStringLiteral(
        "UPDATE profiles SET name=?,group_name=?,tags=?,favorite=?,host=?,port=?,username=?,"
        "auth_method=?,engine=?,settings_json=? WHERE id=?"));
    q.addBindValue(p.name);
    q.addBindValue(p.group);
    q.addBindValue(QString::fromUtf8(QJsonDocument(QJsonArray::fromStringList(p.tags)).toJson(QJsonDocument::Compact)));
    q.addBindValue(p.favorite ? 1 : 0);
    q.addBindValue(p.host);
    q.addBindValue(p.port);
    q.addBindValue(p.username);
    q.addBindValue(p.authMethod);
    q.addBindValue(p.engine);
    q.addBindValue(QString::fromUtf8(QJsonDocument(p.extraJson()).toJson(QJsonDocument::Compact)));
    q.addBindValue(p.id);
    if (!q.exec()) {
        LOG_ERROR(QStringLiteral("profile update failed: %1").arg(q.lastError().text()));
        return false;
    }
    emit profileUpdated(p.id);
    return true;
}

bool ProfileStore::remove(qint64 id)
{
    QSqlQuery q(Database::instance().handle());
    q.prepare(QStringLiteral("DELETE FROM profiles WHERE id=?"));
    q.addBindValue(id);
    if (!q.exec())
        return false;
    emit profileRemoved(id);
    return true;
}

bool ProfileStore::duplicate(qint64 id, const QString& newName)
{
    ConnectionProfile p = get(id);
    if (p.id == 0)
        return false;
    p.id = 0;
    p.name = newName.isEmpty() ? p.name + QStringLiteral(" (copy)") : newName;
    p.lastUsedMs = 0;
    return add(p) > 0;
}

void ProfileStore::touchLastUsed(qint64 id)
{
    QSqlQuery q(Database::instance().handle());
    q.prepare(QStringLiteral("UPDATE profiles SET last_used_ms=? WHERE id=?"));
    q.addBindValue(QDateTime::currentMSecsSinceEpoch());
    q.addBindValue(id);
    q.exec();
}

QStringList ProfileStore::groups() const
{
    QStringList out;
    QSqlQuery q(Database::instance().handle());
    q.exec(QStringLiteral(
        "SELECT DISTINCT group_name FROM profiles WHERE group_name<>'' ORDER BY group_name"));
    while (q.next())
        out.append(q.value(0).toString());
    return out;
}

QStringList ProfileStore::allTags() const
{
    QSet<QString> tags;
    for (const auto& p : all())
        for (const auto& t : p.tags)
            tags.insert(t);
    QStringList out = QStringList(tags.cbegin(), tags.cend());
    out.sort();
    return out;
}

int ProfileStore::count() const
{
    QSqlQuery q(Database::instance().handle());
    q.exec(QStringLiteral("SELECT COUNT(*) FROM profiles"));
    return q.next() ? q.value(0).toInt() : 0;
}

QVariantList ProfileStore::allProfiles() const
{
    QVariantList out;
    const auto profiles = all();
    out.reserve(profiles.size());
    for (const auto& p : profiles)
        out.append(p.toVariantMap());
    return out;
}

QVariantMap ProfileStore::profileById(qint64 id) const
{
    const ConnectionProfile p = get(id);
    return p.id > 0 ? p.toVariantMap() : QVariantMap{};
}

// ---------------------------------------------------------------------------
// Export / import ------------------------------------------------------------
QString ProfileStore::exportToJson(const QString& path, bool) const
{
    QJsonArray arr;
    for (const auto& p : all()) {
        QJsonObject o;
        o.insert(QStringLiteral("name"), p.name);
        o.insert(QStringLiteral("group"), p.group);
        o.insert(QStringLiteral("tags"), QJsonArray::fromStringList(p.tags));
        o.insert(QStringLiteral("favorite"), p.favorite);
        o.insert(QStringLiteral("host"), p.host);
        o.insert(QStringLiteral("port"), p.port);
        o.insert(QStringLiteral("username"), p.username);
        o.insert(QStringLiteral("authMethod"), p.authMethod);
        o.insert(QStringLiteral("privateKeyPath"), p.privateKeyPath);
        o.insert(QStringLiteral("engine"), p.engine);
        o.insert(QStringLiteral("extra"), p.extraJson());
        arr.append(o);
    }
    QJsonDocument doc(arr);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return QStringLiteral("Cannot write %1: %2").arg(path, f.errorString());
    f.write(doc.toJson(QJsonDocument::Indented));
    return {};
}

QString ProfileStore::importFromJson(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QStringLiteral("Cannot read %1: %2").arg(path, f.errorString());
    QJsonParseError err;
    const auto doc = QJsonDocument::fromJson(f.readAll(), &err);
    if (doc.isNull())
        return QStringLiteral("Invalid JSON: %1").arg(err.errorString());
    const auto arr = doc.isArray() ? doc.array() : QJsonArray { doc.object() };
    int n = 0;
    for (const auto& v : arr) {
        const QJsonObject o = v.toObject();
        ConnectionProfile p;
        p.name = o.value(QStringLiteral("name")).toString();
        p.group = o.value(QStringLiteral("group")).toString();
        for (const auto& t : o.value(QStringLiteral("tags")).toArray())
            p.tags.append(t.toString());
        p.favorite = o.value(QStringLiteral("favorite")).toBool();
        p.host = o.value(QStringLiteral("host")).toString();
        p.port = o.value(QStringLiteral("port")).toInt(22);
        p.username = o.value(QStringLiteral("username")).toString();
        p.authMethod = o.value(QStringLiteral("authMethod")).toString(QStringLiteral("password"));
        p.privateKeyPath = o.value(QStringLiteral("privateKeyPath")).toString();
        p.engine = o.value(QStringLiteral("engine")).toString(QStringLiteral("auto"));
        p.applyExtraJson(o.value(QStringLiteral("extra")).toObject());
        if (p.host.isEmpty())
            continue;
        if (add(p) > 0)
            ++n;
    }
    LOG_APP(QStringLiteral("Imported %1 profiles from %2").arg(n).arg(path));
    return {};
}

QString ProfileStore::exportToOpenSshConfig(const QString& path) const
{
    QString out;
    out += QLatin1String("# Generated by Eclipse SSH Desktop\n\n");
    for (const auto& prof : all()) {
        ConnectionProfile p = prof;
        QString sshName = p.name;
        sshName.replace(QLatin1Char(' '), QLatin1Char('-'));
        out += QStringLiteral("Host %1\n").arg(sshName);
        out += QStringLiteral("    HostName %1\n").arg(p.host);
        out += QStringLiteral("    Port %1\n").arg(p.port);
        out += QStringLiteral("    User %1\n").arg(p.username);
        if (!p.privateKeyPath.isEmpty())
            out += QStringLiteral("    IdentityFile %1\n").arg(p.privateKeyPath);
        if (!p.jumpHosts.isEmpty())
            out += QStringLiteral("    ProxyJump %1\n").arg(p.jumpHosts.join(QLatin1Char(',')));
        if (p.compression)
            out += QLatin1String("    Compression yes\n");
        out += QLatin1String("\n");
    }
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return QStringLiteral("Cannot write %1: %2").arg(path, f.errorString());
    f.write(out.toUtf8());
    return {};
}

QString ProfileStore::importFromOpenSshConfig(const QString& path)
{
    QFile f(path);
    if (!f.open(QIODevice::ReadOnly))
        return QStringLiteral("Cannot read %1: %2").arg(path, f.errorString());

    const QStringList lines = QString::fromUtf8(f.readAll()).split(QLatin1Char('\n'));
    int imported = 0;
    ConnectionProfile cur;
    bool haveCur = false;
    auto flush = [&]() {
        if (haveCur && !cur.host.isEmpty()) {
            if (add(cur) > 0)
                ++imported;
        }
        haveCur = false;
        cur = ConnectionProfile {};
    };

    for (QString raw : lines) {
        const QString line = raw.trimmed();
        if (line.isEmpty() || line.startsWith(QLatin1Char('#')))
            continue;
        const int sp = line.indexOf(QLatin1Char(' '));
        if (sp < 0)
            continue;
        const QString key = line.left(sp).toLower();
        const QString val = line.mid(sp + 1).trimmed();
        if (key == QLatin1String("host")) {
            flush(); // start a new block (wildcard blocks are parsed but skipped)
            if (!val.contains(QLatin1Char('*'))) {
                haveCur = true;
                cur.name = val;
                cur.host = val;
            } else {
                haveCur = false;
            }
        } else if (haveCur) {
            if (key == QLatin1String("hostname"))
                cur.host = val;
            else if (key == QLatin1String("port")) {
                cur.port = val.toInt();
                if (cur.port <= 0)
                    cur.port = 22;
            }
            else if (key == QLatin1String("user"))
                cur.username = val;
            else if (key == QLatin1String("identityfile")) {
                cur.privateKeyPath = utils::expandTildePath(val);
                if (cur.authMethod == QLatin1String("password"))
                    cur.authMethod = QStringLiteral("publickey");
            } else if (key == QLatin1String("proxyjump"))
                cur.jumpHosts = val.split(QLatin1Char(','), Qt::SkipEmptyParts);
            else if (key == QLatin1String("compression") && val == QLatin1String("yes"))
                cur.compression = true;
        }
    }
    flush();
    LOG_APP(QStringLiteral("Imported %1 profiles from OpenSSH config %2").arg(imported).arg(path));
    return {};
}

// ---------------------------------------------------------------------------
// Backup / restore -----------------------------------------------------------
QString ProfileStore::backupToFile(const QString& path) const
{
    QJsonObject root;
    root.insert(QStringLiteral("app"), QStringLiteral("eclipse-ssh-desktop"));
    root.insert(QStringLiteral("version"), 1);
    root.insert(QStringLiteral("exportedAt"), QDateTime::currentDateTime().toString(Qt::ISODate));
    root.insert(QStringLiteral("warning"),
                QStringLiteral("This backup contains connection metadata only - no passwords or keys."));
    QJsonArray arr;
    for (const auto& p : all()) {
        QJsonObject o;
        o.insert(QStringLiteral("name"), p.name);
        o.insert(QStringLiteral("group"), p.group);
        o.insert(QStringLiteral("tags"), QJsonArray::fromStringList(p.tags));
        o.insert(QStringLiteral("favorite"), p.favorite);
        o.insert(QStringLiteral("host"), p.host);
        o.insert(QStringLiteral("port"), p.port);
        o.insert(QStringLiteral("username"), p.username);
        o.insert(QStringLiteral("authMethod"), p.authMethod);
        o.insert(QStringLiteral("privateKeyPath"), p.privateKeyPath);
        o.insert(QStringLiteral("engine"), p.engine);
        o.insert(QStringLiteral("extra"), p.extraJson());
        arr.append(o);
    }
    root.insert(QStringLiteral("profiles"), arr);
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly))
        return QStringLiteral("Cannot write %1: %2").arg(path, f.errorString());
    f.write(QJsonDocument(root).toJson(QJsonDocument::Indented));
    return {};
}

QString ProfileStore::restoreFromFile(const QString& path)
{
    return importFromJson(path); // same JSON shape (profiles array or wrapped)
}

} // namespace eclipse
