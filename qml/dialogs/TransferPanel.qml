import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Eclipse.Internal 1.0

Rectangle {
    radius: 10
    color: Theme.surface
    border.color: Theme.border
    z: 500

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 8
        spacing: 4
        RowLayout {
            Layout.fillWidth: true
            Label { text: qsTr("Transfers"); font.weight: Font.DemiBold; color: Theme.text }
            Item { Layout.fillWidth: true }
            Button { flat: true; text: qsTr("Clear finished"); onClicked: Transfers.clearFinished() }
        }
        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: TransferModel { }
            clip: true
            spacing: 2
            delegate: RowLayout {
                width: ListView.view.width
                Label { text: direction === "upload" ? "↑" : "↓"; color: Theme.accent }
                Label { text: title; color: Theme.text; font.pixelSize: 11; Layout.preferredWidth: 200
                    elide: Text.ElideRight }
                ProgressBar { value: progress; Layout.preferredWidth: 160 }
                Label { text: transferred + " / " + total; color: Theme.textMuted; font.pixelSize: 10 }
                Label { text: speed; color: Theme.textMuted; font.pixelSize: 10 }
                Label { text: qsTr("ETA") + " " + eta; color: Theme.textMuted; font.pixelSize: 10 }
                Label { text: stateName; color: state === 4 ? Theme.error : state === 3 ? Theme.success : Theme.textMuted; font.pixelSize: 10 }
                Button { flat: true; text: state === 1 ? "⏸" : "▶"; onClicked: state === 1 ? Transfers.pause(index) : Transfers.resume(index) }
                Button { flat: true; text: "✕"; onClicked: Transfers.cancel(index) }
                Button { flat: true; visible: state === 4 || state === 5; text: "↻"; onClicked: Transfers.retry(index) }
            }
        }
    }
}
