---
id: BUG-0525
title: Planet look presets bypass bulk Flora field ownership and split pending setters
status: fixed
kind: bug
stage: GA shared editor
reported: 2026-11-03
source: Verified F0CD40 F37340 F35B20 call chain and Native hook audit
role: host and guest
versions: GA shared editor preview9
fixed_in: private preview9 C59 (protocol24)
branch: feat/ga-editor-multi-act
commits: 942a05192bf623b1014e98283bffa0a37f33396e
tests: out/editor-preview9-planet-preset/receipt.json; out/editor-preview9-planet-preset/old-c58-red/receipt.json
signature: F0CD40 direct F37340 bulk Flora writes escape nested field gates
docs: docs/re/adventure-editor-history.md#history-planet-look-preset-ownership
related:
---

## Report
Source audit of the actual Tab0 planet-look card found that its native selected
message runs a direct bulk Flora replacement inside one native history item.
The unchanged C58 producer reproduces a real Core ownership rejection in the
controlled adapter fixture: only three scopes are acquired while Flora changes.
This is a source/controlled-producer finding; a running-game preset failure has
not been claimed.

## Root cause
Verified GOG2024 F0CD40 calls F37340 directly between Start F33A90 and Commit
F34400. It does not call the wrapped full Load or Build route. F37340 writes
04E13CB7 and F35B20 rebuilds bands, density and intensities, later exported as
073154E0, 073137BB and 072BEC91. Existing scalar gates do not request these
four field scopes. Deferring this inner call alone would split the native
preset across history boundaries.

## Fix
Preflight the verified outer selected-message boundary for the exact seventeen
potential preset fields before any Native Start or authored setter. A pending
request retains owned values, exact per-field revisions and UI/source/context
identities, then replays one original callback after every grant. Cancellation,
denial, timeout, callback retirement or changed identity discards the intent.
Fresh UILayout/container ancestry is checked before replay. An unused before
image is retired only if it is still the image opened by this operation.

Core admission and native Start/Commit semantics remain unchanged. The message
manager ignores callback AL on both immediate and deferred dispatch paths, so
the existing queued false result is retained. There is no runtime bypass switch;
the RED fixture compiles the genuine unchanged C58 Native producer.

## Verification
FAIL: strict actual-i686 unchanged C58 Native.cpp from a16c41e compiles, captures
one native before/after pair, passes opaque support and reaches real Coordinator
admission. It reports three scopes and rejects with `Editing shared properties
requires ownership of their settings.` The expected exit1 is retained in
`out/editor-preview9-planet-preset/old-c58-red/receipt.json`, with zero input drift.

PASS: strict actual-i686 preset8,910, Flora controls990, Flora UI900, Color UI1,567
and Environment1,137 in `out/editor-preview9-planet-preset/receipt.json`, with
seventy compiler/Make inputs and zero drift. The preset cases independently
hold each of the seventeen fields, prove zero native writes before grant,
one Native Start/Commit and one accepted history item after grant, exact Undo,
no-op cleanup, cancellation, stale values/UI/context and callback retirement.
These are controlled native-interface doubles, not a real serializer oracle or
running-game Tab0 acceptance. That separate check subsequently passes on C59:
host R45 to46 then own Undo/Redo/Undo to49; guest R49 to50 then own history to53.
Each preset changes fourteen actual fields inside one history item, including
Flora keys, bands/intensities and GroundEffect. Every full Planet comparison and
strict peer check converges without faults. Evidence is retained under
`out/preview9-c59-first-preset-probe`, `out/preview9-c59-first-preset-history` and
`out/preview9-c59-guest-preset-r2`. The seventeen-field preflight is a conservative
potential write set; it does not claim all seventeen change in each preset.

## History
- 2026-11-03: opened.
- 2026-11-03: verified C59 outer-boundary source fix and controlled old-source RED/strict GREEN; ticket remains open for native qualification.
- 2026-11-03: C59 both-author native preset/history qualification passes R45 to53; closed for the private candidate, with the installed personal protocol23 payload unchanged.

Legacy fork reference: `BUG-0474` before upstream 0.7.4 integration. Existing commits and OUT receipts retain that historical ID; upstream owns its independent ticket under that number.
