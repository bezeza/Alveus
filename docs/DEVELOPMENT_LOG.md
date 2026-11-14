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
  agents (Claude Code, GitHub Copilot, Cursor, Gemini CLI, Cline, and Windsurf).
- A PR template and CI enforcement requiring a development-log entry whenever
  implementation, tests, CI, or packaging files change.
- Updated Claude/README build guidance to avoid stale test counts and to use the
  same full CTest command as CI.

The diagnostics and agent-governance changes are in the open PR #19. The first
CI run for the diagnostics-only commit passed build, core tests, shell checks,
QML lint, and whitespace validation. The combined changes are subject to a new
CI run on the current PR head. Nothing in this section is shipped until PR #19
is merged.
