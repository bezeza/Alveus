import QtQuick
import "../package/contents/ui/utils.js" as Utils

Item {
    id: root
    width: 900
    height: 600
    property bool prepared: false
    property string failure: ""
    property string dragPath: ""
    property var selection: ({})
    property int selectionRevision: 0
    property int delegateSequence: 0

    ListModel { id: sourceModel }
    ListModel { id: windowModel }

    function makeRows(count) {
        const rows = []
        for (let i = 0; i < count; ++i) {
            rows.push({ path: "/item-" + i, name: "item-" + i })
        }
        return rows
    }

    // Match Fence.syncFiles(): synchronize the source model, then refresh the
    // copied viewport window once after all ListModel mutations have landed.
    function syncFiles(rows) {
        const changed = Utils.syncModel(sourceModel, rows, "path")
        if (changed) viewport.scheduleRefresh()
        return changed
    }

    function sourceIndexForPath(path) {
        if (path === "") return -1
        for (let i = 0; i < sourceModel.count; ++i) {
            if (sourceModel.get(i).path === path) return i
        }
        return -1
    }

    function delegateForPath(path) {
        for (let i = 0; i < delegates.count; ++i) {
            const item = delegates.itemAt(i)
            if (item && item.path === path) return item
        }
        return null
    }

    function windowIndexForPath(path) {
        for (let i = 0; i < windowModel.count; ++i) {
            if (windowModel.get(i).path === path) return i
        }
        return -1
    }

    function selectPath(path) {
        selection[path] = true
        ++selectionRevision
    }

    function fail(message) {
        failure = message
        prepared = true
    }

    function checkRange(range, label) {
        if (windowModel.count !== range.last - range.first
                || windowModel.count >= sourceModel.count
                || windowModel.count <= 0
                || windowModel.get(0).sourceIndex !== range.first
                || windowModel.get(windowModel.count - 1).sourceIndex !== range.last - 1) {
            fail(label + ": delegate window does not match viewport range")
            return false
        }
        return true
    }

    Flickable {
        id: viewport
        anchors.fill: parent
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        readonly property real cell: 80
        readonly property int bufferRows: 2
        property int firstRow: -1
        property int lastRow: -1
        property int firstSourceIndex: 0
        property int refreshCount: 0
        property bool refreshPending: false
        property bool hasDragSource: false
        contentHeight: Utils.honeycombHeight(sourceModel.count, width, cell)

        function windowRow(sourceIndex) {
            const row = sourceModel.get(sourceIndex)
            return { sourceIndex: sourceIndex, path: row.path, name: row.name }
        }

        function refresh(force) {
            const dragIndex = root.sourceIndexForPath(root.dragPath)
            const rows = []
            const range = Utils.honeycombVisibleRange(sourceModel.count, width, cell,
                                                       contentY, height, bufferRows)
            if (!force && range.firstRow === firstRow && range.lastRow === lastRow) return

            firstSourceIndex = range.first
            firstRow = range.firstRow
            lastRow = range.lastRow
            for (let i = range.first; i < range.last; ++i) {
                rows.push(windowRow(i))
            }
            // Keep the active drag's delegate alive if it leaves the viewport.
            if (dragIndex >= 0 && (dragIndex < range.first || dragIndex >= range.last)) {
                rows.push(windowRow(dragIndex))
            }
            Utils.syncModel(windowModel, rows, "path")
            hasDragSource = dragIndex >= 0
            ++refreshCount
        }

        function scheduleRefresh() {
            if (refreshPending) return
            refreshPending = true
            Qt.callLater(function () {
                viewport.refreshPending = false
                viewport.refresh(true)
            })
        }

        onContentYChanged: refresh(false)
        onWidthChanged: refresh(true)
        onHeightChanged: refresh(true)
        Component.onCompleted: refresh(true)

        Connections {
            target: sourceModel
            function onCountChanged() { viewport.scheduleRefresh() }
        }
        Connections {
            target: root
            function onDragPathChanged() { viewport.refresh(true) }
        }

        Repeater {
            id: delegates
            model: windowModel
            delegate: Item {
                required property int sourceIndex
                required property string path
                required property string name
                property int instanceId: -1
                readonly property bool selected: root.selectionRevision >= 0
                                                  && root.selection[path] === true

                x: Utils.honeycombSpot(sourceIndex, viewport.width, viewport.cell).x
                y: Utils.honeycombSpot(sourceIndex, viewport.width, viewport.cell).y
                width: viewport.cell
                height: viewport.cell

                Component.onCompleted: instanceId = ++root.delegateSequence
            }
        }
    }

    Component.onCompleted: {
        const rows = makeRows(5000)
        viewport.refreshCount = 0
        syncFiles(rows)

        Qt.callLater(function () {
            if (viewport.refreshCount > 2) {
                fail("Initial 5,000-row sync rebuilt the window too many times: "
                     + viewport.refreshCount)
                return
            }
            if (sourceModel.count !== 5000 || windowModel.count <= 0
                    || windowModel.count >= sourceModel.count
                    || windowModel.get(0).sourceIndex !== 0) {
                fail("Initial delegate window is not bounded at the first row")
                return
            }

            // An unchanged model must not recreate the visible delegates.
            const unchangedRefreshCount = viewport.refreshCount
            if (syncFiles(rows)) {
                fail("Unchanged source data was reported as a model update")
                return
            }
            Qt.callLater(function () {
                if (viewport.refreshCount !== unchangedRefreshCount) {
                    fail("Unchanged source data unnecessarily rebuilt the delegate window")
                    return
                }

                // Same-count metadata updates must refresh the visible copy.
                rows[0].name = "renamed-item-0"
                if (!syncFiles(rows)) {
                    fail("Same-count metadata update was not detected")
                    return
                }
                Qt.callLater(function () {
                    if (windowModel.get(0).name !== "renamed-item-0") {
                        fail("Same-count metadata update was not copied into the visible window")
                        return
                    }

                    // Reordering does not change count, but it must still refresh
                    // the source-index mapping used by the virtualized delegates.
                    const firstRow = rows[0]
                    rows[0] = rows[1]
                    rows[1] = firstRow
                    if (!syncFiles(rows)) {
                        fail("Same-count row reorder was not detected")
                        return
                    }
                    Qt.callLater(function () {
                        if (sourceModel.count !== 5000
                                || windowModel.get(0).path !== "/item-1"
                                || windowModel.get(1).path !== "/item-0") {
                            fail("Same-count row reorder left stale Honeycomb delegates")
                            return
                        }

                        // A selected item may leave the virtual window and be
                        // recreated later; selection is keyed by path, not delegate.
                        root.selectPath("/item-1")
                        const selectedDelegate = delegateForPath("/item-1")
                        if (!selectedDelegate || !selectedDelegate.selected) {
                            fail("Visible file did not reflect its selection state")
                            return
                        }
                        const selectedInstance = selectedDelegate.instanceId
                        viewport.contentY = Math.floor(viewport.contentHeight / 2)
                        Qt.callLater(function () {
                            if (delegateForPath("/item-1") !== null) {
                                fail("Scrolled-out selected file still has a delegate")
                                return
                            }
                            viewport.contentY = 0
                            Qt.callLater(function () {
                                const restoredSelection = delegateForPath("/item-1")
                                if (!restoredSelection || !restoredSelection.selected
                                        || restoredSelection.instanceId === selectedInstance) {
                                    fail("Selection was not restored when a virtualized delegate was recreated")
                                    return
                                }

                                // A drag source must survive a far scroll. Its
                                // delegate instance must not be destroyed/recreated.
                                root.dragPath = "/item-0"
                                Qt.callLater(function () {
                                    const dragDelegate = delegateForPath("/item-0")
                                    if (!dragDelegate) {
                                        fail("Active drag source was not present before scrolling")
                                        return
                                    }
                                    const dragInstance = dragDelegate.instanceId
                                    viewport.contentY = Math.floor(viewport.contentHeight / 2)
                                    Qt.callLater(function () {
                                        const pinnedDelegate = delegateForPath("/item-0")
                                        const middleRange = Utils.honeycombVisibleRange(
                                            sourceModel.count, viewport.width, viewport.cell,
                                            viewport.contentY, viewport.height, viewport.bufferRows)
                                        if (!pinnedDelegate || pinnedDelegate.instanceId !== dragInstance
                                                || pinnedDelegate.sourceIndex !== 1
                                                || windowIndexForPath("/item-0") < 0
                                                || windowModel.count > middleRange.last - middleRange.first + 1) {
                                            fail("Active drag source delegate was destroyed or lost during scrolling")
                                            return
                                        }

                                        // Reordering while dragging must update the
                                        // pinned source index without replacing it.
                                        const draggedRow = rows[1]
                                        rows[1] = rows[2]
                                        rows[2] = draggedRow
                                        if (!syncFiles(rows)) {
                                            fail("Reorder during active drag was not detected")
                                            return
                                        }
                                        Qt.callLater(function () {
                                            const movedDragDelegate = delegateForPath("/item-0")
                                            if (!movedDragDelegate
                                                    || movedDragDelegate.instanceId !== dragInstance
                                                    || movedDragDelegate.sourceIndex !== 2) {
                                                fail("Active drag source was not preserved across same-count reorder")
                                                return
                                            }

                                            root.dragPath = ""
                                            Qt.callLater(function () {
                                                if (windowIndexForPath("/item-0") !== -1) {
                                                    fail("Completed drag source remained pinned outside the viewport")
                                                    return
                                                }

                                                viewport.contentY = Math.max(
                                                    0, viewport.contentHeight - viewport.height)
                                                Qt.callLater(function () {
                                                    const bottomRange = Utils.honeycombVisibleRange(
                                                        sourceModel.count, viewport.width, viewport.cell,
                                                        viewport.contentY, viewport.height, viewport.bufferRows)
                                                    if (!checkRange(bottomRange, "Bottom scroll")) return
                                                    if (Utils.honeycombSpot(4999, viewport.width, viewport.cell).y
                                                            >= viewport.contentHeight) {
                                                        fail("Last item lies outside the content height")
                                                        return
                                                    }

                                                    // Update a visible item at the far end without
                                                    // changing the item count.
                                                    const bottomSourceIndex = bottomRange.last - 1
                                                    rows[bottomSourceIndex].name = "updated-bottom-item"
                                                    if (!syncFiles(rows)) {
                                                        fail("Bottom-row metadata update was not detected")
                                                        return
                                                    }
                                                    Qt.callLater(function () {
                                                        const visibleIndex = bottomSourceIndex - viewport.firstSourceIndex
                                                        if (visibleIndex < 0 || visibleIndex >= windowModel.count
                                                                || windowModel.get(visibleIndex).name !== "updated-bottom-item") {
                                                            fail("Bottom-row metadata update was not copied into the visible window")
                                                            return
                                                        }
                                                        root.prepared = true
                                                    })
                                                })
                                            })
                                        })
                                    })
                                })
                            })
                        })
                    })
                })
            })
        })
    }
}
