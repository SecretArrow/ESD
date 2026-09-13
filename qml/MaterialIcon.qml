import QtQuick
import Eclipse
import "IconMap.js" as IconMap

// Material Design 3 icon: renders a Material Symbols Rounded glyph by name.
//   icon   : icon name from IconMap.js (e.g. "add", "close", "terminal")
//   iconSize : pixel size (default 20, MD3 default icon size 24)
//   filled : use the filled font variant (active states, favorites)
// The color follows Label semantics: set `color` (defaults to onSurface).
Text {
    id: root

    property string icon: ""
    property int iconSize: 20
    property bool filled: false

    width: iconSize
    height: iconSize

    text: IconMap.code(root.icon)
    font.family: IconMap.family(root.filled)
    font.pixelSize: iconSize
    font.hintingPreference: Font.PreferNoHinting
    color: Theme.onSurface
    textFormat: Text.PlainText
    renderType: Text.QtRendering
    horizontalAlignment: Text.AlignHCenter
    verticalAlignment: Text.AlignVCenter
    antialiasing: true

    Behavior on color { ColorAnimation { duration: Theme.durFast } }
}
