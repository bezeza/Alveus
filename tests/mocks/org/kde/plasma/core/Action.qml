// Stand-in for PlasmaCore.Action (a QAction in real Plasma)
import QtQuick
QtObject {
    property string text
    property ActionIcon icon: ActionIcon {}
    property bool enabled: true
    property bool visible: true
    property bool checkable: false
    property bool checked: false
    property bool isSeparator: false
    signal triggered()
}
