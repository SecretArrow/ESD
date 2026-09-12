import QtQuick
import QtQuick.Controls
import Eclipse

// Modern flat button with animated hover/press feedback.
//   glyph  : optional leading unicode glyph (e.g. "＋", "⏻")
//   accent : filled with the accent color (primary action)
//   danger : red-tinted destructive action
//   showBorder: draw a subtle border on idle state
Button {
    id: control

    property string glyph: ""
    property bool accent: false
    property bool danger: false
    property bool showBorder: false

    font.pixelSize: 13

    hoverEnabled: true
    leftPadding: 12
    rightPadding: 12
    implicitHeight: Theme.controlHeight
    implicitWidth: leftPadding + rightPadding + contentRow.implicitWidth
    opacity: enabled ? 1.0 : 0.45

    Behavior on opacity { NumberAnimation { duration: 150 } }

    contentItem: Row {
        id: contentRow
        spacing: 6
        Label {
            text: control.glyph
            visible: control.glyph.length > 0
            font.pixelSize: control.font.pixelSize
            color: control.accent ? "#ffffff" : (control.danger ? Theme.error : Theme.accent)
            anchors.verticalCenter: parent.verticalCenter
        }
        Label {
            text: control.text
            font: control.font
            color: control.accent ? "#ffffff" : (control.danger ? Theme.error : Theme.text)
            anchors.verticalCenter: parent.verticalCenter
            Behavior on color { ColorAnimation { duration: 150 } }
        }
    }

    background: Rectangle {
        radius: Theme.radiusS
        implicitWidth: 64
        color: {
            if (control.accent) {
                if (!control.enabled) return Theme.accent;
                if (control.pressed) return Qt.darker(Theme.accent, 1.18);
                if (control.hovered) return Qt.lighter(Theme.accent, 1.10);
                return Theme.accent;
            }
            if (control.pressed) return Theme.pressed;
            if (control.hovered) return control.danger ? Qt.rgba(Theme.error.r, Theme.error.g, Theme.error.b, 0.12) : Theme.hover;
            return "transparent";
        }
        border.width: control.showBorder && !control.accent ? 1 : 0
        border.color: control.danger ? Qt.rgba(Theme.error.r, Theme.error.g, Theme.error.b, 0.5) : Theme.border

        Behavior on color { ColorAnimation { duration: 130 } }
        Behavior on border.color { ColorAnimation { duration: 130 } }

        // soft press-scale micro interaction
        scale: control.pressed ? 0.97 : 1.0
        Behavior on scale { NumberAnimation { duration: 90; easing.type: Easing.OutQuad } }
    }

    ToolTip.visible: control.ToolTip.text.length > 0 && control.hovered
    ToolTip.delay: 550
}
