---
id: BUG-0614
title: First Terraform clone and wheel input is dropped pending ownership
status: fixed
kind: bug
stage: GA shared editor
reported: 2026-11-03
source: Private C63 native action coverage
role: host
versions: 0.7.5.0-ga-editor-preview.9
fixed_in: unreleased
branch: feat/ga-editor-multi-act
commits: 55b4a82
tests: tests/editor_native_cancellation_windows_tests.cpp; editor_native_cancellation_windows_tests --terrain-first-use-only; C68 eight native sequences
signature: Terraform pending ownership retains only action1; investigate actual action6/11 producers
docs:
related:
---

## Report
C63 host Alt-clone and Shift+wheel probes on Effect180 produce no transaction atR116. Exact native bytes, semantic projection, canonical hash and history remain unchanged, with zero faults. The wheel probe visibly changes camera zoom, so its actual Native scaling route must be qualified before attributing that probe to ownership loss. Evidence: `out/preview9-c63-terrain-host-effect-clone`, `out/preview9-c63-terrain-host-effect-wheel` and `out/preview9-c63-terrain-first-action-failures`.

## Root cause
Verified denied actions6/11 return without queuing; only action1 was retained. Stock UIMachineScenarioEdit first enters scale mode, then emits action6 for subsequent wheel events and uses separate virtual28 EFC690 on Shift release. Alt-drag emits action11 through F028F0 to native F00D30. Corrected ordinary WM_MOUSEWHEEL input includes MK_SHIFT; the old SDK helper omitted it (BUG-0615). This is distinct from BUG-0613 Delete. C67 retention initially also intercepted already-started wheel gestures, dropping later quanta; C68 routes active gestures through the existing native path.

## Fix
Retain a value-only target capsule with scene/source/generation, project/epoch/participant, act, current tool/context/descriptor/actor/definition/index and record revision. Queue at most64 exact raw wheel quanta, retain clone release and native modifiers, and recheck after callbacks/grant. No native allocation, history or scale mutation occurs before ownership. Separate scale finalization retains a quick Shift release. Escape, focus loss, denial, stale identity, expiry, invalid values and queue overflow retire pending input. Original clone/tick/release and scale/finalizer execute only after grant; borrowed latches restore only to the same scene. This is private candidate68, not a personal installation or public release.

## Verification
FAIL: out/preview9-terrain-first-use-red and out/preview9-terrain-first-use-continuation-red; PASS: out/preview9-terrain-first-use-green-r3 and out/editor-preview9-terrain-first-use/receipt.json.

Controlled strict-i686/O1 RED: out/preview9-terrain-first-use-red/run.log fails retained-first-input assertion when only the new entry is disabled. Continuation RED: out/preview9-terrain-first-use-continuation-red/run.log reproduces C67 losing a post-grant wheel quantum. Final GREEN: out/preview9-terrain-first-use-green-r3/run.log passes553 first-use checks; Delete216, drag195 and full adapter155,260 also pass. Actual C68 Model wheel passes host164-168 and guest168-172; native Alt-clone passes host172-176 and guest176-180. Complete native SHA and authored semantic SHA restore exactly through Undo/Redo/Undo, one history item, zero faults. BUG-0618 records the explicitly excluded monotonic allocation counters. Effect wheel180-184/184-188 and Clone188-192/192-196 also pass; BUG-0619 corrects the independently verified planet-seed companion assertion. exact Native ABI119 passes. A fixture initially expected the allocation double's101 after its movement double correctly writes99; that failed setup is preserved and corrected. Receipt: out/editor-preview9-terrain-first-use/receipt.json.

## History
- 2026-11-03: opened.

Legacy fork reference: `BUG-0479` before upstream 0.7.4 integration. Existing commits and OUT receipts retain that historical ID; upstream owns its independent ticket under that number.

Legacy fork reference: `BUG-0530` before upstream 0.7.8 integration. Historical commits and OUT evidence retain that ID; upstream owns its independent ticket under that number.
