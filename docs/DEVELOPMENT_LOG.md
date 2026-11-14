# Development log

### Merged into main — PR #20

- Added a canonical Alveus icon for Plasma, KRunner, and desktop launchers.
- Added Russian and Ukrainian catalogs, translation validation, and a reproducible
  offscreen desktop preview with a CI artifact.
- Added a Fedora RPM spec update and a build/install helper with CLI validation.
- The current CI verifies the source build and package helper interface. A real
  Fedora RPM build and DNF install/removal remain release checks.

### In progress — PR #23 diagnostics rotation coverage
- Add a regression test that forces four log rotations and verifies the three
  retained archives plus the latest entry in the active log.
- Verification is pending on the updated PR head.


- Add a canonical Alveus application icon for Plasma, KRunner, and desktop launchers.
- Keep English as the source language; add Russian and Ukrainian catalogs, including the Diagnostics UI strings.
- Add catalog parity, placeholder, markup, and QML translation coverage checks.
- Add a Fedora RPM build helper with optional DNF installation and command-line validation.
- Add a reproducible offscreen desktop preview and upload the generated PNG as a CI artifact.
- Validation is pending on the consolidated current head. No Fedora RPM build/install or live Plasma session has been run.


This is the engineering record for Alveus. Entries distinguish merged work from
open pull requests, so proposed code is not mistaken for shipped code.

## 2026-11-14

### Merged into `main`

- **PR #18 — keyed ListModel synchronization.** Replaced repeated linear key
  searches with `Set`/`Map` lookups while preserving existing ListModel rows
  and delegates. Merge commit: `65a7beeb8ce62325458983d93f9f2f80ffc139ef`.
- The same merge added the GitHub Actions build/test workflow.

### Open pull requests (not yet in `main`)

- **PR #14:** background same-filesystem directory operations.
- **PR #15:** cached file placement and targeted fence-model updates.
- **PR #16:** GitHub Actions build and test pipeline.
- **PR #17:** headless UI harness and critical QML warning checks.

### In progress — PR #15 follow-up

- Rebuild per-activity file-model caches when `mergeFences()` changes the
  source fence's activity scope.
- Add regression coverage for both global-to-activity-specific and
  activity-specific-to-global merges.
- GitHub Actions on the updated implementation head passed: build, CTest
  core suite (1/1), shell checks, QML lint, and patch-whitespace validation.
  Runs: [43594527975](https://github.com/bezeza/Alveus/actions/runs/43594527975)
  and [43594525739](https://github.com/bezeza/Alveus/actions/runs/43594525739).
- Live KDE Plasma validation has not been run.

### In progress — PR #19

- Persistent application diagnostics, a Settings → Diagnostics viewer, bounded
  log rotation, and diagnostics documentation.
- A canonical `AGENTS.md` policy plus lightweight entrypoints for common coding
  agents (Claude Code, GitHub Copilot, Cursor, Gemini CLI, Cline, Roo Code, Aider, and Windsurf).
- A PR template and CI enforcement requiring a development-log entry whenever
  implementation, tests, CI, or packaging files change.
- Updated Claude/README build guidance to avoid stale test counts and to use the
  same full CTest command as CI.

The diagnostics and agent-governance changes are in the open PR #19. The first
CI run for the diagnostics-only commit passed build, core tests, shell checks,
QML lint, and whitespace validation. The combined changes are subject to a new
CI run on the current PR head. Nothing in this section is shipped until PR #19
is merged.

The branch was synchronized with the merged performance and UI foundation on
2026-11-14. A thread-local message-handler reentrancy guard and a path-redaction
regression check were added; verification is pending on this combined head.

### Merged into main — PR #24 Fedora RPM validation

- Added a Fedora-container GitHub Actions job that builds the RPM, installs it
  through DNF, verifies the icon, plasmoid metadata, KRunner desktop entry, and
  Russian/Ukrainian locale files, uploads the RPM artifact, and removes the
  package.
- Made the helper's explicit `--install` mode non-interactive for CI by
  passing `-y` to DNF. The normal build-only mode remains unchanged.
- Install the Fedora builddep plugin `dnf-plugins-core` before resolving
  BuildRequires; the Fedora-container build/install check passed with it.
- Fedora-container build/install and payload verification passed; artifact:
  `alveus-fedora-rpm`. Live KDE Plasma session validation remains separate.
