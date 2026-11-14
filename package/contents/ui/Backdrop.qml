/*
 * Frosted glass behind a fence: the part of the wallpaper under the fence,
 * blurred, with rounded corners. Loaded on demand. Requires Qt 6.5+ and
 * the QtQuick.Effects module (MultiEffect).
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

import QtQuick
import QtQuick.Effects

Item {
    id: backdrop

    property Item sourceItem
    property Item target
    property real radius: 8

    // where the fence is, in wallpaper coordinates
    function area() {
        if (!sourceItem || !target) {
            return Qt.rect(0, 0, 1, 1)
        }
        // depend on the fence geometry so the rectangle follows moves and resizes
        void (target.x + target.y + target.width + target.height)
        const p = target.mapToItem(sourceItem, 0, 0)
        return Qt.rect(p.x, p.y, Math.max(1, target.width), Math.max(1, target.height))
    }

    ShaderEffectSource {
        id: snapshot
        anchors.fill: parent
        sourceItem: backdrop.sourceItem
        sourceRect: backdrop.area()
        live: true
        hideSource: false
        visible: false
    }

    Rectangle {
        id: mask
        anchors.fill: parent
        radius: backdrop.radius
        visible: false
        layer.enabled: true
    }

    MultiEffect {
        anchors.fill: parent
        source: snapshot
        blurEnabled: true
        blur: 1.0
        blurMax: 48
        saturation: 0.15
        maskEnabled: true
        maskSource: mask
    }
}
