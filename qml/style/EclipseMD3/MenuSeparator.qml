// EclipseMD3 :: MenuSeparator
import QtQuick
import QtQuick.Templates as T

T.MenuSeparator {
    padding: 4
    topPadding: 6
    bottomPadding: 6

    contentItem: Rectangle {
        implicitWidth: 160
        implicitHeight: 1
        color: ThemeBridge.outlineVariant
    }
}
