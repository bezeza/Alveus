# Alveus — mandatory rules for every coding agent

This is the canonical repository-wide instruction file for **all AI coding agents**
and automated contributors, regardless of vendor or editor. Read it before
analysing, planning, editing, testing, committing, or opening a pull request.
Provider-specific instruction files only point here; they must not weaken or
contradict these rules.

If a deeper directory contains another `AGENTS.md`, read that file too. When
instructions conflict, follow the user's latest explicit request first, then
this file, then local code conventions.

## Product and architecture

Alveus is a KDE Plasma 6 desktop containment that groups desktop files into
fences. Its core is a Qt C++ QML module (`ua.bezeza.alveus`); the desktop UI is
QML. Preserve the existing architecture and user-facing behavior.

- `core/`: Qt C++ core and integrations. Keep the core Qt-only; do not add KDE
  library dependencies without an explicit architectural reason.
- `package/contents/ui/`: Plasma/QML interface. Route visible strings through
  `i18n()`; update translations in `po/` when adding or changing user-facing
  text.
- `tests/`: core tests and the offscreen UI harness/mocks.
- `packaging/`, `install.sh`, `uninstall.sh`, `recover.sh`: distribution
  and desktop integration. Treat these as high-risk paths.
- `docs/DEVELOPMENT_LOG.md`: permanent engineering history.
- `docs/DIAGNOSTICS.md`: runtime diagnostic logging behavior and privacy notes.

The user's desktop and files are valuable data, not disposable test fixtures.
Never move, delete, overwrite, or reorganize real user files as part of tests.
Use temporary directories and test fixtures.

## Mandatory task protocol

### 1. Inspect before changing

Before editing:

1. Read this file and the relevant project documentation.
2. Inspect the current branch, working-tree status, existing diff, and relevant
   files. In a local workspace, preserve all pre-existing user changes.
3. Inspect related tests and recent CI/PR results. Do not assume an old green run
   validates newer commits.
4. State the task's scope and non-goals for non-trivial work.

Never discard changes with `reset --hard`, `clean -fd`, broad revert, or
equivalent destructive commands. Never overwrite unknown/uncommitted work.

### 2. Keep changes isolated

- Work on a dedicated branch; never commit directly to `main`.
- Use one focused task per branch/PR. Prefer prefixes such as `feat/`,
  `fix/`, `perf/`, and `docs/`.
- Keep diffs narrow. Do not mix unrelated refactors, formatting sweeps, or
  speculative features into a bug fix.
- Do not force-push, rewrite shared history, or merge a PR unless the user
  explicitly authorizes that action.
- Do not install Alveus, change the active Plasma layout, restart
  `plasmashell`, or modify desktop/session settings without explicit user
  approval.

### 3. Fix causes and add regression coverage

- Reproduce a reported bug or identify concrete source/test evidence before
  changing code. Do not patch guesses as if they were confirmed causes.
- Fix the violated invariant, not only the visible symptom.
- Add or update a regression test for every reproducible bug fix when practical.
- Handle errors explicitly. Do not silently swallow failures or report success
  when an operation only partially completed.
- Keep public APIs and settings backward-compatible unless the task explicitly
  requires a migration. Add migration tests when changing persisted formats.
- Do not introduce new warnings. Treat compiler, QML, and test warnings as
  issues to investigate, not output to hide with broad suppressions.

### 4. Runtime diagnostics and error handling

When adding or changing runtime behavior:

- Use Alveus' diagnostic system when it is present. Record actionable warnings
  and errors with severity, category, timestamp, and source context where
  available.
- Add a regression test proving that important new error paths are recorded.
- Do not log successful routine operations as errors. Avoid duplicate or
  high-frequency log spam; rate-limit repeated events where appropriate.
- Never log file contents, clipboard contents, passwords, tokens, private keys,
  environment secrets, or other sensitive user data.
- Redact user-specific paths and identifiers where practical. Do not send logs
  or telemetry to remote services. Sharing/exporting logs must remain a
  deliberate user action.
- Keep log retention bounded and document its location, rotation, and privacy
  behavior in `docs/DIAGNOSTICS.md`.
- Be precise about crash coverage: Qt warning/critical/fatal messages are not
  the same as native crashes, segmentation faults, power loss, or SIGKILL.
  Never claim those events are captured unless a tested crash-reporting
  mechanism actually captures them.

### 5. Build and test before claiming completion

Use the repository's current CI workflow as the source of truth. The standard
local verification sequence is:

```sh
cmake -S . -B build -DALVEUS_BUILD_TESTS=ON
cmake --build build --parallel
dbus-run-session -- ctest --test-dir build --output-on-failure
```

Also run the relevant checks for the touched surface:

- QML changes: run the same `qmllint` command and import paths used by
  `.github/workflows/ci.yml`.
- Shell scripts: run `bash -n` and `shellcheck --severity=error`.
- Packaging/install changes: validate the affected packaging path, including
  `./packaging/build-rpm.sh` when the environment supports it.
- Core behavior: run the focused `coretest` case during iteration, then the
  full CTest suite before handoff.
- UI/Plasma integration: distinguish offscreen/CI validation from a real KDE
  Plasma session. If no real session was tested, say so explicitly.

Do not invent, infer, or copy a test count. Report the actual test output.
Never say a test passed if it was not run on the current code. If a check cannot
run, state why and leave it marked as not run. A failing check must be fixed or
clearly reported; do not hide it.

### 6. Keep the development log current

For every PR that changes implementation, tests, build/CI configuration,
packaging, installation, or runtime behavior:

1. Add a dated entry to `docs/DEVELOPMENT_LOG.md` in the same PR.
2. Record the task/PR, a concise summary of what changed, and the verification
   actually run with its result.
3. Record important limitations or follow-up work.
4. Keep merged work separate from open/in-progress work. Do not list a change
   under “Merged” until GitHub confirms that it is merged.

Do not create noise for read-only questions or inspections that make no
repository changes. For a substantial no-code audit requested as a durable
deliverable, record the audit report and its evidence separately.

When diagnostic behavior changes, update `docs/DIAGNOSTICS.md` as well.
The CI workflow enforces a development-log update for implementation changes.

### 7. Pull request and release discipline

Every implementation PR must:

- explain the problem, approach, scope, and any compatibility implications;
- list the exact build/test/lint commands run and their results;
- identify checks that were not run and why;
- include known risks and manual Plasma validation still required;
- update `docs/DEVELOPMENT_LOG.md`;
- contain no secrets, personal data, generated build output, or unrelated files.

Keep a PR in draft until the implementation is complete and its required CI
checks pass. Do not merge or publish a release without the user's explicit
approval. A green CI run is necessary, not proof that every real Plasma
interaction works.

### 8. Required handoff format

Finish each implementation task with a concise report containing:

1. **Status:** completed, partially completed, or blocked.
2. **Changes:** files/features changed and the relevant commit/PR link.
3. **Verification:** exact checks run and pass/fail/not-run results.
4. **Known limitations:** unresolved issues and manual tests still needed.
5. **Development log:** confirm the dated entry was added.

Answer the user in Ukrainian by default unless they request another language.
Keep code, comments, commit messages, and technical documentation in English
unless the existing file or user request requires otherwise.
