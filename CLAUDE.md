# Alveus — notes for Claude Code

> Before any task, read and follow the repository-root `AGENTS.md`. It is the canonical policy for all agents; these notes add Claude-specific project context and must not override it.

Desktop fences for KDE Plasma 6 (like Stardock Fences). The author uses it on
**Fedora with KDE Plasma 6** and speaks Ukrainian — answer in Ukrainian.
Status: 0.03.1 beta. Use the current QtTest/CTest output as the source of truth; never assume or hard-code a test count. Run the Qt 6.4+ test suite before claiming a pass. **This machine is the first real Plasma it runs on**:
expect rough edges and fix them.

## Layout

- `core/` — C++ QML module `ua.bezeza.alveus` (Qt only, no KDE libs), singleton `Core`
  - `alveuscore.*` — fences, placements, pages, activities, monitors, snapshots,
    undo, clipboard, file ops, settings; state in `~/.local/share/alveus/layout.json`
  - `rules.*` — sorting rule language (`pdf`, `folder`, `name:`, `from:`, `newer:`, `older:`, `larger:`, `smaller:`)
  - `thumbnails.*` — freedesktop thumbnail cache + ffmpegthumbnailer/ffmpeg/pdftoppm
  - `fileops.*` — copy/move on a worker thread (cp -a, data-only fallback for regular files; symlinks must be preserved or the operation fails,
    special files are rejected; temp copy + rename, cancel, progress); AlveusCore queues jobs (`fileJob` property)
  - `drives.*` — "Drives" fence (QStorageInfo + lsblk, udisksctl mount/unmount)
  - `integrations.*` — KDE Connect, Spectacle folder, terminal, screen-share detection (pw-dump)
  - `runner.*` — KRunner D-Bus runner (`ua.bezeza.alveus` `/runner`)
- `package/` — the Plasma containment (desktop layout) `ua.bezeza.alveus`
  - `main.qml` (ContainmentItem: selection, rubber band, loose icons, menus, pages),
    `Fence.qml`, `FileIcon.qml`, `DrivesView.qml`, `FenceSettings.qml`,
    `AlveusSettings.qml`, `CleanupDialog.qml`, `Backdrop.qml` (blur, QtQuick.Effects),
    `WidgetsHost.qml` (KDE's private `containmentlayoutmanager`), `WidgetDrop.qml`
    (`org.kde.draganddrop`), `ConfigOverlay.qml`/`ActionButton.qml` copied from plasma-desktop
  - Optional parts are in Loaders so a missing module never breaks the desktop
- `po/uk.po` — Ukrainian translation (domain `plasma_applet_ua.bezeza.alveus`)
- `tests/` — `coretest` (use the actual QtTest/CTest output for the current test count; FAT test needs ALVEUS_TEST_FAT_DIR), `uiharness` + `scene.qml` + mocks for offscreen renders
- `packaging/alveus.spec`, `packaging/build-rpm.sh`, `.copr/Makefile`
- `install.sh`, `uninstall.sh`, `recover.sh` (switch desktops back to the standard one)

## Build, install, test

```sh
./packaging/build-rpm.sh && sudo dnf install ~/rpmbuild/RPMS/x86_64/alveus-0.03.1-*.rpm
# or quicker while iterating:
cmake -S . -B build -DALVEUS_BUILD_TESTS=ON
cmake --build build --parallel
dbus-run-session -- ctest --test-dir build --output-on-failure
# Install only with explicit user approval: sudo cmake --install build
systemctl --user restart plasma-plasmashell   # reload after installing
journalctl --user -b -o cat | grep -iE "alveus|qml|ua\.bezeza" | tail -80
```

QML-only changes: edit `package/…`, `sudo cmake --install build`, restart plasmashell.
Turn on: right-click desktop → Configure Desktop and Wallpaper → Layout: Alveus.
If the desktop breaks: `./recover.sh` (nothing is lost).

## First things to check on real Plasma (not verifiable offscreen)

1. Desktop loads, wallpaper visible, right-click shows KDE's menu with Alveus items
2. `WidgetsHost.qml`: widgets can be added, moved (press-and-hold / edit mode)
3. `Backdrop.qml`: frosted glass lines up with the wallpaper under each fence
4. Drag files from/to Dolphin; drag between fences; dropping onto the desktop places icons
5. Keyboard focus on the desktop (Delete, F2, Ctrl+Z)
6. `Plasmoid.screen` / `availableScreenRect` with panels; two monitors
7. Drives fence shows the author's NTFS disks with free space; mount/unmount
8. KRunner (Alt+Space) finds fences

## Planned next (asked by the author, after testing)

- Animations setting: off / simple / fancy (magic-lamp roll-up, Evanesco effect,
  fence "lift" while moving, springy drop, smooth reflow on resize)
- Snapping and guides for moving/resizing fences; size hint "4 × 3 icons"
- Fence views: table (sortable columns), row/strip, tiles, gallery, compact; sort and group
- Backups of `layout.json` (keep last few), lock fence, arrow-key navigation

## Agent workflow

Follow `AGENTS.md` for every task. Every implementation PR must update
`docs/DEVELOPMENT_LOG.md`; CI enforces this. Do not merge, install, or restart
Plasma without explicit user approval.

## Style

Keep the code readable and commented in plain English; UI strings via `i18n()`
in English with Ukrainian in `po/uk.po`. Run the focused core test while
iterating and the full CTest suite before handoff.
