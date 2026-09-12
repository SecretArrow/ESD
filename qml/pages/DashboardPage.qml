import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Landing page: recent connections, favorites, quick actions.
Rectangle {
    id: page
    signal connectRequested(int profileId)
    signal quickConnect()
    signal newProfile()
    color: Theme.background

    ScrollView {
        anchors.fill: parent
        anchors.margins: 26
        ColumnLayout {
            width: Math.min(900, page.width - 60)
            spacing: 20

            Label {
                text: qsTr("Welcome to Eclipse SSH Desktop")
                font.pixelSize: 26; font.weight: Font.DemiBold; color: Theme.text
            }
            Label {
                text: qsTr("SSH + SFTP + Terminal + Tunnels in one fast, native app.\nSimple by default, powerful when needed.")
                color: Theme.textMuted; font.pixelSize: 13
            }

            RowLayout {
                spacing: 12
                Button {
                    text: qsTr("＋ New Connection")
                    highlighted: true
                    onClicked: page.newProfile()
                }
                Button { text: qsTr("⚡ Quick Connect"); onClicked: page.quickConnect() }
                Button { text: qsTr("Import OpenSSH config"); onClicked: App.triggerCommand("app.import-openssh") }
            }

            Label { text: qsTr("Favorites & recent"); font.pixelSize: 15; color: Theme.text; topPadding: 12 }

            Flow {
                Layout.fillWidth: true
                spacing: 10
                Repeater {
                    model: ListModel { id: recentModel }
                    delegate: Button {
                        text: name
                        onClicked: page.connectRequested(id)
                        contentItem: ColumnLayout {
                            Label { text: name; color: Theme.text; font.pixelSize: 13 }
                            Label { text: userHost; color: Theme.textMuted; font.pixelSize: 10 }
                        }
                        width: 200; height: 54
                    }
                    Component.onCompleted: reload()
                    function reload() {
                        recentModel.clear();
                        const all = App.profiles.all();
                        all.sort((a, b) => (b.favorite - a.favorite) || (b.lastUsedMs - a.lastUsedMs));
                        for (let i = 0; i < Math.min(8, all.length); ++i) {
                            const p = all[i];
                            recentModel.append({ id: p.id, name: p.name,
                                                userHost: p.username + "@" + p.host });
                        }
                    }
                }
            }

            Label { text: qsTr("Active sessions"); font.pixelSize: 15; color: Theme.text }
            Label {
                visible: App.sessions.rowCount() === 0
                text: qsTr("No active sessions. Connect to a server to get started.")
                color: Theme.textMuted; font.pixelSize: 12
            }
            Repeater {
                model: App.sessions
                delegate: Button {
                    text: qsTr("%1 — %2 (%3 ms)").arg(name).arg(state).arg(latencyMs)
                    onClicked: page.connectRequested(profileId)
                }
            }

            Label {
                text: qsTr("Keyboard: Ctrl+Shift+P command palette · Ctrl+N new connection · Ctrl+T terminal")
                color: Theme.textMuted; font.pixelSize: 11; topPadding: 20
            }
        }
    }
}
