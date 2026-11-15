import QtQuick
import QtQuick.Layouts
import org.kde.plasma.core as PlasmaCore
import org.kde.kwin as KWin

KWin.TabBoxSwitcher {
    id: tabBox

    Instantiator {
        active: tabBox.visible

        delegate: PlasmaCore.Window {
            visible: true
            flags: Qt.ToolTip | Qt.X11BypassWindowManagerHint
            color: "transparent"

            width: deck.width
            height: deck.height
            x: tabBox.screenGeometry.x + (tabBox.screenGeometry.width - width) / 2
            y: tabBox.screenGeometry.y + (tabBox.screenGeometry.height - height) / 2

            Item {
                id: deck

                anchors.centerIn: parent
                width: Math.min(tabBox.screenGeometry.width * 0.94, 1540)
                height: Math.min(tabBox.screenGeometry.height * 0.78, countProbe.count > 6 ? 690 : 520)
                focus: true

                Rectangle {
                    anchors.fill: parent
                    radius: 20
                    color: "#ED111A22"
                    border.width: 1
                    border.color: "#52616C"
                }

                Instantiator {
                    id: countProbe

                    active: tabBox.visible
                    model: tabBox.model
                    delegate: QtObject {}
                }

                ColumnLayout {
                    anchors.fill: parent
                    anchors.margins: 22
                    spacing: 14

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 34
                        spacing: 10

                        Text {
                            text: "ALVEUS"
                            color: "#F4F7F8"
                            font.pixelSize: 19
                            font.weight: Font.DemiBold
                            font.letterSpacing: 1.2
                        }

                        Rectangle {
                            Layout.preferredWidth: 2
                            Layout.preferredHeight: 21
                            radius: 1
                            color: "#20CDBD"
                        }

                        Text {
                            text: "FOCUS DECK"
                            color: "#AFC0C8"
                            font.pixelSize: 12
                            font.letterSpacing: 1.1
                        }

                        Item {
                            Layout.fillWidth: true
                        }

                        Rectangle {
                            Layout.preferredWidth: desktopLabel.implicitWidth + 22
                            Layout.preferredHeight: 28
                            radius: 8
                            color: "#293640"
                            border.width: 1
                            border.color: "#43535E"

                            Text {
                                id: desktopLabel
                                anchors.centerIn: parent
                                text: tabBox.allDesktops
                                      ? i18ndc("kwin", "@info:status switcher desktop scope", "ALL DESKTOPS")
                                      : i18ndc("kwin", "@info:status switcher desktop scope", "CURRENT DESKTOP")
                                color: "#C3D0D5"
                                font.pixelSize: 9
                                font.weight: Font.DemiBold
                                font.letterSpacing: 0.7
                            }
                        }
                    }

                    Item {
                        id: views

                        Layout.fillWidth: true
                        Layout.fillHeight: true

                        Loader {
                            id: layoutLoader

                            anchors.fill: parent
                            active: tabBox.visible
                            sourceComponent: countProbe.count > 6 ? gridComponent : carouselComponent
                        }

                        Text {
                            anchors.centerIn: parent
                            visible: countProbe.count === 0
                            text: i18ndc("kwin", "@info:placeholder no entries in the task switcher", "No open windows")
                            color: "#B5C2C8"
                            font.pixelSize: 14
                        }
                    }

                    RowLayout {
                        Layout.fillWidth: true
                        Layout.preferredHeight: 28
                        spacing: 18

                        ShortcutHint {
                            keyText: "Tab"
                            description: i18ndc("kwin", "@info:task switcher shortcut", "Next")
                        }

                        ShortcutHint {
                            keyText: "Shift + Tab"
                            description: i18ndc("kwin", "@info:task switcher shortcut", "Previous")
                        }

                        Item {
                            Layout.fillWidth: true
                        }

                        ShortcutHint {
                            keyText: "Esc"
                            description: i18ndc("kwin", "@info:task switcher shortcut", "Cancel")
                        }
                    }
                }

                Component {
                    id: carouselComponent

                    PathView {
                        id: carouselView

                        anchors.fill: parent
                        model: tabBox.model
                        currentIndex: tabBox.currentIndex
                        pathItemCount: Math.max(1, Math.min(count, 6))
                        preferredHighlightBegin: 0.5
                        preferredHighlightEnd: 0.5
                        highlightRangeMode: PathView.StrictlyEnforceRange
                        interactive: false
                        clip: false

                        readonly property real cardWidth: Math.min(
                            270,
                            Math.max(148, width / Math.max(1, Math.min(count, 6)) * 0.94)
                        )
                        readonly property real cardHeight: Math.min(365, height * 0.88)

                        path: Path {
                            startX: carouselView.cardWidth / 2
                            startY: carouselView.height / 2

                            PathAttribute {
                                name: "deckScale"
                                value: 0.84
                            }

                            PathLine {
                                x: carouselView.width / 2
                                y: carouselView.height / 2
                            }

                            PathAttribute {
                                name: "deckScale"
                                value: 1.0
                            }

                            PathLine {
                                x: carouselView.width - carouselView.cardWidth / 2
                                y: carouselView.height / 2
                            }

                            PathAttribute {
                                name: "deckScale"
                                value: 0.84
                            }
                        }

                        delegate: WindowCard {
                            width: carouselView.cardWidth
                            height: carouselView.cardHeight
                            scale: PathView.deckScale
                            z: PathView.isCurrentItem ? 2 : 1

                            windowIndex: index
                            windowId: model.windowId
                            caption: model.caption
                            iconName: model.icon
                            desktopName: model.desktopName
                            minimized: model.minimized
                            selected: PathView.isCurrentItem

                            onActivated: (targetIndex) => tabBox.model.activate(targetIndex)

                            Behavior on scale {
                                NumberAnimation {
                                    duration: 110
                                    easing.type: Easing.OutCubic
                                }
                            }
                        }

                        onCurrentIndexChanged: {
                            if (tabBox.currentIndex !== currentIndex) {
                                tabBox.currentIndex = currentIndex;
                            }
                        }
                    }
                }

                Component {
                    id: gridComponent

                    GridView {
                        id: gridView

                        anchors.fill: parent
                        model: tabBox.model
                        currentIndex: tabBox.currentIndex
                        clip: true
                        keyNavigationWraps: true
                        cellWidth: Math.max(1, width / Math.max(1, Math.min(5, Math.floor(width / 250))))
                        cellHeight: 176
                        cacheBuffer: 0

                        delegate: Item {
                            id: gridCell
                            width: gridView.cellWidth
                            height: gridView.cellHeight

                            WindowCard {
                                anchors.fill: parent
                                anchors.margins: 6

                                windowIndex: index
                                windowId: model.windowId
                                caption: model.caption
                                iconName: model.icon
                                desktopName: model.desktopName
                                minimized: model.minimized
                                selected: gridCell.GridView.isCurrentItem

                                onActivated: (targetIndex) => tabBox.model.activate(targetIndex)
                            }
                        }

                        onCurrentIndexChanged: {
                            if (tabBox.currentIndex !== currentIndex) {
                                tabBox.currentIndex = currentIndex;
                            }
                            if (currentIndex >= 0) {
                                positionViewAtIndex(currentIndex, GridView.Contain);
                            }
                        }

                        Component.onCompleted: {
                            if (currentIndex >= 0) {
                                positionViewAtIndex(currentIndex, GridView.Contain);
                            }
                        }
                    }
                }

                Keys.onPressed: (event) => {
                    const itemCount = countProbe.count;
                    if (itemCount <= 0) {
                        return;
                    }

                    let nextIndex = tabBox.currentIndex;

                    if (itemCount > 6) {
                        const grid = layoutLoader.item;
                        if (!grid) {
                            return;
                        }

                        const columns = Math.max(1, Math.floor(grid.width / grid.cellWidth));
                        if (event.key === Qt.Key_Left) {
                            nextIndex -= 1;
                        } else if (event.key === Qt.Key_Right) {
                            nextIndex += 1;
                        } else if (event.key === Qt.Key_Up) {
                            nextIndex -= columns;
                        } else if (event.key === Qt.Key_Down) {
                            nextIndex += columns;
                        } else {
                            return;
                        }

                        tabBox.currentIndex = Math.max(0, Math.min(itemCount - 1, nextIndex));
                    } else if (event.key === Qt.Key_Left || event.key === Qt.Key_Right) {
                        const direction = event.key === Qt.Key_Left ? -1 : 1;
                        tabBox.currentIndex = (Math.max(0, tabBox.currentIndex) + direction + itemCount) % itemCount;
                    } else {
                        return;
                    }

                    event.accepted = true;
                }
            }

            onSceneGraphError: () => {
                // Keep the switcher window alive if the graphics scene is reset.
            }
        }
    }
}
