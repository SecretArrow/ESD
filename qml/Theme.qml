pragma Singleton
import QtQuick

// Application-wide Material Design 3 token system.
//
// Colors come from ThemeBridge (C++ ThemeManager) which derives the full MD3
// role set from the accent + light/dark mode. This singleton adds the design
// tokens MD3 needs beyond color: typography scale, shape scale, state-layer
// opacities, motion durations and helper functions.
//
// The v0.2.x property names (text, textMuted, border, surfaceAlt, ...) are
// kept as aliases onto the MD3 roles so mixed/migrated code keeps working.
QtObject {
    readonly property bool dark: ThemeBridge.isDark

    // ---- MD3 color roles -----------------------------------------------------
    readonly property color primary: ThemeBridge.primary
    readonly property color onPrimary: ThemeBridge.onPrimary
    readonly property color primaryContainer: ThemeBridge.primaryContainer
    readonly property color onPrimaryContainer: ThemeBridge.onPrimaryContainer
    readonly property color secondary: ThemeBridge.secondary
    readonly property color onSecondary: ThemeBridge.onSecondary
    readonly property color secondaryContainer: ThemeBridge.secondaryContainer
    readonly property color onSecondaryContainer: ThemeBridge.onSecondaryContainer
    readonly property color tertiary: ThemeBridge.tertiary
    readonly property color onTertiary: ThemeBridge.onTertiary
    readonly property color tertiaryContainer: ThemeBridge.tertiaryContainer
    readonly property color onTertiaryContainer: ThemeBridge.onTertiaryContainer
    readonly property color inversePrimary: ThemeBridge.inversePrimary

    readonly property color error: ThemeBridge.error
    readonly property color onError: ThemeBridge.onError
    readonly property color errorContainer: ThemeBridge.errorContainer
    readonly property color onErrorContainer: ThemeBridge.onErrorContainer
    readonly property color success: ThemeBridge.success
    readonly property color onSuccess: ThemeBridge.onSuccess
    readonly property color successContainer: ThemeBridge.successContainer
    readonly property color onSuccessContainer: ThemeBridge.onSuccessContainer
    readonly property color warning: ThemeBridge.warning
    readonly property color onWarning: ThemeBridge.onWarning
    readonly property color warningContainer: ThemeBridge.warningContainer
    readonly property color onWarningContainer: ThemeBridge.onWarningContainer

    readonly property color background: ThemeBridge.background
    readonly property color surface: ThemeBridge.surface
    readonly property color surfaceDim: ThemeBridge.surfaceDim
    readonly property color surfaceBright: ThemeBridge.surfaceBright
    readonly property color surfaceContainerLowest: ThemeBridge.surfaceContainerLowest
    readonly property color surfaceContainerLow: ThemeBridge.surfaceContainerLow
    readonly property color surfaceContainer: ThemeBridge.surfaceContainer
    readonly property color surfaceContainerHigh: ThemeBridge.surfaceContainerHigh
    readonly property color surfaceContainerHighest: ThemeBridge.surfaceContainerHighest
    readonly property color onSurface: ThemeBridge.onSurface
    readonly property color onSurfaceVariant: ThemeBridge.onSurfaceVariant
    readonly property color outline: ThemeBridge.outline
    readonly property color outlineVariant: ThemeBridge.outlineVariant
    readonly property color inverseSurface: ThemeBridge.inverseSurface
    readonly property color inverseOnSurface: ThemeBridge.inverseOnSurface
    readonly property color scrim: ThemeBridge.scrim

    // ---- legacy aliases (v0.2.x names -> MD3 roles) ---------------------------
    readonly property color accent: ThemeBridge.accent
    readonly property color text: onSurface
    readonly property color textMuted: onSurfaceVariant
    readonly property color border: outlineVariant
    readonly property color surfaceAlt: surfaceContainer

    // interaction state-layer colors (MD3: on-surface at low opacity)
    readonly property color hover: Qt.rgba(onSurface.r, onSurface.g, onSurface.b, 0.08)
    readonly property color pressed: Qt.rgba(onSurface.r, onSurface.g, onSurface.b, 0.12)
    readonly property color accentSoft: Qt.rgba(primary.r, primary.g, primary.b, dark ? 0.22 : 0.14)

    // ---- MD3 shape scale ------------------------------------------------------
    readonly property int radiusXS: 4
    readonly property int radiusS: 8
    readonly property int radiusM: 12
    readonly property int radiusL: 16
    readonly property int radiusXL: 28
    readonly property int radiusFull: 999

    // ---- MD3 state-layer opacities --------------------------------------------
    readonly property real stateHover: 0.08
    readonly property real stateFocus: 0.10
    readonly property real statePress: 0.12
    readonly property real stateDrag: 0.16

    // ---- MD3 typography scale (desktop-calibrated) ----------------------------
    readonly property string fontIconOutlined: "Material Symbols Rounded"
    readonly property string fontIconFilled: "Material Symbols Rounded Fill"

    readonly property int typeDisplaySmall: 32
    readonly property int typeHeadlineMedium: 26
    readonly property int typeHeadlineSmall: 22
    readonly property int typeTitleLarge: 19
    readonly property int typeTitleMedium: 15
    readonly property int typeTitleSmall: 13
    readonly property int typeBodyLarge: 14
    readonly property int typeBodyMedium: 13
    readonly property int typeBodySmall: 12
    readonly property int typeLabelLarge: 13
    readonly property int typeLabelMedium: 11
    readonly property int typeLabelSmall: 10

    // ---- metrics ---------------------------------------------------------------
    readonly property int controlHeight: 40
    readonly property int controlHeightSmall: 34
    readonly property int tabHeight: 48
    readonly property int listHeight: 52
    readonly property int appBarHeight: 56
    readonly property int statusBarHeight: 32

    // ---- MD3 motion --------------------------------------------------------------
    readonly property int durFast: 120
    readonly property int durMed: 200
    readonly property int durLong: 300
    readonly property int easeStandard: Easing.OutCubic
    readonly property int easeEmphasized: Easing.OutCubic

    // ---- helpers -------------------------------------------------------------------
    function alpha(c, a) { return Qt.rgba(c.r, c.g, c.b, a); }
    // MD3 state layer over a base color
    function stateLayer(base, state) {
        const op = state === "hover" ? stateHover
                 : state === "press" ? statePress
                 : state === "focus" ? stateFocus : 0;
        return Qt.rgba(base.r, base.g, base.b, op);
    }
}
