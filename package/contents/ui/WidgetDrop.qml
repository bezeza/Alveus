/*
 * Lets widgets be dragged from "Add Widgets" onto an Alveus desktop,
 * the same way KDE's own desktop does it. Sits underneath the icons and
 * fences, so file drops still go to them. Loaded on demand.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

import QtQuick
import org.kde.draganddrop as DragDrop

DragDrop.DropArea {
    id: drop

    property Item containmentRoot

    preventStealing: false

    onDrop: event => {
        const formats = event.mimeData.formats
        if (formats.indexOf("text/x-plasmoidservicename") === -1
            && formats.indexOf("text/x-plasmoidinstanceid") === -1) {
            return
        }
        const p = drop.mapToItem(containmentRoot, event.x, event.y)
        containmentRoot.processMimeData(event.mimeData, p.x, p.y)
        event.accept(event.proposedAction)
    }
}
