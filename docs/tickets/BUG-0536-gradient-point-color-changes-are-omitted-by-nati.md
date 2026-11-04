---
id: BUG-0536
title: Gradient point color gesture does not reach a shared transaction
status: open
kind: bug
stage: GA shared editor
reported: 2026-11-04
source: C68 live Terraform RGB versus serialized snapshot
role: host
versions: private preview9 C68
fixed_in:
branch: feat/ga-editor-multi-act
commits:
tests:
signature:
docs:
related:
---

## Report
AtR196, native point-color selection/palette procedures produce no canonical transaction or resource-field change. Standalone editing then visibly changes node2 to red; bounded read-only observation finds live tool108 RGB0.6196078658/0.0392156877/0.0549019650 while committed-resource inspection retains old RGB. Both native fault counts remain zero. Evidence: out/preview9-c68-gradient-color-drag-host, out/preview9-c68-gradient-solo-reference, out/preview9-c68-gradient-solo-doubleclick and out/preview9-c68-gradient-live-versus-capture.

## Root cause
Unresolved. CORRECTION (2026-11-04): the preliminary cache-omission explanation is not established. The public inspect command uses capture(resource); live gesture export uses capture_draft, which exports through verified F27C00 into an owned copy. An unshared live-tool/resource difference is expected before export. Static F34380 writes colors108/10C and heights11C/120; EEA090 updates a UI node then synchronously sendsD69290D7, whose EE72FA handler calls Start/F34380/Commit. Same-binary in-place recovery then permits ordinary point selection/family click to commit197, with both live tools updated. Undo198 restores the exact canonical native image. Fresh-panel host198-202, guest202-206 and direct host repeat206-210 all pass exact full native/authored semantic history. The old no-transaction setup remains unexplained; no repeatable adapter bug or cache defect is established.

## Fix
No product fix justified by current evidence. Preserve committedR196 and use owned hidden/muted private scenes for the focused reproduction. No native function is invoked or process memory written by the read-only observations.

## Verification
The earlier no-transaction observations remain retained. Native PASS: out/preview9-c68-shared-gradient-live-steps, out/preview9-c68-point-color-undo198, out/preview9-c68-gradient-color-host-fresh, out/preview9-c68-gradient-color-guest-fresh and out/preview9-c68-gradient-color-host-repeat. Each full sequence changes only B6929C93/node2, one transaction, exact native/authored semantic Undo/Redo/Undo and zero faults. Initial recovery helper stops on a UI precondition after both parties are already ready; the focused live-step probe confirms that committedR196 baseline before input. Keep the historical failure open/unassigned rather than advertising an unproved source fix.

## History
- 2026-11-04: opened.

Legacy fork reference: `BUG-0485` before upstream 0.7.4 integration. Existing commits and OUT receipts retain that historical ID; upstream owns its independent ticket under that number.
