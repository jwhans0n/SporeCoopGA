---
id: BUG-0479
title: First Terraform clone and wheel input is dropped pending ownership
status: open
kind: bug
stage: GA shared editor
reported: 2026-11-03
source: Private C63 native action coverage
role: host
versions: 0.7.5.0-ga-editor-preview.9
fixed_in:
branch: feat/ga-editor-multi-act
commits:
tests:
signature: Terraform pending ownership retains only action1; investigate actual action6/11 producers
docs:
related:
---

## Report
C63 host Alt-clone and Shift+wheel probes on Effect180 produce no transaction atR116. Exact native bytes, semantic projection, canonical hash and history remain unchanged, with zero faults. The wheel probe visibly changes camera zoom, so its actual Native scaling route must be qualified before attributing that probe to ownership loss. Evidence: `out/preview9-c63-terrain-host-effect-clone`, `out/preview9-c63-terrain-host-effect-wheel` and `out/preview9-c63-terrain-first-action-failures`.

## Root cause
Static adapter gap: denied Terraform actions6/11 return without queuing; only action1 creates a held input. The actual Native clone and wheel-scale command producers, including modifier propagation and native finalization, require focused qualification. This is distinct from BUG-0478 Delete.

## Fix
Open: retain verified first-use clone/scale input under source ownership without allocating a native clone or mutating scale before grant. Preserve modifiers, movement/release and cancellation fences. No change is enabled yet.

## Verification
Native failed probes and exact unchanged-state receipt above are retained. Controlled RED/GREEN and actual command qualification remain pending.

## History
- 2026-11-03: opened.
