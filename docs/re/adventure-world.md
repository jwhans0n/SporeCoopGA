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

Act music is `act+80`. Stock music may directly identify audio, without a PaletteItems
prop. The installed Welcome adventure's `2D67FCE4` resolves in `Spore_EP1_Data.package`
as `021407EE!2D67FCE4.01A527DB`, with a `.02B9F662` companion. Treating every music ID
as `D87454E6!ID.00B1B104` incorrectly rejected this stock adventure. Collection first
checks the palette prop, then the native audio key, and requires a vanilla database.

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

The aggregate scenario vehicle update is `F39DF0`, thiscall `(strategy,uint64 dt)`,
`ret8` at `F39E65/F39FAE`. It enumerates vehicle noun `18C6DE8`; its adventure branch
calls `C9F460` and `CA9850`, then behavior-tree tick `BC9160` at `F39F69`. Graphics
update `CA3380` at `F39F77` and adventure combat update `EF1850` at `F39F91` follow.
The generic tree tick is cdecl with nine dwords; argument five is the behavior agent
(`vehicle+5D0`). A per-agent tree filter can suppress guest vehicle AI while retaining
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
