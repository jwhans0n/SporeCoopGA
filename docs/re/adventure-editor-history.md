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
