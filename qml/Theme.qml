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
}
