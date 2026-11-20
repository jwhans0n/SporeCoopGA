---
id: BUG-1266
title: Native creation validation reads UTF-8 symbols with the Windows locale codec
status: fixed
kind: bug
stage: Tools
reported: 2026-11-12
source: internal
role: both
versions: 
fixed_in: unreleased
branch: fix/ga-editor-asset-browser
commits: b736f8ec
tests: tools/tests/coop_editor_creation_validation.py
signature:
docs:
related:
switch: none (tool encoding correction, no mod change)
---

## Report
The native House entry validation aborted with UnicodeDecodeError before it could inspect the request.

## Root cause
Observed: Windows Python used cp1252 for symbol-map read_text(), which rejects UTF-8 bytes in the existing verified notes.

## Fix
Read symbol maps explicitly as UTF-8. Keep all native request, profile, process, hash and model checks intact.

## Verification
FAIL: out/preview12/baseline-house-enter/result.json. PASS: out/preview12/baseline-entertainment-enter/entered-native.json, followed by the verified native Save receipt in baseline-entertainment-save/result.json.

## History
- 2026-11-12: opened.

- 2026-11-12: implemented on the preview 12 review branch; public release and destination promotion remain deferred.

Legacy fork reference: `BUG-0633` before upstream 0.7.13 integration. Historical commits and OUT receipts retain that identity; upstream owns its independently created ticket under that number.
- 2026-11-13 integration: declare the twin strategy for the newer upstream ticket checker; retain the original FAIL/PASS evidence.

Legacy fork reference: `BUG-1021` before upstream0.7.15 integration. Prior aliases and historical commit/OUT identities remain unchanged; upstream owns its independently created ticket under that number.
