---
id: BUG-0486
title: Native Save after Terraform edits freezes shared checkpoint on recipe transition
status: open
kind: bug
stage: GA shared editor
reported: 2026-11-04
source: C68 Road placement R251 native Save and prior C62 R90
role: host
versions: private preview9 C68
fixed_in:
branch: feat/ga-editor-multi-act
commits:
tests:
signature: The native terrain recipe changed during its owned transition
docs:
related:
---

## Report
After successful both-role Road placement/history, guest Redo251 and normal native Save complete locally but freeze shared checkpoint publication. Both participants retain durable251 with zero native faults. Evidence: out/preview9-c68-road-native-save and out/preview9-c68-road-save-frozen. This reproduces the earlier unassigned C62 R90 Save rejection.

## Root cause
Save normalization passes before checkpoint_resources restores the normalized native image. That restore changes native raw transforms before resume_terrain_hold checks the old rendered recipe. Save publication has intentionally emptied current_projection_scope, so ordinary materialization ownership cannot carry this new proof. Read-only comparison with the exact original saved DBPF finds28 Effect and93 Model transforms changed only in counter/XYZ; every key/scalar companion remains exact. No tolerance or offset-based normalization is justified. Evidence: out/preview9-c68-road-save-raw-recipes-r2.

## Fix
In progress: carry completed-Save authority through canonical restoration and verify its new exact materialization proof before publication.

## Verification
Native FAIL: out/preview9-c68-road-native-save. Native Save and genuine original saved-resource acquisition succeed; shared SavedApplied fails because the guarded checkpoint freezes. No successful Save or recovery is claimed for this attempt.

## History
- 2026-11-04: opened.
