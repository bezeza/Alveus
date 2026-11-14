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
