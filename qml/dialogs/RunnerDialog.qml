import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Eclipse.Internal 1.0

// Module import: gives access to the Theme singleton and MD3 components.
import Eclipse

// Command Runner: run a command on multiple connected servers at once.
Dialog {
    id: dlg
    title: qsTr("Command Runner")
    modal: true
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 700
    standardButtons: Dialog.Close
    property var targets: ({})
    property var busy: false

    function openDialog() {
        targets = {};
        serversModel.clear();
        for (let i = 0; i < App.sessions.rowCount(); ++i) {
            const s = App.sessions.get(i);
            if (s.connected) {
                serversModel.append({ sid: s.sessionId, name: s.name, checked: true });
                targets[s.sessionId] = true;
            }
        }
        resultsModel.clear();
        open();
    }

    contentItem: ColumnLayout {
        spacing: 12

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            ComboBox { id: snippetBox; Layout.preferredWidth: 200
                model: SnippetModel { id: snippetModel }
                textRole: "title"
                onActivated: (i) => cmdField.text = snippetModel.get(i).command }
            FlatButton {
                text: qsTr("Insert snippet")
                iconName: "content_paste"
                onClicked: {
                    if (snippetBox.currentIndex >= 0)
                        cmdField.text = snippetModel.get(snippetBox.currentIndex).command }
            }
            Item { Layout.fillWidth: true }
        }

        TextField {
            id: cmdField
            Layout.fillWidth: true
            placeholderText: qsTr("e.g. docker ps   |   df -h   |   systemctl status {service}")
            font.family: "monospace"
            font.pixelSize: Theme.typeBodySmall
            onAccepted: run()
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Label { text: qsTr("Servers:"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.typeLabelMedium }
            Repeater {
                model: ListModel { id: serversModel }
                delegate: CheckBox { text: name; checked: checked0
                    property bool checked0: checked
                    onCheckedChanged: if (dlg.targets) dlg.targets[sid] = checked }
            }
            Item { Layout.fillWidth: true }
            FlatButton {
                text: dlg.busy ? qsTr("Running…") : qsTr("Run on selected")
                iconName: dlg.busy ? "sync" : "play_arrow"
                accent: !dlg.busy
                enabled: !dlg.busy
                onClicked: dlg.run()
            }
        }

        ListView {
            id: resultsList
            Layout.fillWidth: true
            Layout.preferredHeight: 260
            clip: true
            spacing: 6
            model: ListModel { id: resultsModel }
            delegate: Rectangle {
                width: resultsList.width
                height: outText.implicitHeight + 30
                color: Theme.surfaceContainer
                radius: Theme.radiusM
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 10
                    spacing: 4
                    RowLayout {
                        spacing: 6
                        MaterialIcon {
                            icon: "terminal"
                            iconSize: 14
                            color: exitCode === 0 ? Theme.success : Theme.error
                        }
                        Label {
                            text: host
                            font.weight: Font.Medium
                            font.pixelSize: Theme.typeBodySmall
                            color: exitCode === 0 ? Theme.success : Theme.error
                        }
                        Label {
                            text: qsTr("exit %1").arg(exitCode)
                            color: Theme.onSurfaceVariant
                            font.pixelSize: Theme.typeLabelSmall
                        }
                    }
                    Label {
                        id: outText
                        text: output
                        color: Theme.onSurface
                        font.family: "monospace"
                        font.pixelSize: Theme.typeLabelMedium
                        wrapMode: Text.WrapAnywhere
                        Layout.fillWidth: true
                    }
                }
            }
        }
    }
    function run() {
        const ids = Object.keys(targets).filter(k => targets[k]).map(Number);
        if (!ids.length || cmdField.text.length === 0) return;
        busy = true;
        resultsModel.clear();
        for (const sid of ids) {
            const s = App.session(sid);
            if (!s) continue;
            const host = s.name;
            resultsModel.append({ host: host, output: "running…", exitCode: -2, sid: sid });
            s.runCommand(cmdField.text, (res) => {
                for (let i = 0; i < resultsModel.count; ++i) {
                    if (resultsModel.get(i).sid === sid) {
                        resultsModel.setProperty(i, "output", res.output);
                        resultsModel.setProperty(i, "exitCode", res.exitCode);
                    }
                }
                busy = ids.every(id2 => false) || false;
            });
        }
    }
}
