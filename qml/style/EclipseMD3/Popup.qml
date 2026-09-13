// EclipseMD3 :: Popup — generic tonal popup surface (menus/dialogs override)
import QtQuick
import QtQuick.Templates as T

T.Popup {
    topPadding: 8
    bottomPadding: 8
    leftPadding: 8
    rightPadding: 8
    margins: 8

    background: Rectangle {
        radius: 12
        color: ThemeBridge.surfaceContainerHigh
        border.width: 1
        border.color: ThemeBridge.outlineVariant
    }

    contentItem: Item {}
}
