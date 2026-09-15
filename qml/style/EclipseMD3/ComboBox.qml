// EclipseMD3 :: ComboBox — MD3 outlined field + tonal popup menu
// Supports both non-editable (label content) and editable (TextInput) modes.
import QtQuick
import QtQuick.Controls.impl
import QtQuick.Templates as T

T.ComboBox {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding
                            + (indicator ? indicator.implicitWidth : 0), 140)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset,
                             implicitContentHeight + topPadding + bottomPadding, 40)

    leftPadding: 12
    rightPadding: 8
    topPadding: 8
    bottomPadding: 8
    hoverEnabled: true
    font.pixelSize: 13

    selectionColor: ThemeBridge.primary
    selectedTextColor: ThemeBridge.onPrimary

    delegate: T.ItemDelegate {
        id: comboDelegate

        width: ListView.width
        height: 40
        highlighted: control.highlightedIndex === index
        hoverEnabled: control.hoverEnabled

        contentItem: IconLabel {
            text: comboDelegate.text
            font: control.font
            color: comboDelegate.enabled ? ThemeBridge.onSurface : Qt.alpha(ThemeBridge.onSurface, 0.38)
            alignment: Qt.AlignLeft | Qt.AlignVCenter
        }

        background: Rectangle {
            radius: 8
            color: comboDelegate.highlighted
                   ? Qt.rgba(ThemeBridge.onSurface.r, ThemeBridge.onSurface.g,
                             ThemeBridge.onSurface.b, 0.08)
                   : "transparent"
        }
    }

    indicator: Text {
        x: control.mirrored ? control.leftPadding
                            : control.width - width - control.rightPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        width: 22
        height: 22
        text: String.fromCharCode(0xE5CF) // expand_more (Material Symbols Rounded)
        font.family: "Material Symbols Rounded"
        font.pixelSize: 20
        color: control.enabled ? ThemeBridge.onSurfaceVariant : Qt.alpha(ThemeBridge.onSurface, 0.38)
        verticalAlignment: Text.AlignVCenter
        horizontalAlignment: Text.AlignHCenter
        opacity: control.popup && control.popup.opened ? 0 : 1
        rotation: control.popup && control.popup.opened ? 180 : 0
        Behavior on rotation { NumberAnimation { duration: 150 } }
    }

    contentItem: Loader {
        id: contentLoader
        sourceComponent: control.editable ? textInputComp : labelComp
        x: 0
        width: parent ? parent.width : 0
        height: parent ? parent.height : 0
    }
    Component {
        id: labelComp
        Text {
            width: parent ? parent.width : 0
            height: parent ? parent.height : 0
            text: control.displayText
            font: control.font
            color: control.enabled ? ThemeBridge.onSurface : Qt.alpha(ThemeBridge.onSurface, 0.38)
            verticalAlignment: Text.AlignVCenter
            elide: Text.ElideRight
        }
    }
    Component {
        id: textInputComp
        TextInput {
            width: parent ? parent.width : 0
            height: parent ? parent.height : 0
            text: control.editText
            font: control.font
            color: ThemeBridge.onSurface
            selectionColor: ThemeBridge.primary
            selectedTextColor: ThemeBridge.onPrimary
            verticalAlignment: Text.AlignVCenter
            autoScroll: control.editable
            cursorVisible: control.editable && control.activeFocus
        }
    }

    background: Rectangle {
        implicitWidth: 140
        implicitHeight: 40
        radius: 12
        color: control.enabled && control.activeFocus
               ? ThemeBridge.surfaceContainerLowest : "transparent"
        border.width: control.enabled && control.activeFocus ? 2 : 1
        border.color: control.enabled && control.activeFocus
                      ? ThemeBridge.primary : ThemeBridge.outline
        Behavior on border.color { ColorAnimation { duration: 120 } }
    }

    popup: T.Popup {
        y: control.height + 4
        width: Math.max(control.width, 180)
        height: Math.min(contentItem.implicitHeight + topPadding + bottomPadding,
                         control.Window.height - topMargin - bottomMargin - y)
        topPadding: 8
        bottomPadding: 8
        margins: 8

        background: Rectangle {
            radius: 12
            color: ThemeBridge.surfaceContainerHigh
            border.width: 1
            border.color: ThemeBridge.outlineVariant
        }

        contentItem: ListView {
            clip: true
            implicitHeight: contentHeight
            model: control.popup.visible ? control.delegateModel : null
            currentIndex: control.highlightedIndex
            spacing: 2
            T.ScrollIndicator.vertical: T.ScrollIndicator {}
        }
    }
}
