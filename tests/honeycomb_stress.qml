import QtQuick
import "../package/contents/ui/utils.js" as Utils

Item {
    id: root
    width: 900
    height: 600
    property bool prepared: false
    property string failure: ""

    ListModel { id: sourceModel }
    ListModel { id: windowModel }

    function makeRows(count) {
        const rows = []
        for (let i = 0; i < count; ++i) {
            rows.push({ path: "/item-" + i, name: "item-" + i })
        }
        return rows
    }

    // Match Fence.syncFiles(): synchronize the complete source model, then
    // schedule one viewport refresh after all ListModel mutations have landed.
    function syncFiles(rows) {
        Utils.syncModel(sourceModel, rows, "path")
        viewport.scheduleRefresh()
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
        contentHeight: Utils.honeycombHeight(sourceModel.count, width, cell)

        function windowRow(sourceIndex) {
            const row = sourceModel.get(sourceIndex)
            return { sourceIndex: sourceIndex, path: row.path, name: row.name }
        }

        function refresh(force) {
            const range = Utils.honeycombVisibleRange(sourceModel.count, width, cell,
                                                       contentY, height, bufferRows)
            if (!force && range.firstRow === firstRow && range.lastRow === lastRow) return
            windowModel.clear()
            firstSourceIndex = range.first
            firstRow = range.firstRow
            lastRow = range.lastRow
            for (let i = range.first; i < range.last; ++i) {
                windowModel.append(windowRow(i))
            }
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
            // Count can change thousands of times during one syncFiles call.
            // Coalescing prevents rebuilding the visible window for each row.
            function onCountChanged() { viewport.scheduleRefresh() }
        }

        Repeater {
            model: windowModel
            delegate: Item {
                required property int sourceIndex
                x: Utils.honeycombSpot(sourceIndex, viewport.width, viewport.cell).x
                y: Utils.honeycombSpot(sourceIndex, viewport.width, viewport.cell).y
                width: viewport.cell
                height: viewport.cell
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

            // Same-count metadata updates must refresh the visible copy.
            rows[0].name = "renamed-item-0"
            syncFiles(rows)
            Qt.callLater(function () {
                if (windowModel.get(0).name !== "renamed-item-0") {
                    fail("Same-count metadata update was not copied into the visible window")
                    return
                }

                // Reordering does not change count, but it must still refresh
                // the source-index mapping used by the virtualized delegates.
                rows.push(rows.shift())
                syncFiles(rows)
                Qt.callLater(function () {
                    if (sourceModel.count !== 5000 || windowModel.get(0).path !== "/item-1") {
                        fail("Same-count row reorder left stale Honeycomb delegates")
                        return
                    }

                    viewport.contentY = Math.floor(viewport.contentHeight / 2)
                    Qt.callLater(function () {
                        const middleRange = Utils.honeycombVisibleRange(
                            sourceModel.count, viewport.width, viewport.cell,
                            viewport.contentY, viewport.height, viewport.bufferRows)
                        if (!checkRange(middleRange, "Middle scroll")) return

                        viewport.contentY = Math.max(0, viewport.contentHeight - viewport.height)
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
                            syncFiles(rows)
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
    }
}
