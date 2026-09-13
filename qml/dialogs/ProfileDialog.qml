import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import QtQuick.Dialogs

// Module import: gives access to the Theme singleton (same-module types in
// subdirectories are not visible without it - binding errors otherwise).
import Eclipse

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

    // "ssh" | "telnet" | "serial" (progressive disclosure of the sections below)
    property string connType: draft ? draft.connectionType : "ssh"

    contentItem: ColumnLayout {
        spacing: 12

        GridLayout {
            columns: 2
            columnSpacing: 12
            rowSpacing: 10

            Label { text: qsTr("Connection type"); color: Theme.onSurface; font.pixelSize: Theme.typeBodyMedium }
            ComboBox {
                id: typeBox
                model: ["ssh", "telnet", "serial"]
                currentIndex: Math.max(0, model.indexOf(dlg.connType))
                onActivated: (i) => { if (dlg.draft) dlg.draft.connectionType = model[i] }
            }

            Label { text: qsTr("Name"); color: Theme.onSurface; font.pixelSize: Theme.typeBodyMedium }
            TextField { text: dlg.draft ? dlg.draft.name : ""; onTextChanged: { if (dlg.draft) dlg.draft.name = text } Layout.fillWidth: true }
        }

        // ---- SSH basic section ----
        GridLayout {
            columns: 2
            columnSpacing: 12
            rowSpacing: 10
            visible: dlg.connType === "ssh"

            Label { text: qsTr("Host"); color: Theme.onSurface; font.pixelSize: Theme.typeBodyMedium }
            TextField { text: dlg.draft ? dlg.draft.host : ""; onTextChanged: { if (dlg.draft) dlg.draft.host = text } Layout.fillWidth: true }
            Label { text: qsTr("Port"); color: Theme.onSurface; font.pixelSize: Theme.typeBodyMedium }
            TextField { text: dlg.draft ? String(dlg.draft.port) : "22"; onTextChanged: { if (dlg.draft) dlg.draft.port = parseInt(text || "22") } }
            Label { text: qsTr("Username"); color: Theme.onSurface; font.pixelSize: Theme.typeBodyMedium }
            TextField { text: dlg.draft ? dlg.draft.username : ""; onTextChanged: { if (dlg.draft) dlg.draft.username = text } Layout.fillWidth: true }
            Label { text: qsTr("Authentication"); color: Theme.onSurface; font.pixelSize: Theme.typeBodyMedium }
            ComboBox {
                model: ["password", "publickey", "agent", "keyboard-interactive"]
                currentIndex: dlg.draft ? model.indexOf(dlg.draft.authMethod) : 0
                onActivated: (i) => { if (dlg.draft) dlg.draft.authMethod = model[i] }
            }
            Label { text: qsTr("Password"); color: Theme.onSurface; font.pixelSize: Theme.typeBodyMedium; visible: authBoxIdx() === 0 || authBoxIdx() === 3 }
            TextField { id: passField; echoMode: TextInput.Password; Layout.fillWidth: true
                        visible: authBoxIdx() === 0 || authBoxIdx() === 3
                        onTextChanged: dlg.password = text }
            Label { text: qsTr("Private key"); color: Theme.onSurface; font.pixelSize: Theme.typeBodyMedium; visible: authBoxIdx() === 1 }
            RowLayout { visible: authBoxIdx() === 1; Layout.fillWidth: true
                TextField { id: keyField; text: dlg.draft ? dlg.draft.privateKeyPath : ""; onTextChanged: { if (dlg.draft) dlg.draft.privateKeyPath = text } Layout.fillWidth: true }
                IconToolButton { icon: "folder_open"; iconSize: 18; toolTip: qsTr("Browse…"); onClicked: keyDialog.open() }
            }
            Label { text: qsTr("Passphrase"); color: Theme.onSurface; font.pixelSize: Theme.typeBodyMedium; visible: authBoxIdx() === 1 }
            TextField { echoMode: TextInput.Password; Layout.fillWidth: true; visible: authBoxIdx() === 1
                        onTextChanged: dlg.passphrase = text }
        }
        function authBoxIdx() {
            return dlg.draft ? ["password","publickey","agent","keyboard-interactive"].indexOf(dlg.draft.authMethod) : 0
        }

        // ---- Telnet basic section ----
        GridLayout {
            columns: 2
            columnSpacing: 12
            rowSpacing: 10
            visible: dlg.connType === "telnet"

            Label { text: qsTr("Host"); color: Theme.onSurface; font.pixelSize: Theme.typeBodyMedium }
            TextField { text: dlg.draft ? dlg.draft.host : ""; placeholderText: qsTr("host name or IP")
                onTextChanged: { if (dlg.draft) dlg.draft.host = text } Layout.fillWidth: true }
            Label { text: qsTr("Port"); color: Theme.onSurface; font.pixelSize: Theme.typeBodyMedium }
            TextField { text: dlg.draft && dlg.draft.port > 0 ? String(dlg.draft.port) : "23"
                onTextChanged: { if (dlg.draft) dlg.draft.port = parseInt(text || "23") } }
        }

        // ---- Serial basic section ----
        GridLayout {
            columns: 2
            columnSpacing: 12
            rowSpacing: 10
            visible: dlg.connType === "serial"

            Label { text: qsTr("Serial device"); color: Theme.onSurface; font.pixelSize: Theme.typeBodyMedium }
            TextField { text: dlg.draft ? dlg.draft.serialPort : ""; placeholderText: qsTr("COM3 or /dev/ttyUSB0")
                onTextChanged: { if (dlg.draft) dlg.draft.serialPort = text } Layout.fillWidth: true }
            Label { text: qsTr("Baud"); color: Theme.onSurface; font.pixelSize: Theme.typeBodyMedium }
            ComboBox {
                editable: true
                model: [1200, 2400, 4800, 9600, 19200, 38400, 57600, 115200]
                currentIndex: 7
                onActivated: (i) => { if (dlg.draft) dlg.draft.serialBaud = parseInt(model[i]) }
                Component.onCompleted: {
                    if (dlg.draft && dlg.draft.serialBaud > 0) {
                        const idx = model.indexOf(dlg.draft.serialBaud);
                        if (idx >= 0) currentIndex = idx; else editText = String(dlg.draft.serialBaud);
                    }
                }
                onEditTextChanged: { const v = parseInt(editText); if (dlg.draft && v > 0) dlg.draft.serialBaud = v }
            }
            Label { text: qsTr("Data bits"); color: Theme.onSurface; font.pixelSize: Theme.typeBodyMedium }
            ComboBox {
                model: [7, 8]
                currentIndex: dlg.draft && dlg.draft.serialDataBits === 7 ? 0 : 1
                onActivated: (i) => { if (dlg.draft) dlg.draft.serialDataBits = model[i] }
            }
            Label { text: qsTr("Parity"); color: Theme.onSurface; font.pixelSize: Theme.typeBodyMedium }
            ComboBox {
                model: ["none", "even", "odd"]
                currentIndex: dlg.draft ? Math.max(0, model.indexOf(dlg.draft.serialParity)) : 0
                onActivated: (i) => { if (dlg.draft) dlg.draft.serialParity = model[i] }
            }
            Label { text: qsTr("Stop bits"); color: Theme.onSurface; font.pixelSize: Theme.typeBodyMedium }
            ComboBox {
                model: [1, 2]
                currentIndex: dlg.draft && dlg.draft.serialStopBits === 2 ? 1 : 0
                onActivated: (i) => { if (dlg.draft) dlg.draft.serialStopBits = model[i] }
            }
            Label { text: qsTr("Flow control"); color: Theme.onSurface; font.pixelSize: Theme.typeBodyMedium }
            ComboBox {
                model: ["none", "rtscts", "xonxoff"]
                currentIndex: dlg.draft ? Math.max(0, model.indexOf(["none","rtscts","xonxoff"][dlg.draft.serialFlowControl])) : 0
                onActivated: (i) => { if (dlg.draft) dlg.draft.serialFlowControl = i }
            }
        }

        CheckBox {
            id: rememberBox
            text: qsTr("Remember credentials (OS secure storage)")
            checked: true
            visible: dlg.connType === "ssh"
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
            columnSpacing: 12
            rowSpacing: 10
            Layout.topMargin: 6

            // SSH-only advanced options
            Label { text: qsTr("Engine"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.typeLabelMedium; visible: dlg.connType === "ssh" }
            ComboBox {
                visible: dlg.connType === "ssh"
                model: ["auto", "libssh", "libssh2"]
                currentIndex: dlg.draft ? Math.max(0, model.indexOf(dlg.draft.engine)) : 0
                onActivated: (i) => { if (dlg.draft) dlg.draft.engine = model[i] }
            }
            Label { text: qsTr("KEX algorithms"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.typeLabelMedium; visible: dlg.connType === "ssh" }
            TextField {
                visible: dlg.connType === "ssh"
                text: dlg.draft ? dlg.draft.kexAlgorithms : ""
                placeholderText: qsTr("e.g. mlkem768x25519-sha256,sntrup761x25519@openssh.com,curve25519-sha256")
                onTextChanged: { if (dlg.draft) dlg.draft.kexAlgorithms = text } Layout.fillWidth: true
            }
            Label { text: qsTr("X11 forwarding"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.typeLabelMedium; visible: dlg.connType === "ssh" }
            RowLayout {
                visible: dlg.connType === "ssh"
                CheckBox { text: qsTr("enable"); checked: dlg.draft ? dlg.draft.x11Forward : false
                    onCheckedChanged: if (dlg.draft) dlg.draft.x11Forward = checked }
                Label { text: qsTr("Screen"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.typeLabelMedium }
                SpinBox { from: 0; to: 16; value: dlg.draft ? dlg.draft.x11Screen : 0
                    onValueModified: if (dlg.draft) dlg.draft.x11Screen = value }
            }

            Label { text: qsTr("Group / Tags"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.typeLabelMedium }
            RowLayout {
                TextField { text: dlg.draft ? dlg.draft.group : ""; placeholderText: qsTr("group")
                    onTextChanged: { if (dlg.draft) dlg.draft.group = text } Layout.fillWidth: true }
                TextField { text: dlg.draft ? dlg.draft.tags : ""; placeholderText: qsTr("tags, comma-sep")
                    onTextChanged: { if (dlg.draft) dlg.draft.tags = text } Layout.fillWidth: true }
            }
            Label { text: qsTr("Jump hosts"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.typeLabelMedium; visible: dlg.connType === "ssh" }
            TextField { visible: dlg.connType === "ssh"; text: dlg.draft ? dlg.draft.jumpHosts : ""; placeholderText: qsTr("bastion1 -> bastion2 (names or user@host:port)")
                onTextChanged: { if (dlg.draft) dlg.draft.jumpHosts = text } Layout.fillWidth: true }
            Label { text: qsTr("Proxy"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.typeLabelMedium; visible: dlg.connType === "ssh" }
            RowLayout {
                visible: dlg.connType === "ssh"
                ComboBox { model: ["none", "socks5", "socks4", "http"]
                    currentIndex: dlg.draft ? Math.max(0, model.indexOf(dlg.draft.proxyType)) : 0
                    onActivated: (i) => { if (dlg.draft) dlg.draft.proxyType = model[i] }
                }
                TextField { placeholderText: qsTr("host"); text: dlg.draft ? dlg.draft.proxyHost : ""
                    onTextChanged: { if (dlg.draft) dlg.draft.proxyHost = text } Layout.preferredWidth: 110 }
                TextField { placeholderText: qsTr("port"); text: dlg.draft ? String(dlg.draft.proxyPort) : ""
                    onTextChanged: { if (dlg.draft) dlg.draft.proxyPort = parseInt(text || "0") } Layout.preferredWidth: 60 }
                TextField { placeholderText: qsTr("user"); text: dlg.draft ? dlg.draft.proxyUser : ""
                    onTextChanged: { if (dlg.draft) dlg.draft.proxyUser = text } Layout.preferredWidth: 90 }
            }
            Label { text: qsTr("Keepalive (s)"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.typeLabelMedium; visible: dlg.connType === "ssh" }
            TextField { visible: dlg.connType === "ssh"; text: dlg.draft ? String(dlg.draft.keepAliveSeconds) : "15"
                onTextChanged: { if (dlg.draft) dlg.draft.keepAliveSeconds = parseInt(text || "15") } }
            Label { text: qsTr("Timeout (ms)"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.typeLabelMedium }
            TextField { text: dlg.draft ? String(dlg.draft.connectTimeoutMs) : "15000"
                onTextChanged: { if (dlg.draft) dlg.draft.connectTimeoutMs = parseInt(text || "15000") } }
            Label { text: qsTr("Compression"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.typeLabelMedium; visible: dlg.connType === "ssh" }
            CheckBox { visible: dlg.connType === "ssh"; text: qsTr("enable"); checked: dlg.draft ? dlg.draft.compression : false
                onCheckedChanged: if (dlg.draft) dlg.draft.compression = checked }
            Label { text: qsTr("Startup commands"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.typeLabelMedium }
            TextField { text: dlg.draft ? dlg.draft.startupCommands : ""
                onTextChanged: { if (dlg.draft) dlg.draft.startupCommands = text } Layout.fillWidth: true }
            Label { text: qsTr("Environment (KEY=value)"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.typeLabelMedium; visible: dlg.connType === "ssh" }
            TextField { visible: dlg.connType === "ssh"; text: dlg.draft ? dlg.draft.environment : ""
                onTextChanged: { if (dlg.draft) dlg.draft.environment = text } Layout.fillWidth: true }
            Label { text: qsTr("Default remote dir"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.typeLabelMedium; visible: dlg.connType === "ssh" }
            TextField { visible: dlg.connType === "ssh"; text: dlg.draft ? dlg.draft.sftpDefaultRemoteDir : ""
                onTextChanged: { if (dlg.draft) dlg.draft.sftpDefaultRemoteDir = text } Layout.fillWidth: true }
            Label { text: qsTr("Transfer protocol"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.typeLabelMedium; visible: dlg.connType === "ssh" }
            ComboBox { visible: dlg.connType === "ssh"; model: ["sftp", "scp"]
                currentIndex: dlg.draft ? Math.max(0, model.indexOf(dlg.draft.transferProtocol)) : 0
                onActivated: (i) => { if (dlg.draft) dlg.draft.transferProtocol = model[i] }
            }
            Label { text: qsTr("Auto reconnect"); color: Theme.onSurfaceVariant; font.pixelSize: Theme.typeLabelMedium; visible: dlg.connType === "ssh" }
            CheckBox { visible: dlg.connType === "ssh"; text: qsTr("enabled"); checked: dlg.draft ? dlg.draft.autoReconnect : true
                onCheckedChanged: { if (dlg.draft) dlg.draft.autoReconnect = checked } }
        }
    }

    FileDialog {
        id: keyDialog
        nameFilters: ["All files (*)"]
        onAccepted: keyField.text = selectedFile.toString().replace("file://", "")
    }
}
