// Stand-in for Kirigami.Icon: coloured tile with the icon name's key word
import QtQuick
Rectangle {
    property var source
    property string fallback
    property bool isMask: false
    readonly property string key: {
        const s = String(source || fallback || "?")
        const parts = s.split("-")
        return parts.length > 1 ? parts[parts.length - 1] : s
    }
    radius: width / 6
    color: Qt.hsla((Math.abs(key.split("").reduce((a, c) => a * 31 + c.charCodeAt(0), 7)) % 360) / 360, 0.55, 0.5, 1)
    Text {
        anchors.centerIn: parent
        width: parent.width - 4
        horizontalAlignment: Text.AlignHCenter
        text: parent.key
        color: "white"
        font.pixelSize: Math.max(7, parent.width / 5)
        elide: Text.ElideRight
    }
}
