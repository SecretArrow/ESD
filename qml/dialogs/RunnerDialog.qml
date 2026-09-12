import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

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
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            ComboBox { id: snippetBox; Layout.preferredWidth: 200
                model: SnippetModel { id: snippetModel }
                textRole: "title"
                onActivated: (i) => cmdField.text = snippetModel.get(i).command }
            Button { flat: true; text: qsTr("Insert snippet"); onClicked: {
                if (snippetBox.currentIndex >= 0)
                    cmdField.text = snippetModel.get(snippetBox.currentIndex).command } }
        }
        TextField {
            id: cmdField
            Layout.fillWidth: true
            placeholderText: qsTr("e.g. docker ps   |   df -h   |   systemctl status {service}")
            font.family: "monospace"
            onAccepted: run()
        }
        RowLayout {
            Label { text: qsTr("Servers:"); color: Theme.textMuted; font.pixelSize: 11 }
            Repeater {
                model: ListModel { id: serversModel }
                delegate: CheckBox { text: name; checked: checked0
                    property bool checked0: checked
                    onCheckedChanged: if (dlg.targets) dlg.targets[sid] = checked }
            }
            Item { Layout.fillWidth: true }
            Button { text: dlg.busy ? qsTr("Running…") : qsTr("Run on selected"); highlighted: !dlg.busy
                enabled: !dlg.busy; onClicked: dlg.run() }
        }
        ListView {
            id: resultsList
            Layout.fillWidth: true
            Layout.preferredHeight: 260
            clip: true
            model: ListModel { id: resultsModel }
            delegate: Rectangle {
                width: resultsList.width
                height: outText.implicitHeight + 24
                color: Theme.surfaceAlt
                radius: 6
                ColumnLayout {
                    anchors.fill: parent; anchors.margins: 6
                    RowLayout {
                        Label { text: host; font.weight: Font.DemiBold; color: exitCode === 0 ? Theme.success : Theme.error }
                        Label { text: qsTr("exit %1").arg(exitCode); color: Theme.textMuted; font.pixelSize: 10 }
                    }
                    Label { id: outText; text: output; color: Theme.text; font.family: "monospace"; font.pixelSize: 11
                        wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
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
