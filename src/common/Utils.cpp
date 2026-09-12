#include "Utils.h"

#include <QDateTime>
#include <QDir>
#include <QDirIterator>
#include <QRegularExpression>
#include <QStandardPaths>
#include <QTemporaryFile>

namespace eclipse {
namespace utils {

QString humanSize(quint64 bytes)
{
    static const char* units[] = { "B", "KB", "MB", "GB", "TB", "PB" };
    double value = double(bytes);
    int unit = 0;
    while (value >= 1024.0 && unit < 5) {
        value /= 1024.0;
        ++unit;
    }
    if (unit == 0)
        return QString::number(bytes) + QStringLiteral(" B");
    return QString::number(value, 'f', value >= 100.0 ? 0 : 1) + QLatin1Char(' ')
           + QLatin1String(units[unit]);
}

QString humanSpeed(double bytesPerSecond)
{
    if (bytesPerSecond <= 0.0)
        return QStringLiteral("--");
    return humanSize(quint64(bytesPerSecond)) + QStringLiteral("/s");
}

QString humanDuration(quint64 seconds)
{
    const quint64 d = seconds / 86400;
    const quint64 h = (seconds % 86400) / 3600;
    const quint64 m = (seconds % 3600) / 60;
    const quint64 s = seconds % 60;
    if (d > 0)
        return QStringLiteral("%1d %2h %3m").arg(d).arg(h).arg(m);
    if (h > 0)
        return QStringLiteral("%1h %2m %3s").arg(h).arg(m).arg(s);
    if (m > 0)
        return QStringLiteral("%1m %2s").arg(m).arg(s);
    return QStringLiteral("%1s").arg(s);
}

QString etaText(quint64 bytesRemaining, double bytesPerSecond)
{
    if (bytesPerSecond <= 1.0 || bytesRemaining == 0)
        return QStringLiteral("--");
    return humanDuration(quint64(double(bytesRemaining) / bytesPerSecond));
}

QString expandTildePath(const QString& path)
{
    QString p = path.trimmed();
    if (p.startsWith(QStringLiteral("~/"))) {
        return QDir::homePath() + p.mid(1);
    }
#ifdef Q_OS_WIN
    static const QRegularExpression envRe(QStringLiteral("%([A-Za-z0-9_]+)%"));
    QRegularExpressionMatchIterator it = envRe.globalMatch(p);
    QString expanded = p;
    while (it.hasNext()) {
        const auto m = it.next();
        expanded.replace(m.captured(0), qEnvironmentVariable(m.captured(1).toUtf8().constData()));
    }
    return expanded;
#else
    return p;
#endif
}

QString defaultKeyDirectory()
{
    return QDir::homePath() + QStringLiteral("/.ssh");
}

QString stripAnsi(const QString& text)
{
    static const QRegularExpression ansi(QStringLiteral("\x1B\\[[0-9;?]*[ -/]*[@-~]"));
    return QString(text).remove(ansi);
}

QString fingerprintText(const QByteArray& sha256Raw)
{
    return QLatin1String("SHA256:") + QString::fromLatin1(sha256Raw.toBase64().trimmed());
}

QString validateHost(const QString& host)
{
    if (host.trimmed().isEmpty())
        return QStringLiteral("Host must not be empty.");
    if (host.contains(QLatin1Char(' ')))
        return QStringLiteral("Host must not contain spaces.");
    return {};
}

QString validatePort(int port)
{
    if (port <= 0 || port > 65535)
        return QStringLiteral("Port must be between 1 and 65535.");
    return {};
}

bool isPrivateAddress(const QString& host)
{
    return host.startsWith(QStringLiteral("10.")) || host.startsWith(QStringLiteral("192.168."))
           || host.startsWith(QStringLiteral("169.254."))
           || QRegularExpression(QStringLiteral("^172\\.(1[6-9]|2[0-9]|3[01])\\."))
                  .match(host)
                  .hasMatch()
           || host.startsWith(QStringLiteral("127."));
}

QString configDirectory()
{
#ifdef Q_OS_WIN
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
#else
    QString base = qEnvironmentVariable("XDG_CONFIG_HOME");
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.config");
    return base + QStringLiteral("/eclipse-ssh");
#endif
}

QString dataDirectory()
{
#ifdef Q_OS_WIN
    return QStandardPaths::writableLocation(QStandardPaths::AppDataLocation);
#else
    QString base = qEnvironmentVariable("XDG_DATA_HOME");
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.local/share");
    return base + QStringLiteral("/eclipse-ssh");
#endif
}

QString cacheDirectory()
{
#ifdef Q_OS_WIN
    return QStandardPaths::writableLocation(QStandardPaths::CacheLocation);
#else
    QString base = qEnvironmentVariable("XDG_CACHE_HOME");
    if (base.isEmpty())
        base = QDir::homePath() + QStringLiteral("/.cache");
    return base + QStringLiteral("/eclipse-ssh");
#endif
}

QString uniqueTempFile(const QString& prefix, const QString& suffix)
{
    QString dir = cacheDirectory() + QStringLiteral("/editor-tmp");
    QDir().mkpath(dir);
    QTemporaryFile f(dir + QStringLiteral("/") + prefix + QStringLiteral("_XXXXXX") + suffix);
    f.setAutoRemove(false);
    if (!f.open())
        return {};
    return f.fileName();
}

void removeTempFile(const QString& path)
{
    if (!path.isEmpty())
        QFile::remove(path);
}

} // namespace utils
} // namespace eclipse
