---
id: BUG-0535
title: Effect clone fixture incorrectly requires the source random seed
status: fixed
kind: tooling
stage: GA native fixtures
reported: 2026-11-04
source: C68 actual Effect clone companion audit
role: host
versions: C68 native fixture
fixed_in: unreleased
branch: feat/ga-editor-multi-act
commits: 9f94bbd
tests: coop_editor_terrain_stamp_checks.py --self-test; eight captured native cases
signature:
docs:
related: BUG-0530
---

## Report
Host Effect Clone189 commits one new record, retaining all181 old rows exactly, but the fixture fails because the appended03DD8E59 seed differs from source175. Evidence: out/preview9-c68-host-effect-clone/result.json. No native failure or rejected transaction occurs.

## Root cause
The source seed is7940402e; the appended seed676a3b72 exactly equals current Planet02A907B7. Verified F00D30 -> F33540 creates a preview noun and copies only spatial position/rotation/scale. On finalization F381D0 -> F37240 loads current Planet02A907B7 through its property getter into EBX and passes that value through F37313/F37328 to F35910, which appends the seed at tool64/68. It does not copy the source record seed. The initial new-random-seed interpretation is superseded: this path uses the current planet seed. Static audit: out/preview9-c68-effect-clone-seed-audit.

## Fix
The repository tools/tests/coop_editor_terrain_stamp_checks.py assertion now requires the exact current planet seed, exact retained old columns and the source's other scalar/key companions. Transforms retain the native movement result. No product behavior changes. Original failed artifact remains retained.

## Verification
FAIL: original host artifact above. PASS: out/preview9-c68-host-effect-clone-qualified/result.json qualifies its exact append and Undo190/Redo191/Undo192. Both complete native and authored semantic SHA restore exactly on both peers, one history item, zero faults. Portable checker self-tests pass4 cases including wrong seed, mutated old rows, unrelated fields and malformed columns. The actual host/guest append artifacts pass through that permanent checker. Guest independent Clone193/Undo194/Redo195/Undo196 also passes: out/preview9-c68-guest-effect-clone/result.json.

## History
- 2026-11-04: opened.

Legacy fork reference: `BUG-0484` before upstream 0.7.4 integration. Existing commits and OUT receipts retain that historical ID; upstream owns its independent ticket under that number.
