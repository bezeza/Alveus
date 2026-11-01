pragma Singleton
import QtQuick
QtObject {
    property color textColor: "#fcfcfc"
    property color backgroundColor: "#202326"
    property color highlightColor: "#3daee9"
    property color highlightedTextColor: "#ffffff"
    property color negativeTextColor: "#da4453"
    property font defaultFont: Qt.font({ family: "DejaVu Sans", pointSize: 10 })
    property font smallFont: Qt.font({ family: "DejaVu Sans", pointSize: 8 })
}
