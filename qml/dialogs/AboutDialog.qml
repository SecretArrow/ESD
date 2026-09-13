import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Module import: gives access to the Theme singleton (same-module types in
// subdirectories are not visible without it - binding errors otherwise).
import Eclipse

Dialog {
    title: qsTr("About")
    modal: true
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 460
    standardButtons: Dialog.Close
    contentItem: ColumnLayout {
        spacing: 16

        RowLayout {
            spacing: 12
            Layout.fillWidth: true

            // App badge (MD3: container-colored rounded avatar)
            Rectangle {
                width: 44
                height: 44
                radius: Theme.radiusM
                color: Theme.primaryContainer
                MaterialIcon {
                    anchors.centerIn: parent
                    icon: "terminal"
                    iconSize: 24
                    color: Theme.onPrimaryContainer
                }
            }
            Label {
                text: qsTr("Eclipse SSH Desktop %1").arg(App.version)
                font.pixelSize: Theme.typeTitleLarge
                font.weight: Font.DemiBold
                color: Theme.onSurface
                Layout.fillWidth: true
            }
        }

        Label {
            text: qsTr("A modern, native SSH/SFTP client.\nSimple by default, powerful when needed.\n\nMIT licensed. Built with Qt 6, libssh, libssh2, libvterm.")
            color: Theme.onSurfaceVariant
            font.pixelSize: Theme.typeBodyMedium
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }
    }
}
