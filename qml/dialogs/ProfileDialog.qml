import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

// Connection editor with progressive disclosure (Basic / Advanced).
Dialog {
    id: dlg
    property var draft: null
    property string password: ""
    property string passphrase: ""
    property bool rememberSecrets: true
    title: draft && draft.id > 0 ? qsTr("Edit Connection") : qsTr("New Connection")
    modal: true
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 620

    standardButtons: Dialog.Save | Dialog.Cancel

    function openNew() { draft = App.draftProfile(0); advanced.checked = false; open() }
    function openEdit(id) { draft = App.draftProfile(id); advanced.checked = true; open() }
    onAccepted: App.saveProfile(draft, password, passphrase, rememberSecrets)

    contentItem: ColumnLayout {
        spacing: 8
        GridLayout {
            columns: 2
            columnSpacing: 10
            Label { text: qsTr("Name"); color: Theme.text }
            TextField { text: dlg.draft ? dlg.draft.name : ""; onTextChanged: { if (dlg.draft) dlg.draft.name = text } Layout.fillWidth: true }
            Label { text: qsTr("Host"); color: Theme.text }
            TextField { text: dlg.draft ? dlg.draft.host : ""; onTextChanged: { if (dlg.draft) dlg.draft.host = text } Layout.fillWidth: true }
            Label { text: qsTr("Port"); color: Theme.text }
            TextField { text: dlg.draft ? String(dlg.draft.port) : "22"; onTextChanged: { if (dlg.draft) dlg.draft.port = parseInt(text || "22") } }
            Label { text: qsTr("Username"); color: Theme.text }
            TextField { text: dlg.draft ? dlg.draft.username : ""; onTextChanged: { if (dlg.draft) dlg.draft.username = text } Layout.fillWidth: true }
            Label { text: qsTr("Authentication"); color: Theme.text }
            ComboBox {
                model: ["password", "publickey", "agent", "keyboard-interactive"]
                currentIndex: dlg.draft ? model.indexOf(dlg.draft.authMethod) : 0
                onActivated: (i) => { if (dlg.draft) dlg.draft.authMethod = model[i] }
            }
            Label { text: qsTr("Password"); color: Theme.text; visible: authBoxIdx() === 0 || authBoxIdx() === 3 }
            TextField { id: passField; echoMode: TextInput.Password; Layout.fillWidth: true
                        visible: authBoxIdx() === 0 || authBoxIdx() === 3
                        onTextChanged: dlg.password = text }
            Label { text: qsTr("Private key"); color: Theme.text; visible: authBoxIdx() === 1 }
            RowLayout { visible: authBoxIdx() === 1; Layout.fillWidth: true
                TextField { id: keyField; text: dlg.draft ? dlg.draft.privateKeyPath : ""; onTextChanged: { if (dlg.draft) dlg.draft.privateKeyPath = text } Layout.fillWidth: true }
                Button { text: "…"; flat: true; onClicked: keyDialog.open() }
            }
            Label { text: qsTr("Passphrase"); color: Theme.text; visible: authBoxIdx() === 1 }
            TextField { echoMode: TextInput.Password; Layout.fillWidth: true; visible: authBoxIdx() === 1
                        onTextChanged: dlg.passphrase = text }
        }
        function authBoxIdx() {
            return dlg.draft ? ["password","publickey","agent","keyboard-interactive"].indexOf(dlg.draft.authMethod) : 0
        }

        CheckBox {
            id: rememberBox
            text: qsTr("Remember credentials (OS secure storage)")
            checked: true
            onCheckedChanged: dlg.rememberSecrets = checked
        }
        CheckBox {
            id: advanced
            text: qsTr("Show advanced options")
        }

        // ---- Advanced (progressive disclosure) ----
        GridLayout {
            visible: advanced.checked
            columns: 2
            columnSpacing: 10
            Layout.topMargin: 6

            Label { text: qsTr("Group / Tags"); color: Theme.textMuted; font.pixelSize: 11 }
            RowLayout {
                TextField { text: dlg.draft ? dlg.draft.group : ""; placeholderText: qsTr("group")
                    onTextChanged: { if (dlg.draft) dlg.draft.group = text } Layout.fillWidth: true }
                TextField { text: dlg.draft ? dlg.draft.tags : ""; placeholderText: qsTr("tags, comma-sep")
                    onTextChanged: { if (dlg.draft) dlg.draft.tags = text } Layout.fillWidth: true }
            }
            Label { text: qsTr("Engine"); color: Theme.textMuted; font.pixelSize: 11 }
            ComboBox {
                model: ["auto", "libssh", "libssh2"]
                currentIndex: dlg.draft ? Math.max(0, model.indexOf(dlg.draft.engine)) : 0
                onActivated: (i) => { if (dlg.draft) dlg.draft.engine = model[i] }
            }
            Label { text: qsTr("Jump hosts"); color: Theme.textMuted; font.pixelSize: 11 }
            TextField { text: dlg.draft ? dlg.draft.jumpHosts : ""; placeholderText: qsTr("bastion1 -> bastion2 (names or user@host:port)")
                onTextChanged: { if (dlg.draft) dlg.draft.jumpHosts = text } Layout.fillWidth: true }
            Label { text: qsTr("Proxy"); color: Theme.textMuted; font.pixelSize: 11 }
            RowLayout {
                ComboBox { model: ["none", "socks5", "socks4", "http"]
                    currentIndex: dlg.draft ? Math.max(0, model.indexOf(dlg.draft.proxyType)) : 0
                    onActivated: (i) => { if (dlg.draft) dlg.draft.proxyType = model[i] }
                TextField { placeholderText: qsTr("host"); text: dlg.draft ? dlg.draft.proxyHost : ""
                    onTextChanged: { if (dlg.draft) dlg.draft.proxyHost = text } Layout.preferredWidth: 110 }
                TextField { placeholderText: qsTr("port"); text: dlg.draft ? String(dlg.draft.proxyPort) : ""
                    onTextChanged: { if (dlg.draft) dlg.draft.proxyPort = parseInt(text || "0") } Layout.preferredWidth: 60 }
                TextField { placeholderText: qsTr("user"); text: dlg.draft ? dlg.draft.proxyUser : ""
                    onTextChanged: { if (dlg.draft) dlg.draft.proxyUser = text } Layout.preferredWidth: 90 }
            }
            Label { text: qsTr("Keepalive (s)"); color: Theme.textMuted; font.pixelSize: 11 }
            TextField { text: dlg.draft ? String(dlg.draft.keepAliveSeconds) : "15"
                onTextChanged: { if (dlg.draft) dlg.draft.keepAliveSeconds = parseInt(text || "15") } }
            Label { text: qsTr("Timeout (ms)"); color: Theme.textMuted; font.pixelSize: 11 }
            TextField { text: dlg.draft ? String(dlg.draft.connectTimeoutMs) : "15000"
                onTextChanged: { if (dlg.draft) dlg.draft.connectTimeoutMs = parseInt(text || "15000") } }
            Label { text: qsTr("Compression"); color: Theme.textMuted; font.pixelSize: 11 }
            CheckBox { text: qsTr("enable"); checked: dlg.draft ? dlg.draft.compression : false
                onCheckedChanged: if (dlg.draft) dlg.draft.compression = checked }
            Label { text: qsTr("Startup commands"); color: Theme.textMuted; font.pixelSize: 11 }
            TextField { text: dlg.draft ? dlg.draft.startupCommands : ""
                onTextChanged: { if (dlg.draft) dlg.draft.startupCommands = text } Layout.fillWidth: true }
            Label { text: qsTr("Environment (KEY=value)"); color: Theme.textMuted; font.pixelSize: 11 }
            TextField { text: dlg.draft ? dlg.draft.environment : ""
                onTextChanged: { if (dlg.draft) dlg.draft.environment = text } Layout.fillWidth: true }
            Label { text: qsTr("Default remote dir"); color: Theme.textMuted; font.pixelSize: 11 }
            TextField { text: dlg.draft ? dlg.draft.sftpDefaultRemoteDir : ""
                onTextChanged: { if (dlg.draft) dlg.draft.sftpDefaultRemoteDir = text } Layout.fillWidth: true }
            Label { text: qsTr("Transfer protocol"); color: Theme.textMuted; font.pixelSize: 11 }
            ComboBox { model: ["sftp", "scp"]
                currentIndex: dlg.draft ? Math.max(0, model.indexOf(dlg.draft.transferProtocol)) : 0
                onActivated: (i) => { if (dlg.draft) dlg.draft.transferProtocol = model[i] }
            }
            Label { text: qsTr("Auto reconnect"); color: Theme.textMuted; font.pixelSize: 11 }
            CheckBox { text: qsTr("enabled"); checked: dlg.draft ? dlg.draft.autoReconnect : true
                onCheckedChanged: { if (dlg.draft) dlg.draft.autoReconnect = checked } }
            }
        }
    }

    FileDialog {
        id: keyDialog
        nameFilters: ["All files (*)"]
        onAccepted: keyField.text = selectedFile.toString().replace("file://", "")
    }
}
