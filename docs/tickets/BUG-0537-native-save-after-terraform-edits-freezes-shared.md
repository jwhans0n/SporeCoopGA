---
id: BUG-0537
title: Native Save after Terraform edits freezes shared checkpoint on recipe transition
status: fixed
kind: bug
stage: GA shared editor
reported: 2026-11-04
source: C68 Road placement R251 native Save and prior C62 R90
role: host
versions: private preview9 C68
fixed_in: unreleased
branch: feat/ga-editor-multi-act
commits: 517b0d8c
tests: tests/editor_native_terrain_transition_fixture.h; tests/editor_runtime_terrain_transition_fixture.h
signature: The native terrain recipe changed during its owned transition
docs:
related:
---

## Report
After successful both-role Road placement/history, guest Redo251 and normal native Save complete locally but freeze shared checkpoint publication. Both participants retain durable251 with zero native faults. Evidence: out/preview9-c68-road-native-save and out/preview9-c68-road-save-frozen. This reproduces the earlier unassigned C62 R90 Save rejection.

## Root cause
Save normalization passes before checkpoint_resources restores the normalized native image. That restore changes native raw transforms before resume_terrain_hold checks the old rendered recipe. Save publication has intentionally emptied current_projection_scope, so ordinary materialization ownership cannot carry this new proof. Read-only comparison with the exact original saved DBPF finds28 Effect and93 Model transforms changed only in counter/XYZ; every key/scalar companion remains exact. No tolerance or offset-based normalization is justified. Evidence: out/preview9-c68-road-save-raw-recipes-r2.

## Fix
Carry an exact completed-Save receipt through canonical restoration with a stack-scoped owned read identity. Seed only its retained exact materialization map, observe the synchronous native rebuild, verify the resulting recipe and complete native readback against the saved canonical document, then issue a fresh one-use ticket. Reject changed owners, stale receipts, a different authored recipe, unknown transform changes and allocation failures. Runtime checkpoint restoration uses this adapter before durable publication. Ordinary solo/edit scopes remain closed during Save. No float tolerance or authored-field normalization is added.

## Verification
Native FAIL: out/preview9-c68-road-native-save. Native Save and genuine original saved-resource acquisition succeed; shared SavedApplied fails because the guarded checkpoint freezes. No successful Save is claimed for that C68 attempt. Recovery subsequently restores every committed Planet field at251; both games return normally to Galaxy with zero faults.

Controlled FAIL: out/preview9-saved-restore-red/run.log restores without carrying materialization and fails the exact raw comparison. Strict-i686/O1 PASS: out/preview9-saved-restore-green-o1/run.log, 4,801 focused checks across success and thirteen rejection/callback cases; full-adapter.log passes160,106 checks. Runtime PASS: out/preview9-saved-restore-runtime-o0, transition3,774, scene-alias11,193, recovery2,028, complexity1,564 and saved-root158 checks.

Actual C69 PASS: out/preview9-c69-road-native-save verifies SavedApplied2/2 at251, exact all authored root payloads and history, genuine original saved DBPF, and zero faults. A subsequent guest edit/Undo initially hits raw unused-prefix-word differences, while an independent full authored payload audit passes with no root metadata changes: out/preview9-c69-first-post-save-undo-audit. Its complete repeat253-257 passes own raw SHA and authored semantic Undo/Redo/Undo on both peers: out/preview9-c69-guest-after-save-r2. The initial strict fixture failure is retained. Host257-261 also passes exact post-Save atmosphere history; repeated Save261 passes SavedApplied2/2, all authored root payloads and history: out/preview9-c69-road-native-save-repeat. Fresh native cold opening succeeds, but strict decoded equality fails separately under BUG-0538 before hosting. Broader release testing remains deferred; personal payload unchanged.

## History
- 2026-11-04: opened.

Legacy fork reference: `BUG-0486` before upstream 0.7.4 integration. Existing commits and OUT receipts retain that historical ID; upstream owns its independent ticket under that number.
