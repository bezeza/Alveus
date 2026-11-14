# Alveus repository instructions

Before every task, read and follow the repository-root `AGENTS.md`. It is the
canonical, mandatory policy for all coding agents and takes precedence over
provider-specific defaults.

In particular:
- inspect existing changes before editing; preserve user work;
- use a dedicated branch and focused PR; never commit directly to `main`;
- add a dated entry to `docs/DEVELOPMENT_LOG.md` in every implementation PR;
- add regression tests and run the current CMake/CTest, QML, and shell checks
  relevant to the change;
- use the app diagnostics system without logging secrets or file contents;
- distinguish verified results from checks not run;
- never merge, install, switch Plasma layouts, or restart `plasmashell`
  without explicit user approval.

Do not duplicate or weaken `AGENTS.md`; update that canonical file when policy
changes.
