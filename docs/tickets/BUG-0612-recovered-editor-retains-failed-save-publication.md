---
id: BUG-0612
title: Recovered editor retains failed Save publication hold
status: fixed
kind: bug
stage: GA shared editor
reported: 2026-11-03
source: Private C62 Terraform validation
role: host
versions: 0.7.5.0-ga-editor-preview.9
fixed_in: 0.7.5.0-ga-editor-preview.9 (private C63)
branch: feat/ga-editor-multi-act
commits: b120545
tests: editor_runtime_windows_tests.exe --terrain-transition-only; --recovery-lifecycle-only; --complexity-only; --projection-scope-only
signature: save_publication_pending survives finish_recovery; gesture/projection scopes empty
docs: docs/re/adventure-editor-history.md#editor-failed-save-recovery-hold
related:
---

## Report
Private C62 Terraform probes reached a native Save consistency rejection at revision 90. Same-party Recover restored exactly the durable Planet fields and both peers reported Ready with zero faults, but Model and Effect dragging then obtained a terrain lease without starting native history or changing the draft.

Evidence: `out/preview9-c62-model-probe-save-guard-preserved/`, `out/preview9-c62-recover-model-probe/result.json`, `out/preview9-c62-model-held-move-diagnostic/result.json`, and `out/preview9-c62-post-recovery-effect-move/result.json`.

## Root cause
`saved_native` sets `save_publication_pending` before normalizing/publishing the completed native Save. Its recoverable consistency failures retain that hold, as intended. `finish_recovery` publishes the recovered checkpoint and resets `recovery` and `frozen`, but leaves the old Save hold behind. Both current_gesture_scope and current_projection_scope then return empty identities. The latter also causes recurring Extended Complexity original-editor diagnostics. The cause of the earlier native Save recipe mismatch remains unassigned; this ticket addresses the blocked recovered session.

## Fix
Retire the superseded Save publication flag and its terrain/cancellation state only after the new recovery checkpoint is durable and its scene is verified. Retain them while recovery is waiting or fails. No runtime override.

## Verification
Native FAIL: C62 artifacts above. A Win32 Runtime regression induces the native Save normalization failure, delays recovery readiness, requires the old checkpoint/hold to survive, then requires both restored scopes and subsequent edits from both roles. Controlled RED exits1 at the stale-hold assertion: `out/preview9-save-recovery-red-run.log`. Strict-i686 GREEN passes 3,772 terrain-transition, 2,014 recovery, 1,564 complexity-consensus and 190 projection-scope checks: `out/editor-preview9-save-recovery/receipt.json`. Default and Extended modes both exercise subsequent edits from both roles; failed/deferred recovery retains the hold. An initial GREEN fixture stopped on an incorrect manual-join assumption after automatic rejoin and remains retained. The complexity diagnostic assertion was updated from policy1 to the arena-safe policy2.

Private C63 retains 323 production inputs without drift. Fresh native recovery restores R90 exactly and both-role Model movement/checked history subsequently pass. Native failed-Save/same-process recovery acceptance remains to be repeated.

## History
- 2026-11-03: opened.

Legacy fork reference: `BUG-0477` before upstream 0.7.4 integration. Existing commits and OUT receipts retain that historical ID; upstream owns its independent ticket under that number.

Legacy fork reference: `BUG-0528` before upstream 0.7.8 integration. Historical commits and OUT evidence retain that ID; upstream owns its independent ticket under that number.
