# Native Adventure Editor saving and preview (dc04)

## Preview 6 act/captain authoring investigation

Static receipts in `out/editor-preview6/native1.txt` through `native8.txt` use the
same supported executable below. Native receipts are summarized in the
[validation report](../reports/EDITOR-NATIVE-FLOW-VALIDATION.md).

* Placement position, orientation and scale are adventure-wide marker fields
  `04/10/20`. Marker acts at `28` are `34`-byte wander/patrol records, not separate
  placement transforms. Local act browsing must not relabel the stable placement
  or cast ownership identity. These layouts agree with the upstream
  [marker](https://github.com/Spore-Community/Spore-ModAPI/blob/master/Spore%20ModAPI/Spore/Simulator/cScenarioMarker.h)
  and marker-act headers and were checked against `F44210` and `F3D930`.
* Native acts are `534`-byte rows at resource `23C`. Names/descriptions are owning
  scenario strings at `00/3C`; time limit/visibility are `78/7C`; music is `80`;
  goals at `84` have stride `188` and a maximum of three. `DFED80` deep-assigns
  one row (`thiscall`, `ret4`), while `DFF620` owns act-vector assignment
  (`thiscall`, `ret4`). Never copy their string/vector ownership pointers.
* `EFC7B0` is `cdecl(act)` with a plain return. It updates the local context and
  calls `F462B0(data,1,previous,next)` only when the act changes. There is no
  history boundary. Native history restore uses the current context act, so a
  canonical scene refresh can preserve independent browsing.
* Captain settings are resource asset `130`, pose `150/15C/16C`, health `170`,
  invulnerability `174`, and authored lock `175`. `F3D930` writes the special
  `data+8C` captain marker's pose back to these resource fields. `F432C0` creates
  this special class `FFFFFFFE` marker; it is not a normal cast allocation.
* `F40980` is a posse-member append helper, not a captain setter. Its editor
  message callers also handle initial posse; it must not be hooked as though it
  changed the authored captain.
* `ED0B40` is the separate captain-property IWinProc (`thiscall(window,message)`,
  `ret8`); lock control `0770B540` owns a normal Start/Commit pair and writes
  `resource+175`. Class-property handler `EE5380` explicitly excludes negative
  special class indices, so enabling the ordinary cast inspector is insufficient.

The cross-act report names Obnoxious SpacePonies: Solo applies aggression and
finite health correctly, while Together previously showed the aggressive visual
without attacks, damage or finite health. C2 reached one private four-player run
after the missing-assets launch fix in `adventure-world.md`. Its Act 2 intentionally
retains shields; ponies attacked captains with matching hostility and health.
C2 later crashed during spatial goal evaluation. C5's lifetime guard survived a
longer combat session and shared Mission Complete, then restored all four drafts.
Starting together in Act 3 applied hostility and finite `202/202` pony health on
all four clients. Native player damage remains a separate acceptance assertion.

Further static receipts `out/editor-preview6/native20.txt` through `native26.txt`:

* Captain asset acceptance is asynchronous palette `ED3AAE`, calling `F42420`
  inside Start/Commit. `F42420` is `thiscall(data,ResourceKey*)`, `ret4`, writes
  `130/134/138` and recreates the special captain through `F41A50`. Hold a copied
  resource key for ownership replay, never a palette payload or callback pointer.
* `F448E0` AddAct and `F440B0` DeleteAct are `thiscall(data,index)`, `ret4`.
  Their only receiver access is resource `+10`; they can build an owned private
  resource without calling native UI or scene code. Add accepts insertion
  positions `1..count` up to eight acts. Both update the matching class-act and
  marker-act collections. Native controls are `07BD2DD0` and `0743BC38`.
* Class-act vector assignment `DFEC20` is `thiscall(dst,src*)`, `ret4`, with
  `4E0` rows at classData `70` (class map row `78`). Marker-act assignment
  `DFCEB0` has the same convention, `34` rows at marker `28`. Marker-act
  owning patrol points start at `1C`, followed by style `30`; the first `1C`
  bytes are primitive wander/deprecated-position data. Do not use the upstream
  header's conflicting patrol offset without checking native `DFB860`.
* Owning insertions `F44620` (act), `F43720` (class-act) and `F42C60`
  (marker-act) take `thiscall(vector,position,src*)`, `ret8`. Source rows must
  belong to a separate owned resource when the destination may reallocate.
  Reorder all three vectors together; raw row copies alias strings and paths.
* `EE3700` validates goal assignment/drop. It is not an act-reorder function.
* C3 added an act from a guest and converged all four native drafts at revision
  6 with zero caught faults. Immediate roster Undo was denied by overlapping
  ownership. Broad IWinProc gates were acquiring/renewing leases for nonmutating
  panel messages. `ED0B40` only authors captain lock on `287259F6/0770B540` and
  health on `07A4489C`, `07A44749`, `EF00A884` with control `0742CD10`.
  Picker browsing delegates to ECF370; its accepted resource key is independently
  fenced by F42420. `EE5380` authors on messages `6`, `7`, `1C`, `0685A2B9`,
  `04CAB02D`, `EF00A884`, `07A44749`, `07A4489C`, `287259F6`. Mouse movement
  (`8`), paint and focus gain must not acquire class/captain ownership.
  ScriptUI `1C` commits text for controls `0743B978`, `0710A140`,
  `05CA82B7`, `07CCBD48` and the inline goal count `07DF4C98`. The last calls
  `EE3440` on Enter or focus loss and writes goal `0C` inside Start/Commit.
  Focus notifications use argument `0`; `1B/1C` with argument `1` are mouse
  enter/leave. Reserve ownership on actual text focus and renew while typing.
  Mouse hover must not acquire an act lease, and only focus loss can commit text.
  Native input fixtures cover
  these nonmutating messages and stable settings ownership after reordering.

Native failure and recovery receipts:

* C4's roster Undo requested Document plus many redundant Class leases and exceeded
  the `1100`-byte LeaseReply bound after the host had durably accepted it. Roster
  Undo now requests only Document. Ordinary history scopes are deduplicated;
  captain position plus quaternion otherwise requested its Settings scope twice.
* C8 passed four-player Add/Undo/Redo, reorder/Undo and Delete/Undo through native
  controls. Independent act views survived. Private roster gestures and cold
  recovery have no native cursor entries. The exact owned native toolbar buttons
  `B006EF6E`/`F006EFA5` must use shared chronological availability, as do the hooked
  command getters. Never add a dummy native resource edit to make Undo available.
* C6 accepted stock captain Ace from the guest native picker and restored the
  prior Bardados key through Undo without losing its pose. C8 captain drag and
  Undo converged all four drafts with one history item. Checkpoints read back
  through v4 state and v3 native mapping in cold C7, C8 and C9 processes.
* Win32 harness key injection is asynchronous. Keep Ctrl held across a frame
  fence around Z/Y, then release it. Immediate down/key/up can clear the polled
  modifier before the game handles Z and falsely report a broken native shortcut.
  The numeric-count input attempts did not reliably enter a new value. C11 also
  reproduced the same failure in a detached unshared editor. Do not claim native
  numeric-entry acceptance from those attempts. Final native goal acceptance uses
  the All control and goal deletion, each with checked Undo. These are distinct
  from the deterministic complete goal-block tests.
* C10's temporary input trace treated keyboard message `18` as a window pointer.
  A Ctrl input crashed the diagnostic candidate at `07E1310A` with an invalid
  execution target. This introspection is removed from the final source; field
  `18` is inspected as a window only for verified focus notifications. The C12
  authoring and roster runs repeat native keyboard history without faults.
  Receipt: `out/editor-preview6/c10/crash/crash-20261029-004551-i92.txt`.

The new checkpoint wrapper v4 stores stable act order and its revision. Native
mapping v3 stores matching ordered act/settings identities and validates them
against the loaded canonical state. Existing mapping v1/v2 remain readable.
Logical act IDs survive native index changes; the settings owner is independent of its
displayed act number. Rare roster changes require Document ownership, while
ordinary metadata edits retain per-act settings ownership. Native placement pose
and cast data remain adventure-wide; the canonical parent is the minimum live
act ID and is reanchored when that act is deleted.

These static receipts apply only to installed GOG executable SHA-256
`dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`.
They establish addresses, call arguments, field ownership and stack cleanup.
They do not establish a successful shared editor save or playtest in a running game.
The callable names are in
[the save overrides](../../data/symbols/overrides_editor_save_dc04aee5.sym).

## UI ownership and command distinctions

`ED5130` constructs the native Adventure Editor display object; it is not Display
or an operation that should be called on an existing object. `EF2154` allocates
`98` bytes, `EF2167` calls that constructor, `EF2186` stores its result in
`ScenarioMode+14`, and `EF2199` calls `ED5C70` Load on the same base pointer.
Its primary vtable is `1489DFC`. The window callback `ED7910` receives the `+8`
subobject; message callback `ED6AF0` receives `+10`. Their native call sites subtract
those offsets before calling methods on the base.

The display object's `+1C` is its active Build/Play/Terraform mode, updated by
`ED57A0`: its comparison reads `+1C` at `ED57AB`, and the state write is `ED5801`.
`ED6620(base,0)` selects Build through `ED6836 -> ED57A0(0)`.
The distinct `+20` is the return display mode. `ED47A0(base,mode)`, `ret4`,
only writes `+20` at `ED47A4`. Entering Play copies the previous `+1C` into
`+20` at `ED67E6..ED67E9`, then selects active mode1. A normal Build editor
can therefore retain `+20=-1`; requiring that field to be zero would incorrectly
reject a valid editor. Readiness checks active `+1C=0` instead. The shared raw
display guard allows initial/idempotent Build only from active `-1` or `0`;
the coordinated preview return owns its separate bypass. Static receipts are in
`out/editor-save-re/display-contract.txt` and the exact-image verifier.

`F412C0` validates placement; it does not select or replace a captain. The Play
button passes the current avatar marker, while ordinary drags call the same
method at `EFEA0B` and `EFEA4B`. Its checks set/clear marker `+224` bits `10`,
`02` and `04` at `F412E4..F4133D`. These derived validation bits can change during
a valid gesture. The opaque-change guard ignores only mask `16`, retaining the
other authored flags. The verified alias is `ValidatePlacement`; a broad
shared-session hook on this method would incorrectly block ordinary movement.

| Native control | Behavior and receipt |
| --- | --- |
| `90439D7C` | Red X, Cancel/Exit. `ED7FBD..ED7FC0` calls `ED75E0(base,1)`. A hidden isolated-profile observation showed the discard-changes Cancel/Quit prompt. It did not save. |
| `BBBB1001` | Green Done, Save+Exit. `ED7FE6..ED7FE9` calls `ED75E0(base,3)`. |
| `06FCB638` | Disk Save, stays in the editor. The native input Save route at `EDA319` calls `ED75E0(base,2)`. |
| `07104131` | Top Play triangle, native preview. `ED7E48..ED7E7D` checks the selected captain and calls `ED6620(base,1)`. |
| `07104130` | Top Build hammer. In the playing UI, `F0EB09..F0EB25` performs play cleanup, sets the return display mode, then returns to the editor. |
| `04766FF0` | New adventure, not preview. It sets pending action5, eventually calls `F44C70`, and resets the authored document. |

The destructive New path is a useful negative receipt: `F44C70` constructs an empty
`2CB0` ScenarioResource on the stack, copies it over `data+10`, resets avatar/acts,
metadata and history, and publishes the reset notifications. It must never be used
as a preview or editor-return helper.

## Save As, completion and preserved originals

### Coordinated native Exit continuation

The native confirmation callback `ED73F0` resumes pending commands through
`ED5820(UIbase)`. The entry reads `UIbase+58`, dispatches actions 1 and 3 to
`ED586A`, pushes `07EAF738`, and calls the ordinary native exit at `ED5240`.
The selected branch returns with plain `ret` at `ED5875`; its default branch
clears `+58` and returns at `ED583B`. This is a thiscall with no stack arguments.
The verified name is `SporeCoop::Editor::ContinueCommand`; the exact-image save
verifier checks its entry and exit branch return. Intercepting this continuation
can hold an already confirmed Exit without inventing a dialog result or allowing
the native context to disappear before accepted shared edits settle. Calling
the original continuation with action 1 is reserved for an owned, authorized
group exit. Cancelling the ordinary confirmation clears `+58`; a cancelled or
replaced context must not execute a retained continuation.

This static receipt alone does not establish a successful two-game group exit.

The ordinary native save uses its own key generator, thumbnail/metadata work,
resource manager writes and confirmation UI. The mod does not construct a native
creation database record or overwrite Pollination/EditorSaves externally.

`F41110` is thiscall `(data,uint32_t* reasonOut)->bool`, `ret4`. It requires editing
submode1, no existing native save phase, a valid avatar and the native modified
state checks. A rejected gate is not bypassed. `ED7390` is thiscall
`(UIbase,bool replaceOriginal)->void`, `ret4`; the tail at `ED73D2` enters
`F3D760(data,replaceOriginal)`, which queues native thumbnail/save work:

- `data+18=1` starts the pending phase.
- `data+1C=!replaceOriginal` selects retention of the previous resource.
- `F44E80` generates a fresh key of Adventure type `366A930D`, using the native
  configured group. `F44EEF..F44F19` retains the previous key and assigns the new
  key to the live resource. No group constant should be assumed.
- Native metadata, image and scenario resources are saved under that new key.
  The scenario write success and resource-manager completion result are checked
  at `F4529E..F452BC`; later native image-save failure also clears the result.
- `F4535B..F4539B` removes the previous key only when `data+1C` is false.
  Thus `ED7390(base,false)` is the native preserve-original Save As branch.
- `F4540E` clears `data+18`. Only success clears dirty flags at
  `data+80/+84/+85` and publishes `FD7A08D0` with the saved key at
  `F45421..F4546B`. `F44E80` returns success in AL, plain return `F4547B`.

`UIbase+58` is the pending action. Mode2 keeps editing; mode3 is Save+Exit.
The collaboration module starts the verified Save As branch with pending mode2
and `replaceOriginal=false`, after checking the native gate and absence of active
gestures/dialogs. Subsequent Save uses the same native branch with replacement
enabled on the already-owned draft.

Mode2 success restores native input and queues the native saved confirmation
`3CC9F2F3` (`ED6EA6..ED6EEC`, localized record `15AC184`). The normal native
confirmation callback `ED73F0` calls `ED5820`, whose mode2 default clears pending
action. The mod waits for that ordinary dismissal. It does not close or accept
foreign dialogs. Mode3 instead enters the native exit continuation; it must not
be substituted for a stay-in-editor save.

The module observes the real `F44E80` result for its exact pending data/resource/UI
ownership. It then independently opens and fully reads the installed record using
the previously verified IRecord/IStream interfaces, closes it, and asks
GetPrivateResource to parse a fresh uncached ScenarioResource with the same key.
Only after native success, normal confirmation dismissal and that readback does
it publish its saved callback. A changed editor/resource cancels the pending
callback. A failed native result cannot advance saved revision.

The completed native record is also the initial shared draft's byte source.
In candidate 6, the live Alpine Valley resource serialized to 1,037 bytes with
zero terrain properties, while its completed native Save As record
`408A0000!2908BABB.366A930D` contained 15,289 bytes and 30 terrain properties.
The native save constructs and enriches an owned copy. Serializing only the live
resource therefore omitted authored terrain even though a Write/Read/Write of
those incomplete bytes was identical. The host retained its already loaded
terrain; the guest loaded a blank planet. The read-only audit is recorded in
`out/editor-terrain-audit.json`, with the native and transferred byte prefixes in
`out/editor-terrain-root.bin` and `out/editor-terrain-base.bin`.

`read_saved` uses the same verified IRecord readback above to return the exact
completed record. Initial sharing, subsequent Save and recovery Save As must
normalize and validate those full bytes against the canonical placements, then
restore them into the shared native working resource before further captures.
Source identity, stable bindings and local camera ownership remain separately
checked. An identical serialization round trip alone is not terrain acceptance;
the native pair must also render the same authored environment.
This preserves the game's ordinary save-completion guarantees; it does not add an
independent operating-system power-loss guarantee to native creation databases.

### Unsaved planet snapshot export

`F27C00(owned ScenarioResource*)` is a thiscall with no stack arguments, plain
return at `F27C7B`. Native Save calls it at `F45201` on its independently allocated
scenario copy. It passes destination `resource+18` to
`F33A50(ScenarioMode+18, PropertyList*)`, then replaces property `02A907B7` using
the active planet service. `F33A50` refreshes its terrain cache, copies its
PropertyList into the supplied destination, and returns `ret4`. It does not
invoke the creation database save pipeline. The new verified callable name is
`SporeCoop::Editor::CaptureTerrain`; the exact-image save verifier covers its
entry, return and native Save/export call edges. Read-only decompilation and
assembly are retained in `out/editor-native-flow/terrain-query.txt`.

The unsaved-baseline adapter must parse an owned copy, export there, serialize
it and validate the unchanged semantic projection before sharing. Exporting
directly into the active adventure is unnecessary. Normal gesture capture keeps
its existing behavior. Native two-game terrain appearance still needs checking;
matching hashes alone do not establish the same environment.

### Explicit terrain proof across Save, local export and recovery

The live resource key changes at `F44EEF..F44F19`, before the owned scenario
copy receives `CaptureTerrain` at `F45201`. A materialization witness pinned to
the previous local source must not be reused under the new source implicitly.
Likewise, shared local-copy holds deliberately disable ordinary read identity,
and native history rebaselining changes the native generation. Recovery also
changes the logical epoch. These are separate transitions from input ownership.

`editor_native_save::SaveOperation` now pins module generation, a monotonic
operation serial, native generation, the actual numeric scene owner, original
local source and copy mode. Its original source may differ from the canonical
transport key. `preparing` runs before native Begin or copy-dirty changes; a
rejection starts no native save. `saved_operation` runs only after the exact
owned pipeline, normal confirmation dismissal and independent persisted-record
readback. Native Begin may finish the pipeline synchronously. The pending
identity therefore compares immutable fields while treating its completed
`after` source separately. An already observed pipeline cannot replace its
result through another pipeline call.

The strict Win32 Save fixture exercises synchronous success and failure,
completion exactly once, replaced module/source/generation/owner, nested Save,
and expiration during the second native CanSave call. No replacement UI or
owner is written to restore an expired owner's metadata. The focused module
fixture passed 628 checks in
`out/editor-preview9-terrain-transition-save.log`; this is controlled adapter
evidence, not native Save/reopen acceptance after a Terraform stroke.

Runtime uses bounded, value-owned `TerrainTransitionTicket` receipts. They retain
the complete entry snapshot, raw recipe and exact authored-to-materialized
witness, with a combined 64 MiB bound. Save normalization changes only owned
saved bytes, requires exact live and stored recipe witnesses and the exact
completed Save serial/source, and validates the canonical document before a
replacement checkpoint. Metadata and captain sidecars come from the actual
completed native state, including changes made in the native Save As dialog.

Exports retain each participant's actual working source, dirty state and view.
The authoritative held revision is checked independently of the earlier Save
request. A stale recipe requires a separately checked `ExportRebuild` from owned
canonical data, or rejection of the export. Normalization never guesses a newer
authored recipe from current rendered bytes. Restoration waits for native
terrain readiness and strict readback before reopening editing.

Recovery stages its new controller while its authoring hold remains active.
The old controller's recovery-specific teardown sends the existing authenticated
`Leave{3}` once, preserving each guest's Frozen old context and durable draft.
Ordinary End continues to send `Leave{0}`. The new private controller is not
frozen by the old local failure flag while recovery owns its explicit hold.
Editor-controller ticks are withheld during that private stage, so queued joins
cannot publish a new epoch or baseline before the replacement checkpoint.
Session transport and keepalives continue. A distinct `RecoveryBootstrap`
brackets verified canonical Load and `F35E10` under a synchronous read-only
identity when the old map is unavailable. Its prepared witness remains private
until native terrain/collision readiness, strict validation and accepted context
attachment. This boundary grants no input permission. A failed proof or
materialization preserves the previous checkpoint.

Controlled real-UDP coverage verifies that a frozen guest denies edits, rejects
an inconsistent host capability envelope, retains its old document on host
loss, and accepts the replacement baseline only through the normal authenticated
Offer and readiness path. No wire format or authentication rule changes.

The final strict i686 runtime suite passed 31,831 checks, including the 24 MiB
dependency-closure, cached late-join and recovery case. Focused fixtures passed
1,122 transition checks and 1,055
existing recovery checks. Captured-rejection identity coverage passed 1,334
checks; delayed cancellation ownership is covered by the transition cases.
Real UDP session suites passed 21,485 checks on
Win32 and 29,937 on Linux, including 78 focused recovery checks on each. Logs
are under `out/editor-preview9-transition-final-*` and
`out/editor-preview9-transition-session-linux-*`. These use controlled native
adapters; the independent native transition fixture tests actual adapter paths.
The source, executable and log hashes are recorded in
`out/preview9-transition-final-receipt.json` (62 compiler dependencies and no
critical-input drift).

These runtime paths require the focused lifecycle fixtures and a fresh native
Save/reopen reproduction before delivery. In particular, exact exporter/copy
counter differences must be proved at their native boundary; the transition
does not add a tolerance or accept unknown raw recipe differences.

### Private working keys must retain the native creation group

`F44A90` compares the supplied key's **group**, at key `+8`, against the native
configured group global `15B03A8` (`F44AA2..F44AA8`). A different group returns
false before resource loading. The initial value is `408A0000`; read the named
global at runtime instead of assuming that value. The verified name is
`SporeCoop::Editor::CreationGroup`, also used by native Save key generation.
Private project identity does not permit inventing another resource group.
Use a collision-checked private instance within the native group, and retain
separate provenance and native-save ownership.

The negative native receipt is candidate C2's hidden guest crash at live
`ED2F48`, preferred `EE2F48`, with base `3F0000`. The rejected group left the
native resource's act list uninitialized; the UI then dereferenced the absent
current act. Its crash receipt is `out/editor-native-flow/candidate-c2/crash-i74.txt`.
The adapter now checks the configured group and parses/validates the expected
act before entering the native editor. Metadata lookup in `F44A90` is optional:
its missing-metadata branch `F44C5E` rejoins scenario loading at `F44BA0`.
The read-only loader decompilation is `out/editor-native-flow/load-query.txt`.

### Dirty state after canonical remote changes

`F3B7C0` and `F3B2C0` consider a document modified when its saved native history
revision `data+80` is below global `16065F0`, or ordinary authored-change byte
`data+84` is set. The first getter additionally recognizes `data+85`, a distinct
new/imported state that must not be forced for ordinary remote edits.
Native writers `F3B678` and `F3BEE3` set `+84=1` when supported authored category
or resource-key fields change. Their enclosing methods have other mutations and
must not be called merely to mark dirty.

Native placement history commit `F45920` calls `F34170` on global history
`16065D8`. `F34226..F3422E` increments its `+18`, exactly `16065F0`, so normal
native gestures become dirty through history revision. Applying a canonical
remote snapshot without that native history append does not advance this value.
The adapter must explicitly preserve ordinary dirty semantics for accepted
remote forward/Undo/Redo changes and restore its captured dirty state on rollback.
The verified `+84` flag is the narrow authored-change field; `+85` is not a generic
dirty flag. `ED4D90` is also not a generic Save refresh: it forwards to a helper
owned at `UI+2C` and should not be called without separately verifying that role.

An unchanged source may fail native `CanSave` with reason5 until the normal
native editing workflow establishes a change. The save adapter preserves this
gate; other native avatar/transition failures are not converted into permission
to save.

### Live metadata outside ScenarioResource (preview 3)

Native `CanSave` reason4 is an empty name: it compares `data+4C` and `data+50`.
These are the begin/end of the native UTF-16 name, with description at `+5C`
and tags at `+6C`. Genre is `data+7C`, compared/written at `F3B670/F3B675`.
The native name setter starts at `F3CC00` with its `INameableEntity` subobject
in ECX (`data+4`), computes `this+48` at `F3CC0C`, and calls `423690` at
`F3CC39`. It marks `this+81`, equivalently `data+85`, at `F3CC73`.
Thus `+85` is metadata changed, not a generic imported flag. Existing local
rollback field names retain their encoding and are not sent over the network.

`AssignMetadataText=423690` is thiscall `(string16*,begin,end)->string16*`,
`ret8` at `423733`. It owns allocation/replacement; never copy native string
ownership pointers. Preview 3 captures three bounded owned UTF-16 strings and
genre separately from scenario bytes, carries them in mapping version2, and
still reads mapping version1 checkpoints. GA/captain bundle formats are unchanged.
Applying a snapshot installs metadata before native history restoration refreshes
the editor. This does not enable native metadata authoring while sharing.

C4's actual guest copy failed with reason4 because an unsaved host name was not
in the transferred scenario bytes. It was not an unchanged-document rejection.
The failed export restored the shared draft and released its hold. Read-only
receipts are `out/editor-native-flow/metadata-query.txt` and
`metadata-fields-query.txt`; the save verifier checks the assign ABI and fields.

An explicit copy of an otherwise unchanged valid draft sets the verified
metadata-publication flag only for native Save As, then repeats `CanSave`.
Other eligibility failures remain rejected. Cancel/start failure restores that
flag; guest export restores its full captured dirty state after completion.
Ordinary replacement Save retains its existing native unchanged gate.

`ED4B80` takes one stack boolean and returns `ret4`; native callers also pass the
UI base in ECX, although the body does not use ECX. It manages input-capture window
`02CCDA9C`. Release=true is used only to release this module's still-owned pending
mode2 operation after failure. UI work and disk readback run outside Present.

## Actual preview and return

Native top Play first reads `data+8C` through `FF3DB0`, then calls
`F412C0(data,captain,0,0,0)->bool`, `ret10`. This validation is preserved.
`ED7E78..ED7E7D` then calls `ED6620(UIbase,1)`, `ret4`. Its mode1 branch
`ED67D9..ED67F8` calls `EF12D0(ScenarioMode,1)` and updates the normal UI mode.

`EF12D0` is thiscall `(mode,int unused)->void`, `ret4`. For submode `+CC=1`,
it calls editor cleanup `EF0BA0`, changes `+CC` to2, calls the native data mode
transition `F45E70(data,2,0,0)`, and initializes play through `EF0E00`.
This is an editor preview, distinct from Sporepedia quickplay request `(2,1)`.
The return context `mode+D0` remains0.

Native Build/stop sequence `F0EB09..F0EB25` is:

1. `F18F30(play)` performs ordinary native play/result cleanup.
2. `ED47A0(UIbase,0)`, `ret4`, sets `UIbase+20` to the return display mode.
3. `EF1280(mode)`, plain return, stops native play, changes submode2 to1,
   transitions data to editor mode1 and calls `EF0990(mode,false)`.

The results Done path separately checks `mode+D0` at `F1C9C6`: context0 calls
`EF1280` at `F1CA0D`, context1 returns to Galaxy, and context2 returns to Space.
Calling quickplay menu cleanup is not an editor return.
The collaboration still needs its own immutable revision, party readiness,
gameplay isolation and draft/history restoration around these native operations.
The existence of a native preview does not itself make preview cooperative.

### Shared preview ownership and return

The adapter installs a project, epoch, round and revision owner only after the
editor controller confirms the party. It captures an owned native serialization
outside Present and pins the local ScenarioMode, data and resource identities.
The logical project adventure key may differ from a guest's local saved resource
key; native loading readback checks the pinned local key and resource, while GA
messages retain the common logical key. Content collection uses the pinned
unsaved serialization through the ordinary dependency collector, rather than
reopening the last saved adventure.

The existing GA coordinator owns captain, content and loaded acknowledgements,
objectives, damage and terminal-state synchronization. The current authored avatar
is a locked captain for this playtest, so no personal captain wizard is opened.
Its extra GA Ready flag is set only for the still-current confirmed editor round,
after content and captain validation. Replaced owners and delayed callbacks cannot
ready a later round.

During owned preview the `EF1280` hook suppresses a local native Done/hammer
transition and requests the host's group return once. An explicit authorized
`playtest(false)` calls the original trampoline in the native stop order above.
GA results Done and Replay also request that shared editor return; they do not
start ordinary quickplay Replay. Preparation and loaded cleanup reset only the
owned GA state and notify the editor controller. They never switch application
state or game mode to Galaxy. Ownership is explicitly cleared after mandatory GA
cleanup and the authorized return, so a delayed cleanup cannot leave a later
editor session through the quickplay menu path. The editor runtime restores the
committed draft and its checked semantic history separately from gameplay.

### Preview results and captain progression

`F19520` is the native progression-eligibility predicate. It returns true only
when the data flag `data+14` is clear and `ScenarioMode+D0` is nonzero
(`F19528..F1953F`). Thus editor context0 is explicitly ineligible. Results setup
`F18540` calls this predicate at `F186C1`; false jumps over the captain-property
load at `results+B8` to `F18818`. The normal newly created/cleaned results object
therefore retains a null captain-property pointer. The ordinary captain-save
helper `F16460` checks that pointer at `F16478..F16481` and exits without writing
captain progression when it is null.

The reward getter `F19080(play)` independently returns zero unless native play is
successful and `mode+D0` is nonzero (`F19089..F190A0`). Ordinary editor preview
therefore has zero XP and no captain-save target. The native results cleanup
`F17750` releases `results+B8` before publishing the exit choice, preventing a
previous quickplay panel's target from surviving its normal close.

The co-op playtest bridge must retain context0 throughout entry, success/failure
and return, and must skip the quickplay forced captain-save acceptance adapter.
It must still allow native terminal presentation and coordinate party return.
This static evidence does not replace a controlled runtime check that captain
properties, XP and saved creation records remain unchanged after group playtest.

## Resource package lifetime boundary

The existing generic package-registration recipe is recorded in
[creations](creations.md#3-registering-an-extra-package-at-runtime-).
For dc04, `RegisterDatabase=8E1000` is thiscall with three arguments and `ret0C`.
Removal unlinks the priority-list entry, calls `Attach(false,RM,true)`, can call
database Shutdown when only its temporary reference remains, and releases that
reference (`8E107E..8E10D2`). Dropping the caller reference afterward can therefore
destroy the package. It does not prove that every live record has stopped using it.

The dc04 packed-file primary vtable is `1436938`. OpenRecord is `8DA6F0`; its
read-record factory at slot `+54` is `8D8600`, which allocates `70` bytes and calls
`8DD0D0(key,header,db)`, `ret0C`. That constructor stores the raw database at
record `+18` without retaining it (`8DD101..8DD108` through return `8DD1A6`).
The final record vtable is `1436A88`. Its AddRef `8DD4C0` only atomically increments
record `+4`; Release `8DD4D0` only decrements that record count and deletes on zero.
The record destructor `8DD080` dereferences the raw database and calls its
CloseRecord method at `8DD096..8DD09F`. A retained record therefore cannot be
assumed to retain the database, even if only its later destruction needs the DB.
Packed-file GetOpenCount `8D9880` immediately returns zero for read-only access,
so it cannot establish that all read records have gone away.

Consequently an editor restore or return alone is insufficient evidence to evict
old packages from the shared resource manager. Keep explicit dependency ownership
and avoid registering redundant revised root packages. Arbitrary live LRU removal
or a global resource-cache flush is not authorized by these receipts. Native
package retirement still needs a proven complete lifetime boundary.

Owned editor previews now choose cache-only GA content preparation. The run/root
policy is resolved at preparation and checked again before outside-frame apply;
it requires the same pinned native editor draft, authored avatar and confirmed
project/epoch/round/revision. The initial editor baseline owns the unchanged
dependency closure, since this editing scope cannot change classes or creation
assets. Revised preview records still pass the existing manifest/hash receiver,
are written to the content cache and produce the ordinary content ACK. They do
not allocate or register a new native database. Revoked owners or a policy change
fail preparation instead of falling back to mounting. Ordinary GA preparation
keeps the existing mounted-bundle behavior. The native assets fixture covers both
paths, corrupt received bytes, cancelled/stale preparation, and 20 changed
preview snapshots without an additional mounted package. This avoids per-round
database growth; it does not claim complete cross-project native cache eviction.

## Validation status

The native editor's data byte `+124` is not a general editor-loading completion
flag. `F3D740` checks it, calls `F3CDB0(data,false)` and clears it at `F3D753`.
`F3CDB0` constructs an image-request message (vtable `1462CD0`), selects a
128/256 texture, updates the preview-image references and queues that message.
It clears the owner's `+124` and `+2C` at `F3D067..F3D06E`. Native BeginSave also
calls that image path. A ready editor was observed with this byte clear, so editor
readiness must be established from its actual mode, resource and asynchronous
loading state, without requiring this image-request byte.
In the hidden isolated native Adventure Town test, the ordinary Play triangle
entered Test Mode with `+CC=2`, `+D0=0`, a valid authored Blue Soldier and `+124=0`
after 160 frames. The ordinary Build hammer returned to `+CC=1` with the same
owner/data and this byte still zero after 100 frames. Editor preview loading
therefore checks the pinned identity, completed initialization, avatar, context0,
play submode and inactive loading screen. The existing released quickplay loaded
predicate is retained separately.

The private four-player C7 integration test accepted all four explicit editor
Ready confirmations and entered native Test Mode. Native play state `+90` was
three and its clock remained zero while the shared GA intro hold waited for
beam-down confirmations. Editor Test Mode had no native Continue window
`0445C99D`, so the ordinary quickplay beam-down confirmation could never occur.
The ordinary group return completed and restored the editors. This is a failed
editor-intro integration gate, not an accepted coordinated playtest.

The preview bridge now queues one intro confirmation outside the frame only for
the exact current owned editor round after launch/loading, an active shared run
and its Intro scene, native playing state three, no pending intro task and no
active cinematic. The callback rechecks native owner generation and shared run
before invoking the existing intro confirmation. Its authority comes from the
already completed explicit editor Ready barrier; it does not add implicit lobby
readiness or change ordinary Sporepedia quickplay. The expanded bridge fixture
passes 74 checks. In the private C8 four-player retest, all four explicit Ready
confirmations completed and native play clocks advanced without a Continue window
or fixture confirmation. All 12 remote puppets were present and group return
restored all four editors at revision six with matching semantic hashes and no
reported faults. The receipt is
`out/editor-native-party-c8/preview-diagnostic/result.json`. This confirms that
specific entry/return flow; it does not satisfy the separate long-run, performance
or complete editor acceptance gates.

Static assembly/decompiler receipts were collected in ignored
`out/editor-save-re/query10.txt` through `query16.txt`; exact-image assembly used
the same SHA-256 above. The production save module cross-compiles under the exact
20261006 i686 CI toolchain with warnings treated as errors. Symbol lint passes.
`verify_editor_save_dc04.py` checks the exact executable hash, 13 symbol entries,
16 native call edges and 24 field/branch receipts. The native Save/preview fixture
passes 296 checks, including real i686 thiscall boundaries, normal save readback,
owned return suppression and explicit group-return ordering. The native preview
bridge fixture passes 74 checks for local versus logical identity, stale round
callbacks, launch and return ownership, and both identities' score guards.
The native content fixture passes 253 checks for registration policy, unchanged
ordinary mounting, real guest transfer acknowledgements and bounded preview
package turnover.
The post-C8 source runtime orchestration fixture passes 2,153 checks (1,678 in
its quick subset) using the real UDP sessions,
editor controllers, project codecs and durable stores, with native/game APIs
isolated in memory. It covers a queued Begin plus GA preparation batch, saved
resources followed immediately by another commit, preservation of opaque saved
bytes and rotated keys after durable reopen, mandatory return cleanup and delayed
editor readiness, and guest-origin recovery requiring one completed native Save
As copy before hosting. It also covers retained native bases and dependencies,
bounded recovery artifacts, delayed Undo ownership, asynchronous history
retirement, stale-generation protection, and host close waiting for both canonical
coverage and explicit same-revision native Save acknowledgements. It also checks
completed Save data differing from the live resource, retention through later
edits and recovery, unsupported opaque changes before no-op completion, Save,
playtest and canonical apply, and one actual-frame delay before native recreation.
The frame fixture preserves another placement's concurrent accepted move and
invalidates pending requests on close, recovery, disconnect or context change.
Ownership remains isolated while the controller is idle but native close is
pending. These fixtures isolate native/game APIs and do not substitute for game
integration. Native Save As/reopen and the
ordinary native preview entry/return have been observed in private profiles.
Failure/cancellation, coordinated preview return, XP behavior and complete
two-/four-player integration remain separate runtime gates until recorded in the
validation report.


### Validate native application before durable acceptance

Native mutation helpers do not all return a success result. In particular, the
adapter invokes native placement deletion through a void call. A successful
adapter return alone therefore cannot establish that the live authored resource
matches the candidate operation. Previously, the runtime `persist` callback captured the
resource and immediately replaced its semantic document with the candidate, so a
refused or partially applied native change could evade the final consistency
check before persistence.

The runtime now calls the existing verified `editor_native::validate` on that
actual captured snapshot and the candidate document before overwriting semantic
metadata, staging resource/CAS data or writing the durable commit. Validation
parses the owned serialized bytes and checks placement identities, roster,
classes, acts and supported pose fields. No additional native entry point is
introduced. A mismatch before any durable write returns an ordinary transaction
failure: the coordinator restores the prior native state and rejects the host
operation. It does not mark healthy storage as uncertain. A replica failing to
apply an already durable host decision restores its own prior snapshot and
freezes locally; it cannot undo the host's accepted revision.

The focused fake-native regressions deliberately return success after refusing a
Delete or applying a wrong Move pose. Both host cases leave host and guest at
revision zero, preserve their exact durable snapshots and healthy stores, and
perform one native rollback. A guest-only refused Delete instead leaves the host
durable at revision one and freezes the guest at its prior healthy checkpoint.
These tests exercise the real coordinator, UDP sessions and stores with isolated
native/game APIs. They do not establish that a serialized-correct actor is
visible or selectable in the native scene.

The exact 20261006 i686 CI toolchain built
`out/editor/native-after-projection/host/editor_runtime_windows_tests.exe`.
Its full run passed 2,153 checks and its `--quick` run passed 1,678. The full run
also retained the 24 MiB dependency closure across 20 structural edits and Save:
initial envelope 25,166,419 bytes, CAS 25,203,841 bytes, host traffic 64,534 bytes
and guest traffic 3,879 bytes. Commands, source/fixture/executable SHA-256 values
and results are recorded in
`out/editor/native-after-projection/validation.json`.

The running private C8 binary `3c61...` predates this guard and was built against
the 1,949-check runtime baseline. The 2,153/1,678 results cover the updated source
and isolated fixture executable only. Native deployment and integration of this
guard require the next candidate (C9 or later); no running game was changed by
this validation.

## Editor frame measurement contract

`coop.frame_metrics config` is a read-only outside-frame query. It follows the
same named `App::FrameLimit_ptr`, application `+3C`, configuration `+B0` chain
already used by `sporekit/focus.cpp`, checking readability at each hop. It reports
the actual cap without changing it and obtains current backbuffer dimensions
from the existing host API. No new native function is called. The original
performance helper tried bare `frame.limit`, but that framework command requires
a setter argument and fails as a query. Its original numeric-key expression
also omitted digits and silently lost `p95_ms`; both prevent valid comparisons.
The C8 owner queried this command in private instances 87 and 88. Both returned
`frame_limit_ms=16 screen_width=1024 screen_height=768`, recorded in
`out/editor-native-candidate8-frame-config.json`. This verifies the read-only
query in the game; a capped two-process query is not a large-editor FPS result.

The revised helper requires explicit execution, matching candidate hash and
ownership of the exact hidden isolated processes. It records actual CPU/GPU and
driver identity, all Spore process IDs, loaded-module/configuration text, viewport,
window, background mode, authored bytes and projected native landmarks. Independent
samples require every editor to leave the party. Shared samples cover the entire
connected party. Peer recording starts are synchronized and must overlap for at
least 90 percent of the requested duration. Configuration, projection, authority
and authored state must remain unchanged. Failure stops an owned active recording.

`FrameSamples` observes `SK_EV_FRAME`, which can also come from the framework's
game-loop fallback. Before/after Present status alone cannot exclude a temporary
fallback. Samples therefore bracket the interval with the existing bounded log,
require an unbroken pre-sample log anchor, and reject any frame-source transition.
The cold-start private configuration must retain debug/info logging. Log rollover
without the anchor makes the interval unverifiable rather than a passing sample.

A large-editor comparison requires the same process roster/PIDs, candidate,
hardware, native resource/camera and render configuration, at least 400 actual
placements per editor, two or four peers, and an uncapped render loop. The helper
never removes a cap itself. A capped or smaller comparison needs explicit
diagnostic mode and cannot claim this gate. The measured limits remain median
FPS ratio at least 0.90 and p95 frame-time ratio at most 1.15. Two process-memory
snapshots are diagnostic and do not establish the separate 20-cycle memory gate.

Large-editor sampling now also requires every live Spore PID to belong to the
measured two-/four-process cohort. Merely matching the total PID list before and
after cannot exclude another game's changing workload. Other Spore processes
require explicit `--diagnostic` sampling; diagnostic recordings cannot later be
promoted to acceptance by omitting the comparison flag. The helper never closes
personal or unrelated games to meet this condition. Its 18 offline tests pass on
Windows Python and WSL, including extra/missing/duplicate PID cases and diagnostic
promotion rejection.

Prepare a large fixture through ordinary native authoring while disconnected:
choose an existing lightweight template, place it by palette drag onto verified
visible terrain positions, and check an exact increment of one after each
completed gesture. Build a distributed grid rather than hiding a stack of
overlapping objects. Stop on native complexity or placement refusal; do not
bypass the editor or manufacture marker bytes. Preserve the authored test captain.
Then complete native Save As and share the fixture once. Allow every peer to load
and settle before collecting the shared sample. End sharing and leave the party
while keeping the same native editor/resource/camera open, then collect its
independent counterpart. Starting another ordinary Save As between the paired
samples would rotate identity and invalidate the strict comparison. Record the
sample order and repeat intervals to expose timing variability.

The helper's offline regression tests cover invalid metrics and mismatched
processes, capped/small diagnostics, camera checks, recording cleanup and temporary
fallback/log rollover. They do not create a native 400-placement fixture or prove
an FPS pass. Actual large-editor measurements remain an outstanding native gate.


## Transient presence and cache-aware joining

The post-C8 editor transport adds existing-placement transform hints. They are
separate from accepted operations: neither receiving a hint nor expiring it calls
native application, mutates the document/history, or writes a durable record.
The controller retains at most one pending hint per author and publishes the
latest one through the existing unreliable Session channel at most every 50 ms.
Hints expire after 1,000 ms without an accepted refresh. New placements remain
local previews until the host allocates their committed stable identity.

Each hint is scoped by project, epoch, canonical revision, author capability and
monotonic serial. The host checks a committed placement and its act, finite pose,
unit quaternion, positive scale and the exact current placement/document lease.
It also requires matching selection, active editing and no pending commit.
Guests accept only host-relayed hints matching the latest host-derived selection
ownership and exact lease token. The token is an identity fence, not independent
authority: request ownership still depends on the authenticated Session peer.
This prevents an unseen delayed relay from resurrecting a ghost after a no-edit
playtest return or a release/regrant at the same document revision. Commit,
release, expiry, disconnect and control transitions clear hints; a same-serial
clear wins over an active hint already queued for that author.

Presence now includes reported pending work, host-derived selection/document
ownership and the host's participant phase. A guest cannot assert ownership or a
host phase. The host publishes current selection lease identity so stale hints
cannot survive replaced ownership. Normal guest authoring Leave is distinct from
failure: it cancels an active preparation/playtest, keeps the party connected and
creates a fresh editing offer after return. Remaining editors can continue and
the guest can explicitly rejoin with a new capability. Failed replicas still
freeze for explicit recovery.

Cache-aware Join carries a bounded typed inventory: an optional immutable base
SHA/size and up to 2,048 ordered unique resource-key/size/SHA claims. Claims obey
the existing 16 MiB record and 128 MiB bundle limits. An inventory larger than the
1,100-byte editor packet is transferred through guest-only `TransferKind::Join`
with a 256 KiB cap, chunk verification and the existing bounded reliable transfer
scheduler. It does not place the entire inventory in the priority queue.
Only an authenticated offered participant can complete that transfer. The host
then passes its immutable current full resource payload to the verified cache
codec and pins the resulting peer envelope with the chosen baseline revision.
The peer-specific envelope never replaces the host's full late-join cache.
Empty inventories retain the full verified transfer path. Duplicate completed
inventory seals remain recognized after baseline scheduling. Cache byte
verification and missing-resource reconstruction are defined in `editor_project`;
no adventure/captain resource bundle format changes.

The final focused tests use real two-/four-peer UDP Sessions with isolated native
adapters. They cover 1,000 coalesced samples, unchanged canonical/native/durable
state, stale/forged capabilities and revisions, bad lease identities, release,
TTL, pending commits, controls, disconnect and a delayed relay first processed
after return/regrant. Join tests transfer all 2,048 claims while another editor
commits first, verify the latest pinned full payload, preserve full fallback on a
later join, reject preparation/negotiation failures, and ignore duplicate seals.
The final Linux ASan/UBSan/leak run passed 14,099 Session checks; the exact
20261006 i686 Win32 run passed 8,844. Protocol tests passed 5,845 on both. Session
counts include scheduler-dependent polling assertions. Executables and sanitizer
binaries are under `out/editor/transient-presence/host`; exact source/executable
hashes and results are in `out/editor/transient-presence/validation.json`.
These source/fixture results do not establish actual native hint readability,
cache-hit joins or guest cancellation UI in the game. They require the next
candidate's controlled integration tests; the running C8 binary is unchanged.


## Recheck native state at history entry

The native history availability hooks use cached collaboration state to keep UI
queries cheap. The cache can predate a newly started native gesture, save or
transition, and the public history command reaches the same action queue. The
runtime history entry therefore rechecks actual native editing/readiness,
gesture state and owner generation, plus recovery, save-busy and pending native
Save publication, before acquiring a lease or assigning an operation sequence.
A held Undo/Redo does not become another pending transaction that could collide
with capture of the still-active native gesture. The chronological checked-inverse
policy and retention rules are unchanged.

The focused Runtime fixture first creates through the actual captured-gesture
and real-frame submission path. With an existing document lease, each native hold
must leave the operation sequence, pending state, semantic history, native bytes
and durable checkpoint unchanged. Once the holds clear, ordinary own Undo and
Redo both commit and converge. This differs from seeding directly through the
Controller: that test-only path has its own auto-assigned operation counter and
must not be mistaken for a Runtime gesture's explicit sequence. Two unrelated
existing fixture calls also now select the host's fake dispatch context explicitly
after polling the guest.

The exact 20261006 i686 build
`out/editor-undo-entry-build/host/editor_runtime_windows_tests.exe --quick`
passed 1,902 checks, including the independent timing fixture's one local sample
and zero remote samples. The performance helper's 18 offline tests also pass on
Windows and WSL. Exact source/executable hashes and commands are recorded in
`out/editor-undo-entry-build/validation.json`. These changes and the UTF-8-safe
cached hint-name prefix were tested in source/isolated fixtures only; the already
running C9 candidate remains unchanged and needs a later deployment.

## Scope delayed playtest returns to their original round

Native preview result/return callbacks now capture the collaboration project,
epoch, playtest round and canonical revision. A single bounded pending return
slot validates that identity both when queued and when dispatched outside the
frame. It requires the current owned preview in Preparing, Playing or Returning.
An old callback cannot return a later round, and public or native Return while
the editor is already Ready leaves its status unchanged. A current host callback
still drives the existing authoritative return and mandatory GA cleanup; a guest
callback during play still explains host ownership. No native ABI or protocol
changes are involved.

The isolated Runtime test retains callbacks across a complete return and a new
preparation/playing round, delays an old scoped queue item until that new round,
and verifies no new GA cancellation or return. It also verifies a valid current
native return completes once on both peers. The read-only `coop.editor leases`
diagnostic reports cached local scopes, tokens and remaining milliseconds, plus
member phases, selections, host-derived ownership, pending state and preview
lease identity. Output is bounded to 12 KiB and does not capture, restore or mutate
native resources.

The exact 20261006 i686 isolated Runtime executable
`out/editor-return-scope-build/host/editor_runtime_windows_tests.exe --quick`
passed 2,060 checks, including the earlier history-entry and timing regressions.
Hashes and commands are recorded in
`out/editor-return-scope-build/validation.json`. These source/fixture checks do
not change the running C9 candidate or establish actual native C10 acceptance.

## Final isolated candidate 10 regression sweep

The source snapshot at `0afb659` (including production integration `071f6a4`)
passed the complete portable `make test` and a fresh exact-20261006 i686
`test-editor-windows` build with at most two compiler jobs. All 13 Windows
fixtures then passed, including the full Runtime executable rather than its
quick subset. All 244 recorded production, test, helper and symbol inputs had
identical hashes before and after the sweep.

| Fixture | Portable checks | Win32 checks |
| --- | ---: | ---: |
| Frame metrics | 15 | 15 |
| Confirmation metrics | 8 | 8 |
| Native serialized bindings | 8,224 | 8,224 |
| Binary delta | 6,410 | 6,410 |
| Semantic core | 2,753 | 2,753 |
| Durable store | 2,483 | 2,473 |
| Protocol | 5,845 | 5,845 |
| Project and cached Join envelopes | 4,684 | 4,684 |
| Controller with real UDP and fake native APIs | 14,065 | 8,837 |
| Native save contract | Not run here | 296 |
| Owned editor preview contract | Not run here | 74 |
| Native asset registration policy | Not run here | 253 |
| Full Runtime orchestration | Not run here | 3,017 |

Controller counts include scheduler-dependent polling assertions. Portable GA
rules, coordinator, flow, pause, music, asset, actor-motion and control-policy
regressions also passed. Offline Python checks passed 6 UI-find, 18 performance
helper and 4 network-proxy tests. Symbol lint reported 344 used symbols, 2,918
known and zero problems; missing symbols for the frozen unsupported 2016 build
remain informational. The optional real EditorSaves package read in `net_tests`
was unavailable in this environment.

The full Runtime case retains a synthetic 24 MiB dependency closure through 20
structural edits, Save, recovery and a verified cached Join. It recorded initial
CAS usage 25,166,419 bytes and final usage 25,203,841 bytes. Its cached Join sent
29,397 host UDP bytes and 751 guest UDP bytes. These are isolated fixture values,
not measurements of a native adventure, bulk-content contention or game FPS.

All 17 archived GA Windows fixtures were re-executed successfully. Their binary
hashes match `out/ga-regression-windows-final-tests.json`, and declared Make
prerequisites required no rebuild. That older archive has no compiled-source
manifest; these are identified as archived-binary reruns rather than new builds.

Exact commands, output, source hashes and executable hashes are in
`out/editor-final-c10/validation.json`, with separate portable/build logs,
Windows results, archived-GA results and source-before/source-after manifests.
The full Runtime executable SHA-256 is
`db12769d02842a52c6fb9b822d8ed535bc466e9a4cfa978f57f3d5717ef1a3c7`.
No mod DLL was built or deployed by this sweep, and no game session or installed
file was changed. Adapter fixtures substitute native engine APIs; these results
do not establish broader native editor acceptance, actual network latency or
the large-editor FPS and sustained-memory gates.

### Native lifecycle checksum calibration, candidate 12

The first native Build-hammer lifecycle run on 2026-10-26 used the exact private
C12 DLL `0a4a8a96a70a7da46d32d4253a4045b7b09e518966fb47be158b168335aff065`,
instances 87/88 and owned PIDs 51668/55888. The historical failed result is retained
at `out/editor-native-lifecycle-c12/native-return/result.json`; its failure was an
overly strict harness requirement that raw native serialized bytes remain equal.

Both peers completed the real native hammer return. Their authored semantic
checksums, canonical `CONVERGED` check, history, resource key, selected placement
and screen projections were unchanged. The host projection remained
`(492.009369,529.599304)` and the guest projection remained
`(504.678436,437.325928)`. Before Test, both raw snapshots had SHA-256
`40ebf4406022edd2fa479aeda2fa3e2ba111184c7cc888192a9b417f3369fb6b`
despite their different local views. After return the raw checksums were
`38625e9add89409d54852f568717e060fa45053dd431b39782f9c9454f104bae`
and `f3c12f307e9a1eb476b27a1b5773fc7c278776ab386847fb7f3b5c844bff8aa9`.
No byte-level attribution of those differences was established by this run.

The acceptance invariant is the existing verified native integrity path:
`validate(snapshot, canonicalDocument)` plus
`supported_between(current_native, snapshot)`. It checks authored content while
normalizing the documented local camera block at resource `+0x2C30`, length `0x20`,
and the verified derived placement flags `+0x224 & 0x16`; supported semantic poses
are separately checked against the canonical document. Native Test/return and
restoration can publish local camera/bookkeeping state into the resource body.
The resource raw SHA therefore remains diagnostic, rather than a requirement of
raw-byte identity or evidence that a difference is exclusively camera data.

The calibrated harness requires identical `CONVERGED` checks before/after, exact
authored semantics/history/source identity, exact local selection and less than
one pixel of local projection drift. The historical failed run is not relabeled
as an acceptance pass. Calibration evidence is recorded separately under
`out/editor-native-lifecycle-c12`; a fresh native run is required for a pass.

Three fresh two-player C12 runs subsequently passed, each starting from Galaxy
with a new native Save As and one genuine shared Move retained as revision 1,
durable revision 1 and one canonical history entry:

| Actual native stage | Recorded result | Observed boundary |
| --- | --- | --- |
| Native Build-hammer return | `out/editor-native-lifecycle-c12/native-return-calibrated-r2/result.json` | Both captains entered native Test; ordinary hammer input returned both peers with their own selection, camera projections and verified authored draft unchanged. |
| Guest disconnect during preparation | `out/editor-native-lifecycle-c12/disconnect-preparing/result.json` | One host Ready confirmation preceded guest Leave. Host authoring resumed; guest retained the detached native draft and explicitly rejoined with capability `1:8` replaced by `1:9`. |
| Guest disconnect during Test | `out/editor-native-lifecycle-c12/disconnect-playing/result.json` | Both entered native Test before guest Leave. Host returned to the draft; explicit guest rejoin replaced capability `1:11` with `1:12`, preserving the verified draft and history on both peers. |

Every stage completed normal editor exit and party Leave without forced dialogs
or native faults. The final read-only handoff at
`out/editor-native-lifecycle-c12/handoff/result.json` recorded both original owned
PIDs at Galaxy, editor phase 0, offline and `total faults: 0`; the runner and game
processes remained alive for their owner. The first calibrated run's inherited
`four_native_baselines` gate name is historical helper wording for its two
participants, not four-player evidence. The helper now labels two/four according
to the actual roster; historical result files remain unchanged.

An intermediate calibrated attempt failed before Test because the seed drag hit
the open co-op panel and produced no canonical operation. Its failed result is
preserved at `out/editor-native-lifecycle-c12/native-return-calibrated/result.json`.
The successful retry explicitly closed that panel before ordinary placement
input. These native runs do not establish direct host preparation Cancel,
independent imported-guest recovery, packet adversity, large-editor FPS or
sustained memory acceptance. Direct Cancel requires the later C13 candidate.
The exact artifact hashes and final harness checks are recorded in
`out/editor-native-lifecycle-c12/validation.json`.

### Native preparation Cancel and imported recovery, candidate 13

Two additional native two-player stages passed on 2026-10-26 with exact C13 DLL
`db87a588ab30c0be25912c9ad61f125b403df77d1c1873915edea0353c632678`,
instances 87/88 and owned PIDs 43136/39344. Each stage began at Galaxy, used a
fresh ordinary native Save As, then retained one genuine shared Move as canonical
and durable revision 1 with one history entry.

The preparation stage exercised the visible **Cancel playtest** button with zero
Ready confirmations. Its 1024x768 screenshot shows the caption and normal native
dialog styling; ordinary caption input executed the cancellation. A second round
used the public Cancel action after one explicit host Ready. Both returned to the
same draft with identical verified authored checks, source, semantic state,
history, selection and camera projections. An immediate retry then required both
explicit Ready actions, advanced natural native Test clocks, and returned through
the actual Build hammer with the same local context. No synthetic result or
beam acknowledgement was used. Evidence:
`out/editor-native-lifecycle-c13/cancel-preparation/result.json` and
`87-zero-ready-cancel-button.png` in that directory.

The imported recovery stage detached the shared draft, made its former guest the
new party host, and requested Recover. The ordinary native successful-save
dialog appeared while the editor was still non-authoritative. Only after its
normal confirmation did the recovered host become Ready. The saved resource
changed from `408A0000!2909430D.366A930D` to
`408A0000!29094310.366A930D`; project identity, revision 1, history and authored
placements were preserved. The former host explicitly joined that recovered
draft, and both peers reported the same verified authored check for the new
resource identity. Evidence:
`out/editor-native-lifecycle-c13/imported-recovery/result.json`, including
`88-mandatory-imported-recovery-native-save.png`.

Both stages completed normal exit and party Leave with no forced dialogs or
native faults. The final read-only handoff recorded both original owned PIDs at
Galaxy, editor phase 0 and offline, with `total faults: 0`. Runner 38436 and the
game processes were retained for their owner. Exact result, screenshot, source
and handoff hashes are in `out/editor-native-lifecycle-c13/validation.json`.
These are two-player, same-process native lifecycle observations, not evidence
of cold-process recovery, four-player cancellation, large-editor FPS, sustained
memory or release readiness.

### Preview 4 native authoring audit (2026-10-28, in progress)

This work uses only GOG 2024 `dc04aee5`. Protocol 19 rejects older peers before
authoring. The changes are on `fix/ga-editor-preview4`, based on the native-flow
preview. Native verification is still in progress; the portable fixtures do not
establish that a particular native control or manipulator works in a game.

The exact executable establishes these boundaries:

| Entry | Verified signature and use |
| --- | --- |
| `F03770` | `thiscall(toolManager, action, word, word)`, `ret 0C`. Action 1 begins dragging; action 0 finalizes it. |
| `F02B30` | `thiscall(toolManager)`, plain return. Dispatches ordinary and auxiliary movement. |
| `F01B00` | Initializes native grab anchors at context `64/68/78/7C/88/A0`, then invokes a pose step. |
| `F01490` | Ground pose step, no incoming register or stack arguments, plain returns `F017E7/F01AFA`. |
| `EFDAF0` | Vertical pose step, no incoming register or stack arguments, plain return `EFDD75`. Writes marker position and elevation `220`. |
| `F01090` | Radial pose step, no incoming register or stack arguments, plain return `F0148F`. |
| `F02040` | `thiscall(toolManager)`, plain return. Uses incoming ECX via EBP, so it is not interchangeable with the static ordinary pose steps. |
| `DFEE50` | `thiscall(classData, const classData*)`, `ret 4`. Deep copy, including acts, dialog strings, behaviors and graphics references. |
| `DFF8B0` | `thiscall(classMap, const classMap*)`, `ret 4`. Owns the copied `27E8` rows. A borrowed three-pointer descriptor can describe a one-row source slice during this call. |
| `ED36D0` | `thiscall(classMap, const uint32 index*)`, `ret 4`. Sorted insertion returns class data. `F46357` supplies resource `2BF4` and an index address. |
| `F462F0` | `thiscall(scenarioData, const ResourceKey*)`, `ret 4`. Allocates the current resource `2C0C` class index, then increments the counter. Palette callers already open native history. |

Teleporter detection follows native predicate `F25260`: class data's first DWORD
equals `90C48D09`. This is the asset instance key, not a general object-type enum.
The marker's entrance remains position `04`, quaternion `10`, scale `20`, and
continuous scale companion `24`. The destination is position `1E8`, quaternion
`1F4`, scale `204`; elevation is `220`. Native `F02040` uses context `14` for
secondary orientation, `28` for destination translation, and checks manipulator
type `076C67DF`. Quaternion branches use `07A81824` and `07ABDD8D`. Unknown path
and dimension manipulators remain outside the allowed set.

`F42660` derives goal validation bit 0 at goal `14` and diagnostic code `184`.
These are separate from authored goal definitions. Class controls can trigger
this validation, so native acceptance must check this companion behavior.

Class rows contain data at `row+08`, size `27E0`. Act records are an owned fixed
vector at class `70`, stride `4E0`. Copy routines `DFEC20/DFCCB0` agree with the
[upstream class/act layout](https://github.com/Spore-Community/Spore-ModAPI/blob/master/Spore%20ModAPI/Spore/Simulator/cScenarioClassAct.h).
Dialog vectors are at act `04/170/2DC`, with `44` byte elements. Standard behavior
class targets are at `4BC/4C0/4C4`; custom entries are at `4CC`, stride `20`.
`EC8510/EC81B0` enumerate `(2, classIndex)` targets directly from resource `2BF4`.
`EC8510` also supplies type 0 (Any), type 8 (Player), and type 1 (team).
`EC8110` supplies type 3 with emote values. Only type 2 contributes a class
reference. All three target pairs in each custom entry and all standard targets
are projected across every owned act, with bounded vectors and duplicate removal.
Goals also protect their target classes when no instances exist. Undo of an
import must reject if another class's behavior or an objective still names it.

The input continuation prepares only native history and grab anchors at the
original press. It holds the frame dispatcher and all four authored pose steps
until placement ownership arrives. The retained gesture includes release,
modifiers, object revision, project, epoch, participant generation, native
generation and auxiliary identity. Native model fixtures cover quick release,
denial, focus loss, selection replacement and stale scope retirement. This is
not yet a two-player direct-drag acceptance result.

Native picker setup must explicitly select its stock or personal filter. Fresh
guest onboarding left `Recent Downloads` selected, with zero matching stock
cards. Waiting for `Ace` in that filter produced a harness timeout, not an import
transaction. The focused fixture now selects `Maxis` before choosing the stock
card and retains each participant's UI screenshots and history.

Native C7 established guest stock import into an empty cast, canonical allocation
at index 1024, distribution, checked class Undo/Redo, and a health change from
100 to 450 with zero placed instances. The native inspector stayed open after
the committed update. Class-fragment capture originally failed byte comparison:
`F2E670` leaves four screenshot-type DWORDs at `68..74` uninitialized. Zero those
wrapper fields and the local camera on the private fragment before serializing,
then normalize through native Read/Write. This does not alter the live class.
Evidence: `out/editor-preview4/cast-c7` and `native-ui-explore`.

Advanced Behaviors opens by Ctrl-clicking a stance button. SporeKit 0.3.4 mouse
input omits MK_CONTROL from `wParam`, even with its Ctrl key down. The fixture
therefore posts ordinary mouse messages with modifier flags after verifying the
input HWND belongs to the exact hidden private PID. No native calls or memory
writes are used to exercise controls. Closing Advanced Behaviors commits its
owned rule collection as one native history gesture.

Both C7 participants exercised native Play separately and returned with native
Build. The observer remained in the paused editor, party gameplay stayed at run
0, and checks/history matched after return. Test Together then exposed the old
bridge's saved-key requirement on a new draft (native source zero). Native solo
Test accepts that draft, and the group already owns a nonzero canonical transfer
root, so the bridge now pins the local zero identity alongside its exact native
mode/data/resource owner. C9 passed both native solo roles, explicit guest group
request/host approval/readiness, reciprocal live moving captains, and group Build
return with unchanged checks and history. Evidence: `out/editor-preview4/playtests-c9`.

Group place identity must come from the canonical run, not each local Save As
key: `coop.cpp` compares world identity before spawning remote captains. The
editor bridge supplies a run-scoped place only while its exact confirmed native
preview is playing. Saved-key differences, stale rounds, held preparation and
return are covered by the Win32 bridge fixture. C9 established reciprocal moving
native captains; shared objective/interaction and four-player checks remain.

The native Speech/Captions mutator is `F0A960`, `cdecl()->void` with no incoming
register arguments, plain return or a tail call to `F45920` CommitHistory.
`F0A984` calls StartHistory, and `F0A830` copies UI dialog strings into class-act
collections. The class `20` flag selects all acts versus the current act. Active
speech data is global `016C7D90`, with class data at `04`. `F0BA70` closes this UI
and clears the global. This is a separate setter boundary from `EE5380`.

C9 native text input reproduced one history commit per character and overflowed
the eight-entry capture queue before any deferred transaction could run. The
adapter now checks class ownership at `F0A960`, retains the first owned before
and latest owned after while that exact dialog remains open, then submits one
transaction after close. The retained native entry is scoped to project, epoch,
participant and native generation. The i686 fixture sends 128 such commits,
asserts one retained capture and no unowned mutation, then rejects a stale epoch.
C12 passed multi-character speech and captions for both roles. The first C12
fixture used a fixed popup Close coordinate and timed out while the real dialog
remained open. Native `CEFA1100` text-field bounds establish the correct close
and tab coordinates; the repaired fixture and a separate exact close confirmed
one canonical item and responsive matching editors. Evidence: `speech-c12`,
`properties-c12`, and `properties-c12-resume`.

`ECA1D0` is the separate Advanced Behaviors mutator, also `cdecl()->void` with
no incoming register arguments and native Start/Commit. Data-owner global
`016C757C` holds class data at `00`; `ECB520` initializes it. Both mutators honor
native all-acts versus current-act propagation and now gate that exact class
before calling the original routine.

`F00670` placement-from-cast starts history, creates/attaches the native object,
and sets tool cursor state. If its structure lease is pending, retain the class
identity/revision, scope, modifiers and release without calling it. Once granted,
run the original function, consume current cursor movement, and finalize the
same release. Model coverage includes quick release and stale/denied input. A
C12 passed this one-drag placement check, followed by a single existing-object
drag for both roles. Evidence: `import-c12` and `movement-c12`.

C12 stock teleporter import/placement by the host passed. Its actual general
ball at context `24`, type `07ABDD8D`, changed all quaternion components while
leaving its entrance position fixed, with one canonical item and matching peers.
Unlike the creature's speech icon, this is the visible sphere on a non-creature
rotation ring. Evidence: `ball-c12/ball-result.json`, `special-assets-c12`.

Sound audit: native Alarm Buzz import/placement by the guest passed. At the
Locate camera the draggable emitter origin is `(512,394)`, not the floating
speaker image above it. The large cone is the gain control: context `54`, type
`0771AD6A`; it remains blocked as an auxiliary outside pose movement. The actual
origin ground drag reproduced the original sound error. Host revision 33 was
accepted while the guest froze applying it after its initial canonical restore.
The normalized decoded resource differed only at byte 1580, `3F800000` becoming
`3F80000A` in the final marker attribute `078D5A93`.

Exact-image registration `138C30C..138C366` binds `078D5A93`, string `pitch` at
`1452728`, to marker `1E0`. The next registration binds `078D5A94` to gain `1E4`.
The first difference is a 10-ULP reconstruction of pitch 1.0 by native sound UI,
not a changed gain or distance collection. Both scalars now project, apply and
participate in history, and native validation permits at most 16 ULP only for
these two nonnegative finite sound values. Larger or other-field differences
still reject. Their original scalar values remain in the committed document;
an unrelated movement does not authorize a sound-tool edit. New recovery
hydration includes both fields. Further native verification is required.
Evidence: `sound-c12/decoded-field1580.json`, `owned-native-history.json`,
`committed32.native`, `sound-native-before-after.json`, and the private game logs.

### Preview 4 native follow-up: audio, populated class removal and picker callbacks

C15 cold recovery hydrated the two sound fields while retaining existing class
settings revisions and history. Replacing the revisions with zero during an
additive migration makes prior class Undo unsafe; the migration now verifies
the expanded native values and retains the original class objects. The prior
checkpoint stays durable until the upgraded state has been written.

The 16-ULP comparison by itself did not fix audio. C15 passed host movement and
Undo but rejected the guest move. C16 logged pitch `1.00000238`, 20 ULP above
1.0, with unchanged gain, pose companions and class bytes. Repeated native
selection/scene reconstruction accumulated the error. C17 preserves exact
authored pitch/gain across the verified ApplyMarkerState `F3D930` (thiscall,
data/marker/flags, ret8), selection and pose refresh boundaries. It restores the
committed scalar values after scene reconstruction. The guard pins resource,
generation, pool slot and class, and never dereferences a replaced marker.
The small comparison tolerance remains restricted to these two fields.
The i686 fixture repeats the noisy native refresh 64 times and also preserves
an intentional new parameter value supplied before refresh.

C17 `sound-c17` passed host and guest movement/Undo with exact authored audio.
C19 `sound-c19-four` passed those checks from all four editors after cold
recovery. C18 `endpoints-c18` passed both entrance and destination movement/Undo
from both roles, preserving the other endpoint. `ball-c18d` passed quaternion
changes across axes, unchanged position/opposite endpoint and Undo on both
roles. Native Locate resets zoom; clicking an already-selected category can
collapse the cast browser. Fixtures must verify the visible card before Locate.
Earlier ball attempts picked the teleporter body or background and are retained
as failed fixture runs, not successful ball evidence.

`F45480` is the native populated class remover, thiscall(data, classIndex),
ret4 at `F4580D`, called only at `ED44B0` inside palette history. It removes
placements and clears references in other classes. Shared removal therefore
preflights references and holds document, class and affected placement leases.
C19/C20 deletion was safely rejected because canonical application installed
the new class map while the owned intermediate still had the old placements.
Removing placements twice did not fix it and was discarded. C21 rebuilds the
owned marker pool with retained placements and matching bindings before
validating/restoring the class map. `remove-c21b` passed host and guest removal,
Undo, Redo and Undo with four matching native checks and exact retained poses.

C20/C21 native advanced-rule changes passed on host and guest, including guest
Undo/Redo. `custom-c21` passed guest import of the existing private
`Preview4Captain92` creation: seven dependency records were uploaded, admitted
by the host, distributed, then checked Undo/Redo passed on all four editors.

The cast-card dice is Randomize, control `073272B0`, not Disguise. It replaces
the cast asset through palette message `073E7B71`, rebuilding actors. Its
unowned C21 path froze safely at the old durable revision; explicit recovery
restored revision80 and history. Card action `ECE4C0` is thiscall with three
raw words, retC; the second argument is an IWindow read from entry ESP+8 and
queried at vtable1C. Randomize is blocked at this input boundary.
Control `073D54A0` was initially misclassified: it suspends the scenario for
the ordinary Sporepedia picker, including new cast imports. Blocking it broke
imports, so C25 explicitly leaves it enabled. It is not a nested editor guard.

The inspector Normal/Invisible/Disguised menu is the actual disguise control.
Selecting Disguised opens Choose an Object. The stock card is spelled
`Abandonned Saucer` in this executable. Its asynchronous callback also uses the
owned ScriptUI class subtree, which had the class gate but lacked input_cast.
C22 adds the missing tag, but that alone did not fix the complete native path.
C23 additionally retains the binding of a surviving native marker pool slot
with the same class even if its scene actor was replaced. A changed class or a
previously free slot cannot inherit that identity through this rule. The i686
fixture retires the old actor mapping to reproduce this case.

C24 found the remaining companion: choosing Abandonned Saucer changes native
physical scale from 1.0 to 0.3 while relative scale remains 0.5. The class lease
already covers its placements in Coordinator, but Runtime only retained class
settings guards. The class gate now retains affected placement guards too, so
the class subtree and native scale change form one checked transaction. C24
passed the host case. C25 `disguises-c25` passed both roles with one history item,
stable placement identities, matching four-player native checks, exact scale
restoration on Undo, Redo and Undo. Teleporter destination's native visibility
menu only offers Normal/Invisible; Disguised is an entrance option. The earlier
guest fixture incorrectly requested an unavailable destination option.
### Preview 4 palette removal, gameplay launch and terminal cleanup

`SporeCoop::Editor::PaletteMessage` at ED4110 is
`thiscall(palette+8, messageType, rawPayload)`, `ret8`. Remove message 07326686
carries the native class index. The palette removes its visible card and opens
history before calling data removal F45480. Gate this entry before either side
effect; retaining only the data hook leaves a hidden card after rejection.
Pending removal pins the palette listener, project, epoch, native generation,
class revision and participant identity. C26 host/guest populated removal and
checked Undo/Redo passed on four private clients. Removing Ace while an authored
Talk goal references its class was rejected with no card, revision or history
change (`gameplay-c26/reference-rejection.json`).

An editor preview's live local native resource key can differ between players,
and an unsaved draft can have a zero source instance. Its gameplay place must
derive from the immutable co-op adventure descriptor and run, under the exact
owned native preview. C26/C28 four-player tests compared every remote captain
against its owner's local moving position, not just entry into native Playing.
Launch status now waits for each live scoped captain, the authoritative goals
baseline, and the guest roster baseline. The success gate is retained for that
run after initial acceptance so a later downed captain is not a new launch.

Native hammer Stop F0EB09..F0EB25 assumes the ordinary gameplay HUD. An interrupted
Talk or terminal cinematic can retain the cinematic UI, camera and input hold
after cleanup reports native Build. Before the owned preview cleanup, use the
already verified `Simulator::cCinematicManager::Get` and
`SporeCoop::cCinematicManager_EndCinematic` (ADFF50, thiscall(manager), ret).
The end message is synchronous, so it must run while its play object still
exists. Recheck native mode/play/display identity and that the cinematic
actually released before cleanup; replaced or still-held contexts fail safely.
The i686 save fixture covers playing, success and failure, context replacement
and retained cinematic holds (431 checks at C28).

The first private Talk request was rejected because reliable interaction arrived
before the captain pose used by the host's proximity check. The fixture now
waits for pose replication before requesting Talk. Its initial request uses the
explicit lab command; all pages and Finished controls use normal native UI.
C28 four-player conversations held host clocks/pages/goals while only a guest
confirmed, then completed both authored Talk goals with replicated counts and
the actual shared success result. `world social` intentionally cannot complete
Talk outside the group conversation commit, so the generic objective fixture
now excludes Talk and uses the actual guest player ID.

C28/C30 coordinated Save rejected class 1027 after custom import. Decoding the
two owned class fragments found exactly four changed bytes, at decoded offsets
8D6..8D9: attribute `62B74271`, from zero to `5523CA25`. Initializer
138A250..138A3ED and the native string at 148C688 identify it as `mMachineId`,
asset reference offset C. The resource key, server ID, model type, behaviors,
text, stats and all other bytes matched. This is the local machine lookup hint
already described in adventure-world.md, filled by native Save for an imported
creation. Canonical class fragments clear only those three machine hints on
private copies. Recovery normalizes the document, both history directions and
retained decision patches together, checking that class references stay exact;
revisions and history entries are retained. Detailed byte capture was confined
to the C30 diagnostic candidate and removed after identifying the field.

The native multi-select picker calls F462F0 once per asset in the same UI turn.
A single pending key loses earlier selections while document ownership arrives.
C32 retains a FIFO of up to 32 distinct keys under the same project/epoch/author
and native generation, draining one owned import at a time. Each asset keeps its
own checked creation transaction. Denial, Escape, timeout and context replacement
retire the pending queue. The i686 input fixture now has 4,418 checks, including
queue order, bounds, duplicate suppression and no mutation before ownership.

Native picker thumbnail positions are animated during entry. Reading their UI
rectangles as soon as the title appears produces stale coordinates; an early
batch fixture clicked Create Your Own instead. Its excluded nested-editor path
left a black native transition with the committed draft intact and no caught
exception. The next private run recovered the checkpoint. Wait for the picker
layout to settle and verify its selected-item count before accepting a batch.

The C32/C33 native host batch produced both transactions, but a guest's second
key expired while its first cumulative dependency bundle uploaded and applied.
C33 also prevents another import while native capture/drain is outstanding.
C34 keeps the next key's ownership timer fresh while an authorized local
transaction is in flight; the transaction retains its own transfer/cancellation
bounds. Context, generation, Escape and denied-ownership retirement still apply.
The i686 fixture covers both barriers and the resumed ownership timeout, with
4,442 checks. These are distinct from the animation mistakes in the UI fixture.

The transaction-in-flight condition must also block both direct import and FIFO
drain, not just refresh their timer. Runtime's generic lease helper refuses an
awaiting submitted operation, but deliberately allows the earlier retained
`pending` gesture to acquire/retain its own lease. A guest upload remains in that
earlier phase. Without the explicit import barrier, the next key can enter with
a valid document lease while the sole Runtime pending slot still owns its first
gesture. The regression now proves neither path calls the structure gate during
that interval. This explains the guest-only missing second class in C32..C34.
The final input fixture has 4,444 checks. Import queue/drain/native-result debug
receipts log resource keys and bounded counts, without native pointers or text.

C36's trace identified the final cancellation: `scope=0` immediately after the
first accepted import. `current_gesture_scope` deliberately returns an empty
new-input scope while `awaiting_operation` is set. That is correct for starting
another gesture, but incorrectly retired the already-authorized import batch.
C37 supplies a separate retained import-owner scope during that local wait;
project, epoch, participant generation, native generation and all other
transition checks still apply. Its input fixture has 4,450 checks, including
temporary new-input ineligibility and cancellation on a changed participant.

## Preview 5: independent asset ownership and native live visuals (2026-10-28)

Document ownership overlapped every class inspector. Native creation/import and
class removal now acquire a separate Structure capability. Placement creation
also owns its source class; removal owns the affected class and placements.
Document remains the exclusive whole-draft capability. Tombstoned placements
retain their class/act relationship through retained history, so restoring B does
not conflict with a live inspector for A. Lease-use timestamps are per scope.

The native four-player C3 run found a second removal failure after property Undo:
`guarded_settings` still described the pre-Undo revision. Forward property edits
used current field revisions, but DeleteClass used that stale object revision.
Own decisions retire the old guards; idle native gates and removal recapture
current owned revisions. A Win32 regression now performs the actual property
transaction, Undo and populated class removal through Runtime callbacks.

Teleporter entrance and exit have separate spatial components. `F3D930` refreshes
the entrance but does not copy marker `1E8/1F4/204` into the exit display.
`EFCD20` resolves Cast `0175CDC9`, reads its secondary object at `54`, then
sets spatial component `01186577` through virtual `38/3C/40` (position pointer,
quaternion pointer, float scale). `B18D40` is exactly Cast `01186577`.
`EFCD20` itself has caller stack cleanup and is not called through thiscall.
The adapter uses the individually verified spatial setters. This fixes the
host-to-client display refresh without selecting another participant's object.

Live movement writes those render components only. Authored markers remain at
their committed poses. On expiry, ownership loss, cancellation or a new revision,
the adapter resolves current marker bindings again and restores current poses.
No borrowed scene pointer survives a delayed movement callback.

New-placement previews use the existing verified `CreateObjectInstance EEDD40`
cdecl `(key,pos,quat,scale,isAvatar,class,marker,acts,act)`. The preview supplies
`false` and null marker/act pointers, creates no ScenarioData mapping and holds
one intrusive object reference. Noun-manager destruction removes the temporary
scene object, followed by release of that reference. A generation-scoped peer,
exact live Structure/Class capability, project/epoch/revision, monotonic serial
and one-second expiry fence each hint. The wire path is bounded and latest-only.
Source capture sends at most ten updates per second. There is no per-update
transaction, persistence or native history entry.

`EE3610` is the ordinary inspector open/rebind method: thiscall(settings,classID),
ret4. `EE3F85` handles gear message `07465518` by passing script `18` settings.
`810110` reads layout `14` container visibility byte `8C`, returning AL with a
plain ret. The full-screen root IWindow can remain visible while this layout is
closed. Inspector capture/restore therefore uses this native layout predicate.
Native scene refresh and local selection rebuilding preserve the open local
class inspector when its class still exists. Opening it never selects another
participant's object. The main editor layout predicate also hides world hints
while a native full-screen picker covers it.

Native fixture findings retained under `out/editor-preview5`: single-jump drag
input can initialize a native anchor without subsequent displacement; use the
ordinary multi-move drag helper. A populated card's `04A1CB6D` picker action is
Replace, not the empty Add card. The initial import fixture hit that action and
correctly rejected its unowned class replacement, restoring the committed draft.
The corrected fixture uses the verified empty Add card. Native raw class memory
contains uninitialized padding and inactive fixed-array storage; compare owned,
normalized serialized class subtrees, not raw memory byte equality. No test
writes native process memory or edits a loaded profile database.


## Preview 7: marker-dependent placement previews (dc04)

Installed remote crashes `crash-20261029-172303-i2.txt` and
`crash-20261029-172435-i2.txt` both fault at runtime014B12D0, preferredF212D0,
inside CreatePowerupObjectInstance. EBX is zero. Its first argument is the
marker and F212D0 dereferences its class immediately. The temporary preview
called CreateObjectInstance with a null marker; the stack returns through
EEE21A and the editor Runtime's update_previews call. Existing creature-only
preview tests incorrectly blessed null marker/acts arguments.

MarkerCtor F2A960 is thiscall(storage234), returns its storage, plain retF2AAC9.
It initializes the owning inline act vector28/2C/30, with inline begin40 and
allocation sentinel3C zero. MarkerDtor DFC580 is thiscall(marker), retDFC5C4;
it destroys path and act storage without freeing the outer marker. Native
CreateMarker calls these atF44276/F44291. ResizeMarkerActs F42D90 takes
this=marker+28 and uint32 count, ret4 atF42E06/F42E1E; its native caller is
F4437C. The symbol verifier now checks those signatures and call sites.

Each preview owns this detached marker and constructs the current one-to-eight
native act rows before calling the factory. Its class and both endpoint poses
come from the admitted hint. Placement228 remains FFFFFFFF. Neither the live
resource pool nor ScenarioData maps receive a preview entry. The factory's
linked exit borrows the same marker and must be pinned too.

Both ornaments are retired exactly once using DestroyInstance, whose B22560
implementation queues intrusive references. Their marker pointers are detached
before freeing storage. AssignObjectReference B5FAB0 clears the checked parent54
and child50 links while both pins remain held; release follows, then MarkerDtor.
A failed factory drops its owned marker and may retry the same hint later.

The private native fixture found that Escape and dropping back over the cast
card finalized this placement gesture rather than cancelling it. Those attempts
are retained as fixture failures, not cancellation passes. Win32 tests separately
verify withdrawn-preview cleanup, linked references and factory failure/retry.

On the private reported adventure, native Save/reopen changed two building
positions by one float ULP per changed component (largest delta0.000030518).
All other34 placements, complete cast and act/captain settings, quaternions,
scales and properties, including both teleporter endpoints, matched exactly
after existing stable-ID remapping. Marker0 changed from
(-51.15800476,-111.1454239,484.7984924) to
(-51.15800095,-111.1454163,484.7984619); marker3 from
(-210.5035248,-428.4013672,288.2240906) to
(-210.50354,-428.4013977,288.2241211). This is consistent with native position
normalization, but a detached native baseline was not measured here. The exact
semantic-hash assertion failed and is retained; no serialization mask or
authored-position rounding was added to hide the difference.

## Preview 9 C35: exact Saved dependencies before native preflight

C34's missing-local-key warning is not evidence of a ban. The retained alias
BEM/pollen/PNG bytes exist, but no cold process registers their package. The
new adapter uses an immutable per-Saved-image receipt, not a directory-wide
historical cache mount. The receipt binds the genuine persisted bytes BEFORE
terrain/grounding/collector normalization, executable/profile identity, all
Captain/Crew and three per-class raw references (including unused classes),
actual stock database observations and the selected admitted dependency bytes.
Native Save, recovery Save copies and collaborator exports publish this file
only after their existing Saved semantic/terrain checks and before confirmation.
The native save itself and the prior receipt remain intact on publication error.

The exact supported executable's asset descriptor initialization at 138A39A
sets property 62B74273, readers/writers 572810/572840, and offset 18 at
138A3DE. This independently confirms native model+18 alongside key+0,
machine+C and server+10. Complete raw roster reads use the existing owned
ScenarioResource constructor/Read/Release and bounded pool strides. A stock
database basename requires a genuine bounded NUL-terminated location. Group0
keys retain the existing ResourceKey policy. Custom closures preserve the
existing Collector contract: primary-or-BEM body, optional fixed companions,
same-key/different-bytes rejection and the existing admission ledger.

Native EC5940 establishes an owned root at loader+18 and real state1/phase0
before its EC59F1 call to EC52F0. Only this non-checkOnly boundary is deferred.
Ctor EC4B60, destructor EC4930 and reset EC4F20 fence weak tickets by module,
lifetime and request; forward work also freshly reacquires ScenarioMode+1C,
the exact selected root and key. Outside Present, the callback retains the
controller and root, rechecks between AddRefs, compares the actual selected
root's complete raw roster against the Saved record, then admits only that
receipt's dependencies. It rechecks installed provenance/bytes, the disk image
and selected roster before resuming the original EC52F0. All original native
availability, server and model predicates remain unchanged.

Native idle EC51A0 is suppressed only while this adapter owns phase0. Reset,
destruction, replacement, timeout and unload cannot abandon that state for the
native timeout's modal7 OK continuation. Cleanup can cancel the lifetime-proved
old controller after active-parent/key retirement; forward admission cannot.
SporeKit0.3.5 fences lifecycle execution to main/outsidePresent. Every direct
loader detour also retains its callback DLL across nested pumps. Its release
passes through an SDK task with a second module reference, so the last release
occurs in Kit after the callback returns. SDK task retention failure rejects
enqueue; release-enqueue failure conservatively retains the OS reference.

The focused C35 run also reproduces an earlier export-cancel/rebuild failure
in the exact retained C33 binary: terrain_scene_current used a ready-only owner
pin while polling an already-restored, temporarily unready scene. The new
saved_scene_current producer closes raw numeric ownership/generation after its
getter without claiming readiness. Save As key rotation remains allowed in this
owned return path; actual restoration still waits for readiness. Runtime closes
its project/epoch/participant/revision/transport predicate before and after this
probe. Admission and ordinary owner captures retain exact key checks.

Controlled checks pass: native owner3226, collector202, runtime native
flow11128, recovery2026, saved-root158 and alias241, with223 pinned inputs and
zero drift in out/preview9-c35-root-focused-r2/receipt.json. The gate's separate
controlled fixture passes17718, including pending-state cleanup, pointer ABA,
parent/key drift, selected-roster mismatch, nested unload and OS/SDK retention
failure. These are fixture results, not live cold-reopen acceptance. The next
gate is actual two-player import, native Save, process restart and file reopen.

## Regenerated owner caches versus immutable Saved dependencies (C45)

C44's native Save completed locally at revision24 after Creature creation and
content replacement, then stopped before sharing its recovery checkpoint. A
diagnostic-only C45 replay identified the exact missing record:
`40626200!7D8C23AC.0F43029A`. Both recovered peers were strictly converged before
Save. This was a missing admission, not a changed admitted payload. Evidence:
`out/preview9-c45-creatures-native-save/95-commands.json`.

The producer/consumer mismatch is explicit in source. The immutable creation
producer `immutable_version` requires an editable BEM and mints private instances
in7Dxxxxxx; `remap_owner` removes its own family-primary,02D5C9AF and0F43029A
regeneration caches. Native loading can recreate those records. The Saved
collector later observes every currently available companion, including a newly
generated cache that was intentionally absent from the immutable bundle. Treating
every observed record as a new immutable dependency rejected this supported
body-backed alias.

The value-only receipt producer now omits a missing cache solely when it belongs
to an observed immutable7D alias in the four supported editable families, has the
exact effective owner group and instance, and is one of those three regeneration
types. The complete original reference companion list must remain valid and
ordered. Its exact BEM key must appear in that list and in both the selected and
admitted maps, with identical bytes and the verified D37E6548 BEM header. The
omitted record is also removed from that reference's dependency sidecar; no raw
Saved reference, class roster, server, machine or model field changes. The normal
final receipt validation still checks the full body and companion closure.

An admitted record always takes the existing exact-byte comparison before this
exception. Even an admitted regeneration cache with one changed byte rejects.
Missing or changed BEMs, metadata/pixels, foreign owners or groups, unknown types,
nonprivate sources, unsupported families and malformed companion lists reject
atomically. The omitted cache is never mounted or published as immutable data.
No Native producer, scene, catalog, cache record or source key is rewritten.

Controlled codec tests retain the genuine119-class raw Saved image and exact
four-record admitted package. Missing caches collapse to the identical golden
receipt, and cold encode/decode preserves that receipt. Tests also retain the
old producer's genuine failing executable and cover admitted-cache mutation,
body expiry and foreign-cache boundaries. Focused artifacts are under
`out/editor-preview9-saved-cache/`.

Actual C46 Native Save passed the exact recovered revision24 scenario that
failed in C45, producing `408A0000!2913EE8A.366A930D`. Both participants reported
SavedApplied2/2 and settled readiness, with unchanged history and authored
semantics, strict Native agreement and zero faults. The original Saved carrier
was retained. Evidence: `out/preview9-c46-creatures-native-save/result.json`.
Fresh-process cold reopen remains a separate gate.

### C46 clean sidecar restored after accepted creation changes

C46 saved revision4 and received both SavedApplied acknowledgements, then
accepted new Military Land cast entries at revisions5 and6. The final normal
Save rejected reason5 before a dialog or Native write, with both peers still
converged and fault-free. The frozen source retained the completed Save's clean
`Snapshot::local_dirty` through detached preparation/persistence. Creation
return restored that captured false value to `data+84`; canonical cast commits
had not appended a native history entry to make the history counter dirty.
Evidence is retained in `out/preview9-c46-final-save-observe` and the C46 frozen
production source, independently of the later staged-pose experiment.

Runtime now marks only `local_dirty->dirty=true` on the prepared value for an
applied canonical Change, including the adopted-live retention branch. Its
captured `saved_revision` (+80) and metadata flag (+85) are preserved. Rollback's
independent before snapshot is untouched. A Cancel with no intervening change
therefore remains clean, while a foreign accepted change during creation makes
the restored latest draft dirty. Missing optional sidecars stay missing; the
existing Native restore behavior marks those authored inputs dirty without
inventing saved-counter or metadata values. Ordinary Save retains CanSave and
the unchanged reason5 gate; it is not converted to Save As.

The focused actual-i686 regression uses real Runtime/Session/UDP/Store paths,
an opt-in model of the verified history/+84/+85 dirty predicate and real
creation-return orchestration. It covers both admission authors, clean Cancel,
host-away foreign edits, retained adopted gestures, rollback and absent
sidecars. RED uses the real pre-fix Runtime source archived before these two
lines, not a manufactured mutant. Receipts are under
`out/editor-preview9-c47-native-dirty`; actual C47 Save qualification is separate.

### Offline exact Saved/cold projection comparator

`tools/tests/coop_editor_saved_projection.py` compares two owned byte values
with the bounded native DBPF/ClassSerializer decoder. It permits only the
observed class-asset machine transition `5523CA25` to zero for an unchanged
private `7D` instance in one of the four editor families, identical slot/key,
model and maximum server sentinel. Unchanged machine values remain literal.
Other machine IDs require fresh qualification; no general reference mask or
production validator policy is introduced.

Every other decoded byte, including pose/Acts/Captain/Crew and unknown fields,
must match. Prefix comparison permits only the unused fourth word of an exact
type20/flags0 scalar ResourceKey and the independently checked embedded DBPF
length. None payloads, vector padding and all other prefix bytes remain exact.
DBPF header/index unknown bytes also remain exact after the actual packed-size
and index-position bookkeeping is checked. Unsupported layouts reject.

The CLI requires explicitly hashed copied OUT inputs and never overwrites a
report. `compare(saved,cold)` returns a compact report or raises ValueError.
Focused tests use the genuine C46 pair plus controlled decoded/prefix mutations;
the latter are not claimed as newly generated genuine NativeWrite artifacts.
This comparator grants no Native admission, cache or projection authority.

## Captain grounding across native Save (2026-11-05, feat/ga-editor-multi-act)
<a id="editor-save-captain-grounding-preview10"></a>

Native Save exports the grounded special Captain marker and rotates the local
resource identity. A normal live-scene witness cannot be reused against the old
source. C73 reaches native saved confirmation, then correctly refuses shared
publication because property43415003 differs from the committed Captain pose.
The durable draft remains at revision299; no successful SavedApplied is claimed.
C74 also preserves the checkpoint when the normal projection scope is unavailable
during recovery. These are BUG-0541 evidence, separate from BUG-0538.

The local terrain transition ticket now retains the exact Captain position
witness alongside placement witnesses. Saved-image normalization requires the
completed Save receipt, exact source and current scene; private Read/Write must
round-trip the original bytes. Only XYZ equal to the retained authored or
rendered value can normalize. Restore all temporary private writes, verify the
entire original stream again, release native owners, and close the current
fence before publishing normalized bytes. Unexpected pose, appearance, health
or other fields remain subject to full committed-document validation.

After that validation, rebind the live witness to the receipt's exact new owner
only if marker, native generation, Captain revision, scope and primitive pose
still match. This is read evidence during the existing hold, not ownership or a
new saved field. The recovery/Save read scope uses the existing transition key
and fenced callback; ordinary authoring scope stays unavailable. The cold
Saved-versus-reopened payload comparison tracked by BUG-0538 is unchanged.
