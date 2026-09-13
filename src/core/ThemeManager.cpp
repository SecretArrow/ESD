#include "ThemeManager.h"

#include <QGuiApplication>
#include <QStyleHints>

#include <algorithm>

#include "settings/Settings.h"

namespace eclipse {

namespace {

// HSL helper: rebuild a color from the accent hue with explicit
// saturation/lightness (all 0..1). Guards against degenerate hue (-1) for
// grayscale accents by falling back to a calm indigo hue.
QColor hsl(const QColor& seed, double s, double l, double hShift = 0.0)
{
    double h = seed.hslHueF();
    if (h < 0.0)
        h = 228.0 / 360.0;
    h = h + hShift;
    if (h < 0.0)
        h += 1.0;
    if (h > 1.0)
        h -= 1.0;
    s = std::clamp(s, 0.0, 1.0);
    l = std::clamp(l, 0.0, 1.0);
    QColor c;
    c.setHslF(h, s, l);
    return c.isValid() ? c : QColor(0x5B, 0x8D, 0xEF);
}

double clampd(double v, double lo, double hi)
{
    return v < lo ? lo : (v > hi ? hi : v);
}

// Choose black-ish or white-ish foreground for a background per WCAG-ish
// contrast heuristics (good enough for UI text roles).
QColor readableOn(const QColor& bg)
{
    const double lum = 0.2126 * bg.redF() + 0.7152 * bg.greenF() + 0.0722 * bg.blueF();
    return lum > 0.55 ? QColor(0x17, 0x18, 0x1C) : QColor(0xFF, 0xFF, 0xFF);
}

} // namespace

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

void ThemeManager::derivePalette()
{
    const QColor& a = m_accent;
    const double h = a.hslHueF() < 0.0 ? 228.0 / 360.0 : a.hslHueF();
    const double s = clampd(a.hslSaturationF(), 0.12, 0.95);

    // --- primary -----------------------------------------------------------
    if (m_dark) {
        m_primary = hsl(a, s, clampd(a.lightnessF() * 1.10, 0.58, 0.80));
        m_primaryContainer = hsl(a, s * 0.80, 0.30);
        m_onPrimaryContainer = hsl(a, s * 0.55, 0.90);
        m_secondary = hsl(a, s * 0.30, 0.78);
        m_secondaryContainer = hsl(a, s * 0.26, 0.30);
        m_onSecondaryContainer = hsl(a, s * 0.22, 0.88);
        m_tertiary = hsl(a, s * 0.60, 0.76, 1.0 / 6.0);
        m_tertiaryContainer = hsl(a, s * 0.50, 0.32, 1.0 / 6.0);
        m_onTertiaryContainer = hsl(a, s * 0.40, 0.90, 1.0 / 6.0);
        m_inversePrimary = hsl(a, s * 0.55, 0.42);
    } else {
        m_primary = hsl(a, s, clampd(a.lightnessF() * 0.90, 0.32, 0.60));
        m_primaryContainer = hsl(a, s * 0.55, 0.90);
        m_onPrimaryContainer = hsl(a, s * 0.85, 0.14);
        m_secondary = hsl(a, s * 0.32, 0.38);
        m_secondaryContainer = hsl(a, s * 0.22, 0.91);
        m_onSecondaryContainer = hsl(a, s * 0.50, 0.15);
        m_tertiary = hsl(a, s * 0.60, 0.38, 1.0 / 6.0);
        m_tertiaryContainer = hsl(a, s * 0.42, 0.91, 1.0 / 6.0);
        m_onTertiaryContainer = hsl(a, s * 0.70, 0.14, 1.0 / 6.0);
        m_inversePrimary = hsl(a, s * 0.55, 0.78);
    }
    m_onPrimary = readableOn(m_primary);
    m_onSecondary = readableOn(m_secondary);
    m_onTertiary = readableOn(m_tertiary);

    // --- status roles ------------------------------------------------------
    if (m_dark) {
        m_error = QColor(0xF2, 0xB8, 0xB5);
        m_onError = QColor(0x60, 0x14, 0x10);
        m_errorContainer = QColor(0x8C, 0x1D, 0x18);
        m_onErrorContainer = QColor(0xF9, 0xDE, 0xDC);
        m_success = QColor(0x6D, 0xD5, 0x8C);
        m_onSuccess = QColor(0x00, 0x39, 0x17);
        m_successContainer = QColor(0x0F, 0x52, 0x23);
        m_onSuccessContainer = QColor(0xA6, 0xF2, 0xC4);
        m_warning = QColor(0xF6, 0xC2, 0x44);
        m_onWarning = QColor(0x3F, 0x2D, 0x00);
        m_warningContainer = QColor(0x5C, 0x43, 0x00);
        m_onWarningContainer = QColor(0xFF, 0xDF, 0xA1);
    } else {
        m_error = QColor(0xB3, 0x26, 0x1E);
        m_onError = QColor(0xFF, 0xFF, 0xFF);
        m_errorContainer = QColor(0xF9, 0xDE, 0xDC);
        m_onErrorContainer = QColor(0x41, 0x0E, 0x0B);
        m_success = QColor(0x14, 0x6C, 0x2E);
        m_onSuccess = QColor(0xFF, 0xFF, 0xFF);
        m_successContainer = QColor(0xB6, 0xF2, 0xC9);
        m_onSuccessContainer = QColor(0x07, 0x27, 0x11);
        m_warning = QColor(0x7A, 0x54, 0x00);
        m_onWarning = QColor(0xFF, 0xFF, 0xFF);
        m_warningContainer = QColor(0xFF, 0xDF, 0xA1);
        m_onWarningContainer = QColor(0x26, 0x1A, 0x00);
    }

    // --- neutral surface roles, subtly tinted by the accent hue -------------
    if (m_dark) {
        m_background = hsl(a, 0.16, 0.075);
        m_surface = hsl(a, 0.15, 0.095);
        m_surfaceDim = hsl(a, 0.15, 0.075);
        m_surfaceBright = hsl(a, 0.15, 0.24);
        m_surfaceContainerLowest = hsl(a, 0.15, 0.06);
        m_surfaceContainerLow = hsl(a, 0.15, 0.11);
        m_surfaceContainer = hsl(a, 0.15, 0.13);
        m_surfaceContainerHigh = hsl(a, 0.15, 0.155);
        m_surfaceContainerHighest = hsl(a, 0.15, 0.18);
        m_onSurface = hsl(a, 0.10, 0.91);
        m_onSurfaceVariant = hsl(a, 0.09, 0.66);
        m_outline = hsl(a, 0.09, 0.42);
        m_outlineVariant = hsl(a, 0.14, 0.22);
        m_inverseSurface = hsl(a, 0.10, 0.91);
        m_inverseOnSurface = hsl(a, 0.15, 0.13);
        m_scrim = QColor(0, 0, 0);
    } else {
        m_background = hsl(a, 0.24, 0.965);
        m_surface = hsl(a, 0.30, 0.988);
        m_surfaceDim = hsl(a, 0.20, 0.87);
        m_surfaceBright = hsl(a, 0.30, 0.99);
        m_surfaceContainerLowest = QColor(0xFF, 0xFF, 0xFF);
        m_surfaceContainerLow = hsl(a, 0.26, 0.965);
        m_surfaceContainer = hsl(a, 0.26, 0.945);
        m_surfaceContainerHigh = hsl(a, 0.24, 0.925);
        m_surfaceContainerHighest = hsl(a, 0.22, 0.905);
        m_onSurface = hsl(a, 0.22, 0.115);
        m_onSurfaceVariant = hsl(a, 0.08, 0.40);
        m_outline = hsl(a, 0.06, 0.56);
        m_outlineVariant = hsl(a, 0.18, 0.885);
        m_inverseSurface = hsl(a, 0.16, 0.13);
        m_inverseOnSurface = hsl(a, 0.26, 0.95);
        m_scrim = QColor(0, 0, 0);
    }
}

void ThemeManager::applyPalette()
{
    derivePalette();

    QPalette p;
    const bool dark = m_dark;
    p.setColor(QPalette::Window, dark ? m_surface : m_background);
    p.setColor(QPalette::WindowText, m_onSurface);
    p.setColor(QPalette::Base, m_surfaceContainerLowest);
    p.setColor(QPalette::AlternateBase, m_surfaceContainer);
    p.setColor(QPalette::ToolTipBase, m_inverseSurface);
    p.setColor(QPalette::ToolTipText, m_inverseOnSurface);
    p.setColor(QPalette::Text, m_onSurface);
    p.setColor(QPalette::PlaceholderText, m_onSurfaceVariant);
    p.setColor(QPalette::Button, m_surfaceContainer);
    p.setColor(QPalette::ButtonText, m_onSurface);
    p.setColor(QPalette::BrightText, Qt::white);
    p.setColor(QPalette::Highlight, m_primary);
    p.setColor(QPalette::HighlightedText, m_onPrimary);
    p.setColor(QPalette::Link, m_primary);
    p.setColor(QPalette::Disabled, QPalette::Text, dark ? QColor(0x58, 0x5F, 0x6E) : QColor(0xB0, 0xB6, 0xC0));
    p.setColor(QPalette::Disabled, QPalette::ButtonText, dark ? QColor(0x58, 0x5F, 0x6E) : QColor(0xB0, 0xB6, 0xC0));
    QGuiApplication::setPalette(p);

    emit paletteChanged();
}

QHash<QString, QColor> ThemeManager::namedColors() const
{
    return {
        { "background", m_background },
        { "surface", m_surface },
        { "surfaceAlt", m_surfaceContainer },
        { "border", m_outlineVariant },
        { "text", m_onSurface },
        { "textMuted", m_onSurfaceVariant },
        { "success", m_success },
        { "warning", m_warning },
        { "error", m_error },
        { "accent", m_accent },
        { "accentText", m_onPrimary },
        { "primary", m_primary },
        { "onPrimary", m_onPrimary },
        { "outline", m_outline },
        { "outlineVariant", m_outlineVariant },
    };
}

} // namespace eclipse
