---
id: BUG-0641
title: Creation return rejects original placement grounding and poisons subsequent editor entry
status: open
kind: bug
stage: Adventure
reported: 2026-11-13
source: player
role: both
versions: 0.7.8.0-ga-editor-preview.13
fixed_in:
branch: fix/ga-editor-creation-grounding
commits:
tests:
signature:
docs:
related:
---

## Report
Host and guest creation returns reject an exact original placement grounding
witness, leaving shared editing in recovery. A later Create Creature attempt
shows a black viewport. Installed preview 13 logs reproduce the grounding error
after both direct Creature and Creature to Outfitter completions. Native results
are received; no missing-owned-result error is involved in this report.

## Root cause
Observed: entry-witness transfer and validation run before canonical scene
restoration. Installed logs reject placements; a private cold recovery rejects
an exact captain Y difference of one ULP after cancellation and Outfitter Save.
The native result was captured and its creation could already be durable.
Expired scene-derived provenance blocks reconstruction and paired camera resume.
The precise warm-scene placement mutation remains unproven; no tolerance is
authorized by that observation.

## Fix
Restore the latest owned canonical snapshot under the original scene/session
fence before strict final field and opaque verification. Wait for native
readiness and a later frame, coalesce accepted revisions, retain independent
view/selection, and preserve terminal recovery on genuine restoration failure.
Switch: coop.fix editor.creation_restore off.

## Verification
FAIL: out/preview14/runtime-return-red.log; private native
cancel-return-red/ and creation-save-measured-red/.
PASS: runtime-return-green1.log, native-return-host-green1/ and
native-return-guest-green1/, outfitter-host-green1/ and outfitter-guest-green1/.
Final payload qualification remains in progress.

## History
- 2026-11-13: opened.
