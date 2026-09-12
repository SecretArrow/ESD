import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs
import Eclipse
import Eclipse.Internal 1.0

ApplicationWindow {
    id: root
    width: 1280
    height: 800
    minimumWidth: 940
    minimumHeight: 620
    visible: true
    title: currentSession ? qsTr("%1 — Eclipse SSH Desktop").arg(currentSession.name)
                          : qsTr("Eclipse SSH Desktop")
    color: Theme.surface
    onClosing: (close) => {
        if (App.settings.minimizeToTray) {
            close.accepted = false;
            trayIcon.handleWindowClose();
        }
    }
    Component.onCompleted: trayIcon.window = root

    // ---- app state -----------------------------------------------------------
    property int currentTab: -1
    property var currentSession: currentTab >= 0 ? App.sessions.sessionAt(currentTab) : null
    property string currentPage: "dashboard"   // dashboard | session | logs | settings
    property var pendingHostKeySession: 0
    property var pendingAuthSession: 0

    function openPage(p) { currentPage = p; }
    function selectTab(i) { if (i >= 0 && i < App.sessions.rowCount()) { currentTab = i; currentPage = "session"; } }

    onCurrentPageChanged: statusTimer.restart()

    // ---- notifications toast -------------------------------------------------
    function toast(title, body, isError) {
        toastLoader.title = title; toastLoader.body = body; toastLoader.isError = isError;
        toastLoader.visible = true; toastTimer.restart();
    }

    // ---- central command routing ---------------------------------------------
    function runCommand(id) {
        switch (id) {
        case "app.new-connection": profileDialog.openNew(); break;
        case "app.quick-connect": quickConnectDialog.openDialog(); break;
        case "app.new-terminal":
            if (currentSession) App.openTerminalFor(currentSession.sessionId);
            else if (App.sessions.rowCount() > 0) { selectTab(0); App.openTerminalFor(App.sessions.sessionAt(0).sessionId); }
            break;
        case "app.open-sftp":
            if (currentSession) { currentPage = "session"; sessionPage.openFiles(); }
            else toast("Files", "Connect to a server first.", true);
            break;
        case "app.settings": openPage("settings"); break;
        case "app.toggle-dark":
            App.settings.themeMode = ThemeBridge.isDark ? "light" : "dark";
            ThemeBridge.applyFromSettings();
            break;
        case "app.logs": openPage("logs"); break;
        case "app.snippets": sessionPage.toggleSnippets(); break;
        case "app.command-runner": runnerDialog.openDialog(); break;
        case "app.forwarding": forwardDialog.openDialog(); break;
        case "app.diagnostics": diagnosticsDialog.openDialog(); break;
        case "app.import-openssh": importDialog.openDialog(); break;
        case "app.export-profiles": importDialog.openDialog(); break;
        case "app.about": aboutDialog.open(); break;
        case "app.close-tab": if (currentTab >= 0) App.sessions.closeSession(currentTab); break;
        case "app.command-palette": paletteDialog.openDialog(); break;
        }
    }

    Connections {
        target: App
        function onCommandRequested(id) { root.runCommand(id); }
        function onHostKeyNeeded(sessionId, keyInfo, isChanged) {
            pendingHostKeySession = sessionId;
            hostKeyDialog.keyInfo = keyInfo;
            hostKeyDialog.isChangedKey = isChanged;
            hostKeyDialog.open();
        }
        function onAuthPromptNeeded(sessionId, prompts) {
            pendingAuthSession = sessionId;
            authDialog.prompts = prompts;
            authDialog.open();
        }
        function onAuthFailed(sessionId, friendly, technical, hint) {
            errorDialog.errorTitle = qsTr("Authentication failed");
            errorDialog.friendly = friendly; errorDialog.technical = technical; errorDialog.hint = hint;
            errorDialog.open();
        }
        function onNotify(title, body, isError) { root.toast(title, body, isError); }
        function onSessionDisconnected(sessionId, reason, byRequest) {
            if (currentSession && currentSession.sessionId === sessionId) statusTimer.restart();
        }
    }

    Timer { id: statusTimer; interval: 300; onTriggered: sessionTabsModel.refresh() }

    ListModel { id: sessionTabsModel
        function refresh() {
            clear();
            for (let i = 0; i < App.sessions.rowCount(); ++i) {
                const s = App.sessions.get(i);
                append(s);
            }
        }
    }
    Connections {
        target: App.sessions
        function onSessionListChanged() { sessionTabsModel.refresh(); root.currentTab = root.currentTab; }
        function onSessionStateChanged() { sessionTabsModel.refresh(); }
    }

    // ---- layout ----------------------------------------------------------------
    RowLayout {
        anchors.fill: parent
        spacing: 0

        // ======== sidebar =========
        Rectangle {
            Layout.preferredWidth: 260
            Layout.fillHeight: true
            color: Theme.surfaceAlt

            ColumnLayout {
                anchors.fill: parent
                anchors.margins: 10
                spacing: 8

                RowLayout {
                    spacing: 8
                    Rectangle { width: 22; height: 22; radius: 6; color: Theme.accent }
                    Label { text: "Eclipse SSH"; font.weight: Font.DemiBold; font.pixelSize: 15; color: Theme.text }
                    Item { Layout.fillWidth: true }
                    ToolButton { icon.name: "list-add"; text: "+"
                        ToolTip.text: qsTr("New connection"); ToolTip.visible: hovered
                        onClicked: profileDialog.openNew() }
                }

                TextField {
                    id: searchField
                    Layout.fillWidth: true
                    placeholderText: qsTr("Search connections…")
                    font.pixelSize: 12
                }

                ScrollView {
                    Layout.fillWidth: true; Layout.fillHeight: true
                    ScrollBar.horizontal.policy: ScrollBar.AlwaysOff
                    ListView {
                        id: connList
                        model: ListModel { id: connModel }
                        spacing: 2
                        clip: true
                        delegate: ItemDelegate {
                            width: connList.width
                            highlighted: false
                            onClicked: {
                                if (model.needsCreds && !model.hasSecrets)
                                    App.connectProfile(model.id, "", "");
                                else
                                    App.connectProfile(model.id, "", "");
                            }
                            contentItem: RowLayout {
                                spacing: 6
                                Label { text: model.favorite ? "★" : "○"; color: model.favorite ? "#e2b12c" : Theme.textMuted }
                                ColumnLayout {
                                    spacing: 0
                                    Layout.fillWidth: true
                                    Label { text: model.name; color: Theme.text; font.pixelSize: 13;
                                        elide: Text.ElideRight; Layout.fillWidth: true }
                                    Label { text: model.userHost; color: Theme.textMuted; font.pixelSize: 10;
                                        elide: Text.ElideRight; Layout.fillWidth: true }
                                }
                            }
                            Menu {
                                id: ctxMenu
                                MenuItem { text: qsTr("Connect"); onClicked: App.connectProfile(model.id) }
                                MenuItem { text: model.favorite ? qsTr("Remove favorite") : qsTr("Add favorite")
                                    onClicked: App.toggleFavorite(model.id) }
                                MenuItem { text: qsTr("Duplicate"); onClicked: App.duplicateProfile(model.id) }
                                MenuItem { text: qsTr("Edit"); onClicked: profileDialog.openEdit(model.id) }
                                MenuItem { text: qsTr("Diagnostics"); onClicked: { diagnosticsDialog.profileId = model.id; diagnosticsDialog.openDialog(); } }
                                MenuItem { text: qsTr("Delete"); onClicked: App.deleteProfile(model.id) }
                            }
                            MouseArea { anchors.fill: parent; acceptedButtons: Qt.RightButton
                                onClicked: ctxMenu.popup() }
                        }
                        Component.onCompleted: reload()
                        function reload() {
                            connModel.clear();
                            const all = App.profiles.all();
                            const q = searchField.text.toLowerCase();
                            const favorites = [];
                            const others = [];
                            for (let i = 0; i < all.length; ++i) {
                                const p = all[i];
                                if (q && (p.name + " " + p.host + " " + p.username).toLowerCase().indexOf(q) < 0) continue;
                                const item = { id: p.id, name: p.name, userHost: p.username + "@" + p.host,
                                              favorite: p.favorite,
                                              needsCreds: p.authMethod === "password",
                                              hasSecrets: App.hasStoredSecret(p.id) };
                                if (p.favorite) favorites.push(item); else others.push(item);
                            }
                            favorites.forEach(x => connModel.append(x));
                            others.forEach(x => connModel.append(x));
                        }
                    }
                }

                RowLayout {
                    Button { text: qsTr("New"); flat: true; onClicked: profileDialog.openNew() }
                    Button { text: qsTr("Quick"); flat: true; onClicked: quickConnectDialog.openDialog() }
                    Item { Layout.fillWidth: true }
                }
            }

            Component.onCompleted: connList.reload()
            Connections { target: App.profiles; function onProfileAdded() { connList.reload() }
                          function onProfileRemoved() { connList.reload() }
                          function onProfileUpdated() { connList.reload() } }
        }

        // ======== main column =========
        ColumnLayout {
            Layout.fillWidth: true; Layout.fillHeight: true; spacing: 0

            // toolbar
            Rectangle {
                Layout.fillWidth: true; Layout.preferredHeight: 44; color: Theme.surface
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
                RowLayout {
                    anchors.fill: parent; anchors.margins: 6; spacing: 4
                    Button { text: qsTr("New"); flat: true; onClicked: profileDialog.openNew() }
                    Button { text: qsTr("Connect"); flat: true; onClicked: quickConnectDialog.openDialog() }
                    Button { text: qsTr("Terminal"); flat: true
                        onClicked: root.runCommand("app.new-terminal") }
                    Button { text: qsTr("Files"); flat: true; onClicked: root.runCommand("app.open-sftp") }
                    Button { text: qsTr("Tunnel"); flat: true; onClicked: forwardDialog.openDialog() }
                    Button { text: qsTr("Runner"); flat: true; onClicked: runnerDialog.openDialog() }
                    Item { Layout.fillWidth: true }
                    Button { text: qsTr("Palette ⌘"); flat: true; onClicked: paletteDialog.openDialog() }
                    Button { text: qsTr("Settings"); flat: true; onClicked: root.openPage("settings") }
                }
            }

            // session tabs
            TabBar {
                id: tabBar
                Layout.fillWidth: true
                visible: sessionTabsModel.count > 0
                Repeater {
                    model: sessionTabsModel
                    TabButton {
                        text: name + (connected ? " ●" : "")
                        width: Math.min(180, text.length * 8 + 40)
                        onClicked: root.selectTab(index)
                        MouseArea { anchors.fill: parent; acceptedButtons: Qt.MiddleButton
                            onClicked: App.sessions.closeSession(index) }
                    }
                }
            }

            // content
            StackLayout {
                Layout.fillWidth: true; Layout.fillHeight: true
                currentIndex: currentPage === "dashboard" ? 0
                            : currentPage === "session" ? 1
                            : currentPage === "logs" ? 2 : 3

                DashboardPage {
                    onConnectRequested: (pid) => App.connectProfile(pid)
                    onQuickConnect: quickConnectDialog.openDialog()
                    onNewProfile: profileDialog.openNew()
                }

                SessionPage {
                    id: sessionPage
                    session: root.currentSession
                    visible: currentPage === "session"
                }

                LogsPage {}

                SettingsPage {}
            }

            // status bar
            Rectangle {
                Layout.fillWidth: true; Layout.preferredHeight: 26; color: Theme.surfaceAlt
                Rectangle { width: parent.width; height: 1; color: Theme.border }
                RowLayout {
                    anchors.fill: parent; anchors.margins: 4; spacing: 12
                    Label {
                        text: {
                            if (!currentSession) return qsTr("Ready");
                            return currentSession.state + (currentSession.latencyMs >= 0
                                    ? "   ·   " + currentSession.latencyMs + " ms"
                                    : "") + "   ·   " + currentSession.engineName;
                        }
                        color: currentSession && currentSession.connected ? Theme.success : Theme.textMuted
                        font.pixelSize: 11
                    }
                    Item { Layout.fillWidth: true }
                    Label { text: qsTr("%1 connection(s)").arg(App.sessions.rowCount()); color: Theme.textMuted; font.pixelSize: 11 }
                }
            }
        }
    }

    // ---- dialogs ------------------------------------------------------------------
    HostKeyDialog {
        id: hostKeyDialog
        onDecided: (accepted, save) => {
            if (pendingHostKeySession)
                App.session(pendingHostKeySession).decideHostKey(accepted, save);
            pendingHostKeySession = 0;
        }
    }
    Dialog { id: authDialog
        property var prompts: []
        property var fieldRefs: []
        title: qsTr("Authentication required")
        standardButtons: Dialog.Ok | Dialog.Cancel
        parent: Overlay.overlay; anchors.centerIn: parent; modal: true
        onPromptsChanged: fieldRefs = []
        ColumnLayout {
            Repeater {
                model: authDialog.prompts
                ColumnLayout {
                    Label { text: modelData.text; color: Theme.text }
                    TextField {
                        echoMode: modelData.echo ? TextInput.Normal : TextInput.Password
                        Layout.fillWidth: true
                        onAccepted: authDialog.accept()
                        Component.onCompleted: authDialog.fieldRefs.push(this)
                    }
                }
            }
        }
        onAccepted: {
            if (!pendingAuthSession)
                return;
            const answers = [];
            for (let i = 0; i < fieldRefs.length; ++i)
                answers.push(fieldRefs[i].text);
            App.session(pendingAuthSession).answerAuthPrompt(answers);
            fieldRefs = [];
            pendingAuthSession = 0;
        }
        onRejected: {
            if (pendingAuthSession) {
                const answers = [];
                for (let i = 0; i < fieldRefs.length; ++i)
                    answers.push("");
                App.session(pendingAuthSession).answerAuthPrompt(answers);
                fieldRefs = [];
                pendingAuthSession = 0;
            }
        }
    }
    QuickConnectDialog { id: quickConnectDialog }
    ProfileDialog { id: profileDialog }
    ImportDialog { id: importDialog }
    PaletteDialog { id: paletteDialog; onCommandPicked: (id) => root.runCommand(id) }
    ForwardDialog { id: forwardDialog }
    RunnerDialog { id: runnerDialog }
    DiagnosticsDialog { id: diagnosticsDialog }
    AboutDialog { id: aboutDialog }
    FirstRunWizard {
        id: wizard
        Component.onCompleted: if (!App.firstRunDone) open()
        onFinished: App.firstRunDone = true
    }

    // friendly error dialog (UX principle: friendly message + technical details)
    Dialog {
        id: errorDialog
        property string errorTitle: qsTr("Error")
        property string friendly
        property string technical
        property string hint
        title: errorTitle
        modal: true
        standardButtons: Dialog.Close
        parent: Overlay.overlay; anchors.centerIn: parent
        ColumnLayout {
            spacing: 8
            Label { text: errorDialog.friendly; wrapMode: Text.Wrap; Layout.maximumWidth: 420; color: Theme.text }
            Label { text: qsTr("Details:") + " " + errorDialog.technical; wrapMode: Text.WrapAnywhere;
                    Layout.maximumWidth: 420; font.pixelSize: 11; color: Theme.textMuted }
            Label { text: errorDialog.hint; wrapMode: Text.Wrap; Layout.maximumWidth: 420; color: Theme.warning;
                    visible: errorDialog.hint.length > 0; font.pixelSize: 11 }
        }
    }

    // file dialogs for import/export (platform native via Qt Quick Dialogs)
    FileDialog {
        id: importExport
        property bool importMode: true
        fileMode: FileDialog.OpenFile
        nameFilters: ["Profiles (*.json)", "SSH config (config)", "All files (*)"]
        onAccepted: {
            const err = importMode ? App.importOpenSshConfig(selectedFile.toString().replace("file://",""))
                                   : App.exportProfilesJson(selectedFile.toString().replace("file://",""));
            root.toast(importMode ? "Import" : "Export", err.length ? err : "Done", err.length > 0);
        }
        function openImport() { importMode = true; open(); }
        function openExport() { importMode = false; open(); }
    }

    // toast
    Rectangle {
        id: toastLoader
        property string title: ""; property string body: ""; property bool isError: false
        visible: false
        anchors.bottom: parent.bottom; anchors.right: parent.right; anchors.margins: 18
        width: Math.min(420, parent.width - 40); height: toastText.implicitHeight + 24
        radius: 10; color: Theme.surface; border.color: isError ? Theme.error : Theme.accent
        z: 999
        ColumnLayout {
            anchors.fill: parent; anchors.margins: 10
            Label { text: toastLoader.title; font.weight: Font.DemiBold; color: toastLoader.isError ? Theme.error : Theme.accent }
            Label { id: toastText; text: toastLoader.body; wrapMode: Text.Wrap; color: Theme.text; Layout.fillWidth: true }
        }
        Timer { id: toastTimer; interval: 5000; onTriggered: toastLoader.visible = false }
    }

    // transfer panel (bottom, toggleable)
    TransferPanel {
        id: transferPanel
        anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter
        width: parent.width * 0.8
        height: 180
        visible: Transfers.count() > 0
    }

    // global shortcuts
    Shortcut { sequence: Shortcuts.sequenceFor("app.command-palette"); onActivated: paletteDialog.openDialog() }
    Shortcut { sequence: Shortcuts.sequenceFor("app.new-connection"); onActivated: profileDialog.openNew() }
    Shortcut { sequence: Shortcuts.sequenceFor("app.quick-connect"); onActivated: quickConnectDialog.openDialog() }
    Shortcut { sequence: Shortcuts.sequenceFor("app.settings"); onActivated: root.openPage("settings") }
    Shortcut { sequence: Shortcuts.sequenceFor("app.logs"); onActivated: root.openPage("logs") }
    Shortcut { sequence: Shortcuts.sequenceFor("app.toggle-dark"); onActivated: root.runCommand("app.toggle-dark") }
    Shortcut { sequence: Shortcuts.sequenceFor("app.close-tab"); onActivated: root.runCommand("app.close-tab") }

    // system tray
    QTrayIcon { window: root }
}
