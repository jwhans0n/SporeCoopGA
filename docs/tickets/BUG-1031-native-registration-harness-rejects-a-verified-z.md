---
id: BUG-1031
title: Native registration close command differs from the strict Windows harness
status: open
kind: tooling
stage: Lab
reported: 2026-11-13
source: native-Windows-0.7.13-qualification
role: both
versions: 0.7.13.0-ga-editor-preview.15
fixed_in:
branch: sync/upstream-0.7.13-editor-main
commits:
tests: tools/tests/coop_adventure.py; tests/adventure_registration_tool_tests.py
signature: registration dialog has no unique enabled dismissal control
docs: docs/reports/UPSTREAM-0.7.13-SYNC.md
related: BUG-1014
---

## Report
The integrated hidden Windows games load the supported GOG LAA image and
SporeKit 0.3.16. Before a focused editor check can begin, the strict native
registration helper rejects the current modal's close control. This blocks
qualification; it is not evidence that ordinary player registration is broken.

## Root cause
Observed, unclassified: the shallow, scene-owned tree reports the expected
registration modal, Login0519E168 and content0451A4C0, but closeFFFFFFF2 has
command 0 rather than the command 3 required by the existing harness. The earlier
full-tree startup capture reported3. No callable ABI or new command value is
promoted from this observation. Timing, native initialization and the scoped
diagnostic path require comparison before changing the acceptance policy.

## Fix
Pending. Preserve strict ownership, complete modal identity and revalidation
before clicking. Do not accept arbitrary close controls or fixed screen
coordinates in the qualification harness.

## Verification
FAIL: out/windows-lab/upstream0713_native/result.json and49-commands.json,
source 99fa3d01, DLL 53c49fd87a32bb8dd86f1f2135c574b660ac1ade67ca3a55ae8eee60c8ff46fc.
The bounded diagnostic probes are not passing editor or smoke runs. No native
editor movement, recovery or gameplay assertion was reached. Prerequisite and
launcher failures remain tracked by BUG-1014 and GitHub issue7.

## History
- 2026-11-13: opened from native qualification. Initial ticket slug said verified;
  the command 0 behavior remains unclassified and no strict guard was relaxed.
