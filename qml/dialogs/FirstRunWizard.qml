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
        spacing: 12
        StackLayout {
            currentIndex: dlg.page
            ColumnLayout {
                spacing: 10
                Label { text: qsTr("Choose your theme"); color: Theme.text; font.weight: Font.DemiBold }
                RadioButton { text: qsTr("System"); checked: true; onClicked: { App.settings.themeMode = "system"; ThemeBridge.applyFromSettings() } }
                RadioButton { text: qsTr("Light"); onClicked: { App.settings.themeMode = "light"; ThemeBridge.applyFromSettings() } }
                RadioButton { text: qsTr("Dark"); onClicked: { App.settings.themeMode = "dark"; ThemeBridge.applyFromSettings() } }
            }
            ColumnLayout {
                spacing: 10
                Label { text: qsTr("Terminal font"); color: Theme.text; font.weight: Font.DemiBold }
                ComboBox {
                    model: ["monospace", "DejaVu Sans Mono", "Liberation Mono", "Cascadia Mono"]
                    onActivated: (i) => App.settings.terminalFontFamily = model[i]
                }
                Label { text: qsTr("You can change everything later in Settings.")
                        color: Theme.textMuted; font.pixelSize: 11 }
            }
            ColumnLayout {
                spacing: 10
                Label { text: qsTr("Credential storage"); color: Theme.text; font.weight: Font.DemiBold }
                RadioButton { text: qsTr("OS secure storage (recommended)"); checked: true
                    onClicked: App.credentials.mode = 0 }
                RadioButton { text: qsTr("Ask every time"); onClicked: App.credentials.mode = 2 }
                Label { text: App.credentials.storageDescription(); color: Theme.textMuted; font.pixelSize: 11 }
            }
        }
    }
    footer: DialogButtonBox {
        Button { text: qsTr("Back"); flat: true; visible: dlg.page > 0; onClicked: dlg.page-- }
        Button { text: qsTr("Next"); flat: true; visible: dlg.page < 2; onClicked: dlg.page++ }
        Button { text: qsTr("Get Started"); highlighted: true; visible: dlg.page === 2
            onClicked: { dlg.close(); dlg.finished() } }
    }
}
