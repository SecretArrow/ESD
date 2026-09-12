import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

Dialog {
    id: dlg
    property int profileId: 0
    title: qsTr("Connection Diagnostics")
    modal: true
    parent: Overlay.overlay
    anchors.centerIn: parent
    width: 520
    standardButtons: Dialog.Close

    function openDialog() {
        if (!profileId && App.sessions.rowCount() > 0)
            profileId = App.sessions.get(0).profileId;
        stepsModel.clear();
        open();
        if (profileId)
            App.runDiagnostics(profileId, "");
    }

    Connections {
        target: App
        function onDiagnosticsStep(step, ok, detail, ms) {
            stepsModel.append({ step: step, ok: ok, detail: detail, ms: ms });
        }
        function onDiagnosticsFinished(allOk) {
            stepsModel.append({ step: qsTr("Finished"), ok: allOk, detail: "", ms: 0 });
        }
    }

    contentItem: ColumnLayout {
        spacing: 8
        ComboBox {
            id: profileBox
            Layout.fillWidth: true
            model: App.profiles.all().map(p => p.name)
            onActivated: (i) => { dlg.profileId = App.profiles.all()[i].id; stepsModel.clear(); App.runDiagnostics(dlg.profileId, "") }
        }
        ListView {
            Layout.fillWidth: true
            Layout.preferredHeight: 260
            model: ListModel { id: stepsModel }
            delegate: RowLayout {
                width: dlg.width - 60
                Label { text: ok ? "✓" : "✗"; color: ok ? Theme.success : Theme.error; font.pixelSize: 14 }
                Label { text: step; color: Theme.text; Layout.preferredWidth: 130; font.pixelSize: 12 }
                Label { text: detail; color: Theme.textMuted; font.pixelSize: 11; font.family: "monospace";
                    elide: Text.ElideRight; Layout.fillWidth: true }
                Label { text: ms > 0 ? ms + " ms" : ""; color: Theme.textMuted; font.pixelSize: 10 }
            }
        }
    }
}
