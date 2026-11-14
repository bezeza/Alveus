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
    property var storageSummary: ({ total: 0, free: 0, used: 0, count: 0 })

    readonly property int discoveredCount: Core.driveDevices.length
    readonly property var driveSettings: {
        const revision = Core.revision
        return Core.driveOptions(fenceId)
    }
    readonly property string displayMode: driveSettings.displayMode || "large"
    readonly property string informationLevel: driveSettings.informationLevel || "detailed"
    readonly property bool separateUsb: driveSettings.separateUsb !== false
    readonly property bool showSummary: driveSettings.showSummary === true
    readonly property bool showUsageBar: driveSettings.showUsageBar !== false
    readonly property bool expandPartitionsByDefault: driveSettings.expandPartitionsByDefault === true
    readonly property string columnCount: driveSettings.columnCount || "auto"
    readonly property int minimumCardWidth: displayMode === "large" ? 280 : displayMode === "medium" ? 240 : 220
    readonly property int cardSpacing: Kirigami.Units.smallSpacing
    readonly property int requestedColumns: columnCount === "auto" ? 0 : Math.max(1, Number(columnCount))
    readonly property int maxColumns: Math.max(1, Math.floor((width + cardSpacing) / (minimumCardWidth + cardSpacing)))
    readonly property int effectiveColumns: requestedColumns === 0
        ? Math.min(maxColumns, width >= minimumCardWidth * 2 + cardSpacing ? 2 : 1)
        : Math.min(requestedColumns, maxColumns)
    readonly property real cardWidth: Math.max(1, (width - (effectiveColumns - 1) * cardSpacing) / effectiveColumns)
    readonly property int visibleDeviceCount: visibleDevices.length

    clip: true
    boundsBehavior: Flickable.StopAtBounds
    spacing: Kirigami.Units.largeSpacing
    model: []
    QQC2.ScrollBar.vertical: QQC2.ScrollBar {}

    function hasMountedDescendant(children) {
        for (const child of children) {
            if (child.mounted || hasMountedDescendant(child.children || [])) {
                return true
            }
        }
        return false
    }

    function accumulateSummary(devices, totals, seen) {
        for (const device of devices) {
            const children = device.children || []
            const hasMountedChild = hasMountedDescendant(children)
            const total = Number(device.total)
            const id = String(device.id || device.device || "")
            if (device.mounted && !hasMountedChild && total > 0 && id !== "" && !seen[id]) {
                const free = Number(device.free)
                totals.total += total
                totals.free += Math.min(total, Math.max(0, isFinite(free) ? free : 0))
                totals.count += 1
                seen[id] = true
            }
            accumulateSummary(children, totals, seen)
        }
    }

    function formatSize(bytes) {
        if (desk && desk.formatSize) return desk.formatSize(Number(bytes))
        return Number(bytes).toString()
    }

    function refreshModel() {
        const devices = Core.visibleDriveDevices(fenceId, showHiddenTemporarily)
        visibleDevices = devices

        const totals = { total: 0, free: 0, used: 0, count: 0 }
        accumulateSummary(devices, totals, ({}))
        totals.used = Math.max(0, totals.total - totals.free)
        storageSummary = totals

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

    header: ColumnLayout {
        width: view.width
        spacing: Kirigami.Units.smallSpacing

        RowLayout {
            Layout.fillWidth: true
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

        PlasmaComponents.Label {
            visible: view.showSummary && view.storageSummary.count > 0
            Layout.fillWidth: true
            text: i18n("%1 total · %2 used · %3 free",
                       view.formatSize(view.storageSummary.total),
                       view.formatSize(view.storageSummary.used),
                       view.formatSize(view.storageSummary.free))
            color: view.textColor
            opacity: 0.72
            font: Kirigami.Theme.smallFont
            elide: Text.ElideRight
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

            Flow {
                Layout.fillWidth: true
                width: parent.width
                spacing: view.cardSpacing
                implicitHeight: childrenRect.height
                height: implicitHeight

                Repeater {
                    model: modelData.devices
                    delegate: DriveTreeItem {
                        required property var modelData
                        cardWidth: view.cardWidth
                        desk: view.desk
                        fenceId: view.fenceId
                        drive: modelData
                        depth: 0
                        displayMode: view.displayMode
                        informationLevel: view.informationLevel
                        showUsageBar: view.showUsageBar
                        expandPartitionsByDefault: view.expandPartitionsByDefault
                        textColor: view.textColor
                    }
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