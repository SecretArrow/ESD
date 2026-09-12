#include "Logger.h"

#include <QDateTime>
#include <QDir>
#include <QRegularExpression>

#include <cstdio>
#include "../../common/Utils.h"

namespace eclipse {

QString logCategoryName(LogCategory c)
{
    switch (c) {
    case LogCategory::Connection: return QStringLiteral("Connection");
    case LogCategory::Ssh: return QStringLiteral("SSH");
    case LogCategory::Sftp: return QStringLiteral("SFTP");
    case LogCategory::Transfer: return QStringLiteral("Transfer");
    case LogCategory::Tunnel: return QStringLiteral("Tunnel");
    case LogCategory::Authentication: return QStringLiteral("Authentication");
    case LogCategory::Application: return QStringLiteral("Application");
    case LogCategory::Error: return QStringLiteral("Error");
    case LogCategory::Debug: return QStringLiteral("Debug");
    }
    return QStringLiteral("Application");
}

LogCategory logCategoryFromName(const QString& name)
{
    static const QHash<QString, LogCategory> map = {
        { QStringLiteral("connection"), LogCategory::Connection },
        { QStringLiteral("ssh"), LogCategory::Ssh },
        { QStringLiteral("sftp"), LogCategory::Sftp },
        { QStringLiteral("transfer"), LogCategory::Transfer },
        { QStringLiteral("tunnel"), LogCategory::Tunnel },
        { QStringLiteral("authentication"), LogCategory::Authentication },
        { QStringLiteral("application"), LogCategory::Application },
        { QStringLiteral("error"), LogCategory::Error },
        { QStringLiteral("debug"), LogCategory::Debug },
    };
    return map.value(name.toLower(), LogCategory::Application);
}

// ---------------------------------------------------------------------------
// Redaction - the single most important logging function.
// ---------------------------------------------------------------------------
QString Logger::redact(QString msg)
{
    static const QRegularExpression passwordAssign(
        QStringLiteral("(?i)(password|passwd|secret|token|apikey|api_key)\\s*[=:]\\s*\\S+"),
        QRegularExpression::CaseInsensitiveOption);
    static const QRegularExpression privateKeyBlock(
        QStringLiteral("-----BEGIN [A-Z ]*PRIVATE KEY-----[\\s\\S]*?-----END [A-Z ]*PRIVATE KEY-----"));
    static const QRegularExpression passphraseOpt(
        QStringLiteral("(?i)(passphrase)\\s*[=:]\\s*\\S+"));

    msg.replace(privateKeyBlock, QStringLiteral("-----PRIVATE KEY REDACTED-----"));
    msg.replace(passwordAssign, QStringLiteral("\\1: [REDACTED]"));
    msg.replace(passphraseOpt, QStringLiteral("\\1: [REDACTED]"));
    return msg;
}

// ---------------------------------------------------------------------------
Logger& Logger::instance()
{
    static Logger logger;
    return logger;
}

Logger::Logger()
{
    m_uiModel = new LogModel(this);
    // Route Qt's own warnings into our log as well (safe subset).
    qInstallMessageHandler([](QtMsgType type, const QMessageLogContext&, const QString& msg) {
        LogLevel lvl = LogLevel::Info;
        switch (type) {
        case QtDebugMsg: lvl = LogLevel::Debug; break;
        case QtInfoMsg: lvl = LogLevel::Info; break;
        case QtWarningMsg: lvl = LogLevel::Warning; break;
        case QtCriticalMsg:
        case QtFatalMsg: lvl = LogLevel::Error; break;
        }
        Logger::instance().log(LogCategory::Application, lvl, QStringLiteral("[qt] %1").arg(msg));
        // Optional stderr passthrough for headless/CI debugging (off by default).
        static const bool stderrEcho = qEnvironmentVariableIsSet("ECLIPSE_LOG_STDERR");
        if (stderrEcho && type != QtDebugMsg && type != QtInfoMsg) {
            std::fprintf(stderr, "[qt] %s\n", qPrintable(msg));
            std::fflush(stderr);
        }
    });
}

void Logger::enableFileLogging(bool enabled)
{
    std::lock_guard<std::recursive_mutex> lock(m_mutex);
    m_fileLogging = enabled;
    if (!enabled && m_file.isOpen())
        m_file.close();
}

void Logger::enableDebugMode(bool enabled)
{
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        m_debugMode = enabled;
    }
    log(LogCategory::Application, LogLevel::Info,
        QStringLiteral("Debug mode %1").arg(enabled ? QStringLiteral("ENABLED") : QStringLiteral("disabled")));
}

void Logger::log(LogCategory cat, LogLevel level, const QString& message)
{
    const QString clean = redact(message);
    {
        std::lock_guard<std::recursive_mutex> lock(m_mutex);
        if (level == LogLevel::Debug && !m_debugMode)
            return;
        writeToFile(cat, level, clean);
        m_uiModel->append({ QDateTime::currentMSecsSinceEpoch(), cat, level, clean });
    }
}

void Logger::rotateIfNeeded()
{
    if (m_file.size() < 2 * 1024 * 1024)
        return;
    const QString base = utils::dataDirectory() + QStringLiteral("/logs");
    QDir().mkpath(base);
    const QString path = base + QStringLiteral("/eclipse-ssh.log");
    QFile::remove(path + QStringLiteral(".1"));
    m_file.close();
    QFile::rename(path, path + QStringLiteral(".1"));
    m_file.setFileName(path);
    m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
}

void Logger::writeToFile(LogCategory cat, LogLevel level, const QString& msg)
{
    if (!m_fileLogging)
        return;
    if (!m_file.isOpen()) {
        const QString dir = utils::dataDirectory() + QStringLiteral("/logs");
        QDir().mkpath(dir);
        m_file.setFileName(dir + QStringLiteral("/eclipse-ssh.log"));
        m_file.open(QIODevice::WriteOnly | QIODevice::Append | QIODevice::Text);
        if (!m_file.isOpen())
            return;
    }
    rotateIfNeeded();
    static const char* lvlName[] = { "DEBUG", "INFO ", "WARN ", "ERROR" };
    const QString line = QStringLiteral("%1 [%2] %3: %4\n")
                             .arg(QDateTime::currentDateTime().toString(QStringLiteral("yyyy-MM-dd hh:mm:ss.zzz")),
                                  QLatin1String(lvlName[int(level)]), logCategoryName(cat), msg);
    m_file.write(line.toUtf8());
    m_file.flush();
}

QString Logger::exportLogs(const QString& path)
{
    const auto entries = m_uiModel->entriesForExport();
    QFile f(path);
    if (!f.open(QIODevice::WriteOnly | QIODevice::Text))
        return f.errorString();
    for (const auto& e : entries) {
        f.write(QStringLiteral("%1 [%2] %3: %4\n")
                    .arg(QDateTime::fromMSecsSinceEpoch(e.tsMs).toString(Qt::ISODateWithMs),
                         logCategoryName(e.category),
                         e.level == LogLevel::Debug ? QStringLiteral("DEBUG")
                             : e.level == LogLevel::Info    ? QStringLiteral("INFO")
                             : e.level == LogLevel::Warning ? QStringLiteral("WARN")
                                                            : QStringLiteral("ERROR"),
                         e.message)
                    .toUtf8());
    }
    return {};
}

void Logger::clearUiHistory()
{
    m_uiModel->clear();
}

// ---------------------------------------------------------------------------
// LogModel
// ---------------------------------------------------------------------------
LogModel::LogModel(QObject* parent)
    : QAbstractListModel(parent)
{
}

int LogModel::rowCount(const QModelIndex& parent) const
{
    if (parent.isValid())
        return 0;
    QMutexLocker lock(&m_mutex);
    return int(m_filtered.size());
}

QHash<int, QByteArray> LogModel::roleNames() const
{
    return {
        { TsRole, "ts" },
        { CategoryRole, "category" },
        { LevelRole, "level" },
        { MessageRole, "message" },
        { LevelNameRole, "levelName" },
        { CategoryNameRole, "categoryName" },
        { TimeTextRole, "timeText" },
    };
}

QVariant LogModel::data(const QModelIndex& index, int role) const
{
    QMutexLocker lock(&m_mutex);
    const int row = index.row();
    if (row < 0 || row >= int(m_filtered.size()))
        return {};
    const Entry& e = m_ring[size_t(m_filtered[row])];
    switch (role) {
    case TsRole: return e.tsMs;
    case CategoryRole: return int(e.category);
    case LevelRole: return int(e.level);
    case MessageRole: return e.message;
    case LevelNameRole:
        return e.level == LogLevel::Debug ? QStringLiteral("DEBUG")
             : e.level == LogLevel::Info  ? QStringLiteral("INFO")
             : e.level == LogLevel::Warning ? QStringLiteral("WARN")
                                            : QStringLiteral("ERROR");
    case CategoryNameRole: return logCategoryName(e.category);
    case TimeTextRole:
        return QDateTime::fromMSecsSinceEpoch(e.tsMs).toString(QStringLiteral("hh:mm:ss.zzz"));
    }
    return {};
}

void LogModel::rebuildFilter()
{
    m_filtered.clear();
    for (size_t i = 0; i < m_ring.size(); ++i) {
        const Entry& e = m_ring[i];
        if (int(e.level) < m_minLevel)
            continue;
        if (!m_filterCategory.isEmpty() && m_filterCategory != QStringLiteral("All")
            && logCategoryName(e.category).compare(m_filterCategory, Qt::CaseInsensitive) != 0)
            continue;
        if (!m_filterText.isEmpty()
            && !e.message.contains(m_filterText, Qt::CaseInsensitive))
            continue;
        m_filtered.append(int(i));
    }
}

void LogModel::append(const Entry& e)
{
    {
        QMutexLocker lock(&m_mutex);
        m_ring.push_back(e);
        while (m_ring.size() > kMaxEntries)
            m_ring.pop_front();
        rebuildFilter();
    }
}

void LogModel::clear()
{
    {
        QMutexLocker lock(&m_mutex);
        m_ring.clear();
        m_filtered.clear();
    }
    beginResetModel();
    endResetModel();
}

void LogModel::setFilter(const QString& text, int minLevel, const QString& category)
{
    {
        QMutexLocker lock(&m_mutex);
        m_filterText = text;
        m_minLevel = minLevel;
        m_filterCategory = category;
        rebuildFilter();
    }
    beginResetModel();
    endResetModel();
}

QVector<LogModel::Entry> LogModel::entriesForExport() const
{
    QMutexLocker lock(&m_mutex);
    QVector<Entry> out;
    out.reserve(int(m_ring.size()));
    for (const auto& e : m_ring)
        out.append(e);
    return out;
}

} // namespace eclipse
