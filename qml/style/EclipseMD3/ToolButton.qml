// EclipseMD3 :: ToolButton — circular icon button with MD3 state layer
import QtQuick
import QtQuick.Controls.impl
import QtQuick.Templates as T

T.ToolButton {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding, 36)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding, 36)

    padding: 6
    spacing: 4
    hoverEnabled: true
    font.pixelSize: 13

    contentItem: IconLabel {
        text: control.text
        font: control.font
        icon.name: control.icon.name
        icon.source: control.icon.source
        icon.width: control.icon.width > 0 ? control.icon.width : 20
        icon.height: control.icon.height > 0 ? control.icon.height : 20
        color: control.enabled ? ThemeBridge.onSurfaceVariant : Qt.alpha(ThemeBridge.onSurface, 0.38)
        spacing: control.spacing
        display: control.display
        alignment: Qt.AlignHCenter | Qt.AlignVCenter
    }

    background: Rectangle {
        radius: width / 2
        implicitWidth: 36
        implicitHeight: 36
        color: {
            if (!control.enabled)
                return "transparent";
            if (control.pressed)
                return Qt.rgba(ThemeBridge.onSurface.r, ThemeBridge.onSurface.g, ThemeBridge.onSurface.b, 0.12);
            if (control.hovered)
                return Qt.rgba(ThemeBridge.onSurface.r, ThemeBridge.onSurface.g, ThemeBridge.onSurface.b, 0.08);
            return "transparent";
        }
        Behavior on color { ColorAnimation { duration: 120 } }
    }
}
