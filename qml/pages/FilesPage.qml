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
                Layout.preferredHeight: 40
                radius: Theme.radiusS
                color: Theme.surfaceContainer
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 3
                    spacing: 4
                    Label { text: qsTr("LOCAL"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.typeLabelSmall; font.letterSpacing: 0.8 }
                    TextField {
                        id: localPathField
                        Layout.fillWidth: true; Layout.fillHeight: true; text: localModel.path
                        onEditingFinished: localModel.path = text
                        font.pixelSize: 12
                    }
                    FlatButton {
                        iconName: "arrow_upward"
                        small: true
                        ToolTip.text: qsTr("Parent directory")
                        onClicked: localModel.goUp()
                    }
                }
            }

            // REMOTE path pill
            Rectangle {
                Layout.fillWidth: true
                Layout.preferredHeight: 40
                radius: Theme.radiusS
                color: Theme.surfaceContainer
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 10
                    anchors.rightMargin: 3
                    spacing: 4
                    Label { text: qsTr("REMOTE"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.typeLabelSmall; font.letterSpacing: 0.8 }
                    TextField {
                        id: remotePathField
                        Layout.fillWidth: true; Layout.fillHeight: true; text: remoteModel.path
                        onEditingFinished: remoteModel.path = text
                        font.pixelSize: 12
                    }
                    FlatButton {
                        iconName: "arrow_upward"
                        small: true
                        ToolTip.text: qsTr("Parent directory")
                        onClicked: remoteModel.goUp()
                    }
                    FlatButton {
                        iconName: "sync"
                        small: true
                        ToolTip.text: qsTr("Refresh")
                        onClicked: { localModel.refresh(); remoteModel.refresh() }
                    }
                    FlatButton {
                        text: qsTr("Terminal here")
                        iconName: "terminal"
                        variant: "outlined"
                        small: true
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
                    color: Theme.onSurfaceVariant; font.pixelSize: Theme.typeBodyMedium
                }
            }

            // ---- middle separator ----
            Rectangle { Layout.preferredWidth: 1; Layout.fillHeight: true; color: Theme.outlineVariant }

            ColumnLayout {
                Layout.preferredWidth: 92
                spacing: 4
                Item { Layout.fillHeight: true }
                FlatButton {
                    Layout.fillWidth: true
                    iconName: "file_upload"
                    ToolTip.text: qsTr("Upload")
                    onClicked: uploadSelected()
                }
                FlatButton {
                    Layout.fillWidth: true
                    iconName: "file_download"
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
                    color: Theme.onSurfaceVariant; font.pixelSize: Theme.typeBodyMedium
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
                        color: Theme.onSurfaceVariant; font.pixelSize: Theme.typeBodySmall
                        anchors.verticalCenter: parent.verticalCenter
                    }
                }
            }
        }

        // quick actions bar
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 42
            color: Theme.surfaceContainerLow
            RowLayout {
                anchors.fill: parent; anchors.margins: 4; spacing: 4
                FlatButton { iconName: "file_upload"; text: qsTr("Upload")
                    ToolTip.text: qsTr("Upload selected files to the remote server")
                    onClicked: uploadSelected() }
                FlatButton { iconName: "file_download"; text: qsTr("Download")
                    ToolTip.text: qsTr("Download selected files to the local machine")
                    onClicked: downloadSelected() }
                FlatButton { text: qsTr("Rename"); iconName: "edit"
                    ToolTip.text: qsTr("Rename the selected remote item")
                    onClicked: renameRemote() }
                FlatButton { text: qsTr("Delete"); iconName: "delete"; danger: true
                    ToolTip.text: qsTr("Delete the selected remote items")
                    onClicked: deleteRemote() }
                FlatButton { iconName: "add"; text: qsTr("New folder")
                    ToolTip.text: qsTr("Create a new folder on the remote server")
                    onClicked: remoteModel.mkdir("new-folder-" + Date.now() % 1000) }
                FlatButton { text: qsTr("Permissions"); iconName: "tune"
                    ToolTip.text: qsTr("Change permissions of the selected remote item")
                    onClicked: permsRemote() }
                FlatButton { text: qsTr("View Archive"); iconName: "folder_zip"
                    ToolTip.text: qsTr("Browse the selected archive")
                    onClicked: viewArchive() }
                Item { Layout.fillWidth: true }
                Label { font.pixelSize: Theme.typeLabelSmall; color: Theme.onSurfaceVariant
                    text: qsTr("%1 remote items").arg(remoteModel.entryCount) }
                Item { Layout.preferredWidth: 8 }
            }
        }

        Label {
            id: warnLabel
            visible: false
            color: Theme.warning; font.pixelSize: Theme.typeBodySmall
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
            MaterialIcon {
                icon: "folder_open"
                iconSize: 40
                color: Theme.onSurfaceVariant
                Layout.alignment: Qt.AlignHCenter
            }
            Label {
                text: qsTr("No active session")
                font.pixelSize: Theme.typeTitleLarge; font.weight: Font.DemiBold; color: Theme.onSurface
                Layout.alignment: Qt.AlignHCenter
            }
            Label {
                text: qsTr("Connect to a server to browse files.")
                color: Theme.onSurfaceVariant; font.pixelSize: Theme.typeBodyMedium
                Layout.alignment: Qt.AlignHCenter
            }
            FlatButton {
                text: qsTr("Connect")
                variant: "filled"
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
                MaterialIcon {
                    icon: isDir ? "folder" : (isLink ? "link" : "description")
                    iconSize: 16
                    color: isDir ? Theme.primary : (isLink ? Theme.tertiary : Theme.onSurfaceVariant)
                }
                Label { text: name; color: Theme.onSurface; font.pixelSize: Theme.typeBodySmall; elide: Text.ElideRight
                    Layout.preferredWidth: 150 }
                Label { text: sizeText; color: Theme.onSurfaceVariant; font.pixelSize: Theme.typeBodySmall; Layout.preferredWidth: 60
                    horizontalAlignment: Text.AlignRight }
                Label { text: isDir ? "" : permsText; color: Theme.onSurfaceVariant; font.pixelSize: Theme.typeBodySmall }
                Item { Layout.fillWidth: true }
                Label { text: isDir ? "" : mtimeText; color: Theme.onSurfaceVariant; font.pixelSize: Theme.typeBodySmall }
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
        Label { text: confirmDialog.text; color: Theme.onSurface }
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
