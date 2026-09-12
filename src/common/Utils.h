#pragma once

#include <QString>
#include <QStringList>
#include <QUrl>

class QDateTime;

namespace eclipse {
namespace utils {

QString humanSize(quint64 bytes);
QString humanSpeed(double bytesPerSecond);
QString humanDuration(quint64 seconds);
QString etaText(quint64 bytesRemaining, double bytesPerSecond);

QString expandTildePath(const QString& path);   // ~/... and %USERPROFILE%
QString defaultKeyDirectory();                  // ~/.ssh equivalent
QString stripAnsi(const QString& text);

QStringList defaultFingerprintFields();
QString fingerprintText(const QByteArray& sha256Raw);
QString validateHost(const QString& host);      // returns error or empty
QString validatePort(int port);
bool isPrivateAddress(const QString& host);

QString configDirectory();    // OS-appropriate per-user config dir
QString dataDirectory();      // OS-appropriate per-user data dir
QString cacheDirectory();

QString uniqueTempFile(const QString& prefix, const QString& suffix);
void removeTempFile(const QString& path);

} // namespace utils
} // namespace eclipse
