import QtQuick
import Eclipse

// Small MD3 status dot for a session state string
// (SessionState names from SshWorker::sessionStateName).
// Pulsing while busy (Connecting/Authenticating/...), red on failure.
Rectangle {
    id: dot

    // NOTE: must not be named `state` - that would clash with Item.state
    // (duplicate property name = hard QML error, type becomes unavailable).
    property string sessionState: "Idle"

    readonly property bool busy: sessionState === "Connecting" || sessionState === "Authenticating"
                                 || sessionState === "Resolving" || sessionState === "WaitingHostKey"
                                 || sessionState === "Disconnecting" || sessionState === "Reconnecting"
    readonly property bool bad: sessionState === "Error" || sessionState === "AuthFailed"
                                || sessionState === "HostKeyRejected"

    readonly property color dotColor: sessionState === "Connected" ? Theme.success
                                    : busy ? Theme.warning
                                    : bad ? Theme.error
                                    : Theme.outline

    width: 10
    height: 10
    radius: width / 2
    color: dotColor
    border.width: 1
    border.color: Theme.alpha(dotColor, 0.35)

    Behavior on color { ColorAnimation { duration: 220 } }
    Behavior on border.color { ColorAnimation { duration: 220 } }

    SequentialAnimation on opacity {
        running: dot.busy && dot.visible
        loops: Animation.Infinite
        alwaysRunToEnd: true
        NumberAnimation { from: 1.0; to: 0.3; duration: 600; easing.type: Easing.InOutQuad }
        NumberAnimation { from: 0.3; to: 1.0; duration: 600; easing.type: Easing.InOutQuad }
    }
}
