/*
 * Alveus settings: general options, saved layouts (snapshots), pages,
 * help with the sorting rules, and about / support.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents
import ua.bezeza.alveus

import "links.js" as Links

QQC2.Popup {
    id: dialog

    property var desk
    property int tab: 0
    property var diagnosticEntries: []
    readonly property var driveGroups: [
        { key: "internal", label: i18n("Internal devices") },
        { key: "usb", label: i18n("USB devices") },
        { key: "external", label: i18n("Other removable devices") },
        { key: "network", label: i18n("Network storage") },
        { key: "virtual", label: i18n("Virtual devices") },
        { key: "optical", label: i18n("Optical drives") }
    ]
    readonly property var driveFilters: [
        { key: "hideEfi", label: i18n("Hide EFI System partitions") },
        { key: "hideMicrosoftReserved", label: i18n("Hide Microsoft Reserved partitions") },
        { key: "hideRecovery", label: i18n("Hide Windows Recovery partitions") },
        { key: "hideSwap", label: i18n("Hide Linux swap partitions") },
        { key: "hideBiosBoot", label: i18n("Hide BIOS boot partitions") }
    ]
    readonly property var effectiveDriveDefaults: {
        const revision = Core.revision
        return Core.driveDefaults()
    }

    modal: true
    focus: true
    padding: Kirigami.Units.gridUnit
    width: Math.min(parent ? parent.width - Kirigami.Units.gridUnit * 2 : 800, Kirigami.Units.gridUnit * 34)
    height: Math.min(parent ? parent.height - Kirigami.Units.gridUnit * 2 : 700, Kirigami.Units.gridUnit * 30)
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? (parent.height - height) / 3 : 0

    function openAt(t) {
        tab = t
        open()
    }

    function refreshDiagnostics() {
        diagnosticEntries = Core.recentDiagnostics(100)
    }

    onTabChanged: if (tab === 4) refreshDiagnostics()

    Connections {
        target: Core
        function onDiagnosticsChanged() {
            if (dialog.tab === 4) dialog.refreshDiagnostics()
        }
    }

    Timer {
        interval: 2000
        repeat: true
        running: dialog.visible && dialog.tab === 4
        onTriggered: dialog.refreshDiagnostics()
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
                source: "alveus"
                Layout.preferredWidth: Kirigami.Units.iconSizes.medium
                Layout.preferredHeight: Kirigami.Units.iconSizes.medium
            }
            PlasmaComponents.Label {
                text: "Alveus"
                font.bold: true
                font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.4
            }
            PlasmaComponents.Label {
                text: Core.version
                opacity: 0.6
                Layout.fillWidth: true
            }
            QQC2.ToolButton {
                icon.name: "window-close"
                onClicked: dialog.close()
            }
        }

        QQC2.TabBar {
            id: bar
            Layout.fillWidth: true
            currentIndex: dialog.tab
            onCurrentIndexChanged: dialog.tab = currentIndex
            QQC2.TabButton { text: i18n("General") }
            QQC2.TabButton { text: i18n("Layouts") }
            QQC2.TabButton { text: i18n("Rules help") }
            QQC2.TabButton { text: i18n("About") }
            QQC2.TabButton { text: i18n("Diagnostics") }
            QQC2.TabButton { text: i18n("Drives") }
        }

        StackLayout {
            Layout.fillWidth: true
            Layout.fillHeight: true
            currentIndex: bar.currentIndex

            // ---- general
            QQC2.ScrollView {
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: Kirigami.Units.largeSpacing

                    RowLayout {
                        PlasmaComponents.Label { text: i18n("Icon size") }
                        QQC2.Slider {
                            id: sizeSlider
                            from: 24
                            to: 128
                            stepSize: 8
                            value: Core.setting("iconSize")
                            Layout.fillWidth: true
                            onMoved: Core.setSetting("iconSize", value)
                        }
                        PlasmaComponents.Label { text: Math.round(sizeSlider.value) }
                    }
                    QQC2.CheckBox {
                        text: i18n("Open files with a single click")
                        checked: Core.setting("singleClick")
                        onToggled: Core.setSetting("singleClick", checked)
                    }
                    QQC2.CheckBox {
                        text: i18n("Hide what's in fences while the screen is shared (Zoom, Meet, OBS…)")
                        checked: Core.setting("autoPrivacy")
                        onToggled: Core.setSetting("autoPrivacy", checked)
                    }
                    QQC2.CheckBox {
                        text: i18n("Privacy mode now")
                        checked: Core.privacyManual
                        onToggled: Core.privacyManual = checked
                    }
                    QQC2.CheckBox {
                        text: i18n("Remind me to tidy up old files")
                        checked: Core.setting("cleanupReminder")
                        onToggled: Core.setSetting("cleanupReminder", checked)
                    }
                    Kirigami.Separator { Layout.fillWidth: true }
                    PlasmaComponents.Label { text: i18n("Pages of this desktop"); font.bold: true }
                    RowLayout {
                        PlasmaComponents.Label {
                            text: dialog.desk ? i18n("%1 page(s). Scroll the mouse wheel over the empty desktop to switch.", dialog.desk.pageCount) : ""
                            Layout.fillWidth: true
                            wrapMode: Text.Wrap
                        }
                        QQC2.Button {
                            text: i18n("Add page")
                            icon.name: "list-add"
                            enabled: dialog.desk && dialog.desk.pageCount < 9
                            onClicked: dialog.desk.addPage()
                        }
                    }
                    Kirigami.Separator { Layout.fillWidth: true }
                    QQC2.Button {
                        text: i18n("Tidy up old files…")
                        icon.name: "edit-clear-all"
                        onClicked: {
                            dialog.close()
                            dialog.desk.openCleanup()
                        }
                    }
                }
            }

            // ---- layouts (snapshots)
            ColumnLayout {
                spacing: Kirigami.Units.largeSpacing
                PlasmaComponents.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    opacity: 0.75
                    text: i18n("Save how your fences are arranged and bring it back later — for example one layout for work and one for home.")
                }
                RowLayout {
                    QQC2.TextField {
                        id: snapName
                        Layout.fillWidth: true
                        placeholderText: i18n("Name of the layout")
                    }
                    QQC2.Button {
                        text: i18n("Save current")
                        icon.name: "document-save"
                        enabled: snapName.text.trim() !== ""
                        onClicked: {
                            Core.saveSnapshot(snapName.text.trim())
                            snapName.text = ""
                        }
                    }
                }
                ListView {
                    id: snaps
                    Layout.fillWidth: true
                    Layout.fillHeight: true
                    clip: true
                    model: { Core.revision; return Core.snapshots() }
                    delegate: QQC2.ItemDelegate {
                        required property var modelData
                        width: snaps.width
                        contentItem: RowLayout {
                            Kirigami.Icon {
                                source: "view-grid"
                                Layout.preferredWidth: Kirigami.Units.iconSizes.smallMedium
                                Layout.preferredHeight: Kirigami.Units.iconSizes.smallMedium
                            }
                            ColumnLayout {
                                Layout.fillWidth: true
                                spacing: 0
                                PlasmaComponents.Label { text: modelData.name; font.bold: true }
                                PlasmaComponents.Label {
                                    text: i18n("%1 fences · %2", modelData.fences, Qt.formatDateTime(modelData.created, Qt.DefaultLocaleShortDate))
                                    opacity: 0.6
                                    font: Kirigami.Theme.smallFont
                                }
                            }
                            QQC2.Button {
                                text: i18n("Restore")
                                icon.name: "document-revert"
                                onClicked: {
                                    Core.restoreSnapshot(modelData.id)
                                    dialog.desk.showUndoToast(i18n("Layout “%1” restored", modelData.name))
                                }
                            }
                            QQC2.ToolButton {
                                icon.name: "edit-delete"
                                onClicked: Core.deleteSnapshot(modelData.id)
                            }
                        }
                    }
                    PlasmaComponents.Label {
                        anchors.centerIn: parent
                        visible: snaps.count === 0
                        text: i18n("No saved layouts yet")
                        opacity: 0.6
                    }
                }
            }

            // ---- rules help
            QQC2.ScrollView {
                contentWidth: availableWidth
                PlasmaComponents.Label {
                    width: parent.width
                    wrapMode: Text.Wrap
                    textFormat: Text.StyledText
                    text: i18n("<h3>Sorting rules</h3><p>Each fence has a short list of rules. New files on the desktop go to the first fence they fit. Fences with time rules (Today, Old) are checked first.</p><p><b>pdf, docx</b> — file types; <b>tar.gz</b> works too<br><b>folder</b> — folders<br><b>name:*invoice*</b> — by name; <b>*</b> and <b>?</b> are wildcards, otherwise the name only has to contain the text; use quotes for spaces: <b>\"name:my report*\"</b><br><b>from:telegram</b> — downloaded from a web address containing the text<br><b>newer:7</b> — changed in the last 7 days (<b>12h</b> = hours, <b>2w</b> = weeks)<br><b>older:60</b> — not changed for 60 days<br><b>larger:100M</b>, <b>smaller:1M</b> — by size (K, M, G)</p><p>A file fits when it matches <i>any</i> type, name or site rule and <i>all</i> time and size rules. With only time or size rules, any file can fit.</p><p>Files you put somewhere by hand stay there; rules only decide for the rest.</p>")
                }
            }

            // ---- about
            QQC2.ScrollView {
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: Kirigami.Units.largeSpacing
                    PlasmaComponents.Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: i18n("Alveus is Latin for “beehive”: every fence is a cell, every file a bee. Free software under the GPL. This is an early beta — thank you for trying it!")
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing
                        QQC2.Button {
                            visible: Links.website !== ""
                            text: i18n("Website")
                            icon.name: "internet-services"
                            onClicked: Core.openUrl(Links.website)
                        }
                        QQC2.Button {
                            visible: Links.issues !== ""
                            text: i18n("Report a problem")
                            icon.name: "tools-report-bug"
                            onClicked: Core.openUrl(Links.issues)
                        }
                    }
                    Kirigami.Separator { Layout.fillWidth: true }
                    PlasmaComponents.Label {
                        text: i18n("Support the project")
                        font.bold: true
                    }
                    PlasmaComponents.Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        opacity: 0.75
                        text: Links.kofi === "" && Links.monobank === ""
                              ? i18n("Donation links will appear here soon.")
                              : i18n("Alveus is free. If it saves you time, you can buy the author a coffee.")
                    }
                    Flow {
                        Layout.fillWidth: true
                        spacing: Kirigami.Units.smallSpacing
                        QQC2.Button {
                            visible: Links.kofi !== ""
                            text: "Ko-fi"
                            icon.name: "emblem-favorite"
                            onClicked: Core.openUrl(Links.kofi)
                        }
                        QQC2.Button {
                            visible: Links.monobank !== ""
                            text: i18n("monobank jar")
                            icon.name: "emblem-favorite"
                            onClicked: Core.openUrl(Links.monobank)
                        }
                    }
                }
            }

            // ---- diagnostics
            QQC2.ScrollView {
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: Kirigami.Units.largeSpacing

                    PlasmaComponents.Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        text: i18n("Warnings and errors are recorded automatically. Recent entries appear below.")
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        QQC2.Button {
                            text: i18n("Open log folder")
                            icon.name: "folder-open"
                            onClicked: Core.openDiagnosticsFolder()
                        }
                        QQC2.Button {
                            text: i18n("Copy log")
                            icon.name: "edit-copy"
                            onClicked: Core.copyDiagnostics()
                        }
                        QQC2.Button {
                            text: i18n("Refresh")
                            icon.name: "view-refresh"
                            onClicked: dialog.refreshDiagnostics()
                        }
                    }
                    PlasmaComponents.Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        opacity: 0.6
                        font: Kirigami.Theme.smallFont
                        text: Core.diagnosticsPath
                    }
                    ListView {
                        id: diagnosticList
                        Layout.fillWidth: true
                        Layout.fillHeight: true
                        clip: true
                        spacing: Kirigami.Units.smallSpacing
                        model: dialog.diagnosticEntries

                        delegate: ColumnLayout {
                            required property string time
                            required property string level
                            required property string category
                            required property string message
                            width: diagnosticList.width
                            spacing: Kirigami.Units.smallSpacing

                            RowLayout {
                                Layout.fillWidth: true
                                PlasmaComponents.Label {
                                    text: time
                                    opacity: 0.6
                                    font: Kirigami.Theme.smallFont
                                }
                                PlasmaComponents.Label {
                                    text: level.toUpperCase()
                                    font.bold: true
                                }
                                PlasmaComponents.Label {
                                    text: category
                                    opacity: 0.6
                                    Layout.fillWidth: true
                                    elide: Text.ElideRight
                                }
                            }
                            PlasmaComponents.Label {
                                Layout.fillWidth: true
                                wrapMode: Text.Wrap
                                text: message
                            }
                            Kirigami.Separator { Layout.fillWidth: true }
                        }
                    }
                    PlasmaComponents.Label {
                        Layout.fillWidth: true
                        visible: diagnosticList.count === 0
                        horizontalAlignment: Text.AlignHCenter
                        opacity: 0.6
                        text: i18n("No warnings or errors have been recorded.")
                    }
                    PlasmaComponents.Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        opacity: 0.6
                        font: Kirigami.Theme.smallFont
                        text: i18n("The log rotates automatically. File names and paths can appear in third-party error messages, so review the log before sharing it.")
                    }
                }

            // ---- drive visibility defaults
            QQC2.ScrollView {
                contentWidth: availableWidth
                ColumnLayout {
                    width: parent.width
                    spacing: Kirigami.Units.largeSpacing

                    PlasmaComponents.Label {
                        text: i18n("Drive visibility defaults")
                        font.bold: true
                        font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.1
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        PlasmaComponents.Label {
                            Layout.fillWidth: true
                            text: i18n("Drive card size")
                        }
                        QQC2.ComboBox {
                            id: globalDriveCardSize
                            model: [i18n("Compact"), i18n("Medium"), i18n("Large")]
                            currentIndex: Math.max(0, ["compact", "medium", "large"].indexOf(dialog.effectiveDriveDefaults.displayMode))
                            onActivated: Core.setDriveDefault("displayMode", ["compact", "medium", "large"][currentIndex])
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        PlasmaComponents.Label {
                            Layout.fillWidth: true
                            text: i18n("USB device grouping")
                        }
                        QQC2.ComboBox {
                            id: globalUsbGrouping
                            model: [i18n("Separate group"), i18n("Together with other drives")]
                            currentIndex: dialog.effectiveDriveDefaults.separateUsb === false ? 1 : 0
                            onActivated: Core.setDriveDefault("separateUsb", currentIndex === 0)
                        }
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        PlasmaComponents.Label {
                            Layout.fillWidth: true
                            text: i18n("Device information")
                        }
                        QQC2.ComboBox {
                            id: globalDriveInformation
                            model: [i18n("Detailed"), i18n("Basic")]
                            currentIndex: dialog.effectiveDriveDefaults.informationLevel === "basic" ? 1 : 0
                            onActivated: Core.setDriveDefault("informationLevel", currentIndex === 0 ? "detailed" : "basic")
                        }
                    }
                    PlasmaComponents.Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        opacity: 0.7
                        text: i18n("These defaults apply to every drive fence unless an individual fence overrides them.")
                    }

                    PlasmaComponents.Label { text: i18n("Visibility groups"); font.bold: true }
                    Repeater {
                        model: dialog.driveGroups
                        delegate: QQC2.CheckBox {
                            required property var modelData
                            text: modelData.label
                            checked: dialog.effectiveDriveDefaults.groups[modelData.key] !== false
                            onToggled: Core.setDriveDefault("groups." + modelData.key, checked)
                        }
                    }

                    Kirigami.Separator { Layout.fillWidth: true }
                    PlasmaComponents.Label { text: i18n("Automatic service-partition filters"); font.bold: true }
                    Repeater {
                        model: dialog.driveFilters
                        delegate: QQC2.CheckBox {
                            required property var modelData
                            text: modelData.label
                            checked: dialog.effectiveDriveDefaults[modelData.key] !== false
                            onToggled: Core.setDriveDefault(modelData.key, checked)
                        }
                    }

                    Kirigami.Separator { Layout.fillWidth: true }
                    RowLayout {
                        Layout.fillWidth: true
                        PlasmaComponents.Label {
                            Layout.fillWidth: true
                            text: i18n("Partition visibility")
                        }
                        QQC2.ComboBox {
                            id: globalPartitionModeBox
                            model: [i18n("All partitions"), i18n("Mounted only"), i18n("Manually selected")]
                            currentIndex: Math.max(0, ["all", "mounted", "selected"].indexOf(dialog.effectiveDriveDefaults.partitionMode))
                            onActivated: Core.setDriveDefault("partitionMode", ["all", "mounted", "selected"][currentIndex])
                        }
                    }
                    QQC2.CheckBox {
                        text: i18n("Show unmounted volumes")
                        checked: dialog.effectiveDriveDefaults.showUnmounted !== false
                        onToggled: Core.setDriveDefault("showUnmounted", checked)
                    }
                    RowLayout {
                        Layout.fillWidth: true
                        PlasmaComponents.Label {
                            Layout.fillWidth: true
                            text: i18n("Minimum partition size (MiB)")
                        }
                        QQC2.SpinBox {
                            id: globalMinimumPartitionSizeBox
                            from: 0
                            to: 1024
                            stepSize: 16
                            value: dialog.effectiveDriveDefaults.minimumPartitionSizeMiB || 0
                            onValueModified: Core.setDriveDefault("minimumPartitionSizeMiB", value)
                        }
                    }
                    PlasmaComponents.Label {
                        Layout.fillWidth: true
                        wrapMode: Text.Wrap
                        opacity: 0.65
                        text: i18n("0 disables the size filter")
                    }
                    Kirigami.Separator { Layout.fillWidth: true }
                    QQC2.Button {
                        text: i18n("Reset global defaults")
                        icon.name: "edit-undo"
                        onClicked: Core.resetDriveDefaults()
                    }
                }
            }
            }
        }
    }
}
