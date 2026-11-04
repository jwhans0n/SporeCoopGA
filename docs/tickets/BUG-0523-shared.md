---
id: BUG-0523
title: First Alt clone is lost while waiting for Structure ownership
status: open
kind: bug
stage: GA shared editor
reported: 2026-11-03
source: actual C57 Sound Alt-clone qualification
role: 
versions: 
fixed_in:
branch: feat/ga-editor-multi-act
commits:
tests: strict actual-i686 clone-input-only 838 and shared full 143729
signature: ToolAction11 structure-lease return drops first Alt drag
docs: docs/re/adventure-editor-history.md#adventure-editor-first-alt-clone
related:
---

## Report
The first ordinary Alt-drag of the selected Sound is rejected while its Structure lease is in flight. Release arrives before ownership and the entire gesture is lost. Both peers remain strict and no partial clone is allocated (C57, revision 92).

## Root cause
ToolAction11 and direct CloneSelection return on a missing Structure lease without retaining a continuation. Existing move holds start Native history immediately and cannot safely initialize a clone before both Placement and Structure/Class grants. Simply replaying late also recomputes the initial cursor-ray anchor at the release cursor and loses its displacement.

## Fix
Retain one value-only source/owner/revision capsule and the original Native-derived 12-byte ground anchor. Restore derived scratch before waiting. Once both grants are current, start the ordinary clone once, run the Native initializer with authored steps blocked, install the original anchor, and consume the current cursor and matching release. Pending Shift/Ctrl variants cancel explicitly; already-owned variants keep their Native route. Auxiliary clones remain blocked.

## Verification
Exact original C57 producer RED exits1 at the missing held-clone assertion, retained under `out/preview9-c58-clone-input/old-c57-red-receipt.json`. Strict actual-i686 GREEN passes838 focused clone checks and143729 shared full checks; all68 compiler/Make inputs are unchanged (`out/editor-preview9-color-refresh/receipt.json`). Actual running-game qualification remains Root-owned.

## History
- 2026-11-03: opened.

Legacy fork reference: `BUG-0472` before upstream 0.7.4 integration. Existing commits and OUT receipts retain that historical ID; upstream owns its independent ticket under that number.
