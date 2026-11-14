/*
 * Hierarchical storage view: physical devices contain expandable volumes.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents
import ua.bezeza.alveus

ListView {
    id: view

    property var desk
    property string fenceId
    property bool showHiddenTemporarily: false
    property color textColor: Kirigami.Theme.textColor

    readonly property int discoveredCount: Core.driveDevices.length
    readonly property string displayMode: {
        const revision = Core.revision
        return Core.driveOptions(fenceId).displayMode || "medium"
    }

    clip: true
    boundsBehavior: Flickable.StopAtBounds
    spacing: Kirigami.Units.smallSpacing
    model: []
    QQC2.ScrollBar.vertical: QQC2.ScrollBar {}

    function refreshModel() {
        model = Core.visibleDriveDevices(fenceId, showHiddenTemporarily)
    }

    Component.onCompleted: refreshModel()
    onFenceIdChanged: refreshModel()
    onShowHiddenTemporarilyChanged: refreshModel()

    Connections {
        target: Core
        function onChanged() { view.refreshModel() }
        function onDrivesChanged() { view.refreshModel() }
    }

    header: RowLayout {
        width: view.width
        spacing: Kirigami.Units.smallSpacing
        QQC2.CheckBox {
            text: i18n("Show hidden temporarily")
            checked: view.showHiddenTemporarily
            onToggled: view.showHiddenTemporarily = checked
        }
        Item { Layout.fillWidth: true }
        PlasmaComponents.Label {
            text: view.count > 0 ? i18n("%1 devices", view.count) : ""
            color: view.textColor
            opacity: 0.6
            font: Kirigami.Theme.smallFont
        }
        QQC2.ToolButton {
            icon.name: "view-refresh"
            QQC2.ToolTip.text: i18n("Refresh storage devices")
            QQC2.ToolTip.visible: hovered
            onClicked: Core.refreshDrives()
        }
    }

    delegate: DriveTreeItem {
        required property var modelData
        width: view.width
        desk: view.desk
        fenceId: view.fenceId
        drive: modelData
        depth: 0
        displayMode: view.displayMode
        textColor: view.textColor
    }

    PlasmaComponents.Label {
        anchors.centerIn: parent
        visible: view.count === 0
        text: view.discoveredCount === 0
            ? i18n("Searching for storage devices…")
            : i18n("No devices match the current filters")
        color: view.textColor
        opacity: 0.6
    }
}
