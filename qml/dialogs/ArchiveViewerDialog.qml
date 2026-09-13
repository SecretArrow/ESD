import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Module import: gives access to the Theme singleton (same-module types in
// subdirectories are not visible without it - binding errors otherwise).
import Eclipse

// Remote archive viewer: lists entries without downloading the whole archive.
Dialog {
    id: dlg
    property var session: null
    property string remotePath: ""
    title: remotePath
    modal: true
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 620
    height: 560
    standardButtons: Dialog.Close

    function openRemote(session_, path) {
        session = session_; remotePath = path;
        entriesModel.clear();
        const list = App.listArchive(session.sessionId, path);
        for (let i = 0; i < list.length; ++i) {
            const e = list[i];
            if (e.error) { errorLabel.text = e.error; errorLabel.visible = true; return }
            entriesModel.append(e);
        }
        open();
    }
    function openTextPreview(session_, path) {
        const res = App.previewTextFile(session.sessionId, path);
        if (!res.ok) { errorLabel.text = res.error; errorLabel.visible = true; remotePath = path; open(); return }
        previewArea.text = res.content;
        previewPage.visible = true; listPage.visible = false; remotePath = path; open();
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 12

        Label {
            id: errorLabel
            visible: false
            color: Theme.error
            font.pixelSize: Theme.typeBodySmall
            wrapMode: Text.Wrap
            Layout.fillWidth: true
        }

        // list page
        ColumnLayout {
            id: listPage
            visible: true
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 8
            ListView {
                id: entryList
                Layout.fillWidth: true
                Layout.fillHeight: true
                model: ListModel { id: entriesModel }
                clip: true
                spacing: 2
                delegate: RowLayout {
                    width: entryList.width
                    spacing: 10
                    MaterialIcon {
                        icon: isDir ? "folder" : "description"
                        iconSize: 18
                        color: isDir ? Theme.primary : Theme.onSurfaceVariant
                    }
                    Label {
                        text: path
                        color: Theme.onSurface
                        font.pixelSize: Theme.typeBodySmall
                        font.family: "monospace"
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                    Label {
                        text: isDir ? "" : (size / 1024).toFixed(1) + " KB"
                        color: Theme.onSurfaceVariant
                        font.pixelSize: Theme.typeLabelMedium
                    }
                    CheckBox { text: ""; checked: false; onCheckedChanged: selectedFlags[path] = checked }
                }
            }
            RowLayout {
                spacing: 12
                FlatButton {
                    text: qsTr("Extract selected…")
                    iconName: "unarchive"
                    accent: true
                    onClicked: extractDialog.open()
                }
                Item { Layout.fillWidth: true }
                Label {
                    text: qsTr("%1 entries").arg(entriesModel.count)
                    color: Theme.onSurfaceVariant
                    font.pixelSize: Theme.typeLabelMedium
                }
            }
        }

        // text preview page
        ColumnLayout {
            id: previewPage
            visible: false
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 12
            ScrollView {
                Layout.fillWidth: true
                Layout.fillHeight: true
                TextArea {
                    id: previewArea
                    readOnly: true
                    font.family: "monospace"
                    font.pixelSize: Theme.typeBodySmall
                    wrapMode: Text.NoWrap
                    color: Theme.onSurface
                }
            }
            FlatButton {
                text: qsTr("Back to list")
                iconName: "arrow_back"
                onClicked: { previewPage.visible = false; listPage.visible = true }
            }
        }
    }

    property var selectedFlags: ({})

    Dialog {
        id: extractDialog
        title: qsTr("Extract selected entries to…")
        modal: true
        parent: Overlay.overlay
        anchors.centerIn: parent
        standardButtons: Dialog.Ok | Dialog.Cancel
        TextField {
            id: dirField
            text: App.session(dlg.session.sessionId) ? "" : ""
            placeholderText: "/tmp/eclipse-extract or local folder"
            Layout.fillWidth: true
        }
        onAccepted: {
            const entries = [];
            for (const k in dlg.selectedFlags) if (dlg.selectedFlags[k]) entries.push(k);
            const err = App.extractArchive(dlg.session.sessionId, dlg.remotePath, entries, dirField.text || "/tmp");
            errorLabel.text = err; errorLabel.visible = err.length > 0;
        }
    }
}
