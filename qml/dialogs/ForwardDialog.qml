import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Port forwarding manager for the current session's profile.
Dialog {
    id: dlg
    title: qsTr("Port Forwarding")
    modal: true
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 640
    standardButtons: Dialog.Close
    property var draft: null

    function openDialog() {
        const s = root2 && root2.currentSession ? root2.currentSession : (App.sessions.rowCount() ? App.sessions.sessionAt(currentSessionIndex()) : null);
        draft = s ? App.draftProfile(s.profileId) : App.draftProfile(0);
        rulesModel.clear();
        for (let i = 0; i < draft.forwardingRules.length; ++i) rulesModel.append(draft.forwardingRules[i]);
        open();
    }
    function currentSessionIndex() { return 0 }
    property var root2: null

    contentItem: ColumnLayout {
        spacing: 8
        Label { text: qsTr("Rules are saved to the connection profile. Auto-start rules launch on connect.")
                color: Theme.textMuted; font.pixelSize: 11 }
        ListView {
            id: rulesList
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(220, rulesModel.count * 30 + 4)
            model: ListModel { id: rulesModel }
            delegate: RowLayout {
                width: rulesList.width
                Label { text: type === "dynamic" ? "SOCKS5" : type; color: Theme.accent; font.pixelSize: 11; Layout.preferredWidth: 62 }
                Label { text: listenAddress + ":" + listenPort; color: Theme.text; font.pixelSize: 12; Layout.preferredWidth: 130 }
                Label { text: type === "dynamic" ? "→ dynamic" : "→ " + destHost + ":" + destPort;
                    color: Theme.textMuted; font.pixelSize: 12; Layout.fillWidth: true }
                CheckBox { text: qsTr("auto"); checked: autoStart; onCheckedChanged: autoStart = checked }
                Button { flat: true; text: "✕"; onClicked: rulesModel.remove(index) }
            }
        }
        Label { text: qsTr("Add rule"); font.weight: Font.DemiBold; color: Theme.text }
        RowLayout {
            ComboBox { id: typeBox; model: ["local", "remote", "dynamic"]; Layout.preferredWidth: 92 }
            TextField { id: listenAddr; text: "127.0.0.1"; Layout.preferredWidth: 90 }
            TextField { id: listenPort; placeholderText: "5432"; Layout.preferredWidth: 70 }
            Label { text: "→"; color: Theme.textMuted }
            TextField { id: destHost; placeholderText: qsTr("dest host"); Layout.preferredWidth: 130 }
            TextField { id: destPort; placeholderText: qsTr("port"); Layout.preferredWidth: 70 }
            Button { text: qsTr("Add"); highlighted: true; onClicked: {
                rulesModel.append({ id: String(Date.now()), name: "", type: typeBox.currentText,
                    listenAddress: listenAddr.text, listenPort: parseInt(listenPort.text || "0"),
                    destHost: destHost.text, destPort: parseInt(destPort.text || "0"),
                    autoStart: true, enabled: true });
            } }
        }
    }
    onClosed: {
        if (!draft) return;
        const arr = [];
        for (let i = 0; i < rulesModel.count; ++i) arr.push(rulesModel.get(i));
        draft.forwardingRules = arr;
        App.saveProfile(draft, "", "", false);
    }
}
