// EclipseMD3 :: RadioButton — MD3 radio (20dp ring, filled dot on check)
import QtQuick
import QtQuick.Controls.impl
import QtQuick.Templates as T

T.RadioButton {
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
        radius: width / 2
        color: "transparent"
        border.width: control.checked && control.enabled ? 2 : 1.6
        border.color: {
            if (!control.enabled)
                return Qt.alpha(ThemeBridge.onSurface, 0.38);
            if (control.checked)
                return ThemeBridge.primary;
            if (control.hovered || control.pressed)
                return ThemeBridge.onSurface;
            return ThemeBridge.onSurfaceVariant;
        }
        Behavior on border.color { ColorAnimation { duration: 120 } }

        Rectangle {
            anchors.centerIn: parent
            width: control.checked ? 10 : 0
            height: width
            radius: width / 2
            color: control.enabled ? ThemeBridge.primary : Qt.alpha(ThemeBridge.onSurface, 0.38)
            Behavior on width { NumberAnimation { duration: 120; easing.type: Easing.OutCubic } }
            Behavior on color { ColorAnimation { duration: 120 } }
        }
    }

    contentItem: IconLabel {
        leftPadding: control.indicator ? control.spacing : 0
        text: control.text
        font: control.font
        color: control.enabled ? ThemeBridge.onSurface : Qt.alpha(ThemeBridge.onSurface, 0.38)
        verticalAlignment: Text.AlignVCenter
        display: control.display
    }
}
