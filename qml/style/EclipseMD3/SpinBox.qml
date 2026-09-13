// EclipseMD3 :: SpinBox — outlined field with MD3 steppers
import QtQuick
import QtQuick.Controls.impl
import QtQuick.Templates as T

T.SpinBox {
    id: control

    implicitWidth: Math.max(implicitBackgroundWidth + leftInset + rightInset,
                            implicitContentWidth + leftPadding + rightPadding, 140)
    implicitHeight: Math.max(implicitBackgroundHeight + topInset + bottomInset, 40)

    leftPadding: 12
    rightPadding: 12
    hoverEnabled: true
    font.pixelSize: 13

    validator: DoubleValidator {
        locale: control.locale.name
        bottom: Math.min(control.from, control.to)
        top: Math.max(control.from, control.to)
    }

    contentItem: TextInput {
        z: 2
        text: control.displayText
        font: control.font
        color: control.enabled ? ThemeBridge.onSurface : Qt.alpha(ThemeBridge.onSurface, 0.38)
        selectionColor: ThemeBridge.primary
        selectedTextColor: ThemeBridge.onPrimary
        horizontalAlignment: Qt.AlignHCenter
        verticalAlignment: TextInput.AlignVCenter
        readOnly: !control.editable
        validator: control.validator
        inputMethodHints: control.inputMethodHints
    }

    up.indicator: Rectangle {
        x: control.mirrored ? 0 : control.width - width
        y: 4
        width: 30
        height: control.availableHeight - 8
        radius: 8
        color: control.up.pressed
               ? Qt.rgba(ThemeBridge.onSurface.r, ThemeBridge.onSurface.g, ThemeBridge.onSurface.b, 0.12)
               : control.up.hovered
                 ? Qt.rgba(ThemeBridge.onSurface.r, ThemeBridge.onSurface.g, ThemeBridge.onSurface.b, 0.08)
                 : "transparent"
        Behavior on color { ColorAnimation { duration: 120 } }

        Text {
            anchors.centerIn: parent
            text: String.fromCharCode(0xE316) // keyboard_arrow_up
            font.family: "Material Symbols Rounded"
            font.pixelSize: 18
            color: control.enabled ? ThemeBridge.onSurfaceVariant : Qt.alpha(ThemeBridge.onSurface, 0.38)
        }
    }

    down.indicator: Rectangle {
        x: control.mirrored ? control.width - width : 0
        y: 4
        width: 30
        height: control.availableHeight - 8
        radius: 8
        color: control.down.pressed
               ? Qt.rgba(ThemeBridge.onSurface.r, ThemeBridge.onSurface.g, ThemeBridge.onSurface.b, 0.12)
               : control.down.hovered
                 ? Qt.rgba(ThemeBridge.onSurface.r, ThemeBridge.onSurface.g, ThemeBridge.onSurface.b, 0.08)
                 : "transparent"
        Behavior on color { ColorAnimation { duration: 120 } }

        Text {
            anchors.centerIn: parent
            text: String.fromCharCode(0xE313) // keyboard_arrow_down
            font.family: "Material Symbols Rounded"
            font.pixelSize: 18
            color: control.enabled ? ThemeBridge.onSurfaceVariant : Qt.alpha(ThemeBridge.onSurface, 0.38)
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
}
