# Development log

This is the engineering record for Alveus. Entries distinguish merged work
from open pull requests, so proposed code is not mistaken for shipped code.

## 2026-11-14

### Merged into `main`

- **PR #18 — keyed ListModel synchronization.** Replaced repeated linear key
  searches with keyed lookups while preserving existing model rows and
  delegates.
- **PR #14 — background directory validation.** Moved recursive validation and
  same-filesystem directory operations off the UI thread for import, archive,
  and Undo. Avoided redundant tree-size scans for rename-only moves.
- **PR #15 — cached file placement and targeted model updates.** Added
  per-activity file-model caches and refreshed only affected models. Added
  regression coverage for cache invalidation in both fence-merge directions.
- **PR #17 — offscreen UI harness.** Integrated the UI harness into CTest,
  added Plasma API test doubles, and made critical QML warnings fail the
  harness. The merged-head CI passed both CTest targets.
- **PR #22 — development roadmap.** Added `ROADMAP.md` with the product
  direction, staged work, release criteria, and post-stabilization backlog;
  linked it from the README.
- **PR #19 — runtime diagnostics and agent workflow.** Added local JSON-lines
  diagnostics with bounded rotation and path redaction, a Settings diagnostics
  viewer, a canonical `AGENTS.md`, provider-specific agent entrypoints, a PR
  template, and CI enforcement for dated development-log updates. Added
  regression coverage for warning capture, application errors, path redaction,
  and message-handler reentrancy.
- **PR #20 — branding, localization, preview, and Fedora RPM helper.** Added
  the canonical application icon, Ukrainian and Russian translation catalogs
  and validation, a reproducible offscreen desktop preview with a CI artifact,
  and an RPM build/install helper with CLI validation.
- **PR #23 — diagnostics log-rotation coverage.** Added a regression test that
  forces four consecutive rotations, verifies that exactly three archive
  generations are retained, and checks that the latest warning is written to
  the active JSON-lines log.

### Verification and remaining release checks

- The current merged CI for PR #19 passed the build, both CTest targets
  (`coretest` and `uiharness`), shell checks, QML lint, development-log
  enforcement, and whitespace validation.
- The current merged CI for PR #20 passed build, tests, translation checks,
  RPM-helper CLI checks, shell checks, QML lint, offscreen preview rendering,
  artifact upload, and whitespace validation.
- The current merged CI for PR #23 passed build, both CTest targets
  (`coretest` and `uiharness`), translation checks, RPM-helper CLI checks,
  shell checks, QML lint, development-log enforcement, offscreen preview
  rendering, artifact upload, and whitespace validation.
- A real Fedora RPM build, DNF installation/removal, and dependency-resolution
  check have not been run in a supported Fedora KDE environment.
- Live KDE Plasma validation has not been run. CI and offscreen rendering do
  not replace testing file operations, recovery, desktop activities, multiple
  monitors, and session restart in a real Plasma session.

### Earlier implementation notes

- **PR #15 follow-up:** Rebuilt per-activity file-model caches when
  `mergeFences()` changes the source fence's activity scope. Regression
  coverage exercises both global-to-activity-specific and
  activity-specific-to-global merges.
- **CI foundation:** The GitHub Actions workflow builds the Qt application,
  runs CTest, checks shell scripts, lints QML, and validates patch whitespace.
  Later merged changes extended it with the UI harness, translation checks,
  preview rendering, and development-log enforcement.


### In progress — PR #26 Fedora RPM smoke test

- Add a Fedora 43 container workflow that builds the source RPM, installs it
  through DNF, verifies the packaged Plasma/QML/icon files, removes the package,
  and uploads the resulting RPM as a CI artifact.
- Make the explicit `--install` path non-interactive for reliable automation.
- A container build does not replace the outstanding live Fedora KDE/Plasma
  installation and desktop-session validation.


### Merged into `main` — PR #26 Fedora RPM and DNF smoke test

- Added a Fedora 43 container workflow that builds the RPM from the current
  source tree, installs it with DNF, verifies the Plasma metadata, QML module
  descriptor and application icon, uploads the RPM artifact, then removes the
  package with DNF.
- Changed the explicit `--install` helper path to use non-interactive
  `dnf install -y`.
- Fedora build/install/payload/removal workflow passed:
  [run 43601617189](https://github.com/bezeza/Alveus/actions/runs/43601617189).
- Standard Qt build, CTest, translations, RPM-helper CLI, shell, QML lint,
  development-log enforcement, offscreen preview, artifact upload, and
  whitespace checks passed:
  [run 43601617196](https://github.com/bezeza/Alveus/actions/runs/43601617196).
- The Fedora container is not a live KDE desktop. RPM activation in Plasma,
  recovery, multi-monitor behavior, and session restart remain untested.


### In progress — Issue #8 / PR #28 atomic desktop recovery

- Replace direct redirection into the live Plasma config with a same-directory
  temporary file, validation, and atomic rename after a timestamped backup.
- Add EXIT/INT/TERM/HUP cleanup so a restart is attempted on normal completion,
  errors, and interruption after the shell stop is attempted.
- Add temporary-fixture tests for desktop and widget containments, unrelated
  applets, transformation/validation failures, and SIGTERM interruption.
- Verification is pending on the PR head. No live Plasma service is stopped or
  restarted by these tests.


### Merged into `main` — PR #28 atomic and restart-safe recovery

- Replaced direct redirection into the live Plasma applet config with a
  same-directory temporary file, validation, and atomic rename after a
  timestamped backup.
- Added EXIT/INT/TERM/HUP cleanup so restart is attempted after normal
  completion, transformation/validation errors, and interruption.
- Added a temporary-fixture CTest target covering desktop containment
  conversion, Alveus widget/subsection removal, unrelated applet preservation,
  systemd and fallback startup, transformation/validation failures, and
  SIGTERM interruption.
- Current-head CI passed build, all three CTest targets (`coretest`,
  `uiharness`, and `recover_script`), ShellCheck, QML lint, translation
  validation, preview rendering/upload, and whitespace checks:
  [run 43602359421](https://github.com/bezeza/Alveus/actions/runs/43602359421).
- No live Plasma service was stopped or restarted by CI. A real desktop recovery
  run remains part of the manual Plasma validation stage.


### In progress — Issue #9 / PR #30 Qt compatibility baseline

- Align the declared minimum with Qt Quick Effects / MultiEffect, introduced
  in Qt 6.5: CMake requires Qt 6.5+, the RPM declares matching build/runtime
  dependencies, and the README/installer/agent guidance agree.
- Run CMake on Ubuntu 24.04's Qt 6.4.2 toolchain and assert configuration
  rejects it specifically because the requested minimum is Qt 6.5.
- Move legacy-widget removal in `install.sh` until after successful CMake
  configuration and build, so an unsupported Qt version cannot remove the old
  widget before the compatibility check fails.
- Verification is pending on the PR head.


### Merged into `main` — PR #30 Qt 6.5 compatibility baseline

- Raised the CMake Qt minimum to 6.5 because Qt Quick Effects / MultiEffect
  was introduced in Qt 6.5. Aligned Fedora RPM build/runtime dependencies,
  README, installer guidance, and the Claude agent notes.
- Moved legacy-widget removal in `install.sh` until after successful CMake
  configuration and build, preventing an unsupported Qt version from changing
  the user's desktop before the version check fails.
- Ubuntu 24.04 / Qt 6.4.2 was rejected specifically because CMake requested
  Qt 6.5; the standard Qt build/CTest/static checks passed:
  [run 43603426393](https://github.com/bezeza/Alveus/actions/runs/43603426393).
- Fedora 43 RPM build, DNF install, payload verification, artifact upload, and
  DNF removal passed with the new version constraints:
  [run 43603426388](https://github.com/bezeza/Alveus/actions/runs/43603426388).
- No Qt 6.5 exact-minimum build or live KDE Plasma session was run. Those
  remain outside the current CI coverage.


### Merged into `main` — PR #32 bounded external helper processes

- Added a shared QProcess deadline helper with timer ownership tied to the
  process object, timeout marking, and cleanup from finished callbacks.
- Bounded `pw-dump` and `kdeconnect-cli` at 5 seconds, `lsblk` at 10 seconds,
  `udisksctl mount/unmount` at 30 seconds, and video/PDF thumbnail helpers at
  30 seconds.
- Screen-share detection fails closed on missing, invalid, failed, or timed-out
  probes; polling backs off to 60 seconds and resets after success. Phone and
  drive discovery retain their last successful data on failure. Mount/unmount
  operations are not replayed automatically. Thumbnail failures release the
  worker slot and temporary directory, with an on-demand 30-second cooldown.
- Fake-helper regression tests cover hung and non-zero-exit processes, screen-
  share recovery, phone and drive retry, mount/unmount timeouts, thumbnail
  queue release, and suppression of immediate retry storms.
- Current-head standard CI passed build, all three CTest targets, ShellCheck,
  QML lint, translations, development-log enforcement, preview rendering, and
  whitespace checks:
  [run 43606016305](https://github.com/bezeza/Alveus/actions/runs/43606016305).
- Fedora 43 RPM build, payload verification, artifact upload, DNF install, and
  DNF removal passed:
  [run 43606016299](https://github.com/bezeza/Alveus/actions/runs/43606016299).
- No live PipeWire, KDE Connect, udisksctl, or Plasma session was exercised by
  CI; those remain manual desktop validation tasks.

### In progress — Stage 1 / feat/drives-hierarchy-core

- Introduce a recursive drive model that preserves physical devices, nested
  partitions/logical volumes, parent relationships, multiple mount points,
  filesystem metadata, model/vendor, transport, WWN, UUID, PARTUUID, and
  partition type metadata.
- Expose the new hierarchy as the additive Core.driveDevices property while
  retaining Core.drives and the existing flat QML view for compatibility.
- Request optional lsblk columns and fall back to the known baseline column
  set when an older util-linux version rejects them. Keep the existing bounded
  process timeout and last-valid-output behavior.
- Add parser fixtures for a five-partition NVMe device, USB transport,
  removable media without USB transport metadata, stable-ID fallbacks,
  multiple mount points, the legacy singular mountpoint field, and small
  partitions.
- The legacy flat helper retains its historical 2 GiB display cutoff until the
  UI switches to the new hierarchy and configurable visibility filters. The
  new hierarchy parser itself does not discard partitions by size.
- Verification: local build and CTest have not been run because this execution
  workspace has no Qt 6 development toolchain or repository checkout. GitHub
  Actions verification is pending. No live KDE Plasma or physical-drive test
  has been performed.

