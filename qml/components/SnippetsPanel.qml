import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Eclipse
import Eclipse.Internal 1.0

Rectangle {
    id: panel
    property var session: null
    color: Theme.surfaceContainerLow

    SnippetModel { id: snippetModel }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8

        RowLayout {
            Label {
                text: qsTr("Snippets")
                font.pixelSize: Theme.typeTitleMedium; font.weight: Font.Medium; color: Theme.onSurface
                Layout.fillWidth: true
            }
            IconToolButton {
                iconName: "add"
                toolTip: qsTr("Add snippet")
                onClicked: snippetModel.addSnippet(groupField.text || "General", titleField.text, cmdField.text)
            }
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
            section.delegate: Label { text: section; color: Theme.primary; font.pixelSize: Theme.typeLabelMedium;
                font.weight: Font.DemiBold; font.letterSpacing: 0.8; font.capitalization: Font.AllUppercase; topPadding: 6 }
            delegate: RowLayout {
                width: ListView.view.width
                Label { text: title; color: Theme.onSurface; font.pixelSize: Theme.typeBodySmall; Layout.fillWidth: true; elide: Text.ElideRight }
                IconToolButton {
                    id: sendButton
                    iconName: "play_arrow"
                    toolTip: qsTr("Send to terminal")
                    onClicked: {
                        if (!panel.session) return;
                        const vars = {};
                        const names = snippetModel.variablesIn(command);
                        sendButton.pendingSend = { command: command, names: names };
                        if (names.length === 0) {
                            panel.session.sendToTerminal(snippetModel.expandCommand(command, {}) + "\n");
                        } else {
                            varsDialog.model = names;
                            varsDialog.open();
                        }
                    }
                    property var pendingSend: null
                }
                IconToolButton { iconName: "edit"; toolTip: qsTr("Save command"); onClicked: snippetModel.updateSnippet(id, group, title, cmdField.text) }
                IconToolButton { iconName: "delete"; danger: true; toolTip: qsTr("Delete snippet"); onClicked: snippetModel.removeSnippet(id) }
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
                    Label { text: "{" + modelData + "}"; color: Theme.primary; font.family: "monospace" }
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
