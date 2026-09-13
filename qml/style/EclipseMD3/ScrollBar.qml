// EclipseMD3 :: ScrollBar — slim rounded thumb, expands on hover
import QtQuick
import QtQuick.Templates as T

T.ScrollBar {
    id: control

    implicitWidth: control.horizontal ? 8 : 10
    implicitHeight: control.horizontal ? 10 : 8

    contentItem: Rectangle {
        implicitWidth: control.interactive ? 10 : 8
        implicitHeight: control.interactive ? 10 : 8
        radius: width / 2
        color: control.pressed
               ? Qt.rgba(ThemeBridge.onSurface.r, ThemeBridge.onSurface.g, ThemeBridge.onSurface.b, 0.46)
               : control.hovered || control.interactive
                 ? Qt.rgba(ThemeBridge.onSurface.r, ThemeBridge.onSurface.g, ThemeBridge.onSurface.b, 0.34)
                 : Qt.rgba(ThemeBridge.onSurface.r, ThemeBridge.onSurface.g, ThemeBridge.onSurface.b, 0.22)
        Behavior on color { ColorAnimation { duration: 120 } }
        opacity: control.policy === T.ScrollBar.AlwaysOn || control.active ? 1.0 : 0.35
        Behavior on opacity { NumberAnimation { duration: 150 } }
    }

    background: Rectangle {
        visible: control.interactive
        color: "transparent"
    }
}
