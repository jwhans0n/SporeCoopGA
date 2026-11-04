---
id: BUG-0526
title: Extended marker-pool growth leaves native actor marker caches dangling
status: fixed
kind: crash
stage: GA shared editor
reported: 2026-11-03
source: C58 cold1025 shared R0 Extended R1 clone AV ED8A1F, retained crash dump and exact code audit
role: host
versions: private preview9 C58 protocol24
fixed_in: private preview9 C60 (protocol25), not installed personally
branch: feat/ga-editor-multi-act
commits: ecd9580c03adad27da15f98ba7cd8633b81c6e4f; f182eff96c92a96071fba95484532c0107a07c87
tests: out/editor-preview9-marker-capacity/receipt.json; out/preview9-c58-count-1025-clone-crash-preserved
signature: runtime C08A1F / preferred ED8A1F reads old marker through actor+18 after cold1025 clone
docs: docs/re/adventure-complexity.md#complexity-marker-storage-lifetime
related:
---

## Report
The genuine generated Sound carrier cold-opens with1,025 markers,118 classes
and5 acts. Both participants reach shared R0, strict and fault-free. Enabling
Extended reaches R1, then the host's first normal Alt clone crashes while native
UI refresh reads an existing actor's marker. The source carrier's decoded
authored bytes are exact; this is not a duplicate serialized placement ID.
Original crash/dump/log/checkpoint evidence is retained in
`out/preview9-c58-count-1025-clone-crash-preserved`.

## Root cause
The executable loaded at00130000, so runtimeC08A1F maps to preferredED8A1F.
ED8A10 calls6C0230, the leaf returning actor+18; ED8A1F dereferences that cached
marker. Actor0 caches1FCB000C and source24 caches1FCB354C, exactly24*238h apart
in the old arena. Native constructorF2E670 reserves1,024 slots. ReadMarkerRows
reserves exactly the serialized count when it exceeds that capacity; thus a
cold1,025 pool has capacity1,025, while the ordinary419 pool retains1,024.
The next clone reallocates to2,050 and frees the old arena. Native only binds
the newly created actor before synchronous07311FE9 UI refresh enumerates the
old actor caches. The lifted logical bounds had not established storage lifetime.

F2EBC0 protects its source marker by copying it to the stack before growth.
The mod's HeldClone and MarkerAudio retain value/slot identities and resolve
fresh rows. Their cleanup runs after this failing native broadcast, so it
cannot repair the old actors in time. Static and independent dump receipts are
retained in `out/preview9-c58-count-clone-source-audit` and
`out/preview9-c60-marker-capacity-review`.

## Fix
The C60 constructor hook acquires4,096 slots while its native ScenarioResource
still owns an empty, unpublished pool. It uses the verified native allocator/free
pair and changes only begin/end/cap, matching the empty Reserve path. There is
no live-arena promotion or actor-cache rewrite. Extra allocation failure keeps
the valid original1,024 resource; audited native callers can ignore constructor
EAX, so returning null would be unsafe. Read never shrinks, and native Copy
preserves destination capacity for every supported row count.

An immutable value-only policy and atomic retirement fence construction on
the original caller thread, including a resource-factory worker. Main-thread
callbacks also close host/module/generation and allocator identities. Extended
capability, clone/create and authoritative apply/restore require the full reserve;
old or fallback scenes hold with their draft intact. Logical native1,024 limits
and weighted tuning remain unchanged until valid Extended authoring is enabled.
Protocol25 and complexity policy2 exclude unsafe older private binaries; native
Save/recovery formats and the installed personal protocol23 payload are unchanged.

## Verification
FAIL is the actual C58 first-clone crash above. A controlled native-capacity
model independently reproduces the stale broadcast without dereferencing freed
memory. The new fixture invokes the production constructor hook and covers
live1,024 to1,025, cold1,025 to1,026,4,095 to4,096, refusal of4,097 and unchanged
native-default1,024 behavior. It also covers Copy/recovery/late join, failed extra
allocation, callback/reentry retirement and a worker constructor racing policy
retirement. These are controlled allocation/row models, not actual game acceptance
or a full native serializer oracle. Strict actual-i686 passes capacity508,
complexity1,575, Clone838, preset8,910, terrain materialization73,241 and full
adapter153,522,76 compiler/Make inputs with zero drift. The receipt is
`out/editor-preview9-marker-capacity/receipt.json`; its first fixture-only sentinel
setup failure and unrecognized-selector qualification are retained explicitly.
The original running-game cause repro now passes on C60: prior durable R1
recovers at1,025 markers with both peers policy2/Extended Ready; the same normal
clone reaches1,026 at R2, then Undo restores1,025 at R3. Both native SHA and
semantic identity return byte-exactly to the pre-clone values. Only native
count[4] increases630 to631; all six uniqueness counts and thirteen words remain
exact, with no faults or crash. Receipts are under
`out/preview9-c60-recover-count1025` and `out/preview9-c60-count-1025-clone`.
CORRECTION (2026-11-03): PASS now includes actual native4,095 to4,096, refusal
of4,097 with no document/history mutation, exact native Undo/Redo, Native Save
with SavedApplied2/2 and fresh-process cold reopen/shared materialization.
All4,096 placements,118 classes and5 acts survive, both peers strictly converge
and report zero faults. The unchanged strict Saved/cold comparator finds every
decoded byte exact and only one unused scalar-key fourth word different in
the prefix. No machine-ID exception is needed. Evidence:
`out/preview9-c60-count-boundary-qualified/receipt.json`,
`out/preview9-c60-count4096-native-save-r2/result.json` and
`out/preview9-c60-count4096-cold-open/result.json`.
The retained failed harness assertions concern the12-KiB object-list limit and
an inappropriate canonical-hash equality across different revisions. They do
not hide a native crash, authored-byte mismatch or accepted overflow. The
OUT-only Save audit also rejects eight authored/nonfinite metadata mutations;
production validators and the strict Saved/cold comparator are unchanged.
This closes the marker-storage failure in the private candidate. Personal
installation, publication eligibility, four-player breadth and release smoke
remain separate; the installed protocol23 build still requires Extended off.

## History
- 2026-11-03: opened.
- 2026-11-03: exact capacity/cache lifetime cause and strict C60 controlled correction pass; running Native qualification remains open. Personal users were told to leave Extended off until validation.
- 2026-11-03: actual C60 cause repro passes1,025 to1,026 and exact Undo; larger boundary/Save acceptance remains open.
- 2026-11-03: actual4,096/overflow/Undo/Redo/Native Save/cold acceptance passes; fixed in private C60, not yet installed personally or released.

Legacy fork reference: `BUG-0475` before upstream 0.7.4 integration. Existing commits and OUT receipts retain that historical ID; upstream owns its independent ticket under that number.
