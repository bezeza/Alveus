import QtQuick
import QtQuick.Layouts

RowLayout {
    id: root

    required property string keyText
    required property string description

    spacing: 6

    Rectangle {
        Layout.preferredWidth: keyLabel.implicitWidth + 14
        Layout.preferredHeight: 22
        radius: 5
        color: "#2B3943"
        border.width: 1
        border.color: "#52616C"

        Text {
            id: keyLabel
            anchors.centerIn: parent
            text: root.keyText
            color: "#F1F5F6"
            font.pixelSize: 10
            font.weight: Font.DemiBold
        }
    }

    Text {
        text: root.description
        color: "#AFC0C8"
        font.pixelSize: 10
    }
}
