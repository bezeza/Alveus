# Development log

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

### In progress — PR #21: branding and localization

- Add a dedicated Alveus honeycomb app icon in SVG format; use the
  named icon in Plasma metadata, KRunner integration, and QML settings.
- Add a complete Russian Qt/KDE translation catalog. English remains the
  source language in QML, and Ukrainian remains supported.
- Add translation catalog parity, placeholder, and markup checks to CI.
- Render real Alveus QML UI previews with the existing offscreen harness and
  upload them as a GitHub Actions artifact; publish the verified previews in
  the repository README after the CI run.

These changes are proposed in PR #21 and are not shipped until merged.

- The first CI preview exposed a missing Plasma Button mock and a blank render.
  Added the missing test component and strict QML-warning handling so CI refuses
  to publish screenshots when the UI did not load cleanly.

- The second preview run exposed missing test-only KDE drag-and-drop and
  containment-layout modules, plus inline-component context behavior. Added
  lightweight mocks and bound inline components so the preview exercises the
  actual Alveus desktop UI instead of saving an empty wallpaper.

- Removed the preview-only bound-component pragma after it exposed a model
  context mismatch. The harness now attaches its i18n mock to newly-created QML
  contexts, which mirrors KDE's translation helpers without changing app logic.

- Added the new hicolor SVG icon to the RPM file manifest so the packaged
  Plasma metadata and KRunner icon name resolve after installation.
