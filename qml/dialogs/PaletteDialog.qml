import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Module import: gives access to the Theme singleton (same-module types in
// subdirectories are not visible without it - binding errors otherwise).
import Eclipse

// Command palette (Ctrl+Shift+P).
Dialog {
    id: dlg
    signal commandPicked(string id)
    title: qsTr("Command Palette")
    modal: true
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 520
    standardButtons: Dialog.NoButton

    property string query: ""
    function openDialog() { query = ""; paletteList.refresh(); open(); queryField.forceActiveFocus() }

    contentItem: ColumnLayout {
        spacing: 12

        // Search field with leading glyph (MD3 search bar)
        RowLayout {
            spacing: 10
            Layout.fillWidth: true
            MaterialIcon {
                icon: "search"
                iconSize: 20
                color: Theme.onSurfaceVariant
            }
            TextField {
                id: queryField
                Layout.fillWidth: true
                placeholderText: qsTr("Type a command…")
                onTextChanged: { dlg.query = text; paletteList.refresh() }
            }
        }

        ListView {
            id: paletteList
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(360, count * 36 + 4)
            clip: true
            function refresh() {
                paletteModel.clear();
                const cmds = App.queryCommands(dlg.query);
                for (let i = 0; i < cmds.length && i < 40; ++i) {
                    const c = cmds[i];
                    paletteModel.append({ id: c.id, title: c.title, category: c.category });
                }
            }
            model: ListModel { id: paletteModel }
            delegate: ItemDelegate {
                width: paletteList.width
                height: 36
                onClicked: { dlg.commandPicked(model.id); dlg.close() }
                contentItem: RowLayout {
                    spacing: 12
                    Label {
                        text: category
                        color: Theme.onSurfaceVariant
                        font.pixelSize: Theme.typeLabelSmall
                        Layout.preferredWidth: 90
                        elide: Text.ElideRight
                    }
                    Label {
                        text: title
                        color: Theme.onSurface
                        font.pixelSize: Theme.typeBodyMedium
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    MaterialIcon {
                        icon: "keyboard_return"
                        iconSize: 14
                        color: Theme.onSurfaceVariant
                        visible: paletteList.currentIndex === index
                    }
                }
            }
        }
    }
    footer: DialogButtonBox { Button { text: qsTr("Close"); flat: true; onClicked: dlg.close() } }
}
