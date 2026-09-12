#pragma once

#include <QColor>
#include <QHash>
#include <QObject>
#include <QPalette>

namespace eclipse {

// ---------------------------------------------------------------------------
// ThemeManager: light / dark / system / custom themes + accent color + density.
// Computes a full QPalette and exposes named colors to QML as a singleton.
// Terminal color schemes live in the terminal module.
// ---------------------------------------------------------------------------

class ThemeManager : public QObject
{
    Q_OBJECT
public:
    static ThemeManager& instance();

    enum class Mode { System, Light, Dark, Custom };

    void applyFromSettings();
    void setMode(Mode m);
    Mode mode() const { return m_mode; }
    bool isDark() const { return m_dark; }

    QColor accent() const { return m_accent; }
    void setAccent(const QColor& c);

    void applyPalette();

    // Named colors for QML binding (see qml/Theme.qml singleton).
    QHash<QString, QColor> namedColors() const;

signals:
    void themeChanged();
    void accentChanged();

private:
    ThemeManager() = default;
    bool systemInDarkMode() const;

    Mode m_mode = Mode::System;
    bool m_dark = false;
    QColor m_accent{ 91, 141, 239 }; // #5B8DEF default accent
};

} // namespace eclipse
