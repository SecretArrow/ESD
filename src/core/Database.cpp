#include "Database.h"

#include <QDir>
#include <QSqlError>
#include <QSqlQuery>

#include "../common/Utils.h"

namespace eclipse {

Database& Database::instance()
{
    static Database db;
    return db;
}

QSqlDatabase Database::handle() const
{
    return m_db;
}

bool Database::open()
{
    if (m_db.isValid() && m_db.isOpen())
        return true;

    const QString dir = utils::dataDirectory();
    QDir().mkpath(dir);
    const QString path = dir + QStringLiteral("/eclipse.db");

    m_db = QSqlDatabase::addDatabase(QStringLiteral("QSQLITE"), QStringLiteral("eclipse-main"));
    m_db.setDatabaseName(path);
    if (!m_db.open()) {
        m_lastError = m_db.lastError().text();
        return false;
    }
    QSqlQuery q(m_db);
    q.exec(QStringLiteral("PRAGMA journal_mode=WAL"));
    q.exec(QStringLiteral("PRAGMA foreign_keys=ON"));
    return migrate();
}

void Database::close()
{
    if (m_db.isOpen())
        m_db.close();
}

bool Database::migrate()
{
    QSqlQuery q(m_db);

    const bool ok = q.exec(R"SQL(
        CREATE TABLE IF NOT EXISTS schema_version (
            version INTEGER NOT NULL
        )
    )SQL")
                  && q.exec(R"SQL(
        CREATE TABLE IF NOT EXISTS profiles (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT NOT NULL,
            group_name TEXT DEFAULT '',
            tags TEXT DEFAULT '[]',
            favorite INTEGER DEFAULT 0,
            host TEXT NOT NULL,
            port INTEGER DEFAULT 22,
            username TEXT NOT NULL,
            auth_method TEXT DEFAULT 'password',
            engine TEXT DEFAULT 'auto',
            settings_json TEXT DEFAULT '{}',
            created_ms INTEGER,
            last_used_ms INTEGER DEFAULT 0
        )
    )SQL")
                  && q.exec(R"SQL(
        CREATE TABLE IF NOT EXISTS snippets (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            group_name TEXT DEFAULT '',
            title TEXT NOT NULL,
            command TEXT NOT NULL,
            sort_order INTEGER DEFAULT 0
        )
    )SQL")
                  && q.exec(R"SQL(
        CREATE TABLE IF NOT EXISTS saved_commands (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            title TEXT NOT NULL,
            command TEXT NOT NULL,
            last_used_ms INTEGER DEFAULT 0
        )
    )SQL")
                  && q.exec(R"SQL(
        CREATE TABLE IF NOT EXISTS command_history (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            command TEXT NOT NULL,
            host TEXT,
            used_ms INTEGER
        )
    )SQL")
                  && q.exec(R"SQL(
        CREATE TABLE IF NOT EXISTS transfer_history (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            direction TEXT,
            local_path TEXT,
            remote_path TEXT,
            host TEXT,
            size_bytes INTEGER,
            duration_ms INTEGER,
            status TEXT,
            finished_ms INTEGER
        )
    )SQL")
                  && q.exec(R"SQL(
        CREATE TABLE IF NOT EXISTS workspaces (
            id INTEGER PRIMARY KEY AUTOINCREMENT,
            name TEXT UNIQUE NOT NULL,
            layout_json TEXT NOT NULL
        )
    )SQL")
                  && q.exec(R"SQL(
        CREATE INDEX IF NOT EXISTS idx_profiles_group ON profiles(group_name)
    )SQL")
                  && q.exec(R"SQL(
        CREATE INDEX IF NOT EXISTS idx_history_used ON command_history(used_ms DESC)
    )SQL");

    if (!ok) {
        m_lastError = q.lastError().text();
        return false;
    }

    QSqlQuery vq(m_db);
    vq.exec(QStringLiteral("SELECT version FROM schema_version"));
    if (!vq.next()) {
        QSqlQuery iq(m_db);
        iq.prepare(QStringLiteral("INSERT INTO schema_version(version) VALUES(1)"));
        iq.exec();
    }
    return true;
}

} // namespace eclipse
