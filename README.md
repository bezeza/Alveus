# Alveus

<p align="center">
  <img src="assets/alveus.svg" alt="Alveus app icon" width="144">
</p>

**Desktop fences for KDE Plasma 6.** Drag a box on the desktop to make a fence;
new files sort themselves into fences by type, name, download site or date.
Your files never move: they stay in the Desktop folder, fences only group them.
Inspired by Stardock Fences on Windows.

*Alveus* is Latin for "beehive": every fence is a cell, every file a bee.

[English](#what-it-does) · [Українською](#українською) · [Русский](#по-русски)

**Status: 0.03.1 beta.**

## Screenshots

The previews below are rendered from Alveus QML with the project's offscreen UI harness and mock Plasma APIs.

![Alveus desktop with fences](docs/screenshots/alveus-desktop.webp)

![Alveus settings](docs/screenshots/alveus-settings.webp)


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

### Fedora (RPM / DNF)

Build and install in one step. DNF resolves the package's runtime dependencies:

```sh
./packaging/build-rpm.sh --install
```

To build without installing, run `./packaging/build-rpm.sh`. The script prints
the exact `sudo dnf install /path/to/alveus-….rpm` command for the package it
just built. DNF asks for confirmation before installing.

Uninstall with:

```sh
sudo dnf remove alveus
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

- Automated core tests are included; run the current Qt 6.4+ test suite before
  installing this beta on a primary desktop
- Real Plasma desktop runs may still find rough edges — please report them
- Thumbnails for office documents are not generated (videos, PDFs and images are)
- Automatic screen-sharing detection relies on PipeWire and is experimental;
  the manual privacy mode always works

## Where things are stored

- Fence layout, saved layouts, settings: `~/.local/share/alveus/layout.json`
- Diagnostics: `~/.local/share/alveus/logs/alveus.log` (rotating; open from Settings → Diagnostics)
- Files: your Desktop folder, untouched
- Widget positions: Plasma's own config, as with the standard desktop

## Publishing (for the author)

- **GitHub**: push this folder to `github.com/bezeza/Alveus`, tag `v0.03.1`
- **COPR**: create project *alveus* at copr.fedorainfracloud.org → *Packages* →
  *New package* → *SCM*: clone URL of the repo, build method **make srpm**
  (uses `.copr/Makefile`), then *Rebuild*. Choose the current Fedora releases, x86_64 and aarch64
- **KDE Store**: Plasma containments with a compiled part can't be installed
  from *Get New Widgets*; publish the page with a link to COPR / GitHub
- **Donations**: fill in `package/contents/ui/links.js` — the buttons appear in
  *Alveus Settings → About*

## Development

```sh
cmake -S . -B build -DALVEUS_BUILD_TESTS=ON
cmake --build build --parallel
dbus-run-session -- ctest --test-dir build --output-on-failure
```

`tests/uiharness` + `tests/scene.qml` render the desktop offscreen with stand-in
Plasma modules (`tests/mocks`, `tests/mockplasmoid`) for screenshots.

All coding agents must read [`AGENTS.md`](AGENTS.md) before working. It defines
the shared engineering, diagnostics, testing, and handoff rules. Every
implementation PR must update `docs/DEVELOPMENT_LOG.md`; CI checks this
requirement. Provider-specific entrypoints are provided for Claude Code,
GitHub Copilot, Cursor, Gemini CLI, Cline, Roo Code, Aider, and Windsurf.

## License

GPL-2.0-or-later. `ConfigOverlay.qml` and `ActionButton.qml` are copied from KDE
plasma-desktop (LGPL-2.0-or-later).

---

## Українською

**Паркани на стільниці для KDE Plasma 6.** Обведіть мишкою область на столі, і там
з'явиться паркан. Нові файли самі розкладаються за типом, назвою, сайтом чи датою.
Файли нікуди не переміщуються: вони лишаються в теці «Стільниця».

**Встановлення на Fedora:** `./packaging/build-rpm.sh --install` — скрипт
збирає RPM і встановлює його через DNF із розв'язанням залежностей.
Для окремої збірки без встановлення запустіть `./packaging/build-rpm.sh`;
скрипт покаже точну команду DNF. Видалення: `sudo dnf remove alveus`.
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

---

## По-русски

**Ограждения рабочего стола для KDE Plasma 6.** Выделите мышью область на пустом рабочем столе, чтобы создать ограждение. Новые файлы автоматически распределяются по типу, имени, сайту загрузки или дате. Файлы остаются в папке «Рабочий стол»: Alveus только группирует их.

**Основные возможности:**
- готовые ограждения для документов, фотографий, видео, музыки, архивов, программ и старых файлов;
- правила сортировки по типу, имени, сайту, дате и размеру;
- сворачивание ограждений, вкладки и порталы к папкам;
- отображение дисков и свободного места;
- несколько страниц рабочего стола, мониторы и активности;
- режим конфиденциальности при демонстрации экрана;
- отмена большинства действий и безопасное перемещение файлов в архив.

**Установка на Fedora:** выполните `./packaging/build-rpm.sh --install`. Скрипт соберёт RPM и установит его через DNF с автоматическим разрешением зависимостей. Для сборки без установки используйте `./packaging/build-rpm.sh`; скрипт выведет точную команду DNF. Удаление: `sudo dnf remove alveus`. На других дистрибутивах KDE Plasma 6 используйте `./install.sh`. После установки выберите раскладку **Alveus** в настройках рабочего стола.

Интерфейс использует английский как исходный язык, а переводы выбираются автоматически по языку системы. Сейчас доступны украинский и русский переводы.
