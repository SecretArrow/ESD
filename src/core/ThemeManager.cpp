#include "ThemeManager.h"

#include <QGuiApplication>
#include <QStyleHints>

#include "settings/Settings.h"

namespace eclipse {

ThemeManager& ThemeManager::instance()
{
    static ThemeManager t;
    return t;
}

bool ThemeManager::systemInDarkMode() const
{
#if QT_VERSION >= QT_VERSION_CHECK(6, 5, 0)
    return QGuiApplication::styleHints()->colorScheme() == Qt::ColorScheme::Dark;
#else
    return false;
#endif
}

void ThemeManager::applyFromSettings()
{
    const QString mode = Settings::instance().themeMode();
    if (mode == QLatin1String("light"))
        setMode(Mode::Light);
    else if (mode == QLatin1String("dark"))
        setMode(Mode::Dark);
    else if (mode == QLatin1String("custom"))
        setMode(Mode::Custom);
    else
        setMode(Mode::System);

    const QString accent = Settings::instance().accentColor();
    if (!accent.isEmpty())
        setAccent(QColor(accent));
}

void ThemeManager::setMode(Mode m)
{
    m_mode = m;
    const bool wasDark = m_dark;
    m_dark = (m == Mode::Dark)
             || (m == Mode::System && systemInDarkMode())
             || (m == Mode::Custom && Settings::instance().value(QStringLiteral("appearance/customDark"), true).toBool());
    applyPalette();
    if (wasDark != m_dark)
        emit themeChanged();
}

void ThemeManager::setAccent(const QColor& c)
{
    if (!c.isValid() || c == m_accent)
        return;
    m_accent = c;
    applyPalette();
    emit accentChanged();
}

void ThemeManager::applyPalette()
{
    QPalette p;
    if (m_dark) {
        p.setColor(QPalette::Window, QColor(0x14, 0x17, 0x1E));
        p.setColor(QPalette::WindowText, QColor(0xE6, 0xE8, 0xEE));
        p.setColor(QPalette::Base, QColor(0x1A, 0x1E, 0x27));
        p.setColor(QPalette::AlternateBase, QColor(0x20, 0x25, 0x30));
        p.setColor(QPalette::ToolTipBase, QColor(0x24, 0x2A, 0x35));
        p.setColor(QPalette::ToolTipText, QColor(0xE6, 0xE8, 0xEE));
        p.setColor(QPalette::Text, QColor(0xE6, 0xE8, 0xEE));
        p.setColor(QPalette::PlaceholderText, QColor(0x6C, 0x74, 0x85));
        p.setColor(QPalette::Button, QColor(0x1E, 0x23, 0x2D));
        p.setColor(QPalette::ButtonText, QColor(0xE6, 0xE8, 0xEE));
        p.setColor(QPalette::BrightText, Qt::white);
        p.setColor(QPalette::Highlight, m_accent);
        p.setColor(QPalette::HighlightedText, QColor(0xFF, 0xFF, 0xFF));
        p.setColor(QPalette::Link, m_accent);
        p.setColor(QPalette::Disabled, QPalette::Text, QColor(0x58, 0x5F, 0x6E));
        p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0x58, 0x5F, 0x6E));
    } else {
        p.setColor(QPalette::Window, QColor(0xF5, 0xF7, 0xFA));
        p.setColor(QPalette::WindowText, QColor(0x1B, 0x1F, 0x27));
        p.setColor(QPalette::Base, QColor(0xFF, 0xFF, 0xFF));
        p.setColor(QPalette::AlternateBase, QColor(0xF0, 0xF2, 0xF6));
        p.setColor(QPalette::ToolTipBase, QColor(0xFF, 0xFF, 0xFF));
        p.setColor(QPalette::ToolTipText, QColor(0x1B, 0x1F, 0x27));
        p.setColor(QPalette::Text, QColor(0x1B, 0x1F, 0x27));
        p.setColor(QPalette::PlaceholderText, QColor(0x9A, 0xA1, 0xAE));
        p.setColor(QPalette::Button, QColor(0xEC, 0xEF, 0xF3));
        p.setColor(QPalette::ButtonText, QColor(0x1B, 0x1F, 0x27));
        p.setColor(QPalette::BrightText, Qt::white);
        p.setColor(QPalette::Highlight, m_accent);
        p.setColor(QPalette::HighlightedText, QColor(0xFF, 0xFF, 0xFF));
        p.setColor(QPalette::Link, m_accent);
        p.setColor(QPalette::Disabled, QPalette::Text, QColor(0xB0, 0xB6, 0xC0));
        p.setColor(QPalette::Disabled, QPalette::ButtonText, QColor(0xB0, 0xB6, 0xC0));
    }
    QGuiApplication::setPalette(p);
}

QHash<QString, QColor> ThemeManager::namedColors() const
{
    if (m_dark) {
        return {
            { "background", QColor(0x14, 0x17, 0x1E) },
            { "surface", QColor(0x1A, 0x1E, 0x27) },
            { "surfaceAlt", QColor(0x20, 0x25, 0x30) },
            { "border", QColor(0x2A, 0x30, 0x3C) },
            { "text", QColor(0xE6, 0xE8, 0xEE) },
            { "textMuted", QColor(0x8B, 0x93, 0xA5) },
            { "success", QColor(0x3F, 0xB6, 0x6F) },
            { "warning", QColor(0xE2, 0xB1, 0x2C) },
            { "error", QColor(0xE2, 0x5D, 0x5D) },
            { "accent", m_accent },
            { "accentText", QColor(0xFF, 0xFF, 0xFF) },
        };
    }
    return {
        { "background", QColor(0xF5, 0xF7, 0xFA) },
        { "surface", QColor(0xFF, 0xFF, 0xFF) },
        { "surfaceAlt", QColor(0xF0, 0xF2, 0xF6) },
        { "border", QColor(0xDE, 0xE2, 0xE8) },
        { "text", QColor(0x1B, 0x1F, 0x27) },
        { "textMuted", QColor(0x6B, 0x73, 0x82) },
        { "success", QColor(0x2F, 0x9E, 0x5F) },
        { "warning", QColor(0xC4, 0x92, 0x0F) },
        { "error", QColor(0xD4, 0x47, 0x47) },
        { "accent", m_accent },
        { "accentText", QColor(0xFF, 0xFF, 0xFF) },
    };
}

} // namespace eclipse
