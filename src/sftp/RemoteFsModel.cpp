#include "RemoteFsModel.h"

#include <QDir>
#include <QDirIterator>
#include <QFileInfo>
#include <QtConcurrent>

#include "../common/Utils.h"
#include "../core/logging/Logger.h"

namespace eclipse {

namespace {

void applyEntry(const QFileInfo& fi, SftpEntry* e)
{
    e->name = fi.fileName();
    e->attrs.isDir = fi.isDir();
    e->attrs.isRegular = fi.isFile();
    e->attrs.isLink = fi.isSymLink();
    e->attrs.size = fi.isDir() ? 0 : quint64(fi.size());
    e->attrs.mtime = fi.lastModified().toSecsSinceEpoch();
    e->attrs.canRead = fi.isReadable();
    e->attrs.canWrite = fi.isWritable();
    e->attrs.canExec = fi.isExecutable();
    e->attrs.permissions = (e->attrs.canRead ? 4 : 0) | (e->attrs.canWrite ? 2 : 0)
                           | (e->attrs.canExec ? 1 : 0);
}

QString permsText(quint32 p)
{
    QString s;
    const quint32 mask[3] = { 4, 2, 1 };
    const char* who = "rwx";
    for (int shift = 6; shift >= 0; shift -= 3) {
        for (int i = 0; i < 3; ++i)
            s += (p & (mask[i] << shift)) ? QChar::fromLatin1(who[i]) : QChar::fromLatin1('-');
    }
    return s;
}

QVariantMap entryToMap(const SftpEntry& e, int row)
{
    return {
        { "row", row },
        { "name", e.name },
        { "isDir", e.attrs.isDir },
        { "isLink", e.attrs.isLink },
        { "size", qint64(e.attrs.size) },
        { "sizeText", utils::humanSize(e.attrs.size) },
        { "permsText", permsText(e.attrs.permissions) },
        { "perms", QString::number(e.attrs.permissions & 07777, 8) },
        { "uid", e.attrs.uid },
        { "gid", e.attrs.gid },
        { "mtime", e.attrs.mtime },
        { "mtimeText", QDateTime::fromSecsSinceEpoch(e.attrs.mtime).toString(QStringLiteral("yyyy-MM-dd hh:mm")) },
        { "canExec", e.attrs.canExec },
    };
}

} // namespace

// ---------------------------------------------------------------------------
// RemoteFsModel
// ---------------------------------------------------------------------------
RemoteFsModel::RemoteFsModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

void RemoteFsModel::setSession(qint64 sessionId, std::shared_ptr<ISftpSession> sftp)
{
    m_sessionId = sessionId;
    m_sftp = std::move(sftp);
    if (!m_path.isEmpty())
        refresh();
}

void RemoteFsModel::setPath(const QString& p)
{
    if (p == m_path)
        return;
    loadPath(p);
}

void RemoteFsModel::loadPath(const QString& p)
{
    if (!m_sftp)
        return;
    m_path = p;
    emit pathChanged();
    refresh();
}

void RemoteFsModel::applyEntries(const QVector<SftpEntry>& entries)
{
    m_allEntries = entries;
    if (!m_showHidden) {
        QVector<SftpEntry> visible;
        for (const auto& e : entries)
            if (!e.name.startsWith(QLatin1Char('.')))
                visible.append(e);
        m_allEntries = visible;
    }
    std::sort(m_allEntries.begin(), m_allEntries.end(),
              [asc = m_sortAscending](const SftpEntry& a, const SftpEntry& b) {
                  if (a.attrs.isDir != b.attrs.isDir)
                      return a.attrs.isDir;
                  return asc ? a.name.compare(b.name, Qt::CaseInsensitive) < 0
                             : a.name.compare(b.name, Qt::CaseInsensitive) > 0;
              });
    beginResetModel();
    m_entries = m_allEntries;
    endResetModel();
    m_loading = false;
    emit loadingChanged();
}

void RemoteFsModel::refresh()
{
    if (!m_sftp || m_path.isEmpty() || m_loading)
        return;
    m_loading = true;
    emit loadingChanged();

    QPointer<RemoteFsModel> self(this);
    std::shared_ptr<ISftpSession> sftp = m_sftp;
    const QString path = m_path;
    QtConcurrent::run([sftp, path]() {
        QString err;
        const QString real = sftp->canonicalize(path);
        const QVector<SftpEntry> entries = sftp->listDir(real, &err);
        return qMakePair(entries, err);
    }).then(this, [self](const QPair<QVector<SftpEntry>, QString>& result) {
        if (!self)
            return;
        if (!result.second.isEmpty())
            LOG_SFTP_ERR(QStringLiteral("listDir %1: %2").arg(self->m_path, result.second));
        self->applyEntries(result.first);
    });
}

void RemoteFsModel::goUp()
{
    if (m_path == QLatin1String("/"))
        return;
    const int slash = m_path.lastIndexOf(QLatin1Char('/'));
    setPath(slash <= 0 ? QStringLiteral("/") : m_path.left(slash));
}

void RemoteFsModel::setShowHidden(bool on)
{
    if (m_showHidden == on)
        return;
    m_showHidden = on;
    applyEntries(m_allEntries);
}

void RemoteFsModel::sortByName(bool ascending)
{
    m_sortAscending = ascending;
    applyEntries(m_allEntries);
}

int RemoteFsModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QHash<int, QByteArray> RemoteFsModel::roleNames() const
{
    return {
        { NameRole, "name" },
        { SizeRole, "size" },
        { SizeTextRole, "sizeText" },
        { IsDirRole, "isDir" },
        { IsLinkRole, "isLink" },
        { PermsRole, "perms" },
        { PermsTextRole, "permsText" },
        { UidRole, "uid" },
        { GidRole, "gid" },
        { MtimeRole, "mtime" },
        { MtimeTextRole, "mtimeText" },
        { CanExecRole, "canExec" },
    };
}

QVariant RemoteFsModel::data(const QModelIndex& index, int role) const
{
    const int row = index.row();
    if (row < 0 || row >= m_entries.size())
        return {};
    const SftpEntry& e = m_entries[row];
    switch (role) {
    case NameRole: return e.name;
    case SizeRole: return qint64(e.attrs.size);
    case SizeTextRole: return e.attrs.isDir ? QStringLiteral("folder") : utils::humanSize(e.attrs.size);
    case IsDirRole: return e.attrs.isDir;
    case IsLinkRole: return e.attrs.isLink;
    case PermsRole: return QString::number(e.attrs.permissions & 07777, 8);
    case PermsTextRole: return permsText(e.attrs.permissions);
    case UidRole: return e.attrs.uid;
    case GidRole: return e.attrs.gid;
    case MtimeRole: return e.attrs.mtime;
    case MtimeTextRole:
        return QDateTime::fromSecsSinceEpoch(e.attrs.mtime).toString(QStringLiteral("yyyy-MM-dd hh:mm"));
    case CanExecRole: return e.attrs.canExec;
    }
    return {};
}

QVariantMap RemoteFsModel::entryAt(int row) const
{
    if (row < 0 || row >= m_entries.size())
        return {};
    return entryToMap(m_entries[row], row);
}

QStringList RemoteFsModel::selectedPaths(const QVariantList& rows) const
{
    QStringList out;
    for (const auto& r : rows) {
        const int row = r.toInt();
        if (row >= 0 && row < m_entries.size())
            out.append(m_path + (m_path.endsWith(QLatin1Char('/')) ? QString() : QStringLiteral("/"))
                       + m_entries[row].name);
    }
    return out;
}

void RemoteFsModel::mkdir(const QString& name)
{
    if (!m_sftp || name.isEmpty())
        return;
    QPointer<RemoteFsModel> self(this);
    std::shared_ptr<ISftpSession> sftp = m_sftp;
    const QString path = m_path + (m_path.endsWith(QLatin1Char('/')) ? QString() : QStringLiteral("/")) + name;
    QtConcurrent::run([sftp, path]() {
        QString err;
        const bool ok = sftp->mkdir(path, 0755, &err);
        return qMakePair(ok, err);
    }).then(this, [self](const QPair<bool, QString>& r) {
        if (!self)
            return;
        emit self->operationFinished(r.first, r.second);
        if (r.first)
            self->refresh();
    });
}

void RemoteFsModel::rename(int row, const QString& newName)
{
    if (!m_sftp || row < 0 || row >= m_entries.size() || newName.isEmpty())
        return;
    const QString from = m_path + (m_path.endsWith(QLatin1Char('/')) ? QString() : QStringLiteral("/"))
                         + m_entries[row].name;
    const QString to = m_path + (m_path.endsWith(QLatin1Char('/')) ? QString() : QStringLiteral("/")) + newName;
    QPointer<RemoteFsModel> self(this);
    std::shared_ptr<ISftpSession> sftp = m_sftp;
    QtConcurrent::run([sftp, from, to]() {
        QString err;
        const bool ok = sftp->rename(from, to, &err);
        return qMakePair(ok, err);
    }).then(this, [self](const QPair<bool, QString>& r) {
        if (!self)
            return;
        emit self->operationFinished(r.first, r.second);
        if (r.first)
            self->refresh();
    });
}

void RemoteFsModel::removeEntry(int row)
{
    removeSelected(QVariantList{ row });
}

void RemoteFsModel::removeSelected(const QVariantList& rows)
{
    if (!m_sftp)
        return;
    const QStringList paths = selectedPaths(rows);
    if (paths.isEmpty())
        return;
    QPointer<RemoteFsModel> self(this);
    std::shared_ptr<ISftpSession> sftp = m_sftp;
    QtConcurrent::run([sftp, paths]() {
        QString firstErr;
        for (const QString& p : paths) {
            SftpAttrs a;
            QString err;
            if (sftp->stat(p, &a, false) && a.isDir) {
                if (!sftp->rmdir(p, &err) && firstErr.isEmpty())
                    firstErr = err;
            } else if (!sftp->unlink(p, &err) && firstErr.isEmpty()) {
                firstErr = err;
            }
        }
        return firstErr;
    }).then(this, [self](const QString& err) {
        if (!self)
            return;
        emit self->operationFinished(err.isEmpty(), err);
        self->refresh();
    });
}

void RemoteFsModel::setPermissions(int row, const QString& octal)
{
    if (!m_sftp || row < 0 || row >= m_entries.size())
        return;
    bool okParse = false;
    const quint32 mode = quint32(octal.toUInt(&okParse, 8));
    if (!okParse)
        return;
    const QString path = m_path + (m_path.endsWith(QLatin1Char('/')) ? QString() : QStringLiteral("/"))
                         + m_entries[row].name;
    QPointer<RemoteFsModel> self(this);
    std::shared_ptr<ISftpSession> sftp = m_sftp;
    QtConcurrent::run([sftp, path, mode]() {
        QString err;
        const bool ok = sftp->setPermissions(path, mode, &err);
        return qMakePair(ok, err);
    }).then(this, [self](const QPair<bool, QString>& r) {
        if (!self)
            return;
        emit self->operationFinished(r.first, r.second);
        if (r.first)
            self->refresh();
    });
}

void RemoteFsModel::createSymlink(const QString& target, const QString& linkPath)
{
    if (!m_sftp)
        return;
    QPointer<RemoteFsModel> self(this);
    std::shared_ptr<ISftpSession> sftp = m_sftp;
    QtConcurrent::run([sftp, target, linkPath]() {
        QString err;
        const bool ok = sftp->createSymlink(target, linkPath, &err);
        return qMakePair(ok, err);
    }).then(this, [self](const QPair<bool, QString>& r) {
        if (!self)
            return;
        emit self->operationFinished(r.first, r.second);
        if (r.first)
            self->refresh();
    });
}

void RemoteFsModel::openArchive(int row)
{
    if (row < 0 || row >= m_entries.size())
        return;
    const QString p = m_path + (m_path.endsWith(QLatin1Char('/')) ? QString() : QStringLiteral("/"))
                      + m_entries[row].name;
    emit archiveRequested(m_sessionId, p);
}

// ---------------------------------------------------------------------------
// LocalFsModel
// ---------------------------------------------------------------------------
LocalFsModel::LocalFsModel(QObject* parent)
    : QAbstractListModel(parent)
{
    setPath(QDir::homePath());
}

void LocalFsModel::setPath(const QString& p)
{
    if (p == m_path)
        return;
    loadPath(p);
}

void LocalFsModel::loadPath(const QString& p)
{
    m_path = p;
    emit pathChanged();
    refresh();
}

void LocalFsModel::refresh()
{
    QVector<SftpEntry> entries;
    QDir dir(m_path);
    const auto list = dir.entryInfoList(QDir::AllEntries | QDir::Hidden | QDir::NoDotAndDotDot,
                                        QDir::DirsFirst | QDir::Name);
    for (const QFileInfo& fi : list) {
        if (!m_showHidden && fi.fileName().startsWith(QLatin1Char('.')))
            continue;
        SftpEntry e;
        applyEntry(fi, &e);
        entries.append(e);
    }
    beginResetModel();
    m_entries = entries;
    endResetModel();
}

void LocalFsModel::goUp()
{
    QDir d(m_path);
    if (d.cdUp())
        setPath(d.absolutePath());
}

void LocalFsModel::setShowHidden(bool on)
{
    m_showHidden = on;
    refresh();
}

int LocalFsModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_entries.size();
}

QHash<int, QByteArray> LocalFsModel::roleNames() const
{
    return {
        { NameRole, "name" },
        { SizeRole, "size" },
        { SizeTextRole, "sizeText" },
        { IsDirRole, "isDir" },
        { IsLinkRole, "isLink" },
        { PermsRole, "perms" },
        { PermsTextRole, "permsText" },
        { UidRole, "uid" },
        { GidRole, "gid" },
        { MtimeRole, "mtime" },
        { MtimeTextRole, "mtimeText" },
        { CanExecRole, "canExec" },
    };
}

QVariant LocalFsModel::data(const QModelIndex& index, int role) const
{
    const int row = index.row();
    if (row < 0 || row >= m_entries.size())
        return {};
    const SftpEntry& e = m_entries[row];
    switch (role) {
    case NameRole: return e.name;
    case SizeRole: return qint64(e.attrs.size);
    case SizeTextRole: return e.attrs.isDir ? QStringLiteral("folder") : utils::humanSize(e.attrs.size);
    case IsDirRole: return e.attrs.isDir;
    case IsLinkRole: return e.attrs.isLink;
    case PermsRole: return QString::number(e.attrs.permissions & 0777, 8);
    case PermsTextRole: return permsText(e.attrs.permissions);
    case UidRole: return 0;
    case GidRole: return 0;
    case MtimeRole: return e.attrs.mtime;
    case MtimeTextRole:
        return QDateTime::fromSecsSinceEpoch(e.attrs.mtime).toString(QStringLiteral("yyyy-MM-dd hh:mm"));
    case CanExecRole: return e.attrs.canExec;
    }
    return {};
}

QVariantMap LocalFsModel::entryAt(int row) const
{
    if (row < 0 || row >= m_entries.size())
        return {};
    return entryToMap(m_entries[row], row);
}

QStringList LocalFsModel::selectedPaths(const QVariantList& rows) const
{
    QStringList out;
    for (const auto& r : rows) {
        const int row = r.toInt();
        if (row >= 0 && row < m_entries.size())
            out.append(m_path + (m_path.endsWith(QLatin1Char('/')) ? QString() : QStringLiteral("/"))
                       + m_entries[row].name);
    }
    return out;
}

} // namespace eclipse
