// EclipseMD3 :: CheckBox — MD3 checkbox (20dp, radius 6, filled on check)
import QtQuick
import QtQuick.Controls.impl
import QtQuick.Templates as T

T.CheckBox {
    id: control

    implicitWidth: Math.max(implicitContentWidth + leftPadding + rightPadding, 96)
    implicitHeight: Math.max(implicitContentHeight, 32)

    spacing: 10
    padding: 4
    hoverEnabled: true
    font.pixelSize: 13

    indicator: Rectangle {
        implicitWidth: 20
        implicitHeight: 20
        x: control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        radius: 6
        color: {
            if (!control.enabled)
                return control.checked ? Qt.alpha(ThemeBridge.onSurface, 0.38)
                                       : "transparent";
            if (control.checked)
                return ThemeBridge.primary;
            if (control.pressed || control.hovered)
                return Qt.rgba(ThemeBridge.onSurface.r, ThemeBridge.onSurface.g,
                               ThemeBridge.onSurface.b, control.pressed ? 0.12 : 0.08);
            return "transparent";
        }
        border.width: control.checked ? 0 : 2
        border.color: control.enabled ? ThemeBridge.onSurfaceVariant
                                      : Qt.alpha(ThemeBridge.onSurface, 0.38)
        Behavior on color { ColorAnimation { duration: 120 } }
        Behavior on border.color { ColorAnimation { duration: 120 } }

        Text {
            anchors.centerIn: parent
            visible: control.checked
            text: String.fromCharCode(0xE5E8) // check (Material Symbols Rounded)
            font.family: "Material Symbols Rounded"
            font.pixelSize: 16
            color: control.enabled ? ThemeBridge.onPrimary : ThemeBridge.background
        }
    }

    contentItem: IconLabel {
        leftPadding: control.indicator ? control.spacing : 0
        text: control.text
        font: control.font
        color: control.enabled ? ThemeBridge.onSurface : Qt.alpha(ThemeBridge.onSurface, 0.38)
        alignment: Qt.AlignLeft | Qt.AlignVCenter
        display: control.display
    }
}
