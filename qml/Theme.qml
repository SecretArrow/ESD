pragma Singleton
import QtQuick

// Application-wide theme colors driven by ThemeManager (C++).
QtObject {
    readonly property bool dark: ThemeBridge.isDark
    readonly property color background: dark ? "#14171e" : "#f5f7fa"
    readonly property color surface: dark ? "#1a1e27" : "#ffffff"
    readonly property color surfaceAlt: dark ? "#202530" : "#f0f2f6"
    readonly property color border: dark ? "#2a303c" : "#dee2e8"
    readonly property color text: dark ? "#e6e8ee" : "#1b1f27"
    readonly property color textMuted: dark ? "#8b93a5" : "#6b7382"
    readonly property color accent: ThemeBridge.accent
    readonly property color success: dark ? "#3fb66f" : "#2f9e5f"
    readonly property color warning: dark ? "#e2b12c" : "#c4920f"
    readonly property color error: dark ? "#e25d5d" : "#d44747"

    // ---- UX polish tokens (v0.2.4) ----------------------------------------
    // translucent accent fill for soft highlights (selection, banners)
    readonly property color accentSoft: Qt.rgba(accent.r, accent.g, accent.b, dark ? 0.22 : 0.14)
    // interaction states
    readonly property color hover: dark ? "#262d3b" : "#e9edf3"
    readonly property color pressed: dark ? "#2d3547" : "#dde3ec"
    // corner radius scale
    readonly property int radiusS: 6
    readonly property int radiusM: 10
    readonly property int radiusL: 14
    // standard control heights
    readonly property int controlHeight: 34
    readonly property int tabHeight: 40
}
