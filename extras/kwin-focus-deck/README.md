# Alveus Focus Deck

A custom **KWin WindowSwitcher** layout for KDE Plasma 6. It replaces only the visual
layout shown by the native Alt+Tab switcher.

## Design

- A centered, emphasized window preview with smaller neighboring previews.
- A compact horizontal Focus Deck for 1–6 windows.
- An adaptive thumbnail grid for 7 or more windows.
- The application icon and window/document title on every card.
- The KWin-provided desktop name on each card, so windows can be associated with
  their virtual desktop when the selected KWin switcher includes all desktops.
- A restrained graphite surface, thin teal focus border, and short scale transition.
- Native KWin window thumbnails through `KWin.WindowThumbnail`.

PDF viewers, LibreOffice Writer/Calc/Impress, Telegram, Viber, Murglar, browsers,
and other applications are treated equally. The theme does not inspect file types
or classify applications.

## Scope and non-goals

This is a **theme package**, not an application, app launcher, dock, KWin script,
or desktop manager.

- It does not launch or pin Murglar, Telegram, Viber, Dolphin, or any other app.
  Pin launchers to the normal KDE panel.
- It does not add custom categories, app filters, search, or automation.
- It does not create or switch virtual desktops. KWin keeps control of desktop
  navigation and the window list.
- It does not show a miniature of the entire desktop. It shows the desktop name
  supplied by KWin for each window.
- It does not change KWin configuration during installation.

## Requirements

- KDE Plasma 6 with KWin.
- The KWin QML API providing `KWin.TabBoxSwitcher` and
  `KWin.WindowThumbnail`.
- `kpackagetool6` for installation.

## Install

From the repository root:

```sh
bash extras/kwin-focus-deck/install.sh
```

Then open **System Settings → Window Management → Task Switcher** and select
**Alveus Focus Deck** for the main task switcher. Keep the alternative switcher
unchanged unless you also want to use this layout there.

The installer only installs the KPackage. It does not change the active layout,
shortcuts, or any other KWin setting.

## Update

Run the install command again. It upgrades the package when the package ID is
already installed.

## Uninstall

First select another task-switcher layout in System Settings, then run:

```sh
bash extras/kwin-focus-deck/uninstall.sh
```

## Keyboard and mouse

- Alt+Tab / Shift+Alt+Tab: native KWin next/previous window actions.
- Left / Right: move through the horizontal Focus Deck.
- In the grid: arrow keys move through the grid.
- Escape: cancel using KWin's native action.
- Click a card: activate that window.

KWin still owns the window list and activation lifecycle.

## Performance notes

The package creates its overlay only while the switcher is visible. It uses
KWin's compositor-rendered window thumbnails and avoids shader effects, backdrop
blur, polling, timers, external processes, and background scripts.

## Known limitations

- The desktop badge is the window's KWin `desktopName` role. It is not a live
  overview of every virtual desktop.
- Thumbnail appearance depends on KWin compositing. KWin can fall back to an
  icon if compositing is unavailable.
- The first version has not yet been validated in a live Fedora KDE session.
  CI/static package checks are not a substitute for testing Alt+Tab on the
  actual desktop.

## Development

The package lives in `extras/kwin-focus-deck/` so it stays independent from the
Alveus desktop-fence plasmoid. The QML entry point is
`contents/ui/main.qml`.

References:

- [KDE: Create a custom Window Switcher](https://develop.kde.org/docs/plasma/windowswitcher/)
- [KWin source: built-in thumbnail grid](https://github.com/KDE/kwin/tree/master/src/tabbox/switchers/thumbnail_grid)
