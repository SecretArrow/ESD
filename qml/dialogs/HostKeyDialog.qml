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
        spacing: 14

        // Security notice (MD3: tonal error/success container)
        Rectangle {
            Layout.fillWidth: true
            height: changedLabel.implicitHeight + 24
            radius: Theme.radiusM
            color: dlg.isChangedKey ? Theme.errorContainer : Theme.successContainer
            RowLayout {
                anchors.fill: parent
                anchors.margins: 12
                spacing: 10
                MaterialIcon {
                    icon: dlg.isChangedKey ? "warning" : "shield"
                    iconSize: 22
                    color: dlg.isChangedKey ? Theme.onErrorContainer : Theme.onSuccessContainer
                    Layout.alignment: Qt.AlignTop
                }
                Label {
                    id: changedLabel
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    font.pixelSize: Theme.typeBodySmall
                    font.weight: Font.Medium
                    color: dlg.isChangedKey ? Theme.onErrorContainer : Theme.onSuccessContainer
                    text: dlg.isChangedKey
                        ? qsTr("The server presented a DIFFERENT key than before. This may indicate a server reinstall — or a man-in-the-middle attack. Do not continue unless you verified the change.")
                        : qsTr("This is the first time you connect to this server. Verify the fingerprint out-of-band before trusting it.")
                }
            }
        }

        GridLayout {
            columns: 2
            columnSpacing: 12
            rowSpacing: 10

            RowLayout {
                spacing: 6
                MaterialIcon { icon: "dns"; iconSize: 16; color: Theme.onSurfaceVariant }
                Label { text: qsTr("Host"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.typeBodySmall }
            }
            Label { text: keyInfo.host + ":" + keyInfo.port; color: Theme.onSurface; font.family: "monospace"; font.pixelSize: Theme.typeBodySmall }

            RowLayout {
                spacing: 6
                MaterialIcon { icon: "key"; iconSize: 16; color: Theme.onSurfaceVariant }
                Label { text: qsTr("Key type"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.typeBodySmall }
            }
            Label { text: keyInfo.keyType; color: Theme.onSurface; font.family: "monospace"; font.pixelSize: Theme.typeBodySmall }

            RowLayout {
                spacing: 6
                MaterialIcon { icon: "fingerprint"; iconSize: 16; color: Theme.onSurfaceVariant }
                Label { text: qsTr("SHA256"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.typeBodySmall }
            }
            Label {
                text: keyInfo.sha256 ? String(keyInfo.sha256) : ""
                color: Theme.onSurface
                font.family: "monospace"
                font.pixelSize: Theme.typeBodySmall
                Layout.maximumWidth: 320
                wrapMode: Text.WrapAnywhere
            }

            RowLayout {
                spacing: 6
                MaterialIcon { icon: "enhanced_encryption"; iconSize: 16; color: Theme.onSurfaceVariant }
                Label { text: qsTr("MD5"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.typeBodySmall }
            }
            Label {
                text: keyInfo.md5 ? String(keyInfo.md5) : ""
                color: Theme.onSurfaceVariant
                font.family: "monospace"
                font.pixelSize: Theme.typeLabelSmall
            }
        }
    }
    footer: DialogButtonBox {
        Button { text: qsTr("Cancel"); flat: true
            onClicked: { dlg.decided(false, false); dlg.reject() } }
        Button { text: qsTr("Trust once"); flat: true
            onClicked: { dlg.decided(true, false); dlg.accept() } }
        FlatButton {
            text: qsTr("Trust & Save")
            accent: true
            enabled: !dlg.isChangedKey
            ToolTip.text: qsTr("Saving changed keys is disabled for your safety")
            onClicked: { dlg.decided(true, true); dlg.accept() }
        }
    }
    onRejected: { dlg.decided(false, false) }
}
