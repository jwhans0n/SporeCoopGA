---
id: BUG-0608
title: Gradient height Undo restores Planet data but leaves local native Color UI stale
status: fixed
kind: bug
stage: GA shared editor
reported: 2026-11-03
source: C57 native gradient height R100 to101 Undo102
role: host and guest
versions: GA shared editor preview9
fixed_in: private preview9 C58 (protocol24)
branch: feat/ga-editor-multi-act
commits: e19b5362c9ae2e0fe2ce1cd2dd5c41804a66cdb5
tests: out/editor-preview9-color-refresh/receipt.json
signature: C57 R102 local gradient marker y330 after canonical Undo restored y303
docs: docs/re/adventure-editor-history.md#history-color-ui-refresh
related:
---

## Report
Normal native height-node drag changed only Planet field B6929C92 at index2.
Undo restored all Planet values at R102, strict convergence and fault0, but the
authoring host's visible marker remained y330 instead of the restored y303.
The guest showed the correct position. Native evidence is retained in
`out/preview9-c57-gradient-height-probe`.

## Root cause
The existing deferred native UI refresh handled category2 Flora and category3
Environment, skipping category1 Color. Native EE9160's category1 refresh uses
TerraformUI+C wrapper+10 ColorUI, then EE6AD0; it rebuilds the displayed nodes
from the current native vectors through EEB480. The serialized data and Core
history/ownership path had already restored the exact values correctly.

## Fix
Extend the settled, outside-frame passive UI queue to category1 through the
verified native refresh. Freshly fence wrapper, ColorUI, UILayout/container and
gradient/window identities, existing scene/scope/source/generation and queued
serial. Preserve current tab navigation and all authored ownership rules.

## Verification
FAIL: `out/editor-preview9-color-refresh/red/receipt.json` builds the genuine
unchanged C57 Native producer with the new controlled fixture. It exits1 at
the displayed-gradient update assertion,66 compiler/Make inputs with zero drift.
The producer matches the actual retained C57 code after line endings only.
PASS: strict actual-i686 Color1,567, Flora990, Flora UI900, Environment1,137,
delayed Clone838 and full adapter143,729 under
`out/editor-preview9-color-refresh/receipt.json`,68 inputs with zero drift.
The earlier pending running-game qualification is superseded by the following
C58 acceptance. Both authors perform normal height303 to330 followed by own
Undo/Redo/Undo, R3 to11, changing only B6929C92. Six history UI checks prove the
303 node is restored and the330 node absent after Undo; both final screenshots
were reviewed, with no tab-toggle workaround, strict peers and zero faults.
Receipts: `out/preview9-c58-two-gradient-ui/result.json` and `ui-proof.json`.
This is the private protocol24 candidate, not an update to the installed
personal protocol23 Flora payload.

## History
- 2026-11-03: opened.
- 2026-11-03: C58 two-player normal native input and restored UI acceptance pass.

Legacy fork reference: `BUG-0473` before upstream 0.7.4 integration. Existing commits and OUT receipts retain that historical ID; upstream owns its independent ticket under that number.

Legacy fork reference: `BUG-0524` before upstream 0.7.8 integration. Historical commits and OUT evidence retain that ID; upstream owns its independent ticket under that number.
