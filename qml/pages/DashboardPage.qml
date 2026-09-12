import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Landing page: hero, quick actions, stats strip, recent/favorite
// connection cards with live session status dots, friendly empty state.
Rectangle {
    id: page
    signal connectRequested(int profileId)
    signal quickConnect()
    signal newProfile()

    color: Theme.background

    // Number of stored profiles; kept live via App.profiles signals below.
    property int profileCount: 0
    // Signature of the session states currently shown on the cards. Latency
    // pings also fire sessionStateChanged(); only rebuild the card grid when
    // a dot state actually changed so hover state is not disturbed.
    property string lastDotSig: ""

    Component.onCompleted: page.refreshProfiles()

    function sessionStateFor(profileId) {
        for (let i = 0; i < App.sessions.rowCount(); ++i) {
            const s = App.sessions.get(i);
            if (s.profileId === profileId)
                return s.state;
        }
        return "Idle";
    }

    function refreshProfiles() {
        profileCount = App.profiles.allProfiles().length;
        recentRepeater.reload();
    }

    Connections {
        target: App.profiles
        function onProfileAdded() { page.refreshProfiles() }
        function onProfileUpdated() { page.refreshProfiles() }
        function onProfileRemoved() { page.refreshProfiles() }
        function onStoreReset() { page.refreshProfiles() }
    }

    Connections {
        target: App.sessions
        function onSessionStateChanged() {
            let sig = "";
            for (let i = 0; i < recentModel.count; ++i) {
                const it = recentModel.get(i);
                sig += page.sessionStateFor(it.id) + ";";
            }
            if (sig !== page.lastDotSig) {
                page.lastDotSig = sig;
                recentRepeater.reload();
            }
        }
    }

    ScrollView {
        anchors.fill: parent
        anchors.margins: 26
        ColumnLayout {
            width: Math.min(900, page.width - 60)
            spacing: 18

            // ---- hero -------------------------------------------------
            ColumnLayout {
                Layout.fillWidth: true
                spacing: 6
                Label {
                    text: qsTr("Welcome to Eclipse SSH Desktop")
                    font.pixelSize: 28; font.weight: Font.DemiBold; color: Theme.text
                }
                Rectangle {
                    Layout.preferredWidth: 48
                    Layout.preferredHeight: 3
                    width: 48; height: 3
                    radius: 1.5
                    color: Theme.accent
                }
                Label {
                    text: qsTr("SSH + SFTP + Terminal + Tunnels in one fast, native app.\nSimple by default, powerful when needed.")
                    color: Theme.textMuted; font.pixelSize: 13
                }
            }

            // ---- quick actions ----------------------------------------
            RowLayout {
                spacing: 12
                FlatButton {
                    text: qsTr("New Connection")
                    glyph: "＋"
                    accent: true
                    ToolTip.text: qsTr("Create a saved connection profile")
                    onClicked: page.newProfile()
                }
                FlatButton {
                    text: qsTr("Quick Connect")
                    glyph: "»"
                    showBorder: true
                    ToolTip.text: qsTr("Connect without saving a profile")
                    onClicked: page.quickConnect()
                }
                FlatButton {
                    text: qsTr("Import OpenSSH config")
                    showBorder: true
                    ToolTip.text: qsTr("Import hosts from ~/.ssh/config")
                    onClicked: App.triggerCommand("app.import-openssh")
                }
            }

            // ---- stats strip ------------------------------------------
            RowLayout {
                Layout.fillWidth: true
                spacing: 12
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 68
                    radius: Theme.radiusM
                    color: Theme.surfaceAlt
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 2
                        Label {
                            text: qsTr("PROFILES")
                            color: Theme.textMuted; font.pixelSize: 11; font.letterSpacing: 0.8
                        }
                        Label {
                            text: page.profileCount
                            color: Theme.text; font.pixelSize: 18; font.weight: Font.DemiBold
                        }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 68
                    radius: Theme.radiusM
                    color: Theme.surfaceAlt
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 2
                        Label {
                            text: qsTr("ACTIVE SESSIONS")
                            color: Theme.textMuted; font.pixelSize: 11; font.letterSpacing: 0.8
                        }
                        Label {
                            text: activeSessionsRepeater.count
                            color: Theme.text; font.pixelSize: 18; font.weight: Font.DemiBold
                        }
                    }
                }
                Rectangle {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 68
                    radius: Theme.radiusM
                    color: Theme.surfaceAlt
                    ColumnLayout {
                        anchors.fill: parent
                        anchors.margins: 14
                        spacing: 2
                        Label {
                            text: qsTr("VERSION")
                            color: Theme.textMuted; font.pixelSize: 11; font.letterSpacing: 0.8
                        }
                        Label {
                            text: App.version
                            color: Theme.text; font.pixelSize: 18; font.weight: Font.DemiBold
                        }
                    }
                }
            }

            // ---- favorites & recent -----------------------------------
            Label {
                text: qsTr("Favorites & recent")
                font.pixelSize: 15; color: Theme.text
                topPadding: 12
                visible: page.profileCount > 0
            }

            Flow {
                Layout.fillWidth: true
                spacing: 10
                visible: page.profileCount > 0
                Repeater {
                    id: recentRepeater
                    model: ListModel { id: recentModel }
                    delegate: Rectangle {
                        id: card
                        width: 240; height: 64
                        radius: Theme.radiusM
                        color: Theme.surface
                        border.width: 1
                        border.color: cardMouse.containsMouse ? Theme.accent : Theme.border
                        scale: cardMouse.containsMouse ? 1.02 : 1.0

                        Behavior on scale { NumberAnimation { duration: 150; easing.type: Easing.OutQuad } }
                        Behavior on border.color { ColorAnimation { duration: 150 } }

                        MouseArea {
                            id: cardMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: page.connectRequested(model.id)
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 10
                            StatusDot {
                                state: model.state
                                Layout.alignment: Qt.AlignVCenter
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignVCenter
                                spacing: 2
                                Label {
                                    text: model.name
                                    color: Theme.text; font.pixelSize: 13; font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                Label {
                                    text: model.userHost
                                    color: Theme.textMuted; font.pixelSize: 11
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                            }
                            Label {
                                text: "★"
                                visible: model.favorite
                                color: "#e2b12c"
                                font.pixelSize: 14
                                Layout.alignment: Qt.AlignVCenter
                            }
                        }
                    }
                    Component.onCompleted: reload()
                    function reload() {
                        recentModel.clear();
                        const all = App.profiles.allProfiles();
                        all.sort((a, b) => (b.favorite - a.favorite) || (b.lastUsedMs - a.lastUsedMs));
                        for (let i = 0; i < Math.min(8, all.length); ++i) {
                            const p = all[i];
                            recentModel.append({ id: p.id, name: p.name,
                                                userHost: p.username + "@" + p.host,
                                                favorite: p.favorite,
                                                state: page.sessionStateFor(p.id) });
                        }
                    }
                }
            }

            // ---- empty state ------------------------------------------
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 180
                radius: Theme.radiusM
                color: Theme.surface
                border.width: 1
                border.color: Theme.border
                visible: page.profileCount === 0
                ColumnLayout {
                    anchors.centerIn: parent
                    spacing: 10
                    Label {
                        text: "⌁"
                        font.pixelSize: 40
                        color: Theme.accent
                        Layout.alignment: Qt.AlignHCenter
                    }
                    Label {
                        text: qsTr("No connections yet")
                        color: Theme.text; font.pixelSize: 15; font.weight: Font.DemiBold
                        Layout.alignment: Qt.AlignHCenter
                    }
                    Label {
                        text: qsTr("Create your first connection to get started.")
                        color: Theme.textMuted; font.pixelSize: 12
                        Layout.alignment: Qt.AlignHCenter
                    }
                    FlatButton {
                        text: qsTr("＋ New Connection")
                        accent: true
                        Layout.alignment: Qt.AlignHCenter
                        Layout.topMargin: 4
                        onClicked: page.newProfile()
                    }
                }
            }

            // ---- active sessions --------------------------------------
            Label { text: qsTr("Active sessions"); font.pixelSize: 15; color: Theme.text }
            Label {
                visible: activeSessionsRepeater.count === 0
                text: qsTr("No active sessions. Connect to a server to get started.")
                color: Theme.textMuted; font.pixelSize: 12
            }
            Flow {
                Layout.fillWidth: true
                spacing: 10
                Repeater {
                    id: activeSessionsRepeater
                    model: App.sessions
                    delegate: Rectangle {
                        id: sessionCard
                        width: 240; height: 64
                        radius: Theme.radiusM
                        color: Theme.surface
                        border.width: 1
                        border.color: sessionMouse.containsMouse ? Theme.accent : Theme.border
                        scale: sessionMouse.containsMouse ? 1.02 : 1.0

                        Behavior on scale { NumberAnimation { duration: 150; easing.type: Easing.OutQuad } }
                        Behavior on border.color { ColorAnimation { duration: 150 } }

                        MouseArea {
                            id: sessionMouse
                            anchors.fill: parent
                            hoverEnabled: true
                            cursorShape: Qt.PointingHandCursor
                            onClicked: page.connectRequested(model.profileId)
                        }

                        RowLayout {
                            anchors.fill: parent
                            anchors.margins: 10
                            spacing: 10
                            StatusDot {
                                state: model.state
                                Layout.alignment: Qt.AlignVCenter
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                Layout.alignment: Qt.AlignVCenter
                                spacing: 2
                                Label {
                                    text: model.name
                                    color: Theme.text; font.pixelSize: 13; font.weight: Font.DemiBold
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                                Label {
                                    text: model.latencyMs >= 0
                                          ? qsTr("%1 ms · %2").arg(model.latencyMs).arg(model.state)
                                          : model.state
                                    color: Theme.textMuted; font.pixelSize: 11
                                    elide: Text.ElideRight
                                    Layout.fillWidth: true
                                }
                            }
                        }
                    }
                }
            }

            Label {
                text: qsTr("Keyboard: Ctrl+Shift+P command palette · Ctrl+N new connection · Ctrl+T terminal")
                color: Theme.textMuted; font.pixelSize: 11; topPadding: 20
            }
        }
    }
}
