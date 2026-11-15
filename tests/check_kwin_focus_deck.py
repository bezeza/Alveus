#!/usr/bin/env python3
"""Structural checks for the standalone KWin Focus Deck package."""

import json
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
PACKAGE = ROOT / "extras" / "kwin-focus-deck"

metadata_path = PACKAGE / "metadata.json"
main_qml_path = PACKAGE / "contents" / "ui" / "main.qml"
card_qml_path = PACKAGE / "contents" / "ui" / "WindowCard.qml"
hint_qml_path = PACKAGE / "contents" / "ui" / "ShortcutHint.qml"

for path in (metadata_path, main_qml_path, card_qml_path, hint_qml_path):
    if not path.is_file():
        raise SystemExit(f"Missing Focus Deck package file: {path.relative_to(ROOT)}")

metadata = json.loads(metadata_path.read_text(encoding="utf-8"))
plugin = metadata.get("KPlugin", {})

assert metadata.get("KPackageStructure") == "KWin/WindowSwitcher"
assert plugin.get("Id") == "alveus-focus-deck"
assert plugin.get("Name") == "Alveus Focus Deck"
assert plugin.get("Version") == "0.1.0"

main_qml = main_qml_path.read_text(encoding="utf-8")
card_qml = card_qml_path.read_text(encoding="utf-8")
hint_qml = hint_qml_path.read_text(encoding="utf-8")

for token in (
    "KWin.TabBoxSwitcher",
    "PlasmaCore.Window",
    "PathView",
    "GridView",
    "tabBox.model",
    "tabBox.currentIndex",
    "tabBox.allDesktops",
):
    assert token in main_qml, f"Expected main.qml to contain {token!r}"

for token in (
    "KWin.WindowThumbnail",
    "root.caption",
    "root.iconName",
    "root.desktopName",
    "root.minimized",
):
    assert token in card_qml, f"Expected WindowCard.qml to contain {token!r}"

for token in (
    "caption: model.caption",
    "iconName: model.icon",
    "desktopName: model.desktopName",
    "minimized: model.minimized",
    "windowId: model.windowId",
    "selected: gridCell.GridView.isCurrentItem",
):
    assert token in main_qml, f"Expected main.qml to bind {token!r}"

assert "signal activated" in card_qml
assert "required property string keyText" in hint_qml

# Keep this package a visual KWin layout: no second KWin script or external
# process/DBus integration is allowed in the first version.
for forbidden in ("KWin/Script", "callDBus(", "Qt.createQmlObject", "QProcess", "workspace."):
    assert forbidden not in main_qml + card_qml, f"Unexpected extra logic: {forbidden}"

print("Alveus Focus Deck package structure: OK")
