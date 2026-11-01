/*
 * Tidy-up helper: files on the desktop nobody has touched for a long time,
 * moved (not deleted) into Documents/<archive>/<date>. Can be undone.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents
import ua.bezeza.alveus

import "utils.js" as Utils

QQC2.Popup {
    id: dialog

    signal archived(string folder, int count)

    modal: true
    focus: true
    padding: Kirigami.Units.gridUnit
    width: Math.min(parent ? parent.width - Kirigami.Units.gridUnit * 2 : 700, Kirigami.Units.gridUnit * 30)
    height: Math.min(parent ? parent.height - Kirigami.Units.gridUnit * 2 : 700, Kirigami.Units.gridUnit * 30)
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? (parent.height - height) / 3 : 0

    property int days: Core.setting("cleanupDays")
    property var files: []
    property var checked: ({})
    property int checkedCount: 0

    function reload() {
        files = Core.staleFiles(days)
        const c = {}
        files.forEach(function (f) { c[f.name] = true })
        checked = c
        checkedCount = files.length
    }
    onAboutToShow: reload()
    onDaysChanged: if (opened) reload()

    function toggle(name) {
        const c = Object.assign({}, checked)
        c[name] = !c[name]
        checked = c
        checkedCount = Object.keys(c).filter(function (k) { return c[k] }).length
    }

    background: Rectangle {
        radius: Kirigami.Units.smallSpacing * 3
        color: Kirigami.Theme.backgroundColor
        border.width: 1
        border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2)
    }

    contentItem: ColumnLayout {
        spacing: Kirigami.Units.largeSpacing

        RowLayout {
            Kirigami.Icon {
                source: "edit-clear-all"
                Layout.preferredWidth: Kirigami.Units.iconSizes.medium
                Layout.preferredHeight: Kirigami.Units.iconSizes.medium
            }
            PlasmaComponents.Label {
                text: i18n("Tidy up the desktop")
                font.bold: true
                font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.3
                Layout.fillWidth: true
            }
        }
        RowLayout {
            PlasmaComponents.Label { text: i18n("Not opened or changed for") }
            QQC2.SpinBox {
                from: 7
                to: 365
                stepSize: 7
                value: dialog.days
                onValueModified: {
                    dialog.days = value
                    Core.setSetting("cleanupDays", value)
                }
            }
            PlasmaComponents.Label { text: i18n("days") }
        }
        PlasmaComponents.Label {
            Layout.fillWidth: true
            wrapMode: Text.Wrap
            opacity: 0.75
            text: dialog.files.length > 0
                  ? i18n("Ticked files will be moved into the folder “%1” in Documents, sorted by today's date. Nothing is deleted, and you can undo it.", i18n("Desktop archive"))
                  : i18n("Nothing to tidy: every file on the desktop was used recently.")
        }

        ListView {
            id: list
            Layout.fillWidth: true
            Layout.fillHeight: true
            clip: true
            model: dialog.files
            QQC2.ScrollBar.vertical: QQC2.ScrollBar {}
            delegate: QQC2.ItemDelegate {
                required property var modelData
                width: list.width
                contentItem: RowLayout {
                    QQC2.CheckBox {
                        checked: dialog.checked[modelData.name] === true
                        onToggled: dialog.toggle(modelData.name)
                    }
                    Kirigami.Icon {
                        source: modelData.icon
                        fallback: modelData.fallbackIcon
                        Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
                        Layout.preferredHeight: Kirigami.Units.iconSizes.smallMedium
                    }
                    PlasmaComponents.Label {
                        text: modelData.label
                        elide: Text.ElideMiddle
                        Layout.fillWidth: true
                    }
                    PlasmaComponents.Label {
                        text: i18n("%1 days", modelData.days)
                        opacity: 0.6
                    }
                }
                onClicked: dialog.toggle(modelData.name)
            }
        }

        RowLayout {
            Layout.fillWidth: true
            QQC2.Button {
                text: i18n("Remind me in a week")
                onClicked: {
                    Core.snoozeCleanup(7)
                    dialog.close()
                }
            }
            Item { Layout.fillWidth: true }
            QQC2.Button {
                text: i18n("Close")
                onClicked: dialog.close()
            }
            QQC2.Button {
                text: i18n("Move %1 to the archive", dialog.checkedCount)
                icon.name: "archive-insert"
                highlighted: true
                enabled: dialog.checkedCount > 0
                onClicked: {
                    const names = dialog.files.map(function (f) { return f.name })
                                              .filter(function (n) { return dialog.checked[n] })
                    const folder = Core.archiveFiles(names, i18n("Desktop archive"))
                    dialog.close()
                    dialog.archived(folder, names.length)
                }
            }
        }
    }
}
