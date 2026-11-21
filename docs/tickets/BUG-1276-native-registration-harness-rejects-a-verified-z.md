---
id: BUG-1276
title: Native registration close command differs from the strict Windows harness
status: fixed
kind: tooling
stage: Tools
reported: 2026-11-13
source: native-Windows-0.7.13-qualification
role: both
versions: 0.7.13.0-ga-editor-preview.15
fixed_in: unreleased
branch: sync/upstream-0.7.13-editor-main
commits: e620f838300c6996f1dcaa7c4fbdf264c5cf1439, 7a129c0d546470c87316b907c57b7679885fbfcc
tests: tools/tests/coop_adventure.py; tests/adventure_registration_tool_tests.py
signature: registration dialog has no unique enabled dismissal control
docs: docs/reports/UPSTREAM-0.7.13-SYNC.md
related: BUG-1259
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

CORRECTION (2026-11-20): the verified command0/direct-Galaxy variant is now
implemented with exact identity and stale-window checks, alongside the original
warning path. Final native qualification below passes; unrelated modals remain
rejected.

## Verification
FAIL: out/windows-lab/upstream0713_native/result.json and49-commands.json,
source 99fa3d01, DLL 53c49fd87a32bb8dd86f1f2135c574b660ac1ade67ca3a55ae8eee60c8ff46fc.
The bounded diagnostic probes are not passing editor or smoke runs. No native
editor movement, recovery or gameplay assertion was reached. Prerequisite and
launcher failures remain tracked by BUG-1259 and GitHub issue7.

PASS: out/lab/20261120-193146-windows-3a1fc7/results.json, source670f8f91,
276.06 seconds, both native faults0 and clean crash/hook/log gates. The final
official14-case aggregate also passes on the source-equivalent670/7a cohort.

## History
- 2026-11-13: opened from native qualification. Initial ticket slug said verified;
  the command 0 behavior remains unclassified and no strict guard was relaxed.
- 2026-11-20: Reproduced the strict failure on the integrated 0.7.15 intermediate
  payload, then verified the real native close in two different cold private
  profiles. Its vtable slot0x20 matches named UTFWin::Window::GetCommandID and
  reads object+0x80; the zero is real native data, not a diagnostic ABI mismatch.
  A fresh, identity-checked ordinary click removes the registration root and
  reveals verified Galaxy controls directly, with no warning. The earlier
  warning-only probe therefore did not establish failed dismissal. Added only
  this exact embedded-browser control family, retaining command3/warning support
  and all modal/stale guards. Twenty-three startup contracts pass; the real
  fixed helper also reaches verified Galaxy controls in a cold hidden game.
  Final committed-payload startup and smoke acceptance remain required before
  closing the issue. Evidence: out/windows-lab/native0715-startup,
  native0715-registration-probe2, native0715-registration-probe4,
  native0715-startup-fixed and intermediate-payload.json in the isolated
  windows-native-qualification worktree.


Legacy fork reference: `BUG-1031` before upstream0.7.15 integration. Prior aliases and historical commit/OUT identities remain unchanged; upstream owns its independently created ticket under that number.


## Final native verification (2026-11-20)

The exact command0/direct-Galaxy variant, original warning variant and stale
modal guards pass their contracts and real cold starts. Native smoke UI run
20261120-193146-windows-3a1fc7 passes in276.06s, faults0 with clean crash/hook/log
gates. All14 original native smoke rows now pass. The original rejected-control
and unrelated-modal failures remain in the report. No unknown modal is ignored.
GitHub issue10 can close after the qualified source reaches main.
