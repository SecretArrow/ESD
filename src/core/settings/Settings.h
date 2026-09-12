#pragma once

#include <QHash>
#include <QObject>
#include <QSettings>
#include <QVariant>

namespace eclipse {

// Central typed settings store (QSettings INI under the OS config dir).
// Keys are grouped per settings page; unknown keys fall back to defaults.
class Settings : public QObject
{
    Q_OBJECT
    Q_PROPERTY(QString themeMode READ themeMode WRITE setThemeMode)
    Q_PROPERTY(QString accentColor READ accentColor WRITE setAccentColor)
    Q_PROPERTY(double uiDensity READ uiDensity WRITE setUiDensity)
    Q_PROPERTY(int uiFontSize READ uiFontSize WRITE setUiFontSize)
    Q_PROPERTY(QString terminalFontFamily READ terminalFontFamily WRITE setTerminalFontFamily)
    Q_PROPERTY(int terminalFontSize READ terminalFontSize WRITE setTerminalFontSize)
    Q_PROPERTY(int terminalScrollback READ terminalScrollback WRITE setTerminalScrollback)
    Q_PROPERTY(QString terminalCursorStyle READ terminalCursorStyle WRITE setTerminalCursorStyle)
    Q_PROPERTY(bool terminalCursorBlink READ terminalCursorBlink WRITE setTerminalCursorBlink)
    Q_PROPERTY(bool minimizeToTray READ minimizeToTray WRITE setMinimizeToTray)
    Q_PROPERTY(bool confirmOverwrite READ confirmOverwrite WRITE setConfirmOverwrite)
    Q_PROPERTY(bool confirmDelete READ confirmDelete WRITE setConfirmDelete)
    Q_PROPERTY(bool showHiddenFiles READ showHiddenFiles WRITE setShowHiddenFiles)
    Q_PROPERTY(int maxParallelTransfers READ maxParallelTransfers WRITE setMaxParallelTransfers)
    Q_PROPERTY(bool autoReconnect READ autoReconnect WRITE setAutoReconnect)
    Q_PROPERTY(int reconnectRetries READ reconnectRetries WRITE setReconnectRetries)
    Q_PROPERTY(bool debugMode READ debugMode WRITE setDebugMode)
    Q_PROPERTY(QString updateChannel READ updateChannel WRITE setUpdateChannel)
    Q_PROPERTY(QString defaultEngine READ defaultEngine WRITE setDefaultEngine)
    Q_PROPERTY(QString profilesSyncFolder READ profilesSyncFolder WRITE setProfilesSyncFolder NOTIFY profilesSyncFolderChanged)
public:
    static Settings& instance();

    QVariant value(const QString& key, const QVariant& fallback = {}) const;
    void setValue(const QString& key, const QVariant& v);
    bool contains(const QString& key) const;

    // --- typed convenience -------------------------------------------------
    QString uiLanguageDefault() const { return QStringLiteral("system"); }

    void setTerminalFontFamily(const QString& f) { setValue(QStringLiteral("terminal/fontFamily"), f); }
    void setTerminalFontSize(int v) { setValue(QStringLiteral("terminal/fontSize"), v); }
    void setTerminalScrollback(int v) { setValue(QStringLiteral("terminal/scrollback"), v); }
    void setTerminalCursorStyle(const QString& s) { setValue(QStringLiteral("terminal/cursorStyle"), s); }
    void setTerminalCursorBlink(bool b) { setValue(QStringLiteral("terminal/cursorBlink"), b); }
    void setMinimizeToTray(bool b) { setValue(QStringLiteral("behavior/minimizeToTray"), b); }
    void setConfirmOverwrite(bool b) { setValue(QStringLiteral("fileManager/confirmOverwrite"), b); }
    void setConfirmDelete(bool b) { setValue(QStringLiteral("fileManager/confirmDelete"), b); }
    void setShowHiddenFiles(bool b) { setValue(QStringLiteral("fileManager/showHidden"), b); }
    void setMaxParallelTransfers(int v) { setValue(QStringLiteral("transfers/maxParallel"), v); }
    void setAutoReconnect(bool b) { setValue(QStringLiteral("connections/autoReconnect"), b); }
    void setReconnectRetries(int v) { setValue(QStringLiteral("connections/reconnectRetries"), v); }
    void setDebugMode(bool b) { setValue(QStringLiteral("advanced/debugMode"), b); }
    void setUpdateChannel(const QString& s) { setValue(QStringLiteral("advanced/updateChannel"), s); }

    // Appearance
    QString themeMode() const;      // system | light | dark | custom
    void setThemeMode(const QString& mode);
    QString accentColor() const;
    void setAccentColor(const QString& color);
    double uiDensity() const;       // 0.85 compact | 1.0 comfortable | 1.15 dense
    void setUiDensity(double d);
    QString uiFontFamily() const;
    int uiFontSize() const;
    void setUiFontSize(int size);

    // Terminal
    QString terminalFontFamily() const;
    int terminalFontSize() const;
    int terminalScrollback() const;
    QString terminalCursorStyle() const;   // block | beam | underline
    bool terminalCursorBlink() const;
    QString terminalColorScheme() const;   // scheme name

    // Behavior
    bool minimizeToTray() const;
    bool confirmOverwrite() const;
    bool confirmDelete() const;
    bool showHiddenFiles() const;
    int maxParallelTransfers() const;
    bool autoReconnect() const;
    int reconnectRetries() const;
    int reconnectBaseIntervalMs() const;
    QString credentialStorage() const;      // os | session | ask
    QString defaultEngine() const;          // auto | libssh | libssh2
    void setDefaultEngine(const QString& engine);

    // Profiles sync folder / portable mode (key "profiles/syncFolder").
    // When non-empty, the integrator points ProfileStore's storage at this
    // folder (device-sync wiring lives in the app layer); empty = default
    // per-user app data location. Exposed to QML as
    // App.settings.profilesSyncFolder with profilesSyncFolderChanged.
    QString profilesSyncFolder() const;
    void setProfilesSyncFolder(const QString& folder);

    bool debugMode() const;
    QString updateChannel() const;          // stable | beta | nightly

    void sync();

signals:
    void changed(const QString& key);
    void profilesSyncFolderChanged();

private:
    Settings();
    QSettings m_store;
};

} // namespace eclipse
