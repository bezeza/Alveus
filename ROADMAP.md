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
- The GitHub Actions build-and-test workflow is present in `main`.

### Open work

| Priority | Work | Current reference | Next action |
|---|---|---|---|
| P2 | Runtime diagnostics and agent workflow | [PR #19](https://github.com/bezeza/Alveus/pull/19) | Sync with the new `main`, inspect the combined core diff, improve diagnostics edge-case coverage, and rerun CI |
| P2 | Icon, translations, previews, Fedora RPM workflow | [PR #20](https://github.com/bezeza/Alveus/pull/20), [PR #21](https://github.com/bezeza/Alveus/pull/21) | Consolidate into one PR: use the passing PR #20 preview/icon foundation and selectively bring over PR #21's stronger translation checks and RPM installer after review |

PR #19 was authored against the pre-performance `main`. It changes the same
core files as PR #15, so it must be synchronized and reviewed against the
merged cache implementation before it can be marked ready. Its development
log currently matches the version merged by PR #15; preserve that history.

PR #20 and PR #21 are stacked on an earlier diagnostics-branch commit and
overlap in branding, translations, CI, README, settings, and UI test support.
PR #20's recorded CI run passed, including offscreen preview rendering, but
it predates the current `main`. PR #21's recorded run failed in the preview
step: the mock Plasma API lacks members used by the settings/desktop preview,
including `configOverlaySource` and a callable `i18n`; qmllint also reports
mock-type warnings. Fix the mock contract and verify a real rendered artifact.
Do not merge both PRs or duplicate icon assets.

PR #16 contains only an older CI workflow proposal. The current workflow is
already in `main` through merged work; compare the unique workflow changes
before closing #16 as superseded. Do not merge a stale workflow over the
current one.

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
- [ ] Keep offscreen preview screenshots reproducible and verify the generated
  artifacts in the branding/translation PR.

**Exit criteria:** current CI passes both coretest and uiharness; live Plasma
validation remains separate.

### Stage 3 — Diagnostics and contributor guardrails

- [ ] Synchronize and complete PR #19 against the merged performance and UI work.
- [ ] Verify severity, timestamp, category, and source context for recorded
  warnings/errors; add coverage for log rotation, path redaction, and handler reentrancy.
- [ ] Verify bounded log rotation and path redaction; review copied reports for
  possible third-party paths before sharing.
- [ ] Keep diagnostics local and user-controlled; no automatic upload or
  telemetry.
- [ ] Establish `AGENTS.md`, provider-specific pointers, PR template, and
  `docs/DEVELOPMENT_LOG.md` as the common engineering workflow.

**Exit criteria:** current CI passes; logging behavior and privacy limitations
are documented; implementation PRs record the checks actually run.

### Stage 4 — Identity, translations, and Fedora packaging

- [ ] Consolidate PR #20/#21 into one reviewed implementation; remove duplicate
  assets and conflicting CI/README changes.
- [ ] Establish one canonical application-icon path and use it consistently in
  the application metadata, package, and README.
- [ ] Verify English source strings and complete Ukrainian and Russian
  translations; run the translation checker and `msgfmt --check`.
- [ ] Fix the offscreen preview failure and verify that both intended preview
  images are actually generated and referenced correctly.
- [ ] Validate the Fedora RPM build script, including build-only and
  build-and-install modes, argument handling, and DNF dependency resolution.
- [ ] Keep COPR publication separate until local RPM validation is complete.

**Exit criteria:** one coherent PR; current CI passes; icon and preview assets
exist at the referenced paths; translations validate; RPM build path is
reviewed. Do not claim a real Fedora installation was tested unless it was.

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
