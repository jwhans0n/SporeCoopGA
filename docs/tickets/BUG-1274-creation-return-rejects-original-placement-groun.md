---
id: BUG-1274
title: Creation return rejects original placement grounding and poisons subsequent editor entry
status: fixed
kind: bug
stage: Adventure
reported: 2026-11-13
source: player
role: both
versions: 0.7.8.0-ga-editor-preview.13
fixed_in: unreleased
branch: fix/ga-editor-creation-grounding
commits: 7b3e894f05ffd7d75dd624090595468ee36440b4
tests: editor_runtime_windows_tests; editor_native_cancellation_tests; coop_editor_creation_return.py; coop_editor_outfitter.py
signature: creation return lost an exact original placement grounding witness; captain position grounding differs by one ULP
docs: docs/re/creations.md#creations-canonical-return; docs/reports/EDITOR-PREVIEW14-VALIDATION.md
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
Final payload 2ad4765f1daa14b7784258df8abed024e09ab5bbc3546e59d0b058e0236391d3:
qualified-return-47/ and qualified-return-48/ each pass two entries/returns;
qualified-outfitter-47/ and qualified-outfitter-48/ pass full native saves.
Actual observers, per-author Undo/Redo, Native Save acknowledgements, model
dependencies and fault logs pass. Runtime: 124544 checks; native fixture: 161136.
The first return-undo-redo/ fixture incorrectly compared a restored field
revision to its pre-Undo revision; return-undo-redo-qualified/ checks its new
accepted revision while requiring exact content and all unrelated rows.
Repository make test passes with sanitizers/fuzzing and all audit checks.
The qualified preview 14 payload is installed with verified hashes and retained
preview 13 rollback; the exact 4 GB executable and personal settings/profiles
are unchanged. Receipts are recorded in the validation report.

## History
- 2026-11-13: opened; retained installed and private failing evidence.
- 2026-11-13: fixed in preview 14; qualified native returns from both roles,
  retained strict restoration failure tests and repeat-entry coverage.

Legacy fork reference: `BUG-0641` before upstream 0.7.13 integration. Historical commits and OUT receipts retain that identity; upstream owns its independently created ticket under that number.

Legacy fork reference: `BUG-1029` before upstream0.7.15 integration. Prior aliases and historical commit/OUT identities remain unchanged; upstream owns its independently created ticket under that number.
