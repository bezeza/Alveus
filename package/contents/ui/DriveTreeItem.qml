/*
 * Recursive physical-device / partition row used by the drive fence.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents
import ua.bezeza.alveus

Item {
    id: node

    property var desk
    property string fenceId
    property var drive: ({})
    property int depth: 0
    property string displayMode: "large"
    property string informationLevel: "detailed"
    property color textColor: Kirigami.Theme.textColor
    property bool expanded: depth === 0

    readonly property var children: drive.children || []
    readonly property bool hasChildren: children.length > 0
    readonly property bool mounted: !!drive.mounted
    readonly property real used: mounted && Number(drive.total) > 0
        ? Math.min(1, Math.max(0, 1 - Number(drive.free) / Number(drive.total))) : 0
    readonly property int rowHeight: displayMode === "compact" ? 38 : displayMode === "large" ? 66 : 52
    readonly property bool compactControls: displayMode === "compact" || width < Kirigami.Units.gridUnit * 15
    readonly property int iconSize: compactControls ? Kirigami.Units.iconSizes.small
        : displayMode === "large" ? Kirigami.Units.iconSizes.large : Kirigami.Units.iconSizes.medium
    readonly property string title: drive.system
        ? (drive.physical ? i18n("System disk") : (drive.name !== "" && drive.name !== "System" ? i18n("System (%1)", drive.name) : i18n("System")))
        : (drive.name !== "" ? drive.name : drive.device)
    readonly property string mediaLabel: {
        if (drive.network) return i18n("Network")
        if (drive.type === "rom") return i18n("Optical")
        if (drive.type === "part") return drive.fsType
            ? String(drive.fsType).toUpperCase().replace("NTFS3", "NTFS").replace("FUSEBLK", "NTFS")
            : i18n("Partition")
        if (drive.type === "crypt" || drive.type === "lvm") return i18n("Logical volume")
        if (drive.physical && drive.transport === "nvme") return i18n("NVMe")
        if (drive.physical && drive.usb) {
            // ROTA=0 means non-rotational, not necessarily a SATA/NVMe SSD:
            // USB flash drives must not be mislabeled as SSDs.
            return drive.rotationalKnown && drive.rotational ? i18n("USB HDD") : i18n("USB")
        }
        if (drive.physical && drive.rotationalKnown) return drive.rotational ? i18n("HDD") : i18n("SSD")
        if (drive.type === "disk") return i18n("Disk")
        return drive.type ? String(drive.type).toUpperCase() : i18n("Volume")
    }
    readonly property string detailText: {
        if (Core.privacy) return "•••••"
        if (depth === 0 && drive.physical) {
            const model = ((drive.vendor || "") + " " + (drive.model || "")).trim()
            if (model !== "") return model + " · " + drive.device
            return drive.device
        }
        const mountPoints = drive.mountPoints || []
        if (mountPoints.length > 0) return mountPoints.join(" · ")
        if (drive.device) return drive.device
        return i18n("Not mounted")
    }

    width: parent ? parent.width : 0
    implicitHeight: content.implicitHeight + (depth === 0 ? Kirigami.Units.smallSpacing * 2 : 0)
    height: implicitHeight

    function formatSize(bytes) {
        if (desk && desk.formatSize) return desk.formatSize(Number(bytes))
        return Number(bytes).toString()
    }

    function open() {
        if (mounted && drive.mountPoint) {
            Core.openPath(drive.mountPoint)
        } else if (drive.device) {
            Core.mountDrive(drive.device)
            if (desk) desk.showToast(i18n("Mounting %1…", title))
        }
    }

    Rectangle {
        anchors.fill: parent
        radius: depth === 0 ? Kirigami.Units.smallSpacing * 1.5 : Kirigami.Units.smallSpacing
        color: depth === 0
            ? Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.035)
            : (rowMouse.containsMouse || menu.opened
                ? Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.12)
                : "transparent")
        border.width: depth === 0 ? 1 : 0
        border.color: Qt.rgba(textColor.r, textColor.g, textColor.b, 0.12)
    }

    ColumnLayout {
        id: content
        anchors.fill: parent
        anchors.margins: node.depth === 0 ? Kirigami.Units.smallSpacing * 1.5 : 0
        spacing: node.depth === 0 ? Kirigami.Units.smallSpacing : 1

        RowLayout {
            id: headerRow
            Layout.fillWidth: true
            Layout.preferredHeight: node.rowHeight
            spacing: Kirigami.Units.smallSpacing

            QQC2.ToolButton {
                visible: node.hasChildren
                enabled: visible
                Layout.preferredWidth: node.compactControls ? Kirigami.Units.iconSizes.small
                    : Kirigami.Units.iconSizes.small + Kirigami.Units.smallSpacing
                Layout.preferredHeight: Kirigami.Units.iconSizes.small + Kirigami.Units.smallSpacing
                icon.name: node.expanded ? "go-down" : "go-next"
                QQC2.ToolTip.text: node.expanded ? i18n("Collapse partitions") : i18n("Expand partitions")
                QQC2.ToolTip.visible: hovered
                onClicked: node.expanded = !node.expanded
            }
            Item {
                visible: !node.hasChildren
                Layout.preferredWidth: node.compactControls ? Kirigami.Units.iconSizes.small
                    : Kirigami.Units.iconSizes.small + Kirigami.Units.smallSpacing
                Layout.preferredHeight: Kirigami.Units.iconSizes.small + Kirigami.Units.smallSpacing
            }

            Kirigami.Icon {
                source: node.drive.icon || "drive-harddisk"
                fallback: "drive-harddisk"
                opacity: node.mounted ? 1 : 0.65
                Layout.preferredWidth: node.iconSize
                Layout.preferredHeight: node.iconSize
            }

            Item {
                id: labelsArea
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                implicitHeight: labelsColumn.implicitHeight

                ColumnLayout {
                    id: labelsColumn
                    anchors.fill: parent
                    spacing: 1

                RowLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing
                    PlasmaComponents.Label {
                        text: Core.privacy ? "•••••" : node.title
                        font.bold: true
                        color: node.textColor
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                        Layout.minimumWidth: 0
                    }
                    Rectangle {
                        radius: height / 2
                        color: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b, 0.14)
                        implicitWidth: mediaText.implicitWidth + Kirigami.Units.smallSpacing * 2
                        implicitHeight: mediaText.implicitHeight + 4
                        PlasmaComponents.Label {
                            id: mediaText
                            anchors.centerIn: parent
                            text: node.mediaLabel
                            color: node.textColor
                            font: Kirigami.Theme.smallFont
                        }
                    }
                }

                PlasmaComponents.Label {
                    visible: !node.compactControls && node.informationLevel === "detailed"
                    Layout.fillWidth: true
                    text: node.detailText
                    color: node.textColor
                    opacity: 0.62
                    font: Kirigami.Theme.smallFont
                    elide: Text.ElideMiddle
                }
            }


                MouseArea {
            id: rowMouse
            anchors.fill: parent
            z: 1
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onClicked: function(event) {
                if (event.button === Qt.RightButton) {
                    menu.popup(rowMouse, event.x, event.y)
                } else if (node.hasChildren) {
                    node.expanded = !node.expanded
                } else if (Core.setting("singleClick")) {
                    node.open()
                }
            }
            onDoubleClicked: function(event) {
                if (event.button === Qt.LeftButton && !Core.setting("singleClick") && !node.hasChildren) {
                    node.open()
                }
            }
            QQC2.ToolTip.visible: containsMouse && node.mounted && !Core.privacy
            QQC2.ToolTip.delay: 800
            QQC2.ToolTip.text: node.drive.mountPoint + " · " + node.drive.device
        }
    
    
            }

            PlasmaComponents.Label {
                visible: Number(node.drive.total) > 0 && !node.compactControls
                text: node.formatSize(node.drive.total)
                color: node.textColor
                opacity: 0.72
                font: Kirigami.Theme.smallFont
                horizontalAlignment: Text.AlignRight
            }

            QQC2.ToolButton {
                visible: !node.hasChildren && !!node.drive.device && !node.compactControls
                icon.name: node.mounted ? "document-open-folder" : "drive-harddisk"
                QQC2.ToolTip.text: node.mounted ? i18n("Open") : i18n("Mount and open")
                QQC2.ToolTip.visible: hovered
                onClicked: node.open()
            }

            QQC2.ToolButton {
                icon.name: "overflow-menu"
                QQC2.ToolTip.text: i18n("Drive actions")
                QQC2.ToolTip.visible: hovered
                onClicked: menu.open()
            }
        }

        Rectangle {
            visible: node.mounted && Number(node.drive.total) > 0
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.gridUnit * 2 + Kirigami.Units.smallSpacing
            Layout.rightMargin: Kirigami.Units.smallSpacing
            height: node.displayMode === "compact" ? 2 : node.displayMode === "large" ? 6 : 4
            radius: height / 2
            color: Qt.rgba(node.textColor.r, node.textColor.g, node.textColor.b, 0.16)
            Rectangle {
                width: parent.width * node.used
                height: parent.height
                radius: parent.radius
                color: node.used > 0.9 ? "#da4453" : node.used > 0.75 ? "#f67400" : Kirigami.Theme.highlightColor
            }
        }

        PlasmaComponents.Label {
            visible: node.mounted && Number(node.drive.total) > 0
                && node.displayMode !== "compact" && node.informationLevel === "detailed"
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.gridUnit * 2 + Kirigami.Units.smallSpacing
            Layout.rightMargin: Kirigami.Units.smallSpacing
            text: node.drive.readOnly
                ? i18n("%1 free of %2 · read only", node.formatSize(node.drive.free), node.formatSize(node.drive.total))
                : i18n("%1 free of %2", node.formatSize(node.drive.free), node.formatSize(node.drive.total))
            color: node.textColor
            opacity: 0.72
            font: Kirigami.Theme.smallFont
            elide: Text.ElideRight
        }

        PlasmaComponents.Label {
            visible: node.depth === 0 && node.hasChildren
            Layout.fillWidth: true
            Layout.leftMargin: Kirigami.Units.gridUnit * 2 + Kirigami.Units.smallSpacing
            text: i18n("%1 partitions", node.drive.childCount || node.children.length)
            color: node.textColor
            opacity: 0.62
            font: Kirigami.Theme.smallFont
        }

        Repeater {
            model: node.expanded ? node.children : []
            delegate: Loader {
                required property var modelData
                Layout.fillWidth: true
                Layout.minimumWidth: 0
                Layout.leftMargin: Kirigami.Units.gridUnit * 1.5
                Component.onCompleted: setSource(Qt.resolvedUrl("DriveTreeItem.qml"), {
                    "desk": node.desk,
                    "fenceId": node.fenceId,
                    "drive": modelData,
                    "depth": node.depth + 1,
                    "displayMode": node.displayMode,
                    "informationLevel": node.informationLevel,
                    "textColor": node.textColor
                })
            }
        }
    }

    DropArea {
        anchors.fill: parent
        enabled: node.mounted && !node.drive.readOnly && !!node.drive.mountPoint && !Core.privacy
        keys: ["text/uri-list"]
        onDropped: function(drop) {
            if (!drop.hasUrls || !node.desk) return
            node.desk.handleDrop(drop.urls, "", node.drive.mountPoint,
                                 node.mapToItem(node.desk.desktopItem, drop.x, drop.y), null)
            drop.accept(Qt.CopyAction)
        }
    }

    QQC2.Menu {
        id: menu
        QQC2.MenuItem {
            visible: !node.hasChildren
            height: visible ? implicitHeight : 0
            text: node.mounted ? i18n("Open") : i18n("Mount and open")
            icon.name: "document-open-folder"
            enabled: !!node.drive.device
            onTriggered: node.open()
        }
        QQC2.MenuItem {
            visible: !node.drive.system && !node.drive.network && !node.hasChildren
            height: visible ? implicitHeight : 0
            text: node.mounted ? i18n("Unmount") : i18n("Mount")
            icon.name: node.mounted ? "media-eject" : "drive-harddisk"
            onTriggered: node.mounted ? Core.unmountDrive(node.drive.device) : Core.mountDrive(node.drive.device)
        }
        QQC2.MenuItem {
            visible: node.mounted
            enabled: visible
            text: i18n("Open terminal here")
            icon.name: "utilities-terminal"
            onTriggered: Core.openTerminal(node.drive.mountPoint)
        }
        QQC2.MenuItem {
            visible: node.mounted
            enabled: visible
            text: i18n("Properties")
            icon.name: "document-properties"
            onTriggered: Core.showProperties(node.drive.mountPoint)
        }
        QQC2.MenuSeparator {}
        QQC2.MenuItem {
            text: i18n("Hide this item")
            icon.name: "view-hidden"
            enabled: node.drive.id !== ""
            onTriggered: Core.setDriveVisibilityException(node.fenceId, node.drive.id, "hide")
        }
        QQC2.MenuItem {
            text: i18n("Always show this item")
            icon.name: "view-visible"
            enabled: node.drive.id !== ""
            onTriggered: Core.setDriveVisibilityException(node.fenceId, node.drive.id, "show")
        }
        QQC2.MenuItem {
            text: i18n("Use automatic visibility")
            icon.name: "edit-undo"
            enabled: node.drive.id !== ""
            onTriggered: Core.setDriveVisibilityException(node.fenceId, node.drive.id, "")
        }
        QQC2.MenuSeparator {}
        QQC2.MenuItem {
            text: i18n("Refresh")
            icon.name: "view-refresh"
            onTriggered: Core.refreshDrives()
        }
    }
}
