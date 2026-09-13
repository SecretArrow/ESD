import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Module import: gives access to the Theme singleton (same-module types in
// subdirectories are not visible without it - binding errors otherwise).
import Eclipse

Dialog {
    id: dlg
    signal finished()
    title: qsTr("Welcome to Eclipse SSH Desktop")
    modal: true
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 460
    standardButtons: Dialog.NoButton
    property int page: 0

    contentItem: ColumnLayout {
        spacing: 16

        // Welcome badge
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
                    icon: "rocket_launch"
                    iconSize: 24
                    color: Theme.onPrimaryContainer
                }
            }
            Label {
                text: qsTr("Let's set things up")
                color: Theme.onSurface
                font.pixelSize: Theme.typeTitleMedium
                font.weight: Font.Medium
                Layout.fillWidth: true
            }
        }

        StackLayout {
            currentIndex: dlg.page
            ColumnLayout {
                spacing: 12
                RowLayout {
                    spacing: 8
                    MaterialIcon { icon: "palette"; iconSize: 18; color: Theme.onSurfaceVariant }
                    Label {
                        text: qsTr("Choose your theme")
                        color: Theme.onSurface
                        font.pixelSize: Theme.typeTitleMedium
                        font.weight: Font.Medium
                    }
                }
                RadioButton { text: qsTr("System"); checked: true; onClicked: { App.settings.themeMode = "system"; ThemeBridge.applyFromSettings() } }
                RadioButton { text: qsTr("Light"); onClicked: { App.settings.themeMode = "light"; ThemeBridge.applyFromSettings() } }
                RadioButton { text: qsTr("Dark"); onClicked: { App.settings.themeMode = "dark"; ThemeBridge.applyFromSettings() } }
            }
            ColumnLayout {
                spacing: 12
                RowLayout {
                    spacing: 8
                    MaterialIcon { icon: "keyboard"; iconSize: 18; color: Theme.onSurfaceVariant }
                    Label {
                        text: qsTr("Terminal font")
                        color: Theme.onSurface
                        font.pixelSize: Theme.typeTitleMedium
                        font.weight: Font.Medium
                    }
                }
                ComboBox {
                    Layout.fillWidth: true
                    model: ["monospace", "DejaVu Sans Mono", "Liberation Mono", "Cascadia Mono"]
                    onActivated: (i) => App.settings.terminalFontFamily = model[i]
                }
                Label {
                    text: qsTr("You can change everything later in Settings.")
                    color: Theme.onSurfaceVariant
                    font.pixelSize: Theme.typeLabelMedium
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                }
            }
            ColumnLayout {
                spacing: 12
                RowLayout {
                    spacing: 8
                    MaterialIcon { icon: "key"; iconSize: 18; color: Theme.onSurfaceVariant }
                    Label {
                        text: qsTr("Credential storage")
                        color: Theme.onSurface
                        font.pixelSize: Theme.typeTitleMedium
                        font.weight: Font.Medium
                    }
                }
                RadioButton { text: qsTr("OS secure storage (recommended)"); checked: true
                    onClicked: App.credentials.mode = 0 }
                RadioButton { text: qsTr("Ask every time"); onClicked: App.credentials.mode = 2 }
                Label {
                    text: App.credentials.storageDescription()
                    color: Theme.onSurfaceVariant
                    font.pixelSize: Theme.typeLabelMedium
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                }
            }
        }
    }
    footer: DialogButtonBox {
        Button { text: qsTr("Back"); flat: true; visible: dlg.page > 0; onClicked: dlg.page-- }
        Button { text: qsTr("Next"); flat: true; visible: dlg.page < 2; onClicked: dlg.page++ }
        FlatButton { text: qsTr("Get Started"); iconName: "rocket_launch"; accent: true; visible: dlg.page === 2
            onClicked: { dlg.close(); dlg.finished() } }
    }
}
