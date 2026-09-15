// EclipseMD3 :: ProgressBar — 4dp rounded bar, animated when indeterminate
import QtQuick
import QtQuick.Templates as T

T.ProgressBar {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth, 120)
    implicitHeight: 4

    background: Rectangle {
        implicitHeight: 4
        radius: 2
        color: ThemeBridge.surfaceContainerHighest
        anchors.verticalCenter: parent.verticalCenter
    }

    contentItem: Item {
        anchors.verticalCenter: parent.verticalCenter

        Rectangle {
            visible: !control.indeterminate
            width: control.position * control.width
            height: 4
            radius: 2
            color: ThemeBridge.primary
            Behavior on width { NumberAnimation { duration: 120 } }
        }

        Rectangle {
            id: indeterminateBar
            visible: control.indeterminate
            width: control.width * 0.4
            height: 4
            radius: 2
            color: ThemeBridge.primary
            x: (control.width - width) * (0.5 + 0.5 * Math.sin(wobble.value * Math.PI))
            SequentialAnimation on width {
                running: control.indeterminate && control.visible
                loops: Animation.Infinite
                NumberAnimation { to: control.width * 0.65; duration: 700; easing.type: Easing.InOutQuad }
                NumberAnimation { to: control.width * 0.35; duration: 700; easing.type: Easing.InOutQuad }
            }
            Timer {
                id: wobble
                property real value: 0
                running: control.indeterminate && control.visible
                repeat: true
                interval: 40
                onTriggered: value = (value + 0.05) % 1
            }
        }
    }
}
