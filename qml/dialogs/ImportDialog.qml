import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

// Module import: gives access to the Theme singleton (same-module types in
// subdirectories are not visible without it - binding errors otherwise).
import Eclipse

// Import / export center: PuTTY sessions, OpenSSH known_hosts and encrypted
// profile bundles. Calls AppController invokables (exposed to QML as the
// "App" context property, like every other dialog in qml/dialogs):
//   App.importPuttySessions()                              -> {ok, imported, skipped, error}
//   App.importOpenSshKnownHosts(filePath)                  -> {ok, imported, skipped, error}
//   App.exportProfileBundle(filePath, passphrase)          -> {ok, error}
//   App.importProfileBundle(filePath, passphrase)          -> {ok, imported, skipped, error}
Dialog {
    id: dlg
    title: qsTr("Import / Export")
    modal: true
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 580
    standardButtons: Dialog.Close

    // 0 = PuTTY sessions, 1 = OpenSSH known_hosts, 2 = profile bundle
    property int source: 0
    readonly property bool isBundle: source === 2
    property string resultTitle: ""
    property string resultText: ""
    property bool resultIsError: false

    function openDialog() {
        resultTitle = ""; resultText = ""; resultIsError = false;
        open()
    }

    // file:// URL -> local path (handles the Windows "/C:/..." form and %20;
    // same convention as the import/export FileDialog in Main.qml).
    function localPath(url) {
        const s = url.toString();
        if (s.indexOf("file://") !== 0) return s;
        let p = s.substring(7);
        if (/^\/[A-Za-z]:\//.test(p)) p = p.substring(1);
        try { return decodeURIComponent(p); } catch (e) { return p; }
    }

    function showResult(label, res) {
        resultTitle = label;
        if (res && res.ok) {
            let txt = qsTr("Done");
            if (res.imported !== undefined)
                txt = qsTr("Imported %1").arg(res.imported);
            if (res.skipped !== undefined && res.skipped > 0)
                txt += qsTr(", skipped %1").arg(res.skipped);
            if (res.error && res.error.length > 0)
                txt += "\n" + res.error;
            resultText = txt;
            resultIsError = false;
        } else {
            resultText = (res && res.error && res.error.length > 0) ? res.error : qsTr("Operation failed");
            resultIsError = true;
        }
    }

    function runImport() {
        if (source === 0) {
            showResult(qsTr("PuTTY import"), App.importPuttySessions());
        } else if (source === 1) {
            if (pathField.text.length === 0) {
                showResult(qsTr("known_hosts import"), { ok: false, error: qsTr("Choose a known_hosts file first.") });
                return;
            }
            showResult(qsTr("known_hosts import"), App.importOpenSshKnownHosts(pathField.text));
        } else {
            if (pathField.text.length === 0) {
                showResult(qsTr("Bundle import"), { ok: false, error: qsTr("Choose a bundle file first.") });
                return;
            }
            showResult(qsTr("Bundle import"), App.importProfileBundle(pathField.text, passField.text));
        }
    }

    function runExport() {
        if (pathField.text.length === 0) {
            showResult(qsTr("Bundle export"), { ok: false, error: qsTr("Choose a destination file first.") });
            return;
        }
        showResult(qsTr("Bundle export"), App.exportProfileBundle(pathField.text, passField.text));
    }

    contentItem: ColumnLayout {
        spacing: 14

        ColumnLayout {
            spacing: 6
            RadioButton { text: qsTr("PuTTY sessions"); checked: dlg.source === 0
                onToggled: if (checked) dlg.source = 0 }
            RadioButton { text: qsTr("OpenSSH known_hosts (host keys)"); checked: dlg.source === 1
                onToggled: if (checked) dlg.source = 1 }
            RadioButton { text: qsTr("Profile bundle (device sync)"); checked: dlg.source === 2
                onToggled: if (checked) dlg.source = 2 }
        }

        Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            font.pixelSize: Theme.typeBodySmall
            color: Theme.onSurfaceVariant
            visible: dlg.source === 0
            text: qsTr("Reads saved sessions from the Windows registry (HKCU\\Software\\SimonTatham\\PuTTY\\Sessions). Only SSH sessions are imported; telnet/serial sessions are counted as skipped. On other platforms export a .reg file on Windows first and hand it to the PuTTY .reg import.")
        }

        RowLayout {
            visible: dlg.source !== 0
            Layout.fillWidth: true
            spacing: 10
            RowLayout {
                spacing: 6
                MaterialIcon {
                    icon: dlg.isBundle ? "description" : "key"
                    iconSize: 18
                    color: Theme.onSurfaceVariant
                }
                Label { text: dlg.isBundle ? qsTr("File") : qsTr("known_hosts"); color: Theme.onSurface; font.pixelSize: Theme.typeBodyMedium }
            }
            TextField {
                id: pathField
                Layout.fillWidth: true
                placeholderText: dlg.isBundle ? qsTr("bundle.json / bundle.epb")
                                              : qsTr("/path/to/known_hosts")
            }
            IconToolButton {
                iconName: "folder_open"
                iconSize: 18
                toolTip: qsTr("Browse…")
                onClicked: openFileDlg.open()
            }
        }

        RowLayout {
            visible: dlg.isBundle
            Layout.fillWidth: true
            spacing: 10
            RowLayout {
                spacing: 6
                MaterialIcon { icon: "password"; iconSize: 18; color: Theme.onSurfaceVariant }
                Label { text: qsTr("Passphrase"); color: Theme.onSurface; font.pixelSize: Theme.typeBodyMedium }
            }
            TextField {
                id: passField
                Layout.fillWidth: true
                echoMode: TextInput.Password
                placeholderText: qsTr("empty = write / read plaintext bundle")
            }
        }

        RowLayout {
            Layout.fillWidth: true
            spacing: 10
            Item { Layout.fillWidth: true }
            FlatButton {
                visible: dlg.isBundle
                text: qsTr("Export bundle…")
                iconName: "file_upload"
                variant: "outlined"
                onClicked: saveFileDlg.open()
            }
            FlatButton {
                accent: true
                iconName: "file_download"
                text: dlg.source === 0 ? qsTr("Import PuTTY sessions")
                    : dlg.source === 1 ? qsTr("Import host keys…")
                    : qsTr("Import bundle…")
                onClicked: {
                    if (dlg.source === 0) { dlg.runImport(); return; }
                    if (pathField.text.length > 0) { dlg.runImport(); return; }
                    openFileDlg.open();
                }
            }
        }

        // Results area (counts / errors after a run) — MD3 tonal status container
        Rectangle {
            Layout.fillWidth: true
            visible: dlg.resultText.length > 0
            radius: Theme.radiusM
            color: dlg.resultIsError ? Theme.errorContainer : Theme.successContainer
            implicitHeight: resultsCol.implicitHeight + 24
            ColumnLayout {
                id: resultsCol
                anchors.left: parent.left
                anchors.right: parent.right
                anchors.top: parent.top
                anchors.margins: 12
                spacing: 4
                RowLayout {
                    spacing: 6
                    visible: dlg.resultTitle.length > 0
                    MaterialIcon {
                        icon: dlg.resultIsError ? "error" : "check_circle"
                        iconSize: 16
                        color: dlg.resultIsError ? Theme.onErrorContainer : Theme.onSuccessContainer
                    }
                    Label {
                        text: dlg.resultTitle
                        font.weight: Font.Medium
                        font.pixelSize: Theme.typeTitleSmall
                        color: dlg.resultIsError ? Theme.onErrorContainer : Theme.onSuccessContainer
                    }
                }
                Label {
                    text: dlg.resultText
                    wrapMode: Text.WrapAnywhere
                    Layout.fillWidth: true
                    color: dlg.resultIsError ? Theme.onErrorContainer : Theme.onSuccessContainer
                    font.pixelSize: Theme.typeBodySmall
                }
            }
        }
    }

    FileDialog {
        id: openFileDlg
        fileMode: FileDialog.OpenFile
        nameFilters: dlg.source === 1 ? ["known_hosts (*)", "All files (*)"]
                   : dlg.source === 2 ? ["Profile bundle (*.json *.epb)", "All files (*)"]
                   : ["PuTTY registry export (*.reg)", "All files (*)"]
        onAccepted: {
            pathField.text = dlg.localPath(selectedFile);
            if (dlg.source !== 0)
                dlg.runImport();
        }
    }

    FileDialog {
        id: saveFileDlg
        fileMode: FileDialog.SaveFile
        nameFilters: ["Profile bundle (*.json)", "Encrypted bundle (*.epb)", "All files (*)"]
        onAccepted: {
            pathField.text = dlg.localPath(selectedFile);
            dlg.runExport();
        }
    }
}
