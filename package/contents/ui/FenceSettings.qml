/*
 * Settings of one fence: name, sorting rules, folder (portal), look.
 * SPDX-License-Identifier: GPL-2.0-or-later
 */

import QtQuick
import QtQuick.Controls as QQC2
import QtQuick.Dialogs
import QtQuick.Layouts
import org.kde.kirigami as Kirigami
import org.kde.plasma.components as PlasmaComponents
import ua.bezeza.alveus

QQC2.Popup {
    id: dialog

    property string fid
    property var f: ({})

    modal: true
    focus: true
    padding: Kirigami.Units.gridUnit
    width: Math.min(parent ? parent.width - Kirigami.Units.gridUnit * 2 : 700, Kirigami.Units.gridUnit * 32)
    height: Math.min(parent ? parent.height - Kirigami.Units.gridUnit * 2 : 700, contentItem.implicitHeight + padding * 2)
    x: parent ? (parent.width - width) / 2 : 0
    y: parent ? Math.max(Kirigami.Units.gridUnit, (parent.height - height) / 3) : 0

    readonly property var swatches: ["", "#3daee9", "#1d99f3", "#27ae60", "#f67400", "#da4453", "#9b59b6", "#232629", "#eff0f1"]

    function openFor(id) {
        fid = id
        f = Core.fence(id)
        titleField.text = f.title
        rulesField.text = f.rules
        drivesSwitch.checked = f.portal === "drives:"
        portalSwitch.checked = f.portal !== "" && !drivesSwitch.checked
        portalField.text = portalSwitch.checked ? f.portal : Core.downloadsFolder()
        chosenColor = f.color
        opacitySlider.value = Math.round(f.opacity * 100)
        blurBox.checked = f.blur
        viewBox.currentIndex = ["grid", "list", "honeycomb"].indexOf(f.view)
        sizeBox.value = f.iconSize
        open()
        titleField.forceActiveFocus()
    }
    property string chosenColor: ""

    function addPreset(key) {
        const add = Core.presetRules(key)
        rulesField.text = rulesField.text.trim() === "" ? add : rulesField.text.trim() + ", " + add
    }

    function save() {
        Core.updateFence(fid, {
            "title": titleField.text.trim(),
            "rules": portalSwitch.checked || drivesSwitch.checked ? "" : rulesField.text,
            "portal": drivesSwitch.checked ? "drives:" : (portalSwitch.checked ? portalField.text.trim() : ""),
            "color": chosenColor,
            "opacity": opacitySlider.value / 100,
            "blur": blurBox.checked,
            "view": ["grid", "list", "honeycomb"][Math.max(0, viewBox.currentIndex)],
            "iconSize": sizeBox.value
        })
        close()
    }

    background: Rectangle {
        radius: Kirigami.Units.smallSpacing * 3
        color: Kirigami.Theme.backgroundColor
        border.width: 1
        border.color: Qt.rgba(Kirigami.Theme.textColor.r, Kirigami.Theme.textColor.g, Kirigami.Theme.textColor.b, 0.2)
    }

    contentItem: QQC2.ScrollView {
        contentWidth: availableWidth
        ColumnLayout {
            width: parent.width
            spacing: Kirigami.Units.largeSpacing

            PlasmaComponents.Label {
                text: i18n("Fence settings")
                font.bold: true
                font.pointSize: Kirigami.Theme.defaultFont.pointSize * 1.3
            }

            // ---- name
            PlasmaComponents.Label { text: i18n("Name"); font.bold: true }
            QQC2.TextField {
                id: titleField
                Layout.fillWidth: true
                onAccepted: dialog.save()
            }

            // ---- what goes in
            PlasmaComponents.Label { text: i18n("What goes into this fence"); font.bold: true }
            QQC2.RadioButton {
                id: rulesSwitch
                text: i18n("Files from the desktop, sorted by rules")
                checked: !portalSwitch.checked && !drivesSwitch.checked
                onToggled: if (checked) { portalSwitch.checked = false; drivesSwitch.checked = false }
            }
            QQC2.RadioButton {
                id: portalSwitch
                text: i18n("Everything in a folder (a portal)")
                onToggled: if (checked) drivesSwitch.checked = false
            }
            QQC2.RadioButton {
                id: drivesSwitch
                text: i18n("Drives and free space")
                onToggled: if (checked) portalSwitch.checked = false
            }

            ColumnLayout {
                visible: !portalSwitch.checked && !drivesSwitch.checked
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.gridUnit
                spacing: Kirigami.Units.smallSpacing

                QQC2.TextField {
                    id: rulesField
                    Layout.fillWidth: true
                    placeholderText: i18n("for example: pdf, docx, name:*invoice*, newer:7")
                    onAccepted: dialog.save()
                }
                PlasmaComponents.Label {
                    readonly property var errors: Core.ruleErrors(rulesField.text)
                    visible: errors.length > 0
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    color: Kirigami.Theme.negativeTextColor
                    text: i18n("Not understood: %1", errors.join(", "))
                }
                Flow {
                    Layout.fillWidth: true
                    spacing: Kirigami.Units.smallSpacing
                    Repeater {
                        model: [
                            { key: "documents", text: i18n("Documents") },
                            { key: "images", text: i18n("Photos") },
                            { key: "media", text: i18n("Video and music") },
                            { key: "archives", text: i18n("Archives") },
                            { key: "apps", text: i18n("Programs") },
                            { key: "folders", text: i18n("Folders") },
                            { key: "screenshots", text: i18n("Screenshots") },
                            { key: "telegram", text: i18n("From Telegram") },
                            { key: "today", text: i18n("Today") },
                            { key: "week", text: i18n("This week") },
                            { key: "old", text: i18n("Not touched for 60 days") },
                            { key: "big", text: i18n("Bigger than 100 MB") }
                        ]
                        delegate: QQC2.Button {
                            required property var modelData
                            text: modelData.text
                            flat: true
                            icon.name: "list-add"
                            onClicked: dialog.addPreset(modelData.key)
                        }
                    }
                }
                PlasmaComponents.Label {
                    Layout.fillWidth: true
                    wrapMode: Text.Wrap
                    opacity: 0.75
                    textFormat: Text.StyledText
                    text: i18n("<b>pdf, docx</b> — file types · <b>folder</b> — folders · <b>name:*invoice*</b> — by name · <b>from:telegram</b> — downloaded from a site · <b>newer:7</b> / <b>older:60</b> — changed within / more than N days ago (12h for hours) · <b>larger:100M</b> / <b>smaller:1M</b> — by size.<br>A file fits if it matches any type, name or site, and all time and size conditions. Files you moved by hand stay where you put them.")
                }
            }

            RowLayout {
                visible: portalSwitch.checked
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.gridUnit
                QQC2.TextField {
                    id: portalField
                    Layout.fillWidth: true
                }
                QQC2.Button {
                    icon.name: "document-open-folder"
                    text: i18n("Choose…")
                    onClicked: folderDialog.open()
                }
            }
            Flow {
                visible: portalSwitch.checked
                Layout.fillWidth: true
                Layout.leftMargin: Kirigami.Units.gridUnit
                spacing: Kirigami.Units.smallSpacing
                QQC2.Button {
                    flat: true
                    text: i18n("Downloads")
                    icon.name: "folder-download"
                    onClicked: portalField.text = Core.downloadsFolder()
                }
                QQC2.Button {
                    flat: true
                    text: i18n("From phone (KDE Connect)")
                    icon.name: "smartphone"
                    onClicked: portalField.text = Core.phoneFolder()
                }
                QQC2.Button {
                    flat: true
                    text: i18n("Screenshots")
                    icon.name: "spectacle"
                    onClicked: portalField.text = Core.screenshotFolder()
                }
            }

            // ---- look
            PlasmaComponents.Label { text: i18n("Look"); font.bold: true }
            RowLayout {
                spacing: Kirigami.Units.smallSpacing
                Repeater {
                    model: dialog.swatches
                    delegate: Rectangle {
                        required property string modelData
                        width: Kirigami.Units.gridUnit * 1.6
                        height: width
                        radius: width / 2
                        color: modelData !== "" ? modelData : Kirigami.Theme.backgroundColor
                        border.width: dialog.chosenColor === modelData ? 3 : 1
                        border.color: dialog.chosenColor === modelData ? Kirigami.Theme.highlightColor
                                                                       : Qt.rgba(0.5, 0.5, 0.5, 0.6)
                        PlasmaComponents.Label {
                            anchors.centerIn: parent
                            visible: modelData === ""
                            text: "A"
                            font: Kirigami.Theme.smallFont
                        }
                        MouseArea {
                            anchors.fill: parent
                            onClicked: dialog.chosenColor = modelData
                        }
                        QQC2.ToolTip.text: modelData === "" ? i18n("Theme colour") : modelData
                        QQC2.ToolTip.visible: false
                    }
                }
            }
            RowLayout {
                PlasmaComponents.Label { text: i18n("Opacity") }
                QQC2.Slider {
                    id: opacitySlider
                    from: 0
                    to: 100
                    stepSize: 5
                    Layout.fillWidth: true
                }
                PlasmaComponents.Label { text: Math.round(opacitySlider.value) + "%" }
            }
            QQC2.CheckBox {
                id: blurBox
                text: i18n("Blur the wallpaper behind (frosted glass)")
            }
            RowLayout {
                PlasmaComponents.Label { text: i18n("View") }
                QQC2.ComboBox {
                    id: viewBox
                    model: [i18n("Icons"), i18n("List"), i18n("Honeycomb")]
                }
                Item { width: Kirigami.Units.gridUnit }
                PlasmaComponents.Label { text: i18n("Icon size") }
                QQC2.SpinBox {
                    id: sizeBox
                    from: 0
                    to: 128
                    stepSize: 8
                    textFromValue: function (v) { return v === 0 ? i18n("Default") : v }
                    valueFromText: function (t) { return parseInt(t) || 0 }
                }
            }

            RowLayout {
                Layout.alignment: Qt.AlignRight
                Layout.topMargin: Kirigami.Units.largeSpacing
                QQC2.Button {
                    text: i18n("Cancel")
                    onClicked: dialog.close()
                }
                QQC2.Button {
                    text: i18n("Save")
                    icon.name: "dialog-ok"
                    highlighted: true
                    onClicked: dialog.save()
                }
            }
        }
    }

    FolderDialog {
        id: folderDialog
        title: i18n("Folder for this fence")
        onAccepted: portalField.text = decodeURIComponent(selectedFolder.toString().replace(/^file:\/\//, ""))
    }
}
