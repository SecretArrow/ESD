// EclipseMD3 :: Dialog — MD3 modal surface (28dp radius, container-high tone)
import QtQuick
import QtQuick.Templates as T

T.Dialog {
    id: control

    topPadding: 24
    bottomPadding: 20
    leftPadding: 24
    rightPadding: 24
    margins: 12

    font.pixelSize: 13

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 150; easing.type: Easing.OutCubic }
        NumberAnimation { property: "scale"; from: 0.96; to: 1.0; duration: 180; easing.type: Easing.OutCubic }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 100; easing.type: Easing.InCubic }
        NumberAnimation { property: "scale"; from: 1.0; to: 0.97; duration: 100; easing.type: Easing.InCubic }
    }

    background: Rectangle {
        radius: 28
        color: ThemeBridge.surfaceContainerHigh
        border.width: 1
        border.color: ThemeBridge.outlineVariant
    }
}
