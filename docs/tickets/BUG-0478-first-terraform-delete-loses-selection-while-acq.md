---
id: BUG-0478
title: First Terraform Delete loses selection while acquiring ownership
status: open
kind: bug
stage: GA shared editor
reported: 2026-11-03
source: Private C63 native control coverage
role: host
versions: 0.7.5.0-ga-editor-preview.9
fixed_in:
branch: feat/ga-editor-multi-act
commits: f2d3d2c
tests: editor_native_cancellation_windows_tests.exe --terrain-delete-only; --terrain-input-only; full adapter
signature: Terraform vt10 EFEEC0 clears Context4 before F37FF0 ownership check
docs: docs/re/adventure-editor-history.md#editor-terraform-delete-entry
related:
---

## Report
C63 actual host Delete on existing Effect180 produces no transaction atR116. The entire native image, semantic projection, canonical hash and history remain unchanged on both peers, with zero faults. Evidence: `out/preview9-c63-terrain-host-effect-delete/result.json` and `out/preview9-c63-terrain-first-action-failures/result.json`.

## Root cause
Terraform uses vt10 EFEEC0, separately from placement Delete F02CE0. EFEEC0 starts native history and clears selection through vt20 before calling F37FF0. The existing F37FF0 gate can request ownership but cannot retain that already-cleared Delete input. Exact supported-executable prologue, plain return, commit tail, call sites and vtable slot are verified in `out/preview9-terrain-delete-abi.json`.

## Fix
Hook the actual Terraform Delete entry. Retain an immutable value-only command containing the scene owner/source, context/tool, selected current descriptor/actor/definition/index, record revision, act and authoring scope. Grant callbacks are followed by fresh identity checks. No native history or selection changes occur before the grant. Repeated Delete key events coalesce. Ownership denial, Escape, focus loss, expiry or any stale identity retires the command without native mutation. No runtime override. Corrected private C65 host Delete/Undo/Redo/Undo acceptance passes; guest and Model cases remain pending.

## Verification
Controlled RED: `out/preview9-terrain-delete-red-run.log` exits1 at the unowned native invocation/cleared selection assertion. Strict-i686 GREEN: Delete216, existing Terraform input195, full adapter153897 checks, all exit0 (`out/preview9-terrain-delete-green-r3-run.log`, `terrain-delete-drag-regression.log`, `terrain-delete-adapter-regression.log`). Cases include immediate/delayed grants, repeated input, unrelated denial, pending transactions, 18 stale/cancellation variants and three reentrant grant callbacks. An earlier fixture used a preseeded before-image after deleting only its separate current identity; its inconsistent setup is corrected and retained as a failed fixture.

Candidate64 is rejected before deployment because an incorrect use of the partial symbol-map generator omitted existing manual bindings. C65 preserves all165 original entries and adds only the verified Delete entry, with re_lint0 problems. Both builds/pins and the explicit C64 rejection remain retained.

## History
- 2026-11-03: opened.

Native C65: first Delete133 and Undo134 restore exact Planet/semantic values
and both complete native images equal the pre-edit observer image. The old
author raw SHA differs for an unassigned pre-edit reason; that strict fixture
failure remains retained. A complete repeat135-138 then passes exact own-native
SHA restoration on both peers and one history item:
`out/preview9-c65-terrain-host-effect-delete-r2/result.json`. BUG-0481 records
the later simultaneous D3D-reset interruption before guest deletion. Fresh
C65 recovery ofR138 passes. Guest Effect Delete141/Undo142/Redo143/Undo144
then passes exact complete native and semantic restoration on both participants:
`out/preview9-c65-terrain-guest-effect-delete-r3/result.json`. The earlier
guest run139/140 has the same cold-host pre-image mismatch recorded above,
with exact Planet/semantic restoration. Actual Model Delete at144 reaches the
separate synchronous materialization bug BUG-0482, before canonical acceptance.
