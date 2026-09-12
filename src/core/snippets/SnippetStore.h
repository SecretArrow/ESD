#pragma once

#include <QAbstractListModel>

namespace eclipse {

struct Snippet
{
    qint64 id = 0;
    QString group;
    QString title;
    QString command;
    int sortOrder = 0;
};

// SQLite-backed command snippet store with groups and {variable} templates.
class SnippetStore : public QObject
{
    Q_OBJECT
public:
    static SnippetStore& instance();

    bool init();
    QVector<Snippet> all() const;
    qint64 add(const Snippet& s);
    bool update(const Snippet& s);
    bool remove(qint64 id);

    // Substitute {var} tokens using the map; unknown vars left intact.
    static QString expand(const QString& command, const QVariantMap& vars);

signals:
    void changed();

private:
    SnippetStore() = default;
};

class SnippetModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles { IdRole = Qt::UserRole + 1, GroupRole, TitleRole, CommandRole };

    explicit SnippetModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void reload();
    Q_INVOKABLE void addSnippet(const QString& group, const QString& title, const QString& command);
    Q_INVOKABLE void updateSnippet(qint64 id, const QString& group, const QString& title,
                                   const QString& command);
    Q_INVOKABLE void removeSnippet(qint64 id);
    Q_INVOKABLE QString expandCommand(const QString& command, const QVariantMap& vars) const;
    Q_INVOKABLE QStringList variablesIn(const QString& command) const;

private:
    QVector<Snippet> m_snippets;
};

} // namespace eclipse
