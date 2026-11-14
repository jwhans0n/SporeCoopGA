---
id: BUG-1017
title: Dragging a session-created cast asset onto another card crashes in CopyClassActs
status: fixed
kind: crash
stage: Adventure
reported: 2026-11-12
source: player
role: guest
versions: 0.7.5.0-ga-editor-preview.10
fixed_in: unreleased
branch: fix/ga-editor-asset-browser
commits: b736f8ec f7545c3e
tests: tests/editor_native_cancellation_windows_tests.cpp tests/editor_creation_windows_tests.cpp tests/editor_runtime_windows_tests.cpp
signature: CopyClassActs+0x25 read 0x78 through PaletteMessage+0x4FA
docs:
related:
switch: none (stale native card-drop crash routes cannot be runtime-enabled; controlled RED evidence is retained)
---

## Report

Crash after dropping a guest-created entertainment Building onto another cast card in preview 10. The private report and dump are retained in out/preview12/evidence/.

## Root cause

Observed: main-thread AV at CopyClassActs+0x25, read 0x78, through PaletteMessage+0x4FA. Native message 07C7B9BB reads destination/source card components at payload20/24, starts history, imports/replaces content, then resolves class IDs again and copies the act vector. The shared importer can defer and return UINT_MAX; the outer handler does not check the later class lookups. Pending native replay and remapping must not reach this unchecked copy. The exact guest-created Entertainment drop also reproduced in the private preview 11 baseline, CopyClassActs+0x25 read0x78, crash-20261112-150213-i46.

## Fix

Consume the entire drop before history. Queue stable class/resource values, acquire destination ownership, construct an owned native result and submit one checked transaction. Cancel stale or invalid inputs without changing the draft.

## Verification
FAIL: out/preview12/evidence/card-drop-red.log, old native handler forwards a stale/missing-card notification. PASS: out/preview12/evidence/editor_native_cancellation_tests.exe.log and browser-runtime-green.log. Native copy at revision2 plus exact Undo/Redo passed in out/preview12/drop-undo-redo/result.json. Focused native copy/replacement checks, actual observer state and checked history are recorded in docs/reports/EDITOR-NATIVE-FLOW-VALIDATION.md#editor-preview12-browser.

## History
- 2026-11-12: opened.

- 2026-11-12: implemented on the preview 12 review branch; public release and destination promotion remain deferred.

Legacy fork reference: `BUG-0629` before upstream 0.7.13 integration. Historical commits and OUT receipts retain that identity; upstream owns its independently created ticket under that number.
- 2026-11-13 integration: declare the twin strategy for the newer upstream ticket checker; retain the original FAIL/PASS evidence.
