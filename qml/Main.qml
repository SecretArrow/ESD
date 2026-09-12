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
    minimumWidth: 980
    minimumHeight: 640
    visible: true
    font.pixelSize: 13
    title: currentSession ? qsTr("%1 — Eclipse SSH Desktop").arg(currentSession.name)
                          : qsTr("Eclipse SSH Desktop")
    color: Theme.surface
    onClosing: (close) => {
        if (App.settings.minimizeToTray) {
            close.accepted = false;
            trayIcon.handleWindowClose();
        }
    }
    Component.onCompleted: {
        trayIcon.window = root;
        transferPanel.visible = Transfers.count() > 0;
    }

    // ---- app state -----------------------------------------------------------
    property int currentTab: -1
    property var currentSession: currentTab >= 0 ? App.sessions.sessionAt(currentTab) : null
    property string currentPage: "dashboard"   // dashboard | session | logs | settings
    property var pendingHostKeySession: 0
    property var pendingAuthSession: 0

    function openPage(p) { currentPage = p; }

    function selectTab(i) {
        if (i >= 0 && i < App.sessions.rowCount()) {
            currentTab = i;
            currentPage = "session";
        }
    }

    function selectSessionId(sessionId) {
        for (let i = 0; i < App.sessions.rowCount(); ++i) {
            if (App.sessions.get(i).sessionId === sessionId) {
                selectTab(i);
                return true;
            }
        }
        return false;
    }

    // Keep currentTab valid whenever the session list changes; return to the
    // dashboard when the last tab is closed (no more blank session page).
    function clampTab() {
        const n = App.sessions.rowCount();
        if (n === 0) {
            if (currentTab !== -1)
                currentTab = -1;
            if (currentPage === "session")
                currentPage = "dashboard";
        } else if (currentTab >= n) {
            currentTab = n - 1;
            currentPage = "session";
        }
    }

    function closeTabAt(i) {
        if (i >= 0 && i < App.sessions.rowCount())
            App.sessions.closeSession(i);
    }

    function closeCurrentTab() {
        if (currentTab >= 0)
            closeTabAt(currentTab);
    }

    // Central connect entry: asks for the password up front when the profile
    // has no stored secret, then lands the user on the new session tab.
    function connectToProfile(profileId) {
        if (!profileId)
            return;
        if (App.needsPasswordPrompt(profileId)) {
            const p = App.profiles.profileById(profileId);
            passwordDialog.profileId = profileId;
            passwordDialog.pname = p.name ? p.name : "";
            passwordDialog.userHost = (p.username ? p.username : "") + "@" + (p.host ? p.host : "");
            passwordDialog.keyAuth = p.authMethod === "publickey";
            passwordDialog.open();
            return;
        }
        const sid = App.connectProfile(profileId);
        if (sid)
            selectSessionId(sid);
    }

    // ---- notifications toast -------------------------------------------------
    function toast(title, body, isError) {
        toastLoader.title = title;
        toastLoader.body = body;
        toastLoader.isError = isError;
        if (toastLoader.visible)
            toastTimer.restart();
        else
            toastLoader.visible = true;
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
        case "app.disconnect":
            if (currentSession)
                App.disconnectSession(currentSession.sessionId);
            break;
        case "app.close-tab": closeCurrentTab(); break;
        case "app.command-palette": paletteDialog.openDialog(); break;
        }
    }

    onCurrentPageChanged: statusTimer.restart()

    // ---- App signal wiring ----------------------------------------------------
    Connections {
        target: App
        function onCommandRequested(id) { root.runCommand(id); }
        function onUiRaiseRequested() {
            root.showNormal()
            root.raise()
            root.requestActivate()
        }
        function onHostKeyNeeded(sessionId, keyInfo, isChanged) {
            pendingHostKeySession = sessionId;
            hostKeyDialog.keyInfo = keyInfo;
            hostKeyDialog.isChangedKey = isChanged;
            hostKeyDialog.open();
        }
        function onAuthPromptNeeded(sessionId, prompts) {
            pendingAuthSession = sessionId;
            authDialog.prompts = prompts;
            const s = App.session(sessionId);
            authDialog.sessionHost = s ? s.host : "";
            authDialog.open();
        }
        function onAuthFailed(sessionId, friendly, technical, hint) {
            errorDialog.errorTitle = qsTr("Authentication failed");
            errorDialog.friendly = friendly; errorDialog.technical = technical; errorDialog.hint = hint;
            errorDialog.open();
        }
        function onConnected(sessionId) {
            // Visible success feedback: land on the new tab and open a terminal.
            root.selectSessionId(sessionId);
            const s = App.session(sessionId);
            if (s)
                s.openTerminal(80, 24);
        }
        function onReconnecting(sessionId, attempt, maxAttempts) {
            const s = App.session(sessionId);
            root.toast(qsTr("Reconnecting"),
                       qsTr("%1 — attempt %2 of %3…").arg(s ? s.name : "").arg(attempt).arg(maxAttempts),
                       false);
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
        function onSessionListChanged() {
            sessionTabsModel.refresh();
            connList.reload();
            root.clampTab();
        }
        function onSessionStateChanged() {
            sessionTabsModel.refresh();
            connList.reload();
        }
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
                    Rectangle { width: 24; height: 24; radius: 7; color: Theme.accent }
                    Label { text: "Eclipse SSH"; font.weight: Font.DemiBold; font.pixelSize: 15; color: Theme.text }
                    Item { Layout.fillWidth: true }
                    ToolButton {
                        text: "+"
                        font.pixelSize: 16
                        width: 30; height: 30
                        ToolTip.text: qsTr("New connection"); ToolTip.visible: hovered; ToolTip.delay: 550
                        onClicked: profileDialog.openNew()
                    }
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

                        function sessionStateFor(profileId) {
                            for (let i = 0; i < App.sessions.rowCount(); ++i) {
                                const s = App.sessions.get(i);
                                if (s.profileId === profileId)
                                    return s.state;
                            }
                            return "Idle";
                        }

                        delegate: ItemDelegate {
                            id: connItem
                            width: connList.width
                            height: 50
                            hoverEnabled: true
                            highlighted: currentSession !== null && currentSession.profileId === model.id
                            onClicked: root.connectToProfile(model.id)

                            background: Rectangle {
                                radius: Theme.radiusS
                                color: connItem.highlighted ? Theme.accentSoft
                                                            : (connItem.hovered ? Theme.hover : "transparent")
                                Behavior on color { ColorAnimation { duration: 120 } }
                            }

                            contentItem: RowLayout {
                                spacing: 8
                                StatusDot { state: model.state }
                                ColumnLayout {
                                    spacing: 1
                                    Layout.fillWidth: true
                                    Label { text: model.name; color: Theme.text; font.pixelSize: 13;
                                        elide: Text.ElideRight; Layout.fillWidth: true }
                                    Label {
                                        text: model.state !== "Idle" ? model.state + "  ·  " + model.userHost
                                                                     : model.userHost
                                        color: Theme.textMuted; font.pixelSize: 10;
                                        elide: Text.ElideRight; Layout.fillWidth: true
                                    }
                                }
                                Label { text: model.favorite ? "★" : "○";
                                    color: model.favorite ? "#e2b12c" : Theme.textMuted; font.pixelSize: 12 }
                            }

                            Menu {
                                id: ctxMenu
                                MenuItem { text: qsTr("Connect"); onTriggered: root.connectToProfile(model.id) }
                                MenuItem {
                                    text: qsTr("Disconnect")
                                    enabled: model.state !== "Idle" && model.state !== "Disconnected"
                                    onTriggered: {
                                        for (let i = 0; i < App.sessions.rowCount(); ++i) {
                                            const s = App.sessions.get(i);
                                            if (s.profileId === model.id) { App.disconnectSession(s.sessionId); break; }
                                        }
                                    }
                                }
                                MenuItem { text: model.favorite ? qsTr("Remove favorite") : qsTr("Add favorite")
                                    onTriggered: App.toggleFavorite(model.id) }
                                MenuItem { text: qsTr("Duplicate"); onTriggered: App.duplicateProfile(model.id) }
                                MenuItem { text: qsTr("Edit"); onTriggered: profileDialog.openEdit(model.id) }
                                MenuItem { text: qsTr("Diagnostics"); onTriggered: { diagnosticsDialog.profileId = model.id; diagnosticsDialog.openDialog(); } }
                                MenuItem { text: qsTr("Delete"); onTriggered: App.deleteProfile(model.id) }
                            }
                            MouseArea { anchors.fill: parent; acceptedButtons: Qt.RightButton
                                onClicked: ctxMenu.popup() }
                        }

                        Component.onCompleted: reload()
                        function reload() {
                            connModel.clear();
                            const all = App.profiles.allProfiles();
                            const q = searchField.text.toLowerCase();
                            const favorites = [];
                            const others = [];
                            for (let i = 0; i < all.length; ++i) {
                                const p = all[i];
                                if (q && (p.name + " " + p.host + " " + p.username).toLowerCase().indexOf(q) < 0) continue;
                                const item = { id: p.id, name: p.name, userHost: p.username + "@" + p.host,
                                              favorite: p.favorite,
                                              state: connList.sessionStateFor(p.id),
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
                Layout.fillWidth: true; Layout.preferredHeight: 50; color: Theme.surface
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }
                RowLayout {
                    anchors.fill: parent; anchors.margins: 8; spacing: 6
                    FlatButton { text: qsTr("New"); glyph: "＋"; ToolTip.text: qsTr("New connection profile")
                        onClicked: profileDialog.openNew() }
                    FlatButton { text: qsTr("Connect"); glyph: "»"; ToolTip.text: qsTr("Quick connect")
                        onClicked: quickConnectDialog.openDialog() }
                    Rectangle { width: 1; height: 20; color: Theme.border }
                    FlatButton { text: qsTr("Terminal"); onClicked: root.runCommand("app.new-terminal") }
                    FlatButton { text: qsTr("Files"); onClicked: root.runCommand("app.open-sftp") }
                    FlatButton { text: qsTr("Tunnel"); onClicked: forwardDialog.openDialog() }
                    FlatButton { text: qsTr("Runner"); onClicked: runnerDialog.openDialog() }
                    Item { Layout.fillWidth: true }
                    FlatButton {
                        text: qsTr("Disconnect"); danger: true
                        visible: currentSession !== null && currentSession.state !== "Idle"
                                 && currentSession.state !== "Disconnected"
                        onClicked: if (currentSession) App.disconnectSession(currentSession.sessionId)
                    }
                    FlatButton { text: qsTr("Palette"); showBorder: true; onClicked: paletteDialog.openDialog() }
                    FlatButton { text: qsTr("Settings"); onClicked: root.openPage("settings") }
                }
            }

            // ======== session tab strip (custom: big, closable, scrollable) ========
            Rectangle {
                Layout.fillWidth: true
                height: Theme.tabHeight
                visible: sessionTabsModel.count > 0
                color: Theme.surface
                Rectangle { anchors.bottom: parent.bottom; width: parent.width; height: 1; color: Theme.border }

                Flickable {
                    anchors.fill: parent
                    contentWidth: tabRow.implicitWidth + 8
                    contentHeight: height
                    clip: true
                    boundsBehavior: Flickable.StopAtBounds
                    interactive: contentWidth > width

                    Row {
                        id: tabRow
                        x: 4
                        height: parent.height
                        spacing: 4

                        Repeater {
                            model: sessionTabsModel

                            delegate: Rectangle {
                                id: tabDelegate
                                width: Math.max(150, Math.min(200, tabName.implicitWidth + 68))
                                height: parent ? parent.height : Theme.tabHeight
                                property bool isChecked: root.currentTab === index
                                property bool tabHovered: tabMouse.containsMouse
                                radius: Theme.radiusS
                                color: isChecked ? Theme.surfaceAlt
                                                 : (tabHovered ? Theme.hover : "transparent")
                                Behavior on color { ColorAnimation { duration: 130 } }

                                RowLayout {
                                    anchors.fill: parent
                                    anchors.leftMargin: 12
                                    anchors.rightMargin: 6
                                    spacing: 8

                                    StatusDot { state: model.state }

                                    Label {
                                        id: tabName
                                        text: model.name
                                        font.pixelSize: 13
                                        font.weight: tabDelegate.isChecked ? Font.DemiBold : Font.Normal
                                        color: tabDelegate.isChecked ? Theme.text : Theme.textMuted
                                        elide: Text.ElideRight
                                        Layout.fillWidth: true
                                        Behavior on color { ColorAnimation { duration: 130 } }
                                    }

                                    ToolButton {
                                        id: tabClose
                                        width: 22; height: 22
                                        opacity: (tabDelegate.isChecked || tabDelegate.tabHovered) ? 1 : 0
                                        Behavior on opacity { NumberAnimation { duration: 120 } }
                                        contentItem: Label {
                                            text: "✕"; font.pixelSize: 11
                                            color: tabCloseMouse.containsMouse ? Theme.error : Theme.textMuted
                                            anchors.centerIn: parent
                                        }
                                        background: Rectangle {
                                            radius: 4
                                            color: tabCloseMouse.containsMouse
                                                   ? Qt.rgba(Theme.error.r, Theme.error.g, Theme.error.b, 0.14)
                                                   : "transparent"
                                            Behavior on color { ColorAnimation { duration: 100 } }
                                        }
                                        MouseArea {
                                            id: tabCloseMouse
                                            anchors.fill: parent
                                            hoverEnabled: true
                                            cursorShape: Qt.PointingHandCursor
                                            onClicked: root.closeTabAt(index)
                                        }
                                        ToolTip.visible: tabCloseMouse.containsMouse
                                        ToolTip.text: qsTr("Close tab")
                                        ToolTip.delay: 500
                                    }
                                }

                                // active underline
                                Rectangle {
                                    anchors.bottom: parent.bottom
                                    anchors.horizontalCenter: parent.horizontalCenter
                                    width: parent.width - 20
                                    height: 2
                                    radius: 1
                                    color: Theme.accent
                                    opacity: tabDelegate.isChecked ? 1 : 0
                                    Behavior on opacity { NumberAnimation { duration: 150 } }
                                }

                                MouseArea {
                                    id: tabMouse
                                    anchors.fill: parent
                                    hoverEnabled: true
                                    acceptedButtons: Qt.LeftButton | Qt.MiddleButton
                                    cursorShape: Qt.PointingHandCursor
                                    onClicked: (mouse) => {
                                        if (mouse.button === Qt.MiddleButton)
                                            root.closeTabAt(index);
                                        else
                                            root.selectTab(index);
                                    }
                                }

                                ToolTip.visible: tabMouse.containsMouse && !tabCloseMouse.containsMouse
                                ToolTip.text: model.username + "@" + model.host
                                              + (model.port !== 22 ? ":" + model.port : "")
                                              + "  ·  " + model.state
                                ToolTip.delay: 600

                                Menu {
                                    id: tabMenu
                                    MenuItem { text: qsTr("Close tab"); onTriggered: root.closeTabAt(index) }
                                    MenuItem {
                                        text: qsTr("Close other tabs")
                                        enabled: sessionTabsModel.count > 1
                                        onTriggered: {
                                            for (let i = sessionTabsModel.count - 1; i >= 0; --i)
                                                if (i !== index) App.sessions.closeSession(i);
                                        }
                                    }
                                    MenuItem {
                                        text: qsTr("Close all tabs")
                                        onTriggered: {
                                            for (let i = sessionTabsModel.count - 1; i >= 0; --i)
                                                App.sessions.closeSession(i);
                                        }
                                    }
                                    MenuSeparator {}
                                    MenuItem {
                                        text: qsTr("Duplicate session")
                                        onTriggered: {
                                            const s = App.sessions.sessionAt(index);
                                            if (s) s.duplicateSession();
                                        }
                                    }
                                    MenuItem {
                                        text: qsTr("Disconnect")
                                        enabled: model.state !== "Idle" && model.state !== "Disconnected"
                                        onTriggered: App.disconnectSession(model.sessionId)
                                    }
                                }
                            }
                        }
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
                    onConnectRequested: (pid) => root.connectToProfile(pid)
                    onQuickConnect: quickConnectDialog.openDialog()
                    onNewProfile: profileDialog.openNew()
                }

                SessionPage {
                    id: sessionPage
                    session: root.currentSession
                    visible: currentPage === "session"
                    onCloseRequested: root.closeCurrentTab()
                }

                LogsPage {}

                SettingsPage {}
            }

            // status bar
            Rectangle {
                Layout.fillWidth: true; Layout.preferredHeight: 28; color: Theme.surfaceAlt
                Rectangle { width: parent.width; height: 1; color: Theme.border }
                RowLayout {
                    anchors.fill: parent; anchors.leftMargin: 12; anchors.rightMargin: 12; spacing: 8
                    StatusDot { state: currentSession ? currentSession.state : "Idle" }
                    Label {
                        text: {
                            if (!currentSession) return qsTr("Ready");
                            return currentSession.name + "  ·  " + currentSession.state
                                    + (currentSession.latencyMs >= 0
                                       ? "  ·  " + currentSession.latencyMs + " ms" : "")
                                    + "  ·  " + currentSession.engineName;
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
        property string sessionHost: ""
        title: sessionHost.length ? qsTr("Authentication required — %1").arg(sessionHost)
                                  : qsTr("Authentication required")
        standardButtons: Dialog.Ok | Dialog.Cancel
        parent: Overlay.overlay; anchors.centerIn: parent; modal: true
        onPromptsChanged: fieldRefs = []
        onOpened: if (fieldRefs.length > 0) fieldRefs[0].forceActiveFocus()
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

    // up-front password prompt for profiles without a stored secret
    Dialog {
        id: passwordDialog
        property int profileId: 0
        property string pname: ""
        property string userHost: ""
        property bool keyAuth: false
        title: pname.length ? qsTr("Connect to %1").arg(pname) : qsTr("Connect")
        modal: true
        standardButtons: Dialog.Cancel
        parent: Overlay.overlay; anchors.centerIn: parent
        width: 380
        onOpened: pwdField.forceActiveFocus()
        ColumnLayout {
            width: parent.width
            spacing: 8
            Label { text: passwordDialog.userHost; color: Theme.textMuted; font.pixelSize: 12 }
            Label { text: passwordDialog.keyAuth ? qsTr("Key passphrase") : qsTr("Password"); color: Theme.text }
            TextField {
                id: pwdField
                echoMode: TextInput.Password
                Layout.fillWidth: true
                placeholderText: passwordDialog.keyAuth ? qsTr("passphrase") : qsTr("password")
                onAccepted: passwordDialog.accept()
            }
            Label {
                text: qsTr("The secret stays in memory for this session unless you enable "
                           + "remembering in the profile editor.")
                wrapMode: Text.Wrap
                Layout.fillWidth: true
                font.pixelSize: 11
                color: Theme.textMuted
            }
        }
        footer: DialogButtonBox {
            Button { text: qsTr("Cancel"); flat: true; onClicked: passwordDialog.reject() }
            FlatButton { text: qsTr("Connect"); accent: true; onClicked: passwordDialog.accept() }
        }
        onAccepted: {
            const sid = keyAuth ? App.connectProfile(profileId, "", pwdField.text)
                                : App.connectProfile(profileId, pwdField.text, "");
            pwdField.clear();
            if (sid)
                root.selectSessionId(sid);
        }
        onRejected: pwdField.clear()
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
            spacing: 10
            RowLayout {
                spacing: 12
                Rectangle {
                    width: 40; height: 40; radius: 20
                    color: Qt.rgba(Theme.error.r, Theme.error.g, Theme.error.b, 0.15)
                    Label { anchors.centerIn: parent; text: "✕"; color: Theme.error;
                            font.pixelSize: 17; font.bold: true }
                }
                Label { text: errorDialog.friendly; wrapMode: Text.Wrap; Layout.maximumWidth: 380;
                        Layout.fillWidth: true; color: Theme.text; font.pixelSize: 14 }
            }
            Label { text: qsTr("Details:") + " " + errorDialog.technical; wrapMode: Text.WrapAnywhere;
                    Layout.maximumWidth: 430; font.pixelSize: 11; color: Theme.textMuted
                    visible: errorDialog.technical.length > 0 }
            Label { text: errorDialog.hint; wrapMode: Text.Wrap; Layout.maximumWidth: 430; color: Theme.warning;
                    visible: errorDialog.hint.length > 0; font.pixelSize: 11 }
            FlatButton {
                text: qsTr("Copy details"); showBorder: true; glyph: "⧉"
                visible: errorDialog.technical.length > 0
                onClicked: {
                    const all = errorDialog.friendly + "\n" + errorDialog.technical
                                + (errorDialog.hint.length ? "\n" + errorDialog.hint : "");
                    App.copyToClipboard(all);
                    root.toast(qsTr("Copied"), qsTr("Error details copied to clipboard."), false);
                }
            }
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

    // toast (animated, iconized, auto-dismiss)
    Rectangle {
        id: toastLoader
        property string title: ""; property string body: ""; property bool isError: false
        visible: false
        opacity: 0
        anchors.bottom: parent.bottom; anchors.right: parent.right; anchors.margins: 18
        width: Math.min(440, parent.width - 40)
        height: Math.max(62, toastCol.implicitHeight + 20)
        radius: Theme.radiusM
        color: Theme.surface
        border.color: toastLoader.isError ? Theme.error : Theme.accent
        border.width: 1
        z: 999
        transform: Translate {
            id: toastShift
            Behavior on y { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
        }
        onVisibleChanged: {
            if (visible) {
                toastShift.y = 14;
                toastLoader.opacity = 0;
                Qt.callLater(function() { toastShift.y = 0; toastLoader.opacity = 1; });
                toastTimer.restart();
            }
        }
        Behavior on opacity { NumberAnimation { duration: 200; easing.type: Easing.OutCubic } }
        RowLayout {
            id: toastCol
            anchors.fill: parent; anchors.margins: 12; spacing: 10
            Rectangle {
                width: 30; height: 30; radius: 15
                color: toastLoader.isError ? Qt.rgba(Theme.error.r, Theme.error.g, Theme.error.b, 0.15)
                                           : Theme.accentSoft
                Label { anchors.centerIn: parent; text: toastLoader.isError ? "✕" : "✔"
                        color: toastLoader.isError ? Theme.error : Theme.accent
                        font.pixelSize: 14; font.bold: true }
            }
            ColumnLayout {
                Layout.fillWidth: true; spacing: 2
                Label { text: toastLoader.title; font.weight: Font.DemiBold; color: Theme.text;
                        font.pixelSize: 13; Layout.fillWidth: true }
                Label { id: toastText; text: toastLoader.body; wrapMode: Text.Wrap; color: Theme.textMuted;
                        font.pixelSize: 12; Layout.fillWidth: true; visible: text.length > 0 }
            }
            ToolButton {
                text: "✕"
                width: 26; height: 26
                onClicked: { toastTimer.stop(); toastLoader.opacity = 0; toastShift.y = 10; toastHideTimer.restart() }
                contentItem: Label { text: "✕"; font.pixelSize: 11; color: Theme.textMuted; anchors.centerIn: parent }
                background: Rectangle { radius: 4; color: parent.hovered ? Theme.hover : "transparent" }
            }
        }
        Timer { id: toastTimer; interval: 4500; onTriggered: {
            toastLoader.opacity = 0; toastShift.y = 10; toastHideTimer.restart() } }
        Timer { id: toastHideTimer; interval: 230; onTriggered: toastLoader.visible = false }
    }

    // transfer panel (bottom, toggleable) — visibility now REACTS to transfer
    // activity (the old `Transfers.count() > 0` binding never re-evaluated).
    TransferPanel {
        id: transferPanel
        anchors.bottom: parent.bottom; anchors.horizontalCenter: parent.horizontalCenter
        width: parent.width * 0.8
        height: 180
        visible: false
    }
    Connections {
        target: Transfers
        function onListChanged() { transferPanel.visible = Transfers.count() > 0; }
        function onTransferFinished(ok, title) { transferPanel.visible = Transfers.count() > 0; }
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
