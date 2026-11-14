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

Status snapshot for 2026-11-14. Open pull requests are proposals, not shipped
features.

### Merged into `main`

- **PR #18 — keyed ListModel synchronization.** Uses keyed lookups to reduce
  repeated linear searches while preserving existing model rows and delegates.
- The GitHub Actions build-and-test workflow is present in `main`.

### Open work

| Priority | Work | Current reference | Next action |
|---|---|---|---|
| P1 | Background directory validation | [PR #14](https://github.com/bezeza/Alveus/pull/14) | Review behavior, tests, and current CI; merge only when verified |
| P1 | Cached placement and targeted model updates | [PR #15](https://github.com/bezeza/Alveus/pull/15) | Review cache invalidation and stale-file handling; verify with large folders |
| P2 | Offscreen UI harness and QML warning checks | [PR #17](https://github.com/bezeza/Alveus/pull/17) | Align Plasma mocks with the real APIs and make the harness reliable |
| P2 | Runtime diagnostics and agent workflow | [PR #19](https://github.com/bezeza/Alveus/pull/19) | Finish review, verify privacy/rotation, and pass CI |
| P2 | Icon, translations, previews, Fedora RPM workflow | [PR #20](https://github.com/bezeza/Alveus/pull/20), [PR #21](https://github.com/bezeza/Alveus/pull/21) | Consolidate overlapping work into one coherent change; fix the current UI-preview failure before considering it complete |

PR #20 and PR #21 are based on the diagnostics branch and overlap in
branding, translations, CI, README, and UI test support. Do not merge both
blindly. Keep one authoritative implementation and carry over only the
non-duplicated, verified changes.

PR #21's latest recorded CI run failed in the UI-preview step. The build,
core tests, shell checks, translation validation, and QML lint completed, but
the preview harness reported mock/API mismatches (including `DropArea`,
`WidgetsHost`, and `i18n`). Fix the cause in the harness/mocks; do not
suppress the warnings or mark the preview as successful without a rendered
result.

PR #16 is not a separate release gate: a build-and-test workflow is already
present in `main` through the merged work. Compare it for any unique changes
before closing or superseding it; do not merge the stale workflow blindly.

The original successful CI runs linked in PR #14, PR #15, and PR #17
were attached to earlier SHAs. On 2026-11-14, all three branches were synced
with the current `main` after PR #18; fresh GitHub Actions runs were then
observed in progress on the updated heads. Do not merge until those current
runs complete successfully. A successful run on an earlier commit is not
current verification.

A review of PR #15 found a cache-invalidation gap in `mergeFences()`: source
fences inherit the target's activity scope, but the subsequent tab activation
does not rebuild the per-activity file-model caches. Add coverage for merging
fences with different activity scopes and verify cached file placement in both
activities before merging PR #15.

## Near-term execution plan

Complete these stages in order. Independent PRs may proceed separately, but
dependent work must be based on the correct updated branch.

### Stage 1 — Responsiveness and filesystem performance

- [ ] Finish and validate PR #14: move expensive directory validation away
  from the UI thread where required.
- [ ] Finish and validate PR #15: cache file placement data and update only
  affected fence models.
- [ ] Test cache invalidation after file creation, rename, move, deletion,
  mount/unmount, and changes made outside Alveus.
- [ ] Exercise large folders and repeated filesystem events; check for stale
  entries, duplicate rows, excessive rescans, and UI stalls.

**Exit criteria:** no known UI-thread filesystem bottleneck in the changed
paths; correct model contents after external changes; focused regression tests
and current CI pass.

### Stage 2 — Reliable UI regression checks

- [ ] Complete PR #17 and integrate its UI harness with the repository's
  standard test workflow.
- [ ] Make test doubles model the actual Plasma properties and signals used by
  Alveus.
- [ ] Treat unexpected QML warnings and failed screenshot renders as failures.
- [ ] Keep offscreen screenshots reproducible and clearly label them as test
  renders, not screenshots of a live Plasma session.

**Exit criteria:** core tests and UI harness pass on the current PR head;
unexpected QML warnings are fixed rather than hidden; screenshot artifacts are
produced only after successful rendering.

### Stage 3 — Diagnostics and contributor guardrails

- [ ] Complete PR #19.
- [ ] Verify severity, timestamp, category, and source context for recorded
  warnings/errors.
- [ ] Verify bounded log rotation and path redaction; review copied reports for
  possible third-party paths before sharing.
- [ ] Keep diagnostics local and user-controlled; no automatic upload or
  telemetry.
- [ ] Establish `AGENTS.md`, provider-specific pointers, PR template, and
  `docs/DEVELOPMENT_LOG.md) as the common engineering workflow.

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
