#pragma once

#include <QAbstractListModel>
#include <QElapsedTimer>
#include <QHash>
#include <QObject>

#include "../core/profiles/ConnectionProfile.h"
#include "../ssh/ISshEngine.h"
#include "../ssh/SshSession.h"

namespace eclipse {

// ---------------------------------------------------------------------------
// TransferManager - download/upload queue with progress, pause/cancel/retry,
// speed + ETA, history. Jobs run on QtConcurrent threads using their own
// SFTP session (or SCP when the profile requests it), streaming in chunks -
// files of any size are never loaded fully into RAM.
// ---------------------------------------------------------------------------
struct TransferSpec
{
    enum Direction { Upload, Download } direction = Download;
    enum Protocol { Sftp, Scp } protocol = Sftp;

    qint64 sessionId = 0;          // owning session
    QString localPath;
    QString remotePath;
    bool recursive = false;
    quint64 overwriteSize = 0;     // resume hint
};

class TransferManager;

class TransferJob : public QObject
{
    Q_OBJECT
public:
    enum class State { Queued, Running, Paused, Completed, Failed, Canceled };

    TransferJob(const TransferSpec& spec, QObject* parent = nullptr);

    const TransferSpec& spec() const { return m_spec; }
    State state() const { return m_state; }
    quint64 transferred() const { return m_transferred; }
    quint64 total() const { return m_total; }
    double speedBps() const { return m_speedBps; }
    QString errorText() const { return m_error; }
    QString displayTitle() const;

    void pause() { m_pauseRequested = true; }
    void resume() { m_pauseRequested = false; }
    void cancel() { m_cancelRequested = true; }
    void setState(State s) { m_state = s; }

    bool run(SshSession* session);   // executes on a worker thread

signals:
    void jobProgress(quint64 transferred, quint64 total, double speedBps);
    void jobStateChanged(int newState, const QString& error);

private:
    bool runSftp(SshSession* session, QString* err);
    bool runScp(SshSession* session, QString* err);

    TransferSpec m_spec;
    State m_state = State::Queued;
    quint64 m_transferred = 0;
    quint64 m_total = 0;
    double m_speedBps = 0;
    QString m_error;
    QElapsedTimer m_speedTimer;
    std::atomic<bool> m_pauseRequested{ false };
    std::atomic<bool> m_cancelRequested{ false };
};

class TransferManager : public QObject
{
    Q_OBJECT
public:
    static TransferManager& instance();

    Q_INVOKABLE void enqueue(const QVariantMap& spec);
    void enqueueSpec(const TransferSpec& spec, SshSession* session);
    Q_INVOKABLE void pause(int row);
    Q_INVOKABLE void resume(int row);
    Q_INVOKABLE void cancel(int row);
    Q_INVOKABLE void retry(int row);
    Q_INVOKABLE void remove(int row);
    Q_INVOKABLE void clearFinished();

    Q_INVOKABLE int count() const { return m_jobs.size(); }
    QVector<TransferJob*> jobs() const { return m_jobs; }

signals:
    void listChanged();
    void jobUpdated(int row);
    void transferFinished(bool ok, const QString& title);

private:
    TransferManager() = default;
    void startNext();
    void launchJob(TransferJob* job);

    QVector<TransferJob*> m_jobs;
    QHash<qint64, SshSession*> m_sessions;   // sessionId -> session
    int m_running = 0;
};

class TransferModel : public QAbstractListModel
{
    Q_OBJECT
public:
    enum Roles {
        TitleRole = Qt::UserRole + 1, DirectionRole, StateRole, StateNameRole,
        ProgressRole, TransferredRole, TotalRole, SpeedRole, EtaRole, ErrorRole
    };

    explicit TransferModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    Q_INVOKABLE void refresh();

private:
    QVector<TransferJob*> m_jobs;
};

} // namespace eclipse
