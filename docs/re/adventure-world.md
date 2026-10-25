# Adventure content and shared world (dc04)

The native interfaces below were inspected against executable SHA256
`dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`.
Static verification establishes addresses, layouts, call arguments and stack cleanup.
It does not establish that the complete co-op flow works in a running game.
Paired validation below covers selected world/combat/goal routes; broader adventure
validation remains necessary. Entry, loading and result integration
are documented in [adventure.md](adventure.md); executable compatibility is in
[build-dc04.md](build-dc04.md).

## Content extraction and registration

`adventure_collect.cpp` uses the engine resource manager and record streams. It does
not parse or modify Pollination's DBBF records. `GetPrivateResource` at manager vtable
`+1C` accepts `(key, resourceOut, 0, 0, 0, 0)`: wrapper `8DFC10` forwards these six
arguments into `8DF380` with the cache flag cleared, and returns with `ret18` at
`8DFC35`. Query the resulting ResourceObject through vtable `+0C`, type `E742574A`,
to obtain a scenario. ResourceObject `Release` is vtable `+04`.

The scenario's private copy is necessary: reference normalization must never edit an
already loaded/cached scenario. Native `Read` is `F2FC40`, `Write` is `F25E00`, both
thiscall with a stream argument and `ret4`. Schema initialization at `138C850` and
native read helpers confirm:

| Scenario field | Offset | Representation |
| --- | ---: | --- |
| Avatar asset | `130` | native scenario asset |
| Avatar lock | `175` | byte |
| Initial posse | `178` | begin/end vector, stride `38` |
| Acts | `23C` | begin/end vector, stride `534` |
| Classes | `2BF4` | vector map, pair stride `27E8`, class at pair `+8` |
| Markers | `2C10` | native marker vector map |

Class assets are at class offsets `0`, `28`, `50`. Schema initializer
`138B560..138B990` and class read `F2746F` confirm these fields. Native asset layout
is key at `0`, machine ID at `C`, server ID (`uint64`) at `10`, model type at `18`.
Initializer `138A250..138A3ED` confirms the layout.

`ResolveLocalServerAsset=F257B0` is cdecl `(uint64 serverID, Key* output)->bool`, plain
returns at `F25821/F2582A`. It uses the installed local asset database and checks for
removed records. It does not download from the Spore servers. If a server-only
reference cannot resolve locally, collection fails with an explicit error. For
resolved references, only the private copy's local key/server sentinel are replaced,
then the native `Write` method serializes that private copy.

Record extraction uses ResourceManager `FindRecord` vtable `+30` with
`(key, realKeyOut, nullptr)` (`8DFC80`, `retC`), then database `OpenRecord +34` with
`(realKey, recordOut, 1, 6, true, nullptr)` (`8DA6F0`, `ret18`). IRecord `GetStream`
is `+18`; native packed record `8DC650` returns its stream at object `+20`.
The stream has size `+1C`, seek `+28` (two arguments, `ret8`), read `+30`
(`ret8`). Close the record through IRecord `+24`, then release the caller's IRecord
reference through `+08`. This differs from ResourceObject's `Release +04`.

The native output-stream adapter has `Read +30`, `Write +38`, size `+1C`, seek `+28`.
**Write returns a boolean in AL, not the number of bytes written.** FileStream write
`931D00` and integer-write helper `93A9E0` prove this; scenario writer `F26044` tests
AL. Returning a byte count causes serialization failures for 256-byte multiples.

Creation extraction includes the effective primary CRT/BEM record and installed
typed file, creature data, XML, metadata, summary, PNG and known preview/property
companions. Existing FindRecord aliases can resolve a CRT request to a BEM key.
Built-in dependencies are skipped only after resolving their native database to a
known vanilla package basename. The adventure root is always included.

Act music is `act+80`. It identifies a native audio descriptor/event, not a palette
property or necessarily a same-ID audio sample. Earlier collectors checked
`D87454E6!ID.00B1B104` and then `021407EE!ID.01A527DB`. Those assumptions rejected
valid installed music whose descriptor refers to a different sample ID.

Preview 4 additionally recognizes music value 1 as a native audio event without a
standalone DBPF record. Mr Puzzle's Magical Gates is stock scenario C2C7B154 and
reproduces preview 3's D87454E6!00000001.00B1B104 failure in isolated profiles.
SetCurrentAct F1EF2B reads act+80, stores it at play+F4, and F1EFB4 calls 657200.
That cdecl helper routes the value through native audio event properties 03475381
and 03475385 and dispatches via the audio system; it never performs a DBPF lookup.
The collector preserves value 1 and skips its absent descriptor only when that
effective key is missing. A custom override at that key is still rejected.

The descriptor path is verified against dc04, beyond the generic event helper:
AudioSystem's primary vtable is `1452CE8` (constructor `A3250F`), event handler
`A2C6E6..A2C742` reads `03475381` and calls slot94=`A2E5F0`. That function calls
slot138=`A22FD0` with the authored ID and group 021407EE at `A2E75D..A2E776`.
`A22FF0` constructs type 02B9F662 and `A23021` performs native resource loading.
The audio factory `A17610` advertises types 02B9F662 and 02C9EFF2. Parent reading at
`A19F9F..A1A021` uses property 5F6317D5, defaulting missing/wildcard group/type to
021407EE/02B9F662. Wave-array reading `A202BD..A20314` uses 701ED91E; `A28B70`
takes only each wave key's instance, and `A28D9C..A28DA4` fixes its sample type/group
to 01A527DB/021407EE. Other descriptor keys can be event parameters, audio control
properties or curves; treating every key as a required resource is also incorrect.
These additional addresses document static evidence; the collector does not call
them and adds no native ABI dependency.

The recurring The Eighth Day failure was authored music 3DAFBC30. Its real installed
record in `Spore_EP1_Data.package` is `021407EE!3DAFBC30.02B9F662`, file offset 537711279,
68 compressed/77 decoded bytes, SHA256
`1e64a1f4161987be85350c502a8452af5f4ee654ae4871560d2a863f94a7ae46`.
It inherits 869DB904, which inherits 51298C8E, and refers to sample 4BA53FA1 (same
package, offset 454456738, 2,219,806 bytes). There is no 3DAFBC30.01A527DB. Stock scenario
AF1BC5FF uses the same descriptor in acts 2/4, so an isolated stock reproduction is
available without accessing a player's downloaded adventure. Welcome's 2D67FCE4
descriptor refers to sample 2D67FCE4; it happens to have a matching sample ID.

`adventure_music` now follows this verified descriptor/parent/wave path using
effective ResourceManager records, checks built-in provenance on every visited
edge, and rejects custom root, parent and sample overrides. It bounds descriptors,
properties, lengths, references and recursion, rejects cycles/malformed streams,
and validates each distinct music ID once per collection. It never rewrites an act's
authored value, disables music, substitutes tracks, or transfers an unsupported
custom audio graph. Missing ordinary descriptors still fail, with the adventure,
one-based act, authored music ID and actual failing typed key in the diagnostic.
The scalar/array property parser reads native big-endian headers and little-endian
ResourceKey payloads; the existing verified record stream handles decompression.

Focused portable tests include the real Eighth Day/Welcome descriptor bytes,
inherited music settings, distinct sample IDs, custom overrides at each level,
missing parents/samples, reserved controls, cycles and every truncated root length.
An offline installed-data audit passed all 23 distinct music values across 69 stock
scenarios, using serialized actMusicId attribute 078FE86A (constructor
`138C768..138C7BC` maps this to +80). The compact audit package contains real decoded
descriptors and sample-key baselines; it checks resolution/provenance, not audible
playback. See `out/music-resolution/audit-input.json` for local record provenance.
Muted native tests can establish successful load/act transitions and unchanged
authored IDs, but must not be described as audible playback verification.

Card metadata loads an owned native pollen metadata resource. NameText414E00
returns metadata+78's UTF16 string pointer; AuthorText550880 returns+58. Native
Sporepedia641800/641810 tailcall those helpers. Captain names override the body
metadata with String16 property75ED3A1C from LoadAssetProperties. Native quickplay
DFBEEB calls EEBE90, whose5BF960 helper writes this property. GetValueString16
68A4E0 validates type13 and handles inline/pointer storage (returns68A4FD/68A507).
Owner-scoped captain imports remap the image and property companions alongside the
model. AddImageWindow807970 uses GetImage806320, returns an image window at807B33,
and accepts cdecl(key*,float x,float y,parent); changing the window area scales it.

Custom gameplay props, custom act audio and dependencies on mod-provided external
parts are not closed automatically yet. Collection reports unsupported custom
gameplay/audio records instead of silently claiming readiness. Ordinary captain
outfits are carried by their saved BEM/creature data; no separately verified captain
gear resource list is available. Native PNG import/network download is not used.

`net/adventure_assets.*` validates canonical key manifests, SHA256 content and bundle
hashes, counts, sizes, offsets and chunk ordering before import. Bounds are 128 MiB
per bundle, 16 MiB per record, 2048 records, 900 payload bytes per chunk. Its standalone
tests include corrupt cache entries, truncation, malformed manifests, duplicates and
conflicting chunks, and have passed ASan/UBSan.

Runtime registration writes a cached DBPF package under the isolated profile, creates
the native packed database and registers it at a newer priority. Registered packages
remain referenced until the native scenario releases its resources. Resetting a
network transfer does not unregister a package currently used by the game. Explicit
`adventure_assets::release()` must run outside the frame after scenario release.
Captain imports use `register_records()` without resetting scenario content transfer.

Collector traversal was cross-checked against the primary
[CreationZIP Porter export recipe](https://github.com/Spore-Community/Spore-CreationZIP-Porter/blob/c2fe313f0343ca6d7e23d22ee9c4c26d9394e620/SporeCreationZIPPorter/CheatAssetExport.cpp).
That project is MIT licensed. Its ModAPI dependency is independently GPL licensed.
The implementation here uses independently verified native calls and layouts, and
does not copy GPL SDK implementation code.

## Shared actor identity, combat and lifecycle

`adventure_world.cpp` runs only after the adapter's committed/running predicate is
true. Captains are excluded from the NPC actor set. Actor identity is
`(scenarioMarkerID, hostRuntimeID)`; matching only a class or a noun is insufficient
for repeated placements and respawners. Native object `+18` is its marker pointer,
object `+24` is its runtime ID, and marker `+228` is the placement ID. The native
CreateObjectInstance tail `EEE3CE..EEE3D5` writes object `+1C` class and `+18` marker.
`GetMarkerForObject=F3DB10` and `GetObjectForMarker=F3D620` are both thiscall `ret4`.
The old autogenerated map had these two names reversed.

Spatial cast is native primary `Cast +0C`, type `01186577`. Combatant cast type is
`013F94D4`. A combatant's `ToGameData` is virtual `+0C`; subtracting the creature's
combatant offset from every generic combatant corrupts building/vehicle pointers.
Spatial position/orientation setters are virtual `+38/+3C`. Generic damage is virtual
combatant `+18`, with a non-null direction vector and a combatant attacker pointer.

**Combatant `+2C` is raw tuning, not universally maximum HP.** Creatures commonly
store `1.0` there while their true maximum is 100. Use virtual `GetMaxHitPoints +58`
before and after changing tuning, and preserve `currentHP / trueMaximum`. The health
baseline retains both original and applied raw tuning, so native act changes that
preserve the scaled field do not multiply it repeatedly. Snapshots carry true maximum
and raw tuning separately. Health is combatant `+38`.

Native hostility uses `IsHostileToCreature=C0B9C0` thiscall `(creature,otherCreature)`
`ret4`. It queries the native relationship manager and recognizes hostile statuses
2/5, including current adventure relationship overrides. `Cast(CE9F6639)` returns a
base creature pointer (native primary cast `C0B7D0` and scenario call `F1D036`).
Do not infer hostility from class index, noun, or simply team != 2. This predicate
currently covers creature actors; generic non-creature enemy classification remains
unverified and their HP retains authored tuning.

Host snapshots mirror positions, orientation, tuning, HP and native death cleanup.
Live guest creature AI is suppressed; each guest keeps native scenario instances.
A reliable complete actor roster, separate from lossy position snapshots, removes
host-absent native instances outside the frame. Last positions are retained through
ordinary packet loss. Actor respawn/reappearance uses `adventure_world_lifecycle.*`:
verified native map helpers retire stale forward/reverse bindings, create from the
native marker/class, register intrusive references and check both lookup round trips.
A different live incarnation occupying the marker is rejected; a factory yielding
a different noun is retired rather than bound to an unrelated host actor. More than
2048 native actors disables authoritative roster removal. This path still needs live
tests of respawners, repeated placement and simultaneous instances per marker.

Captain hits use a separate event path. A guest reports its owned attacks and its own
environment callbacks; host native callbacks from remote attackers or remote-victim
environment are suppressed to prevent double application. Host NPC attacks remain
authoritative. Type 10 routes to healing. The common combatant base damage entry
`BFCF10` (`ret14`) covers vehicles, buildings, ornaments and other combatants.
Its virtual `ToGameData +0C` is used for ownership. The animal virtual damage entry
`C075E0` is skipped at this lower hook because `game.cpp` already filtered that
override; otherwise an animal hit would be processed twice. Invincibility `+80`
and dead state `+34 == 2` retain native behavior. Base damage type 10 adds HP at
`BFD135`, while normal damage subtracts HP at `BFD176` and triggers native death
handling `F249B0`. Bound live guest NPC vehicles have narrow tree, weapon, timer
and actor filters, while graphics and native death cleanup continue. The final
paired capture below verified all four guest counters at 1130 and host counters
at zero. Effects outside these filtered ticks and common damage paths still need
broader vehicle/building gameplay validation.
Per-peer event IDs, current run, sender
ownership, finite bounds and range are validated. Recognized source ownership is
retained for type-7 damage-over-time callbacks for up to 30 seconds; ownership on
longer or independently spawned effects still needs live validation.

## Native objectives and item interactions

Native play-mode fields: goals begin/end `64/68`, stride `1AC`; goal completed byte
`0`, count `4`, goal schema at `24` (type), `28` (target class), `2C` (second class),
`30` (required count). Current act is play `B8`, state `90`, remaining milliseconds
`BC`, total play time `C0`. Goal snapshots include both clocks and only call
SetCurrentAct when the act actually changes.

`CheckGoalProgress=F1F4E0` gets the active avatar via `B1FD00`, whose complete body is
`mov eax,[ecx+54]; ret`. Native polling handles MoveTo and Collect proximity and
Bring/Block/Defend state. The host evaluates this routine for each party captain
through a scoped, thread-bound override of the native getter. The underlying
noun-manager `+54` stays unchanged, so the adapter's direct local-captain lookup
retains its ownership identity even in nested callbacks. No SetAvatar flag or
ownership mutations are used. The getter's four-byte body has CC padding,
supported by MinHook's short-function handling.

The following interfaces are in `overrides_world_dc04aee5.sym`:

| Native address | Signature / role |
| --- | --- |
| `C15B20` | thiscall(creature, spatial item, slot index, replace), bool, `retC`: native pickup/attachment |
| `C15EB0` | thiscall(creature, spatial item, int, bool), bool, `retC`: native drop |
| `F1AED0` | thiscall(play, holder GameData, item GameData), `ret8`: Hold/Give callback |
| `F1B060` | thiscall(play, holder GameData, item GameData), `ret8`: release Hold callback |
| `F1ACF0` | thiscall(play, goal, first spatial, second spatial), `retC`: count native objective |
| `F1D310` | thiscall(play, target GameData), `ret4`: Befriend and per-object duplicate map |
| `F1D3E0` | thiscall(play, target GameData), `ret4`: AllyWith and duplicate map |

Native creature item slots are vector `E40/E44`, stride `24`, held spatial pointer
`+20`. `C15B20` calls helper `D7D1A0`; native drop `C15D30` calls `D7D240`, which emits
the scenario callback. Host-accepted pickup/drop broadcasts replay these native APIs
outside the frame. A guest's native goal counter calls are suppressed. TalkTo
completion carries the current act/goal/target identity; the host validates target
class and captain distance before counting that goal.

Befriend and AllyWith completion have a separate reliable event, world subkind
135, carrying run/event IDs, act, action category and target placement/incarnation.
Hooks on the verified native `OnBefriend` and `OnAlly` callbacks emit it even when
no matching objective exists. A guest Befriend requires the native impressed flag
already set, matching `D9BBD0` ordering (`D9BD68` before `F1D310` at `D9BDA1`).
The host revalidates the current run/act, connected alive captain, live authored
NPC identity, distance and native social mode before applying any effect.
`CreatureSocialMode=C0E800` is thiscall(creature), no stack arguments, returning
an integer. Modes one/two/four permit completed Befriend events; mode four also
permits recruitment and checks native posse capacity. A successful mode-four
minigame emits Befriend before Ally, so both impressed bit `80` and posse bit `100`
must survive the host replay. The host uses the native relationship/Befriend callback or
the actual recruitment operation. Native callbacks evaluate matching goals; the
client supplies no goal index, class, relationship or progress count on this route.
Run/act/target/category claims precede native calls, suppressing repeated effects.
Legacy goal events accept only TalkTo, so a goal-counter packet cannot substitute
for a completed Befriend or recruitment event. The completion report still comes
from the client; these checks do not prove that an untrusted client played its
minigame.

Kill objectives follow host native damage/death events. Block/Defend retain host
native evaluation. Container-specific Give and recruitment now have separate native
action helpers described below. Native ordinary guest Talk presentation has passed
the later two-instance UI test; simultaneous conversations and full Befriend
minigames still need validation. Shared counters alone
must not be described as complete support for every authored adventure behavior.
The Win32 mirror fixture covers non-objective social completion, genuine native
callback forwarding, duplicate/replayed events, malformed category/sender/run/act,
dead/distant/stale targets, downed senders, queued-state changes and recruitment
refusal. These fake-native passes do not establish a live Befriend minigame or
non-objective recruitment session.

Successful remote Befriend also replays native species relationship event 3 through
`BA4A00` (thiscall manager, event, species-key pointer, NPC; `retC`) and the native
`B58` bit `80` social-completion flag, matching `D9BBD0`. Actor snapshots propagate
that completion to guests. Enemy-only HP tuning is removed, preserving current HP
fraction, if the native hostility predicate changes to friendly.

## Native action helpers and remaining integration

Generic authored relationships have a usable native candidate: `BA29F0` is cdecl
`(cScenarioClass*, actIndex, optionalCreature*) -> relationship status`, plain return.
It reads class acts `+70`, stride `4E0`. Its creature argument is guarded at `BA2A14`,
so null is a valid input. It maps team/stance/custom behaviors to statuses 0/3/5/6.
The native creature relationship manager `BA40E0` obtains its class through
`B18470`, act through `F3BF80`, and calls this function at `BA41FD`. The independently
verified hostile predicate `C0B9C0` accepts statuses 2/5. The separate
`adventure_world_actions::hostile` adapter obtains the object's native class and
calls this function with null as the creature parameter. It validates the current
native scenario, expected data pointer, act, live object, class-map bounds and
returned class membership, act-vector bounds, and advanced behavior entry bounds
before native evaluation. It returns unavailable separately from friendly. Using
this authored-class result for vehicles/buildings remains an inference: a direct
native vehicle caller was not found and authored-team fixture tests are required.

The aggregate scenario vehicle update is `F39DF0`, thiscall `(strategy,int deltaTime,int deltaGameTime)`,
`ret8` at `F39E65/F39FAE`. It enumerates vehicle noun `18C6DE8`; its adventure branch
calls `C9F460` and `CA9850`, then behavior-tree tick `BC9160` at `F39F69`. Graphics
update `CA3380` at `F39F77` and adventure combat update `EF1850` at `F39F91` follow.
The generic tree tick is cdecl with nine dwords; argument five is the complete
vehicle pointer (ESI pushed at `F39F53`), not its behavior-agent subobject. A per-agent tree filter can suppress guest vehicle AI while retaining
graphics; skipping the entire aggregate update would freeze graphics and cleanup.

Actual native posse recruitment is `D542E0`, thiscall `(posseManager,targetCreature)`
returning bool in AL, `ret4` at `D5464B/D5465A`. Manager getter `D53980` is cdecl and
returns the lazily allocated `169E5B8` singleton. Native scenario action `F1D876`
calls it. Recruitment enforces capacity/duplicate rules, adds through noun manager
`B23430`, adjusts flags/herd/stamina/name, and invokes AllyWith callback `F1D3E0` at
`D544C5`. Therefore calling only the goal callback does not recruit an NPC. This
routine invokes UI/effect paths and must execute outside the frame. The separate
`adventure_world_actions::recruit` helper now calls the real operation, preserving
its rejection result and its native AllyWith callback. Callers must supply the
requesting captain's scoped native avatar override. Do not invoke `OnAlly` again.

Container Give can use native inventory entry points rather than copying container
structures. `BCF600` is thiscall `(building,itemGameData)->bool`, `ret4` at
`BCF695/BCF69C`; it creates an `80`-byte object-instance inventory item and adds it
to building inventory `+1EC`, then emits `F1AED0`. `CA44B0` is the equivalent vehicle
method, `ret4` at `CA4545/CA4561`, inventory `+D94`. Native Give behavior at
`D84034..D840E4` casts the building (`0E9CB8BA`) or vehicle (`0137E8E0`) first. If the
building's combatant invincibility is set (`BFC5C0`, getter of `+80`), native Give
emits `OnPickUp` and destroys the item instead of storing it. A vehicle timed-state
predicate `C9F0B0` has a special consume branch with no `OnPickUp` callback. Complete
replication must intercept the Give intent before local item deletion, because the
ordinary callback is absent on that branch.

`adventure_world_actions::give` now implements these native branches, after caller
validation of ownership, run, act, distance and placement identity. It accepts an
item held by the captain or an already dropped item. Held items first call the
verified native Drop with `(spatialItem,1,false)`, matching `D84034..D840AF`.
Storage calls the native building/vehicle operation, including its own pickup
callback. Invulnerable buildings emit the callback before native destruction;
timed vehicle consumption destroys without fabricating a callback. `GiveResult`
distinguishes storage and the two consumption outcomes. Missing inventory or a
native rejection returns a useful error. All mutation requires outside Present.

The helper installs one narrow intent hook at `D83E90`, the Give behavior tick.
The behavior table entry `158B280` points here. Generic tree tick call `BC9477`
loads the callback through behavior `+8`, supplies eight stack words, and cleans
`20` bytes: `(actor,doubleTime,word4,word5,state,word7,floatDelta) -> bool in AL`.
`D83E90` uses actor at entry stack `+4`, state at `+18`, delta at `+20`; returns
are plain `ret` at `D83F0B`, `D83FFF` and `D8410B`. The same shape is used at
`BC969E`. The six-word calls nearby are deactivate callbacks, not tick callbacks.

For a local captain on either host or guest, the hook only intercepts state 1,
active native Give order type 6, a live container/item, and a finished native
animation. Native order lookup is `BC9810`, thiscall `(orderList,maskLow,maskHigh)`,
`ret8` at `BC9830/BC9851/BC9868`; the original Give supplies creature behavior
`+B4C`, order list `+8`, masks `0/40000`. Animation predicate `C12560` tail-jumps
to `C0E420`, thiscall `(creature,animationID)`, `ret4` at
`C0E4B3/C0E4BA/C0E4C1/C0E4C7`. Spatial-to-game-data casts use native virtual
`+B8`, matching `AD25B0` and the original container cast helpers `CB47B0/BD8590`.
The intent callback copies identities and queues/sends; it must not mutate native
objects in Present. A successful queue advances state to 2, preventing local
drop/deletion. A failed send leaves the action pending. Other native Give paths
pass through unchanged. The helper is gated to the exact executable SHA256 and
all required symbols before it installs a hook.

The world integration now:

1. Initializes `adventure_world_actions.cpp` with its context.
   The active callback requires the committed run and actual Scenario game mode.
   Local captain matching must exclude NPCs and other players' puppets.
2. Routes the intent callback through the existing reliable Give message. Accepts a
   validated currently held item as an alternative to a recent Drop record,
   because the hook forwards before dropping. Queues local host intent through the
   same outside-frame apply path, then broadcast only on native success.
3. Replaces callback-only container application with `give`, under the existing
   avatar override and replay guard. A creature with full inventory rejects the
   action instead of falling back to a fabricated container callback.
4. Replaces remote Ally callback-only application with `recruit` under the captain
   override. The native goal hook counts the recruitment's single callback.
5. Uses `hostile` for bounded non-creature authored actors when no live creature
   relationship predicate exists. It does not infer hostility from raw team values.

`tests/adventure_actions_tests.cpp` is a standalone Win32 fake-native fixture.
It exercises actual thiscall/cdecl adapter stack shapes, storage versus consumption
callbacks, native rejection, real recruitment dispatch, stale acts, malformed
advanced vectors, and all eight words of a pass-through Give tick. It passed with
zero failures, compiled with `-Wall -Wextra -Werror`; symbol lint also passed.
This fixture establishes the wrapper contract, not native two-player gameplay.
Vehicle replication also initializes `adventure_world_vehicles`: its narrow hooks
stop tree, weapon, timer and actor processing only for a live guest NPC with a
current mirror binding. The actual Scenario mode and committed run are required;
captains and native death cleanup remain active. Diagnostic status exposes the
four suppressed-call counters. The native aggregate and graphics update remain
untouched. Independent Win32 ABI fixtures cover these hooks.

## Paired act-change reconciliation regression

The first successful paired native run had 405 mirrored actors and passed captain
damage/healing, manual revive interruption/grace and guest MoveTo/TalkTo objectives.
After act changes 0 to 1 to 2, host and guest actor counts diverged and the guest
reported a live placement conflict. Read-only snapshots of isolated processes
56896/25476 found host 438 live marker-backed actors over 417 distinct markers and
guest 390 over 385. Evidence is saved under
`out/windows-lab/native-act-reconcile/world-lifecycle-native-{21,22}.json`.

Three concrete world defects were corrected:

* Marker discovery previously depended on observing a native object. Thirty-two
  placements visible on the host had no guest object and could never enter this
  cache. Enumeration now rebuilds the cache from the bounded native resource
  marker vector (`2C10/2C14`, stride `238`, marker at pair `+4`), verifying marker
  index `+228` and excluding avatar classes. This permits new-act actor creation.
* Genuine host actors can share one marker. For example, powerup class 1341 had
  independent same-noun actors at markers 404 through 416. Classes 1312/1313 had
  both powerup and creature incarnations. Recreate now receives the host's noun:
  it chooses the corresponding verified native factory and permits a separate
  reverse binding while preserving the existing live forward owner. See
  [build-dc04.md](build-dc04.md) for the native reference-count and factory proof.
* A complete roster previously retired only snapshots that already had bindings.
  An obsolete unbound snapshot could remain and retry creation forever. Retirement
  now covers every snapshot at or before the roster sequence, while preserving
  newer snapshots. Old native pointers are removed only when their runtime ID
  still matches the binding, and native retired flag `+20` excludes objects waiting
  for registry cleanup. Act changes reset roster assembly and respawn retries.

`tests/adventure_mirror_tests.cpp` exercises the actual world reconciliation code
with a fake native registry. It covers unseen placement discovery, distinct local
objects for same-marker host incarnations, obsolete unbound retirement, protection
against older rosters and reused local pointers, and retired-object filtering.
The separate lifecycle fixture validates factory selection and intrusive references.
These tests establish regression behavior; the combined native build still requires
the next paired run. No running game was mutated during the reconciliation analysis.

`coop.adventure.world actors [firstMarker] [limit]` and `pending [firstMarker] [limit]`
provide bounded diagnostic pages. Status includes remote snapshots, placements,
complete-roster counts and the last marker/incarnation that failed native creation.
An old lifecycle error clears once all current snapshots have valid bindings.

The next FF25 paired run confirmed complete host-to-guest mirroring through act 2,
but found independent local guest actors outside the bindings: a read-only snapshot
contained host 438 versus guest 450. Eleven type-9 powerup markers had three guest
objects versus two host objects, and marker 177 had two guest creatures versus one
host creature. Every extra had retired `+20 == 0` and dead `+21 == 0`; ornament
parent/child links were zero. These were live duplicates, not delayed destruction.
Evidence is `out/windows-lab/native-act-reconcile/ff25-native-{21,22}.json`.

Complete roster reconciliation now removes unbound local siblings when every host
identity for that marker has both a snapshot and a current native binding with the
correct noun and runtime ID. Newer UDP snapshots add protected required identities;
a missing/newer unbound identity prevents sibling deletion until it can bind. The
existing rule still removes all actors at placements absent from the complete
authoritative roster. If an unbound object owns the native forward slot, lifecycle
removal promotes a validated retained bound peer while holding a temporary victim
reference. Native state, visibility and pose are preserved; reverse maps and
reference counts are checked by the lifecycle fixture. This prevents a deleted
duplicate from orphaning native GetObjectForMarker lookups for surviving actors.

The actual-world fixture covers bound-marker duplicate retirement, incomplete host
identity protection, newer unbound snapshot protection, and newer bound identity
preservation. `nearplayer TARGET [PLAYER]` teleports the selected captain (default
local player) beside another party captain through the existing native teleport
interface, preserving its orientation. This supports isolated revive fixtures.
Host-only `finish success|fail` requests the normal native SetState hook and
coordinator result path for controlled results/progression/save tests. It does not
establish that every authored adventure objective was played to completion.

The fresh native Auto/FF50 paired fixture verified the cleanup in act 2. Root first
observed host 438 and guest 438 with all guest actors mirrored. The final bounded
diagnostic and full read-only registry comparison captured 439 on each instance:
the `(marker,noun,class)` multisets matched exactly, with zero missing or extra
guest objects. Guest bindings, remote snapshots and complete roster were all 439,
and pending was empty. Both instances reported zero faults in the paired fixture.
Artifacts are `out/native-act-reconcile/final-comparison.json` and
`final-{21,22}-{info,status,pending,actors,native}.json`. This is a native cleanup
PASS after the previously failing act-change/spawner sequence.

The guest's vehicle tree, weapon, timer and actor counters each passed 922 and
reached 1130 at final capture; all four host counters remained zero. The counters
confirm the narrow native vehicle hooks are active on guest mirrors during the
paired adventure. Native result context was run 1, committed 1, applied 0 on both
at this capture, before the separately controlled completion test.

## Native result idempotence after coordinator end

Native SetState `F1A9C0` only ignores a request equal to its current state and a
cinematic state 4 request after state 5/6. A conflicting 5-to-6 or 6-to-5 request
can still enter native result/progression effects. The coordinator changes to
Ended before its outside-frame native callback runs, so gating the world hook
only on `active()` left a gap both before and after authoritative application.

The world now keeps a separate native result context: play pointer, run ID,
committed flag and the first applied terminal state. Observing a committed world
arms the context. Native 5/6 requests wait for authoritative application even when
the coordinator reports Ended and replication has reset. Terminal application
claims its state before calling the original method, preventing duplicate,
conflicting and reentrant native result effects. Goal snapshot application uses
the same native entry helper. Nonterminal native transitions, including state 4,
retain their native path. This is an in-memory call guard, not an acknowledgement
that the native routine awarded or saved progression.

`begin_native_attempt(play)` must run immediately before the verified native
Initialize call. It resets the result context at a real lifecycle boundary even
when the play allocation is reused. It records a loading run when present so an
early native loading failure keeps its normal path and is still applied once.
Solo initialization records no co-op run and retains ordinary native behavior.
The automatic inactive tick clears replication without clearing this context;
an explicit adapter `reset()` clears both and cancels pending native application.
`apply_result` additionally requires the actual Scenario game mode.

The actual-world Win32 fixture now tests native SetState dispatch through a fake
thiscall adapter and an isolated loopback session. It covers the Ended interval
before apply, inactive replication reset, duplicate/conflicting/reentrant calls,
cinematic pass-through, another co-op run on the same play pointer, subsequent
solo play, explicit reset cancellation, and native pre-commit loading failure.
All passed with zero failures. Diagnostics include the result run, committed flag
and applied terminal state. The later controlled paired success observed applied
state 5 on both instances, one score suppression each, and the original captains'
XP/level retained after restarting both processes; see [validation](adventure.md#validation).

Paired native initialization exposed a separate captain limitation: remote captain
puppets have no authored scenario class. Native global act setup `C070D0` assumes an
ordinary creature has a valid class act and crashes at `C0714E` when it dereferences
the missing class. Stack `F1E634 -> F1EEAA -> F1F0B1` identifies native Initialize's
SetCurrentAct path, not world goal polling or tuning. Remote captains require a
targeted class-setup bypass and spawning only after initial loading. They cannot be
recreated through NPC marker factories: the native avatar marker `data+8C` is a
singleton and the avatar factory branch reuses the existing local avatar.

## Bounded native social eligibility and relationship diagnostics

`adventure_world::is_live_mirror(object)` is a read-only predicate for the native
social callback adapter. It requires a connected guest in the current running
Scenario, a binding created in the current native act and run, matching local
runtime ID and host snapshot noun/marker, no party captain ownership, and positive
host and native combatant health. Retired `object+20`, destroyed `object+21`, stale
pointers, missing snapshots and dead actors are excluded. Eligibility does not
call native updates or commit the separate native result lifecycle. Binding act
provenance is recorded when the mirror selects or creates its local actor.

The actual-world Win32 fixture covers accepted living bindings and rejection of
host/unbound/captain/null actors, old runs/acts, stale runtime IDs, missing host
snapshots, host/native death and retired/destroyed objects. It also verifies that
eligibility leaves native result bookkeeping unchanged.

Outside-frame `coop.adventure.world relations [marker]` lists at most 32 native
creature rows, with marker, runtime ID, class, current mirror eligibility and the
verified `IsHostileToCreature` result against every present party captain. The
optional marker is an exact filter. It calls the normal native relationship
predicate without moving actors, changing AI state or forcing attacks; the native
relationship adapter's alias counter can establish that a remote captain query
passed through the argument alias hook. Queries alone do not prove autonomous
enemy targeting or a complete social minigame.

During the dc04 social regression, the generic `coop.targets objs` diagnostic
crashed through legacy Civilization `sync_entries -> all_vehicles` and squadron
`sync_entries -> all_ufos`: their build-specific getter symbols were unavailable
and the diagnostic attempted a null native call. The root added capability,
getter and manager guards in both enumeration helpers. This was a diagnostic
capability failure, separate from native adventure simulation. The preserved
crash log is `out/windows-lab/targets-crash/sporekit.22.log`. On fresh native
processes 25184 and 48260, `coop.targets objs` returned an empty result safely
from Menu on both instances. Scenario queries also passed after the paired
adventure loaded; both processes reported zero native faults. Artifact:
`out/windows-lab/native-social-ui/relations-and-targets.json`.

In that native capture, each row's hostile result agreed for the two party
captains within its instance. The argument-alias counters reached 64 on the host
and 33 on the guest. Most sampled rows were neutral. Guest class 1198 markers 140
and 173 returned true for both captains while the host returned false, so the
capture proves party argument routing without establishing cross-instance native
relationship-cache equality or autonomous enemy targeting.

An ordinary guest mouse click on Mr. Mayor subsequently opened native Talk UI,
including cinematic/text, Next pages and its completion control. The guest's
normal completion advanced both players from act 1 to 2, with zero logged faults;
no completion callback was injected. The artifact and screenshots are under
`out/windows-lab/native-social-ui/talk-result.json`. This proves that tested Talk
conversation path; full Befriend presentation and simultaneous native social
interactions remain separate validation work.

## Guest goal HUD refresh

The native goal vector can agree with the host while the guest still displays
yellow objective cards. Directly writing `goal+0` and `goal+4` does not invoke the
native presentation refresh. CountGoal's partial path calls `E16A50` at `F1AE8E`;
the completed-goal helper calls it at `F198E9`. Both pass `false` on the stack and
use the HUD at `ScenarioMode+6C -> display+148` as `this`. `E16A50` returns with
`ret4` at `E170C5`, reads completion through `F19B60` (`goal+0`) and progress
through `F19B00` (`goal+4`), and updates the objective cards. `UpdateGoals` instead
evaluates objective validity and may fail the adventure; it is not a UI refresh.

The guest applies each complete authoritative batch before calling the named
`RefreshGoalHUD` entry outside the frame. It refreshes when completion or count
changes, including partial progress, and skips identical snapshots. If the native
HUD is temporarily unavailable, a pending flag retries on the next valid goal
snapshot without losing the repaint. Diagnostics report refresh count and pending
state. The native adapter requires this symbol as part of its dc04 capability.

The actual-world Win32 fixture verifies the thiscall pointer and false argument,
the complete goal batch being visible to the callback, one refresh for multiple
changed goals, no replay for identical snapshots, retry after missing HUD, and no
mutation or repaint from a stale run. It passed with zero failures. Native visual
validation then passed on the frozen DLL: after controlled host act 2 selection
and a guest native Talk completion callback, goal 0 was complete with count 1 and
the guest HUD showed a green check and green Gallery objective text. Status was
`refreshes=1 pending=0`. The screenshot is
`out/windows-lab/native-final-regression/22-hud-partial.png`; the partial-goal
result and consolidated `relationship-hud-targets.json` are in that directory.
This is a controlled partial-goal presentation check, separate from the earlier
ordinary mouse/Next dialog tests.

The final native relationship check resolved the earlier guard discrepancy:
marker 140 returned non-hostile for both party captains on both processes and the
guest authoritative relationship counter was positive. Object diagnostics worked
in Scenario and both processes reported zero faults. This confirms the tested
host snapshot/native query override; autonomous hostile attacks were not forced.

Ordinary native Talk UI also passed the Gallery, Playwright and third act 2
conversations, advancing the pair to act 3. Carnie's act 3 conversation completed
one of its two objectives on both players. Artifacts are under
`out/windows-lab/native-social-ui/{gallery-retry,playwright,spoffit-side,carnie}`.
The remaining act 3 objective, final Mayor objective and a full Town completion
were not established by those partial conversation results.

Music's ordinary recruitment UI subsequently passed for Lotta: a native NPC mouse
click and Sing response (key 1) won the real minigame, produced OnAlly and advanced
both processes from act 1 to 2 with zero logged faults. The artifact is
`out/windows-lab/native-social-input/lotta-recruit-sing/result.json`. Earlier Dance
responses used key 2, which is a different native ability; those timeouts are not
evidence of a remaining Sing input adapter failure.

The successful recruitment exposed a separate state parity issue: the initiating
guest had native `B58=180` while the host retained only `100`. In native social mode
4, winning calls the genuine Befriend path and sets impressed bit `80` before
recruitment adds party bit `100`. The host's mode validation had rejected that
preceding mode 4 Befriend completion. The narrow fix accepts that real callback
and preserves the existing validated native Ally operation. Its actual-world
fixture checks `80 -> 180`, exactly-once callbacks and recruitment goals. The
final native retry also passed: the ordinary click/Sing win advanced both players
from act 1 to 2, with flags `180` and cached relationship 6 on both processes and
zero faults. The artifact is
`out/windows-lab/native-final-social/lotta-parity-retry/result.json`.

The final native neutral Kid Befriend test used a real body click and native Sing
responses, won the non-recruitment minigame, and left impressed flag `80` on both
processes. Its unrelated act 2 Talk objectives were unchanged and no faults were
logged. The adapter ended cleanly with three starts, three ends, zero rejections
and 622 allowed input queries. The artifact is
`out/windows-lab/native-final-social/kid-befriend/result.json`. No active Befriend
objective existed in this case, so it validates social completion independently
of goals without claiming every authored Befriend objective.

## Preview 5 colored gates and physical powerups (dc04)

Verified native paths: ActivateGate F23AB0 cdecl(captain,keyPowerup,gatePowerup),
tailcalls UpdateGoals F1C390; GateKey F21670 cdecl(powerup)->uint32; OpenGate
F238C0 cdecl(gatePowerup), returns at F23AA2. GateKey reads the native palette
property399C4F6C (`powerupDoorColor`) from powerup+18. `powerupType`1EA08337 is
serialized UInt32 type000A, not Int32. All referenced stock palette records use
groupD87454E6. cInteractiveOrnament::Cast C402B0 returns object+130 for common
powerup type08367427. Its combatant offset is170 in this executable, unlike the
older upstream header. Always use Cast rather than copying that header offset.

A gate placement can contain a respawner powerup9 and a separate closed gate11
with the same marker and noun. Keys are physical powerup12. Opening destroys
the closed object, creates native open ornament14 with the authored replacement
appearance/collision, drops the held key and destroys the consumed key. Marker
identity alone cannot distinguish these objects; use marker+incarnation and the
powerup kind. Debug `actorsat MARKER LIMIT` enumerates every object at one marker.
The old `actors FIRST LIMIT` can show parents while missing physical instances
appended later in the object list.

Interaction132 action3 requests Unlock. The host checks current run/act, owned
live captain, actual held key slot, key12/gate11, matching GateKey, range25 and
one accepted claim per gate incarnation. Broadcast acceptance before mutating
native objects. Both host and guest use the original native gate API outside
Present. Host pickup also rejects a key held by another party captain. Retired
key/gate identities ignore delayed actor snapshots, while a true respawn with a
new incarnation remains eligible. Actor flags high nibble carries powerup kind;
low bits keep combat/death/impressed meanings, and bit3 remains reserved.

Guest mirror bindings require a compatible powerup kind. A host open14 may bind
a closed11 only to run native OpenGate and rebind its resulting14. It must never
bind the same-marker respawner9. Reconstruct physical11/12/14 using verified
CreateObjectInstance, then open11 when host expects14. The marker respawn factory
creates9, so it is suitable for the respawner itself, not the missing physical
gate. The first forced-reconstruction test caught exactly this distinction;
the lifecycle ABI fixture now asserts the physical factory choice.

Final four-client Mr Puzzle checks passed guest pickup, held-key ownership,
wrong-color rejection, guest opening marker108 with key125, host opening110 with
key136, consumed keys absent everywhere, and forced open-gate reconstruction.
Gate108 and key125 share colorA55A8819; gate19 uses a different color. The later
Mystery Key at162 is ordinary powerup0, not a colored keycard12. Do not use its
Hold objective as proof of colored-gate behavior. Evidence:
`out/windows-lab/preview5-release-gates/result.json`,
`out/party-flow-re/powerup-properties.json` and `stock-gate-references.json`.
These passes cover the tested stock colored gates; arbitrary custom gate variants
and a full adventure playthrough remain unvalidated.

The later Music audit exposed another legacy diagnostic capability gap:
`coop.w2s` called the unavailable `App::cViewer::GetCameraToPoint` symbol and
crashed the isolated guest at EIP zero. The preserved log is
`out/windows-lab/native-social-input/read-only-debug-crash-22.log`. The command now
checks the manager, ray and host projection capabilities before native calls, and
guards null manager/viewer and failed ray results. The optional ray branch of
`coop.cam X Y` also checks the same symbol. The actual `debug.cpp` Win32 fixture
tests missing capabilities with zero native calls and null/failed/successful fake
camera paths; it passed with zero failures. This diagnostic failure was separate
from the successful ordinary recruitment game flow.

## Preview 6 stationary actor replication (dc04)

Verified implementation: `mods/coop/adventure_world.cpp`, exercised directly by
`tests/adventure_mirror_tests.cpp`. The classification uses native `Cast`, not the
adventure author's appearance or a copied object layout:

| Native interface | Type id | Replication decision |
|---|---|---|
| Creature | CE9F6639 | Existing dynamic snapshots, including disguised creatures |
| Vehicle | 0137E8E0 | Existing dynamic snapshots |
| Building | 0E9CB8BA | Stationary candidate |
| Ornament | 0175CDC9 | Stationary candidate |
| Interactive ornament | 03A25119 | Stationary candidate |
| Unknown class | no matching verified cast | Existing dynamic snapshots |

Timed bomb kind 10 and grenade kind 13 always remain dynamic. A common powerup
with thrown byte `+30` set, or any item in a party captain's native held-item
slot, also remains dynamic before any observed movement. This avoids freezing a
freshly thrown or carried object merely because its first sampled pose matches.
Common powerup and combatant pointers still come from `Cast`; the installed
interactive ornament's offsets differ from the older upstream SDK.

The host samples both classes at the existing 10 Hz. Stationary candidates keep
their first published pose and receive a reliable full baseline per peer,
followed by reliable changed health, effective maximum health, raw maximum
tuning, flags and relationship. Wire details and queue bounds are in
`docs/04-netcode.md`, section 4.8. Actor identity remains marker plus incarnation.
Gate opening, pickups and explosive retirement retain their tombstones and
native event paths; the stationary packet alone does not describe all powerup
state.

Pose comparison permits translation squared distance up to `1e-8` and normalized
quaternion dot-product squared within `2e-7` of one. Quaternion `q` and `-q` are
equivalent. Comparison is against the initial published pose, so small movements
accumulate rather than disappearing between samples. Any larger observed change,
or a later classification as held/thrown/dynamic, promotes that identity for the
whole current act. Promotion sends a reliable full correction and continues
unreliable full snapshots even after motion stops. Act changes, a fresh run and
new incarnations get fresh classification/baselines.

The guest stores a complete merged state for reconstruction. A partial update
cannot create an unknown identity or replace the retained transform. Native
transform setters are skipped when an existing stationary binding already has
the cached pose; a new binding or local pose drift still applies the baseline.
Future-act reliable baselines are retained until the native act switch rather
than discarded. The receiver assumes forward authored act progression and drops
past-act packets; diagnostic backward jumps within one run need a new baseline
generation before they can be treated as a supported flow.

Three edge cases have explicit regression coverage:

* Initial native testing found host count 351 and guest count 303 despite a
  complete 351-entry roster. The missing 48 were exactly four initial reliable
  batches delivered after commit but before the first world tick. That tick
  reset already acknowledged baselines while the host correctly retained its
  sent shadow. `on_message` now initializes the current run before merging a
  valid current-run packet. The ABI regression delivers all four one-shot
  batches before `tick` and verifies all 48 survive subsequent ticks.
* A bounded sender starting at map begin could repeatedly spend its 48-record
  budget on early dirty identities. Per-peer rotating cursors now resume after
  the last examined identity. The ABI regression dirties the earliest 48 of 80
  actors continuously, verifies all later baselines finish, then verifies a later
  health update still receives service.
* Pickup or explosive retirement can be the first routed event of a newly
  committed run. The shared `retire_identity` bridge now initializes the world
  run before adding its exact tombstone, so the first tick cannot erase it.
  The mirror regression exercises both module bridges, first-tick preservation,
  delayed full dynamic/current-act/future-act stationary records and an eligible
  newer same-marker incarnation. Its build log is
  `out/preview6-static-early-retirement-build.log`, with zero failures.

The Win32 mirror ABI suite builds with `-Wextra -Werror` and reports zero
failures after these fixes. It also covers verified classification, unchanged
stationary silence, unchanged dynamic cadence, quaternion equivalence, compact
health changes, cached-pose retention, native drift repair, reliable
backpressure/latest-value coalescing, rejoin baselines, cumulative slow motion,
promotion after stopping, future-act staging, malformed-packet atomic rejection,
2048-identity bounds, stale cross-channel state, tombstones and reincarnation.
Build logs include `out/preview6-static-first-baseline-build.log` and
`out/preview6-static-fairness-build.log`. Bandwidth comparisons and
large-adventure FPS results remain separate acceptance evidence; the ABI pass
does not establish those results.

The native harness `tools/tests/coop_adventure_static.py` resumes an already
committed recorded host/guest pair. It selects `replication=static` and verifies
one ordinary non-powerup object at that marker, including the guest's bound host
incarnation. It checks a two-second unchanged stationary-byte/baseline window,
guest-only drift repair without a new baseline, host movement promotion and a
second movement carried by dynamic updates. It restores the actor's original
pose, retains the same run/party and requires zero native faults. Forward act
changes are explicit with `--act-transitions`; its default targets 1,2 require
starting in act 0. Offline `--self-test` covers diagnostic parsing and identity
selection. The two-player native test subsequently passed on build 5b SHA-256
`6d97642842f1992e06cf406d1cac18294fe4e1b9d0756f73b1017fd773dbee8f`, including
cache repair without new baseline bytes, exactly one host promotion, continued
dynamic spatial movement, original-pose restoration and complete actor rosters
after forward native act switches 1 and 2. Both games logged zero native faults.
Evidence is
`out/windows-lab/preview6-bandwidth-after-puzzle-provisional2/static/result.json`.
The adjacent bandwidth window ran with six lab games active and is provisional.
It does not establish that the process count caused its observed 7.18 Hz
sampling cadence; the final two-game runs also observed that cadence.
An earlier harness
attempt rejected the console's inline PID format before gameplay; matching the
bounded `pid=` token fixed that test setup issue.

The final candidate-8 two-game Mr Puzzle run subsequently passed the same native
cache, promotion and forward-act 1/2 checks at
`out/windows-lab/preview6-bandwidth-final-puzzle/static/result.json`. Both
before/after bandwidth runs have 351 host actors and 351 mirrored guest actors.
Observed host-to-guest UDP payload fell from 155208.0065 to 25331.6398 B/s,
83.678909% less, with no stationary messages or payload during the steady window.
Its 40 dynamic actors each received 72 updates over 10.015301 seconds (7.1890 Hz).
The baseline actor payload equals 72 complete 351-actor cycles, giving an
inferred 7.1830 Hz. Each complete cycle has 54-byte records and 15-byte packet
headers, matching the unchanged full-snapshot layout.

The matched Town run retained 405 host/405 mirrored guest actors. Payload fell
from 178400.9174 to 56790.4674 B/s, 68.166942% less; its 113 dynamic actors each
received 72 updates over 10.019604 seconds (7.1859 Hz), with no steady stationary
payload. The baseline actor payload likewise gives 72 cycles, inferred 7.1842 Hz.
Configured dynamic sampling remains 10 Hz and observed cadence remains about
7.18 Hz at the measured frame rate. The 405-actor FPS regression passed at host
21.490948 and guest 21.612986 FPS, with advancing native clocks and zero faults.
These measurements count observed UDP payload, including protocol headers,
acknowledgements and retransmitted datagrams, while excluding UDP/IP headers.
The raw artifacts are `out/windows-lab/preview6-bandwidth-final-puzzle/result.json`
and `out/windows-lab/preview6-bandwidth-final-town2/{result.json,performance.json}`.
The final two-game Puzzle impairment run also passed with 5% packet loss and up
to 80 ms random jitter. It measured 25577.3279 B/s host-to-guest and 1696.0913 B/s
guest-to-host, observed 16 down/9 up dropped datagrams, and passed the linked
cached repair, movement promotion, forward acts 1/2 and complete rosters with
zero faults. Evidence is
`out/windows-lab/preview6-bandwidth-final-puzzle-loss/{result.json,static/result.json}`.

## Preview 6 explosive native paths (dc04)

The reviewed actual explosion entry is `F24730`, cdecl(powerup, combatant
attacker). It reads current class-act radius `+498` and amount `+49C`, plays
effects through `F21C00`, drops a held item, applies the native radius routes
`F24580` and `F230F0`, and destroys the source object. No contents-creation
factory occurs in this function. Stock crate `93512140` is powerup kind 15 with
amount 0; barrel `A0EBD95A` is the same kind with amount 10. Preserve the authored
native behavior rather than inventing loot or giving a harmless crate barrel
damage.

Mine `F207D0` and grenade contact `F20860` initiate their own death with damage
type 19. The later blast uses type 0 through `F204F0`. Trigger/death time is not
necessarily explosion time: chained callbacks can be delayed through `F20220`
and `F24910`, so replication observes the actual `F24730` event. Grenade throw
`F208F0` sets thrown byte `+30`, resets age float `+34`, and retains the acting
captain at `+38`. The installed throw path at `D7F9C0` passes the captain through
`F20A70`; the old upstream name describing `+38` as a target is misleading for
this executable. Self-contact is ignored for the first second.

Timer execution `F20390` and powerup tick `F24C40` both pause during the shared
hold. Cleanup at `EF0BC6` calls `F20310`, which executes all delayed callbacks
before releasing them; it does not silently discard pending explosions. Cleanup
guards therefore validate native object, incarnation and run before accepting
`F20810` or `F24910` callbacks from a cancelled attempt.

World bounds `C8A630` is thiscall(spatial, Box24* out) returning Box24*, with
`ret 4`, at spatial virtual slot `6C`. The result is six floats for world minimum
and maximum. Native explosion code uses this slot and averages the bounds into
centres. `tools/addrport/verify_dc04.py` verifies the function and the reviewed
captain/ornament vtable entries. New symbols are named in
`data/symbols/overrides_ga_dc04aee5.sym`; the verification report is
`out/preview6-explosives/verification.json`. The explosive Win32 ABI fixture
`tests/adventure_explosives_tests.cpp` reports zero failures. Four-player natural
mine acceptance with friendly fire 0 subsequently passed: every captain lost
exactly 10 HP, every guest received one effect event, a distant guest claim was
rejected, a new authored respawn was usable by the host and observers outside
that second blast radius were undamaged. Evidence is
`out/windows-lab/preview6-explosives-smoke3/result.json`. This result covers that
mine case; other explosive kinds and friendly-fire settings need their own
acceptance evidence.

Native barrel testing found another ordering case: host detonation count became
1 while guests remained 0 because the short-lived physical object had not yet
acquired a host-identity binding. A missing-binding tombstone was sufficient to
prevent stale state but could not present its effect. `ExplosionEffect` at
`F21C00` is now verified as cdecl(Vec3* position, Quat* orientation, float scale,
uint32 effect), returning at `F21D2F`; native `F24730` calls it at `F2486C` before
radius damage and destruction. The host captures those actual arguments rather
than manufacturing an alive baseline or guessing artwork from a sibling.
Subkind 146 carries the 36-byte descriptor described in `docs/04-netcode.md`,
section 4.9. With no exact binding, a guest displays that helper once, tombstones
the host identity and leaves native object removal to safe reliable roster
reconciliation. With an exact binding, it retains native explosion cleanup and
uses the captured host presentation. It never destroys an unbound same-marker
object by guess.

World tombstones and event deduplication are distinct fences. Module phase and
serial checks prevent duplicate effects, including a later event serial after a
new local binding appears. World tombstones reject delayed full Actors128,
Static136 and staged future-act state, so the spent host identity cannot bind
again. An older complete roster cannot create a binding; without the retired
identity's state it cannot declare that marker complete for sibling removal.
The Win32 fixtures cover missing binding, repeated/delayed events, a newer
same-marker local object and invalid descriptors. Build 5b subsequently passed
the four-player barrel cases at friendly fire 0 and 100, including one native
effect event on each guest. Their exact DLL is SHA256
`6d97642842f1992e06cf406d1cac18294fe4e1b9d0756f73b1017fd773dbee8f`;
the backing case files are
`out/windows-lab/preview6-explosives-build5-matrix/barrel-ff0.json` and
`barrel-ff100.json`. These completed cases do not turn that partly failed parent
matrix into a whole-suite pass.

`tools/tests/coop_adventure_explosives.py` accepts optional `--loss` as a packet
loss fraction and `--jitter` as a maximum random delay in seconds, using one
loopback `NetworkProxy` per guest on host port +1..+3. Default traffic remains
direct. `--seed` makes impairment deterministic per guest; reports include
per-case byte/packet/drop deltas and total proxy counters. Four-player harmless
crate cases at friendly fire 0 and 100 passed on build 5b with 5% packet loss and
up to 80 ms jitter; the completed result is
`out/windows-lab/preview6-explosives-build5-crate-loss-plain2/result.json`.
That run establishes shared crate consumption and presentation under impairment,
not a crate destruction objective.

Timed-bomb kind 10 repick has a separate native lifecycle rule. A genuine native
OnPicked notification clears only the module's thrown guard, permitting a later
throw of the same incarnation. It retains the running fuse and armed state.
An already-held duplicate PickUp request does not invoke native OnPicked and
cannot reset this guard. ABI coverage passes. Candidate 8 also passed genuine
four-player guest-drop/host-repick/drop cases at friendly fire 0 and 100: the same
incarnation was thrown twice, retained the original 15-second fuse and detonated
once. Its native armed-recovery cases held a running fuse through 16 wall-clock
seconds of Talk, cancelled without disconnecting, retried immediately and
launched again through guest Replay/host Done with explicit readiness. The whole
four-case result is
`out/windows-lab/preview6-explosives-final8-safe/result.json`, against DLL SHA256
`075ee6000269235d8a3c41572e01296b60a71e953dc70806a4d3c1ad3cc06f75`.

The completed interaction index is
`out/windows-lab/preview6-explosives-acceptance/result.json`. It lists 18 base
kind/friendly-fire rows and two impaired crate rows with exact DLL hashes per
case. Its two repick checks reuse the long-bomb base artifacts, giving 20
distinct case files. Earlier partly failed exploratory matrices remain failures;
the index accepts their completed individual cases rather than relabelling the
aggregate attempts.

The installed death-message branch
at `F1C7C9` handles message `01622184`: it reads victim GameData class `+1C`,
matches unfinished current goal type `+24` equal to 3 and target class `+28`,
then calls native CountGoal at `F1C82B`. CountGoal increments `goal+4` at
`F1ADC4`. The guest adapter suppresses physical CountGoal callbacks and accepts
host goal snapshots. These fields alone are insufficient to retarget a live
Talk goal: the first such crate test immediately failed the host adventure
before a detonation or meaningful objective count. Evidence is
`out/windows-lab/preview6-explosives-build5-crate-loss/21-commands.json`.
`configure_crate_kill_goal` now refuses that invalid setup before native access.
The unmodified fixture's original first goal is Talk to class 1037, marker 158,
and cannot prove crate destruction objectives.

`build_crate_goal_fixture` instead builds a separate immutable authored goal
under root `7F00600E`, leaving the original 13 fixtures unchanged. The source
scenario embeds a DBPF at byte 7830; its single RefPack record decodes to 614546
bytes of ClassSerializer data. Native attribute constructors at `138B190` map
`07114B5F` to type, `07114B60` to targetClassIndex, `071D3B1D` to requiredCount and
`0733DF78` to targetClass2Index. Their native field offsets and names agree with
the decoded attribute tables. The builder changes the first authored goal's
type/class/required values at decoded offsets 436/440/444 from 2/1037/1 to
3/0/3, and the class-zero palette at 205374 to stock crate `93512140`. It preserves
all other decoded fields, including flags, dialogs and class tuning, rebuilds
the inner DBPF uncompressed and updates the outer length word. No counter or
completion result is injected. Ordinary native loading must initialize the new
goal before the test starts.

The manifest/package are in `out/preview6-fixtures/crate-goal`. Menu registration
and private inspection reuse the existing reviewed packed-database/resource APIs.
`inspect_crate_goal_fixture` requires native authored goal `(3,0,-1,3)` and the
crate class before any gameplay test. Offline decoding/package/schema checks
pass and the native loaded goal schema matches. The later native acceptance
established that a stock powerup crate is excluded from authored Kill objectives,
so no host/guest count-1/count-2 result is claimed.

The first authored-fixture attempt on build 7 failed before any crate shot or
detonation, while waiting for beam-down confirmations. The guests retained
native state 6, elapsed 0 and Kill/class0/required3 with count 0 and completed
false. This was early native validation/failure, not successful completion or
objective autocomplete. The observations are in
`out/windows-lab/preview6-explosives-final7-objective-recovery/native-objective-observations.json`.
Offline inspection found a product initialization dependency in the held powerup
update path. Scenario tick calls play-mode
Update/CheckGoalProgress at `EF308C`, before delayed timers at `EF312C` and
TickPowerups at `EF3138`. Native `GetObjectsForClass` at `F40BA0` detects parent
powerup kind 9 at `F40C8A`, substitutes the physical child through `F20BF0`, then
skips a null child at `F40CA0`. A respawner alone therefore cannot satisfy target
validation. The native attackability filter `F251F0` includes stock crate
`93512140` and barrel `A0EBD95A`; Kill validation has a separate powerup exclusion
described below. Class-zero private native parses already show invulnerability
false in all four acts.

Native powerup construction `F212C0` initializes both timer floats `+24` and
`+28` to zero at `F21589/F2158E`. The authored class spawn delay is added to `+24`
at `F243EE/F243F2`, immediately before child construction, rather than delaying
the first child. Calling the existing TickPowerups original with delta zero can
therefore service initial creation while preserving elapsed time. Its respawner
path subtracts zero; its grenade path adds zero to age. The mine helper
`F20DC0` maintains the glow/sphere effect rather than activating the mine.
Link cleanup `F24080` still runs. Contact, damage and delayed fuse callbacks
retain their separate shared-hold guards. Dropping the entire TickPowerups call
during Intro can prevent native initial goal targets from existing before
Continue. Build 8 subsequently showed parent 9 incarnation 653 and physical
crate 15 incarnation 902 before Continue on all four clients, with matching host
identity. Build 7 had only the parent. The delta-zero setup path therefore runs
and establishes the initial child. The authored fixture still failed with zero
counts/detonations because of a separate native validation rule.
Offline disassembly and its evidence boundary
are in `out/preview6-crate-goal-analysis/{native-paths.asm,findings.json}`.

A read-only host trace then captured goal failure 10 (`0A`) and play-mode `+B0`
equal to 3 at the transition from Intro state 1 to failed state 6, before cleanup.
Its artifact is
`out/windows-lab/preview6-crate-goal-pair-diagnostic/crate-native-validation-run1.json`;
the adjacent `crate-initial-actors-run1.json` confirms all six physical crate
children existed. The reason is the native Kill powerup exclusion: after the
attackability test, `F40515` calls `F255B0` and rejects its true result. `F255B0`
tests normalized category `CF56099A`. The palette-key resolver `EEC980` returns
pseudo resource type `F0000001` when a typed `powerupType` property exists
(literal name at `148AD24`); `EECD40` maps that type to `CF56099A`, and `F255D0`
returns reason `0A` for it at `F255FD`. Stock crate kind 15 follows that route.

This is an expected vanilla authored-goal restriction, independent of damage and
destruction support. Co-op preserves it: there is no invented goal override,
counter injection or loot. The four-player native crate destruction, shared
removal/effects and packet-loss cases remain valid separate evidence. The
authored Kill fixture and failed attempts are retained as proof of the boundary,
not as objective count passes. An offline scan of all four authored acts also
confirmed that later goals target classes 1033/1037, never class zero; unrelated
later-act goals did not cause this rejection.

## Preview 6 native interaction fixtures (dc04)

`tools/tests/adventure_interaction_fixture.py` builds an immutable DBPF of 13
copies of installed Mr Puzzle scenario `C2C7B154`. In each scenario it changes
only the four-byte class-zero palette asset at offset 27341. The source scenario
and its saved captain restrictions remain intact; test root ids are
`7F006001..7F00600D`, type `366A930D`, group `408A0000`. Actual native class parsing
confirmed the following resources and powerup kinds:

| Fixture | Palette asset, group D87454E6/type 00B1B104 | Kind |
|---|---|---|
| mine | F70AB170 | 6 |
| barrel / crate | A0EBD95A / 93512140 | 15 |
| long / ordinary / short timed bomb | D41F216B / C7C9CC8D / 48B8E425 | 10 |
| grenade | 812057C5 | 13 |
| health / energy | 71B5854F / 56CCE16B | 1 / 2 |
| speed / damage / armor | B50BD5CF / A340633B / A880F62B | 3 / 4 / 5 |
| teleporter | 90C48D09 | 8 |

Class zero has physical markers 108, 110, 112, 116, 117 and 118. The copied gate
class acts contain radius 10, amount 10, health/speed/damage multipliers 1 and
spawn delay 5. The fixture manifest records the inherited tuning; it is not a
claim that every stock palette uses those values. In particular, `tune_fixture`
restores crate amount 0 before the harmless-crate test. Native palette durations
are 15/7.5/4 seconds for the three bombs and 15 seconds for speed/damage/armor;
damage and armor palette amounts are 2. Amount or duration cached in a spawned
powerup's property list is not rewritten merely by changing a class-act field.
Source appearance overrides remain, so these fixtures validate native semantics
without promising default stock artwork.

The scenario resource's runtime class map is at `+2BF4`: vector entries have
stride `27E8`, class index at `+0` and class data at `+8`. Its marker map is at
`+2C10`, entry stride `238`, marker data at `+4`. Class-act vectors are `+70/+74`
with stride `4E0`; radius, amount and spawn delay are `+498`, `+49C` and `+4A4`
within the act. Private resource parses have unassigned marker ids `FFFFFFFF` at
marker `+228`; their array ordinal is only a diagnostic until actual native
adventure loading assigns runtime ids.

For a deterministic teleporter fixture, owned marker 108's destination position
is `+1E8`, orientation `+1F4`, scale `+204`. Setting class secondary-graphics
override `+48` to 1 selects the authored marker exit rather than model bone
lookup. Native `F211E0` requires destination distance greater than
`3 * (entryScale + 1)`. The helper chooses an exit 30 units away unless explicitly
given one. This establishes fixture setup, not a multiplayer teleport pass.

The helper verifies the recorded lab profile, process id and dc04 executable
hash before native access. Registration/release require the native menu and no
active attempt. Packed database registration holds one caller reference until
`release_fixtures`; resource inspections release their normal native resource
holds. Native scratch allocations are read/write, non-executable and freed by
the context manager. `call` and `mem.write` run on SporeKit's main-thread command
path, not its outside-frame render-safe path. Use native interaction commands
that schedule rendering/effects outside Present for gameplay actions; this
helper confines tuning to owned lab class/marker/plain gameplay fields.

Verified helper evidence: all 13 private resources parsed, idempotent package
registration reused its held database, normal unregister/release made subsequent
private resource lookup fail, and the isolated menu process logged zero native
faults. It did not launch an adventure or start a party. Artifacts:
`out/preview6-fixtures/manifest.json`, `private-inspections.json`,
`stock-runtime-classes.json` and per-process `registration-*.json`. The energy
fixture is diagnostic coverage only; it does not expand the requested product
pickup scope.

A later four-client mine smoke exposed a fixture staging pitfall: placing every
captain directly on respawner marker 108 during Intro prevented its physical
mine from spawning. Native `F24240` calls obstruction test `F21F40` before
creation and retries when the placement is blocked. Moving the captains away
allowed physical mine 108 to appear on the host and all three guests. This was
not a stationary replication or tombstone deletion. Host static sampling never
deletes native objects, retirement callbacks only discard cached identities, and
roster-driven native removals are guest-only. The harness must stage captains
away from a parent respawner and wait for its physical kind before testing
contact. A separate direct native activation of already spawned mine 110
incremented host activation/detonation and all three guest detonation counters;
that narrow callback result does not establish ordinary collision or the full
explosive acceptance suite.
The direct probe and explicitly transcribed manual observations are in
`out/windows-lab/preview6-mine-probe/notes.json`; the raw per-game command logs
record the physical respawn after moving captains away.

## Preview 8 held items, receiving NPCs and physical catches (dc04)

The old container-only Give hook does not cover creature recipients. The native
receiving-creature behavior starts at `D8A210`, ticks at `D84120` (behavior table
`1585338`, tick pointer `1585340`) and ends at `D8A470`. It uses order type7 under
mask `0/40000`, donor object reference `order+2C`, and donor hand index `order+10`.
Initialization resolves the donor through `D99830`, reads its hand through
`C0FA00`, checks receiver eligibility with `C13DC0(item,true,false)`, and releases
the receiver's existing slot0 item. The actual transfer at `D8432C/D8433E` drops
from the donor and picks into receiver slot0 with native replacement enabled.
`D99830` is cdecl(order,interfaceID), not a one-argument function. The mod reuses
the existing verified one-argument `SocialOrderTarget=D998E0` wrapper instead;
that wrapper supplies `D0036E08` internally and checks a live targetable donor.
Guest NPC Update suppression prevented this behavior from advancing. The guest
now observes only that explicit receive order outside Present, sends the local
captain's intent, and clears the order with `BC9A60`, thiscall two masks, `ret8`.
It does not enable independent guest NPC AI. The host performs native recipient
eligibility and Drop/Pick; its actual native callback owns Give/Hold objectives.

Native full throw begins at `D7F710`; `D7F9C0` is an interior block, not a safe
function entry point. It drops through `C15EB0`, retains the throwing captain's
combatant in interactable `+10C` with `AE6610` at `D7F9FB`, applies its calculated
impulse at `D7FCD4`, and calls physics pair grace `B4C700` at `D7FCF4` with 0.5 s.
`AE6610` is thiscall(storage,combatant), `ret4`, and retains/releases through native
combatant virtual slots 60/64. `B4C700` is thiscall(physics,captainSpatial,itemSpatial,
float duration), `retC`. Remote throws restore these operations. Reference storage
is never assigned directly and no new explosion or friendly-fire rule is added.

Reliable world subkind 137 carries a complete held-item relation snapshot, bounded
to 2048 entries, at most 40 per chunk: run, revision, act, total, first, count, then
item identity, holder identity, captain ID (255 for NPC) and hand index. Unchanged
relations do not recur; the host coalesces newer snapshots under the 64-pending
packet limit. Guests commit only complete valid revisions, retain missing endpoint
bindings for reconstruction, and replay native attachments outside Present. Actor
pose correction skips native held spatials. Native accepted pickup/drop/Give events
also update the retained relation immediately, so an older completed snapshot
cannot reattach a newly dropped item. Ordinary throw requests use subkind 138 with
run/event/act, player, exact item identity, native impulse and add flag; sender,
ownership/recent native drop, range, alive state and finite bounds are revalidated.

Automatic catching is host-owned. It sweeps the physical item's bounding-box
center between samples against captain bounds expanded by the item's extents,
uses earliest contact then player ID, and requires native pickup eligibility and
a free hand. It preserves the original thrower's 0.5 s grace. Ordinary items and
native-repickable timed bombs can be caught; contact grenades keep their impact
behavior. Timed-bomb OnPicked only resets its throw guard, preserving its armed
state and original fuse. A stopped item leaves the flight tracker after 1 s.
Native calls and authored marker/incarnation identities remain mandatory; no
long-range attraction, invented item copies, or guest-owned catches are used.

Win32 actions, world-mirror and explosives fixtures cover the adapter ABIs,
initial NPC attachment sampling, delayed binding and reconstruction, duplicate
and stale state, native recipient rejection, swept contact, simultaneous claims,
full/incompatible/downed hands, thrower grace, contact-grenade exclusion and timed
bomb repick without fuse restart. These fake-native tests do not establish the
actual Phyrus Farmer rendering, real mouse Give order, or live throw trajectory.
The subsequent candidate-2 private Windows pair established those native item
paths separately: three Phyrus Farmers displayed their authored hammers on the
guest, and removing each guest-only native attachment was repaired to the exact
host item/owner/slot relation. A guest's ordinary mouse click gave Town key 354
to Farmer 130, removing it from the donor inventory, adding it to the NPC hand
on both clients and advancing the host Give counter exactly once. Key 355 then
passed guest-to-host and host-to-guest through native physical impulses and
automatic catches. The same two-way test with a native timed bomb retained one
physical identity and detonated 15.235 simulation seconds after initial pickup,
without restarting the authored 15-second fuse. All four checks logged zero
native exceptions. Evidence is under `out/lab/items-town-hammer-native`,
`items-town-give-native-ui`, `items-town-key-catch98` and
`items-timed-bomb-catch2`. The throw fixture drives the rendering-safe native
impulse adapter; a complete mouse-driven native throw animation is not asserted.

Native act population is a fixture prerequisite: the debug `SetCurrentAct`
command updates the current act and existing actors but does not reproduce the
whole native act-completion population path. Town's farmers were absent after
debug act selection. Completing the real MoveTo objective at marker 169 populated
the citizens, bringing the host and guest to 437 actors. Discover the Farmer
through the live species profile name; its authored name override is `***`.

The new Give,
Throw and detachtool debug probes require an explicit `winlab_` profile and run
outside Present; the native item harness never edits a loaded profile.

## Native confirmation and revival HUD (preview 8)

The recorded dc04 Intro and Talk layout uses check control `0445C99D` and bottom
letterbox `045023D0`. Their rectangles are relative to nested UI parents; use
`sui::screen_rect`, rather than treating the stored area as a screen coordinate.
The Intro narrative label is `0445C9AB`. Talk text uses the native speech panel
at the top of the screen. A retained reference does not imply that a control's
ancestors remain visible; the HUD searches visible branches only.

`adventure_flow::confirmation_view` exposes connected run participants and
host-acknowledged confirmations. A local sent confirmation remains pending
until its acknowledgement arrives. The HUD adds owned native text labels in the
bottom bar, immediately left of the check, with one column per participant and
bounded player names. During Intro it leases a smaller narrative-label area to
reserve the bottom rows; the original area is restored on release or cancellation.
Check-button enablement belongs to a `(run, scene serial)` lease, so consecutive
Talk scenes cannot inherit a disabled check from an earlier confirmation.

Shared Talk gates the native final caption handler before its UI cleanup;
holding `TalkFinish` alone is too late. Intermediate Next pages remain local.
After party release, the matching native cinematic-end message performs cleanup
and the host commits the Talk objective once, with native objective callbacks
suppressed. Run/scene ownership, duplicate-end rejection and a bounded settling
fence cover asynchronous caption advancement. The verified controls/flags and
pending native acceptance are recorded in [the scene RE notes](adventure.md#native-talk-final-page-ownership-dc04-preview-8).
F9 remains available; confirmation no longer opens
the full co-op dialog automatically. Opening that dialog temporarily removes the
owned letterbox labels and restores their leases; closing it refreshes the same
scene without clearing confirmations.

The revival overlay reads the existing authoritative Captain state outside
Present at 10 Hz. It displays the existing two-metre radius and three-second
hold, progress, and optional automatic-recovery countdown. It does not estimate
progress from wall time. Overlay drawing projects a cached world-space ring and
draws a progress bar during `SK_EV_OVERLAY`; it performs no native UI/resource
lookups there. A D3D9 state block is created outside Present, captured/applied
around drawing, released before device Reset and on unload, and recreated
outside Present. Ordinary native visual updates continue while downed.

Portable layout/flow tests and native acceptance are distinct. Record actual
screenshots, input checks, cancellation/retry, clock holds and device-reset
results in the preview's validation report rather than inferring them from
rectangle or countdown unit tests.

## Guest creature presentation and NPC clips (dc04, preview 9)

The preview 8 ownership filter rejected the complete living guest
`cCreatureAnimal::Update=C0AE30`. Updating a creature's spatial position alone
does not refresh its animated model. In the native Crystal Mine baseline,
same-incarnation host/guest spatial positions matched while guest rendered
models were about 45 and 95 metres from those positions. Native model root
motion also creates smaller legitimate offsets on the host, so fixed acceptance
compares against that control rather than requiring every model origin to equal
its spatial position exactly. The read-only model chain is creature `+B54` to
AnimatedCreature, then `+180` to its model and model translation `+C`.

Preserve the enclosing native creature Update and filter only the existing
verified `NPCTickAI=C09570` hook. Its float argument is passed by Update at
`C0AF63`; Update's later native presentation/maintenance dispatch remains intact.
This is necessary but was insufficient in native candidate 2: Update and clip
replay counters advanced while the animation root and model stayed behind.
Mirrored creatures also require the combined native pose operation described
below; separate spatial setters do not publish the root-pose change.
The host, party captains, ordinary solo play and native death cleanup keep their
previous AI behavior. Core puppet suppression still runs first. The explicit
filter is installed and cleared with the mod. Diagnostics distinguish actual
native Update calls from suppressed NPC-brain dispatches; pure filter queries
do not increment either count. The core Win32 fixture reproduces a stale visual
pose with the former broad filter, then advances presentation through successive
replicated poses with the NPC brain still fenced.

The existing animation listener previously forwarded only the local avatar.
World message 139 now carries host NPC visual clip requests: run, act, serial, shared
simulation time, exact marker/incarnation, clip ID, block and blend/target
provenance. The host coalesces the newest clip per actor, sends at most two
12-event batches per peer per 100-ms actor tick, and adds nothing when that
peer already has 64 reliable messages pending. Captures expire after two wall
seconds; sorted global serials prevent duplicate or older playback without an
unbounded incarnation-history map. The guest retains at most 2,048 coalesced
events per bounded act and applies at most 32 per outside-frame pass, after
normal binding/reconstruction. Changed runs/acts, replaced incarnations,
unbound/dead actors, scene holds and stale timestamps cannot replay a clip onto
an unrelated live actor.

Replay uses the existing verified `PlayAnimation=C12470`, thiscall
`(creature, animationID, block, disableBlend)`, `ret0C`. Its native body selects
the animation resource and dispatches into AnimatedCreature clip/channel setup;
it does not issue an ability or attack order. Targeted-call provenance is
retained, but replay selects the visual clip rather than reconstructing an
ability target, so targeted aiming is approximate. The existing listener runs
before the native call, and native failure may return zero; whether a valid
AnimIndex zero is possible has not been established. Events therefore represent
observed host requests rather than proven successful clip handles. The guest's
normal native clip lookup still decides whether its matching model can play
them. The existing wrapper suppresses listener echo. A separate
visual guard rejects incidental damage callbacks before guest hit forwarding;
the broad `g_applying` gameplay bypass is deliberately absent. Native NPC damage
and decisions remain host-owned. This is clip presentation, not arbitrary
ability, projectile or gameplay-callback replication. Static actor transforms
and the existing dynamic actor cadence are unchanged.

The actual world fixture covers accepted clips, duplicate/nonhost/old-run and
old-act packets, delayed bindings, replaced incarnations, shared-time expiry,
coalescing, congestion, pause holds and incidental visual damage rejection.
Native fixed-build motion, combat and added clip traffic are separate acceptance
checks recorded in the preview validation report.

## Creature spatial pose to animation root (dc04, preview 9)

The installed animal constructor at `C06996` assigns spatial vtable `1469EC0`
to creature `+C0`. Its separate position/orientation setters update the spatial
subobject and the public AnimatedCreature pose, but do not set the native
root-pose dirty flag. Ordinary animation processing can consequently put the
old animation-root coordinates back into the spatial object. Asynchronous
spatial reads can alternate between that old root and the replicated pose while
the rendered model remains at the old location.

| Native path | Verified installed behavior |
| --- | --- |
| Spatial vtable `+38`, `C0DEA0` | `thiscall(spatial, Vec3*)`, `ret4` at `C0DECB`; calls `C88800`, then copies position to spatial `+A94`'s AnimatedCreature `+4`. |
| Spatial vtable `+3C`, `C0DF10` | `thiscall(spatial, Quaternion*)`, `ret4` at `C0DF85`; calls `C88860`, then updates the AnimatedCreature orientation. |
| Spatial vtable `+44`, `C0C040` | `thiscall(spatial, Vec3*, Quaternion*)`, `ret8` at `C0C05C`; calls combined base setter `C888D0`, then sets spatial `+A9D`, which is creature `+B5D`. |
| Graphics consumer at `E8C315` | Tests creature `+B5D`; when set, calls `A04710(animated, true)`, clears the flag and creature `+330`. This is an internal call site, not a new callable entry. |
| Animation pose refresh `A04710` | Passes AnimatedCreature `+4`/`+10` to core synchronization `9B39F0`/`9B37F0`, then reaches model refresh `A04570`. |
| Model refresh `A0458E`–`A045B4` | Loads AnimatedCreature `+180`, copies core `+18/+1C/+20` to model `+C/+10/+14`, and marks model Transform `+8`. `A05B60` independently confirms the model pointer, flags and retained reference layout. |

The existing `game::teleport` uses this same two-argument spatial vtable `+44`
operation for captains. Replication now uses it for creature poses, retaining
the native combined-operation side effects and the existing attachment skip.
Other actor types retain their existing setters. There is no direct write to a
render model, animation core or dirty flag, and no call to an internal graphics
consumer. NPC AI authority remains fenced at `NPCTickAI`.

Native candidate 2 (`778a729c5aaea28afb114fdd82b5b3be98968384d31f514ef72be5cff797865b`)
provided a controlled one-NPC check in the real Maxis Crystal Mine. The fixture
passed guest NPC 105's current unmodified spatial pose through its actual
vtable `+44`, whose target resolved to preferred address `C0C040`. The immediate
sample had dirty flag 1 with a 71.39-metre model/root mismatch. After one
rendered frame the engine cleared the flag and aligned the animation core,
model and replicated spatial pose; subsequent frame checks retained zero
measured mismatch. Untreated NPC 104's model remained at its old coordinates.
The fixture wrote only disposable argument memory and invoked the reviewed
native operation; no model/core/flag field was patched. Both games logged zero
exceptions and returned to native menus afterward.

Evidence: `out/lab/preview9-mine-candidate2-vt44-probe/result.json`, before/after
screenshots and command logs; static installed disassembly is under
`out/lab/preview9-mine-identity/`. The preceding 25-second candidate-2 trace still
failed full model coherence despite receiving/playing 116 host clip requests;
it must not be reported as a corrected-build pass. A fresh corrected-build
trace, actual animation/health/objective observations and visual inspection are
required separately.

The subsequent corrected candidate-3 trace, loaded on both recorded lab clients
as SHA-256 `e3c2da964abd2908d2836c3258593413597472812f1a964a192adc7aae366d83`,
passed those targeted creature checks over 25 seconds of real Crystal Mine AI.
Its asynchronous within-client spatial/model error p95 was 1.670 metres on the
guest and 0.609 metres on the native host control, compared with 212.285 metres
on candidate 2. Thirteen measured moving incarnation histories, including later
spawns, now showed guest model movement following the spatial motion instead of
remaining at old authored positions. Actual guest native Update advanced by
12,105 calls while 10,208 NPC-brain dispatches were suppressed; 119 host visual
clip requests were sent, received and played. The native generator fell from
3,000 to 2,976.1499 HP on both clients and both native Kill counters advanced
from zero to nine. Inspection of paired frame-11 screenshots showed the real
defenders and hounds fighting in corresponding locations. Neither client logged
an exception.

This is targeted creature presentation and naturally occurring combat evidence,
not complete combat or adventure coverage. Ordinary host W input did not meet
the separate moving-host/stationary-guest prerequisite because captains still
overlapped at spawn; model/root coherence does not depend on that condition.
A subsequent ordinary guest body-click/ability attempt was inconclusive because
the defended generator was naturally destroyed during input, entering native
failure at simulation 213.666 seconds. Terminal cleanup changes to a target's
HP cannot establish an accepted attack. Vehicle model-root parity was not
measured by the creature reader. Evidence is under
`out/lab/preview9-mine-candidate3-trace/` and
`out/lab/preview9-mine-candidate3-guest-attack/`; the pair returned to clean native
menus afterward in `out/lab/preview9-mine-candidate3-handoff/`.

A cold-start ten-second smoke on the final candidate-4 DLL
`cb1c3f36138ef6a58f187cbbac07f15d213c874b9ab0796b55b1f42a7a884c48`
also passed, with 20 moving NPC incarnation histories. Guest model/spatial error
p95 was 1.670 metres versus 0.473 metres on the host, and rendered models followed
41–114 metres of spatial travel. Guest native Update advanced by 4,950 calls,
with 4,500 NPC-brain dispatches suppressed; 46 clip requests were sent, received
and played. Unlike the earlier overlapping-spawn trace, the host moved 81.72
metres under ordinary W input while the idle guest drifted 0.661 metres, exceeding
the fixture's strict 0.2-metre stationary threshold. This short smoke ended before
any kills or generator damage; the combat deltas above belong to candidate 3.
Both final loaded shadow hashes and native frames are retained in
`out/lab/preview9-mine-final/`. The active real wave was handed directly to the
separate final pause fixture.

### Ordinary guest attack fixture entry checks

The test-only `coop_adventure_guest_attack.py` uses the existing verified
`IsHostileToCreature=C0B9C0` and `CreatureTarget=C0EFD0` getters to establish
native hostility and that an ordinary body click selected the intended creature.
Both entries remain covered by `verify_dc04.py`; no new production entry is
introduced. Owner placement and read-only camera/health/identity observations
reuse the previously reviewed fixture interfaces.

The upstream `Simulator::cCreatureGameData::GetAbilityMode` at `D2E490` was
independently checked in the installed dc04 executable. Its complete getter is
`A1 94 E3 69 01 C3`: `mov eax,[0169E394]; ret` at `D2E495`, so the ABI is
`cdecl()->int` with no stack arguments. Native stance values are zero for social
and one for combat. The `0169F374` global recorded in older creature notes belongs
to another executable build; it is not the dc04 address.

Before invoking these three getters, the fixture checks their live entry bytes.
The stance getter's absolute data operand is adjusted by the executable's ASLR
delta; the other two prefixes retain their relative-call bytes. This is a
test-only check in addition to live PID/executable-hash validation. Unnecessary
ability-vector virtual calls and ability-definition field reads were removed.
The fixture invokes no attack or damage function: attack input remains a normal
native mouse click and key 1, and a passive loopback proxy distinguishes unique
native Hit events from reliable retransmissions and captain callbacks. Static
getter assembly and a fresh 163-function/four-global verification report are in
`out/lab/preview9-guest-attack-entry-verification/`. This entry audit does not
itself establish native attack acceptance.

The single bounded final-candidate-7 attempt loaded SHA-256
`b939580707f00867b823f01d0ab3f06dde8384c38213074d2bf8504a38d5d3e2`
on both lab clients and passed those live entry-byte checks. It stopped before
target-click/key1: after normal camera/stance settling the naturally moving
Crystal Hound was 15.802 metres from the staged captain, outside the fixture's
10-metre guard. The exact marker-7/incarnation-7162 target still had 33 HP on both
clients, but no ordinary guest-hit acceptance can be inferred. The result is
explicitly inconclusive in `out/lab/preview9-guest-attack-final/`. Cancellation
restored both native galaxy menus with no faults, and the lab was closed before
installation; no broader native combat pass is claimed.

## Delayed captain health across unanimous pause (preview 9)

A validated guest environmental hit can arrive after the host has entered a
unanimous menu pause. The guest may have emitted it while only its own menu was
open and the host simulation was still running. The world receiver already
deduplicates hit serials, but its captain damage/heal callback reaches the
coordinator directly, so the native actor-update pause fence alone cannot stop
that delayed callback from changing captain health.

The coordinator now admits validated captain damage and healing into one
run-scoped FIFO while its aggregate pause predicate is true. New callbacks also
join an existing queue after resume, preserving their admission order. Shield
reduction is evaluated once at admission, and retained authority event IDs are
applied once after resume, at most 64 entries per coordinator tick. Snapshots
and downing remain unchanged until that application. The queue is capped at
256 entries; overflow cancels the attempt with a synchronization diagnostic
rather than silently dropping an accepted hit. Cancellation, reset, preparation
replacement and terminal results clear it; leaving players remove queued events
whose victim or captain source has departed.

The real-session coordinator fixture covers delayed damage and healing while
paused, exact-once resume, one-time shield reduction, deferred lethal downing,
disconnect pruning, cancellation/reset/retry and bounded overflow recovery.
The actual world receiver fixture separately checks that duplicate delayed hit
and heal messages invoke the source API only once, and rejects an old-run
message. These are deterministic fixture results; fresh native pause and
combat acceptance remains separately recorded in the validation report.

## Delayed social completion across a shared hold (preview 9)

A genuine guest Befriend/Ally completion can already be reliable or queued when
the final player opens a pause menu. Its former per-message native callback
checked captain life and target eligibility but did not recheck the shared hold,
so it could change native relationships and count an authored goal while paused.

Social completions now enter one run-scoped FIFO with one generation-checked
outside-frame drain. The drain checks the hold again before each action and
retains accepted work until Resume. Repeated pending completions for the same
sender, act, type and exact target identity coalesce; Befriend followed by Ally
retains its order. After Resume, the existing native path rechecks run, act,
captain life, target incarnation, health, proximity and social/recruitment
eligibility before any relationship or objective callback. Reset/cancellation
clears retained work and invalidates an already queued drain. Processing is
limited to 32 actions per pass and retention to 256 entries; overflow cancels
with an explicit social-synchronization diagnostic rather than dropping an
acknowledged valid action silently.

The actual-world Win32 fixture covers admission before the final hold, an
already queued drain held before execution, duplicate/coalesced delivery,
exact-once native relationship and matching-goal application after Resume,
replaced act/run/incarnation, invalid source life, reset/discard, stale queued
generations and bounded overflow. This is fixture evidence, separate from the
native Mine and unanimous-pause runs in the validation report.

The same queued boundary applies to guest actor hits and item pickup/drop/give/
unlock actions. Previously an admitted hit consumed its applied serial before
the native damage filter rejected it during a hold, and an item callback simply
returned while held. Both paths now share a bounded 256-entry FIFO so their
source arrival order survives a pause. Hit admission and native-application
serials are separate, preventing duplicate reliable events from filling the
retained queue or applying twice. The existing hit packet is unchanged: its
current native act is captured at admission, and its host actor incarnation is
rechecked after Resume. Item packets retain their transmitted act and exact
item/recipient identities. Host-approved item changes on guests use the same
retention boundary. Every action rechecks current run/act and native ownership
or hit eligibility before applying; reset generations invalidate old drains.
Give's recent-drop window is evaluated at admission time so a long shared hold
cannot expire an otherwise valid queued handoff; ownership remains revalidated.
Ordinary item Throw tasks use this same ordered drain and admission-time check
for their existing two-second dropped-item window. Resume preserves the native
impulse vector/add flag, thrower attribution and half-second pair grace; native
timed-bomb/contact-grenade paths and fuse behavior are unchanged.
Processing is bounded to 32 tasks per pass, and overflow cancels explicitly.

The Win32 mirror fixture runs the real world damage filter in its native hit
stub, reproducing why a held native call would reject an already admitted hit.
It covers queued-before-hold and received-during-hold hit/item work, duplicate
admission, pickup-hit-drop-hit ordering, exact-once Resume, approved guest
ownership retention, recent-drop Give freshness across a long hold,
queued Drop-Throw order, long-hold Throw freshness, native impulse/grace ABI and
reset/discard without a throw,
stale act/run/incarnation/source-life rejection,
cancellation, replacement drain generations and bounded overflow. The native
Mine combat observations remain a separate integration result.


## Vehicle spatial, physics and rendered model paths (dc04, smooth actors)

These addresses were inspected in the installed executable with SHA-256
`dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`.
The actual vehicle constructor starts at `CA7A30`, assigns primary vtable
`1474B90` at `CA7A86`, and assigns spatial vtable `1474BE8` to vehicle `+34`
at `CA7A80`. An older override comment calling `CA79C0` the constructor is
incorrect: that address belongs to an unrelated preceding helper.

| Native path | Installed behavior and ABI |
| --- | --- |
| Vehicle spatial `+38`, `C9FDF0` | `thiscall(spatial, Vec3*)`, `ret4 C9FEC0`; calls base `C88800`, which copies spatial `+4` and sets flags `+50` mask `0x01` and dirty byte `+74` when changed. |
| Vehicle spatial `+3C`, `C9FED0` | `thiscall(spatial, Quat*)`, `ret4 C9FF45`; calls base `C88860` and marks the same dirty state. |
| Vehicle spatial `+44`, `C888D0` | `thiscall(spatial, Vec3*, Quat*)`, `ret8 C88910`; invokes virtual `+38/+3C`, casts the owner, then calls `cPhysicsWorld::Get B3D410` and `B48870(owner,false)`. It has no creature `+B5D` dirty-byte operation. |
| Vehicle spatial `+AC`, `C45E40` | `thiscall(spatial)->Model*`, plain `ret C45E46`; reads spatial `+9C`, which is vehicle `+D0`. |
| Generic spatial view `E92C80` | Native view Update takes two integer deltas. At `E92D5D`, spatial flags `+50` mask `0x04` cause the existing model Transform to be copied back to spatial through its setters. Later `E92E0A` tests/clears dirty `+74`; `E92F10` applies the spatial position, orientation and scale through `537E10(model+8,Transform*)`. |

For a normal vehicle, read position at vehicle `+38`, quaternion at `+44`,
flags at `+84` (a 32-bit field), dirty at `+A8` (one byte), enabled at `+A9`
(one byte), and actual model pointer at `+D0`. The model Transform begins at
model `+8`; its translation is model `+C/+10/+14`. Read-only native observations
must compare the spatial pose with that actual model, not only a replicated
snapshot. Vehicle `+14` is the native `cGameData::mpView`; its Update slot is
`+4`, but callers must validate the current view pointer and vtable before use.

A vehicle using a creature resource has a separate AnimatedCreature at `+D00`.
Creation/assignment is visible at `CA4191..CA42EA`; its animation core is `+17C`
and actual model `+180`. Vehicle setters maintain its public pose. Do not assume
this pointer exists on ordinary vehicle creations: both Crystal Mine tanks have
zero `+D00` and use the ordinary `+D0` model chain.

The teleport's additional native physics operation is material. The inspected
`B48870` active-world branch reaches `B53AB0(spatial)`, which refreshes registered
physics from the current native pose through `B4E2D0` and locomotive processing
`B538C0`. This is a separately verified physics path, not an assumption that the
creature-specific teleport flag also exists for vehicles. A controlled native
current-pose probe below establishes this one-pose bridge. It does not by itself
establish continuous follower movement. No raw model, physics-body or dirty-flag
writes are authorized by this finding.
The `0x04` branch is a verified possible back-copy path, not proof it executes
for these tanks: native boundary samples showed flags `A000/A001`, without
that mask. Physics updates can also restore their own pose before presentation.

The existing authority filters do not contain the generic view maintenance:

- `EF1850` delegates to scenario actor processor `EBDBB0`. Its inspected body
  evaluates authored hazard/effect volumes and calls combatant damage at
  `EBE00F/EBE0FA`; it does not call the generic view or copy model transforms.
- `CA9850` only expires native combat/status timers and queues events `19/1B`.
  Its tail `CA8310` restarts the timer and queues `1B`; neither refreshes models.
- The native aggregate ignores the behavior-tree return at `F39F69` and always
  continues to `CA3380` and `EF1850`. Returning pending from the guest tree hook
  cannot skip those later operations. `CA3380` itself contains placement/view
  effect work, not the actual model-transform copy; the older generic graphics
  label should not be interpreted as proof of model-pose synchronization.

Keep tree, weapons, timers and actor gameplay host-authoritative. Unfiltering an
entire gameplay aggregate is not justified by the graphics symptom.

### Verified vehicle locomotion interfaces

All slots below belong to spatial vtable `1474BE8`, with `this=vehicle+34`:

| Slot and native function | Verified ABI and behavior |
| --- | --- |
| `+E0`, `CA35A0` | `thiscall(spatial,Vec3* destination,float goalStop,float acceptableStop,bool straight)`, `ret10 CA37E8`; installs a locomotion request through virtual `+DC=C9EA20`, which forwards to `C43FA0`. |
| `+C4`, `C9EE70` | `thiscall(spatial,float desiredSpeed,int flag)`, `ret8 C9EE82`; forwards to `C41E80`, which writes spatial `+1E0`. |
| `+CC`, `C42080` | `thiscall(spatial)->float` in ST0, `ret C42086`; reads spatial `+1E4` standard speed. |
| `+EC`, `C43800` | `thiscall(spatial)`, `ret C438E8`; clears the native locomotion request, calls `+C4(0,1)` and resets spatial `+270`. |

MoveTo itself reapplies authored current-act speed at `CA37A0..CA37D6`, so a
follower's chosen speed must be applied after MoveTo. These are verified calling
interfaces, not yet native acceptance that mirrored vehicles can follow while
gameplay AI remains suppressed. Preserve a distinct native physics correction
path if locomotion cannot advance under the authority fences.

Static assembly, vtable/return checks and proposed named symbol entries are in
`out/lab/ga-smooth-vehicle-re/`. Production integrations must use named verified
symbols, not hardcoded native addresses.

The 20-second hidden baseline on preview-9 DLL
`b939580707f00867b823f01d0ab3f06dde8384c38213074d2bf8504a38d5d3e2`
reproduced the normal vehicle bug without manufactured AI: Crystal Mine vehicle
9 moved 80.627 metres on the host while its guest spatial moved 82.128 metres
and guest model moved zero; guest model/spatial error p95 was 83.993 metres.
Vehicle 37 similarly had guest spatial travel 53.411 metres and zero model
travel, with p95 error 52.539 metres. Guest dirty stayed set in 224 of 252 samples
for each, versus two host samples; enabled was always true. Evidence is
`out/lab/smooth-actors-preview9-native-motion/result.json`. This establishes the
baseline, not a corrected-build pass.


### Actual vehicle view and controlled physics-bridge probe

The live Mine vehicles' native views resolve to preferred vtable `1486CBC`,
Update `E956D0`, rather than an assumed building or generic view subclass.
That Update handles model/material changes, then unconditionally calls generic
spatial-view Update `E92C80` at `E95832`, passing its two integer deltas. This
supports retaining the existing gameplay authority fences: the native view
consumer exists independently of the skipped behavior, timer and actor calls.

The bounded hidden/muted probe used preview-9 DLL
`b939580707f00867b823f01d0ab3f06dde8384c38213074d2bf8504a38d5d3e2`
and made exactly one native `+44` call on guest vehicle marker 9. Its input was
a copied existing received host pose, not an invented destination. Before the
call its actual model was 10.277 metres from that pose. The immediate model read
was unchanged; after one rendered frame the native model exactly matched the
copied pose and the dirty byte was clear. It retained that pose at frames 2/3.
Later ordinary separate-setter snapshots again left the model behind, with
3.268 metres error at frame 5 and 5.051 at frame 10. Untreated marker 37's model
never moved and had 14.535/15.793 metres error at those checkpoints. Both games
returned to connected native menus with zero recorded faults. Evidence:
`out/lab/smooth-actors-preview9-vehicle-vt44-probe-retry1/result.json`.

This establishes the native physics-relocation bridge for an ordinary vehicle
correction. It is separate from the creature `+B5D` presentation flag and does
not prove the generic view's `0x04` branch ran: observed flags remained
`A000/A001`. Candidate validation must still measure actual model travel,
spatial/model error and native health/objective authority during continuous
following. The inspected MoveTo body only installs locomotion and desired
speed; it does not call weapons, behavior-tree, actor-damage or objective
callbacks. Whether native physics consumes that request under the retained
fences remains an integration test, including StopMovement and stationary
correction without drift.


### Inherited vehicle request reads and short-goal braking

Vehicle `CA7A3B..CA7A40` constructs `C439C0` with `this=vehicle+34`, so the
locomotive fields are relative to that spatial subobject. The constructor
initializes velocity at spatial `+1C8/+1CC/+1D0`; native movement update
`C43358..C433C3` passes that vector through the locomotion strategy and copies
its output back. The strategy pointer is spatial `+4CC`, which is vehicle
`+500`. Read-only velocity sampling is therefore vehicle `+1FC` (three floats),
not vehicle `+1C8`.

Vehicle request entry `C9EA20` jumps to `C43FA0`; `C44021` copies the request
into inline spatial `+1F4` using `C43F40`. The copy explicitly preserves the
request destination `+14`, goal-stop float `+20`, mode integer `+5C`, and
acceptable-stop float `+60`. Their complete vehicle offsets are respectively
`+23C`, `+248`, `+284` and `+288`. These fields may be read without invoking a
new native function. The request also owns a waypoint vector; `C42520` returns
its last waypoint when nonempty, otherwise the inline destination. Diagnostics
must distinguish the final destination from an active intermediate waypoint.

GA land vehicle strategy construction `CA3173..CA31A5` installs vtable
`14749B4`; its Update `+0C=CB3670` calls `CB3110` and `CAE420`. For a request
with an empty waypoint vector and mode 1 (the false-bool MoveTo path),
`CAE7CA..CAE80E` limits the target velocity magnitude to:

`min(desiredSpeed, sqrt(nativeAcceleration * distanceToGoal), distanceToGoal/dt)`

The vehicle acceleration virtual `+D0=C42240` forwards to `+CC=C42080`, so this
branch uses native standard speed as its acceleration value. In particular,
raising desired speed alone cannot remove the distance-dependent cap. A moving
vehicle needs at least `speed*speed/standardSpeed` metres of navigation goal
distance to sustain that speed in this branch. This explains why using the
latest network pose plus only 100-200 ms of lead can cause a large trailing gap
while the rendered model itself now moves correctly. A vehicle-specific,
bounded navigation lookahead can account for native braking while preserving
snapshot age bounds, native collision and the distinct correction path. It
still requires native turn, stop and loss tests; the formula is not acceptance
of a particular new policy or a reason to remove gameplay authority fences.

Static evidence is retained in `locomotion-target-processing.asm`,
`vehicle-ga-ground-strategy.asm`, `vehicle-ga-ground-core.asm` and
`vehicle-ga-pose-consumer.asm` under `out/lab/ga-smooth-vehicle-re/`.


The fourth MoveTo boolean is not a continuous-motion mode switch. False builds
mode 1. True computes normalized `destination-currentPosition` and calls
`AD2990`, which stores the direction at request `+50` and mode 3 at `+5C`.
Mode 3 follows a direction-constrained path, then still applies the square-root
speed cap at `CAEC35` and the subsequent desired-speed minimum. The native GA
MoveTo call at `CADB3F..CADB56` passes false. Changing the bool to true should
not be described as removing braking, and requires separate native validation.

Each `C43FA0` admission copies the newly constructed request through `C43F40`.
Its waypoint-vector assignment `C1B8F0` replaces any prior route with the new
request's empty vector; it marks current strategy `+4` changed and resets
spatial transit time `+1A0` to zero. Frequent MoveTo retargeting can therefore
restart native route preparation. Preserve a bounded retarget cadence and
measure turns as well as straight-line travel. A longer navigation goal can
avoid braking, but can also change route choice; read-only diagnostics should
record the effective final waypoint and route count alongside current pose,
standard/desired speed and actual velocity.


### Verified fly-through request mode and guarded scope

Native request getter `C42020` is a nonvirtual thiscall leaf taking the
locomotive spatial pointer, returning its inline request in EAX. Its complete
bytes are `8D81F4010000C3` (`lea eax,[ecx+1F4]; ret`). Request mode is one
32-bit integer at request `+5C`, hence spatial `+250`, complete creature
`+310` or complete vehicle `+284`.

Native creature AI demonstrates the intended post-admission mode change:
`D6C102` obtains spatial EBP as complete creature EDI `+C0`; `D6C306` calls
movement builder `C1C330` with the complete creature, and that builder dispatches
its mode-1 request through spatial `+C0` at `C1C38C`. `D6C30D` then calls the
request getter on EBP, and `D6C31B` writes request `+5C=2`. The adjacent native
example repeats the getter at `D6C5DF` and mode write at `D6C5E8`. These are
verified native request writes, not evidence that a broad AI helper should be
called by a mirrored actor. Exact getter/AI-site byte checks are in
`out/lab/ga-smooth-vehicle-re/verified-mode2-specs.json`.

Vehicle `CAE420`'s empty-route jump table `CAF7E0` maps mode 1 to `CAE7CA`,
mode 2 to `CAE858`, mode 3 to `CAE87B` and mode 4 back to `CAE7CA`. The mode-2
branch multiplies normalized goal direction by desired speed, omitting mode 1's
pre-arrival square-root braking. It rejoins `CAF218`, so native turning,
orientation, collision and acceleration limiting (`CAF61B..CAF72E`) still run.
Modes 1 and 2 share the same reached-goal branch `C4326B` through jump table
`C432F0`; `C42030` applies the goal/stop-distance predicate. Native ground
processing clears the request on completion at `CB33D6..CB3404`. Explicit
StopMovement remains the verified mode-0, speed-zero path. Mode 2 is therefore
a point fly-through request, not unrestricted velocity or removal of native
collision. With a nonempty waypoint vector, vehicle processing follows its
existing route branch and retains path/turn speed limits before rejoining the
same acceleration code; mode 2 does not bypass those caps.

A narrow adapter can issue the existing native MoveTo, obtain the request with
the named getter, and change mode 1 to 2 only after confirming that exact mode
and scoped mirrored ownership. It must retain native Stop and stale/hold/act/
incarnation fences. The verified ordinary GA strategy is spatial `+4CC`,
vtable `14749B4`, Update `+0C=CB3670`. Check its readable pointer/vtable and
resolved named Update identity before applying vehicle mode 2. That strategy
is shared by land and water in `CA3010`: complete vehicle `+B1C=0` is land,
`1` water, and `2` air. A land-only accepted scope also requires `+B1C==0`;
the strategy gate alone does not exclude water. Air takes another strategy.
Native water/air movement remains unvalidated by the Mine tank evidence.

Increasing future-goal lead alone did not establish parity. Candidate 2 DLL
`db9a0dbb5a781808e495b7cd5ea9a72f9f1e5e07bf0ea6936aa2e027f663a28f`
had vehicle 9/37 host-to-guest spatial error p95 of 9.755/14.850 metres.
Candidate 3's longer lead, DLL
`aa60891b59665d8f2d120676daf1ad4737a2373f223ec69590f95b8884d549ab`,
had 15.140/13.816 metres in a separate natural Mine run; native model movement
was restored but turns/tracking still diverged. These are negative candidate
results, not a controlled identical-AI trajectory comparison. Evidence is in
`out/lab/smooth-actors-candidate2-native-motion/result.json` and the matching
`candidate3` directory. Prefer bounded near-future goals with verified native
request semantics over treating a far projected point as host path evidence;
final acceptance still needs continuous travel, turns, stops and authority
measurements on the chosen build.
