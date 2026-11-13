import QtQuick

Item {
    enum EditModeCondition { Locked, AfterPressAndHold }
    property bool relayoutLock: false
    property string configKey
    property string fallbackConfigKey
    property var containment
    property Item containmentItem
    property int editModeCondition: EditModeCondition.AfterPressAndHold
    property bool editMode: false
    property real minimumItemWidth: 1
    property real minimumItemHeight: 1
    property real cellWidth: 1
    property real cellHeight: 1
    property real defaultItemWidth: 1
    property real defaultItemHeight: 1
    property Component appletContainerComponent
    property Item placeHolder
}
