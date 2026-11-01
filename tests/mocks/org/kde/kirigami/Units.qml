pragma Singleton
import QtQuick
QtObject {
    property int gridUnit: 18
    property int smallSpacing: 4
    property int largeSpacing: 8
    property int longDuration: 1
    property int shortDuration: 1
    property QtObject iconSizes: QtObject {
        property int small: 16
        property int smallMedium: 22
        property int medium: 32
        property int large: 48
    }
}
