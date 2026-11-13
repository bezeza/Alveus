/*
 * A fence: title bar (drag to move, double-click to roll up, right-click for
 * the menu, tabs when several fences are merged), files inside in a grid,
 * list or honeycomb, resize from the edges. A "portal" fence shows any folder.
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
    id: fence

    // from the fence model
    property string fid
    property string title
    property real fx
    property real fy
    property real fw
    property real fh
    property bool collapsed
    property string rules
    property bool timeRule
    property string portal
    property string group
    property string tabsJson: "[]"
    property string color
    property real fenceOpacity: 0.55
    property bool blur: true
    property string view: "grid"
    property int fenceIconSize: 0
    property int screen
    property bool orphan
    property string fenceActivity   // "" = shown in every activity
    property int page

    // from the desktop
    property var desk
    property real areaWidth: 1
    property real areaHeight: 1
    property bool startRenaming: false

    readonly property var tabs: JSON.parse(tabsJson)
    readonly property int iconSize: fenceIconSize > 0 ? fenceIconSize : (desk ? desk.iconSize : 48)
    readonly property int titleHeight: Kirigami.Units.gridUnit * 1.9
    readonly property int minW: Kirigami.Units.gridUnit * 7
    readonly property int minH: titleHeight + Kirigami.Units.gridUnit * 3
    readonly property bool busy: titleMouse.drag.active || resizing
    property bool resizing: false
    property bool flashing: false

    // portal browsing (not saved: always starts at the portal's own folder)
    property string browsePath: portal
    onPortalChanged: browsePath = portal
    readonly property bool isDrives: portal === "drives:"
    readonly property bool isPortal: portal !== "" && !isDrives

    // Rolled up = only the title ("Reducio"); hovering unrolls for a moment.
    readonly property bool rolledUp: collapsed && !peek.peeking && !busy && !flashing

    readonly property color baseColor: color !== "" ? color : Kirigami.Theme.backgroundColor
    // readable text on a custom colour
    readonly property color textColor: color === "" ? Kirigami.Theme.textColor
        : ((0.299 * baseColor.r + 0.587 * baseColor.g + 0.114 * baseColor.b) > 0.6 && fenceOpacity > 0.45 ? "#1b1b1b" : "white")

    function bindGeometry() {
        x = Qt.binding(function () { return fence.fx * fence.areaWidth })
        y = Qt.binding(function () { return fence.fy * fence.areaHeight })
        width = Qt.binding(function () { return Math.max(fence.minW, fence.fw * fence.areaWidth) })
        fullHeight = Qt.binding(function () { return Math.max(fence.minH, fence.fh * fence.areaHeight) })
    }
    function commitGeometry() {
        Core.updateFence(fid, {
            "x": x / areaWidth, "y": y / areaHeight,
            "w": width / areaWidth, "h": fullHeight / areaHeight,
            "orphan": fence.orphan
        })
        bindGeometry()
    }
    function startRename() {
        titleEdit.text = fence.title
        titleEdit.visible = true
        titleEdit.forceActiveFocus()
        titleEdit.selectAll()
    }
    function flash() {
        flashing = true
        flashTimer.restart()
    }
    function names() {
        const out = []
        for (let i = 0; i < filesModel.count; ++i) {
            out.push(filesModel.get(i).path)
        }
        return out
    }

    property real fullHeight
    height: rolledUp ? titleHeight : fullHeight
    Behavior on height {
        enabled: !fence.resizing
        NumberAnimation { duration: Kirigami.Units.longDuration; easing.type: Easing.OutCubic }
    }

    Component.onCompleted: {
        bindGeometry()
        syncFiles()
        if (startRenaming) {
            startRename()
        }
    }

    // Files of this fence, kept in a model so the view doesn't reset
    ListModel { id: filesModel }
    function syncFiles() {
        const list = isDrives ? [] : isPortal ? Core.listFolder(browsePath) : Core.filesInFence(fid, desk ? desk.activity : "")
        Utils.syncModel(filesModel, list.map(Utils.fileRow), "path")
        if (desk) {
            desk.registerVisible(fid, names())
        }
    }
    Connections {
        target: Core
        function onChanged() { fence.syncFiles() }
    }
    onBrowsePathChanged: syncFiles()
    onStartRenamingChanged: if (startRenaming) Qt.callLater(startRename)
    onFidChanged: syncFiles()
    Component.onDestruction: {
        peek.stop()
        if (desk) {
            desk.registerVisible(fid, [])
        }
    }

    Timer {
        id: peek
        property bool peeking: false
        property bool wanted: false
        interval: peeking ? 450 : 300
        onTriggered: peeking = wanted
        function request(v) {
            wanted = v
            if (v === peeking) {
                stop()
            } else {
                restart()
            }
        }
    }
    Timer {
        id: flashTimer
        interval: 2400
        onTriggered: fence.flashing = false
    }

    // ---- frosted glass: the wallpaper behind the fence, blurred -------------
    Loader {
        id: backdrop
        anchors.fill: frame
        active: fence.blur && fence.desk && fence.desk.wallpaperItem !== null
        source: "Backdrop.qml"
        onLoaded: {
            item.sourceItem = Qt.binding(function () { return fence.desk.wallpaperItem })
            item.target = fence
            item.radius = Qt.binding(function () { return frame.radius })
        }
    }

    // ---- body ------------------------------------------------------------------
    Rectangle {
        id: frame
        anchors.fill: parent
        radius: Kirigami.Units.smallSpacing * 2.5
        color: Qt.rgba(fence.baseColor.r, fence.baseColor.g, fence.baseColor.b, fence.fenceOpacity)
        border.width: dropArea.containsDrag || fence.flashing ? 2 : 1
        border.color: dropArea.containsDrag || fence.flashing
            ? Kirigami.Theme.highlightColor
            : Qt.rgba(1, 1, 1, 0.16)
        clip: true

        HoverHandler {
            onHoveredChanged: if (fence.collapsed) peek.request(hovered)
        }

        // ---- title bar --------------------------------------------------------
        Rectangle {
            id: titleBar
            anchors {
                left: parent.left
                right: parent.right
                top: parent.top
            }
            height: fence.titleHeight
            radius: frame.radius
            color: Qt.rgba(0, 0, 0, titleMouse.containsMouse || titleMouse.drag.active ? 0.22 : 0.12)

            RowLayout {
                anchors {
                    fill: parent
                    leftMargin: Kirigami.Units.smallSpacing * 2
                    rightMargin: Kirigami.Units.smallSpacing * 2
                }
                spacing: Kirigami.Units.smallSpacing

                // back button inside a portal
                QQC2.ToolButton {
                    visible: fence.isPortal && fence.browsePath !== fence.portal && !titleEdit.visible
                    icon.name: "go-previous"
                    Layout.preferredWidth: fence.titleHeight - 6
                    Layout.preferredHeight: fence.titleHeight - 6
                    onClicked: fence.browsePath = Core.parentFolder(fence.browsePath)
                    QQC2.ToolTip.text: i18n("Back")
                    QQC2.ToolTip.visible: hovered
                }
                Kirigami.Icon {
                    visible: fence.isPortal && fence.browsePath === fence.portal && fence.tabs.length === 0
                    source: "folder-open"
                    Layout.preferredWidth: Kirigami.Units.iconSizes.small
                    Layout.preferredHeight: Kirigami.Units.iconSizes.small
                    opacity: 0.8
                }
                Kirigami.Icon {
                    visible: fence.isDrives && fence.tabs.length === 0
                    source: "drive-harddisk"
                    Layout.preferredWidth: Kirigami.Units.iconSizes.small
                    Layout.preferredHeight: Kirigami.Units.iconSizes.small
                    opacity: 0.8
                }
                Kirigami.Icon {
                    visible: fence.timeRule && fence.tabs.length === 0
                    source: "chronometer"
                    Layout.preferredWidth: Kirigami.Units.iconSizes.small
                    Layout.preferredHeight: Kirigami.Units.iconSizes.small
                    opacity: 0.8
                }

                // a single title, or tabs
                PlasmaComponents.Label {
                    visible: !titleEdit.visible && fence.tabs.length === 0
                    text: fence.isPortal && fence.browsePath !== fence.portal
                          ? Core.folderName(fence.browsePath)
                          : (fence.title !== "" ? fence.title : i18n("Untitled fence"))
                    font.bold: true
                    color: fence.textColor
                    elide: Text.ElideRight
                    Layout.fillWidth: true
                    horizontalAlignment: Text.AlignHCenter
                }
                Row {
                    visible: !titleEdit.visible && fence.tabs.length > 0
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    spacing: 2
                    clip: true
                    Repeater {
                        model: fence.tabs
                        delegate: Rectangle {
                            required property var modelData
                            height: parent.height - 6
                            anchors.verticalCenter: parent ? parent.verticalCenter : undefined
                            width: Math.min(tabLabel.implicitWidth + Kirigami.Units.gridUnit, Kirigami.Units.gridUnit * 8)
                            radius: Kirigami.Units.smallSpacing
                            color: modelData.active ? Qt.rgba(1, 1, 1, 0.18) : (tabMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.08) : "transparent")
                            PlasmaComponents.Label {
                                id: tabLabel
                                anchors.centerIn: parent
                                width: Math.min(implicitWidth, parent.width - 8)
                                text: modelData.title !== "" ? modelData.title : i18n("Untitled fence")
                                font.bold: modelData.active
                                color: fence.textColor
                                elide: Text.ElideRight
                            }
                            MouseArea {
                                id: tabMouse
                                anchors.fill: parent
                                hoverEnabled: true
                                acceptedButtons: Qt.LeftButton
                                onClicked: Core.activateTab(modelData.id)
                            }
                        }
                    }
                }
                QQC2.TextField {
                    id: titleEdit
                    visible: false
                    Layout.fillWidth: true
                    onAccepted: {
                        visible = false
                        Core.updateFence(fence.fid, { "title": text.trim() })
                        if (fence.desk) {
                            fence.desk.forceActiveFocus()
                        }
                    }
                    Keys.onEscapePressed: visible = false
                    onActiveFocusChanged: if (!activeFocus && visible) accepted()
                }
                PlasmaComponents.Label {
                    visible: !titleEdit.visible && filesModel.count > 0
                    text: filesModel.count
                    opacity: 0.6
                    color: fence.textColor
                    font: Kirigami.Theme.smallFont
                }
                Kirigami.Icon {
                    visible: !titleEdit.visible
                    source: fence.collapsed ? "go-down" : "go-up"
                    Layout.preferredWidth: Kirigami.Units.iconSizes.small
                    Layout.preferredHeight: Kirigami.Units.iconSizes.small
                    opacity: titleMouse.containsMouse ? 0.9 : 0.35
                }
            }

            // drag the title to move the fence; drop it on another fence's
            // title to make it a tab there
            MouseArea {
                id: titleMouse
                anchors.fill: parent
                z: -1 // tabs and buttons above get their clicks first
                enabled: !titleEdit.visible
                hoverEnabled: true
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                cursorShape: drag.active ? Qt.ClosedHandCursor : Qt.OpenHandCursor
                drag.target: fence
                drag.axis: Drag.XAndYAxis
                drag.minimumX: 0
                drag.maximumX: fence.areaWidth - fence.width
                drag.minimumY: 0
                drag.maximumY: fence.areaHeight - fence.titleHeight
                drag.threshold: Qt.styleHints.startDragDistance

                property point lastPos
                onPositionChanged: function (event) {
                    lastPos = Qt.point(event.x, event.y)
                }
                drag.onActiveChanged: {
                    if (drag.active) {
                        fence.z = 10
                        return
                    }
                    fence.z = 0
                    const p = titleMouse.mapToItem(fence.parent, lastPos.x, lastPos.y)
                    const other = fence.desk ? fence.desk.fenceTitleAt(fence, p.x, p.y) : null
                    if (other) {
                        fence.bindGeometry() // it takes the other fence's frame
                        Core.mergeFences(other.fid, fence.fid)
                        fence.desk.showUndoToast(i18n("Merged into tabs"))
                    } else {
                        fence.commitGeometry()
                    }
                }
                onDoubleClicked: function (event) {
                    if (event.button === Qt.LeftButton) {
                        Core.updateFence(fence.fid, { "collapsed": !fence.collapsed })
                    }
                }
                onClicked: function (event) {
                    if (event.button === Qt.RightButton) {
                        fenceMenu.popup(titleMouse, event.x, event.y)
                    }
                }
                QQC2.ToolTip.visible: containsMouse && !drag.active && fence.tabs.length === 0
                QQC2.ToolTip.delay: 1500
                QQC2.ToolTip.text: fence.collapsed
                    ? i18n("Engorgio — double-click to unroll")
                    : i18n("Reducio — double-click to roll up")
            }
        }

        // ---- files --------------------------------------------------------------
        Item {
            id: content
            anchors {
                top: titleBar.bottom
                left: parent.left
                right: parent.right
                bottom: parent.bottom
                margins: Kirigami.Units.smallSpacing
            }
            // nothing of the files may show through while the screen is shared
            visible: (!fence.rolledUp || fence.height > fence.titleHeight + 4) && !Core.privacy
            clip: true

            // empty area of the fence: click clears selection, drag selects
            MouseArea {
                anchors.fill: parent
                acceptedButtons: Qt.LeftButton | Qt.RightButton
                onClicked: function (event) {
                    if (event.button === Qt.RightButton) {
                        fenceMenu.popup(this, event.x, event.y)
                    } else if (fence.desk && !(event.modifiers & Qt.ControlModifier)) {
                        fence.desk.clearSelection()
                    }
                }
            }

            GridView {
                id: grid
                anchors.fill: parent
                visible: fence.view === "grid" && !fence.isDrives
                boundsBehavior: Flickable.StopAtBounds
                readonly property real minCell: fence.iconSize + Kirigami.Units.gridUnit * 3
                cellWidth: width / Math.max(1, Math.floor(width / minCell))
                cellHeight: fence.iconSize + Kirigami.Units.gridUnit * 2.6
                model: visible ? filesModel : null
                QQC2.ScrollBar.vertical: QQC2.ScrollBar {}
                delegate: FileIcon {
                    width: grid.cellWidth
                    height: grid.cellHeight
                    name: model.name
                    path: model.path
                    label: model.label
                    url: model.url
                    iconName: model.icon
                    fallbackIcon: model.fallbackIcon
                    image: model.image
                    mime: model.mime
                    isDir: model.isDir
                    size: model.size
                    modified: model.modified
                    canThumb: model.canThumb
                    desk: fence.desk
                    iconSize: fence.iconSize
                    fenceId: fence.fid
                    textColor: fence.textColor
                    inPortal: fence.isPortal
                    onOpenFolderRequested: function (p) { fence.browsePath = p }
                }
            }

            ListView {
                id: list
                anchors.fill: parent
                visible: fence.view === "list" && !fence.isDrives
                boundsBehavior: Flickable.StopAtBounds
                model: visible ? filesModel : null
                QQC2.ScrollBar.vertical: QQC2.ScrollBar {}
                delegate: FileIcon {
                    width: list.width
                    mode: "list"
                    name: model.name
                    path: model.path
                    label: model.label
                    url: model.url
                    iconName: model.icon
                    fallbackIcon: model.fallbackIcon
                    image: model.image
                    mime: model.mime
                    isDir: model.isDir
                    size: model.size
                    modified: model.modified
                    canThumb: model.canThumb
                    desk: fence.desk
                    iconSize: Math.min(fence.iconSize, 32)
                    fenceId: fence.fid
                    textColor: fence.textColor
                    inPortal: fence.isPortal
                    onOpenFolderRequested: function (p) { fence.browsePath = p }
                }
            }

            Flickable {
                id: comb
                anchors.fill: parent
                visible: fence.view === "honeycomb" && !fence.isDrives
                boundsBehavior: Flickable.StopAtBounds
                readonly property real cell: fence.iconSize + Kirigami.Units.gridUnit * 2.4
                readonly property var spots: Utils.honeycomb(filesModel.count, width, cell)
                contentHeight: Utils.honeycombHeight(filesModel.count, width, cell)
                clip: true
                QQC2.ScrollBar.vertical: QQC2.ScrollBar {}
                Repeater {
                    model: comb.visible ? filesModel : null
                    delegate: FileIcon {
                        x: comb.spots[index] ? comb.spots[index].x : 0
                        y: comb.spots[index] ? comb.spots[index].y : 0
                        width: comb.cell
                        height: comb.cell
                        mode: "honeycomb"
                        name: model.name
                        path: model.path
                        label: model.label
                        url: model.url
                        iconName: model.icon
                        fallbackIcon: model.fallbackIcon
                        image: model.image
                        mime: model.mime
                        isDir: model.isDir
                        size: model.size
                        modified: model.modified
                        canThumb: model.canThumb
                        desk: fence.desk
                        iconSize: fence.iconSize
                        fenceId: fence.fid
                        textColor: fence.textColor
                        inPortal: fence.isPortal
                        onOpenFolderRequested: function (p) { fence.browsePath = p }
                    }
                }
            }

            DrivesView {
                anchors.fill: parent
                visible: fence.isDrives
                desk: fence.desk
                textColor: fence.textColor
            }

            PlasmaComponents.Label {
                anchors.centerIn: parent
                width: parent.width - Kirigami.Units.gridUnit * 2
                visible: filesModel.count === 0 && !fence.isDrives
                horizontalAlignment: Text.AlignHCenter
                wrapMode: Text.Wrap
                color: fence.textColor
                text: fence.isPortal
                      ? (Core.folderExists(fence.browsePath) ? i18n("This folder is empty") : i18n("Folder not found: %1", fence.browsePath))
                      : (fence.rules !== "" ? i18n("Empty. New files of these types land here: %1", fence.rules)
                                            : i18n("Drag files here"))
                opacity: 0.6
                font: Kirigami.Theme.smallFont
            }
        }

        // ---- privacy: nothing readable while the screen is shared --------------
        Rectangle {
            anchors.fill: content
            visible: Core.privacy && !fence.rolledUp
            radius: frame.radius
            color: "transparent"
            ColumnLayout {
                anchors.centerIn: parent
                Kirigami.Icon {
                    source: "view-private"
                    Layout.alignment: Qt.AlignHCenter
                    Layout.preferredWidth: Kirigami.Units.iconSizes.medium
                    Layout.preferredHeight: Kirigami.Units.iconSizes.medium
                    opacity: 0.7
                }
                PlasmaComponents.Label {
                    text: i18n("Hidden while your screen is shown")
                    color: fence.textColor
                    opacity: 0.7
                    font: Kirigami.Theme.smallFont
                }
            }
            MouseArea { anchors.fill: parent } // no clicks through
        }
    }

    DropArea {
        id: dropArea
        anchors.fill: parent
        keys: ["text/uri-list"]
        onDropped: function (drop) {
            if (!drop.hasUrls || !fence.desk) {
                return
            }
            fence.desk.handleDrop(drop.urls, fence.isPortal ? "" : fence.fid,
                                  fence.isPortal ? fence.browsePath : "",
                                  fence.mapToItem(fence.parent, drop.x, drop.y), null)
            // Copy, so the source app never deletes anything on its own
            drop.accept(Qt.CopyAction)
        }
    }

    // ---- resizing (right edge, bottom edge, corner) ---------------------------
    component ResizeArea: MouseArea {
        // inline components cannot see outer ids, so work through the parent
        readonly property var f: parent
        property bool horizontal: true
        property bool vertical: true
        property point start
        property size startSize
        visible: !f.rolledUp
        hoverEnabled: true
        preventStealing: true
        onPressed: function (event) {
            start = mapToItem(f.parent, event.x, event.y)
            startSize = Qt.size(f.width, f.fullHeight)
            f.resizing = true
        }
        onPositionChanged: function (event) {
            if (!pressed) {
                return
            }
            const p = mapToItem(f.parent, event.x, event.y)
            if (horizontal) {
                f.width = Math.min(f.areaWidth - f.x, Math.max(f.minW, startSize.width + p.x - start.x))
            }
            if (vertical) {
                f.fullHeight = Math.min(f.areaHeight - f.y, Math.max(f.minH, startSize.height + p.y - start.y))
            }
        }
        onReleased: {
            f.resizing = false
            f.commitGeometry()
        }
    }

    ResizeArea {
        anchors { right: parent.right; top: parent.top; bottom: parent.bottom; topMargin: fence.titleHeight }
        width: Kirigami.Units.smallSpacing * 1.5
        vertical: false
        cursorShape: Qt.SizeHorCursor
    }
    ResizeArea {
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: Kirigami.Units.smallSpacing * 1.5
        horizontal: false
        cursorShape: Qt.SizeVerCursor
    }
    ResizeArea {
        anchors { right: parent.right; bottom: parent.bottom }
        width: Kirigami.Units.gridUnit
        height: Kirigami.Units.gridUnit
        cursorShape: Qt.SizeFDiagCursor
    }

    // ---- menu --------------------------------------------------------------------
    QQC2.Menu {
        id: fenceMenu

        QQC2.MenuItem {
            text: i18n("Rename")
            icon.name: "edit-rename"
            onTriggered: fence.startRename()
        }
        QQC2.MenuItem {
            text: i18n("Fence settings…")
            icon.name: "configure"
            onTriggered: fence.desk.openFenceSettings(fence.fid)
        }
        QQC2.MenuItem {
            text: fence.collapsed ? i18n("Unroll") : i18n("Roll up")
            icon.name: fence.collapsed ? "go-down" : "go-up"
            onTriggered: Core.updateFence(fence.fid, { "collapsed": !fence.collapsed })
        }
        QQC2.MenuItem {
            text: i18n("Select all")
            icon.name: "edit-select-all"
            enabled: filesModel.count > 0 && !fence.isDrives
            onTriggered: fence.desk.selectPaths(fence.names())
        }
        QQC2.MenuItem {
            visible: fence.isPortal
            height: visible ? implicitHeight : 0
            text: i18n("Open folder")
            icon.name: "folder-open"
            onTriggered: Core.openPath(fence.browsePath)
        }
        QQC2.MenuItem {
            text: i18n("Paste")
            icon.name: "edit-paste"
            enabled: Core.canPaste && !fence.isDrives
            onTriggered: Core.paste(fence.isPortal ? fence.browsePath : Core.desktopPath, fence.isPortal ? "" : fence.fid)
        }
        QQC2.MenuSeparator {}
        QQC2.Menu {
            id: tabMenu
            title: i18n("Add as a tab to")
            enabled: fence.desk && fence.desk.fenceChoices.length > 1

            Instantiator {
                model: fence.desk ? fence.desk.fenceChoices : []
                delegate: QQC2.MenuItem {
                    required property var modelData
                    text: modelData.title !== "" ? modelData.title : i18n("Untitled fence")
                    visible: modelData.fid !== fence.fid && (fence.group === "" || modelData.group !== fence.group)
                    height: visible ? implicitHeight : 0
                    onTriggered: Core.mergeFences(modelData.fid, fence.fid)
                }
                onObjectAdded: function (index, object) { tabMenu.insertItem(index, object) }
                onObjectRemoved: function (index, object) { tabMenu.removeItem(object) }
            }
        }
        QQC2.MenuItem {
            visible: fence.group !== ""
            height: visible ? implicitHeight : 0
            text: i18n("Detach this tab")
            icon.name: "tab-detach"
            onTriggered: Core.detachTab(fence.fid)
        }
        QQC2.Menu {
            id: pageMenu
            title: i18n("Move to page")
            Instantiator {
                model: fence.desk ? fence.desk.pageCount + 1 : 1
                delegate: QQC2.MenuItem {
                    required property int index
                    text: index < fence.desk.pageCount ? i18n("Page %1", index + 1) : i18n("New page")
                    enabled: index !== fence.page
                    onTriggered: {
                        if (index >= fence.desk.pageCount) {
                            Core.setPageCount(fence.desk.screenId, fence.desk.activity, index + 1)
                        }
                        Core.updateFence(fence.fid, { "page": index })
                    }
                }
                onObjectAdded: function (index, object) { pageMenu.insertItem(index, object) }
                onObjectRemoved: function (index, object) { pageMenu.removeItem(object) }
            }
        }
        QQC2.Menu {
            id: screenMenu
            title: i18n("Move to monitor")
            enabled: fence.desk && fence.desk.screenChoices.length > 1

            Instantiator {
                model: fence.desk ? fence.desk.screenChoices : []
                delegate: QQC2.MenuItem {
                    required property var modelData
                    text: i18n("Monitor %1", modelData + 1)
                    enabled: modelData !== fence.screen
                    onTriggered: Core.updateFence(fence.fid, { "screen": modelData })
                }
                onObjectAdded: function (index, object) { screenMenu.insertItem(index, object) }
                onObjectRemoved: function (index, object) { screenMenu.removeItem(object) }
            }
        }
        QQC2.MenuItem {
            visible: fence.desk && fence.desk.activityName !== ""
            height: visible ? implicitHeight : 0
            text: i18n("Only in activity “%1”", fence.desk ? fence.desk.activityName : "")
            checkable: true
            checked: fence.fenceActivity !== ""
            onTriggered: Core.updateFence(fence.fid, { "activity": checked ? fence.desk.activity : "" })
        }
        QQC2.MenuSeparator {}
        QQC2.MenuItem {
            text: i18n("Remove fence (files stay on the desktop)")
            icon.name: "edit-delete-remove"
            onTriggered: {
                // removing the fence destroys this item (and its i18n context)
                // right away, so take what we need first
                const desk = fence.desk
                const note = i18n("Fence removed")
                Core.removeFence(fence.fid)
                if (desk) desk.showUndoToast(note)
            }
        }
    }
}
