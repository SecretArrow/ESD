import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Eclipse

// Settings hub with searchable category pages.
Rectangle {
    color: Theme.surface

    function applyTheme() { ThemeBridge.applyFromSettings() }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 10

        TextField {
            id: searchBox
            placeholderText: qsTr("Search settings…")
            Layout.preferredWidth: 320
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            spacing: 14

            ListView {
                id: catList
                Layout.preferredWidth: 180
                Layout.fillHeight: true
                model: ListModel {
                    ListElement { name: "Appearance" }
                    ListElement { name: "Terminal" }
                    ListElement { name: "Connections" }
                    ListElement { name: "Transfers" }
                    ListElement { name: "Security" }
                    ListElement { name: "File Manager" }
                    ListElement { name: "Shortcuts" }
                    ListElement { name: "Advanced" }
                }
                delegate: ItemDelegate {
                    width: catList.width
                    text: name
                    highlighted: catList.currentIndex === index
                    onClicked: catList.currentIndex = index
                }
            }

            StackLayout {
                Layout.fillWidth: true
                Layout.fillHeight: true
                currentIndex: catList.currentIndex

                // ---- Appearance ----
                ScrollView {
                    ColumnLayout {
                        width: 520
                        spacing: 10
                        Label { text: qsTr("Theme"); font.weight: Font.DemiBold; color: Theme.text }
                        ComboBox {
                            id: themeBox
                            model: ["system", "light", "dark"]
                            currentIndex: ["system","light","dark"].indexOf(App.settings.themeMode) < 0 ? 0
                                          : ["system","light","dark"].indexOf(App.settings.themeMode)
                            onActivated: (i) => { App.settings.themeMode = model[i]; applyTheme() }
                        }
                        Label { text: qsTr("Accent color"); color: Theme.text }
                        RowLayout {
                            Repeater {
                                model: ["#5B8DEF", "#3fb66f", "#e2b12c", "#e25d5d", "#b46bd6", "#4fc2c5"]
                                Rectangle {
                                    width: 30; height: 30; radius: 15; color: modelData
                                    border.width: ThemeBridge.accent.toUpperCase() === modelData.toUpperCase() ? 3 : 1
                                    border.color: Theme.text
                                    MouseArea { anchors.fill: parent; onClicked: {
                                        App.settings.accentColor = modelData; applyTheme() } }
                                }
                            }
                        }
                        Label { text: qsTr("Density"); color: Theme.text }
                        ComboBox {
                            model: ["Compact", "Comfortable", "High density"]
                            onActivated: (i) => App.settings.uiDensity = [0.85, 1.0, 1.15][i]
                            Component.onCompleted: currentIndex =
                                [0.85, 1.0, 1.15].indexOf(App.settings.uiDensity) < 0 ? 1
                                : [0.85, 1.0, 1.15].indexOf(App.settings.uiDensity)
                        }
                        Label { text: qsTr("UI font size"); color: Theme.text }
                        SpinBox { from: 8; to: 20; value: App.settings.uiFontSize
                            onValueModified: App.settings.uiFontSize = value }
                    }
                }

                // ---- Terminal ----
                ScrollView {
                    ColumnLayout {
                        width: 520; spacing: 10
                        Label { text: qsTr("Terminal font"); font.weight: Font.DemiBold; color: Theme.text }
                        ComboBox {
                            editable: true
                            model: ["monospace", "DejaVu Sans Mono", "Liberation Mono", "Cascadia Mono", "JetBrains Mono"]
                            onActivated: (i) => { App.settings.terminalFontFamily = model[i]; applyTheme() }
                            Component.onCompleted: currentIndex =
                                model.indexOf(App.settings.terminalFontFamily) >= 0 ? model.indexOf(App.settings.terminalFontFamily) : 0
                        }
                        Label { text: qsTr("Font size"); color: Theme.text }
                        SpinBox { from: 8; to: 32; value: App.settings.terminalFontSize
                            onValueModified: App.settings.terminalFontSize = value }
                        Label { text: qsTr("Scrollback lines"); color: Theme.text }
                        SpinBox { from: 1000; to: 100000; stepSize: 1000
                            value: App.settings.terminalScrollback
                            onValueModified: App.settings.terminalScrollback = value }
                        Label { text: qsTr("Cursor"); color: Theme.text }
                        ComboBox {
                            model: ["block", "beam", "underline"]
                            onActivated: (i) => App.settings.terminalCursorStyle = model[i]
                            Component.onCompleted: currentIndex =
                                model.indexOf(App.settings.terminalCursorStyle) < 0 ? 0
                                : model.indexOf(App.settings.terminalCursorStyle)
                        }
                        Switch { text: qsTr("Cursor blinking"); checked: App.settings.terminalCursorBlink
                            onToggled: App.settings.terminalCursorBlink = checked }
                    }
                }

                // ---- Connections ----
                ScrollView {
                    ColumnLayout {
                        width: 520; spacing: 10
                        Switch { text: qsTr("Auto reconnect"); checked: App.settings.autoReconnect
                            onToggled: App.settings.autoReconnect = checked }
                        Label { text: qsTr("Reconnect retries"); color: Theme.text }
                        SpinBox { from: 1; to: 20; value: App.settings.reconnectRetries
                            onValueModified: App.settings.reconnectRetries = value }
                        Label { text: qsTr("Keepalive (seconds, 0 = off)"); color: Theme.text }
                        SpinBox { from: 0; to: 300; value: 15 }
                        Label { text: qsTr("Default SSH engine"); color: Theme.text }
                        ComboBox {
                            model: ["auto (libssh)", "libssh", "libssh2"]
                            onActivated: (i) => App.settings.defaultEngine = ["auto","libssh","libssh2"][i]
                        }
                    }
                }

                // ---- Transfers ----
                ScrollView {
                    ColumnLayout {
                        width: 520; spacing: 10
                        Label { text: qsTr("Max parallel transfers"); color: Theme.text }
                        SpinBox { from: 1; to: 8; value: App.settings.maxParallelTransfers
                            onValueModified: App.settings.maxParallelTransfers = value }
                    }
                }

                // ---- Security ----
                ScrollView {
                    ColumnLayout {
                        width: 560; spacing: 10
                        Label { text: qsTr("Credential storage"); font.weight: Font.DemiBold; color: Theme.text }
                        Label { text: qsTr("Current: %1").arg(App.credentials.storageDescription())
                            color: Theme.textMuted; font.pixelSize: 11; wrapMode: Text.Wrap; Layout.maximumWidth: 480 }
                        ComboBox {
                            id: credBox
                            model: ["os (secure storage)", "session (memory only)", "ask every time"]
                            onActivated: (i) => App.credentials.mode =
                                (i === 1 ? 1 : i === 2 ? 2 : 0)
                        }
                        Label { text: qsTr("Passwords and passphrases are never stored in plaintext.\nHost key verification cannot be disabled.")
                            color: Theme.textMuted; font.pixelSize: 11 }
                    }
                }

                // ---- File Manager ----
                ScrollView {
                    ColumnLayout {
                        width: 520; spacing: 10
                        Switch { text: qsTr("Confirm before delete"); checked: App.settings.confirmDelete
                            onToggled: App.settings.confirmDelete = checked }
                        Switch { text: qsTr("Confirm overwrite"); checked: App.settings.confirmOverwrite
                            onToggled: App.settings.confirmOverwrite = checked }
                        Switch { text: qsTr("Show hidden files"); checked: App.settings.showHiddenFiles
                            onToggled: App.settings.showHiddenFiles = checked }
                    }
                }

                // ---- Shortcuts ----
                ScrollView {
                    ColumnLayout {
                        width: 560; spacing: 4
                        Repeater {
                            model: Shortcuts.allActions()
                            delegate: RowLayout {
                                Layout.fillWidth: true
                                Label { text: title; color: Theme.text; Layout.preferredWidth: 240 }
                                Label { text: category; color: Theme.textMuted; Layout.preferredWidth: 100 }
                                Label { text: Shortcuts.sequenceFor(id_) !== "" ? Shortcuts.sequenceFor(id_).toString() : "—"
                                    color: Theme.accent; font.family: "monospace" }
                            }
                        }
                        Label { text: qsTr("Shortcut editing: coming in a point release — overrides file: settings.ini [shortcuts]")
                            color: Theme.textMuted; font.pixelSize: 11 }
                    }
                }

                // ---- Advanced ----
                ScrollView {
                    ColumnLayout {
                        width: 560; spacing: 10
                        Switch { text: qsTr("Developer mode (extra diagnostics)")
                            checked: App.settings.debugMode; onToggled: App.settings.debugMode = checked }
                        ComboBox {
                            model: ["stable", "beta", "nightly"]
                            onActivated: (i) => App.settings.updateChannel = model[i]
                            Component.onCompleted: currentIndex =
                                model.indexOf(App.settings.updateChannel) < 0 ? 0 : model.indexOf(App.settings.updateChannel)
                        }
                        Label { text: qsTr("Update channel"); color: Theme.text }
                        Repeater {
                            model: Object.keys(App.developerInfo())
                            delegate: Label {
                                text: modelData + ": " + App.developerInfo()[modelData]
                                color: Theme.textMuted; font.pixelSize: 11; font.family: "monospace"
                                Layout.fillWidth: true; wrapMode: Text.WrapAnywhere
                            }
                        }
                    }
                }
            }
        }
    }
}
