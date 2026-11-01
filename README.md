# Alveus

**Desktop fences for KDE Plasma 6.** Drag a box on the desktop to make a fence;
new files sort themselves into fences by type, name, download site or date.
Your files never move: they stay in the Desktop folder, fences only group them.
Inspired by Stardock Fences on Windows.

*Alveus* is Latin for "beehive": every fence is a cell, every file a bee.

**Status: 0.03.1 beta.**

This beta focuses on reliability. File operations use GNU coreutils (`cp`, `mv`),
which are standard on Linux desktops. Copies are staged and preserve metadata and
symlinks when supported; if a destination cannot preserve a symlink, the operation
fails without deleting the source. Special files (FIFOs, sockets, device nodes)
are rejected safely; layout and snapshot schemas are validated before loading; failed Undo operations can be retried;
fence geometry and appearance are undoable; file changes trigger rescans; and
thumbnail generation retries after the source file changes.

[Українською ↓](#українською)

## What it does

**Your desktop, but tidy**
- Drag a box on the empty desktop: the icons inside are selected and a
  *Make a fence* button appears — the fence takes those files with it
- Ready-made fences: Documents, Photos, Video and music, Archives, Programs,
  Folders, Screenshots, Today, Old files
- Icons outside fences can be placed anywhere; *Arrange Icons* puts them back in columns
- Widgets (clock, notes, …) work as on the standard KDE desktop
- Create folder / text file, paste, open terminal — added to the usual right-click menu
- Keyboard: Delete, F2, Enter, Ctrl+A, Ctrl+C / X / V, Ctrl+Z, Ctrl+PgUp/PgDn
- Undo for almost everything (Ctrl+Z or the *Undo* button in the message)

**Fences**
- Sorting rules: `pdf, docx`, `folder`, `name:*invoice*`, `from:telegram`,
  `newer:7`, `older:60`, `larger:100M` — see *Alveus Settings → Rules help*
- Things you put somewhere by hand stay there; rules decide only for the rest
- Roll up with a double-click on the title (*Reducio*), hover to peek
- Tabs: drop a fence onto another fence's title
- Folder portals: a fence that shows any folder — Downloads, KDE Connect's
  "from phone" folder, Screenshots — and lets you browse into subfolders
- Drives fence: every mounted disk (NTFS, USB sticks, network shares) with a
  bar and free space; partitions that are not mounted yet can be mounted with
  a double-click; drop files on a disk to copy or move them there
- Colour, opacity, frosted-glass blur of the wallpaper, icons / list / honeycomb view
- Several desktop pages (mouse wheel over the empty desktop)
- Per-activity fences, several monitors (a monitor unplugged → its fences
  come to the main one and go back when it returns)

**Extras**
- Double-click the empty desktop to hide everything (*Evanesco*)
- Privacy mode: fence contents are hidden while your screen is shared
  (automatic detection is optional and experimental)
- Tidy-up helper: files not used for N days → moved (not deleted) to
  *Documents/Desktop archive/<date>*, with undo
- Saved layouts: one for work, one for home
- Thumbnails for videos and PDFs (shared cache with Dolphin)
- Send to phone with KDE Connect
- KRunner (Alt+Space) finds fences and desktop files

## Install

### Fedora (RPM)

```sh
./packaging/build-rpm.sh
sudo dnf install ~/rpmbuild/RPMS/x86_64/alveus-0.03.1-*.rpm
```

or from COPR once it is published (see *Publishing* below):

```sh
sudo dnf copr enable bezeza/alveus
sudo dnf install alveus
```

### Any distribution

```sh
./install.sh
```

(Arch and derivatives, Fedora, openSUSE Tumbleweed, Debian/Ubuntu-based with Plasma 6.)

### Turn it on

Restart the desktop (log out and in, or `systemctl --user restart plasma-plasmashell`),
then right-click the desktop → **Configure Desktop and Wallpaper…** →
**Layout: Alveus** → Apply. Do this on every monitor where you want fences.

## Something went wrong?

```sh
./recover.sh
```

switches every desktop back to the standard KDE one. Files and fence layout are
kept. By hand: *Configure Desktop and Wallpaper… → Layout: Desktop*.
Remove completely: `./uninstall.sh` (or `sudo dnf remove alveus`).

## Known limitations (0.03.1 beta)

- 38 automated core test cases are included; run them on Qt 6.4+ before
  installing this beta on a primary desktop
- Real Plasma desktop runs may still find rough edges — please report them
- Thumbnails for office documents are not generated (videos, PDFs and images are)
- Automatic screen-sharing detection relies on PipeWire and is experimental;
  the manual privacy mode always works

## Where things are stored

- Fence layout, saved layouts, settings: `~/.local/share/alveus/layout.json`
- Files: your Desktop folder, untouched
- Widget positions: Plasma's own config, as with the standard desktop

## Publishing (for the author)

- **GitHub**: push this folder to `github.com/bezeza/alveus-plasma`, tag `v0.03.1`
- **COPR**: create project *alveus* at copr.fedorainfracloud.org → *Packages* →
  *New package* → *SCM*: clone URL of the repo, build method **make srpm**
  (uses `.copr/Makefile`), then *Rebuild*. Choose the current Fedora releases, x86_64 and aarch64
- **KDE Store**: Plasma containments with a compiled part can't be installed
  from *Get New Widgets*; publish the page with a link to COPR / GitHub
- **Donations**: fill in `package/contents/ui/links.js` — the buttons appear in
  *Alveus Settings → About*

## Development

```sh
cmake -B build -DALVEUS_BUILD_TESTS=ON && cmake --build build
QT_QPA_PLATFORM=offscreen dbus-run-session -- build/tests/coretest  # 38 test cases
ctest --test-dir build --output-on-failure
```

`tests/uiharness` + `tests/scene.qml` render the desktop offscreen with stand-in
Plasma modules (`tests/mocks`, `tests/mockplasmoid`) for screenshots.

## License

GPL-2.0-or-later. `ConfigOverlay.qml` and `ActionButton.qml` are copied from KDE
plasma-desktop (LGPL-2.0-or-later).

---

## Українською

**Паркани на стільниці для KDE Plasma 6.** Обведіть мишкою область на столі, і там
з'явиться паркан. Нові файли самі розкладаються за типом, назвою, сайтом чи датою.
Файли нікуди не переміщуються: вони лишаються в теці «Стільниця».

**Встановлення на Fedora:** `./packaging/build-rpm.sh`, потім
`sudo dnf install ~/rpmbuild/RPMS/x86_64/alveus-0.03.1-*.rpm`.
На інших дистрибутивах: `./install.sh`.

**Увімкнути:** перезайдіть у систему, правий клік на столі →
«Налаштувати стільницю й шпалери…» → Компонування: **Alveus** → Застосувати
(на кожному моніторі).

**Головне:**
- обвели область → кнопка «Зробити паркан» → паркан забирає виділені файли
- подвійний клік по заголовку згортає паркан, по порожньому столу ховає все
- перетягніть паркан на заголовок іншого, і вони стануть вкладками
- правий клік на заголовку: налаштування, правила, кольори, стільники, сторінки, монітори
- правий клік на столі: звичне меню KDE плюс «Створити теку», «Вставити», «Новий паркан»
- паркан «Диски»: усі диски (NTFS, флешки, мережеві) зі смужкою заповнення й вільним місцем;
  непідключений розділ підключається подвійним клацанням (обведіть область → ▾ → Портал до теки → Диски)
- Ctrl+Z скасовує майже все

**Якщо щось зламалося:** `./recover.sh` повертає стандартний стіл, нічого не втрачається.
