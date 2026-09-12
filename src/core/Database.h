#pragma once

#include <QSqlDatabase>
#include <QString>

namespace eclipse {

// Central SQLite access: opens the single application database and runs
// schema migrations. Everything structured (profiles, snippets, transfer
// history, saved commands, workspaces) lives here; credentials never do.
class Database
{
public:
    static Database& instance();

    // Opens (creating if necessary) and migrates. Returns false on failure.
    bool open();
    void close();
    QSqlDatabase handle() const;
    QString lastError() const { return m_lastError; }

private:
    Database() = default;
    bool migrate();

    QSqlDatabase m_db;
    QString m_lastError;
};

} // namespace eclipse
