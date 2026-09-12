import QtQuick
import Eclipse

// Small colored status dot for a session state string
// (SessionState names from SshWorker::sessionStateName).
// Pulsing while busy (Connecting/Authenticating/...), red on failure.
Rectangle {
    id: dot

    property string state: "Idle"

    readonly property bool busy: state === "Connecting" || state === "Authenticating"
                                 || state === "Resolving" || state === "WaitingHostKey"
                                 || state === "Disconnecting" || state === "Reconnecting"
    readonly property bool bad: state === "Error" || state === "AuthFailed"
                                || state === "HostKeyRejected"

    width: 9
    height: 9
    radius: width / 2
    color: state === "Connected" ? Theme.success
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
