/*
 * Desktop widgets (clock, notes, …) on an Alveus desktop, using the same
 * layout manager as KDE's own desktop, so they behave exactly the same:
 * press and hold (or "Enter Edit Mode") to move and resize them.
 * Loaded on demand; the fences keep working even if this cannot load.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

import QtQuick
import org.kde.plasma.plasmoid
import org.kde.kirigami as Kirigami
import org.kde.plasma.private.containmentlayoutmanager as ContainmentLayoutManager

ContainmentLayoutManager.AppletsLayout {
    id: appletsLayout

    property Item containmentRoot
    property rect available
    property rect screenGeo

    relayoutLock: width !== available.width || height !== available.height
    configKey: "ItemGeometries-" + Math.round(screenGeo.width) + "x" + Math.round(screenGeo.height)
    fallbackConfigKey: available.width > available.height ? "ItemGeometriesHorizontal" : "ItemGeometriesVertical"

    Binding on containment {
        value: Plasmoid
        when: Plasmoid.isContainment
    }
    containmentItem: containmentRoot
    editModeCondition: Plasmoid.immutable
        ? ContainmentLayoutManager.AppletsLayout.Locked
        : ContainmentLayoutManager.AppletsLayout.AfterPressAndHold
    onEditModeChanged: Plasmoid.containment.corona.editMode = editMode

    Connections {
        target: Plasmoid.containment.corona
        function onEditModeChanged() {
            appletsLayout.editMode = Plasmoid.containment.corona.editMode
        }
    }

    minimumItemWidth: Kirigami.Units.gridUnit * 3
    minimumItemHeight: minimumItemWidth
    cellWidth: Kirigami.Units.iconSizes.small
    cellHeight: cellWidth
    defaultItemWidth: cellWidth * 6
    defaultItemHeight: cellHeight * 6

    appletContainerComponent: ContainmentLayoutManager.BasicAppletContainer {
        id: appletContainer
        editModeCondition: Plasmoid.immutable
            ? ContainmentLayoutManager.ItemContainer.Locked
            : ContainmentLayoutManager.ItemContainer.AfterPressAndHold
        configOverlaySource: "ConfigOverlay.qml"
        onAppletChanged: applet.visible = true
        Connections {
            target: appletsLayout
            function onEditModeChanged() {
                if (!Plasmoid.containment.corona.editMode) {
                    appletContainer.cancelEdit()
                }
            }
        }
    }

    placeHolder: ContainmentLayoutManager.PlaceHolder {}
}
