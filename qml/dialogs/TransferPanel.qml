import QtQuick
import QtQuick.Controls
import QtQuick.Layouts
import Eclipse.Internal 1.0

// Module import: gives access to the Theme singleton and MD3 components.
import Eclipse

Rectangle {
    radius: Theme.radiusL
    color: Theme.surface
    border.color: Theme.outlineVariant
    z: 500

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 12
        spacing: 8
        RowLayout {
            Layout.fillWidth: true
            spacing: 8
            MaterialIcon { icon: "swap_horiz"; iconSize: 18; color: Theme.primary }
            Label {
                text: qsTr("Transfers")
                font.pixelSize: Theme.typeTitleMedium
                font.weight: Font.Medium
                color: Theme.onSurface
            }
            Item { Layout.fillWidth: true }
            FlatButton {
                text: qsTr("Clear finished")
                iconName: "delete"
                small: true
                onClicked: Transfers.clearFinished()
            }
        }
        ListView {
            Layout.fillWidth: true
            Layout.fillHeight: true
            model: TransferModel { }
            clip: true
            spacing: 2
            delegate: RowLayout {
                width: ListView.view.width
                spacing: 10
                MaterialIcon {
                    icon: direction === "upload" ? "file_upload" : "file_download"
                    iconSize: 18
                    color: Theme.primary
                }
                Label {
                    text: title
                    color: Theme.onSurface
                    font.pixelSize: Theme.typeLabelMedium
                    Layout.preferredWidth: 200
                    elide: Text.ElideRight
                }
                ProgressBar { value: progress; Layout.preferredWidth: 160 }
                Label { text: transferred + " / " + total; color: Theme.onSurfaceVariant; font.pixelSize: Theme.typeLabelSmall }
                Label { text: speed; color: Theme.onSurfaceVariant; font.pixelSize: Theme.typeLabelSmall }
                Label { text: qsTr("ETA") + " " + eta; color: Theme.onSurfaceVariant; font.pixelSize: Theme.typeLabelSmall }
                Label {
                    text: stateName
                    color: state === 4 ? Theme.error : state === 3 ? Theme.success : Theme.onSurfaceVariant
                    font.pixelSize: Theme.typeLabelSmall
                }
                IconToolButton {
                    icon: state === 1 ? "pause" : "play_arrow"
                    iconSize: 18
                    onClicked: state === 1 ? Transfers.pause(index) : Transfers.resume(index)
                }
                IconToolButton {
                    icon: "close"
                    iconSize: 18
                    danger: true
                    onClicked: Transfers.cancel(index)
                }
                IconToolButton {
                    icon: "refresh"
                    iconSize: 18
                    visible: state === 4 || state === 5
                    onClicked: Transfers.retry(index)
                }
            }
        }
    }
}
