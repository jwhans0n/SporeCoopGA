---
id: BUG-1278
title: Act goal Delete loses the first click while awaiting ownership
status: open
kind: bug
stage: Editor
reported: 2026-11-20
source: native integration0715 private Adventure Town two-player qualification
role: guest
versions: 0.7.15.0-ga-editor-preview.16
fixed_in:
branch: sync/upstream-0.7.15-editor-main
commits:
tests: tools/tests/coop_editor_authoring.py; native ga0715-pair/goal-delete-triage
signature: First native goal Delete click grants Settings ownership without committing; second click commits
docs: docs/re/adventure-editor-save.md
related: BUG-1275
---

## Report
In a private copy of Adventure Town, a guest's first click on the enabled native
act-goal Delete control (073BBC38) did not remove the goal. The shared editor
remained responsive and unfrozen. This is a qualification finding, not a new
player report. It blocks qualification of that subcase; other authoring checks
must not be represented as a complete pass.

## Root cause
Observed: at committed revision 8, the guest had no settings lease. One verified
native click acquired scope 30064771072:1 with token 10 within 0.5 seconds, but no
transaction appeared during the following two seconds. A second click while
that lease was held committed revision 9. Public checked Undo restored revision
10. Both native participants remained in phase 4 with zero captured faults.

Inferred: editor_native.cpp's script_input calls settings_gate and suppresses the
native handler when ownership is pending, but does not retain this button
activation for delivery after grant. editor_runtime.cpp's lease requests the
lease and returns false. The relevant script-input logic is unchanged between
the earlier preview-14 tip 31fd9bb6be1e056f307ec9c7cdc034284c0e7c6f and the tested
07b3d9829b13c7c177c42263f3da115f0e1170f0 integration. This evidence points to
pre-existing ownership timing rather than the upstream service merge. It does
not invalidate earlier native goal-control passes that may already have held
the scope.

## Fix
Open. Retain a stable, scene-bound action while waiting for its required
ownership, revalidate its act/goal identity and revision, then deliver exactly
once or cancel safely. No product change is included in this evidence ticket.
As a temporary workaround, click again after ownership arrives.

## Verification
FAIL: out/windows-lab/ga0715-pair/authoring/result.json, the first goal-Delete
action times out after prior settings and Undo checks completed. Reproduction:
out/windows-lab/ga0715-pair/goal-delete-triage/result.json records the first-click
lease grant without mutation, second-click revision 9, and Undo revision 10.
The private payload SHA256 is
3434418927827cc8723e76ef26fcf2f85ae110fb868dd8a089ac98b16b9cb492;
source is 07b3d9829b13c7c177c42263f3da115f0e1170f0, GOG 2024 with the supported
4 GB executable. No fix or passing first-click regression is claimed.

## History
- 2026-11-20: opened.
- 2026-11-20: Retained the failing native subcase and controlled ownership timing
  reproduction during the 0.7.15 integration. Broader authoring qualification is
  explicitly incomplete. GitHub issue creation remains unavailable (403), so an
  exact issue body is retained in the integration OUT evidence.
