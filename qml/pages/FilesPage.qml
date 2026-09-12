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
    color: Theme.surface

    LocalFsModel { id: localModel }
    RemoteFsModel { id: remoteModel }

    property var profile: session ? App.profiles.get(session.profileId) : null

    onSessionChanged: {
        if (session && session.connected) {
            const s = session.createSftpSession();
            if (s) remoteModel.setSession(session.sessionId, s);
            const p = profile ? profile : {};
            remoteModel.path = p.sftpDefaultRemoteDir || "/";
        }
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // path bars
        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 38
            spacing: 6
            Item { Layout.preferredWidth: 6 }
            Label { text: qsTr("LOCAL"); color: Theme.textMuted; font.pixelSize: 10 }
            TextField {
                id: localPathField
                Layout.fillWidth: true; text: localModel.path
                onEditingFinished: localModel.path = text
                font.pixelSize: 12
            }
            Button { flat: true; text: "↑"; onClicked: localModel.goUp() }
            Item { Layout.preferredWidth: 18 }
            Label { text: qsTr("REMOTE"); color: Theme.textMuted; font.pixelSize: 10 }
            TextField {
                id: remotePathField
                Layout.fillWidth: true; text: remoteModel.path
                onEditingFinished: remoteModel.path = text
                font.pixelSize: 12
            }
            Button { flat: true; text: "↑"; onClicked: remoteModel.goUp() }
            Button { flat: true; text: "⟳"
                ToolTip.text: qsTr("Refresh"); ToolTip.visible: hovered
                onClicked: { localModel.refresh(); remoteModel.refresh() } }
            Button { flat: true; text: qsTr("Terminal here")
                onClicked: if (session && session.connected) {
                    App.openTerminalFor(session.sessionId);
                    session.sendToTerminal("cd " + remoteModel.path + "\n");
                } }
            Item { Layout.preferredWidth: 6 }
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
                Button { Layout.fillWidth: true; text: "→"; flat: true
                    ToolTip.text: qsTr("Upload"); ToolTip.visible: hovered
                    onClicked: uploadSelected() }
                Button { Layout.fillWidth: true; text: "←"; flat: true
                    ToolTip.text: qsTr("Download"); ToolTip.visible: hovered
                    onClicked: downloadSelected() }
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
            }
        }

        // quick actions bar
        Rectangle {
            Layout.fillWidth: true
            Layout.preferredHeight: 34
            color: Theme.surfaceAlt
            RowLayout {
                anchors.fill: parent; anchors.margins: 4; spacing: 4
                Button { flat: true; text: qsTr("Upload")
                    onClicked: uploadSelected() }
                Button { flat: true; text: qsTr("Download")
                    onClicked: downloadSelected() }
                Button { flat: true; text: qsTr("Rename"); onClicked: renameRemote() }
                Button { flat: true; text: qsTr("Delete"); onClicked: deleteRemote() }
                Button { flat: true; text: qsTr("New folder"); onClicked: remoteModel.mkdir("new-folder-" + Date.now() % 1000) }
                Button { flat: true; text: qsTr("Permissions"); onClicked: permsRemote() }
                Button { flat: true; text: qsTr("View Archive"); onClicked: viewArchive() }
                Item { Layout.fillWidth: true }
                Label { font.pixelSize: 10; color: Theme.textMuted
                    text: qsTr("%1 remote items").arg(remoteModel.entryCount) }
                Item { Layout.preferredWidth: 8 }
            }
        }
    }

    // shared delegate
    Component {
        id: fileDelegate
        ItemDelegate {
            width: ListView.view.width
            height: 26
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
                Label { text: isDir ? "📁" : (isLink ? "🔗" : "📄"); font.pixelSize: 12 }
                Label { text: name; color: Theme.text; font.pixelSize: 12; elide: Text.ElideRight
                    Layout.preferredWidth: 150 }
                Label { text: sizeText; color: Theme.textMuted; font.pixelSize: 10; Layout.preferredWidth: 60
                    horizontalAlignment: Text.AlignRight }
                Label { text: isDir ? "" : permsText; color: Theme.textMuted; font.pixelSize: 10 }
                Item { Layout.fillWidth: true }
                Label { text: isDir ? "" : mtimeText; color: Theme.textMuted; font.pixelSize: 10 }
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

    Label { id: warnLabel; visible: false; color: Theme.warning; font.pixelSize: 11; Layout.fillWidth: true;
        wrapMode: Text.Wrap }
}
