# Captain and Terrain Edit Stability: preview 10

Version: `0.7.5.0-ga-editor-preview.10`. Protocol:26. Testing update, not release acceptance.
Base: upstream-synchronized `feat/ga-editor-multi-act`, starting at7c512455,
including the upstream0.7.4 merge0c61fe6a and protocol26 Terraform fixes.

## Changes

- Pin Captain versus Placement gesture capture before native mutation. Captain
  poses use Captain Settings ownership and transactions.
- Retain the original press/movement/release through ownership latency; an
  unchanged denied grab preserves its exact grounding evidence.
- Observe native Captain grounding separately from authored XYZ. Match exact
  native identities and values; preserve unrelated Captain and placement fields.
- Extend the existing local Save/terrain transition witness to Captain XYZ.
- Restore the special marker and visible actor through verified native F3D930.
- Use existing fenced read identity during Save/recovery while input stays held.
- Log bounded rejection domain, settings property IDs, revisions and guard state.

There are no new save, recovery, checkpoint or wire fields. Older protocol clients
remain explicitly rejected. All party clients require the same testing payload.

## Evidence

| Check | Evidence/result |
| --- | --- |
| Original player failures | `out/preview10-player-reports/receipt.json`: two logs and six stable draft copies; source files unchanged. |
| Captain routing RED | `out/preview10-captain-red-r2/`: placement-only candidate assertion fails. |
| Captain routing GREEN | `out/preview10-captain-green/`:278 focused checks. |
| Grounding RED | `out/preview10-captain-grounding-red/`: authored Captain preservation fails. |
| Property/denial RED | `out/preview10-captain-properties-grounding-red/`, `out/preview10-captain-denied-grounding-red/`. |
| Saved-image RED | `out/preview10-saved-captain-grounding-red/`: validation fails before witness support. |
| Saved-image GREEN | `out/preview10-saved-captain-grounding-green-r2/`:246 checks, including unexpected pose and health. |
| Held read scope RED/GREEN | `out/preview10-held-captain-grounding-red/` versus `out/preview10-held-captain-grounding-green/`:38 focused checks. |
| Full actual-i686 O0 fixture | `out/preview10-held-captain-grounding-green/full-run.log`:161,123 checks. |
| Runtime rejection recovery | `out/preview10-captain-runtime-o0/run.log`:13,231 checks. |
| Runtime staged capture / terrain transitions |8,804 /3,780 checks in the same fixture directory. |
| C72 host and guest terrain | Native strokes beneath Captain, checked Undo/Redo/Undo, one history item, complete authored semantic comparison, visible native actors and zero faults. |
| C73 host and guest Captain | Actor-checked native drags, Undo/Redo/Undo, complete authored semantics and visible observer updates. `out/preview10-c73-host-captain/`, `out/preview10-c73-guest-captain-history/`. |
| C73 Save failure | `out/preview10-c73-native-save-grounded-captain/`: actual local Save completed; publication refused and checkpoint retained. |
| C74 recovery failure | `out/preview10-c74-recover-r299/`: confirms held-scope gap before C75. |
| Baseline read RED/GREEN | `out/preview10-baseline-read-red/` versus `out/preview10-baseline-read-green/`:108 checks; no input authority during pending-document reads. |
| O1 Win32 full fixture | `out/preview10-final-native-o1/full-run.log`:161,123 checks. |
| Current runtime aliases / recovery |11,199 /2,028 checks. |
| Packaging boundaries |16 synthetic tests pass for preview10/protocol26. |
| Native symbol audit |718 used,3,246 known,0 problems; frozen GOG2016 exclusion is explicit. |

The first native drag fixture selected a neighboring placement. It is not counted
as a Captain pass. Its one edit was undone and the complete authored projection
matched the prior state. Later runs check native actor identity before sending
drag movement. Long history console output truncates at the transport bound;
fixtures use its authoritative count header, not the count of printed rows.
An oversized frame wait timed out after an accepted terrain commit; the separate
history continuation verified the resulting state and complete history cycle.

## Qualification and delivery

C76 production inputs are frozen with 340 hashes and zero drift. Its DLL SHA256
is `c6e80c335ab6a97ad60fbff311381f10aa3bf7aa4fb0b62b0f360ef3097c84ca`;
SporeKit SHA256 is `f68aaa3ac945e1bf3e01275c03bc5ccc59b0d0426c4ebcab13967fe684559f0b`.

Final native qualification passes:

- `out/preview10-c76-recover-r299-index2-r2/result.json`: two fresh-process
  participants recover R299, identical terrain values and strict full convergence.
- `out/preview10-c76-native-save-grounded-captain/result.json`: host Native Save
  with terrain applied; both participants acknowledge and converge at R299. The
  host uses the new saved root; the guest retains its verified local native alias.
- `out/preview10-c76-four-captain-r300/result.json`: four participants, native
  actor-guarded guest Captain drag and Undo/Redo/Undo, R300 through R304.
- `out/preview10-c76-four-terrain-r304/result.json`: another guest places a native
  Raise Round Terrain stroke beneath the Captain, exactly one item, all four
  converge at R305 with visible marker/actor grounding and preserved companions.

The first four-player join fixture fails an overstrict marker/actor distance
assertion after Save. The separate read-only observation in
`out/preview10-c76-four-state/result.json` records identical visible Captain
positions on all four, but the two Save participants retain authored marker XYZ
while newly joined participants have grounded marker XYZ. All four strict
snapshots converge. The native Terrain Undo returns all marker/actor poses to
the exact authored baseline at R300 before the four-player gesture checks.
This observation is retained; no broad distance allowance was added to product
validation or the gesture fixtures.

The installer gates the final frozen payload on these checks, verifies both
personal games are closed, and retains the current preview rollback, INI,
exact4GB executable, profiles and shortcut resolution. Delivery receipts live
under `out/preview10-testing-delivery-candidate76/`.

## Deferred

Broader creation/replacement/failure/recovery matrices, performance acceptance,
final release regressions, lab smoke, public release packaging and publication
claims remain deferred. BUG-0622 cold-reopen comparison remains separate and open.

The C75 private lab later hits the known D3D presentation hang (BUG-0616); its
report and archive are preserved, and that run is not acceptance. Display-mode
indices change again during C76 startup. Native Settings calibration restores
1024x768@60 (index2), with no changes to personal graphics settings.

## Limits of this testing update

The focused native session uses a private recovered 419-placement adventure.
The reported failures are preserved from the original logs and reproduced in
private native strokes and exact-layout Win32 fixtures. A fresh blank-adventure
native matrix, simultaneous competing native strokes, every network-adversity
case and a complete repeat of each reported draft are not acceptance claims.
Ownership latency, denial, stale identities, unrelated edits, checked rejection
restoration and terrain-transition boundaries have targeted Win32 coverage.
Broader native matrices remain follow-up testing; deterministic coverage does
not substitute for native observer validation.

## Installed testing payload

Installed 2026-11-05 after confirming personal games were closed. The installer
and independent verification each verify all 7 changed and 14 preserved files,
including shortcut resolution, exact 4 GB executable, INI and player launchers.
Profiles are untouched. The previous preview9 payload is retained under
`out/preview10-testing-delivery-candidate76/rollback-20261105T080908Z-869845847a864b479d9f8db70aeb767d/`.
Receipts: `installation-receipt.json` and `independent-verification.json` in that
delivery directory. All four private games return normally to Galaxy with zero
faults before their owned runner exits; retirement evidence is
`out/preview10-c76-r305-four-retirement/result.json`.

Production commit: `4dc8c64a`. `committed-source-binding.json` in the delivery
directory compares every frozen production input to that Git tree, permitting
only Git line-ending normalization. This is the installed candidate's source
binding. No main merge, public package or release smoke claim is made.
