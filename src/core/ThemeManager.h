#pragma once

#include <QColor>
#include <QHash>
#include <QObject>
#include <QPalette>

namespace eclipse {

// ---------------------------------------------------------------------------
// ThemeManager: light / dark / system / custom themes + accent color.
//
// Since v0.3.0 this is the single source of truth for the Material Design 3
// color system: it derives the full MD3 role set (primary/secondary/tertiary
// + containers, surface container tonal steps, outline roles, inverse roles,
// status roles) from the user accent hue and the light/dark mode. QML reads
// these through the "ThemeBridge" context property, both from the Theme
// singleton (qml/Theme.qml) and directly from the EclipseMD3 Controls style.
//
// Every color property notifies `paletteChanged()`; `applyPalette()` also
// mirrors the most important roles onto the QApplication QPalette for the
// few native widgets that exist.
// ---------------------------------------------------------------------------

class ThemeManager : public QObject
{
    Q_OBJECT
    Q_PROPERTY(bool isDark READ isDark NOTIFY themeChanged)
    Q_PROPERTY(QColor accent READ accent NOTIFY accentChanged)

    // ---- MD3 accent-derived roles ------------------------------------------
    Q_PROPERTY(QColor primary READ primary NOTIFY paletteChanged)
    Q_PROPERTY(QColor onPrimary READ onPrimary NOTIFY paletteChanged)
    Q_PROPERTY(QColor primaryContainer READ primaryContainer NOTIFY paletteChanged)
    Q_PROPERTY(QColor onPrimaryContainer READ onPrimaryContainer NOTIFY paletteChanged)
    Q_PROPERTY(QColor secondary READ secondary NOTIFY paletteChanged)
    Q_PROPERTY(QColor onSecondary READ onSecondary NOTIFY paletteChanged)
    Q_PROPERTY(QColor secondaryContainer READ secondaryContainer NOTIFY paletteChanged)
    Q_PROPERTY(QColor onSecondaryContainer READ onSecondaryContainer NOTIFY paletteChanged)
    Q_PROPERTY(QColor tertiary READ tertiary NOTIFY paletteChanged)
    Q_PROPERTY(QColor onTertiary READ onTertiary NOTIFY paletteChanged)
    Q_PROPERTY(QColor tertiaryContainer READ tertiaryContainer NOTIFY paletteChanged)
    Q_PROPERTY(QColor onTertiaryContainer READ onTertiaryContainer NOTIFY paletteChanged)
    Q_PROPERTY(QColor inversePrimary READ inversePrimary NOTIFY paletteChanged)

    // ---- MD3 status roles ----------------------------------------------------
    Q_PROPERTY(QColor error READ error NOTIFY paletteChanged)
    Q_PROPERTY(QColor onError READ onError NOTIFY paletteChanged)
    Q_PROPERTY(QColor errorContainer READ errorContainer NOTIFY paletteChanged)
    Q_PROPERTY(QColor onErrorContainer READ onErrorContainer NOTIFY paletteChanged)
    Q_PROPERTY(QColor success READ success NOTIFY paletteChanged)
    Q_PROPERTY(QColor onSuccess READ onSuccess NOTIFY paletteChanged)
    Q_PROPERTY(QColor successContainer READ successContainer NOTIFY paletteChanged)
    Q_PROPERTY(QColor onSuccessContainer READ onSuccessContainer NOTIFY paletteChanged)
    Q_PROPERTY(QColor warning READ warning NOTIFY paletteChanged)
    Q_PROPERTY(QColor onWarning READ onWarning NOTIFY paletteChanged)
    Q_PROPERTY(QColor warningContainer READ warningContainer NOTIFY paletteChanged)
    Q_PROPERTY(QColor onWarningContainer READ onWarningContainer NOTIFY paletteChanged)

    // ---- MD3 surface roles ---------------------------------------------------
    Q_PROPERTY(QColor background READ background NOTIFY paletteChanged)
    Q_PROPERTY(QColor surface READ surface NOTIFY paletteChanged)
    Q_PROPERTY(QColor surfaceDim READ surfaceDim NOTIFY paletteChanged)
    Q_PROPERTY(QColor surfaceBright READ surfaceBright NOTIFY paletteChanged)
    Q_PROPERTY(QColor surfaceContainerLowest READ surfaceContainerLowest NOTIFY paletteChanged)
    Q_PROPERTY(QColor surfaceContainerLow READ surfaceContainerLow NOTIFY paletteChanged)
    Q_PROPERTY(QColor surfaceContainer READ surfaceContainer NOTIFY paletteChanged)
    Q_PROPERTY(QColor surfaceContainerHigh READ surfaceContainerHigh NOTIFY paletteChanged)
    Q_PROPERTY(QColor surfaceContainerHighest READ surfaceContainerHighest NOTIFY paletteChanged)
    Q_PROPERTY(QColor onSurface READ onSurface NOTIFY paletteChanged)
    Q_PROPERTY(QColor onSurfaceVariant READ onSurfaceVariant NOTIFY paletteChanged)
    Q_PROPERTY(QColor outline READ outline NOTIFY paletteChanged)
    Q_PROPERTY(QColor outlineVariant READ outlineVariant NOTIFY paletteChanged)
    Q_PROPERTY(QColor inverseSurface READ inverseSurface NOTIFY paletteChanged)
    Q_PROPERTY(QColor inverseOnSurface READ inverseOnSurface NOTIFY paletteChanged)
    Q_PROPERTY(QColor scrim READ scrim NOTIFY paletteChanged)

public:
    static ThemeManager& instance();

    enum class Mode { System, Light, Dark, Custom };

    Q_INVOKABLE void applyFromSettings();
    void setMode(Mode m);
    Mode mode() const { return m_mode; }
    bool isDark() const { return m_dark; }

    QColor accent() const { return m_accent; }
    void setAccent(const QColor& c);

    void applyPalette();

    // Legacy helper (unused by QML since v0.3.0; kept for compatibility).
    QHash<QString, QColor> namedColors() const;

    // Role getters (see Q_PROPERTY declarations above).
    QColor primary() const { return m_primary; }
    QColor onPrimary() const { return m_onPrimary; }
    QColor primaryContainer() const { return m_primaryContainer; }
    QColor onPrimaryContainer() const { return m_onPrimaryContainer; }
    QColor secondary() const { return m_secondary; }
    QColor onSecondary() const { return m_onSecondary; }
    QColor secondaryContainer() const { return m_secondaryContainer; }
    QColor onSecondaryContainer() const { return m_onSecondaryContainer; }
    QColor tertiary() const { return m_tertiary; }
    QColor onTertiary() const { return m_onTertiary; }
    QColor tertiaryContainer() const { return m_tertiaryContainer; }
    QColor onTertiaryContainer() const { return m_onTertiaryContainer; }
    QColor inversePrimary() const { return m_inversePrimary; }
    QColor error() const { return m_error; }
    QColor onError() const { return m_onError; }
    QColor errorContainer() const { return m_errorContainer; }
    QColor onErrorContainer() const { return m_onErrorContainer; }
    QColor success() const { return m_success; }
    QColor onSuccess() const { return m_onSuccess; }
    QColor successContainer() const { return m_successContainer; }
    QColor onSuccessContainer() const { return m_onSuccessContainer; }
    QColor warning() const { return m_warning; }
    QColor onWarning() const { return m_onWarning; }
    QColor warningContainer() const { return m_warningContainer; }
    QColor onWarningContainer() const { return m_onWarningContainer; }
    QColor background() const { return m_background; }
    QColor surface() const { return m_surface; }
    QColor surfaceDim() const { return m_surfaceDim; }
    QColor surfaceBright() const { return m_surfaceBright; }
    QColor surfaceContainerLowest() const { return m_surfaceContainerLowest; }
    QColor surfaceContainerLow() const { return m_surfaceContainerLow; }
    QColor surfaceContainer() const { return m_surfaceContainer; }
    QColor surfaceContainerHigh() const { return m_surfaceContainerHigh; }
    QColor surfaceContainerHighest() const { return m_surfaceContainerHighest; }
    QColor onSurface() const { return m_onSurface; }
    QColor onSurfaceVariant() const { return m_onSurfaceVariant; }
    QColor outline() const { return m_outline; }
    QColor outlineVariant() const { return m_outlineVariant; }
    QColor inverseSurface() const { return m_inverseSurface; }
    QColor inverseOnSurface() const { return m_inverseOnSurface; }
    QColor scrim() const { return m_scrim; }

signals:
    void themeChanged();
    void accentChanged();
    void paletteChanged();

private:
    ThemeManager() = default;
    bool systemInDarkMode() const;
    void derivePalette();

    Mode m_mode = Mode::System;
    bool m_dark = false;
    QColor m_accent{ 91, 141, 239 }; // #5B8DEF default accent

    QColor m_primary, m_onPrimary, m_primaryContainer, m_onPrimaryContainer;
    QColor m_secondary, m_onSecondary, m_secondaryContainer, m_onSecondaryContainer;
    QColor m_tertiary, m_onTertiary, m_tertiaryContainer, m_onTertiaryContainer;
    QColor m_inversePrimary;
    QColor m_error, m_onError, m_errorContainer, m_onErrorContainer;
    QColor m_success, m_onSuccess, m_successContainer, m_onSuccessContainer;
    QColor m_warning, m_onWarning, m_warningContainer, m_onWarningContainer;
    QColor m_background, m_surface, m_surfaceDim, m_surfaceBright;
    QColor m_surfaceContainerLowest, m_surfaceContainerLow, m_surfaceContainer;
    QColor m_surfaceContainerHigh, m_surfaceContainerHighest;
    QColor m_onSurface, m_onSurfaceVariant, m_outline, m_outlineVariant;
    QColor m_inverseSurface, m_inverseOnSurface, m_scrim;
};

} // namespace eclipse
