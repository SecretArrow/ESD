import QtQuick
import QtQuick.Controls
import QtQuick.Layouts

// Module import: gives access to the Theme singleton (same-module types in
// subdirectories are not visible without it - binding errors otherwise).
import Eclipse

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
        spacing: 12

        ComboBox {
            id: profileBox
            Layout.fillWidth: true
            model: App.profiles.allProfiles().map(p => p.name)
            onActivated: (i) => { dlg.profileId = App.profiles.allProfiles()[i].id; stepsModel.clear(); App.runDiagnostics(dlg.profileId, "") }
        }

        ListView {
            Layout.fillWidth: true
            Layout.preferredHeight: 260
            clip: true
            spacing: 2
            model: ListModel { id: stepsModel }
            delegate: RowLayout {
                width: dlg.width - 60
                spacing: 10
                MaterialIcon {
                    icon: ok ? "check_circle" : "cancel"
                    iconSize: 16
                    color: ok ? Theme.success : Theme.error
                }
                Label {
                    text: step
                    color: Theme.onSurface
                    Layout.preferredWidth: 130
                    font.pixelSize: Theme.typeBodySmall
                    elide: Text.ElideRight
                }
                Label {
                    text: detail
                    color: Theme.onSurfaceVariant
                    font.pixelSize: Theme.typeLabelMedium
                    font.family: "monospace"
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                }
                Label {
                    text: ms > 0 ? ms + " ms" : ""
                    color: Theme.onSurfaceVariant
                    font.pixelSize: Theme.typeLabelSmall
                }
            }
        }
    }
}
