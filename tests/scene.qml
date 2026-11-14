// Test scene: a wallpaper and the Alveus desktop on top of it.
// The step name (last command-line argument) prepares the state.
import QtQuick
import QtQuick.Controls as QQC2
import org.kde.plasma.plasmoid
import ua.bezeza.alveus

Item {
    id: scene
    width: 1600
    height: 900

    readonly property string step: Qt.application.arguments.length > 6 ? Qt.application.arguments[6] : "welcome"
    property bool prepared: false
    property string blankFenceId: ""

    // The Loader-created desktop component inherits this scene context.
    function i18n(s, a, b, c) {
        let out = s
        const args = [a, b, c]
        for (let i = 0; i < args.length; ++i) {
            if (args[i] !== undefined && args[i] !== null) {
                out = out.replace("%" + (i + 1), String(args[i]))
            }
        }
        return out
    }
    function i18nc(context, s, a, b) { return i18n(s, a, b) }
    function i18np(one, many, n, b) { return i18n(n === 1 ? one : many, n, b) }

    Rectangle {
        id: wall
        anchors.fill: parent
        gradient: Gradient {
            GradientStop { position: 0.0; color: "#1d4e6e" }
            GradientStop { position: 0.55; color: "#3b5f8a" }
            GradientStop { position: 1.0; color: "#4a3b6b" }
        }
    }
    Rectangle { // pretend panel
        anchors { left: parent.left; right: parent.right; bottom: parent.bottom }
        height: 44
        color: "#1b1e20"
    }

    function fenceId(title) {
        const all = Core.fences(0, "", 0).concat(Core.fences(0, "", 1))
        for (const f of all) if (f.title === title) return f.id
        return ""
    }

    Loader {
        id: desk
        anchors.fill: parent
        source: Qt.resolvedUrl("../package/contents/ui/main.qml")
        onLoaded: Qt.callLater(scene.prepare)
    }

    function prepare() {
        const d = desk.item
        const s = scene.step
        if (s !== "welcome" && s !== "draw" && s !== "select") d.createDefaults()
        if (s === "draw" || s === "select" || s === "chip") Core.dismissWelcome()
        if (s === "rich") {
            Core.updateFence(fenceId("Photos"), { "view": "honeycomb", "color": "#27ae60", "opacity": 0.5 })
            Core.updateFence(fenceId("Documents"), { "view": "list" })
            Core.updateFence(fenceId("Archives"), { "collapsed": true })
            Core.mergeFences(fenceId("Programs"), fenceId("Video and music"))
            Core.createPortal(0, "", 0, 0.30, 0.05, 0.36, 0.36, "Downloads", Qt.application.arguments[7] || "/tmp")
            Core.updateFence(fenceId("Downloads"), { "color": "#f67400", "opacity": 0.45 })
            Core.setPageCount(0, "", 2)
        }
        if (s === "settings") d.openFenceSettings(fenceId("Documents"))
        if (s === "alveus") d.openSettings(0)
        if (s === "help") d.openSettings(2)
        if (s === "cleanup") d.openCleanup()
        if (s === "privacy") Core.privacyManual = true
        if (s === "kdemenu") kdeMenu.visible = true
        if (s === "tidy") Core.setSetting("cleanupDays", 30)
        if (s === "job") { // a slow copy to another disk (see run-ui: slowdst)
            Core.snoozeCleanup(7)
            Core.importToFolder(["file:///tmp/alveus-big/Відпустка 2026.mkv", "file:///tmp/alveus-big/Фото.zip"], true, "/dev/shm/slowdst")
        }
        if (s === "dropcopy") { // like finishDrop(): copy onto the desktop at a point
            Core.snoozeCleanup(7)
            const paths = Core.importToDesktop(["file:///tmp/alveus-big/Відпустка 2026.mkv"], true, "")
            Qt.callLater(function () { d.placeAt(paths.map(d.desktopName).filter(function (n) { return n !== "" }), Qt.point(600, 420)) })
        }
        if (s === "drives") {
            Core.snoozeCleanup(7)
            Core.createPortal(0, "", 0, 0.30, 0.05, 0.34, 0.30, "Диски", "drives:")
        }
        if (s === "blanktitle") {
            blankFenceId = Core.createFence(0, "", 0, 0.30, 0.05, 0.34, 0.30, "", "")
        }
        scene.prepared = true
    }

    // what the KDE right-click menu would get from Alveus
    Rectangle {
        id: kdeMenu
        visible: false
        x: 620; y: 180
        width: 300
        height: col.implicitHeight + 16
        color: "#fcfcfc"
        border.color: "#999"
        radius: 4
        Column {
            id: col
            x: 8; y: 8
            Text { text: "KDE desktop menu — items from Alveus:"; font.bold: true; font.pixelSize: 12 }
            Repeater {
                model: Plasmoid.contextualActions
                delegate: Text {
                    required property var modelData
                    text: modelData.isSeparator ? "────────────" : ("• " + modelData.text + (modelData.enabled ? "" : "  (disabled)"))
                    font.pixelSize: 13
                    color: modelData.enabled ? "#232629" : "#999"
                }
            }
        }
    }
}
