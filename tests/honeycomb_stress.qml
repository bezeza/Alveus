import QtQuick
import "../package/contents/ui/utils.js" as Utils

Item {
    id: root
    width: 900
    height: 600
    property bool prepared: false
    property string failure: ""

    ListModel {
        id: sourceModel
        Component.onCompleted: {
            for (let i = 0; i < 5000; ++i) append({ name: "item-" + i })
        }
    }
    ListModel { id: windowModel }

    Flickable {
        id: viewport
        anchors.fill: parent
        clip: true
        boundsBehavior: Flickable.StopAtBounds
        readonly property real cell: 80
        readonly property int bufferRows: 2
        property int firstRow: -1
        property int lastRow: -1
        contentHeight: Utils.honeycombHeight(sourceModel.count, width, cell)

        function refresh(force) {
            const range = Utils.honeycombVisibleRange(sourceModel.count, width, cell,
                                                       contentY, height, bufferRows)
            if (!force && range.firstRow === firstRow && range.lastRow === lastRow) return
            windowModel.clear()
            firstRow = range.firstRow
            lastRow = range.lastRow
            for (let i = range.first; i < range.last; ++i) windowModel.append({ sourceIndex: i })
        }
        onContentYChanged: refresh(false)
        onWidthChanged: refresh(true)
        onHeightChanged: refresh(true)
        Component.onCompleted: Qt.callLater(function() {
            refresh(true)
            const initialCount = windowModel.count
            if (initialCount <= 0 || initialCount >= sourceModel.count) {
                root.failure = "Initial delegate window is not bounded: " + initialCount
                root.prepared = true
                return
            }
            contentY = Math.max(0, contentHeight - height)
            Qt.callLater(function() {
                const range = Utils.honeycombVisibleRange(sourceModel.count, width, cell,
                                                           contentY, height, bufferRows)
                if (windowModel.count !== range.last - range.first
                    || windowModel.count >= sourceModel.count
                    || windowModel.get(0).sourceIndex !== range.first
                    || windowModel.get(windowModel.count - 1).sourceIndex !== range.last - 1) {
                    root.failure = "Scrolled delegate window does not match viewport range"
                } else if (Utils.honeycombSpot(4999, width, cell).y >= contentHeight) {
                    root.failure = "Last item lies outside the content height"
                }
                root.prepared = true
            })
        })
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
}
