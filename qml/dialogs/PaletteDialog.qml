import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

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
        TextField {
            id: queryField
            Layout.fillWidth: true
            placeholderText: qsTr("Type a command…")
            onTextChanged: { dlg.query = text; paletteList.refresh() }
        }
        ListView {
            id: paletteList
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(360, count * 34 + 4)
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
                height: 32
                onClicked: { dlg.commandPicked(model.id); dlg.close() }
                contentItem: RowLayout {
                    Label { text: category; color: Theme.textMuted; font.pixelSize: 10; Layout.preferredWidth: 90 }
                    Label { text: title; color: Theme.text; font.pixelSize: 13 }
                }
            }
        }
    }
    footer: DialogButtonBox { Button { text: qsTr("Close"); flat: true; onClicked: dlg.close() } }
}
