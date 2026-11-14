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
- **PR #23 — diagnostics log-rotation regression test.** Verified four rotations,
  retention of exactly three archive generations, and the newest warning in the
  active JSON-lines log. Current-head CI passed both CTest targets (2/2).
- **PR #26 — Fedora RPM build and DNF lifecycle smoke test.** Added a Fedora 43
  container workflow that builds the RPM, installs it through DNF, verifies the
  Plasma metadata/QML/icon payload, uploads the RPM artifact, and removes the
  package through DNF. The container check passed; live Plasma testing remains.
- **PR #28 — atomic and restart-safe recovery.** Replaced direct writes to the
  live Plasma config with a validated temporary file and atomic rename after a
  timestamped backup. Added restart cleanup and isolated regression tests for
  normal, fallback, failure, and interruption paths. CTest passed 3/3.
- **PR #30 — Qt 6.5 compatibility baseline.** Aligned CMake, RPM build/runtime
  dependencies, installer ordering, README, and agent guidance with the Qt
  Quick Effects / MultiEffect minimum. CI proves Qt 6.4.2 is rejected; Fedora
  43 RPM build and DNF install/removal passed.
- **PR #32 — bounded external helper processes.** Added QProcess deadlines for
  PipeWire, KDE Connect, lsblk, udisksctl, and thumbnail helpers. Screen-share
  errors fail closed; phone/drive discovery retain last-known state; thumbnail
  jobs release worker slots and use a 30-second retry cooldown. CTest and
  Fedora RPM/DNF checks passed.
- The GitHub Actions build-and-test workflow is present in `main`.

### Open work

| Priority | Work | Current reference | Next action |
|---|---|---|---|
| P1 | Live Fedora KDE/Plasma validation | planned | Install the built RPM on a supported Fedora KDE desktop, activate Alveus, and test file operations, recovery, activities, monitors, and session restart |

PR #19 was synchronized with the merged performance and UI foundation before
it was merged. Its diagnostics code includes a thread-local message-handler
reentrancy guard and regression coverage for path redaction. PR #23 subsequently
added focused coverage for three-generation log retention. Review copied reports
for third-party paths before sharing; no logger can redact every arbitrary path
emitted by external libraries.

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
- [x] Add focused log-rotation tests: PR #23 forces four rotations and verifies
  that only three archive generations remain, with the newest entry active.
- [ ] Review third-party paths in copied reports before sharing.
- [x] Keep diagnostics local and user-controlled; no automatic upload or
  telemetry.
- [x] Establish `AGENTS.md`, provider-specific pointers, PR template, and
  `docs/DEVELOPMENT_LOG.md` as the common engineering workflow.
- [x] Bound external helper processes, define stale/error policies, and test
  timeout cleanup and retry behavior (PR #32).

**Exit criteria:** merged CI passes; logging behavior, retention, and privacy
limitations are documented. Live Plasma validation remains.

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
- [x] Set the Qt minimum to 6.5+ across CMake, RPM metadata, installer guidance,
  and CI; the Ubuntu 24.04 / Qt 6.4.2 job confirms CMake rejects the old version
  specifically for requesting Qt 6.5 (PR #30).
- [x] Add a Fedora 43 container CI smoke test that builds the RPM, installs it
  with DNF, verifies the installed payload, removes it, and uploads the RPM artifact (PR #26).
- [ ] Run the package on a supported Fedora KDE desktop and verify runtime
  dependency resolution and Plasma integration.
- [ ] Keep COPR publication separate until local RPM validation is complete.

**Exit criteria:** one coherent PR; current CI passes; icon and preview assets
exist at the referenced paths; translations validate; RPM packaging is
reviewed. Fedora 43 container build/install/removal now passes; a real Fedora KDE
desktop installation and live Plasma validation are still required before release.

### Stage 5 — Real KDE Plasma validation

- [x] Complete Issue #8: recovery now uses a validated same-directory temporary
  file, timestamped backup, atomic replacement, and restart cleanup; temporary
  fixture tests cover desktop/widget containments, unrelated applets, failures,
  and interruption (PR #28).

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

## Drive interface feature track

This is a separate, stacked feature track. These PRs remain proposals until
they are reviewed and merged; a passing CI run does not mean the work is
already shipped in `main`.

### Stage A — Drive discovery core

- [ ] Review and merge [PR #35](https://github.com/bezeza/Alveus/pull/35).
- [x] Preserve physical-device and partition hierarchy, stable IDs, mount
  points, filesystem and hardware metadata in the additive `Core.driveDevices`
  API.
- [x] Keep the legacy flat `Core.drives` API for compatibility.

### Stage B — Visibility and filtering

- [ ] Review and merge [PR #37](https://github.com/bezeza/Alveus/pull/37),
  stacked on Stage A.
- [x] Add global defaults, per-fence overrides, service-partition filters,
  manual visibility exceptions, and temporary reveal.
- [x] Test inheritance, persistence, filtering precedence, and UI settings.

### Stage C — Hierarchical drive interface

- [ ] Review and merge the Stage C PR, stacked on PR #37.
- [x] Render physical disks as parent cards with expandable partitions and
  logical volumes.
- [x] Distinguish NVMe, USB, SSD, and HDD using lsblk transport and ROTA data;
  leave media type generic when the hardware does not report enough metadata.
- [x] Show capacity, mounted-volume usage, free space, mount state, and
  read-only status with compact, medium, and large card sizes.
- [x] Keep mount/unmount, open, properties, terminal, drag/drop, and visibility
  actions available on relevant volume rows.
- [ ] Verify the current implementation with CI and on a live KDE Plasma system
  with internal, USB, and removable storage.

**Exit criteria:** current CI passes; hierarchical filtering preserves parent/
child relationships and visibility rules; the UI is usable at narrow and wide
fence sizes; live device testing has no mount, unmount, or data-loss regression.

## Post-stabilization backlog

These items follow the current PR sequence. Reprioritize only when new evidence,
a reproducible bug, or user feedback justifies it.

| Priority | Topic | Reference | Intended outcome |
|---|---|---|---|
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
