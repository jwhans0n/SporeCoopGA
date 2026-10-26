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


## Host NPC captain perception on GOG 2024 (dc04aee5)

Native sensing, authored target matching and relationship lookup are separate
paths. Remote party captains are ordinary `cCreatureAnimal` puppets in their
own quiet herds, with no authored scenario class and no native avatar bit
(`+B58 & 0x200`). Relationship aliasing does not fix either missing sensing or
a failed authored target match. The investigation below first tested singleton
perception, then established that the actual Mine failure occurs after native
ScenarioSensor admission, at a political-ID match. The initial perception
adapter is a negative candidate, not a prerequisite of that narrower fix.

### Adventure applicability and singleton stimulus

Native animal Update `C0AE30` calls `C09570` at `C0AF63` for every non-avatar
animal. There is no Scenario-mode exclusion at that call. `C09570` requires a
nonempty brain root at `creature+B4C`, then calls the noun manager's avatar
getter `B1FD00` at `C0958E` (return `C09593`). It uses that one returned
creature for its sight/hearing stimulus before running the existing behavior
tree once at `C09B29`. Replacing unrelated avatar lookups inside behavior
nodes would also change native authored-target semantics and is not justified
by this perception call site.

There is direct GA-specific evidence in the range helper: `C0E650` checks
native game mode `0x1654C10`, Scenario mode `+CC==2`, and a non-avatar animal;
when its current class-act exists, it returns authored awareness at class-act
`+484`. Otherwise it falls back to phenotype `+34C` or the native default
17.0. This supports the real adventure perception path, rather than inferring
applicability from a creature-stage symbol name. Native Mine host-positive
and guest-near observations must still be recorded separately from static RE.

### Verified native perception helpers

All addresses below are preferred-image addresses in the installed executable
with SHA-256 `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`.
The naming here describes the observed operation; new code must resolve named
aliases through the build's symbol map and verifier.

| Entry | Verified ABI and behavior |
| --- | --- |
| `C0E650` | `float __thiscall(creature*)`, plain ret; authored Scenario awareness or phenotype/default sight range. |
| `C0C170` | `float __thiscall(creature*)`, plain ret; phenotype `+350` full field of view when nonnegative, otherwise 120 degrees. |
| `D38A30` | `float __cdecl(int kind, creature*)`, plain ret; kinds 1 and 2 both return nonnegative phenotype `+358`, otherwise 1.0. |
| `C0C290` | `bool __thiscall(creature*)`, exact leaf `8A81B10B0000C3`; reads `+BB1`, used as the target's stealth state. |
| `C0D6C0` | `bool __thiscall(observer*, target*, float hearingMultiplier, float edgeDistance)`, `ret 0xC`; requires positive target noise at `+B80`, then compares distance with noise plus observer hearing coefficient times multiplier. Coefficient is phenotype `+354` when nonnegative, otherwise 10.0. |
| `D993D0` (`SporeCoop::BT_ValidTarget`) | `creature* __cdecl(creature*)`, same pointer or null, plain returns `D99416/D9941A`; rejects null, dead `+B5E`, untargetable `+135`, retired primary virtual `+2C`, and animal flags `+B58 & 4`. It requires neither an avatar bit nor a scenario class. |

The ordinary sight test in `C09570` uses range `C0E650(observer)` multiplied
by `D38A30(2, observer)`. Its cone threshold is
`cos(C0C170(observer) * 0.008726646192371845)`, or the cosine of half the full
field of view in radians. It normalizes target-minus-observer using `C027F0`
(which adds `1e-8` before its square root), subtracts both native spatial radii
from center distance, clamps the result to zero, checks `+BB1` is false, and
compares forward-dot and range. Hearing passes `D38A30(1, observer)` and that
same edge distance to `C0D6C0`. No world raycast occurs in this ordinary sight
branch. Later native branches provide additional near/noisy stealth detection
and ability-dependent behavior; approximating those branches is not equivalent
to proving that a cloaked target is undetectable.

Native direction must not be inferred from velocity in a sensing fixture.
The animal spatial vtable `1469EC0` has `+5C=C891B0`, ABI
`Vector3* __thiscall(spatial*, Vector3* output)`, returns `C89221/C8924D`
with `ret 4`. Native perception supplies its output buffer at `C09604` and
calls the virtual at `C0960B`. It obtains the quaternion via virtual `+30`
(`97E7C0`, exact leaf `8D4110C3`, returning spatial `+10`), rotates the native
reference-forward vector, normalizes, and writes the supplied buffer. A
fixture may preflight this vtable/function/return identity and use disposable
scratch output without writing an NPC field. Native footprint virtual `+74`
is `C37190`, exact leaf `D94160C3`, returning spatial `+60` as an ST0 float.
Capture forward-dot, radius-subtracted range and target `+BB1` on the actual
host-side guest puppet. Being near or ahead of velocity does not prove being
inside the NPC's native cone. Reviewed static bytes are retained in
`verified-forward-spec.json` under the same ignored evidence directory.

### Perception record ownership and cleanup

The sensed-creature vector is the pointer range at observer `+1124/+1128`.
The vector stride is 4 bytes, not 0x14: `C04993/C0499F` load and advance a
pointer, `C03444/C03448` copy a pointer and advance the destination by four,
and `C06D6C` divides erased element bytes by four. Each pointer refers to a
native 0x14-byte record owned by the observer's deque at `+1034`: `+0` edge distance (float), `+4` sight byte, `+5` hearing byte,
`+8` complete target pointer, and `+10` intrusive owning reference. `C068B0`
constructs/appends these records and balances native AddRef/Release; do not
insert raw records, release their references, or overwrite their ownership.
`C098B2..C09944` removes special records, appends the selected avatar's sensed
record, sorts the pointer list and publishes a native `BC9960` stimulus.

The special removal predicate `C028F0` is
`bool __cdecl(const void* record)`, plain ret at `C02927/C0292E`, prefix
`8B442404568B70088B168B422C8BCEFF`. It reads the complete target at `+8`
and returns true for primary virtual `+2C` retirement, avatar bit `+B58 &
0x200`, false targetability at `+135`, or target flags `+110 & 0x10`; it
returns false for an otherwise valid ordinary animal. The native code pushes
this predicate at `C098BF` and calls `C04980` at `C098CC`. The first callback
is `C04996`, return `C04998`; its remove-copy phase calls at `C0343B`, return
`C0343D`. Those are verified callback return sites for a narrowly scoped
remote-party-record cleanup adapter.

A guest substituted into the singleton perception read lacks the native
avatar bit. Without corresponding scoped cleanup, the native predicate can
retain its previous record and append another one. A safe adapter must retain
original predicate results for every other record and every call outside the
actual host NPC AI scope. Cleanup of the pointer vector does not itself prove
that deque storage is bounded when selected captains alternate: `C05FE0` is
native deque back(), and `C06000` is pop_back(), not front/pop_front. Native
`C0988B..C098B2` pops the last owned record only if its `+10` reference matches
the newly selected avatar. Repeated switching, hidden/invalid targets, run
teardown and disconnected captains therefore need explicit record-count and
lifetime validation. `C06FB0` clears the perception vectors and both owner
deques during native brain reset; that is not evidence of per-tick clearing.

The verified post-cleanup/pre-append boundary is native factory `C068B0`,
called at `C098F0`, return `C098F5`. Its ABI is
`record* __cdecl(deque*, creature* target, float edgeDistance, bool sight,
bool heard)`, five 4-byte stack slots with only each bool's low byte consumed,
plain ret at `C0695D`; the native caller pops `0x14` bytes. Native vector erase
`C06D50` has completed at `C098D7` before this boundary. An adapter scoped to
this exact caller and pool `liveHostNPC+1034` can discard a previous party
record using native Back/PopBack after confirming it is no longer in the
sensed pointer vector. The post-cleanup identity test must include the local
host captain as well as remote captains: on a host-to-guest switch the native
pre-cleanup equality no longer pops the old host record. A remote-only pool
adapter would therefore leak one host record per such switch. Preserve nonparty entries; do not pop before pointer
cleanup or manually release a record's intrusive owner. Factory arguments must
be forwarded with the verified ABI, and native processing still runs once.

For bounded read-only diagnostics, this deque has begin iterator current,
block-begin, block-end and map-node at `+08/+0C/+10/+14`, and end iterator at
`+18/+1C/+20/+24`. `C068B0` advances by record size `0x14`; its block end is
`0xA0`, while `C05FE0` uses previous-block last-record offset `0x8C`, proving
eight records per block. After checking readable/aligned/sane pointers, count
is `8 * ((endNode-beginNode)/4) + (endCurrent-endBlockBegin)/0x14 -
(beginCurrent-beginBlockBegin)/0x14`. The native nonempty test compares begin
current `+08` with end current `+18`. Capture the raw iterators and reject an
inconsistent read rather than inventing a count from a partial structure.

### Authored class enumeration is a different path

`F40BA0` (`GetObjectsForClass`) is a native thiscall taking scenario data,
class index and an output vector, `ret 8`. Its special avatar class index is
**-2**, not -1. It clears the output before filling it and appends the singleton
native avatar with balanced intrusive references. Ordinary authored classes
are enumerated from their placed scenario objects, with the existing respawner
child handling. This output owns intrusive game-object references, so appending
raw pointers or borrowing a foreign vector allocator would be incorrect.

The verified caller `DC7760` reads authored `trackTargetClassIndex` at current
class-act `+4C4`, uses this enumerator and chooses a nearby spatial result.
Extending only class -2 could extend that explicit authored avatar-class
query, but it is not evidence that all free-hostile acquisition uses this
function. The verified singleton perception branch above is one observed
discovery gap. Further caller checks are required before extending this
enumerator: `EC2EC0` (query at `EC2F3A`) is an authored follow/track-spatial
selector, selected by movement behavior `+4B8==5` at `EC3040`. Its custom
avatar-target type also has a direct singleton read at `EC2EF0` instead of
calling the class enumerator. `ED8850` and `ED8F40` class-query consumers
manipulate spatial effects and class-distance display; they are not proof of
autonomous Hound attack acquisition. Preserve native authored orders, neutral
relationships, NPC-versus-NPC candidates and behavior-tree decisions rather
than forcing a party target or changing every avatar getter.

The first native candidate is deliberately recorded as a negative result.
Candidate 1 DLL SHA-256
`15e651e8bdd2a4d0ac9093b2a778d10f07f30c9ec9c5164b67945d86b44bf67a`
placed the guest into the Hound's sensed records in all 81 corrected-forward
samples, including 21 records with native sight true. The host-side guest
puppet was targetable and not stealthed, its species differed from the Hound,
and its sensed record appeared at most once. Nevertheless the native combat
target and checked target getter stayed null, and owner health remained 232.
The observed pool count was 1 through 4, not evidence for repeated-switch
lifetime acceptance. Evidence is
`out/lab/enemy-targeting-candidate1-guest-sight/result.json`. Adding the first
singleton stimulus alone therefore does not establish attack targeting;
downstream behavior-tree selection still needs verified coverage.

Static disassembly, exact helper prefixes/returns and constants are retained
under `out/lab/ga-enemy-targeting-re/`, including
`perception-signatures.json`, `npc-ai.asm`, `awareness-getter.asm`,
`perception-record-create.asm`, `perception-record-filters.asm`,
`perception-remove-if.asm`, `perception-remove-copy.asm`,
`perception-record-pool.asm` and `perception-native-clear.asm`.


### Actual Mine custom behavior and political-ID mismatch

The installed stock Maxis adventure **Defend the Crystal Mine** is resource
`408A0000!6E008979.366A930D`. Hound marker 138, scenario class 1082, has a
native 18-node tree whose active custom selectors include `EC0F00` twice and
`EC15A0`. Its native custom entries are a 0x20-byte vector at complete animal
`+674/+678`, with condition at `+0`, condition target type/value at `+4/+8`,
action at `+C`, action target at `+10/+14` and secondary target at `+18/+1C`.
Observed entry 0 unconditionally attacks explicit class 1069 (the generator,
target type 2). Entry 1 uses condition 1 and attack action 0, with **target
type 1, value 1 for both condition and action**. Entry 2 is unconditional
action 10. This live authored data, rather than a generic creature-stage
brain assumption, identifies the acquisition path relevant to this bug.

`C7D210` is the exact leaf `lea eax,[ecx+68]; ret`. `EC0C90` condition 1 and
`EC0F00` attack selection pass complete animal `+670` to it, so the actual
ScenarioSensor candidate vector is animal `+6D8/+6DC`, stride 4. Its entries
are **spatial pointers**, not complete creature pointers and not the sensed
record pointers at `+1124`. Both loops use spatial virtual `+B8` with
`17F243B` to obtain complete game data, reject retired objects and invoke the
authored matcher. Animal spatial vtable `1469EC0+B8=C06500` subtracts `C0`
then tail-calls the native cast, so an exact captain spatial pointer can also
be recognized read-only as complete captain `+C0`. `C7DE50` populates this
sensor from native contacts with native spatial ownership and geometry/LOS
filters; do not insert borrowed references or synthesize candidates.

`C7D230` is `bool __cdecl(cGameData* candidate, const BehaviorTarget* target)`.
It consumes two stack arguments and returns AL with plain returns at
`C7D246/C7D25D/C7D270/C7D289/C7D28D`. Its verified prefix is
`568B74240C8B0683F808774EFF248590D2C700` (the branch bytes are `77 4E`).
Type 1 invokes candidate primary virtual `+4C` and compares the returned
political ID with `target+4`. For animals, primary table `146A080+4C=5AADB0`,
the exact leaf `8B4130C3`, proves this ID is complete game data `+30`.
Type 2 compares complete candidate `+1C` scenario class. Type 8 compares the
exact singleton avatar pointer; types 3 through 7 reject. These alternatives
must not be conflated with type 1 or broadened without a separate use case.

The host-positive native control placed only the owner captain near the
Hound. Its real sensor contained the host spatial in all 97 samples, native
combat target selected that captain in 97/97, and native damage lowered
owner health from 232 to 139.29996. The matching guest trial already contained
the actual host-side guest spatial in 67/69 native sensor samples, while its
combat target stayed null and guest health remained 232. A further 35-sample
capture established **political ID 1 for each local owner, but FFFFFFFF for
each remote puppet on both peers**. The host-side guest was admitted into the
native ScenarioSensor in all 35 samples. Consequently both authored type-1,
value-1 checks reject this otherwise native-sensed guest. The bug is an
identity-category mismatch, not evidence that all native visibility is absent.
The paired evidence is in `enemy-targeting-candidate1-scenario-sensor-host`,
`enemy-targeting-candidate1-scenario-sensor-guest` and
`enemy-targeting-candidate1-political-guest`, each under `out/lab/` with exact
loaded-shadow hashes recorded in `result.json`.

The verified matcher return boundaries are:

| Native return label | Role |
| --- | --- |
| `EC0CF5` (call `EC0CF0`) | Condition-1 native sensor candidate match in `EC0C90`. |
| `EC0F70` (call `EC0F6B`) | Current-target condition shortcut in `EC0F00`. |
| `EC0F8F` (call `EC0F8A`) | Current-target action match in `EC0F00`. |
| `EC1032` (call `EC102D`) | Ordinary attack sensor candidate match in `EC0F00`. |

Each caller pops 8 bytes after the cdecl match. Covering only the attack
candidate site cannot fix entry 1: its condition fails first at `EC0CF5`.
A narrow adapter can retain the native result and extend only a false result
at these exact sites, during the original live host NPC AI scope, for an
alive participating remote captain when type is 1 and value equals the local
captain's political ID. Native hostility and target validity still apply;
keep existing nonparty combat targets, neutral relationships, explicit class
1069 and all other target types/values native. Remote identity alone is not
alive eligibility: a downed captain still has a puppet and native positive
health. Consult the coordinator's participating/can-act captain set as well.
Do not change political ID, species, avatar bits, authored data, native sensor
storage or target output, and never add another native AI tick.

Candidate 2 passed the isolated Win32 ABI fixture and a fresh native Mine
trial. DLL SHA-256
`2db7a47f3e35340a5e25cdebdbb1a85e2076db46c2da527e7da99f6da3fb5c02`
selected the guest in 90/90 native target samples, with 12 independent getter
checks. Its native ScenarioSensor contained the guest in 90/90 samples, actual
host-side distance was 2.377-4.830 metres, and natural damage lowered guest
health from 232 to 139.29996. The matched host control selected the host in
93/93 samples and caused the same health loss. Neither run assigned an NPC
target/order or manufactured damage. Evidence is
`out/lab/enemy-targeting-candidate2-guest/result.json` and the matching
`candidate2-host` directory.

Candidate 2 still contained the initial perception adapter and diagnostic
hooks, so that result alone did not prove the matcher sufficient. The final
candidate removes singleton perception substitution, sensed-record cleanup/
factory hooks and unused score diagnostics. Its one matcher hook retains the
four verified sites; native sensors and record ownership remain unchanged.

Final matcher-only DLL SHA-256
`85bdebb2aed8dc870a6f0c37cdef29b25b02d2fb558a7ea3ac3a9fa49e71220d`
passes fresh native Mine controls: guest selected in 93/93 target samples with
12 getter checks, host in 95/95 with 12 getter checks, and each naturally loses
92.70004 health (232 to 139.29996). The guest remains in the native
ScenarioSensor in 93/93 samples despite removing the initial sensing adapter.
A guest/host/guest sequence naturally selected and damaged each staged captain
in all three phases, with health losses 55.62/18.54/18.54 over approximately
2.96/2.64/2.58 seconds nearby. The native pool stayed within 2 through 4 records,
ended at 3, and each captain appeared at most once; no ownership adapter was
needed. Every case returned to connected native menus with zero faults.
Evidence is `out/lab/enemy-targeting-candidate3-{guest,host,switches}/result.json`.
This establishes the targeted two-player Mine fix, not all authored commands,
four-player targeting or broader adventure compatibility.

### Negative target-selector and diagnostic paths

The generic player-target score `D8CB20` is registered as evaluator ID
`12E0ECAC`, but is absent from this live Hound tree. Read-only diagnostics
recorded zero calls both inside and outside its NPC AI scope in matched
host/guest trials, despite sustained guest sight. Its flags-`408` branch
uses the singleton avatar, while the alternative sensed-record branch
requires native spatial `IsPlayerOwned` and a `cCreatureCitizen` noun. Remote
animal captains fail both; changing ownership bits alone would not make that
branch suitable. It is not the verified Mine attack selector.

`D993D0` and the stricter animal conversion `D99460` reject native invalid,
dead/retired or untargetable candidates but require neither an avatar bit
nor an authored class. `D8BAE0` uses the friendly relationship predicate
`C0BA00` (relationship 1), rather than hostile `C0B9C0` (2 or 5). `D8CD50`
consumes an already-produced tree target. None explains this observed
political-ID rejection, so no broad validity or relationship replacement is
justified by those helpers.

High-frequency external avatar returns `D49228/D4924E/D4AABF` are native
ability-button/cooldown UI paths; their approximately tenfold counts match
ability slots, not ten NPC target decisions. The rare host-only scoped avatar
return `C0C3CF` belongs to `C0C3B0`, primary virtual `+D4`, which is called
later at `EC109A` in the actual attack selector. It is consistent with an
already-accepted host candidate, not proof of an earlier missing singleton
read. Likewise, adding guests to the class `-2` enumerator would not repair
an authored type-1 political match in an already-populated ScenarioSensor.

Reviewed disassembly and verifier-ready details for this narrower path are
`behavior-target-match.asm`, `custom-behavior-condition.asm`,
`target-selection-d4.asm`, `sensor-candidate-admission.asm` and
`verified-behavior-target-spec.json` in `out/lab/ga-enemy-targeting-re/`.
The unused generic-score ABI and its native cdecl double/ST0 dispatcher proof
remain ignored diagnostic evidence, not a reason to hook additional selectors.

## Terminal outcome ownership after native results

The Crystal Mine report supplied a stronger counterexample than a missed result
packet. A read-only capture of the personal guest found the coordinator Ended,
the native result latch at run 1/applied 6, but the actual play object at act 4,
state 3. Its suppressed-score count was zero. Therefore a prior call to native
SetState 6 is not evidence that the guest remains in native results. This capture
does not identify which earlier callback reopened gameplay, and the personal
sessions were not modified to reconstruct that history.

Two authority gaps were present in the adapter. The broad `g_applying` flag,
intended for replicated interaction/damage callbacks, also allowed nested native
terminal requests to bypass the coordinator. Separately, result idempotence only
rejected later terminal 5/6 calls. Reliable goal application could still call the
native helper with state 3/4, and goal/act authority guards disappeared when the
coordinator left Running. The actual-world Win32 fixture reproduces both gaps:
a native failure nested inside an applying scope must route to the coordinator,
and an already queued goal batch must not change an applied terminal attempt's
act, goals or state.

`terminal_state(play)` is now a run/play-scoped latch that survives Ended and
automatic replication reset. It fences every subsequent SetState request,
CheckGoalProgress, CountGoal, CompleteAct, UpdateGoals and queued world goal
application until explicit cleanup or the verified Initialize boundary. The
shared applying flag never grants terminal authority. `result_applied(run,bool)`
additionally checks actual Scenario mode, the current play pointer and native
`play+90` against the expected 5/6. It can reject an acknowledgement even when
the call latch says applied. This is native-state evidence, not proof that the
captain's later results-panel save has succeeded.

The verified native HandlePlayMessage `F1C570` has a second lifecycle boundary:
the Intro Finished path directly writes `play+90=3` at `F1C6F3`, bypassing the
SetState hook. The terminal branch at `F1C75A..F1C7C6` instead accepts state 5/6,
opens the native results panel through `F18540` at `F1C7AE`, and records
`play+94=2` at `F1C7B4`. It does not match the ended cinematic ID itself. The
adapter permits only that terminal Finished message while the outcome is locked.
It captures the current result cinematic's manager, run, play pointer and running
ID in the existing EndCinematic hook before original cleanup. Original
EndCinematic copies `manager+148` into the stack message, clears it, and publishes
`044F1189` synchronously at `ADFFDA..ADFFE1`. The adapter requires the captured
run/play/ID, matching actual terminal state, and `play+94!=2`, then consumes the
token before forwarding. Delayed Intro/Talk events and duplicate results-panel
opens are rejected. Initialize and explicit cleanup invalidate the token.

The native simulation wrapper retains state-5/6 updates so their normal result
animation and panel can finish. The host's outcome lock holds creature updates,
controls and damage after Running ends and blocks any native gameplay/Intro
update that would otherwise resume. The party can still choose Done or Replay.
Both choices are consumed before the terminal event filter and coordinated by
the run layer. Native results already perform the captain save before publishing
`070B18C7`: the button handler calls `F16460` at `F17728`, and later `F17750`
publishes the choice. The downstream Done branch `F1C994..F1CA1B` only selects
the native exit route. Suppressing that downstream single-instance exit does
not bypass the ordinary local results save.

The installed executable was rechecked as 24,895,536 bytes with SHA-256
`dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`.
The existing verifier now checks the native Finished state write, terminal
panel state, result choice branches, pre-publication save call and cinematic ID
publication bytes. The terminal fence itself introduces no new callable symbols;
the safe forced-acceptance path below adds one reviewed UI entry. The effective
map resolves 2,755 names, with 187 reviewed functions, four globals and eight
noncallable code sites. The expanded actual-world Win32 ABI fixture passed with
zero failures. Paired native failure/Done/Replay acceptance is recorded in the
delivery validation report; these static and isolated ABI checks alone do not
claim a complete Crystal Mine playthrough.

### Saving before a forced party Done exit

Native result state 5/6 and its result-applied acknowledgement do not prove the
captain has been saved. `F18540` enters the results panel and `F15770` calculates
the XP target. Actual captain saving is later, through the ordinary result button
handler. Cleanup `F17750` releases `results+B8` at `F17834..F17849` and publishes
`070B18C7` at `F17944..F17949` only if the choice at `results+14` is nonzero. It
does not call `F16460`. Immediately forcing a guest out after host Done could
therefore discard that guest's unsaved success rewards.

The new `ResultsHandleUIMessage=F16FA0` is verified thiscall with two stack
arguments `(IWindow*, const UTFWin::Message*)`, bool AL return and `ret8` at
`F1773F`, `F17078` and `F170B9`. Its vtable entry is `148C0AC`. The Done path
accepts `Message.type=287259F6` (ComponentActivated) and `Message.a=07C79948`.
At `F16FE9..F1701E` it hides the relevant native controls and sets choice 1;
`F1771A..F17728` sets result phase `+D4=10` and calls the normal native
`F16460(true)` save. The mod drives this reviewed UI entry, not the save helper
directly and not any XP property writes.

The result object is reacquired from the current Scenario display `+12C`, and
its `+10` play reference must match the live current play object. The native
layout at `+18` is a `UTFWin::UILayout*`, not an `IWindow*`. The existing verified
`UILayout::GetContainerWindow=810610` is thiscall with no stack arguments and
returns `layout+14 -> container+88`; the adapter validates those owners and calls
it before any window operation. The resulting real window and Done button must
both be visible; the button must be enabled. Native update `F17EC0..F17ED9`
enables accepting buttons only for result
phases 0 and 9. Mandatory reward selection phase 4 remains blocked, with a single
message asking the player to finish selecting rewards. The adapter never chooses
a reward or forces a disabled control. Before the result panel exists (`play+94`
not 2), it may finish only the current active terminal cinematic through the
existing verified EndCinematic entry. The terminal run/ID gate still validates
that cinematic's Finished publication before opening results.

The acceptance attempt is claimed before invoking the native button handler.
Repeated cleanup polls cannot repeat progression. It waits until the actual
native Saved choice message is observed for the same run and play pointer,
before coordinator routing or destruction of the native panel. Native Initialize
and explicit adapter reset invalidate these tokens. The party's owned cleanup
lease waits for this acceptance before restoring menus; ordinary local Done and
Replay acceptance already satisfy it. A native Saved choice is evidence of the
ordinary native save path, not an independent durable-storage acknowledgement.

The native Saved publication is synchronous. `F17936..F17949` obtains
IMessageManager and calls its vtable slot `+14` with `070B18C7`; constructor
`884B10` installs vtable `1427AC0`, whose `+14` entry is
`cMessageManager::MessageSend=8854A0`. Its `+18` entry is instead
`MessagePost=885980`. MessageSend invokes the registered recipients inline at
`8855A0` or `88568A`, drains its temporary recipient list and returns at
`8856F9`; it does not leave this payload in the asynchronous message queue.
Consequently the normal Saved callback cannot be queued across a later Replay
and credited to a reused play pointer. The shared Replay also waits for the
owned save/cleanup lease before preparing another run. The synthetic delayed
publication test is a conservative adapter check, not evidence that this native
Saved path posts asynchronously.

Repeat-adventure XP eligibility is also native. Initializer `1389040` hashes the
ASCII name `scenarioCaptainAdventureHistory` at `148AEE4` into property ID
global `16C7A58`. `EEBEC0(captainProperties, adventureKey)` reads that property
as an array of 12-byte resource keys and returns true when all three key fields
match. Results setup `F18540` calls it at `F1873F`. A remembered completion takes
`F18805..F18816`, releases and clears the results object's captain properties at
`+B8`, and presents the compact result panel. `F16460` then exits at `F16481`
without writing captain XP. The ordinary successful save first calls `EEF250`
at `F164AA`, recording the adventure key in that same property. That helper
removes a duplicate before appending and bounds the history to 128 keys. This
is a remembered-completion rule, not a permanent rule that an adventure can
never award that captain again.

The separate maximum-rank clamp in `F158BF..F158DF` limits target XP `+C8` to
the maximum threshold when the next rank reaches the native threshold count.
`EEC490` reads count `16C7A28`; `EEC4A0` reads threshold table `16C7A24`.
Read-only captures from both private C4 processes found ten thresholds:
100, 275, 525, 850, 1250, 1725, 2275, 2900, 3600 and 4375. Thus the observed
100-to-100 repeat was not evidence of a 100-XP maximum. Its compact result panel
matches the native remembered-completion branch. The first eligible controlled
success and repeat-save observations are recorded separately in the validation
report. The co-op adapter changes neither eligibility rule nor XP calculation,
and introduces no callable aliases for these audited history helpers.

`test-results-windows` exercises the actual adapter with a native Win32 thiscall
UI fixture. It verifies the complete result pointer, two stack arguments and
seven-word UTFWin message layout; exact build/symbol gating; terminal readback;
cinematic-before-panel ordering; hidden, disabled and foreign UI rejection;
mandatory reward waiting; synchronous and delayed Saved publication; reset
ownership; and two thousand repeat polls without a second native button call.
It passes with zero failures. Native forced-Done progression and persistence
remain separate lab evidence in the delivery report.

The first forced-save native candidate (C3) exposed the pointer-category error
above. It passed the synthetic fixture because that fixture also incorrectly
modeled `results+18` as an IWindow. In the private guest, `sui::visible` invoked
the layout's incompatible virtual slot and faulted at preferred `81068D` with
ECX `3F800000`, through DLL frames `adventure_results::prepare_exit` and
`sui::visible`. The upstream nearest-symbol label `FindWindowByID+3D` is
imprecise here: disassembly at `810670` is a container destructor. The wrong
virtual-interface call reached that destructor. This was before native Save/Done dispatch, not evidence of a bad
results-handler or EndCinematic ABI. The private crash report is
`out/windows-lab/installation/SporebinEP1/mods/logs/crash-20261025-172230-i84.txt`;
the failed case is `out/lab/outcome-candidate3-guest-cinematic-downed/result.json`.
C4 replaces the invalid window cast with the verified container getter and
extends the actual Win32 fixture with distinct UILayout, container and IWindow
objects. Its window mocks explicitly reject a UILayout argument. This revised
fixture passes with zero failures. Paired acceptance of the corrected candidate
is recorded separately in the preview 12 validation report. The private C4 case
completed host Done while the downed guest still had its terminal cinematic:
the guest recorded one cinematic completion, one native Done dispatch and one
Saved choice, and both peers returned to Galaxy without a native fault.

### Private cold-start Data directory diagnostic

One private C4 bootstrap attempt stopped before frame zero with native error
1004, before the outcome adapter could execute. In the same verified executable,
`7E918B..7E91A9` obtains path key `00A02149` through `688CA0`, tests that path
with `932280`, and reports "Could not find the Data directory, we cannot run."
only when that test fails. `932280` rejects a null or empty UTF-16 path,
`GetFileAttributesW` returning `INVALID_FILE_ATTRIBUTES`, or a result without
`FILE_ATTRIBUTE_DIRECTORY`. This check does not inspect package contents.

The private installation had real Data and DataEP1 directories, and the existing
EA registry DataDir values pointed to the installed game. The resolved path in
the failed process was not captured, so the underlying bootstrap cause remains
unproven. Serial private startup, waiting for the first instance to reach native
Galaxy before starting the second, passed with the same C4 binary and profiles.
This was a test-runner startup constraint; no registry or personal-profile
settings were changed and no new native entry was called by production code.

## Native Adventure Editor working resource and history (dc04)

The editor work uses the same exact executable identity as the GA adapter:
`dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`.
`tools/addrport/verify_editor_dc04.py` checks entry bytes, stack cleanup,
call edges and the relevant field references before rendering
`data/symbols/overrides_editor_dc04aee5.sym`. Imported ModAPI addresses alone
are candidate discovery, not an approval to call them.

### Editor entry and draft ownership

Native ScenarioMode initialization `EF1D20` copies launch request `+28` into
mode `+D0`. Context zero creates the editor input and display strategies and
selects submode `+CC = 1`; quickplay contexts one and two select play submode
two. Normal native Create, Create Adventure, Choose a Planet was exercised in
the hidden, muted owned profile `winlab_ga_editor_probe_85`. Choosing Alpine
Valley produced request `(entryKind +24 = 1, context +28 = 0)`, a zero adventure
root key and the selected planet key at `+0C`. It entered Scenario with
submode one. This is a different entry from the verified Sporepedia quickplay
pair `(2, 1)`. A party's quickplay interception must require that exact pair.
The second owned profile remained in Galaxy. No personal instance received input.

The actual editable draft is mode `+74` ScenarioData, then data `+10`
ScenarioResource, not an immutable resource-manager record. The live draft is
native owned. It must not be replaced by writing an EditorSaves or Pollination
file underneath the open editor. Snapshot capture calls the previously verified
ScenarioResource Write into an owned, bounded stream outside Present.

The resource has acts at `+23C` (stride `534`, at most eight), classes at
`+2BF4` (map pair stride `27E8`, class key at zero, class at eight) and
placements in an indexed pool at `+2C10` (row stride `238`, flags at zero,
marker at four). The row word is not a marker ID. Bit 31 marks a free slot;
live marker identity is its row index. `F40F90` frees a pool slot without
compacting survivors, and `F2EBC0` reuses a free index or appends a slot.
Marker position `+04`, orientation `+10` and scale `+20` are global authored placement fields. They are not independent
per-act transforms. Marker `+228` is a runtime identity field, not a substitute
for persistent project IDs. The adapter retains an explicit stable object to
native pool-index binding across reconstruction; class, placement and act IDs
occupy separate initial namespaces. New host-assigned IDs must retain bindings
instead of being derived again after a native index reuse. A reused slot gets
a fresh incarnation. Parsed snapshots can have `marker+228 = FFFFFFFF`, so
that runtime field cannot replace the owned pool-index binding.

### Owned snapshots and native reconstruction

`F2E670` is thiscall ScenarioResource constructor, returns the resource in EAX
and uses a `2CB0` byte allocation. The native history call site `F45865..F458A0`
constructs that object, adds its initial owned reference through vtable slot
zero, then deep copies the live resource. Its initial reference count is zero;
native Release is slot four and releases the complete allocation. The matching
allocator is `F47240`, cdecl size, allocation name, then four zero words. It calls
GeneralAllocator `16C8B44` through `928C80`, and pairs with plain free `F47220`.
Both the resource deleting destructor `F2E9C0` and history-entry deleting
destructor `F3CBE4` call that plain free. The superficially similar existing
`Memory::operator_new_named` at `9262B0` is incompatible here: it allocates an
extra eight-byte allocator header and returns an adjusted pointer; its matching
delete `9262F0` subtracts that header. This distinction was caught by static
review before testing the new adapter in a game. No native resource or history
entry may mix these allocator families.

`DFFA00` is thiscall `(destination, sourceResource*)`, returns the destination
in EAX and pops four stack bytes. It deep copies the authored resource,
including strings, properties, acts, class and marker containers, embedded
resource data and the native editor camera fields. Capture bytes are the raw
ScenarioResource Write format, not an outer DBPF container. Read into a freshly
constructed owned resource avoids depending on previous container contents.
The ResourceObject key at `+08` is kept separately from serialized draft data.

The input stream ABI is byte-count Read at vtable `+30`, not a bool result:
`F2FD9F..F2FDB0` calls that slot and compares full EAX with the requested byte
count. Primitive helper `93A6F0` converts its own result to bool. Write uses
bool AL. The read-only snapshot adapter rejects seek, resize and writes beyond
its owned bounded input; its output counterpart is capped at 32 MiB.

The dependency collector can read captured bytes into an independently owned
ScenarioResource, traverse the existing dependency closure, normalize server
references on that private copy and serialize the normalized root. Guests must
restore the normalized returned root bytes. This operation changes neither the
live draft nor a loaded profile database. The source resource key remains an
explicit argument; a newly created project does not need a first native save
merely to inspect its captured body.

### Native gesture and Undo boundaries

History lives at global `16065D8`: entry vector zero, cursor `+14`, not-editing
flag `+20`, owned in-progress entry `+24`. `F45810` is thiscall
StartHistoryEntry `(ScenarioData*)`, with no stack arguments. It obtains an
owned before snapshot and creates the native history entry. `F45920` is the
matching CommitHistoryEntry: it deep copies the after snapshot, appends one
native gesture and releases the in-progress entry. Neither function returns a
mutation permission. Suppressing this void Start call would not prevent its
caller from subsequently changing the draft, so it is not a valid edit lock.

`F3CAE0` constructs a real native history entry in `18` bytes and pops `0C`:
`(parent ScenarioData*, beforeResource*, afterResource*)`. Parent `+0C` is
borrowed; snapshot pointers `+10/+14` are owned references. The adapter can
retain an entry while the normal Commit releases its own reference, then
serialize both snapshots in an outside-frame task. Deferred tasks must verify
the same native data owner and project generation before publishing a gesture.

`F46400` is thiscall `(realHistoryEntry*, snapshotResource*)`, pops four bytes.
It clears the active native tool through manager `15AD298`, cleans the scene,
deep copies into the existing draft resource, reconstructs actors, rebuilds
the current editor act and sends `072BDB11` synchronously. Native history
Undo `F464B0` passes the entry's before snapshot; Redo `F464C0` passes after.
Remote reconstruction must retain the local camera resource bytes `+2C30..4F`
and must separately account for current local selection/tool ownership.
`EFC410` is the no-argument cdecl editor-act accessor, not the runtime play act.

Native placement create/clone/delete, pre-mutation input fencing, selection
preservation and the normal asynchronous Save As/playtest-return lifecycle
remain separate verification gates. A working snapshot copier does not prove
those operations safe. The following entry receipts enable initial placement
operations; actual paired runtime coverage is recorded separately.

### Verified placement operations and initial UI fences

`F44210` is thiscall `(ScenarioData*, classID, position*, orientation*, scale)`,
pops `10`, and returns a new live marker pointer. A negative scale selects native
class defaults. It inserts a fully constructed marker, creates all current act
data, instantiates the actor and registers both object maps. `F44460` is thiscall
`(ScenarioData*, const sourceMarker*)`, pops four, and returns a new live marker.
It uses deep marker-map insertion `F2EBC0`, preserving opaque per-marker data.
This is appropriate for Clone and for restoring a deleted placement from its
owned native artifact. It must merge into the current resource; restoring a
whole old scenario would overwrite unrelated accepted edits.

`F428A0` is thiscall `(ScenarioData*, marker*)`, pops four. Its native `F416C0`
route cleans the actor and frees the resource marker pool slot. Other slot
indices remain unchanged; a subsequent create can reuse the freed index. The
adapter retains owned actor references
across a gesture, asks the existing verified GetMarkerForObject for surviving
actors, and records distinct before and after stable binding maps. The deferred
gesture serializer uses those explicit maps, not the latest global mapping.

Editor context pointer `16C7B88` contains current act zero and the owned selected
GameData at `+04`, with auxiliary path-node selection at `+10`. Tool manager
pointer `15AD298` uses vtable `148B708`. SetSelection `EFE810` is thiscall
`(manager, GameData*)`, pops four, and updates selection and its UI. After scene
reconstruction the adapter resolves the selected stable placement to its newly
created native actor and reselects it. Deleting that placement clears selection.

ToolAction `F03770` is thiscall `(manager, action, rawWord, rawWord)`, returns a
32-bit handled result and pops `0C`. Action one starts a selected-object drag,
six changes scale and eleven enters clone or path-node handling; action zero
finishes an already started gesture. PlaceClass `F00670` takes a class ID and
returns bool AL, pops four. DeleteSelection `F02CE0` takes no stack arguments.
The shared adapter gates those mutation entry points on the actual held leases
before calling native mutation code. Place/clone require short structural
ownership. Unsupported patrol-node editing is held before its mutation path.

ImportClass `F462F0` is thiscall `(ScenarioData*, const ResourceKey*)`, pops four,
and returns the new class ID or `FFFFFFFF`. The initial shared preview holds it
before its class-map insertion and counter change. Palette templates can be
prepared in the normal native editor before hosting. Class import is a separate
future semantic transaction, rather than an unvalidated change to a shared
class. Known act timeline, global/terraform, avatar, raw playtest, reset and
Save/Done/Cancel controls are disabled through native UI flags while sharing;
the adapter owns references and restores their original enabled flags on exit.
The collaboration coordinator and verified native save adapter own those flows.

Authored goals use the same reviewed layout as `adventure_catalog`: act `+84`
is a fixed vector of up to three `188`-byte goals, type at zero, primary class
at four and secondary class at eight. Initial projection conservatively retains
every existing placement matching a referenced class. The avatar is outside the
ordinary placement projection and is not an editable placement. Private incoming
snapshot validation parses into an owned resource and checks the canonical
class/act roster, stable placement bindings, finite transforms and original
objective references before a live scene can be restored.

ScenarioData dirty state is separate from resource serialization: last saved
native revision at `+80`, ordinary authored dirty at `+84`, and imported/new flag
at `+85`. Accepted remote placement changes set ordinary dirty only. Local
rollback snapshots retain these fields and restore them on failure; they are
never serialized as network artifact metadata. Setting imported/new merely to
make a Save button active would alter the native save lifecycle and is forbidden.


### Editor UI mutation ownership and chronological native history

Native manager Undo `ED9B20` and Redo `ED9B60` change history cursor `+14`
before calling the entry virtual method. Shared history therefore intercepts
these manager entries before any native cursor movement, in addition to the
entry-level fallback guard. Each original native Start/Commit gesture remains
one shared gesture. A rejected shared Undo does not advance native history.

Editor display `ED5130` owns cast palette UI at `+14` and a separate authored
script UI at `+18`. Script constructor `EDD480` installs IWinProc vtable
`148A760` at script `+08`; its `+18` callback is `EE4290`, thiscall on that
interface, with source window and message pointer arguments, bool AL and
`ret 8`. The callback handles authored act, goal, class and text mutations.
While the shared editor owns this exact script object, the initial adapter
leaves these events unhandled without calling those native authored setters.
Independent tool/camera event handlers remain available. This is a native
pre-mutation fence, not an after-the-fact snapshot rejection.

Generic class/act property binding in `F08DFB..F08E28` follows mode `+14`,
display `+18`, script `+18`, then property UI `+10`. That last field is a
`UILayout`, not an `IWindow`: `EDEB50` constructs it through `8100A0` and
stores it at `+10`. The adapter uses the already verified
the layout-scoped FindWindowByID `810650` to resolve its exact property root
`073E6BA0` before disabling that owned subtree outside Present, retaining and
later restoring its original enabled flag. GetContainerWindow returns the
shared parent layer and must never be disabled for this purpose. Cast palette
placement is separate and remains available. Initial shared classes, goals,
act selection and global properties are read-only; class import returns the
native failure sentinel before the class map changes. Unsupported controls
are nonfatal blocks, not a request to freeze or discard the shared document.

The working snapshot also exposes the authored avatar key at resource `+130`
and lock flag `+175`; read-only accessors use the currently normalized live
resource. These fields are distinct from the source adventure ResourceObject
key at `+08`. Preview captain rules must use the former rather than treating
editor entry as Sporepedia quickplay.

An authoritative incoming or recovery full snapshot has no local dirty metadata:
after successful native reconstruction it marks ordinary authored dirty `+84`
only. It leaves saved revision `+80` and imported/new `+85` unchanged. In contrast,
a local rollback carries its captured metadata and restores all three exactly.
This keeps a recovered unsaved draft eligible for the normal native save checks,
without bypassing CanSave or fabricating an imported creation.


### Editor readiness and the thumbnail flag

First hidden native testing found data `+124` was zero in a fully usable native
Adventure Town editor and in its ordinary Test Mode. Real native Play
`07104131` entered `CC=2,D0=0` with the authored Blue Soldier; real Hammer
`07104130` returned to `CC=1` retaining the working data. The field stayed zero
after 160 and 100 rendered frames respectively. Raw `F3D740` calls thumbnail
request builder `F3CDB0`, whose `F3D067` path clears owner `+124` while creating
128/256 pixel render requests. The imported name mHasLoaded is not a stable
editor readiness contract and must not be set or used to unlock capture.

Native editor shell Load `ED5C70` constructs a UILayout via `8100A0`, stores
it at display `+24` at `ED5CCC`, then calls `8121C0`. Failure clears that field
at `ED5D06`. The editor readiness predicate instead requires editor submode
one/context zero, a valid working resource, an initialized shell layout and
readable owned container, and inactive native loading screen `+370`. It uses
read-only fields; capture and all reconstruction still execute outside Present.
The initial runtime observations and screenshots are under
`out/editor-native-lab/87-ordinary-native-preview*.png`; these are ordinary
native editor checks, not a shared multiplayer playtest PASS.

Locally constructed history entries also start with reference count zero at
`+08` (`F3CAE0`). Their primary Release vtable `148D270` points at `7B87B0`,
which destroys only when decrement reaches exactly zero; releasing an unowned
zero-reference entry would leak it and its two snapshot references. The restore
adapter acquires its initial AddRef before the owned entry wrapper releases it.
This matches native history-vector ownership rather than relying on a negative
reference count. C4 already contained this AddRef before its functional
roundtrip. The static ownership audit confirmed the existing code; it did not
establish a runtime leak or change production ownership.

C4 live UI testing caught that distinction: disabling the property layout's
GetContainerWindow disabled shared editor layer `2EDD95CA` (`fl=11`), preventing
palette input even though camera movement remained possible. No native edit
was accepted during that test. The corrected fence uses `810650`, thiscall
`(UILayout, controlID, recursive)`, `ret8`, with native property-root caller
`EDEC46..EDEC52` requesting `073E6BA0`. Its tail helper `8102A0` searches the
layout's owned object list `+64/+68`, then children, instead of treating its
parent layer as the property's window. Paired editing acceptance is tested
after rebuilding this correction; SaveAs and baseline convergence already
passed in C4 and are not evidence for working local mutation input.

Known Save/Done/Cancel, reset, act and global controls are resolved through the
current display `+24` UILayout, not the global cached window tree. C5 inspection
found an enabled live Done/disk alongside a disabled stale duplicate; global
first-match lookup did not establish an effective UI fence. The adapter also
requires the actual active game mode to be Scenario before reporting editing,
preview or readiness. Leaving/re-entering Scenario invalidates its native
generation even if a retained ScenarioMode/data allocation is reused.


### Shared gestures require an independent current before snapshot

`F45810` optimizes native history: when cursor `16065EC` is nonzero it queries
the preceding entry and retains that entry's after resource `+14` as its new
before snapshot. Only when that entry is unavailable does it allocate and copy
the current live resource. This is correct for ordinary native-only edits, but
remote and canonical shared application does not append matching native history.
The previous native after resource can therefore be stale.

C5 underbudget Alpine Valley exposed this directly: the durable journal
recorded one accepted Create followed by its accepted own Undo. A later actual
Move failed closed with a missing stable binding. Read-only native history
inspection found the latest before resource still contained the second slot
from before that canonical Undo, while its after resource correctly contained
one live actor and one free slot. The evidence is `out/editor-native-lab/c5-frozen-history-pools.json`.

The shared adapter now allocates and deep copies the current live resource at
its authenticated Start boundary, independently of native history's optimized
before pointer. It retains that owned copy and the exact before bindings through
the native gesture, then serializes both sides outside Present. Replaced,
cancelled and stale-generation tasks release both references. A valid native
history entry remains the source of the completed after snapshot. This avoids trusting stale native history or falling back to a full old
scenario overwrite. Actual mutation acceptance
was rerun in C6: an actual drag of the original Ace committed on both peers at
revision two with matching native projections. C5's durable own Undo remains
distinguished from a native Forward Delete. This C6 pass does not establish the
other structural operations or complete editor acceptance.


### Raw native commands, mode ownership and detach rebaseline

Save command dispatcher `ED75E0` is thiscall `(UIbase, commandMode)`, `ret4`
at `ED78BB`; real UI callers `ED7FC0/ED7FE9` and keyboard dispatcher `EDA319`
reach it before UI pending mode `+58` is written at `ED760B`. The shared save
adapter holds this raw path before native Save, Done, Cancel or New can change
the resource or leave the editor. The explicit owned save API uses verified
`ED7390` instead, with native asynchronous dialog completion and independent
record readback retained. `ED4A80` pending SaveStay mode two clears `+58`
without calling the guarded command/display dispatcher.

The already verified SelectDisplayMode `ED6620` is also held before raw Play
or Terraform. A narrowly owned API scope permits actual group preview entry
and return. Native initial Build mode zero is allowed for the current owned
UI in editor submode, with active display `+1C` at `-1` or zero, so asynchronous native
editor entry and an idempotent Build refresh can finish normally. Unrelated
ordinary native editing retains the original handlers. Save and preview
ownership checks require the adapter's actual active Scenario context, not a
retained but inactive ScenarioMode allocation.

After canonical restoration has settled, `rebaseline` retires the native-only
history using the separately verified `EraseHistoryRange` and
`DiscardHistoryEntry` composition. It requires no native gesture or queued
adapter captures and invalidates previous callback generation. Cursor `+14`
resets, serial `+18` increments with an overflow guard, and the native dirty
counter predicate is preserved: a previously clean saved counter `data+80`
advances with the serial; an already dirty counter stays old. Authored and
metadata flags `+84/+85` remain intact. This prevents ordinary native Undo after
shared End from restoring a stale pre-collaboration snapshot. Exact reset
receipts are in `docs/re/adventure-editor-history.md` and its separate verified
symbol map, rather than guessed vector memory writes.


### Serialized marker bindings and the actual native clone entry

C6 accepted removal of the original Ace from native pool slot zero while slots
one and two survived. Both native scenes retained those survivor identities,
but private snapshot validation failed with a missing stable binding. The
serialized resource does not retain free slots at their original indices:
native Write emits live markers in pool order and native Read builds a dense
pool. A binding table associated with owned serialized bytes must therefore
name that dense marker order, rather than the live indexed-pool offsets.

Capture projects stable identities against the live pool and converts only
its snapshot binding table to serialized order. The current live binding map
is retained separately. Artifact lookup, private validation and restore use the
serialized table; current native actor references resolve their stable identity
against the live map. Incoming resources, checkpoints, redo artifacts and
recovery all carry the serialized table. The portable mapping fixture covers
all 64 possible single holes in a 64-row pool, slot reuse with a distinct
incarnation, reordered input, duplicate slots and duplicate or invalid IDs.
These mapping checks do not substitute for rerunning actual native Delete,
Undo and Redo on the rebuilt candidate.

Native Alt-drag calls clone-selection entry `F00730`, thiscall
`(toolManager, sourceGameData*)`, returning bool AL and popping four bytes
(`F00877`, `F00880`, `F009D5`). Its ordinary clone branch calls native Start at
`F008D7`, hides the source's cursor representation, calls CloneMarker at
`F0096B`, selects the clone and leaves it held by the native cursor. This
entry can be reached directly as well as through ToolAction eleven. The
shared adapter gates this full entry before those changes, and records its
source stable identity before Start. Restricting interception to ToolAction
alone missed the real Alt path and mislabelled its result as Create. C6 real
Ctrl-drag was ordinary movement; the acceptance fixture now uses Alt-drag.
The new entry is verified in the exact image map and verifier.

The native active display is `UIbase+1C`; `+20` is its separately remembered
return display. `ED57A0` writes the former at `ED5801`; `ED47A0` writes the
latter. In ordinary C6 Build, read-only observation found `+1C=0,+20=-1`.
Readiness requires actual active Build zero, without treating a remembered
return value as current UI initialization.


### Native deletion must settle before a structural replacement

The isolated C6 pair rendered both newly-created Ace placements on the guest,
while the host's local inverse-delete followed by immediate accepted recreation
left the new host model absent. The original visible host Ace still accepted
ordinary Move. Projection equality alone did not establish a rendered,
interactable editor scene.

Exact native cleanup explains a required lifecycle boundary: `F416C0` first
uses `F3FC30` to unregister the old actor, frees its marker slot through
`F40F90`, then publishes class message `07311FF7`. `F3FC30` calls noun-manager
removal `B22560` at `F3FDBB` before erasing the actor/marker maps. That removal
marks the old actor's byte `+20`, retains an owning reference and queues it in
the manager's deferred range `+80..88`. The normal manager model-update path
retires that range at `B22B09..B22B1D`. Holding actor references or removing a
scenario map entry is therefore not proof that native rendering lifetime has
ended. A structural inverse and immediate replacement in one outside-frame
callback can reuse a freed marker slot before normal cleanup completes.

The integration must let the normal native scheduler cross this boundary while
keeping editing input and the pending request fenced. It must not call an
unverified manager drain or force actor destruction. New host models, actual
selection and movement, repeated create/delete, and both sides' scene output
remain native acceptance checks after this ordering correction.


Native Create tests prospective pool index against `400` at `F44267`, and
Clone does the same at `F444DE`, before insertion. This adapter consequently
limits native projected/decoded rosters and snapshot bindings to 1024 while
leaving the portable editor document's generic limit independent. Private
native Read results are checked before live reconstruction; every live restore
also requires matching canonical projection validation, including empty actor
rosters. Applying an oversized native document fails before capture or mutation.
The mapping fixture accepts 1024 distinct slots and rejects 1025 while leaving
its previous output intact.


### Opaque authored edits require a private comparison

The semantic document intentionally exposes only the initial placement fields.
Class/act ID equality and an empty pose diff do not prove that native authored
bytes stayed unchanged. Before accepting a native gesture, `supported_changes`
privately parses its owned before/after resources and constructs normalized
comparison resources. It retains their global, class, act and avatar data and
uses matching surviving marker pairs in stable-ID order. Clone pairs compare
the source's before marker with the new marker; ordinary Create comes from the
verified native defaults, and deleted/new roster entries are excluded from the
pair comparison. This detects unsupported edits even when the semantic mutation
list would otherwise be empty.

The new private-only helper `DFF300` is thiscall `(destination row vector,
source row vector)`, returns the destination and pops four at `DFF42C`.
It deep-copies or destroys owned `238`-byte marker rows and updates vector end;
it does not operate on a live scenario scene. ResourceCopy calls it at
`DFFC4C`, then copies the pool's `+14/+18` free-index metadata at
`DFFC51..DFFC5A`. The validator uses that exact owning composition with an empty
native-constructed resource, rather than replacing vector pointers manually.
`F2EBC0` is thiscall `(marker pool, const marker*)`, returns the new index and
pops four at `F2ECEB`. Native Create and Clone both call this owning deep insertion.
The helpers are named and verified in the editor override map.

The comparison normalizes position `+04`, rotation `+10` and supported scaling;
related normalized scale `+24` is ignored only when actual scale `+20` changed.
Placement validation `F412C0` derives marker flags `+224` bits `10/02/04`, so
only mask `16` is normalized. Other flags and elevation `+220` remain covered.
Only local camera fields `2C30..2C4F` are normalized globally. Two final native
Writes compare the complete normalized resources, avoiding a separate Write
for every individual marker. An unknown difference fails closed; it must not be
silently dropped or declared a new supported property.

Native PlaceClass also rejects special class `FFFFFFFE` during shared editing:
`EFEDC0` routes it to avatar placement `F432C0`, outside the shared placement
roster. The authored avatar remains protected. This is distinct from placement
validation `F412C0`, which is called by normal drags and must not be broadly
blocked as though it selected a captain.

Static receipts are under `out/editor-native-re/opaque-query.txt`. Actual valid
Move/Clone/Scale and negative UI coverage must run on the rebuilt candidate to
establish that these checks reject unsupported edits without rejecting allowed
native behavior. Native projection and serialized-binding fixtures alone do
not establish that result.


### Native chronological history availability (dc04 editor candidate 8)

The native command handler at `EDA1F0` gates Undo command `C9D86390`
through `ED4750` at `EDA4D4`, then reaches native history Undo at
`EDA4F9`. Redo command `C9D86391` calls `ED47C0` at `EDA528` before
native history Redo at `EDA54D`. Both availability getters are
`bool __thiscall(editorHistory*)`, with a plain return. `ED4750` requires
history `+20` and cursor `+14 > 0`; `ED47C0` requires `+20` and cursor
less than vector size. Co-op Undo deliberately leaves the native cursor
unchanged, so intercepting the manager alone prevented native Redo inputs
from reaching the co-op history callback.

The adapter hooks the two verified availability getters only for the exact
native history object while shared editing owns it. It returns the cached
co-op chronological availability; unrelated editors and ordinary native
editing delegate to the original getters. The native command's existing
modal/input checks remain in place, and no resource or UI work occurs in
these getters. Actual rebuilt native toolbar/shortcut acceptance remains a
separate required check. Exact instruction and caller receipts are checked
by `verify_editor_dc04.py`; the editor override map now has 31 entries.

### Placement investigation: distinguish native terrain locations from reconstruction

Candidate 7's Alpine fixture created a placement at screen `(650,470)`.
Both marker and creature spatial, animation root and model translation were
at radius 500. Its apparent visible creature could be a cursor thumbnail:
there was no corresponding new cast-member window, and genuine subsequent
selection and movement did not select that exact marker. A fresh ordinary
native Alpine editor, before any shared session or snapshot Restore,
reproduced the same radius-500 placement and failed selection at that
location. A native placement at `(601,470)` instead had radius about 559.6
and genuine subsequent movement advanced native history and changed its
marker position. Therefore the radius-500 result alone does not establish
a co-op reconstruction defect. The pair fixture now leaves the verified native `(601,470)` point free by
first moving its original actor aside through ordinary native input. It still
requires actual selection and movement of each newly created stable identity.

Negative RE findings: `F035D0` finalizes a native placement by calling
`EEF6F0` before history Commit, so its authored marker capture is not
simply missing a later terrain-grounding write. Its post-Commit cast
`074E0069` is null for the Ace creature (`C06500 -> C02930 -> C0B7D0`),
so the conditional `+12B/+128` finalizer cannot repair this fixture.
`F41350` refreshes placement-validity flags and their derived invalid style;
`F40040` validates markers through `F3FF30`. No speculative call to these
functions or authored position/radius adjustment was added. Receipts are
`out/editor-native-re/c7-ground-query.txt`, `c7-finalize-query.txt`,
`c7-render-ui-query.txt` and
`out/editor-native-candidate7-clean-native-baseline`.


### Native cast icons group nearby placements (dc04 editor candidate 8)

The apparent second-actor rendering/selection failure was a fixture error.
Native marker, spatial, animation-root and model transforms matched the
committed document. The editor deliberately combines nearby cast icons into
one stacked icon, so the projected foot point of one member does not necessarily
have its own clickable window.

The executable references cast window ID `100A0000` at `EBEBB6` and
`EBF851`. `EBF540` clears the current widget groups, visits the selected marker,
avatar and every live marker, and populates groups through `EBF220`. It then
merges overlapping rectangles with `EBF4C0`. A group owns its marker-pointer
vector at `+14/+18`, native layout at `+28`, and rectangle at `+04..+10`.
The final pass draws stacked frames `100A0002/100A0003` and count label
`100A1000` for groups with multiple markers. `EBEB50` returns the first marker
of a hit group. The selected marker is exempt from merging: `EFC800` reads the
owned native selection at Context `+04` and resolves its marker through
`EEBE80`.

This exactly explains the tests' doubled frames. At whole-planet zoom a typical
icon is 50 pixels wide. The clone at x538 overlapped both original x503 and
source x572; retaining the old source selection left the clone grouped with the
original. Ordinary native cloning selected the new clone and kept its separate
icon; clearing selection let it join a stack. A separated ordinary native clone
at x434 retained its own icon after Escape, and genuine selection/movement
changed only that exact new marker. Similarly, creating a second actor at x430
after moving the first to x463 produces overlapping icons.

The adapter adds only a local `select_placement` helper using the already
verified `SetSelection` and marker-to-object lookup. The coordinator can select
its own newly committed Create/Clone, matching normal native editing after the
optimistic temporary object is replaced. Remote commits retain the remote
editor's independent selection. This selection does not change authored bytes,
camera or native dirty flags. It does not disable native grouping or change
actor positions. Tests should use separated icons or the normal native group
selection UI, rather than treating a missing per-marker hit rectangle as a
missing model. No speculative render finalizer, radius correction, pool-flag
write or repeated full-scene reconstruction was added.

Receipts: `out/editor-native-re/c8-cast-widget-query.txt`,
`out/editor-native-candidate8-separated-native-clone/result.json` and
`out/editor-native-candidate8-second-clone/pool-view-readonly.json`.
Canonical pools had correct sentinel bits and heads; the low free-list bits in
a reused live row are not a rendering fix. Candidate 8 also proved genuine
native Ctrl+Z/Ctrl+Y and explicit-Ready native preview simulation/return; see
`out/editor-native-candidate8-native-history` and
`out/editor-native-candidate8-preview`.

### Native class-property binder requires its own input guard

C9 exposed a real read-only UI gap. Disabling root `073E6BA0` left its
descendant sliders enabled (`0xB`), and their separately registered native
binder handled speed-slider input despite the ScriptUI `EE4290` guard.
The opaque-change validator rejected the edit and restored revision 12.
Immediate native-byte equality after the slider was rollback evidence, not
proof that the slider never changed authored data: the recorded UI tree already
contained rejection notifications before the close button was pressed.
The reproduced sequence and read-only native-history inspection are under
`out/editor-native-candidate9-class-property-negative` and
`out/editor-native-candidate9-transform-probes`.

Verified exact dc04 receipts: constructor `EDEA20` installs vtable `148A7A8`
at class-settings object `+0C`; `EDEB50` registers that interface directly on
root and descendant controls. Its IWinProc `+18` is `EE5380`, thiscall
`(classSettings+0C, window*, Message*)`, `ret8` (including `EE6716`).
`EE53C8` loads the second stack argument as Message, `+08` is its type, and
`EE59CC` tests control `0742CD10` at Message `+0C`. The function contains
class/act setters and native Start/Commit calls, so blocking only its parent
ScriptUI callback cannot protect these controls. Static receipts are in
`out/editor-native-re/c9-class-boundary-query.txt` and
`c9-class-input-query.txt`; the exact executable verifier now covers 32 symbols.

The adapter suppresses this exact owned binder before its setters while
sharing. It permits the normal dismiss component `0595E0A8` activation; that
ID has no authored-setter branch in this handler. Normal nonshared input
delegates unchanged. The UI fence also disables the owned descendants, preserving
that close button, rather than relying on inherited enabled state. Traversal is
bounded to 2048 nodes and retained flags/references to 4096 windows; a changed
native ownership generation restores/releases the old fence before installing
the new one. The pre-entry binder guard remains authoritative between UI ticks.
Rejection diagnostics now report the first differing normalized byte and both
normalized lengths without relaxing any opaque-field check.

These source changes require actual rebuilt native testing. In particular,
retest opening, slider interaction, normal closing, settling, and a subsequent
legitimate placement gesture with unchanged opaque state; testing only an
immediate inspector hash is insufficient. The second generic property layout
created by `F08DD0` is a distinct scope and is not claimed covered merely by
traversing the first owned class layout.

C10's first deployment also demonstrated a loader-path pitfall: the correct
32-entry source map was copied beside `mods/coop.dll`, while the loader retained
the old 31-entry map in `mods/sporekit/symbols`. The missing ClassInput symbol
correctly left the native adapter unavailable; no shared session started.
Always verify the actual loaded target, including its bytes and runtime symbol
resolution, rather than only the source map. The corrected cold C10b startup
resolved ClassInput at runtime `00ED5380` on both owned processes and reported
`available=1`. The bounded pair runner now compares all three installed editor
maps with their verified sources before launching; the acceptance fixture fails
before input if the adapter is unavailable. Deployment and preflight receipts:
`out/editor-native-candidate10/deploy-corrected.json` and
`out/editor-native-candidate10b-preflight.json`.

The corrected C10b pair verified a narrower positive read-only route on both
participants: normal double-click did not open the editable class controls,
authored native bytes stayed unchanged after settling, and a subsequent normal
placement Move committed on each client. This does not claim a visible slider
and close-button test, because the shared opener was blocked before the pane
became editable. Receipt:
`out/editor-native-candidate10b-class-negative-final/result.json`.

### Hidden-lab transform-input diagnostics

The installed EP1 `readme_en-us.txt` lists Shift plus mouse wheel for Scale,
held Tab with the selected object for advanced rotation, and right-click on the
left palette to locate an asset. The last operation genuinely zooms to the
actor and exposes its normal blue rotation ring and vertical handle; it is a
useful ordinary UI route when whole-planet cast icons obscure manipulators.
See `out/editor-native-candidate10b-ordinary-locate/87-native-locate.png`.

SporeKit 0.3.4's queued `input_wheel` posts `MAKEWPARAM(0, delta)`, omitting
mouse modifier bits. The fixture therefore has a bounded ordinary Win32 wheel
helper with current owned HWND/PID checks, `ClientToScreen`, real held keyboard
input and matching `MK_SHIFT`/`MK_CONTROL`/held-left-button flags. It waits for
both native frames and wall-clock delivery before releasing modifiers. This
does not call native game functions or write game memory. The drag helper also
waits at least 120 ms after down and 60 ms per motion fraction: uncapped native
editors can execute frame-only fences faster than ordinary drag thresholds.

C10b ordinary unshared controls after normal shared End still did not produce Scale or expanded Tab
handles with these inputs, so this diagnostic alone is not evidence of a shared
Scale defect. This control excludes the active input fence, but does not exclude
retained tool state from prior native restoration or history retirement. A fresh
ordinary editor before sharing remains a separate control. Native input dispatch
remains a validation gate. Static wheel
receipts are `out/editor-native-re/c10-wheel-query.txt`: `EF0910` forwards to
game-input vtable `+60`, `B196F0`, which emits event-kind 8 through `B1C480`
and then the configured dispatcher `B1BA80`. The ordinary editor's input-mode
global `15ACC34` was 4, satisfying that entry's mode check. Manipulator visibility
`EFCF10` queries game-input vtable `+18`, `B18CD0`, which calls `8D3A40` for
the action hash held at `15AD294` (`07A98D04` in this instance). No speculative
native input-state write or manipulator finalizer was introduced.

Further static checks distinguish the two controls. `EECD40` maps `.crt`
`2B978C46` to category `E34E8A60`, outfitted creature `074E0069` to
`E137FF08`, vehicle `.vcl` to `5B3D1D0D`, and UFO to `D37C1045`.
`EFCF10` calls `F25450`, which excludes these four categories from the
additional three-axis handles. Buildings `.bld` (`2399BE55`, category
`B10E526F`) pass that check. Therefore absence of expanded Tab handles on Ace
is not proof of failed keyboard delivery. In fact, a read-only observation of
the native command table `167BEF0` showed count zero before input, then count
one with hash `07A98D04` while the ordinary Tab key was held. Both Shift-wheel
directions remained unproved in the ordinary editor. `F40840` does not
categorically reject creature scaling: ordinary creature assets delegate to
`EED310`'s authored native min/max and tier bounds; avatar class `-2` and
gameplay properties use separate paths. Receipts:
`out/editor-native-re/c11-transform-eligibility.txt` and
`out/editor-native-re/c10-native-held-key-readonly.json`.

`B3D340` is the existing `cGameViewManager::Get`, not a keyboard-device
singleton. Its `+34` picks the world object; `B18DB0` queries the verified
spatial interface `01186577`. Do not label its spatial visibility/ownership
flags as keyboard modifiers or write them to manufacture input.

The C10b ordinary native Edit route also established a large authored fixture
without constructing synthetic content: normal Sporepedia selection of
Adventure Town followed by native ready reported 419 placements, 118 classes
and five acts, source `408A0000!BF0CE0D0.366A930D`. Gameplay noun counts are a
different measurement. No Town edits or native Save were performed for this
count. Receipt: `out/editor-native-candidate10b-town-count-final`.

C11 subsequently established a genuinely fresh ordinary control: normal
Create opened Alpine Valley, imported Ace and placed one actor, before any
shared Begin or native Restore (`restores=0`). Right-clicking its palette card
and allowing the close camera to settle exposed the blue yaw ring. Dragging its
front arc from `(601,475)` to `(625,435)` changed only the authored quaternion;
position and uniform scale remained identical. The earlier unsuccessful left
arc was a poor input target and is not evidence that native rotation is broken.
Receipts: `out/editor-native-candidate11-fresh-yaw/result.json` and
`out/editor-native-candidate11-fresh-ace-retry/result.json`. The latter fresh
control also tried Shift-wheel down after a modifier-bearing mouse move: scale
remained 2 and the quaternion/position did not change. This keeps native Scale
input acceptance open without attributing it to shared restoration. No native
save or shared session was started during these controls.

The fresh C11 control proves the ordinary front-ring recipe only; a shared
invocation is separate evidence. A bounded shared check can use the calibrated
Alpine/Ace setup and require exact rotation changes with position/scale
preserved on both participants, checking only the intended stable identity. Large
Town diagnostics can truncate the last quaternion, so the fixture accepts only
complete four-component rotation fields and never treats a partial last row
as a native pose.

The subsequent C11 shared front-ring check did not rotate the actor: three
ordinary attempts increased the native rejection counter by three, without
starting native history, changing revision zero or freezing either peer. The
local selected identity remained valid, and no waiting-for-ownership status
appeared. Both participants returned normally to Galaxy. This is a failed
shared-rotation check, not a successful extension of the fresh ordinary control.
Receipt: `out/editor-native-candidate11-shared-yaw/actual/result.json`.

C12 static investigation corrects the earlier `Context+10` patrol-only label.
It is the active auxiliary manipulator. `F03770` action 1 with nonzero `+10`
and no existing dragged object calls `EFEB70`; that function owns `+0C`, starts
the manipulator through its interface and invokes native StartHistory. The
normal frame dispatcher `F02B30` then calls `F02040` while `+10` is nonzero.
`F02040` dispatches by the manipulator's native type. Its `07ABDD8D` branch
copies the resulting quaternion to marker `+10` and applies marker state;
the `07A81824` branch also writes that quaternion except for its distinct
`Context+14` special case. `EFCF10` explicitly compares `+10` against the
rotation-handle pointers at `+18`, `+1C` and `+20`. Other branches modify
per-act paths, secondary orientation or creature dimensions, so allowing every
nonzero auxiliary would expose unsupported authored edits. Receipts:
`out/editor-native-re/c12-tool-context.txt` and the earlier
`out/editor-native-re/c11-transform-eligibility.txt`.

The diagnostic-only C12 adapter retains its fail-closed auxiliary guard and
logs the actual rejection cause before returning: auxiliary context, empty
selection, missing or denied placement gate, or missing or denied structure
gate. It includes action, selection count, context and selected/auxiliary
pointers, the three rotation-handle pointers, native readiness, generation and
the cause's count. A global 250 ms
and per-cause one-second throttle bounds repeated input logging. These logs
read the already-owned context and state; they perform no resource capture,
UI traversal or new native calls. The isolated exact-CI i686 translation unit
compiled with `-Wall -Wextra -Werror`; an actual C12 diagnostic run remains
separate evidence.

The isolated C12n run established that exact cause before native history could
start. All three host front-ring attempts logged `context-auxiliary`, action 1,
one selected placement, ready 1 and generation 3. Auxiliary `1A487DA0` exactly
matched `rotation20=1A487DA0`; the other handle pointers were distinct. The
shared revision stayed zero, neither peer froze, and normal End plus native
Quit returned both to Galaxy with zero faults. C12n deliberately contained the
new diagnostic and Runtime grant grace with the older Session object; it is not
evidence for the later Session release fences. Receipts:
`out/editor-native-candidate12n/rejection-logs.json`,
`out/editor-native-candidate12n/shared-yaw-retry/result.json` and
`out/editor-native-candidate12n/handoff/result.json`. Its first attempt timed
out in redundant fresh-start creator-radial fixture clicks before editor entry;
that failure is retained separately in `shared-yaw`.

The subsequent narrow policy permits nonzero auxiliary pointers only for
actions 1 and 6, only when the complete context through `+20` is readable, and
only for an exact match with `+18`, `+1C` or `+20`. Stable placement selection
and its exact lease are still mandatory. Every auxiliary action 11 and every
unknown or path auxiliary remains blocked before native mutation. The pure
policy fixture covers all three rotation identities, unknown identities,
missing readable handles and clone rejection. Actual shared rotation after
this change remains a separate native acceptance check.
The binding/input-policy fixture passes 8,241 checks on both the portable host
and actual i686 Win32 executable, including 17 new auxiliary-policy cases.
The production native translation unit also compiles with the exact CI i686
toolchain and `-Wall -Wextra -Werror`.
