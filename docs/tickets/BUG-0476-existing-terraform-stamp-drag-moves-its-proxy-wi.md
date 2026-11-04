---
id: BUG-0476
title: Existing Terraform stamp drag moves its proxy without a synchronized commit
status: open
kind: desync
stage: GA shared editor
reported: 2026-11-03
source: C60 actual existing Effect drag at shared R54, proxy position changed while native recipe and revision stayed unchanged
role: host
versions: private preview9 C60 protocol25
fixed_in:
branch: feat/ga-editor-multi-act
commits:
tests: out/preview9-c60-terrain-existing-move; out/preview9-c60-terrain-input-red-preserved; out/editor-preview9-terraform-input/receipt.json
signature: active Terraform action F028F0 and tick F029E0 bypass placement-only F03770/F02B30 hooks
docs: docs/re/adventure-editor-history.md#history-terraform-input-dispatch
related:
---

## Report
After actual shared Basic Tools append at R54, ordinary picking selects the
new Effect descriptor180 and displays its native handles. A normal40-pixel
drag moves the host proxy to the expected new screen point, but revision,
serialized recipe and collaboration history remain unchanged. Both peers
retain strict canonical convergence and zero faults because the unsupported
local proxy move never becomes authored recipe data. A later attempted pick
at the moved proxy location deselects it, consistent with unfinished native
proxy state; that later pick is not an independent successful drag.

## Root cause
Verified native tool vtable148B688 uses F028F0 at slot2C and F029E0 at slot40.
The existing co-op ToolAction and TickTools hooks address the separate
placement functions F03770/F02B30. Thus their Terraform branch is absent
from ordinary Terraform input. The late F381D0 record gate requests ownership
only at release and rejects that update, after native proxy movement already
ran. The old controlled terrain input fixture manually invoked the placement
hook; it passed without testing the actual Terraform entry.

Slot40 is independently read from the exact original GOG executable. The
initial local inferred tick slot30 assertion failed before any game call;
slot30 is950E00. The hook address F029E0 was correct throughout. Evidence:
`out/preview9-c61-terraform-native-route-audit-r2/receipt.json` and the earlier
bounded `out/preview9-c59-terrain-manipulator-audit` disassembly.

## Fix
Private C61 hooks the verified Terraform action and tick functions explicitly.
Held input retains native anchor/history setup, waits for record ownership
before executing the direct Terraform writer, and routes completion and
cancellation to the Terraform originals. The controlled fixture now enters
the Terraform hook and asserts zero native ticks before its grant, including
quick release and stale-identity retirement. Placement routing remains separate.
Scale/clone/delete breadth and actual native C61 acceptance remain pending.

## Verification
FAIL: actual C60 ordinary selected-stamp drag,
`out/preview9-c60-terrain-existing-move/result.json`; readonly actual proxy,
selection and strict document evidence in
`out/preview9-c60-terrain-move-rejection-diagnostic/result.json`.
Logs and process ownership are retained under
`out/preview9-c60-terrain-input-red-preserved`.
PASS: strict actual-i686 controlled Terraform input195, delayed Clone838,
planet preset8,910 and full adapter153,522 checks; compiler inputs remain
unchanged. Receipt: `out/editor-preview9-terraform-input/receipt.json`.
ABI verification passes117 exact GOG entries, including separate native
action/tick function and vtable receipts. This is not yet an actual C61 game PASS.

CORRECTION (2026-11-03): actual C61 now reaches the shared transaction, where
Core rejects `Terrain replacement must retain its identity, order and expected
revision.` The acquired record revision and native projection revision belong
to different domains. C62 constructs replacement metadata from the acquired
canonical record and takes only the verified new native payload; it checks
before-image, object, kind and order before submission. The Core validator is
unchanged. Host/i686366 checks reproduce the legacy zero-revision rejection,
accept the new request, preserve a foreign stroke through Undo/Redo, and reject
six forged/invalid variants atomically. Evidence:
`out/preview9-c61-terrain-revision-red-preserved` and
`out/editor-preview9-terrain-revisions/receipt.json`.
Actual C62 qualification remains pending.

CORRECTION (2026-11-03): PASS on actual C62 for both roles. The same ordinary
40-pixel selected-Effect drag produces one synchronized transaction/history
item, changing only transform column02A907B6. Host A/U/R/U reaches R54 to58;
guest A/U/R/U reaches R58 to62. Both peers strictly converge and all native
Planet fields restore/reapply exactly through history, with zero faults.
Receipts: `out/preview9-c62-terrain-existing-move/result.json` and
`out/preview9-c62-terrain-guest-move/result.json`.
This qualifies existing-stamp movement, not every remaining Terraform control.

## History
- 2026-11-03: opened.
- 2026-11-03: actual C60 failure preserved, separate native dispatch verified; C61 controlled checks pass, running-game qualification pending.
- 2026-11-03: C61 exposes the second checked-revision rejection; C62 retains acquired metadata and passes host/i686 Core acceptance/history checks.
- 2026-11-03: actual C62 host/guest movement and checked A/U/R/U pass with exact Planet restoration and one transaction per drag.
