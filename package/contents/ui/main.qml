/*
 * Alveus — desktop fences for KDE Plasma 6
 * The whole desktop of one monitor (and activity): icons you can place
 * anywhere, fences you draw with the mouse, files that sort themselves.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents
import org.kde.plasma.core as PlasmaCore
import org.kde.plasma.plasmoid
import ua.bezeza.alveus

import "utils.js" as Utils

ContainmentItem {
    id: root

    // ---- who am I --------------------------------------------------------------
    // Alveus only works as a desktop layout. Plasma also lets people add it as a
    // widget ("Add Widgets…"); then it must not act as a desktop (it would draw
    // fences over the real desktop and claim its files), only explain itself.
    readonly property bool asWidget: Plasmoid.isContainment === false
    // Plasmoid.screen is unsigned: "no screen" arrives as a huge number
    readonly property int screenId: (asWidget || Plasmoid.screen === undefined || Plasmoid.screen > 64) ? -1 : Plasmoid.screen
    readonly property string activity: Plasmoid.activity !== undefined ? Plasmoid.activity : ""
    readonly property string activityName: Plasmoid.activityName !== undefined ? Plasmoid.activityName : ""
    property int registeredScreen: -1
    property string registeredActivity: ""
    readonly property bool isPrimary: Core.revision >= 0 && screenId === Core.primaryScreen(activity)
    readonly property int iconSize: Core.revision >= 0 ? Core.setting("iconSize") : 48
    readonly property int pageCount: Core.revision >= 0 ? Core.pageCount(screenId, activity) : 1
    property int page: 0
    onPageCountChanged: if (page >= pageCount) page = pageCount - 1
    readonly property Item wallpaperItem: (root.wallpaper !== undefined && root.wallpaper !== null) ? root.wallpaper : null

    // Area free of panels, relative to this desktop
    readonly property rect freeRect: {
        const r = Plasmoid.availableScreenRect
        return (r && r.width > 0 && r.height > 0) ? r : Qt.rect(0, 0, width, height)
    }

    signal requestRename(string path)

    function syncDesktop() {
        if (registeredScreen === screenId && registeredActivity === activity) {
            return
        }
        if (registeredScreen >= 0) {
            Core.unregisterDesktop(registeredScreen, registeredActivity)
        }
        registeredScreen = screenId
        registeredActivity = activity
        if (screenId >= 0) {
            Core.registerDesktop(screenId, activity)
        }
    }
    // Later, not right here: screenId/activity are often re-evaluated while
    // another binding (pageCount, isPrimary) reads them, and registering changes
    // Core.revision, which that same binding depends on (a binding loop).
    onScreenIdChanged: Qt.callLater(syncDesktop)
    onActivityChanged: Qt.callLater(syncDesktop)
    Component.onCompleted: {
        syncDesktop()
        refresh()
        desktop.forceActiveFocus()
    }
    Component.onDestruction: {
        if (registeredScreen >= 0) {
            Core.unregisterDesktop(registeredScreen, registeredActivity)
        }
    }

    // ---- selection -------------------------------------------------------------
    property var selection: ({})
    property int selectionRevision: 0
    property var dragOrigin: null
    property var visibleInFences: ({})

    function isSelected(path) { return selection[path] === true }
    function selectOnly(path) { selection = {}; selection[path] = true; ++selectionRevision }
    function toggleSelected(path) {
        if (selection[path]) { delete selection[path] } else { selection[path] = true }
        ++selectionRevision
    }
    function selectPaths(paths, add) {
        if (!add) { selection = {} }
        paths.forEach(function (p) { selection[p] = true })
        ++selectionRevision
    }
    function clearSelection() {
        if (Object.keys(selection).length > 0) { selection = {}; ++selectionRevision }
    }
    function selectedPaths() { return Object.keys(selection).filter(function (p) { return selection[p] }) }
    function selectedUrls() { return Core.toUrls(selectedPaths()) }
    function desktopName(path) {
        const prefix = Core.desktopPath + "/"
        if (path.indexOf(prefix) !== 0) return ""
        const rest = path.substring(prefix.length)
        return rest.indexOf("/") === -1 ? rest : ""
    }
    function selectedNames() {
        return selectedPaths().map(desktopName).filter(function (n) { return n !== "" })
    }
    function registerVisible(fid, paths) {
        const v = visibleInFences
        v[fid] = paths
        visibleInFences = v
    }
    function selectAll() {
        let all = []
        for (let i = 0; i < looseModel.count; ++i) {
            const name = looseModel.get(i).name
            if (loosePositions[name]) all.push(looseModel.get(i).path)
        }
        for (const fid in visibleInFences) all = all.concat(visibleInFences[fid])
        selectPaths(all, false)
    }

    // ---- models ----------------------------------------------------------------
    ListModel { id: fencesModel }
    ListModel { id: looseModel }
    property var fenceChoices: []
    property var screenChoices: []
    property var loosePositions: ({})

    function refresh() {
        if (screenId < 0) {
            return
        }
        const fences = Core.fences(screenId, activity, page)
        // a tab group shows as one fence: its active tab
        Utils.syncModel(fencesModel, fences.filter(function (f) { return f.group === "" || f.activeTab })
                                           .map(Utils.fenceRow), "fid")
        let choices = []
        for (let p = 0; p < pageCount; ++p) {
            Core.fences(screenId, activity, p).forEach(function (f) {
                if (f.portal === "") choices.push({ fid: f.id, title: f.title, group: f.group })
            })
        }
        fenceChoices = choices
        screenChoices = Core.screens(activity)
        Utils.syncModel(looseModel, Core.looseFiles(screenId, activity).map(Utils.fileRow), "path")
        layoutLoose()
    }
    Connections {
        target: Core
        function onChanged() { root.refresh() }
        function onFenceRequested(id) { root.showFence(id) }
        function onMessage(kind, detail) {
            // one desktop speaks for all, otherwise every monitor shows the same message
            if (!root.isPrimary) return
            if (kind === "mount-failed") root.showToast(i18n("Couldn't mount the drive: %1", detail))
            else if (kind === "unmount-failed") root.showToast(i18n("Couldn't unmount the drive: %1", detail))
            else if (kind === "undo-failed") root.showToast(i18n("Couldn't undo: “%1” could not be put back. Try again later.", detail))
            else if (kind === "undo-conflict") root.showToast(i18n("Couldn't undo: “%1” is in the way.", detail))
            else if (kind === "settings-save-failed") root.showToast(i18n("Couldn't save Alveus settings: %1", detail))
            else if (kind === "settings-dir-failed") root.showToast(i18n("Couldn't create the settings folder: %1", detail))
            else if (kind === "settings-save-blocked") root.showToast(i18n("Settings were not overwritten because they cannot be safely updated: %1", detail))
            else if (kind === "settings-corrupt") root.showToast(i18n("Alveus settings were damaged. A backup was saved: %1", detail))
            else if (kind === "settings-backup-failed") root.showToast(i18n("Alveus settings were damaged and could not be backed up: %1", settingsReason(detail)))
            else if (kind === "settings-newer-version") root.showToast(i18n("These settings require a newer Alveus version: %1", detail))
            else if (kind === "settings-load-failed") root.showToast(i18n("Couldn't read Alveus settings: %1", detail))
            else if (kind === "file-copy-failed") root.showToast(i18n("Couldn't copy: %1", detail))
            else if (kind === "file-move-failed") root.showToast(i18n("Couldn't move: %1", detail))
            else if (kind === "file-source-left") root.showToast(i18n("Copied, but the original could not be removed: %1", detail))
            else if (kind === "files-done") {
                const what = detail.split("|")[0], n = parseInt(detail.split("|")[1])
                root.showUndoToast(what === "copy" ? i18np("Copied %1 item", "Copied %1 items", n)
                                                   : i18np("Moved %1 item", "Moved %1 items", n))
            }
        }
        function onFileJobChanged() { jobCard.update() }
    }
    function settingsReason(code) {
        if (code === "not-object") return i18n("the file is not a settings object")
        if (code === "bad-structure") return i18n("unexpected file structure")
        return code
    }
    onPageChanged: {
        clearSelection()
        refresh()
    }

    // ---- placing loose icons ---------------------------------------------------
    readonly property real cellW: iconSize + Kirigami.Units.gridUnit * 3
    readonly property real cellH: iconSize + Kirigami.Units.gridUnit * 2.6

    function blockedRects() {
        const w = desktop.width, h = desktop.height, out = []
        for (let i = 0; i < fencesModel.count; ++i) {
            const f = fencesModel.get(i)
            out.push(Qt.rect(f.fx * w - 6, f.fy * h - 6, f.fw * w + 12,
                             (f.collapsed ? Kirigami.Units.gridUnit * 2 : f.fh * h) + 12))
        }
        return out
    }
    function cellRect(c, r) { return Qt.rect(c * cellW, r * cellH, cellW, cellH) }
    function cellAt(x, y) {
        return { col: Math.max(0, Math.floor(x / cellW)), row: Math.max(0, Math.floor(y / cellH)) }
    }
    function layoutLoose() {
        const w = desktop.width, h = desktop.height
        if (w <= 0 || h <= 0) return
        const cols = Math.max(1, Math.floor(w / cellW))
        const rows = Math.max(1, Math.floor(h / cellH))
        const blocked = blockedRects()
        const used = {}
        const free = function (c, r) {
            if (c >= cols || r >= rows || used[c + "," + r]) return false
            const cell = cellRect(c, r)
            return !blocked.some(function (b) { return Utils.intersects(cell, b) })
        }
        const pos = {}
        const waiting = []
        for (let i = 0; i < looseModel.count; ++i) {
            const e = looseModel.get(i)
            if (e.hasPos) {
                if (e.posPage !== page) continue            // lives on another page
                if (free(e.col, e.row)) {
                    used[e.col + "," + e.row] = true
                    pos[e.name] = Qt.point(e.col * cellW, e.row * cellH)
                    continue
                }
            } else if (page !== 0) {
                continue                                     // new files start on page 1
            }
            waiting.push(e.name)
        }
        // the rest go in columns from the top-left, around fences
        for (let c = 0; c < cols && waiting.length > 0; ++c) {
            for (let r = 0; r < rows && waiting.length > 0; ++r) {
                if (free(c, r)) {
                    used[c + "," + r] = true
                    pos[waiting.shift()] = Qt.point(c * cellW, r * cellH)
                }
            }
        }
        // no room left: stack in the last cell rather than lose them
        waiting.forEach(function (n) { pos[n] = Qt.point((cols - 1) * cellW, (rows - 1) * cellH) })
        loosePositions = pos
    }
    // free cells near (col,row), nearest first
    function freeCellsNear(col, row, count, ignore) {
        const w = desktop.width, h = desktop.height
        const cols = Math.max(1, Math.floor(w / cellW)), rows = Math.max(1, Math.floor(h / cellH))
        const blocked = blockedRects(), taken = {}
        for (const name in loosePositions) {
            if (ignore && ignore.indexOf(name) !== -1) continue
            const p = loosePositions[name]
            taken[Math.round(p.x / cellW) + "," + Math.round(p.y / cellH)] = true
        }
        const out = []
        for (let d = 0; d < Math.max(cols, rows) && out.length < count; ++d) {
            for (let c = col - d; c <= col + d && out.length < count; ++c) {
                for (let r = row - d; r <= row + d && out.length < count; ++r) {
                    if (Math.max(Math.abs(c - col), Math.abs(r - row)) !== d) continue
                    if (c < 0 || r < 0 || c >= cols || r >= rows || taken[c + "," + r]) continue
                    const cell = cellRect(c, r)
                    if (blocked.some(function (b) { return Utils.intersects(cell, b) })) continue
                    taken[c + "," + r] = true
                    out.push({ col: c, row: r })
                }
            }
        }
        return out
    }
    function placeAt(names, point) {
        if (names.length === 0) return
        const target = cellAt(point.x, point.y)
        let items = []
        // moving icons already on the desktop: keep their arrangement
        const origin = dragOrigin && loosePositions[dragOrigin.name] ? loosePositions[dragOrigin.name] : null
        if (origin && names.every(function (n) { return loosePositions[n] })) {
            const oc = cellAt(origin.x + 1, origin.y + 1)
            names.forEach(function (n) {
                const p = loosePositions[n]
                const c = cellAt(p.x + 1, p.y + 1)
                items.push({ name: n, col: target.col + c.col - oc.col, row: target.row + c.row - oc.row })
            })
            const cols = Math.floor(desktop.width / cellW), rows = Math.floor(desktop.height / cellH)
            if (items.some(function (i) { return i.col < 0 || i.row < 0 || i.col >= cols || i.row >= rows })) {
                items = []
            }
        }
        if (items.length === 0) {
            const cells = freeCellsNear(target.col, target.row, names.length, names)
            names.forEach(function (n, i) {
                const c = cells[i] || target
                items.push({ name: n, col: c.col, row: c.row })
            })
        }
        Core.setFilePositions(items.map(function (i) {
            return { name: i.name, screen: root.screenId, page: root.page, col: i.col, row: i.row }
        }))
    }

    // ---- creating fences -------------------------------------------------------
    function createFenceAt(rect, title, rules, withSelection) {
        const id = Core.createFence(screenId, "", page, rect.x / desktop.width, rect.y / desktop.height,
                                    rect.width / desktop.width, rect.height / desktop.height, title, rules)
        if (withSelection) {
            const names = selectedNames()
            if (names.length > 0) Core.assignFiles(names, id)
        }
        clearSelection()
        return id
    }
    function createDefaults() {
        const specs = [
            [i18n("Programs"), "apps", 1.0],
            [i18n("Documents"), "documents", 1.6],
            [i18n("Photos"), "images", 1.2],
            [i18n("Video and music"), "media", 0.9],
            [i18n("Archives"), "archives", 0.8]
        ]
        const total = specs.reduce(function (a, s) { return a + s[2] }, 0)
        const gap = 0.02, free = 0.96 - gap * (specs.length - 1)
        let y = 0.02
        specs.forEach(function (s) {
            const h = free * s[2] / total
            Core.createFence(screenId, "", page, 0.74, y, 0.24, h, s[0], Core.presetRules(s[1]))
            y += h + gap
        })
        Core.dismissWelcome()
    }
    function newFenceInMiddle() {
        renameFenceId = createFenceAt(Qt.rect(desktop.width * 0.35, desktop.height * 0.3,
                                              desktop.width * 0.3, desktop.height * 0.35), i18n("New fence"), "", true)
    }
    property string renameFenceId: ""

    function fenceTitleAt(self, x, y) {
        for (let i = 0; i < fenceRepeater.count; ++i) {
            const f = fenceRepeater.itemAt(i)
            if (!f || f === self) continue
            if (x >= f.x && x <= f.x + f.width && y >= f.y && y <= f.y + f.titleHeight) return f
        }
        return null
    }
    function showFence(id) {
        const f = Core.fence(id)
        if (f.id === undefined || f.hostScreen !== screenId) return
        if (f.activity !== "" && f.activity !== activity) return
        if (f.group !== "") Core.activateTab(id)
        page = f.page
        Qt.callLater(function () {
            for (let i = 0; i < fenceRepeater.count; ++i) {
                const item = fenceRepeater.itemAt(i)
                if (item && (item.fid === id || item.tabsJson.indexOf(id) !== -1)) item.flash()
            }
        })
    }
    function addPage() {
        Core.setPageCount(screenId, activity, pageCount + 1)
        page = pageCount - 1
    }

    // ---- drops -----------------------------------------------------------------
    property var pendingDrop: null
    function handleDrop(urls, fenceId, folder, point, dummy) {
        const list = []
        for (let i = 0; i < urls.length; ++i) list.push(urls[i])
        if (folder !== "") {
            pendingDrop = { urls: list, folder: folder, fenceId: "", point: point }
            dropMenu.popup(desktop, point.x, point.y)
            return
        }
        const fromDesktop = [], outside = []
        list.forEach(function (u) { (Core.isDesktopUrl(u) ? fromDesktop : outside).push(u) })
        const names = fromDesktop.map(function (u) { return Core.fileNameFromUrl(u) })
        if (names.length > 0) {
            if (fenceId !== "") {
                Core.assignFiles(names, fenceId)
            } else {
                // only files that were in a fence need taking out
                const fenced = names.filter(function (n) { return Core.fenceOfFile(n, root.activity) !== "" })
                if (fenced.length > 0) Core.assignFiles(fenced, "")
                placeAt(names, point)
            }
        }
        if (outside.length > 0) {
            pendingDrop = { urls: outside, folder: "", fenceId: fenceId, point: point }
            dropMenu.popup(desktop, point.x, point.y)
        }
        dragOrigin = null
    }
    function finishDrop(copy) {
        const d = pendingDrop
        pendingDrop = null
        if (!d) return
        if (d.folder !== "") {
            Core.importToFolder(d.urls, copy, d.folder)
            return
        }
        const paths = Core.importToDesktop(d.urls, copy, d.fenceId)
        if (d.fenceId === "") {
            Qt.callLater(function () { root.placeAt(paths.map(root.desktopName).filter(function (n) { return n !== "" }), d.point) })
        }
    }

    // ---- messages --------------------------------------------------------------
    function undoLabel(text) {
        const kind = text.split("|")[0], detail = text.substring(kind.length + 1)
        switch (kind) {
        case "create-fence": return i18n("Undo: create fence “%1”", detail)
        case "edit-fence": return i18n("Undo: change fence “%1”", detail)
        case "remove-fence": return i18n("Undo: remove fence “%1”", detail)
        case "merge": return i18n("Undo: merge into tabs")
        case "detach": return i18n("Undo: detach tab")
        case "assign": return i18n("Undo: move files")
        case "move-icons": return i18n("Undo: move icons")
        case "arrange": return i18n("Undo: arrange icons")
        case "copy": return i18n("Undo: copy files")
        case "move": return i18n("Undo: move files")
        case "rename": return i18n("Undo: rename “%1”", detail)
        case "trash": return i18n("Undo: move to Trash")
        case "create": return i18n("Undo: create “%1”", detail)
        case "snapshot": return i18n("Undo: restore layout “%1”", detail)
        case "archive": return i18n("Undo: tidy up")
        }
        return i18n("Undo")
    }
    function formatSize(bytes) {
        const units = [i18n("B"), i18n("KB"), i18n("MB"), i18n("GB"), i18n("TB")]
        let v = bytes, i = 0
        while (v >= 1024 && i < units.length - 1) { v /= 1024; ++i }
        return (i === 0 || v >= 10 ? Math.round(v) : v.toFixed(1)) + " " + units[i]
    }
    readonly property Item desktopItem: desktop
    function showToast(text) { toast.show(text, false) }
    function showUndoToast(text) { toast.show(text, Core.canUndo) }
    function openFenceSettings(id) { fenceSettings.openFor(id) }
    function openCleanup() { cleanupDialog.open() }
    function openSettings(tab) { settingsDialog.openAt(tab) }
    function doUndo() {
        const label = Core.undoText
        if (Core.undo()) showToast(undoLabel(label).replace(/^[^:]*: /, i18n("Undone: ")))
    }

    // ---- the KDE right-click menu gets our items too ---------------------------
    property point lastMouse: Qt.point(desktop.width / 2, desktop.height / 2)
    function createHere(kind) {
        const path = kind === "folder" ? Core.createFolder("", i18n("New Folder"))
                                       : Core.createTextFile("", i18n("New text file") + ".txt")
        if (path === "") return
        const name = desktopName(path)
        placeAt([name], lastMouse)
        Qt.callLater(function () { root.selectOnly(path); root.requestRename(path) })
    }

    Plasmoid.contextualActions: [
        PlasmaCore.Action {
            text: i18n("Create Folder")
            icon.name: "folder-new"
            onTriggered: root.createHere("folder")
        },
        PlasmaCore.Action {
            text: i18n("Create Text File")
            icon.name: "document-new"
            onTriggered: root.createHere("text")
        },
        PlasmaCore.Action {
            text: i18n("Paste")
            icon.name: "edit-paste"
            enabled: Core.canPaste
            onTriggered: root.finishPaste()
        },
        PlasmaCore.Action {
            text: Core.canUndo ? root.undoLabel(Core.undoText) : i18n("Undo")
            icon.name: "edit-undo"
            enabled: Core.canUndo
            onTriggered: root.doUndo()
        },
        PlasmaCore.Action { isSeparator: true },
        PlasmaCore.Action {
            text: i18n("New Fence")
            icon.name: "view-grid"
            onTriggered: root.newFenceInMiddle()
        },
        PlasmaCore.Action {
            text: i18n("Arrange Icons")
            icon.name: "view-sort"
            onTriggered: Core.clearPositions(root.screenId, root.page)
        },
        PlasmaCore.Action {
            text: Core.hidden ? i18n("Show Everything") : i18n("Hide Everything")
            icon.name: Core.hidden ? "view-visible" : "view-hidden"
            onTriggered: Core.hidden = !Core.hidden
        },
        PlasmaCore.Action {
            text: i18n("Privacy Mode")
            icon.name: "view-private"
            checkable: true
            checked: Core.privacyManual
            onTriggered: Core.privacyManual = !Core.privacyManual
        },
        PlasmaCore.Action {
            text: i18n("Open Terminal Here")
            icon.name: "utilities-terminal"
            onTriggered: Core.openTerminal(Core.desktopPath)
        },
        PlasmaCore.Action {
            text: i18n("Alveus Settings…")
            icon.name: "folder-favorites"
            onTriggered: root.openSettings(0)
        },
        PlasmaCore.Action { isSeparator: true }
    ]
    function finishPaste() {
        const paths = Core.paste(Core.desktopPath, "")
        placeAt(paths.map(desktopName).filter(function (n) { return n !== "" }), lastMouse)
    }

    // =============================================================================
    // Shown instead of the desktop when Alveus was added as a widget
    Loader {
        anchors.fill: parent
        active: root.asWidget
        sourceComponent: Rectangle {
            radius: Kirigami.Units.cornerRadius
            color: Qt.rgba(Kirigami.Theme.backgroundColor.r, Kirigami.Theme.backgroundColor.g,
                           Kirigami.Theme.backgroundColor.b, 0.9)
            clip: true
            ColumnLayout {
                anchors.fill: parent
                anchors.margins: Kirigami.Units.largeSpacing
                spacing: Kirigami.Units.smallSpacing
                PlasmaComponents.Label {
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    wrapMode: Text.Wrap
                    elide: Text.ElideRight
                    text: i18n("Alveus is a desktop layout, not a widget. Remove this widget, then right-click the desktop → Configure Desktop and Wallpaper… → Layout: Alveus.")
                }
                PlasmaComponents.Button {
                    Layout.alignment: Qt.AlignHCenter
                    icon.name: "edit-delete-remove"
                    text: i18n("Remove this widget")
                    onClicked: {
                        const action = Plasmoid.internalAction("remove")
                        if (action) action.trigger()
                    }
                }
            }
        }
    }
    Layout.preferredWidth: asWidget ? Kirigami.Units.gridUnit * 18 : -1
    Layout.preferredHeight: asWidget ? Kirigami.Units.gridUnit * 9 : -1

    Item {
        id: area
        visible: !root.asWidget
        x: root.freeRect.x
        y: root.freeRect.y
        width: root.freeRect.width
        height: root.freeRect.height

        // widgets live in KDE's own layout manager; our desktop sits inside
        // it, below the widgets
        Loader {
            id: widgetsHost
            anchors.fill: parent
            source: "WidgetsHost.qml"
            onLoaded: {
                item.containmentRoot = root
                item.available = Qt.binding(function () { return root.freeRect })
                item.screenGeo = Qt.binding(function () {
                    return Plasmoid.screenGeometry !== undefined ? Plasmoid.screenGeometry : Qt.rect(0, 0, root.width, root.height)
                })
                desktop.parent = item
                desktop.z = -1
            }
        }
    }

    FocusScope {
        id: desktop
        parent: area
        width: area.width
        height: area.height
        focus: true
        onWidthChanged: root.layoutLoose()
        onHeightChanged: root.layoutLoose()

        Keys.onPressed: function (event) {
            const ctrl = event.modifiers & Qt.ControlModifier
            if (event.key === Qt.Key_Delete) {
                const n = Core.trashPaths(root.selectedPaths())
                if (n > 0) root.showUndoToast(i18n("Moved to Trash: %1", n))
            } else if (event.key === Qt.Key_F2) {
                const sel = root.selectedPaths()
                if (sel.length === 1) root.requestRename(sel[0])
            } else if (event.key === Qt.Key_Return || event.key === Qt.Key_Enter) {
                root.selectedPaths().forEach(function (p) { Core.openPath(p) })
            } else if (ctrl && event.key === Qt.Key_A) {
                root.selectAll()
            } else if (ctrl && event.key === Qt.Key_C) {
                Core.copyToClipboard(root.selectedPaths(), false)
            } else if (ctrl && event.key === Qt.Key_X) {
                Core.copyToClipboard(root.selectedPaths(), true)
            } else if (ctrl && event.key === Qt.Key_V) {
                root.finishPaste()
            } else if (ctrl && event.key === Qt.Key_Z) {
                root.doUndo()
            } else if (ctrl && event.key === Qt.Key_PageDown) {
                if (root.page < root.pageCount - 1) root.page++
            } else if (ctrl && event.key === Qt.Key_PageUp) {
                if (root.page > 0) root.page--
            } else if (event.key === Qt.Key_Escape) {
                root.clearSelection()
                band.visible = false
                chip.visible = false
            } else {
                return
            }
            event.accepted = true
        }

        HoverHandler {
            onPointChanged: root.lastMouse = point.position
        }

        // widgets dragged in from "Add Widgets" (underneath everything else)
        Loader {
            anchors.fill: parent
            source: "WidgetDrop.qml"
            onLoaded: item.containmentRoot = root
        }

        // Empty desktop: drag to select (and offer a fence there),
        // double-click to hide everything, wheel to change pages.
        // Right-click falls through to the usual KDE desktop menu.
        MouseArea {
            id: canvas
            anchors.fill: parent
            acceptedButtons: Qt.LeftButton
            property point start
            property var before: ({})
            onPressed: function (event) {
                desktop.forceActiveFocus()
                start = Qt.point(event.x, event.y)
                chip.visible = false
                before = (event.modifiers & Qt.ControlModifier) ? Object.assign({}, root.selection) : {}
                if (!(event.modifiers & Qt.ControlModifier)) root.clearSelection()
            }
            onPositionChanged: function (event) {
                if (Core.hidden) return
                const dx = event.x - start.x, dy = event.y - start.y
                if (!band.visible && Math.abs(dx) + Math.abs(dy) < Qt.styleHints.startDragDistance * 2) return
                band.visible = true
                band.x = Math.max(0, Math.min(start.x, event.x))
                band.y = Math.max(0, Math.min(start.y, event.y))
                band.width = Math.min(desktop.width, Math.max(start.x, event.x)) - band.x
                band.height = Math.min(desktop.height, Math.max(start.y, event.y)) - band.y
                // live selection of loose icons inside the box
                const sel = Object.assign({}, before)
                const box = Qt.rect(band.x, band.y, band.width, band.height)
                for (let i = 0; i < looseModel.count; ++i) {
                    const e = looseModel.get(i), p = root.loosePositions[e.name]
                    if (p && Utils.intersects(box, Qt.rect(p.x + 8, p.y + 4, root.cellW - 16, root.cellH - 8))) sel[e.path] = true
                }
                root.selection = sel
                root.selectionRevision++
            }
            onReleased: function (event) {
                if (!band.visible) return
                band.visible = false
                if (band.width >= Kirigami.Units.gridUnit * 6 && band.height >= Kirigami.Units.gridUnit * 4) {
                    chip.rect = Qt.rect(band.x, band.y, band.width, band.height)
                    chip.show()
                }
            }
            onDoubleClicked: {
                band.visible = false
                chip.visible = false
                Core.hidden = !Core.hidden
                if (Core.hidden) root.showToast(i18n("Evanesco! Double-click the desktop to bring everything back"))
            }
            onWheel: function (wheel) {
                if (root.pageCount < 2 || wheelCooldown.running) return
                wheelCooldown.start()
                if (wheel.angleDelta.y < 0 && root.page < root.pageCount - 1) root.page++
                else if (wheel.angleDelta.y > 0 && root.page > 0) root.page--
            }
            Timer { id: wheelCooldown; interval: 350 }
        }

        DropArea {
            anchors.fill: parent
            keys: ["text/uri-list"]
            onDropped: function (drop) {
                if (!drop.hasUrls) return
                root.handleDrop(drop.urls, "", "", Qt.point(drop.x, drop.y), null)
                drop.accept(Qt.CopyAction)
            }
        }

        // the box being drawn
        Rectangle {
            id: band
            visible: false
            z: 50
            radius: Kirigami.Units.smallSpacing * 2
            color: Qt.rgba(Kirigami.Theme.highlightColor.r, Kirigami.Theme.highlightColor.g,
                           Kirigami.Theme.highlightColor.b, 0.22)
            border.width: 2
            border.color: Kirigami.Theme.highlightColor
        }

        // after drawing a box: "make a fence here"
        Rectangle {
            id: chip
            visible: false
            z: 60
            property rect rect
            function show() {
                visible = true
                chipTimer.restart()
            }
            x: Math.min(rect.x + rect.width - width, desktop.width - width - 4)
            y: Math.min(rect.y + rect.height + 6, desktop.height - height - 4)
            width: chipRow.implicitWidth + Kirigami.Units.smallSpacing * 2
            height: chipRow.implicitHeight + Kirigami.Units.smallSpacing * 2
            radius: height / 2
            color: Kirigami.Theme.highlightColor
            Timer { id: chipTimer; interval: 6000; onTriggered: chip.visible = false }
            HoverHandler { onHoveredChanged: if (hovered) chipTimer.stop(); else chipTimer.restart() }
            RowLayout {
                id: chipRow
                anchors.centerIn: parent
                spacing: 0
                PillButton {
                    iconName: "list-add"
                    text: root.selectedNames().length > 0
                          ? i18n("Make a fence with %1 files", root.selectionRevision >= 0 ? root.selectedNames().length : 0)
                          : i18n("Make a fence here")
                    onClicked: {
                        chip.visible = false
                        root.renameFenceId = root.createFenceAt(chip.rect, i18n("New fence"), "", true)
                    }
                }
                PillButton {
                    text: "▾"
                    tooltip: i18n("Ready-made fences")
                    onClicked: {
                        chipTimer.stop()
                        createMenu.popup(chip, 0, chip.height)
                    }
                }
            }
        }

        // ---- loose icons ---------------------------------------------------
        Item {
            id: looseLayer
            anchors.fill: parent
            opacity: Core.hidden ? 0 : 1
            visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: Kirigami.Units.longDuration } }

            Repeater {
                model: looseModel
                delegate: FileIcon {
                    readonly property var spot: root.loosePositions[model.name]
                    visible: spot !== undefined
                    x: spot ? spot.x : 0
                    y: spot ? spot.y : 0
                    Behavior on x { NumberAnimation { duration: Kirigami.Units.shortDuration } }
                    Behavior on y { NumberAnimation { duration: Kirigami.Units.shortDuration } }
                    width: root.cellW
                    height: root.cellH
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
                    desk: root
                    iconSize: root.iconSize
                    onWallpaper: true
                }
            }
        }

        // ---- fences --------------------------------------------------------
        Item {
            id: fenceLayer
            anchors.fill: parent
            opacity: Core.hidden ? 0 : 1
            visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: Kirigami.Units.longDuration } }

            Repeater {
                id: fenceRepeater
                model: fencesModel
                delegate: Fence {
                    fid: model.fid
                    title: model.title
                    fx: model.fx
                    fy: model.fy
                    fw: model.fw
                    fh: model.fh
                    collapsed: model.collapsed
                    rules: model.rules
                    timeRule: model.timeRule
                    portal: model.portal
                    group: model.group
                    tabsJson: model.tabsJson
                    color: model.color
                    fenceOpacity: model.fenceOpacity
                    blur: model.blur
                    view: model.view
                    fenceIconSize: model.fenceIconSize
                    screen: model.screen
                    orphan: model.orphan
                    fenceActivity: model.activity
                    page: model.page
                    desk: root
                    areaWidth: desktop.width
                    areaHeight: desktop.height
                    startRenaming: model.fid === root.renameFenceId
                    onStartRenamingChanged: if (startRenaming) root.renameFenceId = ""
                }
            }
        }

        // ---- pages -----------------------------------------------------------
        Row {
            z: 40
            visible: root.pageCount > 1 && !Core.hidden
            anchors {
                horizontalCenter: parent.horizontalCenter
                bottom: parent.bottom
                bottomMargin: Kirigami.Units.smallSpacing * 2
            }
            spacing: Kirigami.Units.smallSpacing * 2
            Repeater {
                model: root.pageCount
                delegate: Rectangle {
                    required property int index
                    width: index === root.page ? Kirigami.Units.gridUnit * 1.4 : Kirigami.Units.smallSpacing * 2.5
                    height: Kirigami.Units.smallSpacing * 2.5
                    radius: height / 2
                    color: "white"
                    opacity: index === root.page ? 0.95 : 0.5
                    Behavior on width { NumberAnimation { duration: Kirigami.Units.shortDuration } }
                    MouseArea {
                        anchors.fill: parent
                        anchors.margins: -6
                        onClicked: root.page = index
                    }
                }
            }
        }

        // ---- first start ---------------------------------------------------
        Card {
            id: welcome
            visible: Core.showWelcome && root.isPrimary && !Core.hidden
            anchors.centerIn: parent
            width: Math.min(parent.width - Kirigami.Units.gridUnit * 4, Kirigami.Units.gridUnit * 32)

            RowLayout {
                spacing: Kirigami.Units.largeSpacing
                Kirigami.Icon {
                    source: "folder-favorites"
                    Layout.preferredWidth: Kirigami.Units.iconSizes.large
                    Layout.preferredHeight: Kirigami.Units.iconSizes.large
                }
                ColumnLayout {
                    spacing: 0
                    PlasmaComponents.Label {
                        text: i18n("Welcome to Alveus")
                        font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.5
                        font.bold: true
                    }
                    PlasmaComponents.Label {
                        text: Core.version
                        opacity: 0.6
                    }
                }
            }
            PlasmaComponents.Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                text: i18n("Drag a box on the empty desktop to make a fence. Each fence can collect files by type, name, site or date: new documents, photos or archives find their own place. Your files stay in the Desktop folder; fences only group them.")
            }
            PlasmaComponents.Label {
                Layout.fillWidth: true
                wrapMode: Text.Wrap
                opacity: 0.75
                font: Kirigami.Theme.smallFont
                text: i18n("Double-click a fence title to roll it up · double-click the desktop to hide everything · right-click for more.")
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                QQC2.Button {
                    text: i18n("I'll draw my own")
                    onClicked: Core.dismissWelcome()
                }
                QQC2.Button {
                    text: i18n("Create the usual fences")
                    icon.name: "view-sort"
                    highlighted: true
                    onClicked: root.createDefaults()
                }
            }
        }

        // ---- tidy-up reminder ------------------------------------------------
        Card {
            id: cleanupCard
            visible: Core.revision >= 0 && Core.cleanupDue() && root.isPrimary && !Core.hidden && !welcome.visible
            anchors {
                horizontalCenter: parent.horizontalCenter
                bottom: parent.bottom
                bottomMargin: Kirigami.Units.gridUnit * 2
            }
            width: Kirigami.Units.gridUnit * 22
            RowLayout {
                Kirigami.Icon {
                    source: "edit-clear-all"
                    Layout.preferredWidth: Kirigami.Units.iconSizes.medium
                    Layout.preferredHeight: Kirigami.Units.iconSizes.medium
                }
                PlasmaComponents.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    text: i18n("%1 files on the desktop haven't been opened for a long time.", Core.staleCount)
                }
            }
            RowLayout {
                Layout.alignment: Qt.AlignRight
                QQC2.Button {
                    text: i18n("Later")
                    onClicked: Core.snoozeCleanup(7)
                }
                QQC2.Button {
                    text: i18n("Have a look")
                    highlighted: true
                    onClicked: cleanupDialog.open()
                }
            }
        }

        // ---- copy / move in the background ------------------------------------
        // Shown only when it takes a moment, so quick copies don't flicker.
        Card {
            id: jobCard
            readonly property var job: Core.fileJob
            property bool due: false
            visible: due && job.active && root.isPrimary
            anchors {
                horizontalCenter: parent.horizontalCenter
                bottom: parent.bottom
                bottomMargin: cleanupCard.visible ? cleanupCard.height + Kirigami.Units.gridUnit * 3 : Kirigami.Units.gridUnit * 2
            }
            width: Kirigami.Units.gridUnit * 24
            z: 75
            function update() {
                if (!job.active) { due = false; dueTimer.stop() }
                else if (!due && !dueTimer.running) dueTimer.start()
            }
            Timer { id: dueTimer; interval: 600; onTriggered: jobCard.due = jobCard.job.active }
            RowLayout {
                spacing: Kirigami.Units.largeSpacing
                Kirigami.Icon {
                    source: jobCard.job.kind === "copy" ? "edit-copy" : (jobCard.job.kind === "undo" ? "edit-undo" : "go-next")
                    Layout.preferredWidth: Kirigami.Units.iconSizes.medium
                    Layout.preferredHeight: Kirigami.Units.iconSizes.medium
                }
                ColumnLayout {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing
                    PlasmaComponents.Label {
                        Layout.fillWidth: true
                        elide: Text.ElideMiddle
                        text: {
                            const j = jobCard.job
                            if (!j.active) return ""
                            if (j.cancelling) return i18n("Cancelling…")
                            const verb = j.kind === "copy" ? i18n("Copying “%1”", j.name)
                                       : j.kind === "undo" ? i18n("Undoing: “%1”", j.name)
                                       : i18n("Moving “%1”", j.name)
                            return j.count > 1 ? i18n("%1 (%2 of %3)", verb, j.index, j.count) : verb
                        }
                    }
                    QQC2.ProgressBar {
                        Layout.fillWidth: true
                        from: 0; to: 1
                        indeterminate: !(jobCard.job.progress >= 0)
                        value: jobCard.job.progress >= 0 ? jobCard.job.progress : 0
                    }
                    PlasmaComponents.Label {
                        opacity: 0.7
                        font.pointSize: Kirigami.Theme.smallFont.pointSize
                        visible: text !== ""
                        text: {
                            const j = jobCard.job
                            if (!j.active || !(j.progress >= 0)) return ""
                            let t = i18n("%1 of %2", root.formatSize(j.progress * j.bytesTotal), root.formatSize(j.bytesTotal))
                            if (j.queued > 0) t += " · " + i18np("%1 more waiting", "%1 more waiting", j.queued)
                            return t
                        }
                    }
                }
                QQC2.Button {
                    text: i18n("Cancel")
                    icon.name: "dialog-cancel"
                    enabled: !jobCard.job.cancelling
                    onClicked: Core.cancelFileJobs()
                }
            }
        }

        // ---- privacy indicator ------------------------------------------------
        Rectangle {
            z: 70
            visible: Core.privacy
            anchors {
                top: parent.top
                horizontalCenter: parent.horizontalCenter
                topMargin: Kirigami.Units.smallSpacing * 2
            }
            width: privacyRow.implicitWidth + Kirigami.Units.gridUnit
            height: privacyRow.implicitHeight + Kirigami.Units.smallSpacing * 2
            radius: height / 2
            color: Qt.rgba(0, 0, 0, 0.7)
            RowLayout {
                id: privacyRow
                anchors.centerIn: parent
                Kirigami.Icon {
                    source: "view-private"
                    Layout.preferredWidth: Kirigami.Units.iconSizes.small
                    Layout.preferredHeight: Kirigami.Units.iconSizes.small
                    color: "white"
                }
                PlasmaComponents.Label {
                    color: "white"
                    text: Core.screenShared && !Core.privacyManual ? i18n("Screen is shared: fences are hidden")
                                                                   : i18n("Privacy mode — click to turn off")
                }
            }
            MouseArea {
                anchors.fill: parent
                enabled: Core.privacyManual
                onClicked: Core.privacyManual = false
            }
        }

        // ---- short messages (with Undo) --------------------------------------
        Rectangle {
            id: toast
            z: 80
            anchors {
                horizontalCenter: parent.horizontalCenter
                bottom: parent.bottom
                bottomMargin: Kirigami.Units.gridUnit * 2
                              + (cleanupCard.visible ? cleanupCard.height + Kirigami.Units.gridUnit : 0)
                              + (jobCard.visible ? jobCard.height + Kirigami.Units.gridUnit : 0)
            }
            width: toastRow.implicitWidth + Kirigami.Units.gridUnit * 1.5
            height: toastRow.implicitHeight + Kirigami.Units.smallSpacing * 3
            radius: height / 2
            color: Qt.rgba(0, 0, 0, 0.75)
            opacity: 0
            visible: opacity > 0
            Behavior on opacity { NumberAnimation { duration: Kirigami.Units.longDuration } }
            property bool undoable: false
            function show(text, undoable) {
                toastLabel.text = text
                toast.undoable = undoable
                opacity = 1
                toastTimer.restart()
            }
            Timer {
                id: toastTimer
                interval: 4500
                onTriggered: toast.opacity = 0
            }
            HoverHandler { onHoveredChanged: if (hovered) toastTimer.stop(); else toastTimer.restart() }
            RowLayout {
                id: toastRow
                anchors.centerIn: parent
                spacing: Kirigami.Units.largeSpacing
                PlasmaComponents.Label {
                    id: toastLabel
                    color: "white"
                }
                PillButton {
                    visible: toast.undoable
                    text: i18n("Undo")
                    iconName: "edit-undo"
                    onClicked: {
                        toast.opacity = 0
                        root.doUndo()
                    }
                }
            }
        }
    }

    // white text button for dark or coloured pills, readable on any theme
    component PillButton: Rectangle {
        id: pill
        property string text
        property string iconName
        property string tooltip
        signal clicked()
        implicitWidth: pillRow.implicitWidth + Kirigami.Units.largeSpacing * 2
        implicitHeight: Math.max(pillLabel.implicitHeight, Kirigami.Units.iconSizes.small) + Kirigami.Units.smallSpacing * 2
        radius: height / 2
        color: pillMouse.pressed ? Qt.rgba(1, 1, 1, 0.3) : (pillMouse.containsMouse ? Qt.rgba(1, 1, 1, 0.18) : "transparent")
        RowLayout {
            id: pillRow
            anchors.centerIn: parent
            spacing: Kirigami.Units.smallSpacing
            Kirigami.Icon {
                visible: pill.iconName !== ""
                source: pill.iconName
                color: "white"
                isMask: true
                Layout.preferredWidth: Kirigami.Units.iconSizes.small
                Layout.preferredHeight: Kirigami.Units.iconSizes.small
            }
            PlasmaComponents.Label {
                id: pillLabel
                text: pill.text
                color: "white"
                font.bold: true
            }
        }
        MouseArea {
            id: pillMouse
            anchors.fill: parent
            hoverEnabled: true
            cursorShape: Qt.PointingHandCursor
            onClicked: pill.clicked()
            QQC2.ToolTip.text: pill.tooltip
            QQC2.ToolTip.visible: pill.tooltip !== "" && containsMouse
        }
    }

    // a rounded panel for messages on the desktop
    component Card: Rectangle {
        default property alias content: cardLayout.data
        height: cardLayout.implicitHeight + Kirigami.Units.gridUnit * 2
        z: 60
        radius: Kirigami.Units.smallSpacing * 3
        color: Qt.rgba(Kirigami.Theme.backgroundColor.r, Kirigami.Theme.backgroundColor.g,
                       Kirigami.Theme.backgroundColor.b, 0.94)
        border.width: 1
        border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.15)
        ColumnLayout {
            id: cardLayout
            anchors {
                left: parent.left
                right: parent.right
                verticalCenter: parent.verticalCenter
                margins: Kirigami.Units.gridUnit
            }
            spacing: Kirigami.Units.largeSpacing
        }
    }

    // ---- menus and dialogs -----------------------------------------------------
    QQC2.Menu {
        id: createMenu
        QQC2.MenuItem {
            text: i18n("Empty fence")
            icon.name: "list-add"
            onTriggered: root.renameFenceId = root.createFenceAt(chip.rect, i18n("New fence"), "", true)
        }
        QQC2.MenuSeparator {}
        Instantiator {
            model: [
                { key: "documents", text: i18n("Documents") },
                { key: "images", text: i18n("Photos") },
                { key: "media", text: i18n("Video and music") },
                { key: "archives", text: i18n("Archives") },
                { key: "apps", text: i18n("Programs") },
                { key: "folders", text: i18n("Folders") },
                { key: "screenshots", text: i18n("Screenshots") },
                { key: "today", text: i18n("Today") },
                { key: "old", text: i18n("Old files") }
            ]
            delegate: QQC2.MenuItem {
                required property var modelData
                text: modelData.text
                onTriggered: root.createFenceAt(chip.rect, modelData.text, Core.presetRules(modelData.key), true)
            }
            onObjectAdded: function (index, object) { createMenu.insertItem(index + 2, object) }
            onObjectRemoved: function (index, object) { createMenu.removeItem(object) }
        }
        QQC2.MenuSeparator {}
        QQC2.Menu {
            title: i18n("Folder portal")
            QQC2.MenuItem {
                text: i18n("Downloads")
                icon.name: "folder-download"
                onTriggered: root.createPortalAt(text, Core.downloadsFolder())
            }
            QQC2.MenuItem {
                text: i18n("From phone (KDE Connect)")
                icon.name: "smartphone"
                onTriggered: root.createPortalAt(i18n("From phone"), Core.phoneFolder())
            }
            QQC2.MenuItem {
                text: i18n("Screenshots")
                icon.name: "spectacle"
                onTriggered: root.createPortalAt(text, Core.screenshotFolder())
            }
            QQC2.MenuItem {
                text: i18n("Drives")
                icon.name: "drive-harddisk"
                onTriggered: root.createPortalAt(text, "drives:")
            }
            QQC2.MenuItem {
                text: i18n("Other folder…")
                icon.name: "folder-open"
                onTriggered: {
                    const id = root.createPortalAt(i18n("Folder"), Core.downloadsFolder())
                    root.openFenceSettings(id)
                }
            }
        }
    }
    function createPortalAt(title, path) {
        const r = chip.rect.width > 0 ? chip.rect : Qt.rect(desktop.width * 0.35, desktop.height * 0.3, desktop.width * 0.3, desktop.height * 0.35)
        chip.visible = false
        return Core.createPortal(screenId, "", page, r.x / desktop.width, r.y / desktop.height,
                                 r.width / desktop.width, r.height / desktop.height, title, path)
    }

    QQC2.Menu {
        id: dropMenu
        onClosed: Qt.callLater(function () { root.pendingDrop = null })
        QQC2.MenuItem {
            text: i18n("Move here")
            icon.name: "go-jump"
            onTriggered: root.finishDrop(false)
        }
        QQC2.MenuItem {
            text: i18n("Copy here")
            icon.name: "edit-copy"
            onTriggered: root.finishDrop(true)
        }
        QQC2.MenuSeparator {}
        QQC2.MenuItem {
            text: i18n("Cancel")
            icon.name: "dialog-cancel"
        }
    }

    FenceSettings {
        id: fenceSettings
        parent: area
    }
    CleanupDialog {
        id: cleanupDialog
        parent: area
        onArchived: function (folder, count) {
            root.showUndoToast(i18n("%1 files moved to %2", count, folder))
        }
    }
    AlveusSettings {
        id: settingsDialog
        parent: area
        desk: root
    }
}
