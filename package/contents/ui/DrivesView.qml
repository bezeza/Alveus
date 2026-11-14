/*
 * Inside a "Drives" fence: every disk with a bar of how full it is and the
 * free space. Double-click opens it (or mounts it first); drop files on a
 * disk to copy or move them there.
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
    readonly property int discoveredCount: Core.drives.length

    clip: true
    boundsBehavior: Flickable.StopAtBounds
    spacing: Kirigami.Units.smallSpacing
    model: []
    QQC2.ScrollBar.vertical: QQC2.ScrollBar {}

    function refreshModel() {
        model = Core.visibleDrives(fenceId, showHiddenTemporarily)
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
        height: implicitHeight
        spacing: Kirigami.Units.smallSpacing
        QQC2.CheckBox {
            text: i18n("Show hidden temporarily")
            checked: view.showHiddenTemporarily
            onToggled: view.showHiddenTemporarily = checked
        }
    }

    delegate: Item {
        id: row
        required property var modelData
        readonly property bool mounted: modelData.mounted
        readonly property real used: mounted && modelData.total > 0 ? 1 - modelData.free / modelData.total : 0
        readonly property string title: modelData.system
            ? (modelData.name !== "" ? i18n("System (%1)", modelData.name) : i18n("System"))
            : (modelData.name !== "" ? modelData.name : modelData.device)

        width: view.width
        height: Kirigami.Units.gridUnit * 2.9

        function open() {
            if (mounted) {
                Core.openPath(modelData.mountPoint)
            } else {
                Core.mountDrive(modelData.device)
                if (view.desk) view.desk.showToast(i18n("Mounting %1…", row.title))
            }
        }

        Rectangle {
            anchors.fill: parent
            radius: Kirigami.Units.smallSpacing * 1.5
            color: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g, Kirigami.Theme.highlightColor.b,
                           dropHere.containsDrag ? 0.45 : (mouse.containsMouse || menu.opened ? 0.22 : 0))
            border.width: dropHere.containsDrag ? 2 : 0
            border.color: Kirigami.Theme.highlightColor
        }

        RowLayout {
            anchors {
                fill: parent
                leftMargin: Kirigami.Units.smallSpacing * 2
                rightMargin: Kirigami.Units.smallSpacing * 2
            }
            spacing: Kirigami.Units.smallSpacing * 2

            Kirigami.Icon {
                source: row.modelData.icon
                fallback: "drive-harddisk"
                opacity: row.mounted ? 1 : 0.5
                Layout.preferredWidth: Kirigami.Units.iconSizes.medium
                Layout.preferredHeight: Kirigami.Units.iconSizes.medium
            }

            ColumnLayout {
                Layout.fillWidth: true
                spacing: 2

                RowLayout {
                    Layout.fillWidth: true
                    PlasmaComponents.Label {
                        text: Core.privacy ? "•••••" : row.title
                        font.bold: true
                        color: view.textColor
                        elide: Text.ElideRight
                        Layout.fillWidth: true
                    }
                    PlasmaComponents.Label {
                        text: row.modelData.fsType.toUpperCase().replace("FUSEBLK", "NTFS").replace("NTFS3", "NTFS")
                        opacity: 0.55
                        color: view.textColor
                        font: Kirigami.Theme.smallFont
                    }
                }

                // how full it is
                Rectangle {
                    visible: row.mounted
                    Layout.fillWidth: true
                    height: Kirigami.Units.smallSpacing * 1.5
                    radius: height / 2
                    color: Qt.rgba(view.textColor.r, view.textColor.g, view.textColor.b, 0.18)
                    Rectangle {
                        width: parent.width * Math.min(1, Math.max(0, row.used))
                        height: parent.height
                        radius: parent.radius
                        color: row.used > 0.9 ? "#da4453" : row.used > 0.75 ? "#f67400" : Kirigami.Theme.highlightColor
                    }
                }

                PlasmaComponents.Label {
                    Layout.fillWidth: true
                    color: view.textColor
                    opacity: 0.75
                    font: Kirigami.Theme.smallFont
                    elide: Text.ElideRight
                    text: !row.mounted
                          ? i18n("Not mounted — double-click to mount")
                          : (row.modelData.readOnly
                             ? i18n("%1 free of %2 · read only", view.desk.formatSize(row.modelData.free), view.desk.formatSize(row.modelData.total))
                             : i18n("%1 free of %2", view.desk.formatSize(row.modelData.free), view.desk.formatSize(row.modelData.total)))
                }
            }
        }

        MouseArea {
            id: mouse
            anchors.fill: parent
            hoverEnabled: true
            acceptedButtons: Qt.LeftButton | Qt.RightButton
            onClicked: function (event) {
                if (event.button === Qt.RightButton) {
                    menu.popup(mouse, event.x, event.y)
                } else if (Core.setting("singleClick")) {
                    row.open()
                }
            }
            onDoubleClicked: function (event) {
                if (event.button === Qt.LeftButton && !Core.setting("singleClick")) row.open()
            }
            QQC2.ToolTip.visible: containsMouse && row.mounted && !Core.privacy
            QQC2.ToolTip.delay: 800
            QQC2.ToolTip.text: row.modelData.mountPoint + "  ·  " + row.modelData.device
        }

        DropArea {
            id: dropHere
            anchors.fill: parent
            enabled: row.mounted && !row.modelData.readOnly
            keys: ["text/uri-list"]
            onDropped: function (drop) {
                if (!drop.hasUrls || !view.desk) return
                view.desk.handleDrop(drop.urls, "", row.modelData.mountPoint,
                                     row.mapToItem(view.desk.desktopItem, drop.x, drop.y), null)
                drop.accept(Qt.CopyAction)
            }
        }

        QQC2.Menu {
            id: menu
            QQC2.MenuItem {
                text: row.mounted ? i18n("Open") : i18n("Mount and open")
                icon.name: "document-open-folder"
                onTriggered: row.open()
            }
            QQC2.MenuItem {
                visible: !row.modelData.system && !row.modelData.network
                height: visible ? implicitHeight : 0
                text: row.mounted ? i18n("Unmount") : i18n("Mount")
                icon.name: row.mounted ? "media-eject" : "drive-harddisk"
                onTriggered: row.mounted ? Core.unmountDrive(row.modelData.device) : Core.mountDrive(row.modelData.device)
            }
            QQC2.MenuItem {
                enabled: row.mounted
                text: i18n("Open terminal here")
                icon.name: "utilities-terminal"
                onTriggered: Core.openTerminal(row.modelData.mountPoint)
            }
            QQC2.MenuItem {
                enabled: row.mounted
                text: i18n("Properties")
                icon.name: "document-properties"
                onTriggered: Core.showProperties(row.modelData.mountPoint)
            }
            QQC2.MenuSeparator {}
            QQC2.MenuItem {
                text: i18n("Hide this item")
                icon.name: "view-hidden"
                enabled: row.modelData.id !== ""
                onTriggered: Core.setDriveVisibilityException(view.fenceId, row.modelData.id, "hide")
            }
            QQC2.MenuItem {
                text: i18n("Always show this item")
                icon.name: "view-visible"
                enabled: row.modelData.id !== ""
                onTriggered: Core.setDriveVisibilityException(view.fenceId, row.modelData.id, "show")
            }
            QQC2.MenuItem {
                text: i18n("Use automatic visibility")
                icon.name: "edit-undo"
                enabled: row.modelData.id !== ""
                onTriggered: Core.setDriveVisibilityException(view.fenceId, row.modelData.id, "")
            }
            QQC2.MenuSeparator {}
            QQC2.MenuItem {
                text: i18n("Refresh")
                icon.name: "view-refresh"
                onTriggered: Core.refreshDrives()
            }
        }
    }

    PlasmaComponents.Label {
        anchors.centerIn: parent
        visible: view.count === 0
        text: view.discoveredCount === 0
              ? i18n("Looking for drives…")
              : i18n("No drives match the current filters")
        color: view.textColor
        opacity: 0.6
    }
}
