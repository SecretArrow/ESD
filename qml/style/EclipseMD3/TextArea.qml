// EclipseMD3 :: TextArea — MD3 outlined multiline field
import QtQuick
import QtQuick.Templates as T

T.TextArea {
    id: control

    implicitWidth: implicitBackgroundWidth + leftPadding + rightPadding || 200
    implicitHeight: Math.max(contentHeight + topPadding + bottomPadding, 72)

    leftPadding: padding + 2
    rightPadding: padding + 2
    topPadding: 10
    bottomPadding: 10
    padding: 10

    color: enabled ? ThemeBridge.onSurface : Qt.alpha(ThemeBridge.onSurface, 0.38)
    placeholderTextColor: ThemeBridge.onSurfaceVariant
    selectionColor: ThemeBridge.primary
    selectedTextColor: ThemeBridge.onPrimary
    font.pixelSize: 13

    background: Rectangle {
        radius: 12
        color: control.enabled && control.activeFocus
               ? ThemeBridge.surfaceContainerLowest : "transparent"
        border.width: control.enabled && control.activeFocus ? 2 : 1
        border.color: control.enabled && control.activeFocus
                      ? ThemeBridge.primary : ThemeBridge.outline
        Behavior on border.color { ColorAnimation { duration: 120 } }
    }
}
