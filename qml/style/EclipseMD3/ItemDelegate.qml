// EclipseMD3 :: ItemDelegate — rounded row with MD3 state layers;
// highlighted rows use the secondary container pill (MD3 navigation).
import QtQuick
import QtQuick.Controls.impl
import QtQuick.Templates as T

T.ItemDelegate {
    id: control

    implicitWidth: Math.max(implicitContentWidth + leftPadding + rightPadding, 64)
    implicitHeight: Math.max(implicitContentHeight + topPadding + bottomPadding, 44)

    padding: 8
    leftPadding: 12
    rightPadding: 12
    spacing: 10
    hoverEnabled: true
    font.pixelSize: 13

    icon.width: 20
    icon.height: 20
    icon.color: control.enabled ? (control.highlighted ? ThemeBridge.onSecondaryContainer
                                                       : ThemeBridge.onSurfaceVariant)
                                : Qt.alpha(ThemeBridge.onSurface, 0.38)

    contentItem: IconLabel {
        text: control.text
        font: control.font
        icon.name: control.icon.name
        icon.source: control.icon.source
        icon.width: control.icon.width
        icon.height: control.icon.height
        icon.color: control.icon.color
        color: control.enabled ? (control.highlighted ? ThemeBridge.onSecondaryContainer
                                                      : ThemeBridge.onSurface)
                               : Qt.alpha(ThemeBridge.onSurface, 0.38)
        spacing: control.spacing
        display: control.display
        alignment: Qt.AlignLeft | Qt.AlignVCenter
    }

    background: Rectangle {
        radius: 10
        color: {
            if (!control.enabled)
                return "transparent";
            if (control.pressed)
                return Qt.rgba(ThemeBridge.onSurface.r, ThemeBridge.onSurface.g, ThemeBridge.onSurface.b, 0.12);
            if (control.highlighted)
                return ThemeBridge.secondaryContainer;
            if (control.hovered)
                return Qt.rgba(ThemeBridge.onSurface.r, ThemeBridge.onSurface.g, ThemeBridge.onSurface.b, 0.08);
            return "transparent";
        }
        Behavior on color { ColorAnimation { duration: 120 } }
    }
}
