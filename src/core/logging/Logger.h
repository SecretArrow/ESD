#pragma once

#include <QAbstractListModel>
#include <QDateTime>
#include <QFile>
#include <QHash>
#include <QMutex>
#include <mutex>
#include <QObject>
#include <QStringList>
#include <deque>

namespace eclipse {

// ---------------------------------------------------------------------------
// Structured application logging.
//  - Categories mirror the product spec (Connection, SSH, SFTP, ...).
//  - NEVER log credentials: all messages pass through redact() before they
//    reach any sink.
//  - Sinks: console, rotating file, in-app LogModel (UI live viewer).
// ---------------------------------------------------------------------------

enum class LogLevel { Debug = 0, Info, Warning, Error };

enum class LogCategory {
    Connection,
    Ssh,
    Sftp,
    Transfer,
    Tunnel,
    Authentication,
    Application,
    Error,
    Debug
};

QString logCategoryName(LogCategory c);
LogCategory logCategoryFromName(const QString& name);

class LogModel;

class Logger : public QObject
{
    Q_OBJECT
public:
    static Logger& instance();

    void log(LogCategory cat, LogLevel level, const QString& message);
    void enableFileLogging(bool enabled);
    void enableDebugMode(bool enabled);
    bool debugMode() const { return m_debugMode; }

    // Scrubs password/key patterns out of a message.
    static QString redact(QString msg);

    LogModel* uiModel() { return m_uiModel; }
    QString exportLogs(const QString& path);
    void clearUiHistory();

signals:
    void entryAdded();

private:
    Logger();
    void writeToFile(LogCategory cat, LogLevel level, const QString& msg);
    void rotateIfNeeded();

    QFile m_file;
    std::recursive_mutex m_mutex;
    bool m_fileLogging = true;
    bool m_debugMode = false;
    LogModel* m_uiModel = nullptr;
};

class LogModel : public QAbstractListModel
{
    Q_OBJECT
public:
    struct Entry
    {
        qint64 tsMs = 0;
        LogCategory category = LogCategory::Application;
        LogLevel level = LogLevel::Info;
        QString message;
    };

    enum Roles { TsRole = Qt::UserRole + 1, CategoryRole, LevelRole, MessageRole, LevelNameRole, CategoryNameRole, TimeTextRole };

    explicit LogModel(QObject* parent = nullptr);

    int rowCount(const QModelIndex& parent = {}) const override;
    QVariant data(const QModelIndex& index, int role) const override;
    QHash<int, QByteArray> roleNames() const override;

    void append(const Entry& e);
    Q_INVOKABLE void clear();
    Q_INVOKABLE void setFilter(const QString& text, int minLevel, const QString& category);
    QVector<Entry> entriesForExport() const;

private:
    mutable QMutex m_mutex; // LogModel keeps its own mutex
    std::deque<Entry> m_entries;
    std::deque<Entry> m_ring; // raw ring buffer
    QVector<int> m_filtered;  // indices into ring matching filter
    QString m_filterText;
    int m_minLevel = 0;
    QString m_filterCategory;
    static constexpr size_t kMaxEntries = 8000;
    void rebuildFilter();
};

// Convenience macros
#define LOG_APP(msg)      ::eclipse::Logger::instance().log(::eclipse::LogCategory::Application, ::eclipse::LogLevel::Info, (msg))
#define LOG_WARN(msg)     ::eclipse::Logger::instance().log(::eclipse::LogCategory::Application, ::eclipse::LogLevel::Warning, (msg))
#define LOG_ERROR(msg)    ::eclipse::Logger::instance().log(::eclipse::LogCategory::Application, ::eclipse::LogLevel::Error, (msg))
#define LOG_DEBUG(msg) \
    do { \
        if (::eclipse::Logger::instance().debugMode()) \
            ::eclipse::Logger::instance().log(::eclipse::LogCategory::Debug, ::eclipse::LogLevel::Debug, (msg)); \
    } while (0)
#define LOG_SSH(msg)      ::eclipse::Logger::instance().log(::eclipse::LogCategory::Ssh, ::eclipse::LogLevel::Info, (msg))
#define LOG_SSH_ERR(msg)  ::eclipse::Logger::instance().log(::eclipse::LogCategory::Ssh, ::eclipse::LogLevel::Error, (msg))
#define LOG_CONN(msg)     ::eclipse::Logger::instance().log(::eclipse::LogCategory::Connection, ::eclipse::LogLevel::Info, (msg))
#define LOG_CONN_ERR(msg) ::eclipse::Logger::instance().log(::eclipse::LogCategory::Connection, ::eclipse::LogLevel::Error, (msg))
#define LOG_AUTH(msg)     ::eclipse::Logger::instance().log(::eclipse::LogCategory::Authentication, ::eclipse::LogLevel::Info, (msg))
#define LOG_AUTH_ERR(msg) ::eclipse::Logger::instance().log(::eclipse::LogCategory::Authentication, ::eclipse::LogLevel::Error, (msg))
#define LOG_SFTP(msg)     ::eclipse::Logger::instance().log(::eclipse::LogCategory::Sftp, ::eclipse::LogLevel::Info, (msg))
#define LOG_SFTP_ERR(msg) ::eclipse::Logger::instance().log(::eclipse::LogCategory::Sftp, ::eclipse::LogLevel::Error, (msg))
#define LOG_XFER(msg)     ::eclipse::Logger::instance().log(::eclipse::LogCategory::Transfer, ::eclipse::LogLevel::Info, (msg))
#define LOG_TUNNEL(msg)   ::eclipse::Logger::instance().log(::eclipse::LogCategory::Tunnel, ::eclipse::LogLevel::Info, (msg))
#define LOG_TUNNEL_ERR(msg) ::eclipse::Logger::instance().log(::eclipse::LogCategory::Tunnel, ::eclipse::LogLevel::Error, (msg))

} // namespace eclipse
