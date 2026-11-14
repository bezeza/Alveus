/*
 * One file or folder: icon (or thumbnail) and name.
 * Click selects, double-click opens, drag moves, right-click for the menu.
 * Three looks: grid tile, list row, honeycomb cell.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents
import ua.bezeza.alveus

import "utils.js" as Utils

Item {
    id: cell

    // file data
    property string name
    property string path
    property string label
    property string url
    property string iconName
    property string fallbackIcon
    property string image
    property string mime
    property bool isDir
    property real size
    property real modified
    property bool canThumb

    property var desk                 // the desktop (selection, menus, drag)
    property int iconSize: 48
    property string fenceId: ""       // "" = on the desktop
    property bool inPortal: false
    property bool onWallpaper: false  // white text with shadow over wallpaper
    property color textColor: Kirigami.Theme.textColor
    property string mode: "grid"      // grid | list | honeycomb

    signal openFolderRequested(string path)

    readonly property bool selected: desk ? (desk.selectionRevision >= 0 && desk.isSelected(path)) : false
    readonly property bool hidePrivate: Core.privacy
    property string thumb: ""

    width: mode === "list" ? 300 : iconSize + Kirigami.Units.gridUnit * 3
    height: mode === "list" ? Math.max(iconSize, Kirigami.Units.gridUnit * 1.6) + 4 : iconSize + Kirigami.Units.gridUnit * 2.6

    function refreshThumb() {
        thumb = canThumb ? Core.thumbnail(path) : ""
    }
    Component.onCompleted: refreshThumb()
    onPathChanged: refreshThumb()
    onModifiedChanged: refreshThumb()
    Connections {
        target: Core
        enabled: cell.canThumb && cell.thumb === ""
        function onThumbnailReady(p) {
            if (p === cell.path) {
                cell.refreshThumb()
            }
        }
    }

    Connections {
        target: cell.desk
        function onRequestRename(p) {
            if (p === cell.path) {
                cell.startRename()
            }
        }
    }

    function startRename() {
        renameField.text = cell.name
        renameField.visible = true
        renameField.forceActiveFocus()
        const dot = cell.isDir ? -1 : cell.name.lastIndexOf(".")
        renameField.select(0, dot > 0 ? dot : cell.name.length)
    }
    function open() {
        if (cell.isDir && cell.inPortal) {
            cell.openFolderRequested(cell.path)
        } else {
            Core.openPath(cell.path)
        }
    }

    // ---- look ----------------------------------------------------------------
    readonly property color hl: Kirigami.Theme.highlightColor
    readonly property real hlAlpha: selected ? (mouse.containsMouse ? 0.55 : 0.42)
                                             : (mouse.containsMouse || menu.opened ? 0.22 : 0)

    Rectangle {
        anchors.fill: parent
        anchors.margins: 2
        visible: cell.mode !== "honeycomb" && cell.hlAlpha > 0
        radius: Kirigami.Units.smallSpacing * 1.5
        color: Qt.rgba(cell.hl.r, cell.hl.g, cell.hl.b, cell.hlAlpha)
        border.width: cell.selected ? 1 : 0
        border.color: Qt.rgba(cell.hl.r, cell.hl.g, cell.hl.b, 0.9)
    }

    // honeycomb: the cell itself is a hexagon
    Canvas {
        id: hex
        visible: cell.mode === "honeycomb"
        anchors.centerIn: parent
        width: Math.min(parent.width, parent.height) * 1.05
        height: width
        property color fill: cell.hlAlpha > 0
            ? Qt.rgba(cell.hl.r, cell.hl.g, cell.hl.b, cell.hlAlpha + 0.1)
            : Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.07)
        onFillChanged: requestPaint()
        onPaint: {
            const ctx = getContext("2d")
            ctx.reset()
            const r = width / 2 - 1, cx = width / 2, cy = height / 2
            ctx.beginPath()
            for (let i = 0; i < 6; ++i) {
                const a = Math.PI / 6 + i * Math.PI / 3
                const px = cx + r * Math.cos(a), py = cy + r * Math.sin(a)
                if (i === 0) ctx.moveTo(px, py); else ctx.lineTo(px, py)
            }
            ctx.closePath()
            ctx.fillStyle = fill
            ctx.fill()
            ctx.lineWidth = 1
            ctx.strokeStyle = Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.18)
            ctx.stroke()
        }
    }

    component Picture: Item {
        id: pic
        property int size: cell.iconSize
        implicitWidth: size
        implicitHeight: size
        readonly property string pictureSource: cell.hidePrivate ? "" : (cell.thumb !== "" ? cell.thumb : cell.image)
        Image {
            id: preview
            anchors.fill: parent
            visible: pic.pictureSource !== "" && status === Image.Ready
            source: pic.pictureSource
            asynchronous: true
            fillMode: Image.PreserveAspectFit
            sourceSize.width: pic.size * 2
            sourceSize.height: pic.size * 2
            smooth: true
        }
        Kirigami.Icon {
            anchors.fill: parent
            visible: !preview.visible
            source: cell.iconName !== "" ? cell.iconName : cell.fallbackIcon
            fallback: cell.fallbackIcon !== "" ? cell.fallbackIcon : "unknown"
        }
    }

    // grid / honeycomb tile
    ColumnLayout {
        id: tile
        visible: cell.mode !== "list"
        anchors {
            fill: parent
            margins: Kirigami.Units.smallSpacing
            topMargin: cell.mode === "honeycomb" ? parent.height * 0.12 : Kirigami.Units.smallSpacing
        }
        spacing: Kirigami.Units.smallSpacing / 2

        Picture {
            Layout.alignment: Qt.AlignHCenter
            size: cell.mode === "honeycomb" ? Math.round(cell.iconSize * 0.8) : cell.iconSize
        }
        PlasmaComponents.Label {
            id: caption
            Layout.fillWidth: true
            Layout.fillHeight: true
            visible: !renameField.visible
            text: cell.hidePrivate ? "•••••" : cell.label
            horizontalAlignment: Text.AlignHCenter
            verticalAlignment: Text.AlignTop
            wrapMode: Text.Wrap
            maximumLineCount: cell.mode === "honeycomb" ? 1 : 2
            elide: Text.ElideRight
            font: Kirigami.Theme.smallFont
            color: cell.onWallpaper ? "white" : cell.textColor
            style: cell.onWallpaper ? Text.Outline : Text.Normal
            styleColor: Qt.rgba(0, 0, 0, 0.55)
        }
    }

    // list row
    RowLayout {
        visible: cell.mode === "list"
        anchors {
            fill: parent
            leftMargin: Kirigami.Units.smallSpacing
            rightMargin: Kirigami.Units.smallSpacing
        }
        spacing: Kirigami.Units.smallSpacing * 2
        Picture {
            size: Math.min(cell.iconSize, Kirigami.Units.iconSizes.medium)
        }
        PlasmaComponents.Label {
            color: cell.textColor
            Layout.fillWidth: true
            visible: !renameField.visible
            text: cell.hidePrivate ? "•••••" : cell.label
            elide: Text.ElideMiddle
        }
        PlasmaComponents.Label {
            color: cell.textColor
            visible: !cell.isDir && !cell.hidePrivate
            text: cell.desk ? cell.desk.formatSize(cell.size) : Utils.humanSize(cell.size)
            opacity: 0.6
            font: Kirigami.Theme.smallFont
        }
        PlasmaComponents.Label {
            color: cell.textColor
            visible: !cell.hidePrivate && cell.modified > 0
            text: Qt.formatDate(new Date(cell.modified), Qt.DefaultLocaleShortDate)
            opacity: 0.6
            font: Kirigami.Theme.smallFont
        }
    }

    QQC2.TextField {
        id: renameField
        visible: false
        z: 5
        anchors {
            left: parent.left
            right: parent.right
            bottom: cell.mode === "list" ? undefined : parent.bottom
            verticalCenter: cell.mode === "list" ? parent.verticalCenter : undefined
        }
        font: Kirigami.Theme.smallFont
        onAccepted: {
            visible = false
            if (text !== cell.name) {
                Core.renamePath(cell.path, text)
            }
            if (cell.desk) {
                cell.desk.forceActiveFocus()
            }
        }
        Keys.onEscapePressed: visible = false
        onActiveFocusChanged: if (!activeFocus) visible = false
    }

    // ---- dragging: a real system drag, so files can go to Dolphin too ----
    Item {
        id: dragProxy
        Drag.dragType: Drag.Automatic
        Drag.supportedActions: Qt.MoveAction | Qt.CopyAction
        Drag.proposedAction: Qt.MoveAction
        Drag.hotSpot.x: cell.iconSize / 2
        Drag.hotSpot.y: cell.iconSize / 2
    }

    MouseArea {
        id: mouse
        anchors.fill: parent
        hoverEnabled: true
        acceptedButtons: Qt.LeftButton | Qt.RightButton
        enabled: !renameField.visible

        property point pressPos
        property bool dragStarted: false
        property bool selectOnRelease: false

        onPressed: function (event) {
            pressPos = Qt.point(event.x, event.y)
            dragStarted = false
            selectOnRelease = false
            if (!cell.desk) {
                return
            }
            cell.desk.forceActiveFocus()
            const ctrl = event.modifiers & Qt.ControlModifier
            if (event.button === Qt.RightButton) {
                if (!cell.selected) {
                    cell.desk.selectOnly(cell.path)
                }
            } else if (ctrl) {
                cell.desk.toggleSelected(cell.path)
            } else if (!cell.selected) {
                cell.desk.selectOnly(cell.path)
            } else {
                selectOnRelease = true // keep a multi-selection for dragging
            }
        }
        onPositionChanged: function (event) {
            if (!pressed || dragStarted || !(event.buttons & Qt.LeftButton) || !cell.desk) {
                return
            }
            if (Math.abs(event.x - pressPos.x) + Math.abs(event.y - pressPos.y) > Qt.styleHints.startDragDistance) {
                dragStarted = true
                selectOnRelease = false
                const urls = cell.desk.selectedUrls()
                dragProxy.Drag.mimeData = { "text/uri-list": urls.join("\r\n") }
                cell.desk.dragOrigin = { name: cell.name, path: cell.path, fenceId: cell.fenceId }
                tile.grabToImage(function (result) {
                    dragProxy.Drag.imageSource = result.url
                    dragProxy.Drag.active = true
                }, Qt.size(tile.width, tile.height))
            }
        }
        onReleased: function (event) {
            dragProxy.Drag.active = false
            if (selectOnRelease && cell.desk) {
                cell.desk.selectOnly(cell.path)
            }
        }
        onClicked: function (event) {
            if (event.button === Qt.RightButton) {
                menu.popup(mouse, event.x, event.y)
            } else if (Core.setting("singleClick") && !(event.modifiers & Qt.ControlModifier)) {
                cell.open()
            }
        }
        onDoubleClicked: function (event) {
            if (event.button === Qt.LeftButton && !Core.setting("singleClick")) {
                cell.open()
            }
        }

        QQC2.ToolTip.visible: containsMouse && !pressed && !cell.hidePrivate
                              && (caption.truncated || cell.mode === "honeycomb")
        QQC2.ToolTip.delay: 700
        QQC2.ToolTip.text: cell.label
    }

    // ---- menu (acts on the whole selection) ----------------------------------
    QQC2.Menu {
        id: menu
        onAboutToShow: Core.refreshPhones()
        readonly property var paths: cell.desk ? cell.desk.selectedPaths() : [cell.path]
        readonly property bool single: paths.length <= 1

        QQC2.MenuItem {
            text: i18n("Open")
            icon.name: "document-open"
            onTriggered: menu.paths.forEach(function (p) { Core.openPath(p) })
        }
        QQC2.MenuItem {
            text: i18n("Show in folder")
            icon.name: "folder-open"
            onTriggered: Core.showInFolder(cell.path)
        }
        QQC2.MenuSeparator {}
        QQC2.MenuItem {
            text: i18n("Copy")
            icon.name: "edit-copy"
            onTriggered: Core.copyToClipboard(menu.paths, false)
        }
        QQC2.MenuItem {
            text: i18n("Cut")
            icon.name: "edit-cut"
            onTriggered: Core.copyToClipboard(menu.paths, true)
        }
        QQC2.MenuItem {
            text: i18n("Rename")
            icon.name: "edit-rename"
            enabled: menu.single
            onTriggered: cell.startRename()
        }
        QQC2.MenuSeparator { visible: !cell.inPortal }
        QQC2.Menu {
            id: moveMenu
            title: i18n("Move to fence")
            enabled: !cell.inPortal && cell.desk && cell.desk.fenceChoices.length > 0

            Instantiator {
                model: cell.desk ? cell.desk.fenceChoices : []
                delegate: QQC2.MenuItem {
                    required property var modelData
                    text: modelData.title
                    enabled: modelData.fid !== cell.fenceId
                    onTriggered: Core.assignFiles(cell.desk.selectedNames(), modelData.fid)
                }
                onObjectAdded: function (index, object) { moveMenu.insertItem(index, object) }
                onObjectRemoved: function (index, object) { moveMenu.removeItem(object) }
            }
        }
        QQC2.MenuItem {
            text: i18n("Take out of the fence")
            icon.name: "go-up"
            visible: cell.fenceId !== "" && !cell.inPortal
            height: visible ? implicitHeight : 0
            onTriggered: Core.assignFiles(cell.desk.selectedNames(), "")
        }
        QQC2.Menu {
            id: phoneMenu
            title: i18n("Send to phone")
            enabled: Core.phones.length > 0

            Instantiator {
                model: Core.phones
                delegate: QQC2.MenuItem {
                    required property var modelData
                    text: modelData.name
                    icon.name: "smartphone"
                    onTriggered: Core.sendToPhone(modelData.id, menu.paths)
                }
                onObjectAdded: function (index, object) { phoneMenu.insertItem(index, object) }
                onObjectRemoved: function (index, object) { phoneMenu.removeItem(object) }
            }
        }
        QQC2.MenuSeparator {}
        QQC2.MenuItem {
            text: i18n("Move to Trash")
            icon.name: "user-trash"
            onTriggered: {
                const n = Core.trashPaths(menu.paths)
                if (cell.desk && n > 0) {
                    cell.desk.showUndoToast(i18n("Moved to Trash: %1", n))
                }
            }
        }
        QQC2.MenuItem {
            text: i18n("Properties")
            icon.name: "document-properties"
            enabled: menu.single
            onTriggered: Core.showProperties(cell.path)
        }
    }
    Component.onDestruction: dragProxy.Drag.active = false
}
