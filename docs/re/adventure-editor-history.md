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

## Native creation return camera boundary, GOG 2024

The private preview 9 C6 creation probe returned the host to Scenario with
`editing=1`, `ready=1`, and no native faults, but the active App camera remained
`A50C91D5` (`Editors::EditorCamera`, registry index 29). The registered Adventure
Editor camera `00EBB801` still existed at index 40. The read-only receipt is
`out/preview9-creation-probe/camera-native-readonly.json`. This is an observed
camera ownership failure, not evidence that the saved creation was lost.

The native mode-return message `00E11332` is handled at `7D827B` by passing the
message's `+08` mode ID to `cGameModeManager::SetActiveMode` (`7D8CF0`). Its
`SwitchToIndex` (`7D8670`) calls the old mode's `+1C` OnExit and the new mode's
`+18` OnEnter. It has no generic camera restoration. Scenario OnEnter
`EF24C0` sets mode `+DC=1`; the first ordinary Scenario update consumes that
flag at `EF2F3C` and invokes editor resume `EF0990` at `EF2FB2`. That resume
unconditionally calls `EFF670` at `EF09A3`; its complete path reaches
`EFDDC0` at `F0007A`, which activates `00EBB801`. A completed ordinary native
editor resume therefore restores the orbit camera. The earlier direct-access
audit of `EF0990` missed this indirect call. The C6 receipt does not establish
whether resume was incomplete or another later action replaced the camera.
The adapter's history restoration does not directly clear mode `+DC` or the
native Scenario initialization state.

The narrow native camera ownership boundary is `B5CEF0`,
`thiscall(SimulatorSystem*, uint32 cameraID) -> bool`, `ret4`. Native edit-world
camera setup `EFDDC0` passes `00EBB801` at `EFDDD0`. The boundary obtains App's
camera manager through game-mode-manager slot `+50`, invokes camera-manager
slot `+34` (`7C6750`, SetActiveCameraByID), then obtains the actual active camera
through slot `+38` and casts it to orbit interface `011966ED`. It stores that
interface in singleton camera state `+0C`. `B3D320` returns the singleton storage
address `0167EAC0`; `B3D330` returns its word-zero SimulatorSystem pointer. The
cached orbit pointer is therefore not `SimulatorSystem*+0C`.

The Boolean return is explicit: `B5CF44..B5CF50` tests the orbit result, writes
the cache, uses `SETNE CL`, copies `CL` to `AL`, and returns. The null-active path
at `B5CF53..B5CF61` writes a null cached orbit and returns false. Failure to find
the requested ID can leave the old App camera active, so a true native return
alone is insufficient. The adapter prevalidates the registered `00EBB801` orbit
object, calls this paired boundary, and then verifies both exact active-camera
identity and cached orbit identity. No raw camera-manager fields are written.

The adapter requires the exact project, epoch, participant generation, creation
session serial, returned native generation, and entry scene owner. The local
owner witness retains only numeric mode/data/resource identities and the actual
source key; return compares freshly resolved objects without dereferencing a
retained address. Ordinary OnEnter `EF24C0` and initialization reset `EF1BC0`
use the current ScenarioData, rather than the separate cold scene allocation
path at `EF1D20`. An unexpected owner replacement rejects this return.
Runtime authorizes only its
current, not yet materialized creation return, and all native access occurs in
the outside-frame drain. Its read-only resume probe requires global
`015ACC34 == 4`, consumed mode `+DC`, cleared SimulatorGameModeManager `+28`,
and a visible native world layout, in addition to ordinary editor readiness.
Scenario Update `EF2EF6..EF2FB2` verifies these transition/initialization/OnEnter
states before editor resume. The named manager getter `B3D420` returns the
singleton at `0167EAEC`. No adapter writes these flags. Camera activation is an
idempotent fallback after this settled state, not a substitute for native resume.
Non-Scenario, playtest, unready, active-gesture, replaced scope, missing camera,
or missing owner rejects before activation. The broader
`EFDDC0` setup also focuses the authored captain and loads its camera bookmark;
the adapter does not call it. The runtime restores its separately pinned local
camera and selection and waits for scene acknowledgement afterward.

The static verifier pins the paired function's entry, three return paths,
native caller, manager slots, cache store, and exact singleton getters. Focused
i686 fixtures cover rejected scope/context/session/generation, inactive named
camera validation, native refusal, registry replacement during activation, and
a false-positive activation that fails to update the cached orbit. Running-game
creation Save/Cancel return acceptance remains required after deployment of
this candidate.

## Immutable gesture metadata witnesses

The ordinary deferred history drain obtains its before and after resources from
native history entries through `capture_resource`. This resource has no owning
ScenarioData name, description, tags or genre, so the former drain silently
left `Snapshot.metadata` absent. Public `capture` separately populated that
sidecar from ScenarioData. Consequently, a release-time snapshot and the
otherwise identical committed snapshot always missed runtime's exact authored
comparison and unnecessarily ran opaque normalization. This finding identifies
extra work; it is not a measured performance acceptance result.

Native Start now copies bounded UTF-16 metadata at the actual before boundary.
Native Commit independently copies the actual after values after its original
native finalizer. Pending history retains these values alongside its existing
project/epoch/participant, native generation and owner fences. Speech coalescing
keeps the first before and the latest after. The drain attaches both witnesses
without reading the current strings. A failed boundary read rejects the capture;
it cannot replace both witnesses with the later metadata and hide a mutation.

The real opaque-change guard rejects differing known metadata. Absent optional
metadata remains explicitly unknown for older recovery snapshots, rather than
being treated as equal to a current value. New queued native gestures require
both witnesses. Validation and opaque comparison also check asserted captain
identity/lock sidecars against the privately parsed resource. Projections older
than Captain tracking retain their default unknown values until native expansion
hydrates them; typed current Captain settings always assert an identity. Exact
validation-cache keys include source, captain, lock and metadata as well as bytes,
bindings, target document, act and native generation. Local dirty flags remain
local rollback state. Focused i686 fixtures cover immutable before/after strings,
coalesced metadata, malformed strings, known metadata changes and forged captain
sidecars; the private game benchmark remains the performance gate.

## Metadata caption refresh without an authored mutation

Restoring ScenarioData metadata through the lower owning string assignment
updates the native value but skips the title binding's NameChanged event. The
ordinary `SetName` (`F3CC00`) cannot repair an already equal title: it returns
early, and its other path also marks ScenarioData's imported flag. Native
`7AA519DC` is instead sent synchronously through IMessageManager slot `+14`
(`8854A0`, MessageSend), using a 20-byte packet with ScenarioData's `+4`
nameable interface at packet `+0`, null-terminated UTF-16 text at `+8`, and
zero at `+10`. Both reviewed title receivers consume these fields inline;
`MessagePost` at slot `+18` is unsuitable for this temporary packet. Full native
sender, receiver and lifetime receipts are in the creations RE notes and the
exact-image editor ABI verifier.

After metadata and native resource restoration, the adapter sends this bounded
notification for its pinned mode/data/resource/source owner, native generation,
and logical scope. It resolves and checks that owner again after the synchronous
call, preserves saved revision/dirty/imported flags, and confirms the metadata
still matches. It sends no authored transaction, history item, or resource
change. Focused actual-i686 adapter fixtures check the precise packet and
synchronous copy, unchanged draft/history/selection/view, dirty preservation,
missing dispatcher, and owner/generation/scope changes during notification.
Native visible-title acceptance remains a separate running-game check.

## C8 pre-entry captain-sidecar regression

The first C8 guest rejected an Adventure Town baseline with `The native captain
identity differs from its authored sidecar` before native entry, with capture
and restore counters both zero and no native faults. Its source evidence is
`out/preview9-c8-four-town-bootstrap-inspection/bootstrap-failure-current.json`.
The retained host revision-zero checkpoint independently asserts captain
`40626200!07E89007.2B978C46`, unlocked; the extracted native resource has SHA-256
`4eaa187acb73b601c08804c3bd9856abffc548f11a56483902e33ebf4da9a79e`.

`validate_entry` privately parsed the resource and built a fresh typed projection
before recursive validation, but left that temporary Snapshot's captain sidecars
at their zero defaults. The new strict assertion therefore rejected its own
otherwise valid projection. This constructor omission did not establish a bad
resource transfer or a native scene mutation. Entry validation now hydrates its
own captain identity from the verified projection, retaining raw legacy values
only where no typed Captain row exists. The same internal hydration helper is
used consistently by capture, expansion, owned edits and both prepared-snapshot
paths; supplied snapshots still receive the strict independent native check.

A focused actual-i686 adapter regression reproduces the original unhydrated
failure with the checkpoint's nonzero captain, then exercises `validate_entry`
success while the active game mode is Galaxy, before any native entry or restore.
Forged supplied captain keys and lock flags still reject. Native running-game
baseline admission remains the next candidate's acceptance check.

## Exact act-roster projection cache

The C9 four-player Adventure Town run completed 120 valid edits with strict
per-operation convergence, but guest confirmation and frame timing still missed
their budgets. Each authoring projection previously built one owning resource
fragment per act, performed NativeWrite/NativeRead, normalized native goal
validation flags, then wrote the canonical ActBlock. Town has five acts, so
unchanged act properties repeated this work during placement-only commits.

ActBlock projection depends only on that act's complete authored bytes, fixed
fragment defaults, and the verified native Read/Write routines. Its reference
metadata comes from enabled goals' raw class IDs. It neither resolves server
assets nor filters references through the current class roster. Captain and
crew projection have independent server-resolution dependencies and remain
fresh on every call, including cache hits.

The adapter now takes one owning CopyActs (`DFF620`) of the entire ordered act
array into a detached native resource and serializes it once. Exact fingerprint
bytes, source, native cache context, project/epoch, ordered stable act/settings
identities, and authoring schema key immutable ActBlock values produced by the
existing checked per-act path. Every hit still reconstructs act order, settings
identities and next_id. No native pointer, native allocation, revision-only
proof, or partial layout digest survives in the cache. Owning copy, NativeWrite,
canonical Read and destruction remain on the game thread outside rendering.
The cache retains at most eight versions and 32 MiB; status reports separate
act_cache byte/entry/hit/miss counters. Scene replacement and shutdown retire its
context. Failed optional allocation or fingerprint serialization falls back to
the unchanged per-act path.

Actual-i686 production adapter fixtures exercise cache hits with no per-act
NativeRead, nested goal/reference and opaque-byte changes, roster creation,
deletion and reordering, stable identity/source/project/epoch/context changes,
and unrelated placement edits. They also verify fresh captain/crew server
remaps, locks and capacity, resolver failure, optional fingerprint allocation
and Write failure, bounded eviction, and value lifetime after native resource
destruction. Native fixture and ABI validation establish implementation
behavior; performance improvement remains a separate next-candidate measurement.

## Crew picker identity collisions across native profiles

The C9 ordinary Crew Add test selected Spice Crawler at
`40626200!29058782.2B978C46`. Two observers rejected the uploaded closure at
`40626200!29058782.02D5C9AF`; the host and the observer cloned from the same
profile advanced. There were no native faults. The first reported difference
was a generated Species summary, but the read-only authoritative-model audit
also found different valid BEM records under that owner: 2,425 bytes on profile
95 and 7,199 bytes on profile 96. Evidence is retained in
`out/preview9-c9-crew-failure-probe/offline-source-records.json`. Omitting only
the summary, or weakening the same-key/different-bytes guard, cannot fix this.

The selected owner closure must pass existing immutable project-version
allocation before canonical submission. This requires no new cast entry.
Native `crew_assets` privately parses and canonical-validates the bounded Crew
fragment and returns ordered keys, retaining duplicate slots. Legacy drafts
without an advertised Crew schema return no keys; malformed advertised rosters
reject. `remap_crew_assets` accepts at most three unique slot edits with checked
expected keys and replacement identities of the same type/group. It changes
only the selected row's primary ResourceKey and canonical local machine/server
hints. All other bytes in the native 0x38-byte records, unchanged duplicate
slots, order, count and capacity remain intact.

Both canonical Crew fragments and main scenario snapshots are independently
owned. Main rows are checked again through verified server-key resolution
before mutation. The result retains every unrelated Document value, stable ID,
revision and allocation counter; full parsed validation and the existing opaque
change guard run before it becomes an output. Success and failure leave live
native scene/history/selection state untouched. Runtime must validate the
original captured native delta before this sanctioned identity rewrite, stage
the versioned dependencies, and retain the unchanged content-conflict guard.
Focused actual-i686 tests cover duplicate slots, opaque record bytes, stale
reorder/removal targets, server-backed hints, malformed and missing rosters,
capacity and atomic failure. Running-game cross-profile acceptance remains a
separate next-candidate check.

The C10 four-player native follow-up confirmed one capacity-plus transaction
and one Crew history item at revision 1, with exact native convergence and no
faults. The unique Spice Crawler card was then selected and accepted through
the ordinary picker. Before canonical submission, the runtime rejected it with
`A verified local creation is required for shared content versioning` and
restored the committed draft. All four stayed ready at revision/durable 1 with
matching native checks and zero faults. Evidence is in
`out/preview9-c10-crew-capacity/result.json` and
`out/preview9-c10-crew-select/rejection-all-four.json`.

Native snapshot projection intentionally resets its Document and projects
authored values/identities without populating the session project UUID. The
runtime versioning call passed that zero native-projection UUID to
immutable_version, rather than the current fenced canonical context. This is
an input-provenance failure before version allocation, distinct from the C9
cross-profile model conflict. The rejected C10 after-image was not persisted,
so the retained unique card/UI evidence does not constitute a new raw selected
ResourceKey capture. The narrow runtime correction and its next native
acceptance are tracked separately.

For ordinary Crew picker automation, use the visible Sporepedia control
`53C6DC80` as readiness. C10's visible header was `Choose one or more Creatures`;
the hidden/clipped `Choose A Crew Member` caption is not a reliable wait target.
The C9/C10 empty-slot point `(998,489)` is measured at 1024x768 only and must
still hit both native `03D982FF` and `03D98298` before clicking. Capacity-plus
remains `0774B060` and the Crew lock checkbox is `07C4B4A0`.

The C11 custom Crew Add crossed the runtime project-provenance check but was
rejected by the native helper with `The adventure crew version changed an
unrelated native identity`. All four restored revision/durable 1 with matching
native checks and zero faults; the rejected Add produced no history item.
Evidence is in `out/preview9-c11-crew-add/rejection-all-four.json`.

The ScenarioResource source key is external envelope provenance, not a field
of its authored serialization. Constructor `F2E670` zeros resource offsets
`+8/+C/+10` at `F2E685..F2E68B`. Read `F2FC40` and Write `F25E00` serialize
metadata from `+18`; their core helpers `F2F6D0` and `F25700` use descriptor
`15AF500`. Its initializer `138C850..138D54B` constructs 34 entries of 0x3C
bytes through `15AFCBC`. Their field offsets are `150,170,174,15C,16C,175,178,
238,2BF4,23C,2C10,B8,F4,7C,78,2C2C,2C0C,68,130,2C50,2C54,2C30,2C3C,2C4C,
2C94,2CA0,54,58,2C6C,2C80,50,5C,60,64`, with no source-key field. Static
receipt: `out/editor-creation-re/scenario-descriptor-138C850.txt`.

The Crew owned-edit callback now hydrates only its detached copy's `+8` key
from the already validated base snapshot, matching the existing prepare path.
The generic owned-edit helper and the exact output source/act/bindings/metadata
guard remain unchanged. A controlled native Read/Write fixture that omits the
source key reproduces the C11 error before this correction. After correction,
the serialized output still omits the key, its snapshot retains the checked
envelope key, and all live scene/history/selection state remains untouched.
The full actual-i686 native fixture passes 12,472 checks, the supported ABI
verifier passes 91 entries, and RE lint reports no supported-build problems.
Actual cross-profile Crew Add acceptance remains pending in the next candidate.

The C12 four-player native follow-up passed the custom Add under DLL
`13410f7065550c9a7b525f3a99c11893d57bce01110fbd4da32b00e639a3b8a1`.
Capacity-plus and the unique Spice Crawler picker selection reached revision 2
with two author-owned Crew history entries. Host Add Undo/Redo reached revision
4; guest 96 Crew lock and its Undo/Redo reached revision 7, retaining exactly
three history entries. All four passed strict native convergence and reported
zero faults at each stage. Evidence is retained in
`out/preview9-c12-crew-add/result.json`, `crew-history-host/result.json`,
`crew-lock-guest/result.json` and `crew-history-guest/result.json` under the same
`out/preview9-c12-` prefix.

The native padlock control ID is reused. Before the guest lock click, its
measured point `(977,466)` at 1024x768 was checked against the actual Crew card
`03D98304` and header `064A90A0`, with lock `07C4B4A0`. The Captain lock was
separately visible near `(1008,719)`. The resulting history label was
`Edit adventure crew` under the guest author. Read-only hit and screenshot
evidence is in `out/preview9-c12-crew-lock-calibration`. The Crew card interior
appeared blue/blank in that screenshot, so these results establish canonical
content admission and history, not visible thumbnail/model rendering. Crew
replacement/removal and that visual follow-up remain separate acceptance work.

All four then used Shared End, party Leave and ordinary native Cancel/Exit with
Quit discard to return to Galaxy, with zero faults. No Terraform, new class or
creation-editor changes were made during this core Crew check, and no runner
stop request was written by the fixture. Retirement evidence is retained in
`out/preview9-c12-crew-retire/result.json`.

## Immutable creation thumbnails and native pixel carriers

The C12 blank Crew card is a missing presentation resource. Both the host after
Add and the guest after lock show an empty blue interior, including hover.
Native Crew refresh `ED5550` obtains the Crew key at `ED568C`, changes only its
type to PNG `2F7D0004` at `ED56B6`, and invokes card vtable90 `E21E30`. That
function forwards the exact owner-group image key to `807CA0`/`806320` and the
image pool. The missing-image path at `833F34` constructs an empty image. The
actual delivered alias `40626200!7D824BB6` contains metadata (656 bytes) and
BEM (2,425 bytes), with no PNG in either owner group or group+1. Receipt:
`out/preview9-c12-crew-thumbnail-offline/aliased-records.json`.

The selected original PNG is 128x128 RGBA8, 26,339 bytes, SHA256
`e846add23bd3d4b3ce9f1e3bae6ca4a57e6213532a976ad32bea0acafdcf7632`.
Its record was read from the closed private source profile, without changing
profile files. Reproducing `PngEncoder` header decoding establishes top-down
BGRA byte order: the native header is `16 0D 45 02 34 09 00 00`, meaning magic
`02450D16` and payload length 2,356. Other channel or vertical permutations do
not match. The alternate native hash mask yields `16 B5 0E E3 BB B0 94 2F`.
Evidence is in `source-header-analysis.json` under the same output directory.

`68E600` initializes bit-plane limit8; `68EB40` decodes RGB24/RGBA32 pixels
without changing that limit. The pixel carrier at `68DDB0`/`68DFC0` uses masks
1, 2 and4, an LFSR polynomial selected from `1403688` by the highest set bit
of decoded byte count, and FNV seed `811C9DC5`. It XORs each carrier bit with
FNV bit15, so clearing low bits does not imply an all-zero decoded header.
`68E660` tests the first little-endian word against `02450D16`, then retries
from reset state with hash mask `~1` instead of `~7`. The private portable
carrier witness reproduces both paths and fails closed when it cannot obtain
both complete headers. PNG ancillary and trailing data must also be discarded
because native `68E990` supports a separate non-pixel data path.

The independent codec fixture checks real Windows WIC decoding, deterministic
core-only RGBA8 output, all pixel upper-five bits and alpha, both native header
rejections, RGB/tRNS and Adam7, stored-DEFLATE block boundaries, PNG CRC and
Adler32, bounded formats and atomic rejection. Direct WIC and its format
converter both returned success for valid-CRC PNGs with invalid DEFLATE, bad
or missing Adler32, invalid zlib headers, and trailing compressed junk. Strict
bounded RFC1950/1951 validation before WIC is therefore required; the fixture
retains all these counterexamples. Diagnostic receipt:
`out/preview9-c12-crew-thumbnail-offline/wic-deflate-diagnostic.txt`.

Strict Linux and actual-i686 carrier fixtures pass 32 checks each; independent
stored/fixed/dynamic DEFLATE corpus fixtures pass 346 checks on each platform.
The actual-i686 WIC fixture passes 2,234,201 checks including the selected native
PNG and independently decoded RGBA comparison. Sanitized output is 65,737
bytes, each channel differs by at most 7/255, and neither native header variant
matches. This establishes the codec and recorded image behavior. Visible Crew
rendering through a newly delivered alias remains a separate native UI check.

### Terraform proxy materialization, exact authored provenance

The first actual C12 Raise Round Terrain card drag reached native history, but
release rejected an ambiguous recipe identity. The frozen read-only capture in
`out/preview9-c12-terrain-rpm/result.json` contains 181 host effects versus 180
observer effects and the same 102 models. All prior keys and effect scalars are
exact. Twenty-seven old effect transforms and 92 model transforms increment
their native WORD at Transform+2; three old effects and seven models also change
translation by one to three float ULPs. Counter-only identity normalization is
therefore insufficient; accepting approximate positions would hide real edits.

Verified native `F35E10`, now named `TerraformMaterialize`, reconstructs editor
proxy nouns from the recipe. `F37530` calls it at `F37858`; rebuild's validation
path calls it at `F37CC6`. It consumes no stack arguments and returns through
`F3621E` or the final proxy Release tail at `F36215`. Its effect path calls
`F325B0` at `F3603B`: noun slot+2C supplies position, then `F32603..1A` copies
XYZ into Transform+4/+8/+C, ORs flag4 and increments WORD+2. The model path
performs the same writes inline at `F36164..79`. `F33C10` only reads its input
Transform: position and matrix are copied to stack arguments before native
proxy setters. These receipts are checked by `verify_editor_dc04.py`.

Transform assignment `537E10` independently compares flags, offset, scale and
rotation, increments WORD+2 at `537E83..91`, then copies actual geometric data.
`PreTransformBy40CCB0` increments that word too; `ToMatrix4` ignores it. Record
identity matching may exclude only that counter and retain the complete old
committed payload for an otherwise exact match. Ordinary projection now rejects
reordering distinct committed stamps; checked whole-recipe Load can allocate a
replacement sequence. No numeric tolerance is introduced.

The owned materialization witness brackets the verified proxy boundary and
accepts only unchanged rows or exactly the audited XYZ/flag4/counter increment.
Exact prior rendered payloads can chain back to authored input. Verified native
Append and Update mark fresh row indices, so new or genuinely edited input
cannot borrow another stamp's old authored coordinate. Ambiguous output aliases
reject. External preset Load and native scene/owner/project/epoch/participant
changes retire the witness. At most four bounded primitive-byte maps survive,
including before-history copies; no native pointers are retained as owned data.

Only an exact rendered payload match can replace transform bytes in a privately
serialized snapshot. Live vectors, native save buffers, unknown planet fields
and the adventure DBPF body remain unchanged. Persisted snapshots contain the
authored recipe itself, so recovery never depends on this temporary witness.
Unaudited output fails through the existing deferred synchronization failure
path before further native authoring. Running-game acceptance of this adapter
is still required. Strict portable and actual-i686 terrain fixtures each pass
559 checks, including the retained 282-to-283-record evidence and independent
negative cases for real field changes, duplicate provenance, stale order and
two appends.
The production adapter's actual-i686 fixture additionally passes 163 focused
checks for the verified hook boundary, exact owned normalization, fresh-row
marking, stale owner/scope/source/generation rejection and deferred failure.
The complete native fixture passes 13,586 checks with these additions. A final
row deletion retains its before-history witness even when the current recipe
is empty; C13's initial helper lacked that case, and C14 includes its regression.

### Terraform new-proxy Update precedes recipe allocation

The actual C14 Raise Round Terrain card drag opened native history while held,
but release produced no transaction, no failure and no changed recipe columns.
Both peers remained at revision 62 with exact convergence. Evidence is in
`out/preview9-c14-terrain-card-drag/result.json` and
`out/preview9-c14-terrain-rpm/result.json`. The authored-row guard introduced
for materialization incorrectly required an already allocated recipe index.

Verified `TerraformUpdate F381D0` reads the selected proxy descriptor at mode+1B0.
Its branch at `F38229` tests mode+1B8: true means a new proxy, and descriptor+8
is not used to update an existing row. Definition+68 selects model append
`F382A3 -> F37150 -> F37225 -> F35890` or effect append
`F382D3 -> F37240 -> F37328 -> F35910`. Preview creation passes the current
recipe count into `F33C10` at `F36488`, inserts the descriptor into mode+19C's
16-byte proxy vector at `F36538` or `F36545`, and sets mode+1B8 at `F3654E`.
It immediately resolves that new actor through `TerraformSelection F32630`
at `F36571`. That lookup scans the proxy vector by descriptor+0 actor identity.
Thus a valid new proxy can be resolved before its recipe row exists.

Only the new-proxy Update route bypasses the existing-row bound. The adapter
pins current mode, descriptor, actor, definition, index and preview flag across
the existing ownership/history gate, then verifies the actor resolves back to
the same descriptor. Native Append marks the actual allocated row. Ordinary
Update and Remove retain their original strict index bounds. No existing row
is marked merely because a preview index coincides with it. Actual-i686 hook
fixtures cover both native kinds, coincident/count/sentinel preview indices,
exactly one append and one native history item, denied ownership, changed
selection/owner/generation/scope and malformed descriptors. Focused fixtures
pass 476 checks; the full Native fixture passes 13,899. Running-game acceptance
of this fix remains pending.

### Retaining exact terrain provenance during a local creation editor

The C14 guest Vehicle Save reached durable revision 70, then failed the strict
creation-entry terrain check before any canonical restoration. The guest's
restore counter stayed 41 while capture advanced from 383 to 384. Read-only
sampling in `out/preview9-c14-vehicle-failure-rpm/result.json` proves that every
live recipe column still equals that participant's pre-creation revision-62
sample in `out/preview9-c14-terrain-rpm/result.json`: 180 effects and 102 models,
with no new terrain mutation. Mode departure/return changed native generation
and correctly retired the active materialization map, while the native tool
retained its already rendered recipe.

Both copied project journals independently recover revision 70 and identical
60,799-byte native payloads, SHA256
`afc5475620bd8cab886f6bbd0b8fe3fe6b4ef2960351b9b5063870129d7ee668`.
Compared with those authored bytes, both live recipes differ only in the
previously audited counter/XYZ Transform output: 27 effects and 92 models.
Keys and scalar columns are exact. The offline extractor used the production
frame/delta readers without opening a Store or repairing either original or
copied journal. Artifacts and the complete per-record comparison are under
`out/preview9-c14-vehicle-store`.

The participant-local creation ticket now owns an exact copy of the previously
verified materialization map, rendered recipe and entry snapshot. It never
owns native objects; retained mode/data/resource/tool addresses are compared
only against freshly verified owners. Capture requires the current authorized
project/epoch/participant/creation serial and native scene, validates the entry,
and proves that the existing exact map normalizes the raw recipe to that entry.
Combined owned values, including vector capacity and conservative map storage,
are bounded to 64 MiB before copying. Allocation or budget failure rejects
entry without changing the adventure.

On ordinary return, the ticket is attempted once, after native initialization
settles. The same mode/data/resource/source/tool, scope, serial, exact entry
bytes/bindings/metadata/document identities and explicitly supplied current
generation must match. The complete current raw recipe must equal the saved
rendered recipe exactly. Changed counters or numeric values reject; no new
normalization is inferred. The runtime records successful terrain resume
before later camera/scene retries, and retains the original strict entry and
final canonical validation. Entering a new adventure scene uses fresh native
materialization from verified canonical bytes, without transferring the token.
Actual-i686 fixtures cover successful generation retirement/reattachment and
strict draft capture, one-use rejection, stale owners/tickets/schema/acts,
changed raw data, incomplete resume, allocation failure and the combined bound.
Running-game acceptance of this creation-return fix remains pending.

### C16 current-layout history toolbar traversal stall

C16 DLL SHA256
`39fa1c4380a232104596ac205f838b9688297d6fecf18cdaef4c5cdea77882d1`
stalled both private native editors immediately after shared revision 0. This
is a failed native bootstrap, despite the preceding deterministic toolbar
fixture passing. Owned thread samples show advancing stacks in the new toolbar
collector/current-owner guard and approximately one CPU core per process;
`VirtualQuery` appears in the sampled active path. The evidence establishes
expensive continuing work, not a proven deadlock. No authored gesture was run.

The passive reader pins the exact runner PID, executable/module, candidate DLL,
private profile arguments and hidden HWNDs. It sends no console commands, calls
no native functions and performs no game/profile/audio mutation. Mode, display,
layout, owning list and source identities, plus each inspected raw blob, are
double-read. Actual passive artifacts are under
`out/preview9-c16-toolbar-owned-rpm`, `out/preview9-c16-toolbar-cast-rpm` and
`out/preview9-c16-toolbar-topology-rpm`; CPU samples are under
`out/preview9-c16-thread-samples`.

Verified current-layout ownership and IWindow offsets are:

* Scenario singleton `16C7AA4`, mode+14 display, display+24 UILayout,
  UILayout+14 owning object, owning object+64/+68 pointer-vector begin/end.
* Both actual root objects use Cast thunk `7F3290`: `sub ecx,4` then jump to
  `95F8C0`. Its `EEEE8218` branch adds 4 and returns. The net result is the
  original owning pointer, already an IWindow interface; no speculative root
  adjustment is needed for these exact observed objects.
* IWindow GetParent `95F930` reads +34 and returns stored primary parent+4,
  or null. GetControlID `A02C70` reads +7C directly. Both actual root and
  descendant vtable slots match these verified getters.
* The verified `sui::children` intrusive anchor is IWindow+38; each node's
  first word is its next pointer, and its window interface is node-4. The
  passive reader interprets only that next link. It retains other link words
  without assigning unverified semantics. Node, edge and cycle bounds precede
  each further read; it does not call Cast or a native child/window getter.
* The actual Undo/Redo button vtable is `1441270`. Slot +28 is GetFlags
  `641760`, which reads IWindow+28. Visible/enabled bits are 1/2.

Both actual current-layout trees have exactly one owned root, 429 windows and
428 edges. Each passive traversal completes without a cycle, duplicate parent
or unverified getter. Each tree contains exactly one Undo `B006EF6E` and one
Redo `F006EFA5`, both under parent control `0578EC50`; the observed ancestor
chain includes `0476A9B8` and root `FFFFFFFF`. There are no duplicate history
button IDs in these current-layout trees. Each button and every observed
ancestor is locally visible. Both button flags are 1, meaning disabled, which
is expected at revision 0 and does not establish the earlier post-edit button
failure.

The inventory rules out an actual child cycle, thousands of owning roots or
duplicate history buttons as the C16 stall mechanism. The collector repeatedly
performs full owner/logical checks around hundreds of native window callbacks.
Any proposed memory-probe reuse must end before native callbacks and recheck
identity afterward; a cached readability result cannot authorize a changed
scene. A repair still requires fresh native bootstrap and actual history-button
acceptance. The retained C16 failure must not be relabeled as a successful run.

The source audit also identified a self-sustaining scheduling mechanism, rather
than an unbounded child walk. The 100 ms keepalive worker in `coop.cpp` queues
`keepalive_task`; that task clears its queued flag before calling
`coop_editor::tick`, which reaches native `tick`. The old native fence timestamp
was recorded at scheduling time. If one fence takes more than its 200 ms
interval, a keepalive queued during that work can schedule another fence in the
same bounded 256-task outside-frame pump. Repeated costly fences can therefore
keep that pump busy across many intervals. This mechanism follows from the
verified source ordering; the passive topology and sampled advancing stacks are
the actual C16 observations. Completion-time throttling and an in-progress
guard need deterministic queue coverage and new native acceptance. Neither a
synthetic repair test nor the static mechanism makes C16 a passing run.

### Native toolbar availability bypasses HasUndo and HasRedo

Exact GOG 2024 disassembly identifies why hooking the native availability
functions alone cannot keep the visible history buttons current. Scenario
display Update `ED6440..ED661B` is `thiscall(display*, uint32 elapsedMilliseconds)`
with `ret 4`. Its sole direct native caller is `EF31F7`, inside Scenario virtual
Update `EF2EF0` (vtable slot address `148B014`). The caller passes mode+14 as
`this` and elapsed milliseconds computed from float delta time at
`EF2FE8..EF3010`. Display+24 is the UILayout used by its native button lookups.
This call chain is not proof that the updater runs outside rendering; an added
post-original UI lookup or enable operation would require a separate safe phase.

The native updater does not call `HasUndo ED4750` or `HasRedo ED47C0`. It inlines
reads of the same `cScenarioEditHistory 16065D8` used by those functions:

* Undo `ED6587..ED659E` requires byte `16065F8` (history+20) and cursor
  `16065EC` (history+14) greater than zero.
* Redo `ED65C1..ED65E6` requires that byte and cursor smaller than
  `(end 16065DC - begin 16065D8) / 4`.
* Native FindLayoutWindow `810650` receives current display+24 and recursive
  control IDs `B006EF6E` / `F006EFA5` at `ED65AC` / `ED65F6`.
* The already-existing window-vtable +7C calls pass mask 2 and the computed
  value at `ED65BF` / `ED6609`. Their exact return sites are `ED65C1` /
  `ED660B`. In the observed C16 buttons, vtable `1441270` slot +7C is
  `964560`, `thiscall(IWindow*, uint32 mask, bool enabled)`, `ret 8`.
  Its original base call `9616B0` reads the bool byte and changes flags at
  IWindow+28 before its existing button refresh. The same vtable's control-ID
  slot +1C is `A02C70`, which reads IWindow+7C directly.

The shortcut/message paths do call `HasUndo` at `EDA4D4` and `HasRedo` at
`EDA528`; both explicitly set ECX to `16065D8` first. Therefore a different
history-owner pointer is not the cause established by this audit. The concrete
gap is the toolbar's inline native reads bypassing the co-op availability hooks.
Likewise, the retained C16 current layout contains one actual button of each
ID, so a cached duplicate-button lookup is not established for that scene.

The approved narrow adapter design scopes the existing display update and
changes only the arguments of its two existing SetFlag calls. It requires the
exact return site, mask 2, matching raw control ID and fresh pinned
mode/data/resource/source/display/layout/generation/logical scope. It obtains
the current pure chronological-availability value at the override, after
rechecking callbacks, and leaves unrelated native calls unchanged. Nested
scope storage must restore correctly. It adds no UI lookup, window operation,
cursor write or allocation to the native update and must not reuse a cached
readability check across native callbacks. The new symbol/ABI receipts cover
the exact entry, returns, native caller, inline globals, vtable slots and
argument-call sites. Actual visible Undo/Redo acceptance of this design remains
pending; this static finding does not claim that C15 or C16 passed it.

### C17 rejected Terraform gesture did not restore exact placement orientation

The actual C17 simultaneous BasicTools attempt entered both native histories,
then rejected without an accepted terrain transaction. Its final state reported
Ready and unfrozen, but strict native checks diverged. This is not a successful
rollback. Root-owned passive samples are retained under
`out/preview9-c17-marker253-{rpm,actor-rpm,spatial-rpm}`. The sampler makes no
console/native/window call and preserves the exact private process/image/source
and double-read owner, pool row, path, map and code evidence.

Marker slot 253 has class 1264 and native marker lookup key 253 at marker+228.
Its position matches the reported canonical placement, but both live marker
quaternions are `BF16ACF8 BE2E89B6 3E8838CB 3F3E8043`; the committed quaternion
is `BF16ACF6 BE2E89B5 3E8838CB 3F3E8042`. Thus the reported mismatch exists in
the live authored marker and is not merely a projection/formatting artifact.
The sampler deliberately does not read the adapter's logical binding map:
`marker_id` consults that map before its default `(1<<32)|slot` mapping. The
matching unchanged roster, native key and exact position support correspondence
to reported logical placement `4294967549:1`, without claiming passive logical
binding verification.

The actor lookup is independently verified by `F3D620 -> 645E40`: marker+228 is
the uint32 key, ScenarioData+B0 is the map, map+4 is its bucket array and map+8
the bucket count. Lookup uses key modulo count; each chained node has key+0,
object+4 and next+8, with the end sentinel in buckets[count]. The passive tool
checks the exact lookup instruction bytes, bounds and cycles, and double-reads
all interpreted nodes. It never executes the lookup or Cast.

Both actual actors have primary vtable `146A080`; GetType `C03270` returns
creature `018EB45E`, and Cast is `C02930`. Constructor `C06996` installs spatial
vtable `1469EC0` at creature+C0. Its verified position getter `6E6560` returns
spatial+4 and orientation getter `97E7C0` returns spatial+10. Both actual spatial
quaternions are `BF16ACB6 BE2E8934 3E883921 3F3E7FDC`, distinct from both marker
and committed values; spatial position also differs slightly from the marker.
These observations do not establish a copy from current spatial pose into the
marker. The initial spatial receipt's `scale` label for +20 was unverified and
must not be interpreted: the helper now labels that word as uninterpreted raw
data. Marker scale +20 is separately verified and remains exactly 2.

The static rebuild order is `F37A00 -> F37530`, then grounding `F3E250`, marker
validation `F40040`, and goal validation `F42660`. Grounding calls
`F3E150 -> F3B6A0`; the reviewed direct marker writes are XYZ +4/+8/+C and byte
209. Its `EED640` call writes an output descriptor, not marker quaternion data.
`F3D930` passes marker+10 to spatial orientation setter +3C. Generic setter
`C88860` copies those four words without arithmetic, and creature setter
`C0DF10` preserves the input while updating spatial/AnimatedCreature state.
These reviewed paths do not establish quaternion renormalization. Indeed,
unit normalization would produce different values from the observed 1-2 ULP
changes. `F35E10` writes terrain-recipe Transforms, not Adventure marker
quaternions.

Adapter restore currently validates and parses the owned snapshot, calls native
`F46400`, selects the local act, optionally loads/rebuilds terrain, then restores
selection. It records grounding positions and reasserts sound pitch/gain, but
has no exact quaternion restoration step. The unresolved causal boundary is
the parsed owned input, native scene reconstruction, optional terrain rebuild,
selection or subsequent settlement. A future value-only diagnostic should
compare the exact parsed-copy quaternion against the snapshot document before
the first native restore, then compare these individual boundaries. No epsilon
comparison or blanket transform overwrite is justified by these observations.

### C18 Terraform opaque rejection: cleared None property union bytes

The actual C18 fresh Town simultaneous attempt again rejected without a terrain
transaction. Its owned opaque comparison reported first differences at native
offset 21624 (host) and 21632 (guest), with equal 60,654-byte image lengths. The
expanded diagnostic decoded both 1,229,480-byte adventure DBPF bodies and found
them byte-identical. This establishes a difference outside that body; it does
not establish successful Terraform or rollback pose acceptance.

The retained native Town artifact
`out/preview9-c8-four-town-bootstrap-inspection/host-durable-native.bin` maps both
offsets into property `040539D4`, type 0, flags 0. Its header starts at 21616 and
its sixteen value bytes occupy 21624 through 21639. The actual baseline bytes
are `00000000 D8290000 ACADBB01 06CE1400`. Thus the two reported positions are
value offsets 0 and 8, not recipe, property metadata or DBPF bytes.

Exact native producer and consumer inspection distinguishes semantic presence
from the unused union payload:

* Terraform `F37530` first loads its property list, then clears `040539D4`.
  At `F37544..F3754D` it zeros EAX/EDX and only the stack Property's flags/type
  WORDs. It passes this default Property to list vtable +14 at `F37561`; the
  sixteen-byte inline union is not initialized. Upstream `Property`'s default
  constructor similarly initializes only flags 0 and `PropertyType::None` 0.
* PropertyList setter `6A2E20` calls assignment `542B80` for the existing
  entry. `542BCD..542BE0` copies all sixteen inline bytes; subsequent stores
  copy type and flags. These copies do not turn the unused union into authored
  data.
* Generic Property Write `693360` with flags 0/type 0 takes its default branch
  at `6933BF..6933C7`, then writes sixteen raw bytes at `69357C..693586`.
  Generic Read `694410` takes the matching default branch at
  `6944B2..6944B9` and reads sixteen raw bytes at `694789..694793`.
* The exact executable has only two direct literal references to this property
  ID: that writer and reader `FA6FE8`. The latter calls typed GetArrayKey
  `6A0AE0` at `FA6FEE`. The getter requires type `20` at `6A0B01` and array
  flag `10` at `6A0B0C`; None 0/flags 0 rejects before its union is read.
  Generic property presence can still matter. This audit does not permit
  removing a None property, changing its type/flags, or masking key-array data.

The approved comparison correction is limited to `normalize_supported`:
when BOTH images contain exactly `040539D4` with type 0, flags 0 and sixteen
value bytes, copy the before value bytes into the changed private comparison.
The before image remains unchanged. Presence/type/flags, differently shaped
values, typed key arrays, every other unknown property and the complete tail
retain their strict comparison. The field is not added to editable planet
schema or recipe properties. Exact native instruction receipts are enforced
by `verify_editor_dc04.py`; this adds no callable native ABI or live mutation.

Strict Linux and actual-i686 codec runs each pass 426 checks, including the
retained 60,654-byte Town artifact with independent mutations at both observed
offsets and exact full-image restoration. Negatives retain differences for
absence, type, flags, length, another None property, typed key-array contents,
an unrelated unknown field and the DBPF tail. Running-game Terraform acceptance
and exact settled rollback remain separate pending checks.

### C20 terrain export parent hydration boundary

The C20 private attempt identifies only quaternion placement changes on markers
188, 253, 265 and 293. Its restore-only quaternion refresh subsequently produces
exact ordinary native convergence on both peers. The remaining held-restoration
diagnostic identifies Planet property `00B2CCCB` as absent from `capture_draft`,
while ordinary resource capture retains it. Captain settings are unchanged.
Evidence is `out/preview9-c20-terrain-failure-full/result.json`; these results do
not yet establish a successful Terraform transaction or release of the hold.

`00B2CCCB` is PropertyList's serialized parent key. `SetParent 6A1710` retains
runtime parent `+30` with AddRef/Release at `6A1738..6A1752`. With a nonnull
parent, it creates a scalar ResourceKey from that parent's ResourceObject `+8`
and sets `00B2CCCB` at `6A176E..6A178C`. With a null parent, it removes that
property at `6A17BC..6A17C8`. Its presence and key are inheritance semantics,
not ignored export padding.

`PropertyList::Read 6A2F60` loads an inline runtime parent only for a count with
the high bit set (`6A2F86..6A2FE3`). Ordinary positive-count input reads its
property rows and returns at `6A306A` without resolving a parent merely because
the key is present. The retained C20 stream has positive count `00000024`.
Consequently, an owned fresh parse can contain the parent key while its runtime
`+30` remains null.

Native resource initialization handles this distinction. Getter `67DDF0` is
`cdecl() -> borrowed IPropManager*`, implemented as
`mov eax,[015FD8A8]; ret`. At `F261C2..F261C9`, initialization obtains this
manager and calls `6A3DE0(manager, resource+18)` before building the terrain.
`6A3DE0` is `void thiscall(IPropManager*, PropertyList*)`, with `ret4` at
`6A3EDE/6A3F00`; EAX or AL is not a success contract. It reads the asserted
parent key, uses the child's external group only for a zero key group, and
looks up the `.prop` parent through manager slot `+2C`. That lookup's temporary
output is owning. On Boolean success, `SetParent` takes the child's retained
reference, then the resolver releases its temporary at `6A3EC3..6A3ECC`.
Lookup failure leaves the prior runtime parent unchanged. The caller must
verify the resulting parent identity and required dependency, rather than
testing the resolver's incidental return register.

`TerraformLoadProperties F32A90` copies source properties, then explicitly
calls `SetParent(toolProperties, sourceProperties.parent)` at `F32ABF`.
`CaptureTerrain F27C00 -> F33A50` refreshes and copies the active tool's property
map, then repeats that parent assignment for the destination at `F33A77`.
Passing an unhydrated owned parse to the Load boundary can therefore remove
the key from the tool and later from an otherwise valid owned draft export.
Ordinary resource capture still describes the untouched serialized parent.

The approved correction is limited to resolving and checking the owned Load
input's asserted parent before any live restoration write, with the existing
owner/source/generation/scope fences retained across callbacks. No exported
absence is silently replaced, no parentless input acquires an old parent, and
changed parents remain authored data. The new named symbol receipts cover the
getter, resolver, native caller, Load/export callers, reference lifetime and
positive-count Read behavior. Controlled and running-game validation of the
correction remain required.

### C24 detached terrain generation and host-away canonical merge audit

This is a read-only audit of the supported GOG image SHA-256
`dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`,
using `out/4gb/exe-roundtrip/original.bak`. Exact instruction-range hashes and
disassembly are retained in `out/preview9-detached-terrain-re/receipt.json`.
No game, UI or native function was called. No isolated recipe-to-height-map
generation route was established. In particular, a privately allocated sphere
does not make the reviewed native Generate routine isolated.

* `cTerrainSphere::Generate F9B860` is a thiscall with five stack words,
  ending in `ret14` at `F9B9C8/F9B9DC`. Their complete semantic types are not
  asserted here. Before generation, `F9B867` calls `GetTerrainEditor F48A30`,
  whose entire body returns global `[016C8EB0]`. The borrowed editor is then
  passed to `F67A10` with the sphere as its argument at `F9B871`.
  `F67A10` is `thiscall(editor, sphere)`, `ret4`: it AddRefs a different
  nonnull sphere, replaces editor+74, and Releases the prior nonnull sphere.
  Generate then resets that same editor through `F67D50`. Thus even its
  initial lifetime operations change a globally retained active object.
* Terraform recipe build `F37390` obtains global PlanetModel through
  `B3D450` (`mov eax,[0167EAF8]; ret`). Its first-load branch calls `B8D9F0`
  on that model. Its existing-sphere branch calls Generate at `F37498`.
  `BuildProperties F37530`, `thiscall(tool, properties)`, `ret4`, combines
  property loading with this build, global model state, environment/flora,
  proxies and UI work. The other two direct Generate call sites found at
  `F9336F` and `FAAD86` call this same global-registering entry; they do not
  bypass its editor+74 update.
* `Rebuild F37A00`, `thiscall(tool)` with no stack arguments, reads global
  ScenarioMode `[016C7AA4]` and stores its ScenarioData in tool+198. Its
  dirty path calls `F37530`, then grounding `F3E250`, placement validation
  `F40040` and goal validation `F42660`. This is live scene reconstruction.
* Grounding `F3E150 -> F3B6A0` is not detached height evaluation.
  `F3B6A0` consumes a marker, native grounding context and raw flag (`ret0C`),
  calls descriptor builder `EED640` and query `EEC1D0`, and writes marker
  XYZ and byte209. `EED640` immediately reads the global ScenarioMode and
  its ScenarioData pointer at mode+74. `EEC1D0` returns caller-owned Vec3
  output storage, but its
  `EEC010` height/collision query uses global PlanetModel and collision
  object `[0167ECD0]`. `F3E150` subsequently calls `F3D930`, applying the
  marker to live actors. An output buffer is not a detached scene context.
* `PlanetModel::GetHeightAt B7F0C0` is
  `thiscall(model, const Vec3*) -> float ST0`, `ret4`. Its active-terrain
  branch obtains the sphere's borrowed map set through virtual+0C, then
  calls `TerrainMapSet::GetHeight F92760`. The latter is an instance sample
  with the same argument/return ABI; it samples existing map storage and
  combines base radius+34 with sample times amplitude+38. This does not
  establish an independently constructible, generated map set, nor its
  recipe/environment/collision dependencies and lifetime. It cannot justify
  generating or grounding a different adventure while a creator owns the
  current global terrain state.

The current shared-editor path deliberately avoids those global operations
while a participant is away. The relevant C24 source call graph is:

```
Runtime detached_scene = local_creation || rejection_restore
  check_native_integrity -> validate(current_native, canonical document)
  apply -> before_apply = current_native
        -> prepare(before_apply, accepted document, change, owned artifact)
             -> owned ScenarioResource Read/copy/marker assignment/Write
             -> terrain::merge(owned bytes, canonical recipe/settings)
             -> validate(prepared, canonical document)
             -> supported_changes(base, prepared)
        -> prepared_native (skip apply_prepared while detached)
  persist/poll -> durable current_native (do not advance rendered_revision)
```

In `editor_runtime.cpp`, the detached branch of `apply` also skips resident
resource registration; a terrain change allocates no placement and therefore
does not enter its local-author new-placement selection tail.
`editor_native::prepare` explicitly has no capture/state/restore call. Its
general path parses owned ScenarioResources, assigns typed fields on private
storage, serializes, merges the canonical terrain columns in bytes, and checks
typed projection plus the opaque supported-change comparison. The pure
`editor_native_terrain::merge` decodes the property prefix, changes the known
planet fields/ordered recipe columns, and encodes it while retaining the tail.
Neither this route nor its validation/comparison code dispatches TerrainLoad,
Rebuild, Generate, live grounding or ApplyMarkerState. Native resource
Read/copy/Write and fresh asset resolution still execute on the game thread;
this is resource validation, not a detached renderer or collision proof.

Presence keeps `scene_ready=false` while detached and retains the prior
rendered revision. On normal creation return, `materialize_creation_return`
requires the original owner, complete native initialization/world readiness,
and the one-use entry terrain ticket before checking the loaded entry draft.
It then registers dependencies and restores the **latest** durable canonical
snapshot. Only here does native terrain Load/Rebuild operate on the returned
Adventure scene. It waits for exact owner settlement and a later actual frame,
strictly captures/validates the latest document, then restores local
selection/view before marking the rendered revision/materialization complete.
A changing canonical head causes another latest-snapshot restore. Replacement
owners, expired session context or failed validation do not authorize readiness.

Consequently no new detached height API is needed to merge a guest's canonical
terrain recipe while the host remains in a creator. That merge must not claim
the absent host scene has been generated, grounded or collision-validated.
Native reconstruction/validation and its readiness checks are deferred to the
actual return. Strict document convergence alone is not an independent visual
or collision test.

The existing `coop_editor_terrain_parallel.py --mode host-away` is structured
for this distinction: hold the same host creation serial while the guest
authors a checked stroke, require all-peer canonical/durable revision+hash
agreement and one correctly authored Terraform history item, and require
strict native convergence only on participants still in Adventure. Exercise
guest Undo/Redo with exact recipe order/payloads and unrelated planet fields
unchanged while that host serial remains designing. After ordinary creator
Cancel, require all scenes ready, exact native convergence and zero faults;
also inspect the returned terrain/grounded actors and preserved local
selection/camera. A host `coop.editor check` collected while in the creator is
explicitly inapplicable, not an acceptance result. A current successful native
host-away stroke, history and return run remains required; this static audit
does not provide that acceptance or a performance pass.

### Durable-state encoding without an import copy

The wire state validator previously constructed a temporary `Coordinator` and
called `reset(State, epoch)`. The by-value import copied the entire document,
retained gesture patches and remembered decisions solely to check them, then
destroyed the copy. This occurred in both state encoding and decoding. Session
coverage also encodes a document-only State after each durable acceptance; its
validation incurred another document copy on the game thread.

`valid(const State&, const Limits&, error)` now shares the exact durable import
checks with `Coordinator::reset` and reads retained values in place. Reset still
owns its input, rejects a missing/freshness-conflicting epoch or prepared
transaction, clears old operation-retirement fences when changing epoch, then
validates the normalized owned state before publication. History operation
epochs are retained as before. Wire validation explicitly finishes validation
before reading its error text, avoiding function-argument evaluation order when
selecting the rejection reason. The serializer, format versions, bounds and
durability ordering are unchanged.

This removes a validation copy, not the remaining transactional state copies.
Replica replay still stages and recomputes the canonical decision and inverse;
the persistence worker still owns its complete input. Resource root replacement
and envelope construction currently copy and hash retained dependencies more
than once. Reusing their digests would require private immutable, byte-verified
record provenance; public mutable bundle keys/counts are insufficient proof.

The journal already uses format 3 checked deltas for native bytes, semantic
state, bindings, manifest and metadata. It selects unchanged, prefix/suffix or
bounded binary patches and checks the previous state digest during recovery.
The sparse-journal regression covers thirty changes to a 2 MiB native image and
1 MiB semantic state with less than 16 KiB of journal output. A missing journal
delta implementation was therefore disproven. Normal journal encoding, full
semantic-state hashing and flushing occur on the owned commit worker; they must
not be attributed to rendering-thread work merely because they affect durable
confirmation latency.

The const-validation regression retains a 64 KiB structured history value while
refusing allocations of at least 4 KiB, and checks malformed history, decisions,
clone provenance and retirement fences without modifying input. Exact wire
fixtures captured before the change cover legacy encoding, extension versions
1 through 5 and 128 retained gestures/decisions. These are deterministic source
and codec checks; actual commit/frame latency still needs a fresh native run.

The strict O2 receipt is
`out/preview9-const-state-proposal/receipt.json`: 40 actual compiler inputs,
zero source drift, and all seven before/after wire fixtures byte-identical on
Linux and actual i686 Windows. Document/protocol/project suites passed
3,054/9,218/5,683 checks on each platform; the real-UDP/fake-native Session
suites passed 29,312 checks on Linux and 22,514 on Windows.

### Creation return retains exact placement grounding and logical identity

C27's actual host-away run accepted guest terrain/history through revision 12
while the host remained in Creature. The first return failure preceded another
canonical restore: object `4294967302` had raw XYZ
`(-459.84436,116.277344,187.374207)` against entry XYZ
`(-459.844482,116.277374,187.374252)`, with identical rotation, scale and
properties. The entry snapshot remained revision 8; validating it before
materializing the latest revision was intentional. Evidence is retained in
`out/preview9-c27-terrain-host-away/result.json` and
`out/preview9-c27-terrain-failure-full/result.json`.

The native mode-generation transition cleared `terrain_grounding`, live
bindings and `act_identity`. The existing creation ticket retained the exact
terrain materialization map but omitted the placement grounding map. Its
initial return capture consequently exposed rendered XYZ instead of the
previously witnessed authored XYZ. Retrying after Runtime froze then lost the
read scope and recipe materialization witness, producing later recipe errors;
those later errors do not replace the first placement failure as causal
evidence. Cleared act/terrain identity was also an unpinned input: projection
uses it to retain logical IDs and exact prior terrain payload/counters.

The one-use ticket now owns the full original live placement roster: raw slot,
logical object/revision, native key/class, and exact rendered/authored XYZ. A
differing pair is retained only from an already existing exact grounding
witness. Capture checks that the live roster compacts to the entry snapshot's
dense bindings. Return checks the same owner, source, tool, generation,
operation scope/serial and exact entry image/document, then verifies every
returned raw row and XYZ before reattaching the saved grounding, raw bindings
and minimal entry act/terrain identity. No live marker or terrain value is
written; unknown returned changes remain subject to the ordinary strict entry
capture/validation. No approximate XYZ or inferred latest-document mapping is
introduced.

All callback inputs and operation predicates are copied before Native getters.
The entry size is checked before copying, the retained proof remains bounded
to 64 MiB, and publication follows a final callback-free owner/roster check.
An expired or failed attempt consumes its ticket without publishing maps.
The controlled regression uses sparse slots 1/3, dense serialized slots 0/1,
nondefault object/act/terrain IDs and a real mode excursion. It validates the
returned draft against the entry without a manual identity rebind, and rejects
row reuse, unknown XYZ, stale identities and reentrant caller retirement.
Actual host-away return acceptance for this change remains a separate native
candidate gate.

The actual-i686 strict O2 receipt is
`out/preview9-creation-grounding-native-receipt.json`: 46 compiler/Make inputs,
zero drift, grounding 6,564, materialization 24,534 and full Native 68,285
checks passed. The original C27 adapter from `b34450c` fails the same new
public-API regression at the first restored raw-binding assertion; its
expected-failure receipt is
`out/preview9-creation-grounding-red/receipt.json`. The passing current case
continues through strict draft capture and entry validation. These receipts
prove controlled adapter behavior, not live host-away acceptance or timing.

### Ordinary pose capture uses separate outside-frame proof phases

The optional ordinary-pose route retains one operation, with its original
history entry and independently referenced BEFORE and AFTER resources. It
advances only from Runtime's outside-frame drain tail, after canonical work,
and requires a strictly greater actual frame for each phase: BEFORE capture,
AFTER byte capture, full AFTER Read/typed projection, canonical BEFORE proof,
opaque forward proof, and value-only delivery. A foreign durable commit can
pause progress; it cannot change the retained request's original base or CAS
guards. The authorizing gesture scope stays empty while held. A separate read
identity pins project/epoch/peer, generation, actual mode/data/resource/source,
native context/act and operation serial.

The full AFTER typed projection remains mandatory. Its immutable value proof
and the validated BEFORE value proof can be borrowed on a later synchronous
stack, with fresh resolver witnesses and exact image/schema/identity checks.
No parsed resource, ReadImage or ValidationScope survives a drain, and no native
pointer is transferred to Runtime. Before the opaque phase temporarily
overlays known pose primitives on the original private BEFORE, a fresh Write
must reproduce its captured bytes. The existing RAII forward comparison then
requires one Write to reproduce the entire AFTER image, and restores every
temporary primitive on all exits. A genuine sealed result permits reuse of
that completed support proof; ambiguous supported changes retain the full
ordinary guard. Runtime still performs an independent fresh live after-image
proof before request/adoption. The seal never certifies current live bytes.

Every callback boundary closes the original owner/source/read identity, and
entry-child replacement retires the operation rather than repinning it.
Owning native references are released before failure or later delivery; only
bounded owned values survive. Grounded post-terrain gestures continue through
the generic path because a raw BEFORE owner is not automatically a canonical
grounding proof. Six cumulative `native_capture_*_{calls,ticks}` spans and last
`begin/end/frame` values expose the actual phases; these inclusive timings do
not establish a frame or confirmation budget pass. The controlled regression
also covers reentry, NativeWrite failure/RAII restoration, Wait retries,
resolver ABA, unknown bytes, cache eviction, and zero-Read/Write sealed reuse.

The strict actual-i686 O2 receipt is
`out/preview9-staged-capture-native-receipt.json`: staged 1,662, pose 2,720,
and full Native 70,295 checks pass with zero drift across 47 compiler/Make
inputs. Independent source review found no additional lifetime or seal bypass.
This is controlled adapter evidence; live staged scheduling and the required
two/four-participant frame and confirmation budgets remain separate gates.

### Save readback retains exact placement grounding provenance

C28's first genuine Town Save completed its native confirmation and persisted
record read, then sharing stopped in `normalize_saved_terrain`. The first
placement mismatch was object `4294967302`: persisted XYZ
`(-459.84436, 116.277344, 187.374207)` versus canonical authored XYZ
`(-459.844482, 116.277374, 187.374252)`. Rotation, scale and typed placement
properties matched. Both ordinary live checks subsequently converged. Evidence
is retained in `out/preview9-c28-save-failure-full/result.json` and the genuine
saved carrier rooted at `408A0000!2912D504.366A930D`.

The transition token already retained exact rendered-to-authored terrain recipe
values, but did not retain the corresponding placement grounding rows. Runtime
substitutes the independently persisted image before calling the transition
normalizer. Its full typed validation therefore rejected the raw rendered XYZ
before reaching the separate later grounding step. An owning serialized fixture
with sparse live slots 1/3 and dense persisted slots 0/1 reproduces that precise
failure on the pre-fix implementation; its immutable executable, failed log and
source fingerprints are in `out/preview9-save-grounding-red/receipt.json`.

Settled transition capture/refresh now retains the complete bounded placement
roster: full logical binding, raw slot, native key/class, row revision, and exact
authored/rendered XYZ. A differing XYZ requires the preexisting exact grounding
map at capture; no map is inferred from a later document. Prepared recovery
tokens acquire this proof only through their settled refresh. The combined
token remains limited to 64 MiB. Native Save checks the original entry document,
schema and sidecars, rotated source receipt, owner, generation, read context,
operation serial and complete unchanged raw live roster before using it.

Only an independently owned persisted resource is normalized. Its initial
Read/Write must reproduce every input byte. Each persisted dense binding must
resolve to the retained native key/class, and its XYZ must be either the exact
retained rendered value or the exact authored value. Only the former is changed
to the latter. A guard restores the private XYZ on every exceptional exit; a
second serialization after restoration must reproduce the complete original
image, including unknown fields. The private owner is released before the final
owner/source/context and raw-roster checks, and the existing full typed
validation still precedes publication. Reentrant use of the same token is
refused. No live marker, recipe, actor or UI field is written; no numeric
tolerance or latest-document inference is introduced. Public signatures and the
standalone legacy grounding helper are unchanged.

Controlled negatives cover unknown XYZ, binding/class/native-key reuse,
revision/schema/source/context expiry, Read normalization, opaque Write mutation,
Native callback exceptions, Release expiry, nested proof reuse and retirement of
caller-owned holders. Live Save acceptance and performance remain separate
checks against the next privately deployed candidate.

The fresh actual-i686 strict O2 receipt is
`out/preview9-save-grounding-native-receipt.json`: Save grounding 17,403,
materialization 41,937, staged capture 1,662 and full Native 87,701 checks pass
with zero drift across all 47 compiler/Make inputs. The earlier pre-fix RED is
kept separately; an independent read-only review found no additional ownership
or byte-closure gap in the private persisted-image normalization.

### Water completion and native Environment refresh boundaries

The ordinary Water slider `072FF8E8` has a completion boundary separate from
`TerraformRebuild`. Environment handler `F3AAA0` handles its live
`EF00A884` change through `F320E0` at `F3AD66`. For a changed-value completion
`07A44749`, it calls `F335B0` at `F3ACDA`, then commits at `F3ACE1`.
`F335B0` is a no-stack-argument thiscall on TerraformMode. It completes the
terrain object at `+190`, refreshes ScenarioData through `F3E250(data,false)`,
updates water contacts through `B87F10` and validation through `F40040`, then
tail-calls `F42660`; the tail returns with plain `ret` at `F42858`.
The Environment caller ignores EAX. A raw executable relative-reference scan
finds only this direct caller; it does not exclude indirect calls.

C28i's first ordinary Water drag reached this path before the existing
`F37A00` q/XYZ observer. Capture then rejected an unwitnessed placement change
before delivering a gesture for Runtime's inverse. The retained failure is
`out/preview9-c28i-water-failure-full/result.json`; this is evidence for the
missing completion observation, not a successful Water acceptance test.
The new wrapper observes exact raw q/XYZ immediately after the original
completion and before callbacks, and requires the same values again before
publishing owned provenance. No numeric tolerance or Placement lease bypass
is justified by this trace. Reviewed `F40040/F3FF30/F3FE10/F3B880(false)`
direct stores affect validation companions, not marker q; global side effects
of `FB7BC0/B87F10` are not a general setter-independence proof.

EnvironmentUI constructor `F3A310` installs primary vtable `148D1AC` at
`F3A321`, IWinProc vtable `148D190` at `+8`, and root IWindow at `+C`.
TerraformUI allocates it at `EE8995` and retains it at `+14` (`EE89BB`);
Scenario display owns TerraformUI at `+28`. Native category-3 refresh
`EE9175` and show-true `F3B13B` call `F3AF60` with this exact owner.
It takes no stack arguments and tail-calls `F3A8D0`, which returns at
`F3AA9C`. It reads Water, Atmosphere and Temperature from the current tool and
refreshes controls `072FF8E8`, `071842D8` and `07183C38`, respectively.
Its reviewed tail updates checkbox, color, dirty and caption UI paths.

All three slider updates pass `SetValue(value,false)`. WinSlider constructor
`985A35` installs ISlider at `+20C`, vtable `1444CA8`; Cast `983A70` returns
that interface for `0F00A8A0`. Slot `+1C` is `983B10`, which reads the notify
argument at entry ESP+8 (body ESP+28), emits `EF00A884` only when nonzero, and
returns `ret8` at `983B80`. Deferred Flora density replay must supply both
arguments and use false because it explicitly replays the native input event
after updating the owned slider. Its controlled tests cover one event, density
endpoints and stale owner/layout/window rejection; live Flora acceptance is
separate.

Exact executable bytes, disassembly and the ABI verifier report are retained in
`out/preview9-water-boundary/`. The symbol map and
`tools/addrport/verify_editor_dc04.py` pin these boundaries to supported
executable SHA `dc04aee5...dd5e59a37`. This proof permits an owner-fenced,
later-frame Environment refresh outside rendering; it does not authorize a
refresh inside Present or bypass Native input/history gates.
