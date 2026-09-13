// EclipseMD3 :: ToolTip — inverse surface, label typography
import QtQuick
import QtQuick.Templates as T

T.ToolTip {
    id: control

    padding: 8
    topPadding: 6
    bottomPadding: 6
    leftPadding: 12
    rightPadding: 12
    delay: 500
    timeout: 5000

    enter: Transition {
        NumberAnimation { property: "opacity"; from: 0; to: 1; duration: 120; easing.type: Easing.OutCubic }
    }
    exit: Transition {
        NumberAnimation { property: "opacity"; from: 1; to: 0; duration: 80 }
    }

    background: Rectangle {
        radius: 8
        color: ThemeBridge.inverseSurface
    }

    contentItem: Text {
        text: control.text
        font.pixelSize: 12
        wrapMode: Text.Wrap
        color: ThemeBridge.inverseOnSurface
        linkColor: ThemeBridge.inversePrimary
    }
}
