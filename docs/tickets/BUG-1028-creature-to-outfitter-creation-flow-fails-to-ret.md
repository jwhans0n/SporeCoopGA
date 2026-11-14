---
id: BUG-1028
title: Creature to outfitter creation flow fails to return to shared adventure editing
status: fixed
kind: bug
stage: Adventure
reported: 2026-11-13
source: player
role: guest
versions: 0.7.8.0-ga-editor-preview.12
fixed_in: unreleased
branch: fix/ga-editor-browser-runtime
commits: 90535e52 4d0b4116 0083a944
tests: tools/tests/coop_editor_outfitter.py, tests/editor_creation_windows_tests.cpp
signature: native creation editor returned without its owned result after Outfitter
docs: docs/re/creations.md#creations-browser-pulses-outfitter
related:
---

## Report
A guest created a creature, switched into its outfitter, then returned to the adventure. The installed log reports that the native creation editor returned without its owned result; restoration failed and the host requested explicit recovery.

## Root cause
Observed static and native input evidence: native Continue follows the intrusive
root request+98 link and replaces cEditor+1CC without Submit or intermediate
NotifyResult. The final Outfitter notifier receives that child. Preview 12
compares only the original root pointer, ignores the result and forces recovery.

## Fix
Match the exact retained root or a bounded, validated owned descendant. Check
cycles, request vtable, live reference, same family, verified editor variant and
return mode. Preserve project/session/content lease fences and native posted
result cancellation. Only the final result enters coordinated admission.
Negative-control switch: `coop.fix editor.creation_chain off`.

## Verification
FAIL: `out/preview13/outfitter-return-red/` and `outfitter-red-native.log`
reproduce the exact installed missing-result and recovery error through normal
native Creature/Outfitter input. Unit FAIL: `outfitter-unit-red.log`; unit PASS:
`outfitter-unit-final.log` (2016 checks). Native PASS:
`outfitter-guest-green1/` and `outfitter-host-green2/` each save one shared cast
entry with one history item, matching actual observer scenes and zero faults.
`outfitter-guest-cancel/` returns without changing canonical cast/history.
Further final candidate evidence is in the preview 13 validation report.

## History
- 2026-11-13: opened.
- 2026-11-13: reproduced and fixed on the preview 13 testing branch; native host/guest return passes.

- 2026-11-13: final 694490b9 payload passes focused native host/guest checks, affected Win32 fixtures and complete repository tests; preview 13 installed with preview 12 rollback. See docs/reports/EDITOR-PREVIEW13-VALIDATION.md.

Legacy fork reference: `BUG-0640` before upstream 0.7.13 integration. Historical commits and OUT receipts retain that identity; upstream owns its independently created ticket under that number.
