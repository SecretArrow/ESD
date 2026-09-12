#include "SnippetStore.h"

#include <QRegularExpression>
#include <QSqlError>
#include <QSqlQuery>

#include "../Database.h"
#include "../logging/Logger.h"

namespace eclipse {

SnippetStore& SnippetStore::instance()
{
    static SnippetStore s;
    return s;
}

bool SnippetStore::init()
{
    return Database::instance().open();
}

QVector<Snippet> SnippetStore::all() const
{
    QVector<Snippet> out;
    QSqlQuery q(Database::instance().handle());
    q.exec(QStringLiteral(
        "SELECT id,group_name,title,command,sort_order FROM snippets "
        "ORDER BY group_name COLLATE NOCASE, sort_order, title COLLATE NOCASE"));
    while (q.next()) {
        Snippet s;
        s.id = q.value(0).toLongLong();
        s.group = q.value(1).toString();
        s.title = q.value(2).toString();
        s.command = q.value(3).toString();
        s.sortOrder = q.value(4).toInt();
        out.append(s);
    }
    return out;
}

qint64 SnippetStore::add(const Snippet& s)
{
    QSqlQuery q(Database::instance().handle());
    q.prepare(QStringLiteral(
        "INSERT INTO snippets(group_name,title,command,sort_order) VALUES(?,?,?,?)"));
    q.addBindValue(s.group);
    q.addBindValue(s.title);
    q.addBindValue(s.command);
    q.addBindValue(s.sortOrder);
    if (!q.exec()) {
        LOG_ERROR(QStringLiteral("snippet add failed: %1").arg(q.lastError().text()));
        return 0;
    }
    emit changed();
    return q.lastInsertId().toLongLong();
}

bool SnippetStore::update(const Snippet& s)
{
    QSqlQuery q(Database::instance().handle());
    q.prepare(QStringLiteral(
        "UPDATE snippets SET group_name=?,title=?,command=?,sort_order=? WHERE id=?"));
    q.addBindValue(s.group);
    q.addBindValue(s.title);
    q.addBindValue(s.command);
    q.addBindValue(s.sortOrder);
    q.addBindValue(s.id);
    const bool ok = q.exec();
    if (ok)
        emit changed();
    return ok;
}

bool SnippetStore::remove(qint64 id)
{
    QSqlQuery q(Database::instance().handle());
    q.prepare(QStringLiteral("DELETE FROM snippets WHERE id=?"));
    q.addBindValue(id);
    const bool ok = q.exec();
    if (ok)
        emit changed();
    return ok;
}

QString SnippetStore::expand(const QString& command, const QVariantMap& vars)
{
    static const QRegularExpression varRe(QStringLiteral("\\{([a-zA-Z_][a-zA-Z0-9_]*)\\}"));
    QString out = command;
    QRegularExpressionMatchIterator it = varRe.globalMatch(command);
    while (it.hasNext()) {
        const auto m = it.next();
        const QString name = m.captured(1);
        if (vars.contains(name))
            out.replace(m.captured(0), vars.value(name).toString());
    }
    return out;
}

// ---------------------------------------------------------------------------
// SnippetModel
// ---------------------------------------------------------------------------
SnippetModel::SnippetModel(QObject* parent)
    : QAbstractListModel(parent)
{
    reload();
}

int SnippetModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_snippets.size();
}

QHash<int, QByteArray> SnippetModel::roleNames() const
{
    return { { IdRole, "id" }, { GroupRole, "group" }, { TitleRole, "title" }, { CommandRole, "command" } };
}

QVariant SnippetModel::data(const QModelIndex& index, int role) const
{
    const int row = index.row();
    if (row < 0 || row >= m_snippets.size())
        return {};
    const Snippet& s = m_snippets[row];
    switch (role) {
    case IdRole: return s.id;
    case GroupRole: return s.group;
    case TitleRole: return s.title;
    case CommandRole: return s.command;
    }
    return {};
}

void SnippetModel::reload()
{
    beginResetModel();
    m_snippets = SnippetStore::instance().all();
    endResetModel();
}

void SnippetModel::addSnippet(const QString& group, const QString& title, const QString& command)
{
    Snippet s;
    s.group = group;
    s.title = title;
    s.command = command;
    SnippetStore::instance().add(s);
    reload();
}

void SnippetModel::updateSnippet(qint64 id, const QString& group, const QString& title,
                                 const QString& command)
{
    for (const auto& s : std::as_const(m_snippets)) {
        if (s.id == id) {
            Snippet u = s;
            u.group = group;
            u.title = title;
            u.command = command;
            SnippetStore::instance().update(u);
            break;
        }
    }
    reload();
}

void SnippetModel::removeSnippet(qint64 id)
{
    SnippetStore::instance().remove(id);
    reload();
}

QString SnippetModel::expandCommand(const QString& command, const QVariantMap& vars) const
{
    return SnippetStore::expand(command, vars);
}

QStringList SnippetModel::variablesIn(const QString& command) const
{
    static const QRegularExpression varRe(QStringLiteral("\\{([a-zA-Z_][a-zA-Z0-9_]*)\\}"));
    QSet<QString> names;
    QRegularExpressionMatchIterator it = varRe.globalMatch(command);
    while (it.hasNext())
        names.insert(it.next().captured(1));
    QStringList out(names.cbegin(), names.cend());
    out.sort();
    return out;
}

} // namespace eclipse
