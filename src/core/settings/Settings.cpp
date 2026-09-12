#include "Settings.h"

#include <QCoreApplication>
#include <QDir>

#include "../../common/Utils.h"
#include "../logging/Logger.h"

namespace eclipse {

Settings& Settings::instance()
{
    static Settings s;
    return s;
}

Settings::Settings()
    : m_store(utils::configDirectory() + QStringLiteral("/settings.ini"), QSettings::IniFormat)
{
}

QVariant Settings::value(const QString& key, const QVariant& fallback) const
{
    return m_store.value(key, fallback);
}

void Settings::setValue(const QString& key, const QVariant& v)
{
    if (m_store.value(key) == v)
        return;
    m_store.setValue(key, v);
    emit changed(key);
}

bool Settings::contains(const QString& key) const
{
    return m_store.contains(key);
}

void Settings::sync()
{
    m_store.sync();
}

QString Settings::themeMode() const
{
    return m_store.value(QStringLiteral("appearance/theme"), QStringLiteral("system")).toString();
}
void Settings::setThemeMode(const QString& mode)
{
    setValue(QStringLiteral("appearance/theme"), mode);
}

QString Settings::accentColor() const
{
    return m_store.value(QStringLiteral("appearance/accent"), QStringLiteral("#5B8DEF")).toString();
}
void Settings::setAccentColor(const QString& color)
{
    setValue(QStringLiteral("appearance/accent"), color);
}

double Settings::uiDensity() const
{
    return m_store.value(QStringLiteral("appearance/density"), 1.0).toDouble();
}
void Settings::setUiDensity(double d)
{
    setValue(QStringLiteral("appearance/density"), d);
}

QString Settings::uiFontFamily() const
{
    return m_store.value(QStringLiteral("appearance/uiFont"), QStringLiteral("")).toString();
}
int Settings::uiFontSize() const
{
    return m_store.value(QStringLiteral("appearance/uiFontSize"), 10).toInt();
}
void Settings::setUiFontSize(int size)
{
    setValue(QStringLiteral("appearance/uiFontSize"), size);
}

QString Settings::terminalFontFamily() const
{
    return m_store.value(QStringLiteral("terminal/fontFamily"), QStringLiteral("monospace")).toString();
}
int Settings::terminalFontSize() const
{
    return m_store.value(QStringLiteral("terminal/fontSize"), 12).toInt();
}
int Settings::terminalScrollback() const
{
    return m_store.value(QStringLiteral("terminal/scrollback"), 10000).toInt();
}
QString Settings::terminalCursorStyle() const
{
    return m_store.value(QStringLiteral("terminal/cursorStyle"), QStringLiteral("block")).toString();
}
bool Settings::terminalCursorBlink() const
{
    return m_store.value(QStringLiteral("terminal/cursorBlink"), true).toBool();
}
QString Settings::terminalColorScheme() const
{
    return m_store.value(QStringLiteral("terminal/colorScheme"), QStringLiteral("eclipse-dark")).toString();
}

bool Settings::minimizeToTray() const
{
    return m_store.value(QStringLiteral("behavior/minimizeToTray"), true).toBool();
}
bool Settings::confirmOverwrite() const
{
    return m_store.value(QStringLiteral("fileManager/confirmOverwrite"), true).toBool();
}
bool Settings::confirmDelete() const
{
    return m_store.value(QStringLiteral("fileManager/confirmDelete"), true).toBool();
}
bool Settings::showHiddenFiles() const
{
    return m_store.value(QStringLiteral("fileManager/showHidden"), false).toBool();
}
int Settings::maxParallelTransfers() const
{
    return m_store.value(QStringLiteral("transfers/maxParallel"), 2).toInt();
}
bool Settings::autoReconnect() const
{
    return m_store.value(QStringLiteral("connections/autoReconnect"), true).toBool();
}
int Settings::reconnectRetries() const
{
    return m_store.value(QStringLiteral("connections/reconnectRetries"), 5).toInt();
}
int Settings::reconnectBaseIntervalMs() const
{
    return m_store.value(QStringLiteral("connections/reconnectBaseInterval"), 1500).toInt();
}
QString Settings::credentialStorage() const
{
    return m_store.value(QStringLiteral("security/credentialStorage"), QStringLiteral("os")).toString();
}
QString Settings::defaultEngine() const
{
    return m_store.value(QStringLiteral("ssh/defaultEngine"), QStringLiteral("auto")).toString();
}
void Settings::setDefaultEngine(const QString& engine)
{
    setValue(QStringLiteral("ssh/defaultEngine"), engine);
}

QString Settings::profilesSyncFolder() const
{
    return m_store.value(QStringLiteral("profiles/syncFolder"), QString()).toString();
}

void Settings::setProfilesSyncFolder(const QString& folder)
{
    // Store with forward slashes so bundles/settings stay portable across OSes.
    const QString normalized = QDir::fromNativeSeparators(folder.trimmed());
    if (profilesSyncFolder() == normalized)
        return; // mirrors setValue()'s no-op-on-unchanged behavior
    setValue(QStringLiteral("profiles/syncFolder"), normalized);
    emit profilesSyncFolderChanged();
}

bool Settings::debugMode() const
{
    return m_store.value(QStringLiteral("advanced/debugMode"), false).toBool();
}
QString Settings::updateChannel() const
{
    return m_store.value(QStringLiteral("advanced/updateChannel"), QStringLiteral("stable")).toString();
}

} // namespace eclipse
