// EclipseMD3 :: Menu — MD3 container menu
import QtQuick
import QtQuick.Templates as T

T.Menu {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            contentWidth + leftPadding + rightPadding, 200)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             contentHeight + topPadding + bottomPadding)

    topPadding: 8
    bottomPadding: 8
    leftPadding: 8
    rightPadding: 8
    margins: 8
    overlap: 4

    background: Rectangle {
        radius: 12
        color: ThemeBridge.surfaceContainerHigh
        border.width: 1
        border.color: ThemeBridge.outlineVariant
    }

    contentItem: ListView {
        clip: true
        implicitHeight: contentHeight
        model: control.contentModel
        currentIndex: control.currentIndex
        spacing: 2
    }
}
