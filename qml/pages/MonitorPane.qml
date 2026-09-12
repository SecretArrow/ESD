import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Lightweight server monitor via standard commands (no daemon needed).
Rectangle {
    id: pane
    property var session: null
    color: Theme.surface

    Timer { id: refreshTimer; interval: 4000; repeat: true; running: session && session.connected; onTriggered: pane.refresh() }

    function refresh() {
        if (!session || !session.connected) return;
        session.runCommand(
            "cat /proc/loadavg 2>/dev/null; echo ---; grep -E 'MemTotal|MemAvailable' /proc/meminfo 2>/dev/null; echo ---; df -P -k / 2>/dev/null | tail -1; echo ---; uptime -p 2>/dev/null || uptime; echo ---; nproc",
            (out, code) => { pane.parse(out) });
    }
    function parse(out) {
        const parts = out.split("---");
        if (parts.length >= 5) {
            loadText.text = parts[0].trim().split(" ").slice(0, 3).join(" ");
            const mem = parts[1].trim().split("\n");
            let total = 0, avail = 0;
            for (const line of mem) {
                const v = parseInt(line.replace(/[^0-9]/g, ""));
                if (line.indexOf("MemTotal") === 0) total = v;
                if (line.indexOf("MemAvailable") === 0) avail = v;
            }
            if (total > 0) {
                memBar.value = (total - avail) / total;
                memText.text = ((total - avail) / 1024 / 1024).toFixed(1) + " / " + (total / 1024 / 1024).toFixed(1) + " GB";
            }
            const df = parts[2].trim().split(/\s+/);
            if (df.length >= 5) {
                diskBar.value = parseInt(df[4]) / 100.0;
                diskText.text = df[2] + "k / " + df[1] + "k (" + df[4] + ")";
            }
            upText.text = parts[3].trim();
            coresText.text = parts[4].trim() + " cores";
        }
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 20
        spacing: 16

        Label { text: session ? session.name : ""; font.pixelSize: 18; font.weight: Font.DemiBold; color: Theme.text }
        Label { id: upText; text: "—"; color: Theme.textMuted }

        GridLayout {
            columns: 2
            columnSpacing: 30
            rowSpacing: 14
            ColumnLayout {
                spacing: 4
                Label { text: qsTr("Memory"); color: Theme.text; font.pixelSize: 12 }
                ProgressBar { id: memBar; from: 0; to: 1; value: 0; Layout.preferredWidth: 260 }
                Label { id: memText; text: "—"; color: Theme.textMuted; font.pixelSize: 11 }
            }
            ColumnLayout {
                spacing: 4
                Label { text: qsTr("Disk (/)"); color: Theme.text; font.pixelSize: 12 }
                ProgressBar { id: diskBar; from: 0; to: 1; value: 0; Layout.preferredWidth: 260 }
                Label { id: diskText; text: "—"; color: Theme.textMuted; font.pixelSize: 11 }
            }
            ColumnLayout {
                spacing: 4
                Label { text: qsTr("Load average"); color: Theme.text; font.pixelSize: 12 }
                Label { id: loadText; text: "—"; color: Theme.text; font.pixelSize: 15; font.family: "monospace" }
            }
            ColumnLayout {
                spacing: 4
                Label { text: qsTr("CPU cores"); color: Theme.text; font.pixelSize: 12 }
                Label { id: coresText; text: "—"; color: Theme.text; font.pixelSize: 15; font.family: "monospace" }
            }
        }
        Label { text: qsTr("Collected via standard commands (/proc, df, uptime) - no agent required.")
                color: Theme.textMuted; font.pixelSize: 10 }
    }
}
