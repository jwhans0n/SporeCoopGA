---
id: BUG-1270
title: Native act creation after independent editor return loses scene identity
status: open
kind: bug
stage: Adventure
reported: 2026-11-12
source: internal
role: host
versions: 0.7.8.0-ga-editor-preview.12 candidate c815be1b
fixed_in:
branch: fix/ga-editor-asset-browser
commits:
tests: out/preview12/qualify-final-drops.py
signature:
docs:
related:
---

## Report
During cross-act fixture setup, clicking the native Add Act control after independent creation editor entry/return froze the shared draft. Revision 3 and its three accepted history entries remained durable. No new act or history item was accepted and neither game crashed.

## Root cause
Observed: capture_scene_owner fails or its source differs from gesture.after.source in the general gesture path. The reported error text is empty. The host reports that the captured gesture could not pin its native editor; the guest requires explicit baseline recovery. The precise source mismatch and relationship to prior creation return remain unclassified.

## Fix
Unfixed, retained separately from the asset-browser changes. Do not relax native identity checks. Recovery requested in that same native scene correctly refuses and requires a return to Galaxy first.

## Verification
FAIL: out/preview12/c815-cross-act/45-status.json and 46-status.json, plus matching durable history and native act diagnostics. Same-scene recovery attempt: out/preview12/c815-act-setup-recovery/. No passing act-creation result is claimed. The independent creation, content replacement and observer checks immediately before this setup passed separately.

## History
- 2026-11-12: opened.

Legacy fork reference: `BUG-0637` before upstream 0.7.13 integration. Historical commits and OUT receipts retain that identity; upstream owns its independently created ticket under that number.

Legacy fork reference: `BUG-1025` before upstream0.7.15 integration. Prior aliases and historical commit/OUT identities remain unchanged; upstream owns its independently created ticket under that number.
