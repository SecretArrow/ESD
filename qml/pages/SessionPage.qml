import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Eclipse
import Eclipse.Internal 1.0

// One connection session: Terminal / Files / Monitor sub-pages.
Rectangle {
    id: page
    property var session: null
    color: "#14171e"

    function openFiles() { subBar.currentIndex = 1 }
    function toggleSnippets() { snippetsDrawer.open() }
    function openFind() {
        const t = terminalSplits.primaryTerm;
        if (!t)
            return;
        findBar.term = t;
        findBar.open();
    }

    Shortcut {
        sequence: StandardKey.Find
        enabled: page.visible && subBar.currentIndex === 0
        onActivated: page.openFind()
    }

    ColumnLayout {
        anchors.fill: parent
        spacing: 0

        // sub tabs
        TabBar {
            id: subBar
            Layout.fillWidth: true
            TabButton { text: qsTr("Terminal") }
            TabButton { text: qsTr("Files") }
            TabButton { text: qsTr("Monitor") }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: subBar.currentIndex

            // ============ TERMINAL ============
            ColumnLayout {
                spacing: 0

                RowLayout {
                    Layout.fillWidth: true
                    Layout.preferredHeight: 30
                    spacing: 6
                    Item { Layout.preferredWidth: 4 }
                    Button { flat: true; font.pixelSize: 11; text: qsTr("Split ▸")
                        onClicked: terminalSplits.newRight() }
                    Button { flat: true; font.pixelSize: 11; text: qsTr("Split ▾")
                        onClicked: terminalSplits.newDown() }
                    Button { flat: true; font.pixelSize: 11; text: qsTr("Copy")
                        onClicked: terminalSplits.copyActive() }
                    Button { flat: true; font.pixelSize: 11; text: qsTr("Paste")
                        onClicked: terminalSplits.pasteActive() }
                    Button { flat: true; font.pixelSize: 11; text: qsTr("Clear")
                        onClicked: terminalSplits.clearActive() }
                    Button { flat: true; font.pixelSize: 11; text: qsTr("Find")
                        onClicked: page.openFind() }
                    Button { flat: true; font.pixelSize: 11
                        text: terminalSplits.recording ? qsTr("■ Stop recording") : qsTr("● Record")
                        onClicked: terminalSplits.toggleRecording() }
                    Item { Layout.fillWidth: true }
                    Label { color: "#8b93a5"; font.pixelSize: 10
                        text: session ? qsTr("%1@%2 · %3").arg(session.username).arg(session.host).arg(session.engineName) : "" }
                    Item { Layout.preferredWidth: 8 }
                }

                // simple split container: 1..N tiles laid out by mode
                Item {
                    id: terminalSplits
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    property int mode: 0 // 0 single, 1 h-split, 2 v-split, 3 quad
                    property var tiles: []
                    property bool recording: false
                    // first tile = "active" terminal for find / shell-integration UI
                    property var primaryTerm: tiles.length > 0 ? tiles[0].term : null
                    function newRight() { mode = mode === 2 ? 3 : 1; rebuild() }
                    function newDown() { mode = mode === 1 ? 3 : 2; rebuild() }
                    function rebuild() {
                        tiles.forEach(t => t.destroy());
                        tiles = [];
                        const n = mode === 0 ? 1 : mode === 1 ? 2 : mode === 2 ? 2 : 4;
                        for (let i = 0; i < n; ++i) {
                            const c = tileComponent.createObject(terminalSplits);
                            tiles.push(c);
                        }
                        doLayout();
                        tiles.forEach(t => t.term.openTerminalIfPossible());
                    }
                    function doLayout() {
                        const w = width, h = height, m = mode;
                        for (let i = 0; i < tiles.length; ++i) {
                            const t = tiles[i];
                            if (m === 0) t.setGeometry(0, 0, w, h);
                            else if (m === 1) t.setGeometry(i === 0 ? 0 : w / 2, 0, w / 2, h);
                            else if (m === 2) t.setGeometry(0, i === 0 ? 0 : h / 2, w, h / 2);
                            else {
                                t.setGeometry(i % 2 === 0 ? 0 : w / 2, i < 2 ? 0 : h / 2, w / 2, h / 2);
                            }
                        }
                    }
                    onWidthChanged: doLayout()
                    onHeightChanged: doLayout()
                    function copyActive() { if (tiles.length) tiles[0].term.copySelection() }
                    function pasteActive() { if (tiles.length) tiles[0].term.pasteClipboard() }
                    function clearActive() { if (tiles.length) tiles[0].term.clearScrollback() }
                    function toggleRecording() {
                        recording = !recording;
                        tiles.forEach(t => t.term.recording = recording);
                    }
                    Component {
                        id: tileComponent
                        Rectangle {
                            color: "#0d0f14"
                            border.color: "#2a303c"
                            property alias term: termItem
                            function setGeometry(x, y, w, h) { this.x = x; this.y = y; this.width = w; this.height = h }
                            TermItem {
                                id: termItem
                                anchors.fill: parent
                                anchors.margins: 1
                                session: page.session
                                function openTerminalIfPossible() {
                                    if (session && session.connected)
                                        session.openTerminal(80, 24);
                                }
                            }
                        }
                    }
                    Component.onCompleted: rebuild()
                }
            }

            // ============ FILES ============
            FilesPage {
                session: page.session
            }

            // ============ MONITOR ============
            MonitorPane {
                session: page.session
            }
        }
    }

    // ---- find-in-buffer overlay (Task 2-a) --------------------------------
    TerminalFindBar {
        id: findBar
        parent: terminalSplits
        anchors.top: parent.top
        anchors.right: parent.right
        anchors.margins: 8
        matchCount: term ? term.matchCount : 0
        currentMatch: term ? term.currentMatch : 0
        onClosed: {
            if (term) {
                term.endSearch();
                term.forceActiveFocus(); // keyboard focus back to the terminal
            }
        }
    }

    // ---- shell integration banner (Task 2-a) -------------------------------
    // SessionPage-local toast (Main.qml owns the global toasts; not touched).
    Rectangle {
        id: cmdBanner
        parent: terminalSplits
        anchors.horizontalCenter: parent.horizontalCenter
        anchors.bottom: parent.bottom
        anchors.margins: 12
        radius: 17
        height: 34
        width: bannerText.implicitWidth + 30
        visible: false
        z: 40
        property string message: ""
        property bool isError: false
        color: Theme.surface
        border.color: isError ? Theme.error : Theme.success
        Label {
            id: bannerText
            anchors.centerIn: parent
            text: cmdBanner.message
            color: cmdBanner.isError ? Theme.error : Theme.success
            font.pixelSize: 12
        }
        Timer { id: bannerTimer; interval: 4000; onTriggered: cmdBanner.visible = false }
        function show(message, isError) {
            cmdBanner.message = message;
            cmdBanner.isError = isError;
            visible = true;
            bannerTimer.restart();
        }
    }

    Connections {
        target: terminalSplits.primaryTerm
        function onCommandFinished(exitCode) {
            cmdBanner.show(exitCode === 0 ? qsTr("✔ command finished (exit 0)")
                                          : qsTr("✘ command finished (exit %1)").arg(exitCode),
                           exitCode !== 0);
        }
    }

    // ---- sixel preview strip (Task 2-a; in-buffer rendering deferred) ------
    Rectangle {
        id: sixelStrip
        parent: terminalSplits
        anchors.right: parent.right
        anchors.bottom: parent.bottom
        anchors.margins: 8
        width: sixelRow.implicitWidth + 58
        height: 96
        radius: 8
        color: Theme.surface
        border.color: Theme.border
        z: 30
        clip: true
        visible: sixelRepeater.count > 0

        Row {
            id: sixelRow
            anchors.left: parent.left
            anchors.leftMargin: 8
            anchors.verticalCenter: parent.verticalCenter
            spacing: 6
            Repeater {
                id: sixelRepeater
                model: terminalSplits.primaryTerm ? terminalSplits.primaryTerm.sixelThumbnails : []
                delegate: Rectangle {
                    width: 76
                    height: 76
                    radius: 6
                    color: Theme.surfaceAlt
                    border.color: Theme.border
                    Image {
                        anchors.fill: parent
                        anchors.margins: 2
                        source: modelData
                        fillMode: Image.PreserveAspectFit
                        asynchronous: true
                    }
                    ToolTip.visible: thumbArea.containsMouse
                    ToolTip.text: qsTr("Sixel graphic (click to dismiss)")
                    MouseArea {
                        id: thumbArea
                        anchors.fill: parent
                        hoverEnabled: true
                        onClicked: if (terminalSplits.primaryTerm)
                                       terminalSplits.primaryTerm.clearSixelThumbnails()
                    }
                }
            }
        }
        Button {
            anchors.right: parent.right
            anchors.rightMargin: 6
            anchors.verticalCenter: parent.verticalCenter
            flat: true
            font.pixelSize: 12
            text: qsTr("✕")
            onClicked: if (terminalSplits.primaryTerm)
                           terminalSplits.primaryTerm.clearSixelThumbnails()
        }
    }

    // snippets drawer
    Drawer {
        id: snippetsDrawer
        edge: Qt.RightEdge
        width: 340
        height: parent.height
        SnippetsPanel {
            anchors.fill: parent
            session: page.session
        }
    }
}
