import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Module import: gives access to the Theme singleton (same-module types in
// subdirectories are not visible without it - binding errors otherwise).
import Eclipse

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
        spacing: 12

        Label {
            text: qsTr("Rules are saved to the connection profile. Auto-start rules launch on connect.")
            color: Theme.onSurfaceVariant
            font.pixelSize: Theme.typeBodySmall
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        ListView {
            id: rulesList
            Layout.fillWidth: true
            Layout.preferredHeight: Math.min(220, rulesModel.count * 36 + 4)
            clip: true
            spacing: 4
            model: ListModel { id: rulesModel }
            delegate: RowLayout {
                width: rulesList.width
                spacing: 10

                // rule-type chip (MD3: full-round tonal chip)
                Rectangle {
                    Layout.preferredWidth: 62
                    implicitHeight: 20
                    radius: Theme.radiusFull
                    color: Theme.secondaryContainer
                    Label {
                        anchors.centerIn: parent
                        text: type === "dynamic" ? "SOCKS5" : type
                        color: Theme.onSecondaryContainer
                        font.pixelSize: Theme.typeLabelMedium
                    }
                }
                Label {
                    text: listenAddress + ":" + listenPort
                    color: Theme.onSurface
                    font.pixelSize: Theme.typeBodySmall
                    font.family: "monospace"
                    Layout.preferredWidth: 130
                }
                RowLayout {
                    spacing: 4
                    Layout.fillWidth: true
                    MaterialIcon { icon: "arrow_forward"; iconSize: 14; color: Theme.onSurfaceVariant }
                    Label {
                        text: type === "dynamic" ? qsTr("dynamic") : destHost + ":" + destPort
                        color: Theme.onSurfaceVariant
                        font.pixelSize: Theme.typeBodySmall
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                }
                CheckBox { text: qsTr("auto"); checked: autoStart; onCheckedChanged: autoStart = checked }
                IconToolButton {
                    iconName: "close"
                    iconSize: 18
                    danger: true
                    toolTip: qsTr("Remove rule")
                    onClicked: rulesModel.remove(index)
                }
            }
        }

        RowLayout {
            spacing: 8
            MaterialIcon { icon: "add"; iconSize: 18; color: Theme.primary }
            Label {
                text: qsTr("Add rule")
                font.pixelSize: Theme.typeTitleSmall
                font.weight: Font.Medium
                color: Theme.onSurface
            }
        }

        RowLayout {
            spacing: 8
            ComboBox { id: typeBox; model: ["local", "remote", "dynamic"]; Layout.preferredWidth: 92 }
            TextField { id: listenAddr; text: "127.0.0.1"; Layout.preferredWidth: 90 }
            TextField { id: listenPort; placeholderText: "5432"; Layout.preferredWidth: 70 }
            MaterialIcon { icon: "arrow_forward"; iconSize: 18; color: Theme.onSurfaceVariant }
            TextField { id: destHost; placeholderText: qsTr("dest host"); Layout.preferredWidth: 130 }
            TextField { id: destPort; placeholderText: qsTr("port"); Layout.preferredWidth: 70 }
            FlatButton {
                text: qsTr("Add")
                iconName: "add"
                accent: true
                onClicked: {
                    rulesModel.append({ id: String(Date.now()), name: "", type: typeBox.currentText,
                        listenAddress: listenAddr.text, listenPort: parseInt(listenPort.text || "0"),
                        destHost: destHost.text, destPort: parseInt(destPort.text || "0"),
                        autoStart: true, enabled: true });
                }
            }
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
