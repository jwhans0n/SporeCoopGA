---
id: BUG-0480
title: Virtual wheel messages omit held modifier flags
status: open
kind: tooling
stage: Win32 native fixtures
reported: 2026-11-04
source: Terraform Shift wheel qualification
role: test
versions: private SporeKit input helper
fixed_in:
branch: feat/ga-editor-multi-act
commits:
tests:
signature:
docs:
related:
---

## Report
The C63 Shift+wheel fixture zooms the camera and cannot qualify native wheel scaling. `input_wheel` constructs WM_MOUSEWHEEL with MAKEWPARAM(0,delta), omitting MK_SHIFT/MK_CONTROL even while virtual modifiers are held.

## Root cause
Observed helper code in sporekit/input.cpp supplies zero mouse modifier bits. Stock UIMachineScenarioEdit explicitly requires SHIFT for the scale transition. Whether correcting the messages exposes a separate adapter ownership bug still requires a qualified actual run.

## Fix
No product input change yet. A targeted fixture may send ordinary Win32 wheel messages with MK_SHIFT only to the verified hidden game HWND, while the existing exclusive virtual keyboard supplies Shift. Do not interpret the old wheel probe as a gameplay/editor failure.

## Verification
Native unqualified probe: `out/preview9-c63-terrain-host-effect-wheel`. Its exact unchanged native/canonical/history proof is in `out/preview9-c63-terrain-first-action-failures/result.json`. Corrected-input acceptance pending.

## History
- 2026-11-04: opened.
