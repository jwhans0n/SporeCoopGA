---
id: BUG-0541
title: Terraform gesture changes unowned settings and fails canonical restoration
status: fixed
kind: desync
stage: GA shared editor
reported: 2026-11-04
source: Player preview9 logs: authored settings ownership missing during shared terrain edits
role: both
versions: 0.7.5.0-ga-editor-preview.9
fixed_in: 0.7.5.0-ga-editor-preview.10 (private testing)
branch: feat/ga-editor-multi-act
commits:
tests: tests/editor_native_cancellation_windows_tests.cpp, tests/editor_native_captain_grounding_fixture.h, tests/editor_runtime_windows_tests.cpp; docs/reports/EDITOR-PREVIEW10-VALIDATION.md
signature:
docs:
related: BUG-0540, BUG-0538
---

## Report
Player logs from both roles show `The authored settings gesture has no current ownership`; two restoration attempts additionally report `Native act/captain settings differ from the committed draft` and retain recovery. All six recent project copies and the two current logs are preserved with a source-stability manifest in `out/preview10-player-reports/receipt.json`.

## Root cause
A native Raise Round Terrain stroke in the private 419-placement draft changes Captain XYZ (property `43415003`) through F3E250. Authored XYZ `(-266.498657,486.234894,88.080429)` becomes rendered XYZ `(-268.155426,489.257782,88.628014)`. Treating that derived grounding as an authored Captain edit requires an unrelated Captain lease and rejects the terrain stroke. Native history also retains the special Captain marker across same-asset restoration, allowing its old pose to overwrite the restored resource. Save/recovery holds hide ordinary projection identity; an incoming baseline's Captain revision is not yet in Controller while its native scene is being applied. BUG-0538 remains separate.

## Fix
Observe the verified F3E250 native grounding boundary and retain exact scene, marker, act, resource, Captain revision and authored/rendered XYZ evidence. Normalize only owned snapshots; health, appearance, rotation, scale, lock and unwitnessed changes remain strictly validated. Preserve evidence through unchanged denied grabs. Restore the special marker and visible actor via verified F3D930 before native history can export stale values. Extend the existing local terrain Save proof to Captain grounding and use exact read identities during held transitions and incoming baseline application. These scopes grant no input ownership. Existing field/record guards, latest committed restoration and recovery safeguards remain in place; no persistent or wire formats change.

## Verification
RED native reproduction: `out/preview10-c70-terrain-under-captain/` and the preserved 22:40:59 guest rejection. The oversized frame wait later times out and is not a passing fixture. Actual-i686 RED fixtures cover grounding, changed properties, denied grabs, Save normalization and held reads; final O0/O1 native fixtures each pass 161,123 checks. Baseline-read RED/GREEN adds 108 focused checks without granting gesture authority.

Native host/guest terrain strokes under the Captain pass one-item history and Undo/Redo/Undo with complete authored semantic hashes and actual observer actor checks. Final C76 fresh-process recovery at R299 and Native Save with applied terrain pass on both participants. The final report lists four-player results and all unrun cases explicitly: `docs/reports/EDITOR-PREVIEW10-VALIDATION.md`.

## History
- 2026-11-04: opened.
- 2026-11-05: exact Captain grounding and held baseline causes fixed; private testing delivery.
