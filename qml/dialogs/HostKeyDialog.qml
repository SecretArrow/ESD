import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Module import: gives access to the Theme singleton (same-module types in
// subdirectories are not visible without it - binding errors otherwise).
import Eclipse

// Unknown / changed host key verification - security critical.
Dialog {
    id: dlg
    property var keyInfo: ({})
    property bool isChangedKey: false
    signal decided(bool accepted, bool save)
    title: isChangedKey ? qsTr("WARNING: Server identity changed") : qsTr("Unknown host")
    modal: true
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 480
    standardButtons: Dialog.NoButton

    ColumnLayout {
        spacing: 10
        Rectangle {
            Layout.fillWidth: true
            height: changedLabel.implicitHeight + 20
            color: dlg.isChangedKey ? "#3a2020" : "#20301a"
            radius: 8
            Label {
                id: changedLabel
                anchors.centerIn: parent
                width: parent.width - 20
                wrapMode: Text.Wrap
                font.pixelSize: 12
                color: dlg.isChangedKey ? "#ff9d9d" : "#a6d189"
                text: dlg.isChangedKey
                    ? qsTr("The server presented a DIFFERENT key than before. This may indicate a server reinstall — or a man-in-the-middle attack. Do not continue unless you verified the change.")
                    : qsTr("This is the first time you connect to this server. Verify the fingerprint out-of-band before trusting it.")
            }
        }
        GridLayout {
            columns: 2
            Label { text: qsTr("Host"); color: Theme.textMuted }
            Label { text: keyInfo.host + ":" + keyInfo.port; color: Theme.text; font.family: "monospace" }
            Label { text: qsTr("Key type"); color: Theme.textMuted }
            Label { text: keyInfo.keyType; color: Theme.text; font.family: "monospace" }
            Label { text: qsTr("SHA256"); color: Theme.textMuted }
            Label { text: keyInfo.sha256; color: Theme.text; font.family: "monospace";
                    Layout.maximumWidth: 320; wrapMode: Text.WrapAnywhere }
            Label { text: qsTr("MD5"); color: Theme.textMuted }
            Label { text: keyInfo.md5; color: Theme.textMuted; font.family: "monospace"; font.pixelSize: 10 }
        }
    }
    footer: DialogButtonBox {
        Button { text: qsTr("Cancel"); flat: true
            onClicked: { dlg.decided(false, false); dlg.reject() } }
        Button { text: qsTr("Trust once"); flat: true
            onClicked: { dlg.decided(true, false); dlg.accept() } }
        Button { text: qsTr("Trust & Save"); highlighted: !dlg.isChangedKey; enabled: !dlg.isChangedKey
            ToolTip.text: qsTr("Saving changed keys is disabled for your safety"); ToolTip.visible: hovered
            onClicked: { dlg.decided(true, true); dlg.accept() } }
    }
    onRejected: { dlg.decided(false, false) }
}
