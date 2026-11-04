---
id: BUG-0533
title: Model stamp deletion freezes on ambiguous terrain import provenance
status: fixed
kind: bug
stage: GA shared editor
reported: 2026-11-04
source: Private C65 actual Model Delete control
role: host
versions: 0.7.5.0-ga-editor-preview.9 private C65
fixed_in: unreleased
branch: feat/ga-editor-multi-act
commits: 091b71b2f6f6d3c773b92058e1f075d7ca33e0ab
tests: tests/editor_native_cancellation_windows_tests.cpp; editor_native_cancellation_windows_tests --terrain-materialization-only; C66 host/guest Model Delete
signature: F37FF0 synchronous F35E10 observes stale deleted-index fresh mark
docs:
related: BUG-0529
---

## Report
Actual host Delete of Model64 at revision144 freezes both private C65 participants before canonical acceptance. Logs report "Native terrain import counters have ambiguous authored provenance", followed by a not-ready snapshot. Durable revision remains144 and both native fault counts remain zero. Evidence: `out/preview9-c65-terrain-host-model-delete-r3` and `out/preview9-c65-model-delete-frozen`. Earlier Model picks stopped before Delete because the fixture clicked a ground position rather than the elevated native icon.

## Root cause
Observed static order: F37FF0 compacts parallel arrays, then calls F35E10 at F381AD before returning at F381C9. Transform compaction uses F33870/537E10 assignment, including its destination counter. The adapter currently calls `authored_terrain_record` before Remove, which forgets that row's witness and marks its index fresh. It shifts/removes fresh marks only after native Remove returns. The synchronous materialization can therefore treat an unchanged successor at the old deleted index as a new authored Update, corrupting its provenance. The precise runtime ambiguous-candidate payload still requires a focused reproduction; the new regression checks this ordering directly.

## Fix
The adapter validates the current descriptor, actor, definition and bounded recipe index without marking that row fresh or forgetting its before-history witness. Existing fresh indices are shifted before entering native Remove and its synchronous rebuild. No float tolerance, ambiguous-source preference or validation bypass is introduced. Actual private C66 host and guest Model Delete acceptance passes.

## Verification
PASS: out/preview9-terrain-remove-green and out/editor-preview9-terrain-remove/receipt.json.

Actual C65 FAIL is retained above. Controlled strict-i686/O1 RED: `out/preview9-terrain-remove-red-r3/run.log` fails on the stale fresh mark at synchronous native entry. GREEN: `out/preview9-terrain-remove-green/run.log` passes73,437 materialization checks. The fixture models native destination-counter assignment and first/middle/final removal for both kinds, asserts exact surviving authored payloads, retains the complete old before-history normalization and leaves the draft untouched. Existing Delete216, drag195 and full adapter154,118 checks also pass in that directory. An initial fixture compile used two different types in one auto declaration; that build failure is preserved separately.

Actual C66 GREEN: host147-150 (`out/preview9-c66-terrain-host-model-delete-r2`) and guest151-154 (`out/preview9-c66-terrain-guest-model-delete-r2`) pass one transaction, exact two-column deletion and complete native/semantic Undo/Redo/Undo restoration on both peers, zero faults. The first corrected host145/146 attempt also completes Delete/Undo without freezing; its raw-host pre-image assertion fails on the separately retained cold-load difference, with exact Planet/semantic restoration. Receipt: `out/editor-preview9-terrain-remove/receipt.json`. Not personally installed or main-merged.

## History
- 2026-11-04: opened.

Legacy fork reference: `BUG-0482` before upstream 0.7.4 integration. Existing commits and OUT receipts retain that historical ID; upstream owns its independent ticket under that number.
