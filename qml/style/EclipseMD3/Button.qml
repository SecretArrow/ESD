// EclipseMD3 :: Button — MD3 text button (default) with state layers.
// Primary CTAs in the app use the custom FlatButton (filled/tonal variants).
import QtQuick
import QtQuick.Controls.impl
import QtQuick.Templates as T

T.Button {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding, 40)

    leftPadding: 16
    rightPadding: 16
    spacing: 8
    hoverEnabled: true
    font.pixelSize: 13
    font.weight: Font.Medium

    contentItem: IconLabel {
        text: control.text
        font: control.font
        icon.name: control.icon.name
        icon.source: control.icon.source
        icon.width: control.icon.width
        icon.height: control.icon.height
        color: control.enabled ? ThemeBridge.primary : Qt.alpha(ThemeBridge.onSurface, 0.38)
        spacing: control.spacing
        display: control.display
        alignment: Qt.AlignHCenter | Qt.AlignVCenter
    }

    background: Rectangle {
        radius: height / 2
        implicitWidth: 64
        color: {
            if (!control.enabled)
                return "transparent";
            if (control.pressed)
                return Qt.rgba(ThemeBridge.primary.r, ThemeBridge.primary.g, ThemeBridge.primary.b, 0.12);
            if (control.hovered)
                return Qt.rgba(ThemeBridge.primary.r, ThemeBridge.primary.g, ThemeBridge.primary.b, 0.08);
            return "transparent";
        }
        Behavior on color { ColorAnimation { duration: 120 } }
    }
}
