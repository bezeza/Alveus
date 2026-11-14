import QtQuick

Item {
    enum EditModeCondition { Locked, AfterPressAndHold }
    property bool relayoutLock: false
    property string configKey: ""
    property string fallbackConfigKey: ""
    property var containment
    property Item containmentItem
    property int editModeCondition: AppletsLayout.AfterPressAndHold
    property bool editMode: false
    property real minimumItemWidth: 0
    property real minimumItemHeight: 0
    property real cellWidth: 0
    property real cellHeight: 0
    property real defaultItemWidth: 0
    property real defaultItemHeight: 0
    property Component appletContainerComponent
    property Component placeHolder
}
