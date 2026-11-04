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

### Save refresh retains the exact observed entry

C29's Native Save completed locally at revision 59, then the shared terrain
readback stopped at the exact committed-entry guard. Both live checks still
converged. The retained diagnostic is
`out/preview9-c29-save-failure-full/result.json`; this is a handoff failure,
not evidence that Native Save changed a placement. The canonical transport
root was `408A0000!C6A75465.366A930D`, while the independently pinned host
scene used `408A0000!BF0CE0D0.366A930D`. The refreshed ticket copied its
supplied canonical entry, while Runtime retained the returned strictly
observed live entry. Their source sidecars therefore differed before Save's
new root `408A0000!2912E312.366A930D` was compared.

The adapter now gives the refreshed ticket and Runtime the same strict
observed Snapshot. Initial capture has no observed output and preserves its
caller's supplied image after proving the readback relationship; only refresh
replaces that image with its paired output. It retains the original full
placement roster before
callbacks, checks the original owner/source/generation/read identity and
that roster again, and keeps the 64 MiB owned-value bound. The subsequent
Save guard compares exact bytes, source, bindings, act, Captain, metadata and
the full revision-bearing Document. A transport alias or a later Save key
cannot substitute for that entry. Failure details contain a bounded field
mask, revision pair and root instance pair, without authored text.

Observation itself is not permission to adopt an opaque edit. Before the
handoff, independent private Read owners must reproduce both the supplied
entry and observed bytes exactly. Only the known external resource key and
local camera primitives are overlaid on the private original. Its Write must
reproduce the observed image, using the existing verified Planet comparison
normalization; unknown bytes remain exact. Cleanup restores those primitives
and reproduces both original serializations, then releases both native owners
before the final context/roster closure. No live pose, recipe or UI field is
written. Callback retirement, source/domain/revision changes and untracked
placement bytes cannot publish a replacement ticket.

A genuinely unsaved live owner may have an entirely empty source while its
canonical transport root is already nonzero. Only that existing first-SaveAs
case permits different source type/group domains; the observed empty key must
still equal the independently pinned live owner. Nonempty unrelated domains
remain rejected. Direct capture-to-Save and empty-source fixtures cover these
two API contracts separately from Runtime's usual capture/refresh pair.

The additional unknown-byte handoff RED is retained under
`out/preview9-save-observation-red/receipt.json`. It uses an OUT-only source
copy excluding only the observation closure and accepts an untracked
marker `+100` mutation before failing the controlled negative. The fixed
actual-i686 strict suite passes 35,934 Save, 1,269 Water, 1,137 Environment,
62,479 materialization, 1,662 staged and 110,764 full checks, with zero drift
across all 49 compiler/Make inputs. Its final receipt is
`out/preview9-c30-native-receipt.json`; the preceding strict receipt is
explicitly superseded by this observation-closure run.

The original source-alias failure is retained with its executable and hashes
in `out/preview9-save-alias-red/receipt.json`. Controlled coverage uses document
revision 59, placement revision 51, sparse live slots and dense persisted
bindings, plus exact namespace/source/byte and callback-expiry negatives.
These fixtures are adapter evidence only; Root's next real Native Save remains
the acceptance test.

The Water observer now brackets the separately verified completion boundary,
samples q/XYZ before any policy callback, and refuses any later unknown row or
companion delta. Deferred Environment refresh runs on a later outside-frame
callback after terrain/view settlement, for the same current category-3 owner.
It retires on new local interaction, scope/source/generation expiry, queued
serial replacement or UI-owner change. Flora density replay supplies the
verified `SetValue(value,false)` ABI and then sends its one existing native
input event. Real Water forward/history behavior and visible refreshed slider
values still require the next private-game checkpoint.

### Persisted marker keys are not live actor lookup keys

C30's normal Save completed locally at revision 78, but sharing stopped at
`The persisted native Save placement identity changed`. The preceding exact
source/snapshot handoff guard had passed. The carrier invocation is retained in
`out/preview9-c30-native-save-carrier/result.json`, with the later full failure
capture under `out/preview9-c30-save-failure-full/`. This failure did not justify
ignoring marker identity or mutating the live scene.

Native marker `+228` belongs to the live actor lookup domain. Constructor
`F2A960` initializes it to `FFFFFFFF` at `F2AAAB`. The full reflected marker
descriptor initializer `138C040..138C5B6` registers fourteen authored offsets,
ending at `+28`, and never registers `+228`. Marker Read `F2D680` leaves that
constructor value intact. Pool insertion `F2EBC0` copies through `DFDFB0`, whose
`DFE108..DFE10E` copy preserves the value. The actual writer loop
`F28C10..F28C2C` uses that same descriptor with `marker=row+4` through
`Write6928C0`; its size pass uses it as well. Thus a fresh persisted NativeRead
has the constructor sentinel rather than the original live lookup key.
Exact disassembly is retained in
`out/preview9-c31-eligibility/marker-key-disassembly.txt` and
`marker-key-writer-disassembly.txt` in that directory.

The Save adapter keeps every original live slot/object/class/key/revision and
rendered/authored-position fence. Only its private persisted-image check now
requires the verified `FFFFFFFF` domain. It records that exact private key and
requires it unchanged across all temporary position writes and native
callbacks. Class, dense logical bindings, full serialized roundtrips and
unknown bytes remain strict; no persisted key is guessed from a live slot or
accepted as arbitrary. The owning controlled serializer now omits the live key
on Write and constructs the sentinel on Read. The old C30 executable then
reproduces the exact identity failure, retained in
`out/preview9-save-key-red/receipt.json`.

The fixed actual-i686 strict run passes 37,317 Save, 63,862 materialization,
1,269 Water, 1,137 Environment, 1,662 staged and 112,147 full checks, including
private-key mutation at Read and each Write phase. All 49 compiler/Make inputs
remain unchanged in `out/preview9-save-key-native-receipt.json`. The real Save
acceptance test remains separate from this controlled fixture evidence.
The subsequent C31 private two-player run passed normal Native Save after both
roles' Temperature and Water edit/history sequences at revision 16, with
SavedApplied 2/2 and the genuine carrier retained. Its independent result is
`out/preview9-c31-native-save-carrier/result.json`.

### C32 private pose preparation reuses an exact typed base proof

C30's 60 ordinary movements completed durably and converged, but observer
frame time still exceeded the requested budget. Its author-side seal remained
effective: Runtime's measured supported scope was about 1.5 ms at p95, while
Native's aggregate supported counter included both the earlier forward proof
and the later seal check. Those differently scoped counters did not establish
a seal-reuse regression. The separate observer preparation route still read
its prepared image twice and projected the complete Act/Crew/Class subtrees.

The narrow C32 route retains independent bounded input values before any
callback. It requires the genuine private projection proof for the exact base
bytes, source, bindings and sidecars, with fresh context and asset witnesses.
It then owns one new NativeRead of the prepared AFTER image. Writing that
unmodified owner must reproduce every AFTER byte before any reversal. Its
complete raw marker roster supplies position, quaternion, scale and the seven
existing supported companions to an independently derived typed projection.
Only the previously recorded primitive writes and existing derived flag mask
`0x16` are reversed; a second Write must reproduce every validated BASE byte.
Unknown fields and unrelated subtree bytes are never reconstructed or omitted.

Eligibility requires stable presence and type for all seven known properties;
an extra property, removal or partial destination triplet retains the full
existing route. The version field must be 17, marker deprecated-distance
vectors must be empty (`+20C==+210`), and every ClassAct must have neither a
meaningful external legacy LocalizedString (`+470` and `+46C` both nonzero)
nor inline legacy text (`+45C!=+460`). This follows `F2CF60..F2CFA5`; a static
empty-text pointer is permitted. Any noncanonical Read/Write result falls back
to the existing full typed and opaque checks. Context or resolver expiry is a
rejection rather than authority to repin a replacement.

Temporary reversal holds values and slot/class/key identities, not marker
pointers across callbacks. Cleanup re-resolves only the exact retained pool.
Both private AFTER and producer owners finish Release before a reusable proof
or caller output can be published. A failed copied-input allocation rejects
rather than borrowing inputs a callback could retire. Nested preparation uses
separate owners and the full fallback while the private proof is busy. The
existing value-scope accessor now copies its callback before invocation, so
self-reset cannot destroy captured storage during that invocation; fresh scope
equality remains mandatory afterward.

The genuine retained C30 executable succeeds on the owning controlled pose
case with 3 Reads, 1 projection and 4 Writes, then fails the new work assertion.
The new route succeeds with 2 Reads, 0 projections and 3 Writes. This fixture
advertises Act/Crew authoring and owns class bytes without a typed cast schema;
it does not manufacture a third class fingerprint saving. The minimal pose
codec's Write count instead changes from 2 to 3, so that codec alone would not
demonstrate reduced Write work. Exact old-source binaries and input hashes are
retained in `out/preview9-c32-pose-prepare-red-receipt.json`.

Controlled negatives cover all four owning subtree taints, unknown/presence/type
changes, legacy fallback, context and resolver expiry, final producer Release,
row-buffer replacement, reverse-Write exception and nested preparation, plus
callback self-reset and bounded allocation failure. The initial fixture AV was
its taint callback inspecting a marker-less fragment during full projection;
the fixed callback checks the roster and still requires its real AFTER taint
to fire. The retained diagnostic is `out/preview9-c32-fixture-debug.json`.
The C++ value-copy `bad_alloc` branch was reviewed to fail closed; allocation
injection covers the native allocator, not a global C++ allocator replacement.
Actual-i686 strict O2 passes 6,300 focused, 37,317 Save, 1,269 Water, 1,137
Environment, 63,862 materialization, 1,662 staged and 118,668 full checks.
All 51 compiler/Make inputs have zero drift in
`out/preview9-pose-prepare-native-receipt.json`. Controlled work counts are not
running-game frame times, and no final latency acceptance is claimed.

### C33 durable-worker completion is independent of frame polling

The complete C32 two-player run accepted all 60 movements, but missed the frame
and LAN budgets. Separating roles matters: own-author window maxima were
96.126/97.764 ms at p95; observer windows were 140.677/188.690 ms. Actual marker
application was about 1 ms, while observer pre-apply work and private resource
serialization dominated. Even a paired-operation counterfactual removing both
participants' entire apply intervals leaves guest confirmation at 268.678 ms
p95. Native serialization changes alone cannot establish the 200 ms budget.
The exact analysis is retained in `out/preview9-c32-full-performance-analysis.json`
and `out/preview9-c32-critical-path-counterfactual.json`; inclusive Native work
spans and staged phases are not summed.

C33 preserves apply-before-durable semantics and both worker barriers:
owned encoding, game-thread context/publication-capacity validation, then the
irreversible Store append. Each worker receives an immutable wake ticket and
copied enqueue function. It records QPC values only in its owned result and
posts through the existing outside-frame queue. It does not query Controller,
Native scene or Runtime state. The SDK queue is mutex-protected; its retained
callback keeps the calling DLL mapped through callback and payload cleanup.

The game-thread callback owns even a stale payload. Exact runtime-domain,
job/phase, operation/revision, Controller/Session/transport, canonical context
and passive owner/read-scope values must still match. A zero-time future probe
denies an early callback without waiting or reposting. Normal update polling
remains the fallback for early dispatch or allocation/enqueue failure. The
job destructor joins before any worker input/result member ends its lifetime.
Shutdown retires its domain before joining workers and clearing adapters.

The completion-only Controller entry does not drain inbound, local, control or
Saved queues. Successful durability may publish its existing ordered Decision
and coverage; failures remain held for the ordinary outside-frame drain before
rollback or error callbacks. The wake does not refresh UI or query Native state.
Its cached owner context is continuation fencing, not fresh scene authority;
the original Native application and input hold remain the authorization path.

Worker encode/flush start, end and first-observed QPC values now separate actual
owned work from scheduler delay. Only worker start/end join the explicit sampled
timestamp allowlist, and a recorder restart drops an older phase's samples.
NativePrepare begin/end surrounds only the actual private prepare call. The
retained C32 public-behavior RED finishes encoding but has no outside task and
cannot reach Flush without another tick/frame; its exact 192-input baseline is
in `out/preview9-c33-completion-c32-red-receipt.json`.

The final actual-i686 strict O2 run passes 2,225 completion-focused and 114,743
full Runtime checks, 480 focused and 23,766 full Session checks, and 132 numeric
stage checks. All 80 compiler/Make inputs remain unchanged in
`out/preview9-c33-completion-receipt.json`. This proves the controlled lifecycle,
queue and durable-byte behavior; running-game frame/LAN budgets remain unproved.

### Inactive portable v17 pose/container producer

`editor_native_refpack.{h,cpp}` is a scalar, value-only transcription of the
supported encoder at 92C780. The original 974-byte implementation has only
five external calls, to verified memcpy/memset thunks. An OUT-only actual-i686
fixture executed that extracted code with caller-owned bounded buffers and
guard pages. No extracted machine code or Native ABI call ships in the module.
The exact match/gain tie order, literal grouping, hash reset and selected
3FFF/1FFFF link window are retained. Input is bounded to 16,000,000 bytes.

`editor_native_pose_format.{h,cpp}` has no Native caller. It accepts complete
logical ID/generation, dense serialized slot, class and all ten exact raw
pose/companion payloads. Only existing position 071147E6 (12 bytes) and optional
quaternion 071147E7 (16 bytes) change. Scale, seven companions, flags, unknown
fields, directory order and every prefix byte are preserved. Core finite/range
and quaternion eligibility checks never normalize or approximate the raw words.

The profile requires both Native version words to be 17. The first precedes
PropertyList; Write F25FFE..F26006 emits another after PropertyList, then
F26012..F26020 emits the embedded DBPF length. The memory DBPF is a canonical
96-byte v3 header, one 01897C18 core record, flags7 index immediately after
payload, no holes or tail. The bounded five-byte RefPack header and declared
decoded size are checked before invoking the generic decoder. BASE recompression
must reproduce every original compressed byte. Complete reverse comparison and
an independent decode of generated AFTER precede atomic output publication.

Image bytes are limited to 16 MiB, decoded bytes to 16,000,000, markers to 4096,
edits to 64 and directory entries to 131072. Checked offsets/counts and staged
buffers bound helper-owned working data below 64 MiB. Larger capabilities and
unrecognized profiles return Unsupported; malformed input, stale raw words or
identity and allocation/codec failures return Error. Neither changes output.
Nonempty deprecated marker distances require fallback. The compressed ratio
must be strictly below one half: this avoids claiming equivalence near Native's
x87 0x3F733333 keep threshold. The genuine Town ratio is approximately 3%.

`tests/fixtures/editor_native_pose/` retains 15 exact, deduplicated closed C33
snapshots and the independently decoded typed targets for all 28 consecutive
author/observer pairs. Whole AFTER, inverse, Position-only contract negatives
and four actual compressed-size changes are checked on Linux and actual i686.
The raw roster also retains unadvertised native destination defaults without
claiming semantic presence. The encoder's deterministic goldens originate in
the earlier independently executed Native differential corpus; its test runs
read-only guarded inputs, including 16,000,000-byte boundaries.

This is format reproduction, not Native admission, semantic projection trust,
fixed-point token minting or successor induction. A future consumer still needs
an independently genuine BASE NativeRead/Write fixed point, complete semantic
schema/presence, supported code/descriptor profile, sidecars and fresh context
and resolver closure. ClassAct legacy localized-text and marker read postpasses
remain separate conservative eligibility gates. Live BEFORE/post-apply opaque
checks remain unchanged. No Native/Runtime/wire/map/hook/cache integration is
part of this checkpoint.

OUT evidence is `out/preview9-c34-pose-format/receipt.json` and the independent
`out/preview9-c34-pose-format-review/receipt.json`. The same-process scalar
encoder p95 was 12.252 ms versus extracted raw 11.330 ms; the bounded chunk
experiment also showed no useful p95 win. These results support no compression
hook or frame/LAN performance claim.

### Saved-image dependency receipt values and immutable files

`editor_saved_dependencies.{h,cpp}` binds a bounded receipt to the genuine raw
Saved image before terrain/grounding normalization: exact key, size, SHA-256,
supported executable/profile domain, Captain, every Crew slot, and all three
asset references of every class, including unused classes. Raw local key,
machine, server and model plus independently observed effective identity remain
separate. Stock database indices use the fixed 13-name table from the existing
Collector. Absence from the custom set never establishes stock provenance.

Selected custom bytes must equal previously admitted immutable bytes, and every
retained record must belong to an explicit selected reference. Only the exact
selected root and the existing Collector's known root companions are dropped.
Foreign transport roots and unreachable historical records reject. All six
existing Collector families use its fixed, at-most-eight companion relation;
unknown families and arbitrary private/transitive edges remain unsupported.
This does not add editor routes or claim recursive BEM dependency completeness.
Each closure needs the effective primary record or its BEM body; metadata and
other known companions remain optional, matching Collector behavior. New alias
canonicalization has its own separate pollen requirement, not a generic receipt
restriction on already admitted native source closures.

The deterministic codec checks complete counts, full identities, ordering,
per-record hashes and whole-frame checksum before atomic output publication.
Limits are 1024 classes, 32 Crew entries, 3105 references, 2048 records, 16 MiB
per image/record, 128 MiB payload and 132 MiB receipt. Collision inspection
reports only exact byte/key observations and missing records. It has no boolean
that can grant current-module package ownership, reuse or a budget reservation;
the actual Native ProcessBudget and fresh resource observations remain final.

`editor_saved_dependencies_store.{h,cpp}` addresses exactly
`<Saved group>-<instance>-<type>-<raw image SHA>.sdr` in an explicit isolated
cache directory. It never scans old receipts or edits a Native save database.
Publication fully encodes, exclusively creates and flushes a temporary, then
installs without replacement. Windows uses MoveFileEx WRITE_THROUGH; POSIX uses
same-directory link plus directory/ancestor fsync. Equal concurrent publication
may reuse only exact full bytes with a fresh durability barrier; unequal content
cannot overwrite an existing receipt. Cleanup owns only successfully created
temporaries. Visibility followed by a failed durability acknowledgement is
Uncertain, including reuse. A failed acknowledgement cannot announce readiness.
Prior receipts remain unchanged. Checksums provide integrity, not authentication
against an actor able to rewrite the isolated cache and recompute its hashes.

The genuine C34 fixture retains the raw 60,745-byte Saved image, the actual
four-record Vehicle package and the independently tested prototype receipt.
Tests decode and compare all 119 classes/358 raw reference slots and all four
dependency records. Stock tags/profile values are explicitly controlled issuer
observations; the codec does not mint Native selection, stock, scene or resolver
authority. OS fault injection is private to the store test translation unit.
Focused Linux/actual-i686 receipts are retained under
`out/editor-preview9-saved-dependencies/`; running-game cold-load integration
is a separate Native issuer/lifetime checkpoint.


### Shared-editor service pulse (C36 controlled scheduling)

C35 completed all 60 movement commits at revision 62 with matching strict state and no faults, but guest submit-to-confirm p95 was 330.0971 ms (host 67.8128 ms). Guest request-to-host and host-decision-to-guest handoffs each occupied roughly one 46?47 ms frame; the guest encode-completion observation tail reached 41.5522 ms p95 despite 2.4175 ms p95 encode work. These are application scheduling intervals, not measured socket RTT. Host observer Native apply also reached 123.8219 ms in the guest p95 operation, so this scheduling change alone carries no frame or LAN budget claim. Evidence: `out/preview9-c35-commit-baseline-two-town/summary.json` and its retained result.

The pulse services the existing Session update and editor scheduling path outside Present at a nominal 5 ms cadence while the main thread publishes an active Ready shared-editor identity. The worker receives only an owned lifecycle, passive snapshot, and copied SDK dispatcher. A single serial reservation remains held until the task returns; retirement precedes editor shutdown. Fresh main-thread transport/runtime/context/source checks reject stale tasks. A nonreentrant main service guard, Runtime drain guard, and passive Native call-stack guard defer nested editor work, including final Native owner Release. No frame index is advanced by a pulse. Local creation and restoration holds can retain detached canonical progress; Native busy work simply waits for another pulse. The original 500 ms no-frame fallback remains, with its 100 ms retry cadence. Unlike the new fast pulse, that legacy fallback may continue transport receive/send during long Native/modal work; it does not schedule editor Native work until the passive idle gate permits it. The zero-Session/Controller nested-call guarantee applies to the new fast Ready-editor pulse. Validation and durable-before-confirmation rules are unchanged.

Controlled proof uses `tests/editor_service_pulse_tests.cpp`, `tests/editor_runtime_service_pulse_fixture.h`, and `tests/editor_native_service_pulse_fixture.h`. Final source/executable hashes and results are retained in `out/preview9-editor-pulse-receipt.json`. No game calls or performance acceptance are part of these tests. The retained prior staged-test timeout expected forbidden nested Runtime durability; its replacement asserts the foreign request stays queued through the outer supported callback, then both distinct operations converge once each with unrelated content/history preserved.

### Snapshot value construction after C36

The six-movement C36 probe retained strict agreement through revision 8 with no
faults. Observer windows still reached roughly 139 ms; their actual marker apply
was about 1.1-1.3 ms, while Native preparation and its full serialized witnesses
remained substantially larger. This is a three-sample-per-author diagnostic, not
a complete performance acceptance. Evidence:
`out/preview9-c36-commit-probe-two-town/result.json`.

Two pose-preparation constructions copied values that their following statements
immediately replaced. `Snapshot prepared=base; prepared.document=document`
copied both the old document and old serialized bytes before the existing
NativeWrite supplied new bytes. The derived projection then copied the genuine
producer Snapshot, including its old byte image, before replacing that image and
its sidecars. `editor_native_snapshot_values.h` constructs exactly the final
field domains instead. Its structured bindings enumerate every Snapshot member;
adding a future member fails compilation until the constructors account for it.
The derived projection still copies the genuine producer's document and local
rollback dirty state, rather than adopting the caller's requested document.

Every NativeRead, NativeWrite, resolver/context check, exact opaque comparison,
private-owner lifetime and callback fence remains in place. The new
`native_snapshot_copy_calls` and `native_snapshot_copy_ticks` status fields use
`native_work_frequency` and time only these CPU value constructions. They grant
no semantic or Native provenance. The focused owning pose fixture requires the
same two Reads, three Writes and zero projections, plus two value-copy spans.
The standalone allocation-count fixture retains the previous constructors and
proves that the discarded base document/image copies disappear while all nine
Snapshot fields and independent storage remain equal. Focused receipts belong
under `out/editor-preview9-snapshot-values/`; no running-game speedup is claimed.

### Passive integrity and pose-proof call-site clocks (C39)

The six C37 observer samples already reused complete canonical projections:
zero projection/resolver calls and zero parsed-image cache hits/misses. Their
remaining two independent Reads took 15.50-16.04 ms total; four or five Writes
took 45.44-58.47 ms total. These are inclusive counter deltas, not disjoint
durations to add. The existing typed value proof cannot stand in for a current
live-byte witness or the mutable BASE and genuine AFTER Native owners.

`WorkStats.steps` now passively labels the existing call sites:
`integrity_write`, `integrity_closure_write`, `prepare_base_read`,
`prepare_producer_write`, `prepare_after_read`, `prepare_fixedpoint_write`, and
`prepare_inverse_write`. Each exports `native_<step>_{calls,ticks,begin,end,frame}`.
Calls/ticks are cumulative; begin/end are the last completed QPC interval and
frame is the passive SDK frame counter at that invocation. The timestamps can
be correlated with the existing operation recorder; they are not authority or
a promise that all seven last intervals belong to one operation. Retried or
nested calls remain inclusive. Failed calls are timed too.

Only the existing parse/serialize expressions are wrapped. Their Native calls,
short-circuit order, byte comparisons, callbacks, normalization, ownership and
publication remain unchanged. SDK frame-counter exceptions are ignored by the
diagnostic. Timing excludes the following materialization normalization and
freshness callbacks, making those residual costs visible rather than silently
attributing them to serialization. There is no passive per-stage memo API;
this checkpoint does not execute a Kit diagnostic command or extend its ABI.

The performance parser accepts legacy receipts without this entire optional
group. Present groups must be complete with valid unsigned counters/clocks;
availability changes, resets, partial/duplicate fields and uncompleted interval
changes reject. The report keeps same-frame step intervals without imposing
the six-stage author-capture frame progression. Focused Native tests retain
two integrity Writes and the modern preparation's two Reads/three Writes/zero
projections, and check completed clocks and passive frame-counter failure.
Receipts belong under `out/editor-preview9-proof-steps/`. No new game timing or
budget acceptance is established by this instrumentation.

The performance driver now inventories known compiler/linker/build-tool process
names and PIDs before input and after every measured operation, outside frame
recording. Windows uses only Get-Process Id/ProcessName; each currently running
WSL distribution uses ps pid/comm. It never reads process arguments, paths or
environments. An unavailable, partial or malformed query fails closed. Active
tools or unavailable inventory stamp the exact author/sample window as
contaminated and reject its timing. Post-window checks preserve an existing
Native error rather than masking it. These pre/post snapshots cannot observe a
job wholly between queries and do not constitute continuous CPU-quiescence
monitoring. Both snapshots and that limitation remain explicit in the artifact.

## Diagnostic tree root membership (C39 crash)

The C39 host crash after `coop.ui tree 3 visible 05D17546` was a diagnostic
input error: `05D17546` was a control ID, not an IWindow address. The previous
command converted the token with unchecked `strtoul` and passed it directly to
`dump`, whose first visibility vcall dereferenced it. The retained AV was
`coop.i95.1.dll+2F2964`, ECX `05D17546`, reading `8CB605F9`.

Explicit tree roots now require exactly eight hex digits (an optional `0x`
prefix is accepted), a nonzero value and membership in the current main-window
tree. The candidate is only compared with identities reached from a fresh
`sui::main_window()` through the existing verified native child references:
IWindow+38 is the anchor, the first node word is its next link, and IWindow is
node-4. No meaning is assigned to the other link word. Readability, repeated
nodes, 4096 nodes per child list, 65536 total windows/edges and unchanged list
heads are checked. A second fresh main-window getter must return the same main,
then a second complete traversal must still find the root before dumping it.
Missing, stale, malformed, cyclic or incomplete trees fail without a dump.
Default-main tree commands retain their existing behavior.

`test-sui-tree-root` and `test-sui-tree-root-windows` exercise strict parsing,
unrelated control IDs never reaching the inspector, nested roots, exact bounds,
cycles, duplicate children, read/allocation failure and getter-time retirement.
These are offline value-graph checks; actual UI qualification is separate.

## Cold recovery's second canonical terrain boundary (C40/C42)

Cold RecoveryBootstrap restores the independently retained canonical Snapshot,
then explicitly loads and rebuilds its terrain again before attaching the new
controller. The first restoration's grounding witness does not authorize the
second rebuild's output. C40 first failed object4294967302 with authored XYZ
`-459.844482,116.277374,187.374252` versus rendered
`-459.84436,116.277344,187.374207`. C42's exact XYZ receipt passed that row and
exposed object4294967549 quaternion drift at the same second boundary:
canonical words `BF16ACF6 BE2E89B5 3E8838CB 3F3E8042`, rendered
`BF16ACF8 BE2E89B6 3E8838CB 3F3E8043`. Its XYZ, scale and properties matched.
The original R22 checkpoint and journals remained unchanged after both failed
recoveries and ordinary closure.

The second boundary now pins the existing TerrainRotationOwner from the full
canonical binding roster and requires every initial quaternion to match the
canonical artifact bit for bit. After Native Load, raw owner/roster checks run
before Rebuild. Immediately after Rebuild, before policy callbacks, it samples
finite XYZ and quaternion values under the same pool, class, native key and
logical binding fences. The existing guarded ApplyMarkerState rotation repair
accepts only that exact sampled quaternion and restores the canonical value;
a third value, changed XYZ or expired owner rejects. Its audio guard and spatial
component checks remain intact. No gesture rotation ticket survives this call.

The one-use transition token owns the exact Snapshot, full placement witnesses
and bounded pre-Load metadata. Settled resume normalizes only witnessed XYZ in
an owned readback copy, requires canonical live quaternion words, and performs
the existing full typed and opaque image comparison before publishing grounding
and materialization. Missing legacy Snapshot metadata retains the exact original
owner metadata. Unknown fields, changed properties, roster, keys, revisions,
source or context cannot become a new canonical witness. No live XYZ is written.

A failed recovery attempt now retains its first reason and held resources until
explicit Recover, End or teardown. Later frames do not reuse its retired ticket
or overwrite the useful mismatch with an invalid/expired error. Controlled
Native cases reproduce both actual XYZ and quaternion pairs and reject policy,
Native Apply and final Release mutations; Runtime cases prove no retry across
frames and explicit retry without replacing the prior checkpoint. Focused
receipts and the exact C42-source quaternion RED are retained under
`out/editor-preview9-cold-recovery/`.

Actual C44 recovery passed directly from those unchanged C39 R22 checkpoints:
both participants were Ready, revision/durable22 and unfrozen, with121 classes,
419 placements and5 acts. Every retained asset key, setting, behavior and content
revision matched the old R22 document; both strict Native checks agreed in the
new epoch and both fault counters were zero. The document hash changes with its
new namespace, so old/new hash equality is not the recovery criterion. Evidence:
`out/preview9-c44-recover-creations/result.json`.

## Default-off two-frame ordinary pose preparation experiment

`SPORECOOP_EDITOR_TWO_FRAME_POSE=1` enables a bounded development experiment;
the default remains the existing synchronous commit path. Only ordinary forward
Position/Rotation commits enroll. Terrain, creation, history, Scale, property
changes, destination-bearing targets, legacy Native profiles and unavailable
projection provenance retain the existing route. A nonempty grounding ledger
or a live terrain rotation ticket also opts out: this experiment does not infer
canonical XYZ or quaternion authority from a previously rendered scene.

The first game-frame phase owns immutable Snapshot/Document/Change values and
copies its policy callback before any Native callback. It pins the actual raw
scene owner, source, generation, value context and genuine cached projection
issuer. It performs the existing BASE Read, private producer Write, genuine
AFTER Read and exact fixed-point Write. All class/act/crew/unknown bytes remain
in the produced image. It releases the producer and retains exactly one unique
private AFTER owner, plus bounded value witnesses, between frames. It retains
no ParsedImage, ValidationScope, borrowed marker pointer or worker-owned Native
reference. The combined owned-value preflight is bounded to 64 MiB.

A later actual SDK frame rechecks owner, context, resolver identities, the full
private slot/class/native-key roster and all retained pose/companion words. It
then reverses only the allowed known primitives, requires one full Native Write
to equal the entire original BASE image, restores the private AFTER fields and
releases that last owner. Every Native Release runs under the same nonreentrant
service guard. Context and asset witnesses close again after the final Release
before publishing a value-only prepared Snapshot. A same-frame poll performs
no Read or Write. The successful proof still totals two Reads, three Writes
and zero full AFTER projections; this experiment changes their frame placement,
not their unsupported-byte coverage.

Session exposes an explicit Native-pending phase before persistence. Semantic
preparation does not report a successful live apply while Native proof is
pending. Runtime holds new local input while preserving the original read/value
scope and transport service. After the private proof is Ready, the real apply
still performs both existing fresh live integrity Writes. It requires the
original raw scene owner and exact live BASE; an exact-BASE mismatch discards
the optional seal and uses full existing preparation. The checked apply uses
copied authority and owner values around every Native callback, including audio
cleanup. A failed partially applied operation can restore only its original
fenced owner. Persistence and accepted confirmation begin after actual apply.

Read-only status exposes `pose_prepare_started`, `pose_prepare_ready`, errors,
cancellations, fallback count/reason, grounding-row count, rotation-ticket
presence, pending ticket/owner and its starting frame. Existing per-call proof
spans identify the BASE Read/fixed-point frame and the later inverse frame.
These fields are diagnostics, not authority. In particular a guest whose join
populates grounding may take the complete old path; actual route counters must
establish enrollment before interpreting a timing comparison.

The controlled i686 fixture covers Position and Rotation, sparse live versus
dense serialized slots, same-frame zero-work polling, exact call counts, all
Snapshot sidecars, between-frame opaque/class/act/Captain changes, private pool
replacement, resolver/context/owner expiry, callback-held input retirement,
wrong tickets, off-thread cancellation refusal, producer/final Release expiry
and cleanup reentry. Checked-apply cases include a callback that changes both
the actual source and the caller's borrowed owner value; the independent copy
rejects it. Focused source/EXE receipts are retained at
`out/editor-preview9-saved-cache/native-stage-o2-receipt.json`. Actual Native
frame and confirmation targets remain unqualified until a quiet A/B run.

## Armed outside-pump and service-pulse diagnostic (C47)

C47's default-off warmup capture showed actual SDK frames11582 through11586
about550 ms apart. Its Before/AfterBytes/AfterProjection/Canonical/Opaque bodies
took about12/24/36/5/37 ms. The six-phase capture correctly waits for a strictly
later actual frame, so frequent outside-frame ticks cannot advance it on their
own. The pulse scheduler, scope/idle checks and Native busy predicate matched
the retained C46 sources. No new two-frame preparation ticket was active.

An existing scheduling risk remains unproven: `pump_outside_tasks` consumes
newly arriving tasks until its256-call limit. The5 ms pulse releases its owned
reservation after scheduling Runtime drain, before that queued drain runs.
Repeated pulses/drains might keep the same pump nonempty and postpone Present.
The old status counters could not distinguish this from a missing eligible
editor observation and the normal500 ms no-frame fallback.

The diagnostic adds no scheduling or eligibility change. Kit command
`outside.metrics start|stop|status` records only an explicitly armed interval:
completed pump count, processed calls,256-limit hits, in-Present deferrals,
the longest completed pump and16 recent completed rows. Each row contains only
SDK frame ID, processed count, cap flag, queue length after the pump and QPC
begin/end/duration. A cap flag means all256 iterations ran; queue-left tells
whether work still remained. Nested pump durations are inclusive. Start clears
old startup/load samples; restart retires any older in-flight measurement.
Stop freezes completed rows and excludes an unfinished interval. No task
function, command text, process argument or Native address is recorded.

`coop.pulse_metrics start|stop|status` separately arms/resets editor/generic
reserve, dispatch, stale/busy skip, Session-update and editor-tick counters.
The counter locks protect diagnostic values only. They do not change a queued
reservation, frame age,5 ms/100 ms intervals, callback order or transport scope.
A reservation already present at Start may dispatch inside the measured window,
so reserve and dispatch totals need not match at its boundary.

For one capture, arm the pulse first and the SDK pump second, perform one
ordinary gesture and wait for settled Ready, then stop the SDK recorder before
the pulse recorder. `outside.metrics` is a Kit-owned any-thread diagnostic;
the co-op command is marshalled outside-frame. Output is bounded below the
8192-byte console reply cap. Both are passive and initially disarmed; SDK ABI,
wire format, Native calls, the existing256 loop and capture frame rules remain
unchanged. Controlled host/i686 tests cover ring eviction with retained peak,
restart/stop retirement, concurrent snapshots, bad clocks, editor/generic
classification and resets preserving an existing pulse reservation. Actual
cap starvation or fallback classification still requires the armed Native run.

C48's armed actual capture confirmed one outside pump containing184 callbacks
on frame3217, QPC17764370664086 through17764375864190 at10 MHz:520.0104 ms,
with no256-cap hit and an empty queue on return. The corresponding frame maximum
was558.62 ms. Idle pumps peaked at10 callbacks/10.59 ms with ordinary frames near
46 ms. Editor reserve/dispatch counts were746 versus6 generic during the5.71 s
capture interval. Thus a live stream can delay Present substantially before the
old count limit; absence of cap hits did not rule out this scheduling cause.
Evidence: `out/preview9-c48-pump-probe/diagnostic.json`.

The SDK pump now snapshots the last queued sequence and executes only through
that FIFO boundary, with the existing256-callback cap and an8 ms elapsed budget
checked after each callback and its retained-function cleanup. At least one
callback can run; a single long callback is never interrupted. Newly posted
pulse/drain work stays queued for a later pump. A nested pump may consume old
entries, so a sequence fence prevents the outer pump from accidentally replacing
those entries with newer work. Nested callbacks retain their existing ability
to pump independently; no new arbitrary-callback quiescence guarantee is made.

All queue operations own their `std::function` values under a mutex, and invoke
them with no queue mutex held. Queued module/payload retention is unchanged.
Remaining work receives another normal outside-pump window message, including
after a throwing callback. In-Present deferral and command marshalling remain
intact. Only WndProc calls `pump_outside_tasks` in production. The diagnostic
adds `budget_hits` and per-row `budget_reached` alongside its existing cap fields.
This bounds an individual pump; a single callback and Windows message dispatch
can still affect frame time, so actual frame-budget acceptance remains pending.

Controlled pure/i686 cases exercise sustained self-requeueing, a pulse/drain
feedback stream, finite FIFO batches, budget overrun by one indivisible callback,
the256 cap, nested consumption, concurrent producer arrival, retained payload
cleanup and exceptions. An i686 fixture includes the actual SDK console/pump,
with controlled clocks and passive window/frame endpoints, and checks Present
deferral, asynchronous command replies and command removal while queued. The
exact retained C48 pump fails its first new-work deferral assertion. Focused
receipts and the old-source executable are retained under
`out/editor-preview9-outside-batch/`; no game or deployment is part of this test.

### Upstream 0.7.3.6 integration and native Terraform follow-up (2026-11-03)

Upstream reserves top-level NEST/PARTS IDs 52/53. The uninstalled integration
uses protocol 24 with ADVENTURE/EDITOR IDs 61/62; installed C49 remains protocol
23. C52 native joining exposes the Controller Config's stale literal 53 despite
the dispatcher using the new enum. Use `net::MSG_EDITOR` for the default and
receive independently through the production dispatch constant in the fixture.
C53 native automatic host/guest entry passes the exact 129-class saved file.

The upstream bare `coop.editor` creation-request/parts diagnostic and named
shared-editor actions must have one registration. Duplicate command names
silently replace the earlier handler. The combined handler retains upstream
no-argument output and executes both paths outside Present.

On C53, both authors pass native Hazards (`899EB157`, field `8E390196`),
day/night lock (`074261F8`) and time (`073BEC90`, both field `0B208B41`).
Checked native-toolbar Undo/Redo restores only the expected field; each action
creates one synchronized history item. Fresh Environment source hits and
screenshots are retained with `out/preview9-c53-two-hazards/` and
`out/preview9-c53-two-clock/`. Six actions plus their history sequences settle at
revision 24 with strict peer convergence. This does not qualify other controls.
## Flora replay resolves UILayout before IWindow traversal (2026-11-03)
<a id="editor-flora-layout-container"></a>

CORRECTION (2026-11-03): the earlier `FloraUIVTable` symbol comment called
FloraUI `+168` a root IWindow. It is a **UILayout pointer**. Original dc04
`EE8C92` loads it into ECX; `EE8C98/EE8C9C` push true and control072BFDFD,
then `EE8CA5` calls UILayout::FindWindow810650. The installed C49 and C53
deferred replay incorrectly passed this UILayout directly to `sui::find`,
which begins with IWindow::GetControlID at vtable slot1C. UILayout vtable
1418360 has811C70 in that slot, not a window getter.

C53's normal Density edit committed R24 to25 and Undo reached26. Repeating
the full-family fixture crashed the host before the slider setter: the exact
stack resolves to `sui::find` from `replay_flora_control`, with Native
UILayout::Delete+75 faulting on read19. Loaded game base00130000 gives bias
-2D0000; the observed vtable01148360 is preferred1418360. Crash, closed
profiles and R26 draft are preserved in
`out/preview9-c53-flora-crash-preserved/receipt.json`. This does not implicate
the separately verified ISlider::SetValue(value,false) ABI.

The existing verified GetContainerWindow810610 is thiscall(UILayout), plain
ret, with no allocation or callbacks in its body: read layout+14, return null
if absent, otherwise read backing+88. The narrow Sui helper checks both
headers before that getter and closes backing/window identity afterward.
Deferred Flora controls retain only the original root pointer value beside
the existing UI/layout/scope/generation/field guards. Replay first checks the
current owner, resolves the current IWindow, and refuses a replaced container
before window traversal. It checks current context/root again before touching
the returned card and before authored replay. No IWindow method is called on
the UILayout, and no UI/scene mutation is permitted while ownership waits.

The controlled regression gives UILayout, backing and IWindow distinct
storage, poisons the layout's window-getter slot, and requires repeated
Density replay without that dispatch. It also covers layout/root replacement,
ownership loss, cancellation and scope/generation/field expiry during getters.
This is a regression contract; native replay acceptance remains a separate
Root-run check. Static executable bytes, bounded disassembly and the source
hash are retained under `out/preview9-c53-flora-layout/`; no Native call was
used for that proof.

Controlled actual-i686 validation retains a genuine RED against the unmodified
C53 adapter source, SHA0ff6b0a253784d57d8f4165fb5ce909f172bad1d95021c70680836bb3c6bd5bd:
the new fixture fails exactly at the poisoned UILayout window-getter dispatch.
Its old complexity fixture avoids coupling that RED to the unrelated new sampler
API. The superseding strict build passes990 Flora checks and140,147 full Native
checks, with59 compiler/Make inputs unchanged. The working build includes the
separate passive complexity sampler; the Flora-only patch excludes its code.
Receipts are `out/editor-preview9-flora-layout/red-receipt.json` and
`out/editor-preview9-flora-layout/receipt.json`.

The first new repeat-sequence test correctly waited because its synthetic Undo
settlement cleared pending history but omitted terrain_rebuilding. Only the
fixture's settlement was corrected. Its failed executable, log and original
source remain in the same directory as first-*; no production fix was hidden
by that test correction. These controlled results do not replace live C54 or
installed-C49 backport acceptance.

## GroundEffect forest companion ownership and Flora refresh (2026-11-03, feat/ga-editor-multi-act)
<a id="history-ground-effect-flora-refresh"></a>

BUG-0471: C54's first normal Flowers action at revision42 was rejected with
`Editing shared properties requires ownership of their settings.` The retained
failure is `out/preview9-c54-two-flowers/result.json`. Independent read-only
checks in `out/preview9-c54-flowers-rejected-fields-r2/verification.json` prove
both participants returned to exact revision42 Planet values and history,
strictly converged and fault-free. This was an ownership rejection, rather
than a failed restoration or a reason to widen Core's lease rules.

CORRECTION: the earlier single-field Flowers/GroundCover recipe was incomplete.
Both native UI routes call `F35160` (`EE81A2`, `EE8233`). That setter writes
TerraformMode+160, then calls `F34500` at `F3516A`. In addition to selected
GroundEffect `56B14F05`, `F34500` removes and regenerates forest key array
`03ABB0AF` (`F34542`, `F345F4`, `F34644`). Its first ResourceKey is
`57974AC6!00000000.00000000`; a nonzero selected effect supplies the second.
Both fields therefore require their existing exact Planet Field leases before
the setter runs. GroundEffect's gate now requests exactly those two fields.

The actual restored native baseline on both peers has GroundEffect `A9D6A81D`.
Forest type20/flags9C bytes are
`000000020000000cc64a975700000000000000001da8d6a90000000000000000`.
The verified emitter substitutes `57e2c074` for `1da8d6a9` when Flowers selects
`74C0E257`; count, width, first key and zero companion words stay unchanged.
This expected AFTER is static evidence until a successful running-game action
retains it. Exact executable-range hashes and disassembly are in
`out/preview9-c54-ground-effect-re/receipt.json` and `disassembly.txt`.

The new controlled Native gate passes its actual requested scopes to the real
Coordinator. The unchanged retained C54 source fails with the same Core1083
error at `decision.accepted()`; the corrected source passes108 focused checks,
990 prior Flora checks and140,280 full Native checks, with59 inputs unchanged.
The test requires zero setters before grant, foreign ownership of the forest
companion blocking the queued first action, value/revision/denial expiry,
one two-field gesture, and exact Core Undo. RED/GREEN receipts are under
`out/editor-preview9-ground-effect/`; no Core or Runtime ownership policy changed.

After the rejected action, the native Flowers checkbox could retain its clicked
state despite restored Planet values. Native `EE8C80`, called by TerraformUI
category2 at `EE917F`, reads the current density and ground effect and refreshes
all nine Flora cards, fifteen GroundCover choices and Flowers flags/images.
Its Density slider uses `SetValue(value,false)`; the card helper `EE8360` only
updates child flags. The verified alias is `RefreshFloraUI`; GOG2016 remains
disabled under BUG-0108.

The existing deferred Environment refresh queue also services visible Flora.
Restoration already schedules that queue after a terrain rebuild, covering
Undo, checked rejection rollback and remote canonical changes. It waits for
a later settled frame outside Present, retains the existing scene/source,
generation, scope, serial and input fences, then independently pins the current
FloraUI, UILayout, backing object, container IWindow and category. The leaf
container getter runs without cached memory probes across its call. Policy
callbacks are followed by a callback-free comparison of the complete UI owner
before dispatch. A stale queue cannot refresh a replaced widget or replay an
authored setter. The existing `environment_ui_updates` diagnostic counts either
supported Terraform panel; no wire or public Native API changed.

The superseding strict actual-i686 executable passes108 GroundEffect,990 prior
Flora,900 Flora UI refresh,1,137 Environment and141,185 full Native checks.
All59 compiler/Make inputs are unchanged. The new UI cases cover exact restored
checkbox/option/nine-card values, later-frame execution, source/generation and
all UI owner replacements during the container getter, replacement from the
last policy callback, cancellation/held input, exceptions and reentrant newer
refresh preservation. `out/editor-preview9-flora-refresh/receipt.json` retains
the complete source set and logs. The ABI verifier passes102 entries and
repository symbol/docs lints pass. Running-game C56 acceptance remains pending.

### Galaxy intro must finish before Adventure Editor entry (C56/C57)

C56 recovery retained exact canonical/native data and reached Ready at revision58,
but the host remained in the Galaxy orbit cinematic without toolbar07104132.
The guest had the ordinary editor UI. The host's existing `coop.status` proved
`cinematic 8915F9BB playing (menu intro)` and `busy: cinematic,input_lock`;
this was not inferred from camera appearance or missing controlled creatures.
Evidence is under `out/preview9-c56-cinematic-observe` and
`out/preview9-c56-editor-ui-arrival`.

The existing world cleanup handles the exact GGE_Intro8915F9BB before a normal
world load, and later only in Cell/Creature/Tribe/Civ/Space. Scenario is excluded.
The shared editor's existing native entry bypassed that load cleanup. A menu
intro crossing into Scenario can consequently retain its input lock and hidden
HUD while canonical recovery checks still pass. The current Adventure intro and
talk barriers require an active co-op attempt, so an idle content run does not
authorize ending any Adventure cinematic.

The common `editor_native::enter` now runs an exact-menu preflight before
ResetRequest, the first native entry mutation. It owns the input key and pins
module, game thread and generation before any symbol/getter callback; getter
reinit cannot become a new entry authority. The world adapter ends only active
GGE_Intro while that copied Galaxy fence remains current. Other active
cinematics return an explicit error untouched. Fresh observations require the
same manager, inactive state and native end ID zero; unknown/stale contexts
perform no entry mutation. Reentrant entry is denied. No native owner or task
is retained, and all existing entry callers remain outside Present.

ADFF50's exact dc04 body supplies the postcondition: ADFF87 decrements the
native input lock, ADFFA5 reads the prior cinematic ID, ADFFBF explicitly clears
manager+148, then ADFFE1 publishes044F1189. The native active-state cleanup is
called at ADFF60. Inactive managers may retain an old ID and are accepted without
calling End. This does not broaden the global world tick's stage set.

The host's explicit Recover rejects before a new Recovery/controller is
published and can be retried. An automatic guest entry failure retains the
existing explicit failed-join/Retry path; it never becomes a healthy baseline
or silently spins. Full planet/placement/dependency/recovery proof is unchanged.

Strict controlled validation passes134 actual-i686 native-entry assertions,
54 pure preflight assertions on Linux and i686, and the affected world object
compile. Focused Flora990, Environment1,137 and complexity1,199 remain green.
The exact retained C56 native producer reproduces the missing-toolbar assertion
with an active menu intro. `out/editor-preview9-editor-entry` retains source
pins, exact ADFF50 bytes, old-producer RED, final focused logs and executables.
These controlled checks do not replace C57's running-game entry qualification.

## Restored Color UI gradient controls (2026-11-03, preview9)
<a id="history-color-ui-refresh"></a>

C57 normal gradient height input changed only B6929C92, at index2, from the
visible y303 node to y330. Undo restored every Planet field and both replicas
remained strictly converged and fault-free at R102. The authoring host's native
gradient marker remained at y330 while the guest displayed y303. This is a local
control refresh failure, separate from accepted data/history correctness
(BUG-0473). Exact input, fields and both screenshots are retained under
`out/preview9-c57-gradient-height-probe`.

Verified dc04 native TerraformUI::Refresh EE9160 has a category1 path at EE9186:
TerraformUI+C owns a wrapper (primary vtable148ABDC, ctorEE8B2A), whose+10 owns
the actual ColorUI (primary vtable148AA00, ctorEE6D95). EE9190 calls EE6AD0 as
thiscall(ColorUI), without stack arguments; EE6D79 is a plain ret. ColorUI+18
is a UILayout created through8100A0, not an IWindow. ColorUI+1C owns the gradient
UI (ctorEEAA18, primary vtable148AC9C). Its+14 F68DDB4A and+18 168F59FC windows
are retained by EEB2B0/EEB2F7. The refresh updates four swatches, then reads
the current native color/elevation vectors through F0D2C0/F31CE0 and calls
EEB480 to reconstruct the gradient's UI nodes. It has no direct authored setter
call. Static bytes, ABI checks and disassembly are retained under
`out/preview9-c58-color-refresh`; the exact supported executable hash is
dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37.

The existing later-frame, outside-Present deferred refresh now includes current
category1. It freshly pins the wrapper, ColorUI, gradient, their vtables and
window pointers, and the distinct UILayout/backing/container. Native scene,
source, generation, read scope, input counters and queued serial must remain
current across the getter and policy callbacks and after the refresh. A category
change drops the passive request without navigating the user. No authored
field, lease, camera or native history policy changes. The genuine unchanged
C57 Native producer fails the displayed-gradient assertion in the new fixture;
its code matches the retained C57 source after CRLF-to-LF conversion only.
`out/editor-preview9-color-refresh/red/receipt.json` records actual-i686 strict
build success, expected test exit1 and66 compiler/Make inputs with zero drift.
The final shared producer passes1,567 Color checks,990 Flora controls,900 Flora
UI,1,137 Environment,838 delayed Clone and143,729 full adapter checks, with68
compiler/Make inputs and zero drift. Its receipt is
`out/editor-preview9-color-refresh/receipt.json`; the first compile failure and
two earlier clone-fixture assertion failures remain retained separately.
Actual corrected native gradient Undo UI acceptance remains a separate Root-run
check.

CORRECTION (2026-11-03): that pending qualification is now completed on private
C58. `out/preview9-c58-two-gradient-ui/result.json` and `ui-proof.json` retain
both authors' normal height303 to330 and own Undo/Redo/Undo, R3 to11. Only
B6929C92 changes; six UI checks prove restored303 and absent330 after Undo.
Both final screenshots were reviewed, with no tab toggle, strict peers and
zero faults. This closes BUG-0473 for the private protocol24 candidate.

## First Alt clone retains its press while ownership is pending (2026-11-03, feat/ga-editor-multi-act)
<a id="adventure-editor-first-alt-clone"></a>

Actual C57 rejected the first ordinary Alt-drag of the selected Sound at
revision92: ToolAction11 returned on a pending Structure lease, without a held
continuation. No clone was allocated and both peers remained strict. Existing
move holds start Native history before the lease, so they cannot be used to
allocate an unowned clone. Replaying only after release is also insufficient:
the first tick would sample the release cursor as the original grab.

Exact GOG2024 `EFD860` is `cdecl(GameData* source)`, plain `ret` at`EFDAEA`.
`F01BB1` passes the dragged actor and the caller removes four stack bytes. The
function computes its local offset from the current camera ray and source
transform. Its only Context output stores are the three floats at`6C/70/74`
(`EFDACD`, `EFDAD9`, `EFDADF`); it does not load the old output words before
writing them. The fallback can write a valid offset while returning false in
AL. The adapter therefore temporarily marks only these derived outputs NaN,
requires all three Native outputs finite, and restores the previous bytes
before waiting. No marker, history or authored pose operation runs there.
The named alias and exact entry/caller/output receipts are in
`tools/addrport/verify_editor_dc04.py` (original executable SHA`dc04aee5...`).

The held clone owns values: the original scene/source/context identities,
source and class revisions, exact source marker and class-row bytes, complete
live slot/class/editor-key roster, modifiers and the Native-derived anchor.
Every policy callback closes those pins again. Both Placement and
Structure/Class gates are requested on the first gesture. Denial, Escape,
focus loss, timeout, a new capture or any identity/revision change retires it.
No borrowed Native reference or marker pointer is dereferenced across waits.

After both grants, one original ToolAction11 starts the genuine clone/history.
A one-shot replay identity spans its nested direct CloneSelection route. Its
first Native tick runs with all four authored pose steps independently blocked,
including if a callback clears the held optional. That tick consumes Native
flag64 and initializes radius/elevation. Only then is the retained press anchor
installed; one ordinary tick consumes the current cursor and the matching
release finalizes once. Existing Native capture, opaque proof and canonical
admission remain the final authority.

Already-owned Shift/Ctrl variants keep their Native route. A pending modified
variant cancels explicitly with a notice; preserving its alternative drag mode
remains open. Unknown auxiliary clones remain blocked. This change does not
claim complete modified-delayed-clone coverage.

The retained exact C57 producer (`88253c94...`) compiles under the controlled
i686 fixture and exits1 at the first missing held-clone assertion.
`out/preview9-c58-clone-input/old-c57-red-receipt.json` retains its executable,
original fixture bytes and source hashes. The superseding shared strict
controlled run records delayed-grant-after-release movement, one allocation,
scratch restoration, first-tick overwrite, cancellation, source/class/key and
callback-retirement negatives: 838 focused checks and143,729 full checks pass
under strict actual-i686 compilation, with68 compiler/Make inputs unchanged.
The shared receipt is `out/editor-preview9-color-refresh/receipt.json`.
Actual Native Alt-clone and count-delta
qualification remain Root-owned.

CORRECTION (2026-11-03): C58 now qualifies a real Sound Alt-clone, R1 to2 and
Undo3, with exact restored native/semantic bytes, Sound count4 plus one, and
unchanged uniqueness counts and thirteen tuning words. Evidence is retained in
`out/preview9-c58-sound-clone-qualification/result.json`. This does not establish
every delayed modified clone variant or native count-boundary acceptance for
the generated carriers.

CORRECTION (2026-11-03): the preceding shorthand "Sound count4 plus one" means
native input count[4], from24 to25. It does not mean a baseline value of four.
The retained C58 sample and typed new-class proof establish this exact delta.

## Planet-look preset ownership begins at its selected card (2026-11-03, feat/ga-editor-multi-act)
<a id="history-planet-look-preset-ownership"></a>

Verified GOG2024 executable SHA `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`
has the Tab0 selected-card callback at F0CD40. Its ABI is bool AL
`thiscall(presetCard+20, uint32 message, void* source)`, ret8 at F0D2A5/F0D2B3.
Only message0743B6FA with source equal to the primary card is the selected
operation. The primary card vtable is148BAC0, message-interface vtable148BA84;
card24 owns a UILayout, and card28/card30 hold the selected `.prop` instance/group.
The current TerraformUI10 Tab0 wrapper has vtable148ABBC. It is distinct from
the card and from the UILayout's container IWindow.

F0CF0D starts one history item through F33A90. The callback then invokes the
water/temperature/atmosphere, type, color/gradient and ground-effect setters,
directly calls F37340 at F0D220, and commits through F34400 at F0D25D. It never
calls the existing wrapped full Load F32A90 or Build F37530 route. F37340 is
`thiscall(tool, const vector<ResourceKey>*)`, ret4 at F3737F. F31DC0 writes the
Flora key list, then F35B20 regenerates/truncates the live bands and intensity
vectors as required. Native capture F27C00 through F33A50/F32BB0 exports the
four Flora fields04E13CB7,073154E0,073137BB,072BEC91. The previous nested
gates therefore missed bulk Flora; adding a queued inner setter would split
one native operation. BUG-0474 records this source/controlled-producer defect.

The new outer barrier requests these seventeen Planet field scopes together:
`03A23F98,03A23F99,07B18058,0536250C,0536250D,968C496A,968C496B,B6929C94,`
`B6929C95,B6929C92,B6929C93,56B14F05,03ABB0AF,04E13CB7,073154E0,073137BB,072BEC91`.
It retains only owned values and exact field revisions plus current scene,
source, project/epoch, generation, category, selected key, wrapper/card and
UILayout/container identities. Copy charging precedes allocation. Fresh root
ancestry and policy closure are repeated before replay; no Native AddRef owner
or marker pointer is retained. Denial, Escape, focus loss, timeout or changed
context retires the intent. One original callback still owns one native
Start/Commit. Cleanup retires only this operation's unused before-image after
pre-start callback expiry or native acknowledgement without a Start.

The caller's false return must not be guessed as a UI propagation contract.
F0CB30 registers this listener through the native message manager. Immediate
dispatch8855A0 is followed by unconditional jump8855A2; deferred-copy
dispatch88568A by unconditional jump88568C. Neither tests AL. The queued false
result is retained. Supported entry/return/call and vtable receipts are added
to `tools/addrport/verify_editor_dc04.py`; GOG2016 remains explicitly frozen
under BUG-0108, with no invented2016 addresses.

The unchanged C58 Native producer from a16c41e passes real before/after capture
and opaque support in the controlled fixture, acquires only three scopes and
hits the real Core rejection `Editing shared properties requires ownership of
their settings.` Expected exit1 and exact old-source inputs are retained in
`out/editor-preview9-planet-preset/old-c58-red/receipt.json`. The new strict
actual-i686 fixture passes8,910 preset checks, including each independently
foreign-held field, zero setter writes before grants, one accepted history item,
exact Undo, no-op cleanup and stale/reentrant callback negatives. Adjacent
Flora990, Flora UI900, Color UI1,567 and Environment1,137 checks also pass;
`out/editor-preview9-planet-preset/receipt.json` pins seventy compiler/Make
inputs with zero drift. Earlier controlled setup failures remain retained.
No actual Native Tab0 preset acceptance or full serializer oracle is claimed.

CORRECTION (2026-11-03): the separate native preset acceptance now passes on
private C59. Host first preset R45 to46 is one history item, then own
Undo/Redo/Undo reaches49. Guest R49 to50 produces one item, then own history
reaches53. Both actual presets change fourteen fields, including Flora keys,
bands/intensities and GroundEffect; full Planet comparisons and strict peer
checks remain exact and fault-free. Seventeen is the potential native write
set, not a claim that every field changes on each preset. Evidence is retained
under `out/preview9-c59-first-preset-probe`, `out/preview9-c59-first-preset-history`
and `out/preview9-c59-guest-preset-r2`. BUG-0474 is closed for the private candidate.

CORRECTION (2026-11-03): controls07393E58/073A7978 in F3AAA0 are Update/Paint
parent controls, not an established Style popup. The verified B60B4D toF339F0
route is the separate native `adventureLook` cheat, with options `-filmnoir`,
`-eightbit`, `-sixteenbit`, `-watercolor`, `-norainbows`, `-sepia`, `-none`.
Root's click857280 correctly produced no change at R44, retained under
`out/preview9-c58-style-popup`. No missing Style GUI control or GUI defect is
inferred; command synchronization qualification remains separate.

The independent C58 first-clone crash above1,024 is a native marker-arena
lifetime gap, not a preset or serialized-identity failure. The exact capacity
and actor+18 evidence, C60 pre-bind constructor policy, allocation-fallback
behavior and protocol25 isolation are recorded in
[the complexity storage audit](adventure-complexity.md#complexity-marker-storage-lifetime).
The native authored Save/history formats are unchanged.
