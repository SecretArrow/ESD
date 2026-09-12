#pragma once

#include <QAbstractListModel>
#include <QDateTime>

#include "../ssh/Types.h"
#include "../ssh/ISshEngine.h"

namespace eclipse {

// Remote file system model backed by an ISftpSession. All directory reads run
// on QtConcurrent threads; results are posted back to the model. Supports
// chunked population (canFetchMore) for directories with thousands of files.
class RemoteFsModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(int entryCount READ entryCount NOTIFY pathChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1, SizeRole, SizeTextRole, IsDirRole, IsLinkRole,
        PermsRole, PermsTextRole, UidRole, GidRole, MtimeRole, MtimeTextRole, CanExecRole
    };

    explicit RemoteFsModel(QObject* parent = nullptr);

    // Bind to a session; uses a dedicated SFTP session created on demand.
    void setSession(qint64 sessionId, std::shared_ptr<ISftpSession> sftp);
    // QML entry point: takes the eclipse::SshSession object, creates the
    // SFTP session internally (std::shared_ptr cannot cross QML), then
    // forwards to setSession().
    Q_INVOKABLE void attachSession(QObject* session);
    bool hasSession() const { return bool(m_sftp); }

    QString path() const { return m_path; }
    void setPath(const QString& p);
    bool loading() const { return m_loading; }
    int entryCount() const { return m_entries.size(); }

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void goUp();
    Q_INVOKABLE void setShowHidden(bool on);
    Q_INVOKABLE void sortByName(bool ascending);
    Q_INVOKABLE QVariantMap entryAt(int row) const;
    Q_INVOKABLE QStringList selectedPaths(const QVariantList& rows) const;

    // Operations (all asynchronous, completion via operationFinished)
    Q_INVOKABLE void mkdir(const QString& name);
    Q_INVOKABLE void rename(int row, const QString& newName);
    Q_INVOKABLE void removeEntry(int row);
    Q_INVOKABLE void removeSelected(const QVariantList& rows);
    Q_INVOKABLE void setPermissions(int row, const QString& octal);
    Q_INVOKABLE void createSymlink(const QString& target, const QString& linkPath);
    Q_INVOKABLE void openArchive(int row);

signals:
    void pathChanged();
    void loadingChanged();
    void operationFinished(bool ok, const QString& error);
    void archiveRequested(qint64 sessionId, const QString& remotePath);

private:
    void loadPath(const QString& p);
    void applyEntries(const QVector<SftpEntry>& entries);

    std::shared_ptr<ISftpSession> m_sftp;
    qint64 m_sessionId = 0;
    QString m_path;
    QVector<SftpEntry> m_entries;
    QVector<SftpEntry> m_allEntries;
    bool m_loading = false;
    bool m_showHidden = true;
    bool m_sortAscending = true;
};

// Local file system model (dual panel, other side).
class LocalFsModel : public QAbstractListModel
{
    Q_OBJECT
    Q_PROPERTY(QString path READ path WRITE setPath NOTIFY pathChanged)
    Q_PROPERTY(bool loading READ loading NOTIFY loadingChanged)
    Q_PROPERTY(int entryCount READ entryCount NOTIFY pathChanged)

public:
    enum Roles {
        NameRole = Qt::UserRole + 1, SizeRole, SizeTextRole, IsDirRole, IsLinkRole,
        PermsRole, PermsTextRole, UidRole, GidRole, MtimeRole, MtimeTextRole, CanExecRole
    };

    explicit LocalFsModel(QObject* parent = nullptr);

    QString path() const { return m_path; }
    void setPath(const QString& p);
    bool loading() const { return false; }
    int entryCount() const { return m_entries.size(); }

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void refresh();
    Q_INVOKABLE void goUp();
    Q_INVOKABLE void setShowHidden(bool on);
    Q_INVOKABLE QVariantMap entryAt(int row) const;
    Q_INVOKABLE QStringList selectedPaths(const QVariantList& rows) const;

signals:
    void pathChanged();
    void loadingChanged();

private:
    void loadPath(const QString& p);

    QString m_path;
    QVector<SftpEntry> m_entries;
    QVector<SftpEntry> m_allEntries;
    bool m_showHidden = true;
};

} // namespace eclipse
