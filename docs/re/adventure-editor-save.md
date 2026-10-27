# Native Adventure Editor saving and preview (dc04)

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
