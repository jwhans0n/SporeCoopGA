---
id: BUG-0522
title: Native Flowers and GroundCover omit the regenerated forest field lease
status: open
kind: bug
stage: GA shared editor
reported: 2026-11-03
source: C54 native Flowers normal input R42 rejected at Core1083
role: host and guest
versions: GA shared editor preview9
fixed_in:
branch: feat/ga-editor-multi-act
commits:
tests: out/editor-preview9-ground-effect/red-receipt.json; out/editor-preview9-flora-refresh/receipt.json
signature: Core1083 Flowers companion Field lease missing
docs: docs/re/adventure-editor-history.md#history-ground-effect-flora-refresh
related:
---

## Report
Normal Flowers input in the C54 shared Terraform Flora tab was rejected at
revision42 with `Editing shared properties requires ownership of their settings.`
Both participants restored exact baseline Planet values/history, remained
strictly converged and fault-free. The clicked checkbox could remain checked
after its underlying property was restored.

## Root cause
Verified native setter F35160 writes GroundEffect56B14F05 and callsF34500,
which regenerates forest key array03ABB0AF. The adapter requested ownership of
only56B14F05. The real Coordinator correctly rejected the second field at
editor_document.cpp1083. The deferred canonical UI refresh covered the
Environment category but omitted Flora's native EE8C80 refresh.

## Fix
Request both exact Planet Field leases before entering the GroundEffect setter.
Keep existing field revisions, deferred-input and Core admission checks. Extend
the existing settled later-frame UI refresh to visible Flora through verified
EE8C80, pinning its current UILayout/container/category and scene context.
No broader Settings ownership or retry-click workaround is introduced.

## Verification
FAIL: `out/editor-preview9-ground-effect/red-receipt.json`, genuine unchanged
C54 Native source, actual-i686 exit1 at decision.accepted(), same exact Core
ownership error as the native receipt. PASS: sibling receipt.json,108 focused
GroundEffect checks,990 Flora checks and140280 full checks,59 inputs/zero drift.
The fixture uses the actual Native gate fields with the real Coordinator,
foreign-held companion denial, queued first input, stale value/revision/denial,
and exact two-field Undo. Superseding strict actual-i686 validation passes108
GroundEffect,990 Flora,900 Flora UI,1137 Environment and141185 full Native checks,
59 inputs/zero drift, in `out/editor-preview9-flora-refresh/receipt.json`.
The UI fixture includes current layout/container/category, getter and final
policy callback expiry, reentry, held input and restored card/checkbox values.
ABI102 entries and symbol/docs lints pass. Actual C56 acceptance is pending.

## History
- 2026-11-03: opened.
- 2026-11-03: bounded native disassembly and actual restored forest bytes close
  the missing companion. Core behavior is retained. Live fix acceptance remains
  a separate Root-run check.

Legacy fork reference: `BUG-0471` before upstream 0.7.4 integration. Existing commits and OUT receipts retain that historical ID; upstream owns its independent ticket under that number.
