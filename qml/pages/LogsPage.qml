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

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 16
        spacing: 12

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
            FlatButton {
                text: qsTr("Clear")
                iconName: "delete"
                variant: "text"
                small: true
                ToolTip.text: qsTr("Clear the log")
                onClicked: Logs.clear()
            }
            FlatButton {
                text: qsTr("Copy")
                iconName: "content_copy"
                variant: "tonal"
                small: true
                ToolTip.text: qsTr("Copy the log to the clipboard")
                onClicked: App.copyToClipboard(exportText())
            }
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
                radius: Theme.radiusXS
                color: level === 3 ? Theme.errorContainer
                     : (index % 2 ? "transparent" : Theme.surfaceContainerLow)
                RowLayout {
                    anchors.fill: parent
                    anchors.leftMargin: 6
                    anchors.rightMargin: 6
                    spacing: 8
                    Label { text: timeText; color: Theme.onSurfaceVariant; font.pixelSize: Theme.typeLabelSmall; font.family: "monospace" }
                    Rectangle { width: 6; height: 6; radius: 3
                        color: level === 3 ? Theme.error : level === 2 ? Theme.warning : Theme.primary }
                    Label { text: categoryName; color: level === 3 ? Theme.onErrorContainer : Theme.onSurfaceVariant; font.pixelSize: Theme.typeLabelSmall; Layout.preferredWidth: 92 }
                    Label { id: messageText; text: message; color: level === 3 ? Theme.onErrorContainer : Theme.onSurface
                        font.pixelSize: Theme.typeLabelMedium
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
