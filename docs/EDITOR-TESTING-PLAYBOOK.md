# Adventure Editor testing playbook

Use focused checks during feature development. Reserve the long acceptance
matrix for an explicitly requested overnight run against a frozen release
candidate. A development preview can remain under validation while implementation
continues; pending release gates must stay visible.

## Test tiers

| Tier | Trigger | Scope |
| --- | --- | --- |
| Development | A relevant code change | Rebuild/run affected deterministic tests and one focused two-player native reproduction when game behavior changes. Aim for minutes of feedback. |
| Integration | A coherent feature is ready for review, or a shared interface changes | Portable suite, affected Win32 adapters, two-player save/Test/Return and a focused four-player check of the changed behavior. |
| Release acceptance | Explicit overnight run before a user-facing release | Complete automated suite, two/four-player adversity, lifecycle, cold recovery, actual bulk transfers, exclusive FPS and sustained memory. |

These are workflow targets, not measured durations. A failing changed behavior
returns to focused debugging. Do not automatically expand each fix into another
full release sweep. Repository requirements for `make test` and lab smoke before
merging to `main` still apply.

## Development loop

Choose the smallest fixture covering the changed behavior. The individual Win32
executable targets in the Makefile build incrementally; `make test-editor` is a
broader portable sweep that recompiles its fixtures. For example, a document or
history change can build its focused test in the configured Linux/WSL environment:

```sh
make B=out/editor-dev out/editor-dev/host/editor_document_tests.exe
```

Run the executable on Windows from this worktree:

```powershell
& .\out\editor-dev\host\editor_document_tests.exe
```

Building `test-editor-windows` alone does not execute the tests. Rebuild affected
targets rather than using stale passing binaries. Protocol, persistence, runtime
and native ABI changes need their corresponding fixtures. Python harness-only
changes normally need pure self-tests and a focused reproduction, rather than a
rebuilt game DLL.

For a native change, use two owned, hidden, muted profiles and the relevant narrow
stage exposed by the harness's `--help`. Do not default to `all`, four players or
repeated cycles. The existing entry points are
`tools/tests/coop_adventure_editor.py`, `coop_editor_lifecycle.py`,
`coop_editor_save.py` and `coop_editor_party.py` in that same directory.

Preview 3 adds `tools/tests/coop_editor_native_flow.py` for short automatic
sharing/native-control checks. It defaults to preparation only and never starts
or deploys games. Use an existing owned hidden/muted cohort, its process metadata
and exact DLL hash with `--execute`. Run `empty` with two instances, `toolbar`
with four, then `handoff` with the original pair and the same `--fixture-tag`.
The toolbar stage saves two local test creations that the handoff stage reopens.
Output directories must be new. `--onboard-guest` is only for the native game's
first-use captain gate in a fresh toolbar profile. This is an integration fixture,
not the sustained overnight playbook.

Record the result, continue development, and run integration checks at a coherent
checkpoint. Repeat unrelated checks only when a change, failure or unresolved
concern justifies doing so.

## Overnight release playbook

Freeze the commit, DLL SHA-256, verified native maps, fixture identities and
configuration. Record which previous evidence remains applicable.

1. Verify the supported executable, matching binaries/maps, isolated profiles,
   hidden windows, muting and owned PIDs. Closing personal games requires prior
   authorization. Exclusive process cohorts are needed for measured FPS/memory,
   not every native functional check.
2. Run `make test`, execute the complete Win32 editor/GA suites and run the
   required lab smoke. Save build/source manifests and machine-readable results.
3. Test advertised placements and checked Undo/Redo with two, then four players:
   disjoint/conflicting edits, invalid gesture cancellation, native Save As,
   cold reopen/recovery and reconnects.
4. Test readiness, immutable snapshots, natural success/failure, Done/Replay,
   loading/preparation cancellation and disconnects. Verify draft/history,
   local camera/selection and captain progression across repeated Test/Return.
5. Test LAN/WAN delivery, loss, duplicates and reordering. Measure real cold
   content imports under an aggregate 2 Mbit/s host link while established
   authors edit. Count retransmissions, queue delays and per-peer bytes.
6. Separately measure large-editor two/four-player FPS and sustained memory.
   Fill the documented bounded caches before admitting the memory baseline.
   Keep raw samples and normal cleanup evidence. Completed cycles alone do
   not establish a retained-memory plateau.
7. Review each gate as passed, failed, pending or interrupted. Normally return
   owned games to Galaxy and close them. Update the
   [validation report](reports/EDITOR-IMPLEMENTATION-VALIDATION.md).

Keep the existing numeric criteria in the
[implementation plan](MULTIPLAYER-ADVENTURE-EDITOR-PLAN.md#validation-and-performance-gates).
Deferring release checks does not relax their budgets or imply a release pass.

This is an operator playbook. The versioned harnesses provide individual stages;
some long-run orchestration and aggregate-link drivers currently exist only as
reviewed local `out/` evidence. A portable, one-command unattended runner is not
yet shipped. Promoting those drivers requires fixture preflight, bounded stage
timeouts, safe stopping between owned stages, resumable receipts and an aggregate
report. Interrupted runs must retain that status, and game termination must never
be reported as successful native cleanup.
