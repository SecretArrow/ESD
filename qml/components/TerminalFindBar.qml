import QtQuick
import QtQuick.Controls
import Eclipse

// Compact find-in-buffer overlay bar for the terminal (Task 2-a).
// SessionPage mounts it over the terminal split area and assigns `term`
// (the active TermItem). Enter = next match, Shift+Enter = previous,
// Esc closes. On close the terminal regains keyboard focus (SessionPage
// handles the closed() signal).
Rectangle {
    id: bar

    // TermItem currently being searched (set by SessionPage on open)
    property var term: null
    property bool caseSensitive: false
    property int matchCount: 0
    property int currentMatch: 0

    signal closed()

    function open() {
        visible = true;
        findField.forceActiveFocus();
        findField.selectAll();
    }

    function close() {
        if (!visible)
            return;
        visible = false;
        closed();
    }

    implicitWidth: 480
    implicitHeight: 40
    width: Math.min(implicitWidth, parent ? parent.width - 16 : implicitWidth)
    radius: 8
    color: Theme.surface
    border.color: Theme.border
    visible: false
    z: 60

    RowLayout {
        anchors.fill: parent
        anchors.margins: 5
        spacing: 4

        TextField {
            id: findField
            Layout.fillWidth: true
            placeholderText: qsTr("Find in terminal…")
            font.pixelSize: 12
            selectByMouse: true
            onTextChanged: if (bar.term) bar.term.searchTerm = text
            Keys.onEscapePressed: bar.close()
            Keys.onReturnPressed: (event) => {
                if (!bar.term)
                    return;
                if (event.modifiers & Qt.ShiftModifier)
                    bar.term.findPrevious();
                else
                    bar.term.findNext();
                event.accepted = true;
            }
            Keys.onEnterPressed: (event) => {
                if (!bar.term)
                    return;
                if (event.modifiers & Qt.ShiftModifier)
                    bar.term.findPrevious();
                else
                    bar.term.findNext();
                event.accepted = true;
            }
        }

        ToolButton {
            id: caseButton
            text: "Aa"
            checkable: true
            font.pixelSize: 11
            font.bold: checked
            ToolTip.text: qsTr("Match case")
            ToolTip.visible: hovered
            onToggled: {
                bar.caseSensitive = checked;
                if (bar.term)
                    bar.term.searchCaseSensitive = checked;
            }
        }

        Label {
            Layout.preferredWidth: 68
            horizontalAlignment: Text.AlignHCenter
            text: findField.text.length === 0 ? ""
                  : (bar.matchCount > 0 ? (bar.currentMatch + 1) + "/" + bar.matchCount
                                        : qsTr("no hits"))
            color: bar.matchCount > 0 ? Theme.textMuted : Theme.error
            font.pixelSize: 11
            elide: Text.ElideRight
        }

        ToolButton {
            text: qsTr("↑")
            font.pixelSize: 13
            enabled: bar.matchCount > 0
            ToolTip.text: qsTr("Previous match (Shift+Enter)")
            ToolTip.visible: hovered
            onClicked: if (bar.term) bar.term.findPrevious()
        }

        ToolButton {
            text: qsTr("↓")
            font.pixelSize: 13
            enabled: bar.matchCount > 0
            ToolTip.text: qsTr("Next match (Enter)")
            ToolTip.visible: hovered
            onClicked: if (bar.term) bar.term.findNext()
        }

        ToolButton {
            text: qsTr("✕")
            font.pixelSize: 12
            ToolTip.text: qsTr("Close (Esc)")
            ToolTip.visible: hovered
            onClicked: bar.close()
        }
    }
}
