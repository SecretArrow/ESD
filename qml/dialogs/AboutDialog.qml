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
    standardButtons: Dialog.Close
    contentItem: ColumnLayout {
        spacing: 8
        Label { text: qsTr("Eclipse SSH Desktop %1").arg(App.version); font.pixelSize: 18; font.weight: Font.DemiBold; color: Theme.text }
        Label { text: qsTr("A modern, native SSH/SFTP client.\nSimple by default, powerful when needed.\n\nMIT licensed. Built with Qt 6, libssh, libssh2, libvterm.")
                color: Theme.textMuted; font.pixelSize: 12 }
    }
}
