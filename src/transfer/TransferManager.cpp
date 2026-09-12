#include "TransferManager.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QFile>
#include <QFileInfo>
#include <QSaveFile>
#include <QSqlQuery>
#include <QtConcurrent>

#include "../common/Utils.h"
#include "../core/Database.h"
#include "../core/logging/Logger.h"
#include "../core/settings/Settings.h"

namespace eclipse {

// ---------------------------------------------------------------------------
// TransferJob
// ---------------------------------------------------------------------------
TransferJob::TransferJob(const TransferSpec& spec, QObject* parent)
    : QObject(parent)
    , m_spec(spec)
{
}

QString TransferJob::displayTitle() const
{
    const QFileInfo fi(m_spec.direction == TransferSpec::Upload ? m_spec.localPath : m_spec.remotePath);
    return (m_spec.direction == TransferSpec::Upload ? QStringLiteral("Uploading ")
                                                     : QStringLiteral("Downloading "))
           + fi.fileName();
}

bool TransferJob::run(SshSession* session)
{
    m_state = State::Running;
    emit jobStateChanged(int(m_state), {});
    m_speedTimer.start();

    QString err;
    bool ok = false;
    if (m_spec.protocol == TransferSpec::Sftp)
        ok = runSftp(session, &err);
    else
        ok = runScp(session, &err);

    if (m_cancelRequested) {
        m_state = State::Canceled;
        m_error = QStringLiteral("Canceled by user.");
    } else if (ok) {
        m_state = State::Completed;
    } else {
        m_state = State::Failed;
        m_error = err.isEmpty() ? QStringLiteral("Transfer failed.") : err;
    }
    emit jobStateChanged(int(m_state), m_error);
    return ok;
}

static bool walkDir(const QString& dirPath, QStringList* filesOut)
{
    QDirIterator it(dirPath, QDir::Files | QDir::Dirs | QDir::NoDotAndDotDot | QDir::Hidden);
    while (it.hasNext()) {
        const QString p = it.next();
        const QFileInfo fi(p);
        if (fi.isDir()) {
            if (!walkDir(p, filesOut))
                return false;
        } else {
            filesOut->append(p);
        }
    }
    filesOut->append(dirPath); // for dir creation pass
    return true;
}

bool TransferJob::runSftp(SshSession* session, QString* err)
{
    QString serr;
    std::shared_ptr<ISftpSession> sftp = session->createSftpSession(&serr);
    if (!sftp) {
        *err = serr;
        return false;
    }

    // ---- Upload ----------------------------------------------------------
    if (m_spec.direction == TransferSpec::Upload) {
        const QFileInfo fi(m_spec.localPath);
        if (!fi.exists()) {
            *err = QStringLiteral("Local file vanished: %1").arg(m_spec.localPath);
            return false;
        }
        // Local recursive: build list
        QVector<QPair<QString, QString>> pairs; // (local, remote)
        if (fi.isDir()) {
            if (!m_spec.recursive) {
                *err = QStringLiteral("Source is a directory - enable recursive transfer.");
                return false;
            }
            const QString baseName = fi.fileName();
            const QString remoteBase = m_spec.remotePath;
            QStringList all;
            walkDir(m_spec.localPath, &all);
            quint64 totalBytes = 0;
            for (const QString& p : all) {
                const QFileInfo pf(p);
                if (pf.isFile())
                    totalBytes += quint64(pf.size());
            }
            m_total = totalBytes;
            for (const QString& p : all) {
                const QFileInfo pf(p);
                QString rel = QDir(m_spec.localPath).relativeFilePath(p);
                pairs.append({ p, remoteBase + QLatin1Char('/') + baseName + QLatin1Char('/') + rel });
            }
        } else {
            m_total = quint64(fi.size());
            pairs.append({ m_spec.localPath, m_spec.remotePath });
        }

        for (const auto& pair : pairs) {
            if (m_cancelRequested)
                return false;
            while (m_pauseRequested && !m_cancelRequested)
                QThread::msleep(80);
            const QFileInfo pf(pair.first);
            if (pf.isDir()) {
                sftp->mkdir(pair.second, 0755, nullptr); // best effort
                continue;
            }
            QFile in(pair.first);
            if (!in.open(QIODevice::ReadOnly)) {
                *err = QStringLiteral("Cannot read %1").arg(pair.first);
                return false;
            }
            quint64 size = quint64(pf.size());
            auto h = sftp->openForWrite(pair.second, size, false, 0644, err);
            if (!h)
                return false;
            QByteArray chunk;
            chunk.resize(64 * 1024);
            quint64 sent = m_transferred;
            while (sent < size) {
                if (m_cancelRequested) {
                    sftp->closeFile(h);
                    return false;
                }
                while (m_pauseRequested && !m_cancelRequested)
                    QThread::msleep(80);
                const qint64 n = in.read(chunk.data(), chunk.size());
                if (n <= 0)
                    break;
                const int w = sftp->writeFile(h, chunk.constData(), int(n), err);
                if (w != n) {
                    sftp->closeFile(h);
                    return false;
                }
                sent += quint64(n);
                m_transferred = sent;
                const double el = double(m_speedTimer.elapsed()) / 1000.0;
                m_speedBps = el > 0 ? double(m_transferred) / el : 0;
                emit jobProgress(m_transferred, m_total, m_speedBps);
            }
            sftp->closeFile(h);
        }
        return true;
    }

    // ---- Download --------------------------------------------------------
    quint64 size = 0;
    auto h = sftp->openForRead(m_spec.remotePath, &size, err);
    if (!h)
        return false;
    m_total = size;

    QSaveFile out(m_spec.localPath);
    if (!out.open(QIODevice::WriteOnly)) {
        sftp->closeFile(h);
        *err = QStringLiteral("Cannot write %1: %2").arg(m_spec.localPath, out.errorString());
        return false;
    }
    QByteArray chunk;
    chunk.resize(64 * 1024);
    for (;;) {
        if (m_cancelRequested) {
            sftp->closeFile(h);
            out.cancelWriting();
            return false;
        }
        while (m_pauseRequested && !m_cancelRequested)
            QThread::msleep(80);
        const int n = sftp->readFile(h, chunk.data(), chunk.size(), err);
        if (n < 0) {
            sftp->closeFile(h);
            out.cancelWriting();
            return false;
        }
        if (n == 0)
            break;
        out.write(chunk.constData(), n);
        m_transferred += quint64(n);
        const double el = double(m_speedTimer.elapsed()) / 1000.0;
        m_speedBps = el > 0 ? double(m_transferred) / el : 0;
        emit jobProgress(m_transferred, m_total, m_speedBps);
    }
    sftp->closeFile(h);
    if (!out.commit()) {
        *err = QStringLiteral("Cannot finalize %1: %2").arg(m_spec.localPath, out.errorString());
        return false;
    }
    return true;
}

bool TransferJob::runScp(SshSession* session, QString* err)
{
    Q_UNUSED(err);
    QString serr;
    // SCP is executed through the worker thread's engine via a dedicated
    // engine call; here we use SFTP-wrapped fallback with progress since the
    // engine mutex serializes SCP transfers anyway. The profile's protocol
    // choice is honored by libssh-based sessions through LibsshScp.
    std::shared_ptr<ISftpSession> sftp = session->createSftpSession(&serr);
    if (!sftp) {
        m_error = serr;
        return false;
    }
    // Simple single-file path with SFTP underneath (progress identical).
    TransferSpec spec = m_spec;
    spec.protocol = TransferSpec::Sftp;
    TransferJob jobCopy(spec);
    connect(&jobCopy, &TransferJob::jobProgress, this, [&](quint64 t, quint64 total, double s) {
        emit jobProgress(t, total, s);
    });
    return jobCopy.run(session);
}

// ---------------------------------------------------------------------------
// TransferManager
// ---------------------------------------------------------------------------
TransferManager& TransferManager::instance()
{
    static TransferManager m;
    return m;
}

void TransferManager::enqueue(const QVariantMap& spec)
{
    TransferSpec s;
    s.direction = spec.value(QStringLiteral("direction")).toString() == QLatin1String("upload")
                      ? TransferSpec::Upload
                      : TransferSpec::Download;
    s.sessionId = spec.value(QStringLiteral("sessionId")).toLongLong();
    s.localPath = spec.value(QStringLiteral("localPath")).toString();
    s.remotePath = spec.value(QStringLiteral("remotePath")).toString();
    s.recursive = spec.value(QStringLiteral("recursive")).toBool();
    enqueueSpec(s, nullptr);
}

void TransferManager::enqueueSpec(const TransferSpec& spec, SshSession* session)
{
    if (session)
        m_sessions.insert(spec.sessionId, session);
    auto* job = new TransferJob(spec, this);
    m_jobs.append(job);
    emit listChanged();
    startNext();
}

void TransferManager::startNext()
{
    const int maxParallel = qMax(1, Settings::instance().maxParallelTransfers());
    while (m_running < maxParallel) {
        TransferJob* next = nullptr;
        for (TransferJob* j : m_jobs)
            if (j->state() == TransferJob::State::Queued) {
                next = j;
                break;
            }
        if (!next)
            return;
        launchJob(next);
    }
}

void TransferManager::launchJob(TransferJob* job)
{
    SshSession* session = m_sessions.value(job->spec().sessionId);
    if (!session) {
        job->setState(TransferJob::State::Failed);
        emit listChanged();
        return;
    }
    ++m_running;
    const qint64 sessionId = job->spec().sessionId;
    const qint64 row0 = qint64(m_jobs.indexOf(job));
    QPointer<TransferJob> guard(job);
    QPointer<TransferManager> self(this);

    QtConcurrent::run([job, session]() {
        job->run(session);
    }).then(this, [this, job, guard, sessionId, row0]() {
        if (!guard)
            return;
        --m_running;
        const bool ok = job->state() == TransferJob::State::Completed;
        LOG_XFER(QStringLiteral("Transfer %1: %2 (%3 of %4)")
                     .arg(ok ? "completed" : "failed", job->displayTitle(),
                          utils::humanSize(job->transferred()), utils::humanSize(job->total())));
        // history row
        QSqlQuery q(Database::instance().handle());
        q.prepare(QStringLiteral("INSERT INTO transfer_history(direction,local_path,remote_path,size_bytes,"
                                 "duration_ms,status,finished_ms) VALUES(?,?,?,?,?,?,?)"));
        q.addBindValue(job->spec().direction == TransferSpec::Upload ? QStringLiteral("upload")
                                                                     : QStringLiteral("download"));
        q.addBindValue(job->spec().localPath);
        q.addBindValue(job->spec().remotePath);
        q.addBindValue(qint64(job->total()));
        q.addBindValue(0);
        q.addBindValue(ok ? QStringLiteral("completed") : QStringLiteral("failed"));
        q.addBindValue(QDateTime::currentMSecsSinceEpoch());
        q.exec();
        emit transferFinished(ok, job->displayTitle());
        startNext();
        emit listChanged();
    });
    Q_UNUSED(row0);
    emit jobUpdated(m_jobs.indexOf(job));
}

void TransferManager::pause(int row)
{
    if (row >= 0 && row < m_jobs.size())
        m_jobs[row]->pause();
}

void TransferManager::resume(int row)
{
    if (row >= 0 && row < m_jobs.size())
        m_jobs[row]->resume();
}

void TransferManager::cancel(int row)
{
    if (row >= 0 && row < m_jobs.size())
        m_jobs[row]->cancel();
}

void TransferManager::retry(int row)
{
    if (row < 0 || row >= m_jobs.size())
        return;
    TransferJob* job = m_jobs[row];
    if (job->state() != TransferJob::State::Failed && job->state() != TransferJob::State::Canceled)
        return;
    auto* fresh = new TransferJob(job->spec(), this);
    m_jobs[row] = fresh;
    job->deleteLater();
    emit listChanged();
    startNext();
}

void TransferManager::remove(int row)
{
    if (row < 0 || row >= m_jobs.size())
        return;
    m_jobs[row]->cancel();
    m_jobs[row]->deleteLater();
    m_jobs.remove(row);
    emit listChanged();
}

void TransferManager::clearFinished()
{
    m_jobs.erase(std::remove_if(m_jobs.begin(), m_jobs.end(),
                                [](TransferJob* j) {
                                    if (j->state() == TransferJob::State::Completed
                                        || j->state() == TransferJob::State::Failed
                                        || j->state() == TransferJob::State::Canceled) {
                                        j->deleteLater();
                                        return true;
                                    }
                                    return false;
                                }),
                 m_jobs.end());
    emit listChanged();
}

// ---------------------------------------------------------------------------
// TransferModel
// ---------------------------------------------------------------------------
TransferModel::TransferModel(QObject* parent)
    : QAbstractListModel(parent)
{
    refresh();
    connect(&TransferManager::instance(), &TransferManager::listChanged, this, &TransferModel::refresh);
}

int TransferModel::rowCount(const QModelIndex& parent) const
{
    return parent.isValid() ? 0 : m_jobs.size();
}

QHash<int, QByteArray> TransferModel::roleNames() const
{
    return {
        { TitleRole, "title" },
        { DirectionRole, "direction" },
        { StateRole, "state" },
        { StateNameRole, "stateName" },
        { ProgressRole, "progress" },
        { TransferredRole, "transferred" },
        { TotalRole, "total" },
        { SpeedRole, "speed" },
        { EtaRole, "eta" },
        { ErrorRole, "error" },
    };
}

QVariant TransferModel::data(const QModelIndex& index, int role) const
{
    const int row = index.row();
    if (row < 0 || row >= m_jobs.size())
        return {};
    const TransferJob* j = m_jobs[row];
    switch (role) {
    case TitleRole: return j->displayTitle();
    case DirectionRole:
        return j->spec().direction == TransferSpec::Upload ? QStringLiteral("upload")
                                                           : QStringLiteral("download");
    case StateRole: return int(j->state());
    case StateNameRole:
        switch (j->state()) {
        case TransferJob::State::Queued: return QStringLiteral("Queued");
        case TransferJob::State::Running: return QStringLiteral("Running");
        case TransferJob::State::Paused: return QStringLiteral("Paused");
        case TransferJob::State::Completed: return QStringLiteral("Completed");
        case TransferJob::State::Failed: return QStringLiteral("Failed");
        case TransferJob::State::Canceled: return QStringLiteral("Canceled");
        }
        return {};
    case ProgressRole:
        return j->total() > 0 ? double(j->transferred()) / double(j->total()) : 0.0;
    case TransferredRole: return utils::humanSize(j->transferred());
    case TotalRole: return utils::humanSize(j->total());
    case SpeedRole: return utils::humanSpeed(j->speedBps());
    case EtaRole:
        return utils::etaText(j->total() > j->transferred() ? j->total() - j->transferred() : 0,
                              j->speedBps());
    case ErrorRole: return j->errorText();
    }
    return {};
}

void TransferModel::refresh()
{
    beginResetModel();
    m_jobs = TransferManager::instance().jobs();
    endResetModel();
}

} // namespace eclipse
