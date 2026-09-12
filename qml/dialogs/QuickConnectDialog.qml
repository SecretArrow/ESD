import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dlg
    title: qsTr("Quick Connect")
    modal: true
    parent: Overlay.overlay
    anchors.centerIn: parent
    standardButtons: Dialog.Ok | Dialog.Cancel

    property bool saveProfile: false

    ColumnLayout {
        spacing: 8
        GridLayout {
            columns: 2
            Label { text: qsTr("Host"); color: Theme.text }
            TextField { id: hostField; Layout.fillWidth: true; placeholderText: "server.example.com" }
            Label { text: qsTr("Port"); color: Theme.text }
            TextField { id: portField; text: "22"; Layout.fillWidth: true }
            Label { text: qsTr("Username"); color: Theme.text }
            TextField { id: userField; Layout.fillWidth: true; placeholderText: "root" }
            Label { text: qsTr("Authentication"); color: Theme.text }
            ComboBox { id: authBox; model: ["password", "agent", "publickey", "keyboard-interactive"]; Layout.fillWidth: true }
            Label { text: qsTr("Password"); color: Theme.text; visible: authBox.currentText === "password" }
            TextField { id: passField; echoMode: TextInput.Password; Layout.fillWidth: true
                        visible: authBox.currentText === "password" }
        }
        CheckBox { id: saveBox; text: qsTr("Save as profile"); onCheckedChanged: saveProfile = checked }
        TextField { id: nameField; placeholderText: qsTr("Profile name (optional)"); visible: saveBox.checked; Layout.fillWidth: true }
    }
    function openDialog() { open() }
    onAccepted: {
        App.quickConnect({
            host: hostField.text, port: parseInt(portField.text || "22"),
            username: userField.text, authMethod: authBox.currentText,
            password: passField.text, saveAsProfile: saveBox.checked,
            profileName: nameField.text
        });
    }
}
