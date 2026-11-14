---
id: BUG-1027
title: Shared editor frame rate regresses with the native cast browser
status: fixed
kind: bug
stage: Adventure
reported: 2026-11-13
source: player
role: both
versions: 0.7.8.0-ga-editor-preview.12
fixed_in: unreleased
branch: fix/ga-editor-browser-runtime
commits: 90535e52 4d0b4116 0083a944
tests: tools/tests/coop_editor_browser_fps.py, tests/editor_creation_ui_windows_tests.cpp
signature: repeated native cast scans from same-frame editor service pulses
docs: docs/re/creations.md#creations-browser-pulses-outfitter
related:
---

## Report
Both roles report sustained editor framerate loss in preview 12; the guest improves on non-creature viewer tabs. Installed logs show repeated 0.5-second hitches, host 2 FPS and guest 6 FPS, with zero native faults.

## Root cause
Observed native sampling and outside-work metrics show repeated cast-tree scans
between Present frames. The 5 ms editor service drains the queued task and then
schedules it again before rendering. An already-queued flag alone is insufficient.
Unchanged decoration writes add native invalidations, but guarding those alone
did not fix the native frame rate.

## Fix
Fence native browser scheduling by the rendered frame index as well as the
pending task. Skip unchanged border/padlock geometry, visibility and colors.
Keep networking service active and ownership authorization unchanged.
Negative-control switch: `coop.fix editor.browser_idle off`.

## Verification
FAIL: `out/preview13/ui-pulse-red.log` reproduces extra native scans in one
frame. PASS: `ui-pulse-green.log` and `ui-unit-green.log` cover service pulses,
idle setters, ownership/hover refresh and teardown. Native FAIL:
`out/preview13/browser-fps-final-red/result.json` drops below 2 FPS with the
switch off while scenes are ready and native observers converge. Native PASS:
`out/preview13/browser-fps-green1/result.json` retains about 21 FPS on both games
across Creature/Building/Creature tabs, with unchanged document/history and zero
faults. Further final candidate evidence is in the preview 13 validation report.

## History
- 2026-11-13: opened.
- 2026-11-13: reproduced and fixed on the preview 13 testing branch; no main promotion.

- 2026-11-13: final 694490b9 payload passes focused native host/guest checks, affected Win32 fixtures and complete repository tests; preview 13 installed with preview 12 rollback. See docs/reports/EDITOR-PREVIEW13-VALIDATION.md.

Legacy fork reference: `BUG-0639` before upstream 0.7.13 integration. Historical commits and OUT receipts retain that identity; upstream owns its independently created ticket under that number.
