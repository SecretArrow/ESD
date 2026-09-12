#pragma once

#include <QVariant>
#include <QVector>

#include "ConnectionProfile.h"

#include <QSqlQuery>

class QSqlDatabase;

namespace eclipse {

// SQLite-backed connection profile storage with search, tags, groups and
// favorites. Designed to stay responsive with 1000+ profiles (indexed lookups,
// lazy load on demand).
class ProfileStore : public QObject
{
    Q_OBJECT
public:
    static ProfileStore& instance();

    explicit ProfileStore(QObject* parent = nullptr);

    bool init();

    QVector<ConnectionProfile> all() const;
    ConnectionProfile get(qint64 id) const;
    qint64 add(const ConnectionProfile& p);              // returns new id
    bool update(const ConnectionProfile& p);
    bool remove(qint64 id);
    bool duplicate(qint64 id, const QString& newName);   // returns via profileAdded
    void touchLastUsed(qint64 id);

    // Import / export --------------------------------------------------------
    QString exportToJson(const QString& path, bool includeNotes) const;
    QString importFromJson(const QString& path);
    QString exportToOpenSshConfig(const QString& path) const;
    QString importFromOpenSshConfig(const QString& path); // ~/.ssh/config

    // Backup / restore -------------------------------------------------------
    QString backupToFile(const QString& path) const;     // profiles+snippets+settings (no secrets)
    QString restoreFromFile(const QString& path);

    QStringList groups() const;
    QStringList allTags() const;
    int count() const;

    // QML-facing accessors. ProfileStore is exposed to QML through
    // AppController::profiles, but QVector<ConnectionProfile> is not usable
    // from QML/JS, so these return plain QVariant containers. Without them
    // QML call sites (Dashboard, Files page, Diagnostics dialog) throw
    // "Property 'all' of object ProfileStore is not a function".
    Q_INVOKABLE QVariantList allProfiles() const;
    Q_INVOKABLE QVariantMap profileById(qint64 id) const;   // empty map when not found

signals:
    void profileAdded(qint64 id);
    void profileUpdated(qint64 id);
    void profileRemoved(qint64 id);
    void storeReset();

private:
    ConnectionProfile rowToProfile(QSqlQuery& q) const;
};

} // namespace eclipse
