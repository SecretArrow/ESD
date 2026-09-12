import QtQuick
import Eclipse

// Small colored status dot for a session state string
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

    width: 9
    height: 9
    radius: width / 2
    color: sessionState === "Connected" ? Theme.success
         : busy ? Theme.warning
         : bad ? Theme.error
         : Theme.textMuted

    Behavior on color { ColorAnimation { duration: 220 } }

    SequentialAnimation on opacity {
        running: dot.busy && dot.visible
        loops: Animation.Infinite
        alwaysRunToEnd: true
        NumberAnimation { from: 1.0; to: 0.3; duration: 600; easing.type: Easing.InOutQuad }
        NumberAnimation { from: 0.3; to: 1.0; duration: 600; easing.type: Easing.InOutQuad }
    }
}
