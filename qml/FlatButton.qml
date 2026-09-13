import QtQuick
import QtQuick.Controls
import Eclipse

// Material Design 3 button with the four emphasized variants plus a legacy
// compatibility mapping from the v0.2.x boolean properties:
//
//   variant: "filled"   -> primary fill, on-primary label   (CTA)
//            "tonal"    -> secondary container fill         (soft action)
//            "outlined" -> 1px outline, primary label
//            "text"     -> plain primary label + state layer
//
//   accent: true  -> "filled"          (legacy)
//   accent: false + showBorder -> "outlined"      (legacy)
//   danger: true  -> destructive colors on the chosen variant
//
//   iconName: leading Material Symbols glyph
//   small:    compact 34dp height
Button {
    id: control

    property string iconName: ""
    property string variant: "text"
    property bool accent: false
    property bool danger: false
    property bool showBorder: false
    property bool small: false

    // ---- resolve variant (legacy props win for back-compat) -----------------
    readonly property bool isFilled: accent || variant === "filled"
    readonly property bool isTonal: !accent && !showBorder && variant === "tonal"
    readonly property bool isOutlined: !accent && (showBorder || variant === "outlined")
    readonly property bool isDanger: danger

    // ---- MD3 color resolution ------------------------------------------------
    readonly property color bg: {
        if (isDanger)
            return isFilled ? Theme.error
                 : isOutlined ? "transparent"
                 : Theme.errorContainer;
        if (isFilled) return Theme.primary;
        if (isTonal) return Theme.secondaryContainer;
        return "transparent";
    }
    readonly property color fg: {
        if (isDanger)
            return isFilled ? Theme.onError
                 : isOutlined ? Theme.error
                 : Theme.onErrorContainer;
        if (isFilled) return Theme.onPrimary;
        if (isTonal) return Theme.onSecondaryContainer;
        return Theme.primary;
    }
    readonly property color borderColor: isDanger ? Theme.alpha(Theme.error, 0.5) : Theme.outline

    readonly property int btnHeight: small ? Theme.controlHeightSmall : Theme.controlHeight

    font.pixelSize: 13
    font.weight: isFilled || isTonal ? Font.Medium : Font.Normal

    hoverEnabled: true
    leftPadding: 16
    rightPadding: 16
    implicitHeight: btnHeight
    implicitWidth: leftPadding + rightPadding + contentRow.implicitWidth
    opacity: enabled ? 1.0 : 0.45
    Behavior on opacity { NumberAnimation { duration: Theme.durFast } }

    contentItem: Row {
        id: contentRow
        spacing: 8

        MaterialIcon {
            icon: control.iconName
            iconSize: control.small ? 18 : 20
            color: control.fg
            visible: control.iconName.length > 0
            anchors.verticalCenter: parent.verticalCenter
        }
        Label {
            text: control.text
            font: control.font
            color: control.fg
            anchors.verticalCenter: parent.verticalCenter
            Behavior on color { ColorAnimation { duration: Theme.durFast } }
        }
    }

    background: Rectangle {
        radius: Theme.radiusFull >= 999 ? control.height / 2 : Theme.radiusFull
        implicitWidth: 64
        color: {
            if (!control.enabled)
                return control.bg;
            if (control.isFilled && !control.isDanger) {
                if (control.pressed) return Qt.darker(Theme.primary, 1.15);
                if (control.hovered) return Qt.lighter(Theme.primary, 1.08);
                return Theme.primary;
            }
            if (control.pressed) {
                if (control.isDanger && control.isFilled) return Qt.darker(Theme.error, 1.15);
                return Theme.alpha(control.bg, 1.0);
            }
            if (control.hovered) {
                if (control.isTonal) return Qt.lighter(Theme.secondaryContainer, 1.06);
                if (control.isDanger) return Theme.alpha(Theme.errorContainer, 1.0);
                return Theme.alpha(Theme.primary, Theme.stateHover);
            }
            return control.bg;
        }
        border.width: control.isOutlined ? 1 : 0
        border.color: control.borderColor
        Behavior on color { ColorAnimation { duration: Theme.durFast } }
        Behavior on border.color { ColorAnimation { duration: Theme.durFast } }

        // MD3 press micro-interaction
        scale: control.pressed ? 0.97 : 1.0
        Behavior on scale { NumberAnimation { duration: 90; easing.type: Easing.OutQuad } }
    }

    ToolTip.visible: control.ToolTip.text.length > 0 && control.hovered
    ToolTip.delay: 550
}
