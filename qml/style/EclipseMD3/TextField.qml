// EclipseMD3 :: TextField — MD3 outlined text field
// 1px outline idle, 2px primary on focus, 12dp corner radius.
import QtQuick
import QtQuick.Templates as T

T.TextField {
    id: control

    implicitWidth: implicitBackgroundWidth + leftPadding + rightPadding
                   || 140
    implicitHeight: Math.max(implicitBackgroundHeight + topPadding + bottomPadding,
                             contentHeight + topPadding + bottomPadding, 40)

    leftPadding: padding + 2
    rightPadding: padding + 2
    topPadding: 8
    bottomPadding: 8
    padding: 8

    color: enabled ? ThemeBridge.onSurface : Qt.alpha(ThemeBridge.onSurface, 0.38)
    placeholderTextColor: ThemeBridge.onSurfaceVariant
    selectionColor: ThemeBridge.primary
    selectedTextColor: ThemeBridge.onPrimary
    verticalAlignment: TextInput.AlignVCenter
    font.pixelSize: 13

    background: Rectangle {
        radius: 12
        color: control.enabled && control.activeFocus
               ? ThemeBridge.surfaceContainerLowest : "transparent"
        border.width: control.enabled && control.activeFocus ? 2 : 1
        border.color: control.enabled && control.activeFocus
                      ? ThemeBridge.primary : ThemeBridge.outline
        Behavior on border.color { ColorAnimation { duration: 120 } }
        Behavior on color { ColorAnimation { duration: 120 } }
    }
}
