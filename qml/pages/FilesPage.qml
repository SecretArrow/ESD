import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Eclipse
import Eclipse.Internal 1.0

// Dual-panel file manager: LOCAL <-> REMOTE, with quick actions,
// context menus, permissions and archive viewing.
Rectangle {
    id: page
    property var session: null
    signal connectWanted()

    color: Theme.surface

    // A session is attached and authenticated -> show the browser,
    // otherwise show the empty-state overlay.
    readonly property bool sessionActive: session !== null && session.connected

    LocalFsModel { id: localModel }
    RemoteFsModel { id: remoteModel }

    property var profile: session ? App.profiles.profileById(session.profileId) : null

    onSessionChanged: {
        if (session && session.connected) {
            remoteModel.attachSession(session); // creates the SFTP session in C++
            const p = profile ? profile : {};
            remoteModel.path = p.sftpDefaultRemoteDir || "/";
        }
    }

    ColumnLayout {
        id: browser
        visible: page.sessionActive
        anchors.fill: parent
        spacing: 0

        // path bars
        RowLayout {
            Layout.fillWidth: true
            Layout.margins: 8
            spacing: 6

            // LOCAL path pill
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 34
                radius: Theme.radiusS
                color: Theme.surfaceAlt
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 3
                    spacing: 4
                    Label { text: qsTr("LOCAL"); color: Theme.textMuted; font.pixelSize: 10 }
                    TextField {
                        id: localPathField
                        Layout.fillWidth: true; text: localModel.path
                        onEditingFinished: localModel.path = text
                        font.pixelSize: 12
                        background: Rectangle {
                            implicitHeight: 26
                            radius: Theme.radiusS
                            color: "transparent"
                            border.width: localPathField.activeFocus ? 1 : 0
                            border.color: localPathField.activeFocus ? Theme.accent : "transparent"
                        }
                    }
                    FlatButton {
                        glyph: "↑"
                        ToolTip.text: qsTr("Parent directory")
                        onClicked: localModel.goUp()
                    }
                }
            }

            // REMOTE path pill
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 34
                radius: Theme.radiusS
                color: Theme.surfaceAlt
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 3
                    spacing: 4
                    Label { text: qsTr("REMOTE"); color: Theme.textMuted; font.pixelSize: 10 }
                    TextField {
                        id: remotePathField
                        Layout.fillWidth: true; text: remoteModel.path
                        onEditingFinished: remoteModel.path = text
                        font.pixelSize: 12
                        background: Rectangle {
                            implicitHeight: 26
                            radius: Theme.radiusS
                            color: "transparent"
                            border.width: remotePathField.activeFocus ? 1 : 0
                            border.color: remotePathField.activeFocus ? Theme.accent : "transparent"
                        }
                    }
                    FlatButton {
                        glyph: "↑"
                        ToolTip.text: qsTr("Parent directory")
                        onClicked: remoteModel.goUp()
                    }
                    FlatButton {
                        glyph: "⟳"
                        ToolTip.text: qsTr("Refresh")
                        onClicked: { localModel.refresh(); remoteModel.refresh() }
                    }
                    FlatButton {
                        text: qsTr("Terminal here")
                        showBorder: true
                        ToolTip.text: qsTr("Open a terminal in the remote directory")
                        onClicked: if (session && session.connected) {
                            App.openTerminalFor(session.sessionId);
                            session.sendToTerminal("cd " + remoteModel.path + "\n");
                        }
                    }
                }
            }
        }

        // dual panels
        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 1

            // ---- LOCAL ----
            ListView {
                id: localList
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: localModel
                clip: true
                delegate: fileDelegate
                property var selection: ({})
                property string side: "local"
                Label {
                    visible: localList.count === 0
                    anchors.centerIn: parent
                    text: qsTr("Local directory is empty")
                    color: Theme.textMuted
                }
            }

            // ---- middle actions ----
            Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: Theme.border }

            ColumnLayout {
                Layout.preferredWidth: 92
                spacing: 4
                Item { Layout.fillHeight: true }
                FlatButton {
                    Layout.fillWidth: true
                    glyph: "↑"
                    ToolTip.text: qsTr("Upload")
                    onClicked: uploadSelected()
                }
                FlatButton {
                    Layout.fillWidth: true
                    glyph: "↓"
                    ToolTip.text: qsTr("Download")
                    onClicked: downloadSelected()
                }
                Item { Layout.fillHeight: true }
            }

            // ---- REMOTE ----
            ListView {
                id: remoteList
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: remoteModel
                clip: true
                delegate: fileDelegate
                property var selection: ({})
                property string side: "remote"
                Label {
                    visible: remoteList.count === 0
                    anchors.centerIn: parent
                    text: session && session.connected ? qsTr("Empty directory")
                          : qsTr("Not connected")
                    color: Theme.textMuted
                }
                Row {
                    spacing: 8
                    visible: remoteModel.loading
                    anchors.horizontalCenter: parent.horizontalCenter
                    anchors.bottom: parent.bottom
                    anchors.bottomMargin: 10
                    BusyIndicator { running: remoteModel.loading; width: 22; height: 22 }
                    Label {
                        text: qsTr("Loading…")
                        color: Theme.textMuted; font.pixelSize: 12
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }
        }

        // quick actions bar
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 42
            color: Theme.surfaceAlt
            RowLayout {
                anchors.fill: parent; anchors.margins: 4; spacing: 4
                FlatButton { glyph: "↑"; text: qsTr("Upload")
                    ToolTip.text: qsTr("Upload selected files to the remote server")
                    onClicked: uploadSelected() }
                FlatButton { glyph: "↓"; text: qsTr("Download")
                    ToolTip.text: qsTr("Download selected files to the local machine")
                    onClicked: downloadSelected() }
                FlatButton { text: qsTr("Rename")
                    ToolTip.text: qsTr("Rename the selected remote item")
                    onClicked: renameRemote() }
                FlatButton { text: qsTr("Delete"); danger: true
                    ToolTip.text: qsTr("Delete the selected remote items")
                    onClicked: deleteRemote() }
                FlatButton { glyph: "＋"; text: qsTr("New folder")
                    ToolTip.text: qsTr("Create a new folder on the remote server")
                    onClicked: remoteModel.mkdir("new-folder-" + Date.now() % 1000) }
                FlatButton { text: qsTr("Permissions")
                    ToolTip.text: qsTr("Change permissions of the selected remote item")
                    onClicked: permsRemote() }
                FlatButton { text: qsTr("View Archive")
                    ToolTip.text: qsTr("Browse the selected archive")
                    onClicked: viewArchive() }
                Item { Layout.fillWidth: true }
                Label { font.pixelSize: 10; color: Theme.textMuted
                    text: qsTr("%1 remote items").arg(remoteModel.entryCount) }
                Item { Layout.preferredWidth: 8 }
            }
        }

        Label {
            id: warnLabel
            visible: false
            color: Theme.warning; font.pixelSize: 12
            Layout.fillWidth: true
            Layout.leftMargin: 8; Layout.rightMargin: 8; Layout.bottomMargin: 4
            wrapMode: Text.Wrap
        }
    }

    // empty state: no active session -> centered overlay instead of browser
    Rectangle {
        anchors.fill: parent
        visible: !page.sessionActive
        color: Theme.background
        ColumnLayout {
            anchors.centerIn: parent
            spacing: 10
            Label {
                text: "⇄"
                font.pixelSize: 36
                color: Theme.textMuted
                Layout.alignment: Qt.AlignHCenter
            }
            Label {
                text: qsTr("No active session")
                font.pixelSize: 17; font.weight: Font.DemiBold; color: Theme.text
                Layout.alignment: Qt.AlignHCenter
            }
            Label {
                text: qsTr("Connect to a server to browse files.")
                color: Theme.textMuted; font.pixelSize: 13
                Layout.alignment: Qt.AlignHCenter
            }
            FlatButton {
                text: qsTr("Connect")
                accent: true
                Layout.alignment: Qt.AlignHCenter
                Layout.topMargin: 8
                onClicked: page.connectWanted()
            }
        }
    }

    // shared delegate
    Component {
        id: fileDelegate
        ItemDelegate {
            id: row
            width: ListView.view.width
            height: 28
            hoverEnabled: true
            highlighted: ListView.view.selection[index] === true
            background: Rectangle {
                color: row.highlighted ? Theme.accentSoft
                     : (row.hovered ? Theme.hover : "transparent")
                Behavior on color { ColorAnimation { duration: 120 } }
            }
            onClicked: {
                if (mouse.modifiers & Qt.ControlModifier)
                    ListView.view.selection[index] = !ListView.view.selection[index];
                else
                    ListView.view.selection = ({ [index]: true });
                ListView.view.selectionChanged();
            }
            onDoubleClicked: {
                const side = ListView.view.side;
                const model_ = side === "local" ? localModel : remoteModel;
                const e = model_.entryAt(index);
                if (e.isDir)
                    model_.path = model_.path + (model_.path.endsWith("/") ? "" : "/") + e.name;
                else if (side === "remote")
                    openRemoteFile(e);
            }
            TapHandler { acceptedButtons: Qt.RightButton; onTapped: sideMenu.popup() }
            Menu {
                id: sideMenu
                MenuItem { text: qsTr("Open"); onTriggered: {
                    const e = ListView.view.side === "local" ? localModel.entryAt(index) : remoteModel.entryAt(index);
                    if (e.isDir) (ListView.view.side === "local" ? localModel : remoteModel).path =
                        (ListView.view.side === "local" ? localModel : remoteModel).path
                        + (ListView.view.side === "local" ? localModel : remoteModel).path.endsWith("/") ? "" : "/" + e.name;
                } }
                MenuItem { text: qsTr("Download"); onTriggered: downloadSelected() }
                MenuItem { text: qsTr("Upload"); onTriggered: uploadSelected() }
                MenuItem { text: qsTr("Copy SSH path"); onTriggered: {
                    const e = remoteModel.entryAt(index);
                    App.copyToClipboard(remoteModel.path + "/" + e.name);
                } }
                MenuItem { text: qsTr("View Archive"); onTriggered: viewArchive() }
                MenuItem { text: qsTr("Permissions"); onTriggered: permsRemote() }
                MenuItem { text: qsTr("Rename"); onTriggered: renameRemote() }
                MenuItem { text: qsTr("Delete"); onTriggered: deleteRemote() }
            }
            contentItem: RowLayout {
                spacing: 6
                Label { text: isDir ? "📁" : (isLink ? "🔗" : "📄"); font.pixelSize: 13 }
                Label { text: name; color: Theme.text; font.pixelSize: 12; elide: Text.ElideRight
                    Layout.preferredWidth: 150 }
                Label { text: sizeText; color: Theme.textMuted; font.pixelSize: 12; Layout.preferredWidth: 60
                    horizontalAlignment: Text.AlignRight }
                Label { text: isDir ? "" : permsText; color: Theme.textMuted; font.pixelSize: 12 }
                Item { Layout.fillWidth: true }
                Label { text: isDir ? "" : mtimeText; color: Theme.textMuted; font.pixelSize: 12 }
                Item { Layout.preferredWidth: 6 }
            }
        }
    }

    // ---- operations ----
    function selectedRemoteRows() {
        const rows = [];
        for (const k in remoteList.selection) if (remoteList.selection[k]) rows.push(parseInt(k));
        return rows;
    }
    function selectedLocalRows() {
        const rows = [];
        for (const k in localList.selection) if (localList.selection[k]) rows.push(parseInt(k));
        return rows;
    }

    function uploadSelected() {
        if (!session || !session.connected) return;
        const rows = selectedLocalRows();
        for (const r of rows) {
            const e = localModel.entryAt(r);
            const local = localModel.path + "/" + e.name;
            const remote = remoteModel.path + "/" + e.name;
            Transfers.enqueue({
                direction: "upload", sessionId: session.sessionId,
                localPath: local, remotePath: remote, recursive: e.isDir });
        }
    }
    function downloadSelected() {
        if (!session || !session.connected) return;
        const rows = selectedRemoteRows();
        for (const r of rows) {
            const e = remoteModel.entryAt(r);
            const local = localModel.path + "/" + e.name;
            const remote = remoteModel.path + "/" + e.name;
            Transfers.enqueue({
                direction: "download", sessionId: session.sessionId,
                localPath: local, remotePath: remote, recursive: e.isDir });
        }
    }
    function renameRemote() {
        const rows = selectedRemoteRows();
        if (rows.length !== 1) return;
        const e = remoteModel.entryAt(rows[0]);
        renameDialog.originalName = e.name;
        renameDialog.row = rows[0];
        renameDialog.open();
    }
    function deleteRemote() {
        const rows = selectedRemoteRows();
        if (!rows.length) return;
        if (App.settings.confirmDelete) {
            confirmDialog.text = qsTr("Delete %1 selected item(s)?").arg(rows.length);
            confirmDialog.action = () => remoteModel.removeSelected(rows);
            confirmDialog.open();
        } else remoteModel.removeSelected(rows);
    }
    function permsRemote() {
        const rows = selectedRemoteRows();
        if (rows.length !== 1) return;
        const e = remoteModel.entryAt(rows[0]);
        permsDialog.row = rows[0];
        permsDialog.octal = e.perms;
        permsDialog.open();
    }
    function viewArchive() {
        const rows = selectedRemoteRows();
        if (rows.length !== 1) return;
        const e = remoteModel.entryAt(rows[0]);
        archiveDialog.openRemote(session, remoteModel.path + "/" + e.name);
    }
    function openRemoteFile(e) {
        if (e.size > 5 * 1024 * 1024) {
            warnLabel.text = qsTr("Large file (over 5 MB) - use Download instead of inline editing.");
            warnLabel.visible = true;
            return;
        }
        archiveDialog.openTextPreview(session, remoteModel.path + "/" + e.name);
    }

    Dialog {
        id: renameDialog
        property string originalName: ""
        property int row: -1
        title: qsTr("Rename"); modal: true
        parent: Overlay.overlay; anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        TextField { id: renameField; text: renameDialog.originalName; Layout.fillWidth: true }
        onAccepted: remoteModel.rename(row, renameField.text)
    }
    Dialog {
        id: confirmDialog
        property string text: ""
        property var action: null
        title: qsTr("Confirm"); modal: true
        parent: Overlay.overlay; anchors.centerIn: parent
        standardButtons: Dialog.Yes | Dialog.No
        Label { text: confirmDialog.text; color: Theme.text }
        onAccepted: if (action) action()
    }
    Dialog {
        id: permsDialog
        property int row: -1
        property string octal: "644"
        title: qsTr("Permissions (octal)"); modal: true
        parent: Overlay.overlay; anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        TextField { id: permsField; text: permsDialog.octal; Layout.fillWidth: true }
        onAccepted: remoteModel.setPermissions(row, permsField.text)
    }

    // archive viewer + text preview (C++ service via ArchiveBridge)
    ArchiveViewerDialog { id: archiveDialog; session: page.session }
}
