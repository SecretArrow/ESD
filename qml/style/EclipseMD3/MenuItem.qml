// EclipseMD3 :: MenuItem — rounded 44dp row with state layer
import QtQuick
import QtQuick.Controls.impl
import QtQuick.Templates as T

T.MenuItem {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset, 44)

    leftPadding: 12
    rightPadding: 12
    topPadding: 4
    bottomPadding: 4
    spacing: 10
    hoverEnabled: true
    font.pixelSize: 13

    arrow: Text {
        x: control.mirrored ? control.leftPadding
                            : control.width - width - control.rightPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        visible: control.subMenu
        text: String.fromCharCode(0xE315) // keyboard_arrow_right
        font.family: "Material Symbols Rounded"
        font.pixelSize: 20
        color: ThemeBridge.onSurfaceVariant
        verticalAlignment: Text.AlignVCenter
        horizontalAlignment: Text.AlignHCenter
    }

    indicator: Text {
        x: control.mirrored ? control.width - width - control.rightPadding : control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        visible: control.checked
        text: String.fromCharCode(0xE5E8) // check
        font.family: "Material Symbols Rounded"
        font.pixelSize: 18
        color: ThemeBridge.primary
        verticalAlignment: Text.AlignVCenter
        horizontalAlignment: Text.AlignHCenter
    }

    contentItem: IconLabel {
        readonly property real arrowWidth: control.arrow && control.arrow.visible ? control.arrow.width : 0
        readonly property real indicatorWidth: control.indicator && control.indicator.visible ? control.indicator.width : 0
        leftPadding: !control.mirrored ? indicatorWidth : 0
        rightPadding: control.mirrored ? 0 : arrowWidth
        text: control.text
        font: control.font
        color: control.enabled ? ThemeBridge.onSurface : Qt.alpha(ThemeBridge.onSurface, 0.38)
        spacing: control.spacing
        display: control.display
        alignment: Qt.AlignLeft | Qt.AlignVCenter
        elide: Text.ElideRight
    }

    background: Rectangle {
        radius: 8
        color: {
            if (!control.enabled)
                return "transparent";
            if (control.pressed)
                return Qt.rgba(ThemeBridge.onSurface.r, ThemeBridge.onSurface.g, ThemeBridge.onSurface.b, 0.12);
            if (control.highlighted || control.hovered)
                return Qt.rgba(ThemeBridge.onSurface.r, ThemeBridge.onSurface.g, ThemeBridge.onSurface.b, 0.08);
            return "transparent";
        }
        Behavior on color { ColorAnimation { duration: 100 } }
    }
}
