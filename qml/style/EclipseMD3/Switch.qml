// EclipseMD3 :: Switch — MD3 switch (52x32 track, thumb grows when on)
import QtQuick
import QtQuick.Controls.impl
import QtQuick.Templates as T

T.Switch {
    id: control

    implicitWidth: Math.max(implicitContentWidth + leftPadding + rightPadding, 116)
    implicitHeight: Math.max(implicitContentHeight, 32)

    spacing: 10
    padding: 4
    hoverEnabled: true
    font.pixelSize: 13

    indicator: Rectangle {
        implicitWidth: 52
        implicitHeight: 32
        x: control.leftPadding
        y: control.topPadding + (control.availableHeight - height) / 2
        radius: height / 2
        color: {
            if (!control.enabled)
                return control.checked ? Qt.alpha(ThemeBridge.onSurface, 0.24)
                                       : Qt.alpha(ThemeBridge.onSurface, 0.10);
            if (control.checked) {
                if (control.pressed) return ThemeBridge.primary;
                if (control.hovered) return Qt.lighter(ThemeBridge.primary, 1.08);
                return ThemeBridge.primary;
            }
            return Qt.alpha(ThemeBridge.onSurface.r, ThemeBridge.onSurface.g,
                            ThemeBridge.onSurface.b, control.pressed ? 0.12 : 0.08);
        }
        border.width: control.checked ? 0 : 2
        border.color: control.enabled ? ThemeBridge.onSurfaceVariant
                                      : Qt.alpha(ThemeBridge.onSurface, 0.24)
        Behavior on color { ColorAnimation { duration: 150 } }
        Behavior on border.color { ColorAnimation { duration: 150 } }

        // state layer ring around the thumb on hover/press
        Rectangle {
            x: control.checked ? parent.width - width - 2 : 2
            anchors.verticalCenter: parent.verticalCenter
            width: control.checked ? 28 : 20
            height: width
            radius: width / 2
            color: control.enabled && (control.hovered || control.pressed)
                   ? Qt.rgba(ThemeBridge.primary.r, ThemeBridge.primary.g,
                             ThemeBridge.primary.b, control.pressed ? 0.16 : 0.10)
                   : "transparent"
            Behavior on color { ColorAnimation { duration: 120 } }

            Rectangle {
                id: thumb
                anchors.centerIn: parent
                width: control.checked ? 24 : 16
                height: width
                radius: width / 2
                color: {
                    if (!control.enabled)
                        return ThemeBridge.surfaceContainerHighest;
                    if (control.checked)
                        return ThemeBridge.onPrimary;
                    return ThemeBridge.onSurfaceVariant;
                }
                Behavior on width { NumberAnimation { duration: 150; easing.type: Easing.OutCubic } }
                Behavior on color { ColorAnimation { duration: 150 } }

                Text {
                    anchors.centerIn: parent
                    visible: control.checked && control.enabled
                    text: String.fromCharCode(0xE5E8) // check
                    font.family: "Material Symbols Rounded"
                    font.pixelSize: 14
                    color: ThemeBridge.primary
                }
            }
        }
    }

    contentItem: IconLabel {
        leftPadding: control.indicator ? control.spacing : 0
        text: control.text
        font: control.font
        color: control.enabled ? ThemeBridge.onSurface : Qt.alpha(ThemeBridge.onSurface, 0.38)
        alignment: Qt.AlignLeft | Qt.AlignVCenter
        display: control.display
    }
}
