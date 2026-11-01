# Alveus platform architecture

**Status:** migration in progress. The shipped 0.03.1 release remains a KDE Plasma 6
desktop widget. The feature/core-refactor branch is the development baseline for
the shared-core migration.

## Product intent

Alveus should provide the same fence model, sorting rules, file operations, saved
layouts and interaction language on Windows and the major Linux desktop environments.
The core must not depend on a particular desktop shell or OS-specific filesystem APIs.
Platform-specific behavior belongs behind small, testable adapters.

Alveus must not become unrelated implementations that merely share a name. It must
not bypass operating-system security or depend on undocumented Explorer internals
as its only Windows integration strategy.

## Layers

1. **Core domain and services** — fences, placements, sorting rules, undo history,
   snapshots, configuration and job orchestration.
2. **Platform contracts** — narrow interfaces for filesystem operations, desktop
   hosting, monitors, clipboard, trash, thumbnails, drives and shell integration.
3. **Platform adapters** — Windows APIs and Linux desktop-specific implementations.
4. **Shared UI** — Qt Quick components and settings shared where the host permits it.
5. **Desktop hosts** — Plasma package, Windows desktop host and Linux integrations
   for Cinnamon, XFCE, GNOME, MATE and LXQt.

## First extracted contract: file operations

The platform/interfaces/fileoperations.h header defines IFileOperations. AlveusCore
uses this interface for tree sizing, volume checks, copy, move and recursive removal.

- LinuxFileOperations delegates to the existing 0.03.1 POSIX implementation.
  This preserves Linux behavior while moving the dependency boundary.
- WindowsFileOperations uses Qt filesystem APIs and stages copies in a temporary
  sibling before final rename. It rejects symlinks/reparse points during copy rather
  than following or silently converting them. Same-volume renames can move a link
  without dereferencing it. Native reparse-point preservation needs dedicated tests.

The legacy core/fileops.* API remains available to Linux regression tests in this
phase; new core code must use IFileOperations.

## Platform-specific modules

- KRunner and its D-Bus service are compiled only on Linux.
- The Plasma package and KRunner desktop metadata are installed only on Linux.
- The Windows build currently compiles the shared QML core and platform file adapter;
  it is not yet a complete Windows desktop application or Explorer integration.
- Linux desktop adapters beyond KDE Plasma are not yet implemented. The next seams
  are IDesktopHost, IMonitorProvider, IClipboard, IDriveProvider and IShellIntegration.

## Build and test

```sh
cmake -S . -B build -DALVEUS_BUILD_TESTS=ON
cmake --build build --parallel
ctest --test-dir build --output-on-failure
```

platformfileoperationstest is intended to run on Windows and Linux. The existing
coretest contains POSIX and D-Bus-specific cases and therefore remains Linux-only.

## Decisions

- **One shared core, separate hosts:** preserve one domain model and one settings
  format; isolate shell lifecycle and desktop placement in host adapters.
- **No fake Windows support:** compiling the shared core is not the same as shipping
  a Windows desktop host. Windows integration is a later milestone.
- **Fail safely on unsupported links:** never dereference or skip a link during copy
  if its semantics cannot be preserved.
- **Keep main stable:** architectural work is developed on feature/core-refactor
  and reviewed before merging into develop or main.
