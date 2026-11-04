---
id: BUG-0532
title: Two hidden native games hang in presentation reset
status: open
kind: freeze
stage: Win32 lab rendering
reported: 2026-11-04
source: Private C65 Terraform control validation
role: both
versions: private C65 SporeKit 0.3.6
fixed_in:
branch: feat/ga-editor-multi-act
commits:
tests:
signature: both main threads wait in d3d9 presentation reset; zero native faults
docs:
related:
---

## Report
After host Effect Delete/Undo/Redo/Undo passes through durableR138 in C65, a guest selection probe stops before deletion. A later status probe times out. Both watchdogs report no frames for30s at00:15:58/59 on2026-11-04, with zero native faults.

## Root cause
Both sampled main-thread stacks include d3d9.dll waiting beneath Graphics::RenderUtils::SetPresentationParameters/Renderer_SetVideoMode. The trigger is unassigned. This does not establish a Delete or ownership defect. Both processes remain alive; main-thread console work cannot complete. NVIDIA overlay is present in the recorded environment.

## Fix
No graphics or editor product fix is claimed. Retain both reports/logs/archives and the durable recovery directories. Retire only the exact owned hidden test processes through their runner, then resume focused checks in fresh processes. Personal games and installation remain untouched.

## Verification
Evidence: `out/preview9-c65-presentation-hang-preserved/` has the copied reports/logs/archives. The first live recovery-copy attempt encounters the exclusive project.lock; no recovery data is changed. After owned process retirement, `out/preview9-c65-presentation-hang-closed/receipt.json` verifies all six/four recovery files before/after copying. Last directly verified canonical/durable revision is138. Fresh unchanged C65 processes recover revision138 with exact Planet values and strict two-peer convergence: `out/preview9-c65-r2-recover-baseline-c/result.json`. The graphics trigger remains unassigned.

## History
- 2026-11-04: opened.

The next run starts at1600x1000@165 instead of1024x768@60 because the native
resolution enumeration changed: old index11 now names1600x1000. The measured
native Settings selection1024x768@60 persists index2. The running window does
not resize immediately; restart with index2 and verify both owned HWND client
rectangles after recovery. The initial navigation's source-control fence stops
before more input. Exact calibration evidence:
`out/preview9-c65-resolution-calibration-receipt.json`. This establishes changed
display enumeration, not the original D3D-reset trigger.

Legacy fork reference: `BUG-0481` before upstream 0.7.4 integration. Existing commits and OUT receipts retain that historical ID; upstream owns its independent ticket under that number.
