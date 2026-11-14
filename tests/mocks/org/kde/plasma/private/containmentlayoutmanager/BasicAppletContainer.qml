import QtQuick

Item {
    property int editModeCondition: ItemContainer.AfterPressAndHold
    property string configOverlaySource
    property Item applet
    function cancelEdit() {}
}
