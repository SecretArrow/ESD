import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Module import: gives access to the Theme singleton (same-module types in
// subdirectories are not visible without it - binding errors otherwise).
import Eclipse

Dialog {
    id: dlg
    title: qsTr("Quick Connect")
    modal: true
    parent: Overlay.overlay
    anchors.centerIn: parent
    standardButtons: Dialog.Ok | Dialog.Cancel

    property bool saveProfile: false

    ColumnLayout {
        spacing: 12

        RowLayout {
            spacing: 12
            Layout.fillWidth: true
            Rectangle {
                width: 44
                height: 44
                radius: Theme.radiusM
                color: Theme.primaryContainer
                MaterialIcon {
                    anchors.centerIn: parent
                    icon: "bolt"
                    iconSize: 24
                    color: Theme.onPrimaryContainer
                }
            }
            Label {
                text: qsTr("Connect to a server without saving the details")
                color: Theme.onSurfaceVariant
                font.pixelSize: Theme.typeBodyMedium
                wrapMode: Text.Wrap
                Layout.fillWidth: true
            }
        }

        GridLayout {
            columns: 2
            columnSpacing: 12
            rowSpacing: 10

            RowLayout {
                spacing: 6
                MaterialIcon { icon: "dns"; iconSize: 16; color: Theme.onSurfaceVariant }
                Label { text: qsTr("Host"); color: Theme.onSurface; font.pixelSize: Theme.typeBodyMedium }
            }
            TextField { id: hostField; Layout.fillWidth: true; placeholderText: "server.example.com" }

            RowLayout {
                spacing: 6
                MaterialIcon { icon: "settings_ethernet"; iconSize: 16; color: Theme.onSurfaceVariant }
                Label { text: qsTr("Port"); color: Theme.onSurface; font.pixelSize: Theme.typeBodyMedium }
            }
            TextField { id: portField; text: "22"; Layout.fillWidth: true }

            RowLayout {
                spacing: 6
                MaterialIcon { icon: "person"; iconSize: 16; color: Theme.onSurfaceVariant }
                Label { text: qsTr("Username"); color: Theme.onSurface; font.pixelSize: Theme.typeBodyMedium }
            }
            TextField { id: userField; Layout.fillWidth: true; placeholderText: "root" }

            RowLayout {
                spacing: 6
                MaterialIcon { icon: "key"; iconSize: 16; color: Theme.onSurfaceVariant }
                Label { text: qsTr("Authentication"); color: Theme.onSurface; font.pixelSize: Theme.typeBodyMedium }
            }
            ComboBox { id: authBox; model: ["password", "agent", "publickey", "keyboard-interactive"]; Layout.fillWidth: true }

            RowLayout {
                spacing: 6
                visible: authBox.currentText === "password"
                MaterialIcon { icon: "password"; iconSize: 16; color: Theme.onSurfaceVariant }
                Label { text: qsTr("Password"); color: Theme.onSurface; font.pixelSize: Theme.typeBodyMedium; visible: authBox.currentText === "password" }
            }
            TextField { id: passField; echoMode: TextInput.Password; Layout.fillWidth: true
                        visible: authBox.currentText === "password" }
        }

        CheckBox { id: saveBox; text: qsTr("Save as profile"); onCheckedChanged: saveProfile = checked }
        TextField {
            id: nameField
            placeholderText: qsTr("Profile name (optional)")
            visible: saveBox.checked
            Layout.fillWidth: true
        }
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
