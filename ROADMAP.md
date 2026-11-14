# Alveus roadmap

> Last reviewed: 2026-11-14  
> This document describes planned work and release gates. It is not a changelog.
> Completed work belongs in the repository history and `docs/DEVELOPMENT_LOG.md`.

## Product direction

Alveus is a desktop organizer for KDE Plasma 6. It groups desktop files into
fences while keeping the files in their original locations unless the user
explicitly performs a file operation.

The near-term goal is to make the existing KDE experience dependable and easy
to install. Keep the current Qt/C++ and QML architecture; keep the core Qt-only
where practical and isolate Plasma-specific behavior at the desktop integration
layer.

### Engineering principles

- **Protect user data.** File operations must be recoverable, validated, and
  tested with temporary fixtures rather than real user files.
- **Keep Plasma responsive.** Filesystem work and expensive model updates must
  not block the UI thread.
- **Make failures diagnosable.** Report actionable errors locally, bound log
  retention, and never send telemetry or logs automatically.
- **Prefer focused changes.** One task per branch/PR, regression coverage for
  fixes, and a current development-log entry for implementation changes.
- **Do not rewrite for fashion.** No Rust rewrite or new framework solely for
  novelty; preserve the existing Qt/C++ and QML codebase unless evidence shows
  a concrete architectural limitation.

## Current baseline

Status snapshot for 2026-11-14. Merged work is shipped in `main`; open PRs
remain proposals until GitHub confirms the merge.

### Merged into `main`

- **PR #18 — keyed ListModel synchronization.** Replaced repeated linear key
  searches with keyed lookups while preserving existing model rows and delegates.
- **PR #14 — background directory validation.** Same-filesystem directory
  validation and moves now run in the worker for import, archive, and Undo;
  redundant tree-size scans for rename-only moves are avoided.
- **PR #15 — cached file placement and targeted model updates.** Per-activity
  file models are cached and only affected models are refreshed. The merge
  activity-scope cache invalidation bug is covered in both directions.
- **PR #17 — offscreen UI harness.** The UI harness is part of CTest, with
  Plasma API mocks and critical QML warning checks. The merged-head CI run
  passed both CTest targets (coretest and uiharness).
- **PR #19 — runtime diagnostics and agent workflow.** Added local rotating
  diagnostics with path redaction, a Settings viewer, provider-specific agent
  entrypoints, a canonical `AGENTS.md`, PR template, and CI enforcement for
  dated development-log updates.
- **PR #22 — development roadmap.** Added this dedicated roadmap and linked it
  from the README.
- **PR #20 — branding, localization, and Fedora RPM helper.** Added the canonical
  app icon, Russian/Ukrainian catalogs and validation, desktop preview, and RPM
  build/install helper. The current CI passes the build and preview checks.
- The GitHub Actions build-and-test workflow is present in `main`.

### Open work

| Priority | Work | Current reference | Next action |
|---|---|---|---|
| P1 | Diagnostics log-rotation regression test | [PR #23](https://github.com/bezeza/Alveus/pull/23) | Verify bounded three-generation retention after repeated rotations |
| P1 | Fedora RPM build and DNF validation | planned | Build and inspect the RPM in a Fedora environment, then verify DNF install/removal and dependency resolution |

PR #19 was synchronized with the merged performance and UI foundation before
it was merged. Its diagnostics code now includes a thread-local message-handler
reentrancy guard and regression coverage for path redaction. Log rotation
behavior still needs a focused test and live review.

PR #20 is merged. It consolidates the passing preview/icon foundation
with PR #21's Russian/Ukrainian catalogs, translation checker, and RPM build
helper. Its final CI passed, including preview rendering and artifact upload.
PR #21 was closed as superseded after its unique useful changes were carried over.

PR #16 was closed as superseded. Its only proposed workflow file was compared
with the current workflow in `main`; the current workflow includes the same
build/test, shell, QML-lint, and whitespace checks plus the `perf/**` push
trigger. Do not reopen or merge the stale workflow.

## Near-term execution plan

Complete these stages in order. Independent PRs may proceed separately, but
dependent work must be based on the correct updated branch.

### Stage 1 — Responsiveness and filesystem performance

- [x] Merge PR #14: move expensive same-filesystem directory validation away
  from the UI thread where required.
- [x] Merge PR #15: cache file placement data and update only affected fence
  models.
- [x] Add regression coverage for activity-scope cache invalidation in both
  merge directions.
- [x] Exercise cached retrieval across 600 files and multiple fences with a
  benchmark path.
- [ ] Stress-test repeated filesystem events, rename/move/delete sequences,
  mount/unmount, and changes made outside Alveus on a real KDE session.

**Exit criteria:** the merged implementation builds and its core regression
suite passes. Live Plasma stress testing remains a release gate.

### Stage 2 — Reliable UI regression checks

- [x] Merge PR #17 and integrate its UI harness with the standard CTest workflow.
- [x] Provide test doubles for the Plasma APIs used by the harness.
- [x] Make critical QML warnings fail the UI harness.
- [x] Keep offscreen preview screenshots reproducible and verify the generated
  artifacts in the merged branding/translation PR.

**Exit criteria:** current CI passes both coretest and uiharness; live Plasma
validation remains separate.

### Stage 3 — Diagnostics and contributor guardrails

- [x] Merge PR #19.
- [x] Verify severity, timestamp, category, and source context for recorded
  warnings/errors; add regression coverage for path redaction and handler
  reentrancy.
- [ ] Add focused log-rotation tests and review third-party paths in copied
  reports before sharing. PR #23 adds the missing rotation regression test.
- [x] Keep diagnostics local and user-controlled; no automatic upload or
  telemetry.
- [x] Establish `AGENTS.md`, provider-specific pointers, PR template, and
  `docs/DEVELOPMENT_LOG.md` as the common engineering workflow.

**Exit criteria:** merged CI passes; logging behavior and privacy limitations
are documented. Log-rotation coverage and live Plasma validation remain.

### Stage 4 — Identity, translations, and Fedora packaging

- [x] Consolidate PR #20/#21 into one reviewed implementation; remove duplicate
  assets and conflicting CI/README changes.
- [x] Establish one canonical application-icon path and use it consistently in
  the application metadata, package, and README.
- [x] Keep English as the source language; complete Ukrainian and Russian
  catalogs, run the translation checker and `msgfmt --check`.
- [x] Fix the offscreen preview and verify that the preview image is generated;
  the current CI run uploaded the PNG artifact.
- [x] Validate RPM helper argument handling, including build-only and
  `--install` selection, help, and invalid-option behavior.
- [ ] Run an actual RPM build, DNF installation/removal, and dependency
  resolution on a supported Fedora KDE system.
- [ ] Keep COPR publication separate until local RPM validation is complete.

**Exit criteria:** one coherent PR; current CI passes; icon and preview assets
exist at the referenced paths; translations validate; RPM packaging is
reviewed. A real Fedora build/install and live Plasma validation are still
required before release.

### Stage 5 — Real KDE Plasma validation

Run this stage on a Fedora KDE Plasma machine before calling the release
candidate ready. CI and offscreen rendering are not substitutes for a live
desktop session.

- [ ] Install and remove the RPM through DNF.
- [ ] Activate Alveus and verify recovery back to the standard Plasma desktop.
- [ ] Test create, rename, move, copy, delete, Undo, and failed-operation
  recovery with disposable test files.
- [ ] Test large folders, thumbnails, folder portals, drives, and external
  filesystem changes.
- [ ] Test multiple monitors, monitor unplug/reconnect, desktop activities, and
  desktop/session restart.
- [ ] Verify logs, log rotation, privacy redaction, and manual report copying.
- [ ] Record the Plasma, Qt, Fedora, and package versions used for validation.

**Exit criteria:** no data-loss issue or release-blocking regression; recovery
works; known limitations are documented; manual test results are recorded.

## Post-stabilization backlog

These items follow the current PR sequence. Reprioritize only when new evidence,
a reproducible bug, or user feedback justifies it.

| Priority | Topic | Reference | Intended outcome |
|---|---|---|---|
| P1 | Recovery safety | [Issue #8](https://github.com/bezeza/Alveus/issues/8) | Make recovery predictable and protect the user's desktop configuration |
| P1 | Qt compatibility baseline | [Issue #9](https://github.com/bezeza/Alveus/issues/9) | Define and test the minimum supported Qt version |
| P2 | External-process timeouts | [Issue #10](https://github.com/bezeza/Alveus/issues/10) | Prevent a stalled helper process from hanging an operation |
| P2 | Persisted configuration validation | [Issue #13](https://github.com/bezeza/Alveus/issues/13) | Reject or recover safely from invalid saved state |
| P2 | Honeycomb view scalability | [Issue #7](https://github.com/bezeza/Alveus/issues/7) | Keep large fences responsive without unnecessary delegate work |

## Release direction

### 0.1 — Stable KDE foundation

The first public stable milestone should focus on the existing product, not
feature count:

- responsive desktop and file models;
- safe file operations and a tested recovery path;
- reliable core and UI regression checks;
- local diagnostics with documented privacy behavior;
- complete English, Ukrainian, and Russian interface coverage;
- reproducible Fedora RPM packaging and clear installation/uninstallation
  instructions;
- live Plasma validation and a documented compatibility baseline.

The release number is a planning target, not a claim that the current beta is
already release-ready.

### 0.2 — Usability and scale

After the stable foundation, prioritize improvements supported by real use:
large-folder behavior, settings clarity, interaction consistency, accessibility,
and performance measured on actual Plasma sessions.

### 0.3 — Integrations and extensibility

Consider additional integrations and extension points only where they solve a
demonstrated user problem and can be maintained without compromising the core
desktop experience.

### 1.0 — Mature product

A mature release requires a supported compatibility matrix, dependable
packaging/update/removal, documented recovery, reproducible tests, and a
maintained issue/feedback process.

### Other desktop environments and Windows

Treat other Linux desktop environments and Windows as feasibility work after
the KDE foundation is stable. They require explicit platform backends and
separate integration/testing plans; they are not part of the current release
commitment. Do not weaken the KDE implementation or delay stabilization to
start a speculative port.

## Definition of done

A task is complete only when:

1. it is isolated in a focused branch and PR;
2. the cause and scope are clear;
3. relevant regression tests are added or updated;
4. current CI checks pass, or any remaining failure is explicitly documented;
5. implementation changes include a dated entry in
   `docs/DEVELOPMENT_LOG.md`;
6. documentation and translations are updated where applicable;
7. manual Plasma checks that were not run are clearly identified;
8. the PR is reviewed and merged by an authorized maintainer.

A green CI run is necessary, but it does not prove that every interaction works
in a live Plasma session.
