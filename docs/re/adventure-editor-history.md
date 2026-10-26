# Native Adventure Editor history retirement

Evidence applies to GOG 2024 `SporeApp.exe`, SHA-256
`dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`.
The exact entry, return, caller and field receipts are checked by
[verify_editor_history_dc04.py](../../tools/addrport/verify_editor_history_dc04.py)
and rendered into
[overrides_editor_history_dc04aee5.sym](../../data/symbols/overrides_editor_history_dc04aee5.sym).

## Native ownership and reset operations

The native history object is global `016065D8`. It contains its owning vector at
`+0`, vector end at `+4`, cursor at `+14`, mutation serial at `+18`, not-editing
flag at `+20`, and owned pending entry at `+24`. The serial's absolute address is
`016065F0`. These addresses have `160`, not `16C`, after the leading zero.

The retained-entry limit is `+1C`, preferred address `016065F4`. The private C7
process used image base `003F0000`, so the observed runtime address was
`015F65F4`; its bytes were `19 00 00 00`, a limit of 25 entries. Native
`F34170` compares the cursor against this limit before appending. At capacity it
shifts retained entries and releases the retired owning entry, then appends and
advances cursor and mutation serial. The static decompilation is recorded in
`out/editor-native-re/history-query.txt`. Native history is therefore already
bounded; an additional arbitrary periodic rebaseline is not required to impose
this limit. The collaboration journal has its own independent retention rules.

`E25BB0` is the native owning-vector erase operation. Its verified signature is
`thiscall(historyVector*, entry** first, entry** last)`, with `ret 8`. It moves
surviving entries using `678280`, releases each removed primary interface through
vtable slot `+4` at `E25BD5..E25BE7`, and reduces vector end at `E25BF8`.
Erasing the full `[begin,end)` range releases the native references and retains
the vector's allocation for subsequent native gestures. It does not reset the
history cursor or mutation serial itself.

`F322B0` is an isolated native discard operation with no arguments and a plain
return. It clears global pending-entry `016065FC` before releasing that owned
primary interface through slot `+4`, then sets `016065F8` to one. It does not
restore the pending gesture's before snapshot and does not erase committed
history. Four exact native callers are recorded by the verifier.

Native scenario lifecycle paths inline the full reset. Verified sequences at
`F41BDD..F41C1C`, `F44755..F44795`, `F44E17..F44E58`, and
`F466BF..F46702` set cursor zero, erase the full vector, increment the serial,
set not-editing, and release the owned entry. Their surrounding routines perform
other load/reset operations and must not be called as standalone history clear
functions. No isolated native `cScenarioEditHistory::Clear` was found.

`EF28E0` clears a different global vector at `015ACCA4`. Its appearance after
several native history resets does not make it a history-clear or UI-refresh API.
The history retirement adapter does not need to call it.

## Detaching a shared draft

Native history entries contain complete before/after resources from local native
gestures. Canonical remote application does not append equivalent native entries.
Resuming ordinary native Undo over the old vector can therefore overwrite the
shared draft with a stale local snapshot. Checked collaboration history and native
single-player history have different ownership and must be retired separately.

Detachment must first finish native gestures and saves, invalidate queued native
capture callbacks, and restore the last canonical owned scenario. Only that exact
settled editor context may have its old native vector retired using the verified
operations. A partial gesture must not be silently detached or have its pending
entry released before a valid canonical restoration. Native pending-entry discard
does not itself perform restoration.

Native dirty getters `F3B2C0` and `F3B7C0` test the unsigned relation
`globalSerial > data+80`, then ordinary authored-change byte `data+84`. The latter
getter also recognizes the distinct imported/new byte `data+85`. Native commit,
Undo, and Redo increment the serial. A rebaseline that increments the serial must
preserve the previous dirty predicates as well as `+84/+85`; simply retaining a
clean draft's old saved counter makes it dirty. Serial overflow must be rejected.
None of these operations constitute a native Save or permission to clear changes.

The adapter's `editor_native::rebaseline` now performs this verified composition
only when ready, not applying, without an active gesture or pending captured
entry. It rejects invalid native vector bounds and exhausted serials before any
write. It sets cursor zero, calls the native full-range erase, increments serial,
and invokes native pending-entry discard. If the old counter was clean
(`oldSerial <= savedCounter`), it advances the saved counter to the new serial;
otherwise it preserves the old smaller saved counter. Authored/imported dirty
bytes remain unchanged. Successful retirement advances adapter generation so a
delayed old capture cannot apply to a future editing session.

The runtime retains shared native input guards during asynchronous close. It
first keeps the controller and party transport running until active participants
cover the latest accepted canonical revision and complete any matching native
Save checkpoint. Saved completion uses a host-assigned serial and explicit guest
`SavedApplied` acknowledgement after native restoration and durable checkpoint;
transport receipt alone does not prove application. Offered, departed and frozen
participants do not hold this barrier. A local native freeze is published through
the controller while its recoverable draft and native input guards remain owned.
It then waits for native gesture/save completion, restores the canonical owned snapshot
once, waits for capture retirement, then ends the controller and releases the
project store. An authoritative party editor exit follows the same path on
guests. If the native editor generation was replaced, it detaches without
restoring the old snapshot or clearing the new context's history.

Recovery follows the same capture-retirement boundary after restoring the durable
draft and completing any mandatory new native Save As copy. Its pinned native
generation is checked before any restoration on each retry. Pending native
captures hold retirement while new gestures are blocked. Successful rebaseline
advances generation before a new collaboration epoch starts, so an old queued
gesture from the same physical editor cannot restore the previous draft into the
recovered session.

## Deferred noun retirement during inverse capture

The ordinary scenario remove path `F416C0`, thiscall with one marker argument
and `ret4`, calls `F3FC30` to remove the actor, then `F40F90` to release the marker
slot. The actor path resolves the marker through the scenario's `data+B0` map,
calls native `B22560` at `F3FDBB`, and erases the marker-to-actor and reverse maps.
The slot path sets the deleted bit `80000000` and links the slot into its free
list. Native `F44460` allocates or reuses a slot through `F2EBC0`, creates its
actor through `F3F8C0`, installs the scenario maps, applies marker state through
`F3D930`, and initializes the actor through `F40A50`.

`B22560` does not synchronously retire all noun ownership. It marks actor `+20`
at `B22566`, retains it, and appends that owned reference to the manager's
`+80..+88` vector at `B2257B..B225B4`, before calling `B20CC0` and `B200F0`.
The ordinary `B228F0` UpdateModels pass clears this vector at
`B22B09..B22B1D`, releasing its queued references. This proves deferred lifetime;
it does not prove which render or model teardown side effect causes a recreated
host object to disappear. In particular, `B20CC0` also invokes an actor virtual
method and must not be assumed to defer every side effect.

Private C6 observation found a locally created host actor absent after the
adapter's inverse deletion and immediate canonical recreation, while the same
creation rendered on the guest. The runtime therefore holds the typed request
after inverse application until `SkHost::frame_index` advances. Repeated
outside-frame callbacks in the same frame do not satisfy the gate. No guessed
native queue drain or forced model update is called. Input stays blocked while
the request is held, and close, recovery, disconnect or generation replacement
invalidates it before submission. Other placements may advance the canonical
revision during this wait: their changes remain intact and the held operation
still receives the normal field/object conflict checks. These guarantees have
runtime fixture coverage; actual render recovery remains a private-game gate.

## C5 private-profile history audit

The apparent missing-own-Undo report from private instance 87 was checked by
copying only the mod's `checkpoint.bin` and `journal.bin` into an isolated audit
directory, then opening that copy with the real project store and typed decoder.
No native creation database or active project file was changed.

For project `9d59108181055d479079d18d0e9efd5d`, epoch
`2761728298364532940`, the durable revision was two. Its one original gesture was
Create, author capability `0:2`, sequence one, original revision one, transition
revision two, `applied=false`, and `redo_available=true`. Decision sequence two
was accepted Undo, not a second forward Delete. A further Undo correctly had no
eligible applied original gesture; Redo was the expected next history action.
This audit does not invalidate the separate native stale-before-snapshot finding.
