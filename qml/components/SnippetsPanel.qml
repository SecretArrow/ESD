import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Eclipse.Internal 1.0

Rectangle {
    id: panel
    property var session: null
    color: Theme.surface

    SnippetModel { id: snippetModel }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 10
        spacing: 8

        RowLayout {
            Label { text: qsTr("Snippets"); font.weight: Font.DemiBold; color: Theme.text; Layout.fillWidth: true }
            Button { flat: true; text: "+"; onClicked: snippetModel.addSnippet(groupField.text || "General", titleField.text, cmdField.text) }
        }
        RowLayout {
            TextField { id: groupField; placeholderText: qsTr("Group"); Layout.preferredWidth: 80 }
            TextField { id: titleField; placeholderText: qsTr("Title"); Layout.fillWidth: true }
        }
        TextField { id: cmdField; placeholderText: qsTr("command with {vars}"); Layout.fillWidth: true; font.family: "monospace" }

        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: snippetModel
            clip: true
            spacing: 2
            section.property: "group"
            section.delegate: Label { text: section; color: Theme.accent; font.pixelSize: 11;
                font.weight: Font.DemiBold; topPadding: 6 }
            delegate: RowLayout {
                width: ListView.view.width
                Label { text: title; color: Theme.text; font.pixelSize: 12; Layout.fillWidth: true; elide: Text.ElideRight }
                Button { flat: true; text: "▶"; ToolTip.text: qsTr("Send to terminal"); ToolTip.visible: hovered
                    onClicked: {
                        if (!panel.session) return;
                        const vars = {};
                        const names = snippetModel.variablesIn(command);
                        pendingSend = { command: command, names: names };
                        if (names.length === 0) {
                            panel.session.sendToTerminal(snippetModel.expandCommand(command, {}) + "\n");
                        } else {
                            varsDialog.model = names;
                            varsDialog.open();
                        }
                    }
                    property var pendingSend: null
                }
                Button { flat: true; text: "✎"; onClicked: snippetModel.updateSnippet(id, group, title, cmdField.text) }
                Button { flat: true; text: "✕"; onClicked: snippetModel.removeSnippet(id) }
            }
        }
    }

    Dialog {
        id: varsDialog
        property var model: []
        property var pending: null
        title: qsTr("Fill variables")
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        ColumnLayout {
            Repeater {
                model: varsDialog.model
                ColumnLayout {
                    Label { text: "{" + modelData + "}"; color: Theme.accent; font.family: "monospace" }
                    TextField { Layout.fillWidth: true }
                }
            }
        }
        onAccepted: {
            if (!panel.session) return;
            const vars = {};
            // collect fields
            const fields = contentItem.children;
            panel.session.sendToTerminal(varsDialog.pending.command + "\n");
        }
        onOpened: pending = null
    }
}
