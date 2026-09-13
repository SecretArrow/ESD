import QtQuick
import QtQuick.Controls
import Eclipse

// MD3 circular icon tool button (standard icon button with state layer).
//   icon      : Material Symbols icon name
//   iconSize  : glyph size (default 20)
//   filled    : filled glyph variant
//   danger    : error-colored glyph
//   toolTip   : optional tooltip text
ToolButton {
    id: control

    property string icon: ""
    property int iconSize: 20
    property bool filled: false
    property bool danger: false
    property string toolTip: ""

    width: 36
    height: 36
    hoverEnabled: true

    contentItem: MaterialIcon {
        icon: control.icon
        iconSize: control.iconSize
        filled: control.filled
        color: !control.enabled
               ? Theme.alpha(Theme.onSurface, 0.38)
               : (control.danger ? Theme.error
                                 : (control.pressed ? Theme.primary
                                                    : Theme.onSurfaceVariant))
        anchors.centerIn: parent
    }

    background: Rectangle {
        radius: width / 2
        color: !control.enabled ? "transparent"
              : control.pressed ? Theme.alpha(Theme.onSurface, Theme.statePress)
              : control.hovered ? Theme.alpha(Theme.onSurface, Theme.stateHover)
              : "transparent"
        Behavior on color { ColorAnimation { duration: Theme.durFast } }
    }

    ToolTip.visible: control.toolTip.length > 0 && control.hovered
    ToolTip.delay: 500
    ToolTip.text: control.toolTip
}
