---
id: BUG-0635
title: Creation Save fixture rejects valid large native confirmation trees
status: open
kind: test
stage: Tests
reported: 2026-11-12
source: internal
role: both
versions: 
fixed_in:
branch: fix/ga-editor-asset-browser
commits:
tests: tools/tests/coop_editor_creation_save.py --self-test
signature:
docs:
related:
---

## Report
Normal native Done reached the Save New/Replace dialog for an edited shared building. The staged fixture rejected the complete UI tree because unrelated creation controls pushed it above8KiB, leaving the valid dialog open.

## Root cause
Observed: _creation_exit_modal bounded the whole console response before extracting its already bounded dialog subtree. A real native editor contains thousands of unrelated controls. The original checkpoint correctly prevented blindly clicking Done a second time.

## Fix
Keep the console response bounded at60KiB and the recognized complete modal at8KiB. Add an explicit confirmation-only resume that requires the original pending Save, scope, request, source, native command and unchanged revision; it never replays Done. Reject completed or previously confirmed checkpoints.

## Verification
FAIL: out/preview12/cursor-guest-hover-edit-save/result.json and out/preview12/evidence/confirmation-bound-red.log. PASS: out/preview12/evidence/confirmation-bound-green.log (705 checks), then out/preview12/cursor-guest-hover-edit-confirm/result.json. The same original native request completed with one replacement transaction and matching native observer checks.

## History
- 2026-11-12: opened.
