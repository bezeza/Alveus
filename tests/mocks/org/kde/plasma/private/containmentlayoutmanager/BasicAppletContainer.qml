import QtQuick

Item {
    enum EditModeCondition { Locked, AfterPressAndHold }
    property Item applet
    property int editModeCondition: BasicAppletContainer.AfterPressAndHold
    function cancelEdit() {}
}
