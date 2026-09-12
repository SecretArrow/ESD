import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Eclipse

// Live log viewer with filters.
Rectangle {
    color: Theme.surface

    // root-level so Component.onCompleted below can reference it
    function refreshFilter() {
        Logs.setFilter(filterField.text, levelBox.currentIndex, categoryBox.currentText);
    }

    // root-level so Component.onCompleted can reference it
    function refreshFilter() {
        Logs.setFilter(filterField.text, levelBox.currentIndex, categoryBox.currentText);
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            TextField { id: filterField; placeholderText: qsTr("Filter…"); Layout.preferredWidth: 260 }
            ComboBox {
                id: levelBox
                model: ["All levels", "INFO+", "WARN+", "ERROR"]
            }
            ComboBox {
                id: categoryBox
                model: ["All", "Connection", "SSH", "SFTP", "Transfer", "Tunnel", "Authentication", "Application", "Error", "Debug"]
            }
            Button { text: qsTr("Clear"); onClicked: Logs.clear() }
            Button { text: qsTr("Copy"); onClicked: App.copyToClipboard(exportText()) }
            Item { Layout.fillWidth: true }
            Switch {
                text: qsTr("Debug mode")
                checked: App.settings.debugMode
                onToggled: App.settings.debugMode = checked
            }
        }

        ListView {
            id: logList
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: Logs
            clip: true
            spacing: 1
            delegate: Rectangle {
                width: logList.width
                height: Math.max(18, messageText.implicitHeight + 4)
                color: level === 3 ? "#33202a" : (index % 2 ? "transparent" : Theme.surfaceAlt)
                RowLayout {
                    anchors.fill: parent
                    anchors.margins: 2
                    spacing: 8
                    Label { text: timeText; color: Theme.textMuted; font.pixelSize: 10; font.family: "monospace" }
                    Rectangle { width: 6; height: 6; radius: 3
                        color: level === 3 ? Theme.error : level === 2 ? Theme.warning : Theme.accent }
                    Label { text: categoryName; color: Theme.textMuted; font.pixelSize: 10; Layout.preferredWidth: 92 }
                    Label { id: messageText; text: message; color: Theme.text; font.pixelSize: 11
                        font.family: "monospace"; wrapMode: Text.WrapAnywhere; Layout.fillWidth: true }
                }
            }
            ScrollBar.vertical: ScrollBar {}
        }
    }

    function exportText() {
        let out = "";
        for (let i = 0; i < Logs.rowCount(); ++i) {
            const idx = Logs.index(i, 0);
            out += Logs.data(idx, 260) + "\n";
        }
        return out;
    }

    Component.onCompleted: {
        filterField.onTextChanged.connect(refreshFilter);
        levelBox.onActivated.connect(refreshFilter);
        categoryBox.onActivated.connect(refreshFilter);
        refreshFilter();
    }
}
