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
    property var visibleDevices: []

    readonly property int discoveredCount: Core.driveDevices.length
    readonly property var driveSettings: {
        const revision = Core.revision
        return Core.driveOptions(fenceId)
    }
    readonly property string displayMode: driveSettings.displayMode || "large"
    readonly property string informationLevel: driveSettings.informationLevel || "detailed"
    readonly property bool separateUsb: driveSettings.separateUsb !== false
    readonly property int visibleDeviceCount: visibleDevices.length

    clip: true
    boundsBehavior: Flickable.StopAtBounds
    spacing: Kirigami.Units.largeSpacing
    model: []
    QQC2.ScrollBar.vertical: QQC2.ScrollBar {}

    function refreshModel() {
        const devices = Core.visibleDriveDevices(fenceId, showHiddenTemporarily)
        visibleDevices = devices

        const groups = []
        if (separateUsb) {
            const otherDevices = []
            const usbDevices = []
            for (const device of devices) {
                if (device.usb) usbDevices.push(device)
                else otherDevices.push(device)
            }
            if (otherDevices.length > 0) {
                groups.push({ key: "other", title: i18n("Other storage"), devices: otherDevices })
            }
            if (usbDevices.length > 0) {
                groups.push({ key: "usb", title: i18n("USB devices"), devices: usbDevices })
            }
        } else if (devices.length > 0) {
            groups.push({ key: "all", title: i18n("Storage devices"), devices: devices })
        }
        model = groups
    }

    Component.onCompleted: refreshModel()
    onFenceIdChanged: refreshModel()
    onShowHiddenTemporarilyChanged: refreshModel()
    onSeparateUsbChanged: refreshModel()

    Connections {
        target: Core
        function onChanged() { view.refreshModel() }
        function onDrivesChanged() { view.refreshModel() }
    }

    header: RowLayout {
        width: view.width
        height: implicitHeight
        spacing: Kirigami.Units.smallSpacing
        QQC2.CheckBox {
            visible: view.width > Kirigami.Units.gridUnit * 15
            text: i18n("Show hidden temporarily")
            checked: view.showHiddenTemporarily
            onToggled: view.showHiddenTemporarily = checked
        }
        QQC2.ToolButton {
            visible: view.width <= Kirigami.Units.gridUnit * 15
            icon.name: view.showHiddenTemporarily ? "view-visible" : "view-hidden"
            QQC2.ToolTip.text: i18n("Show hidden temporarily")
            QQC2.ToolTip.visible: hovered
            onClicked: view.showHiddenTemporarily = !view.showHiddenTemporarily
        }
        Item { Layout.fillWidth: true }
        PlasmaComponents.Label {
            visible: view.width > Kirigami.Units.gridUnit * 15
            text: view.visibleDeviceCount > 0 ? i18n("%1 devices", view.visibleDeviceCount) : ""
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

    delegate: Item {
        required property var modelData
        width: view.width
        implicitHeight: groupLayout.implicitHeight
        height: implicitHeight

        ColumnLayout {
            id: groupLayout
            width: parent.width
            spacing: Kirigami.Units.smallSpacing

            RowLayout {
                Layout.fillWidth: true
                PlasmaComponents.Label {
                    Layout.fillWidth: true
                    text: modelData.title
                    color: view.textColor
                    font.bold: true
                }
                PlasmaComponents.Label {
                    text: String(modelData.devices.length)
                    color: view.textColor
                    opacity: 0.55
                    font: Kirigami.Theme.smallFont
                }
            }

            Repeater {
                model: modelData.devices
                delegate: DriveTreeItem {
                    required property var modelData
                    Layout.fillWidth: true
                    Layout.minimumWidth: 0
                    Layout.preferredHeight: implicitHeight
                    desk: view.desk
                    fenceId: view.fenceId
                    drive: modelData
                    depth: 0
                    displayMode: view.displayMode
                    informationLevel: view.informationLevel
                    textColor: view.textColor
                }
            }
        }
    }

    PlasmaComponents.Label {
        anchors.centerIn: parent
        visible: view.visibleDeviceCount === 0
        text: view.discoveredCount === 0
            ? i18n("Searching for storage devices…")
            : i18n("No devices match the current filters")
        color: view.textColor
        opacity: 0.6
    }
}
