import QtQuick
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.kwin as KWin

Item {
    id: root

    required property int windowIndex
    required property var windowId
    required property string caption
    required property string iconName
    required property string desktopName
    required property bool minimized

    property bool selected: false

    signal activated(int targetIndex)

    Rectangle {
        anchors.fill: parent
        radius: 13
        color: root.selected ? "#E91D2A33" : "#D918242D"
        border.width: root.selected ? 2 : 1
        border.color: root.selected ? "#20CDBD" : "#40515D"
    }

    ColumnLayout {
        anchors.fill: parent
        anchors.margins: 11
        spacing: 8

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 28
            spacing: 8

            Kirigami.Icon {
                Layout.preferredWidth: 23
                Layout.preferredHeight: 23
                source: root.iconName
            }

            Text {
                Layout.fillWidth: true
                text: root.caption
                color: "#F1F5F6"
                font.pixelSize: 13
                font.weight: root.selected ? Font.DemiBold : Font.Medium
                elide: Text.ElideRight
                maximumLineCount: 1
                verticalAlignment: Text.AlignVCenter
            }
        }

        Item {
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true

            Rectangle {
                anchors.fill: parent
                radius: 7
                color: "#10171D"
            }

            KWin.WindowThumbnail {
                anchors.fill: parent
                wId: root.windowId
            }
        }

        RowLayout {
            Layout.fillWidth: true
            Layout.preferredHeight: 20
            spacing: 6

            Text {
                Layout.fillWidth: true
                text: root.desktopName
                visible: root.desktopName.length > 0
                color: "#AAB9C0"
                font.pixelSize: 10
                elide: Text.ElideRight
                maximumLineCount: 1
            }

            Text {
                visible: root.minimized
                text: i18ndc("kwin", "@info:status window state", "Minimized")
                color: "#89A0AA"
                font.pixelSize: 9
                font.capitalization: Font.AllUppercase
            }
        }
    }

    MouseArea {
        anchors.fill: parent
        onClicked: root.activated(root.windowIndex)
    }
}
