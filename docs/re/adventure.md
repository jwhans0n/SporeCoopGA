# Galactic Adventures co-op entry and rules

The implementation targets the installed March2017 Galactic Adventures executable,
SHA256 `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`.
See [build-dc04.md](build-dc04.md) for compatibility and isolated native Windows
testing, and [adventure-world.md](adventure-world.md) for content and world interfaces.
Addresses below are preferred PE addresses. Runtime code resolves named symbols.

## Terminal outcome control ownership

`adventure_native::apply` intentionally renders an authoritative zero-health
Downed captain as native 1 HP and temporarily immune. Native death must not run
an independent respawn or results path. The coordinator life state, rather than
that positive native HP, decides whether the captain may act. This presentation
rule remains unchanged.

The control adapter's original Downed predicate required coordinator Running,
and its captain-state reader also stopped supplying state after Ended. A native
results failure or subsequent nonterminal transition could therefore leave a
1 HP captain without its Downed input fence. The outcome latch now supplies an
independent optional terminal predicate, scoped by the native adapter to the
connected co-op Scenario attempt. It holds the exact local avatar's verified
brain, WASD, cursor and order entries even after Running/life state end. The
first real native creature Update clears movement and queued orders once; stable
updates remove residual velocity, correcting only actual displacement. Object
incarnation replacement starts a fresh transition. Reset only forgets pointers.
No native visual Update, camera input, authored health or immunity is changed by
this narrow input fence. The shared outcome's simulation hold owns world-wide
gameplay suspension and terminal/exit sequencing.

The Win32 `test-controls-windows` fixture passed with 1,000 terminal iterations
while the active predicate was false, including all four native callback ABIs,
one-shot order removal, displacement correction, incarnation replacement,
unrelated NPC/remote ownership, release and retry. This fixture verifies the
control boundary; paired native failure, result and shared-exit acceptance are
recorded separately in the preview validation report.

## Downed captain controls and local lighting (dc04, preview 8)

The previous native adapter held a downed captain with `StopMovement` and
`SetPosition` only when a 10 Hz health snapshot was applied, while rejecting its
entire `cCreatureAnimal::Update`. The authority already refused position changes
for downed captains, but this did not stop native input strategies from issuing
movement requests. Remote interpolation could also drive a downed puppet toward
the owner's independently sampled motion. The resulting competing paths are a
confirmed implementation defect. Native frame/camera and overlay-isolation
acceptance below bounds the visual evidence; it does not establish every cause
of the originally reported flicker.

`adventure_control` fences the narrow native control paths and leaves the normal
creature visual/effects/physics update intact:

* `AvatarTickAI=C04020`: thiscall `(creature,float seconds)`, `ret4` at `C040ED`.
  Native Update calls it at `C0B015` after testing the avatar bit. Its behavior-tree
  dispatch is suppressed for an authoritative downed captain only.
* `MoveAvatarWASD=D32330`: thiscall `(inputStrategy)`, plain return at `D32DE5`.
  It obtains the native avatar, tests `mbDead` and strategy `+C0`, reads movement
  triggers and generates avatar locomotion. Calls include `D32E81` and `D32EA9`.
* `MoveAvatarToCursor=D31A70`: thiscall `(inputStrategy,bool,bool) -> bool`,
  `ret8` at `D32329`. `D32E9A` passes `(0,1)`; its return is AL. The downed
  wrapper returns false without generating a cursor movement request.
* `IssueCreatureOrder=C032F0`: thiscall `(creature, seven stack words) -> order`,
  `ret1C` at `C03357`. The third word contains float bits; the wrapper preserves
  all seven words without reinterpreting or inventing an EASTL object.

These entries and returns were inspected in the installed executable with SHA256
`dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`.
They are required capabilities for the adapter and are added to its executable
verification manifest. Unsupported hashes install no control hooks.

An observer around each allowed real creature Update owns the downed transition.
It clears queued behavior orders with the verified `BC9A60` order-list method
and both mask words set to `FFFFFFFF`, stops locomotion once, clears velocity,
and uses the host's saved downed position. Stable updates do not rewrite the pose
or restart an animation. A displacement caused by native physics is corrected
after the actual update, rather than waiting for another health snapshot.
Identity includes the native incarnation, so same-address reuse starts a new
transition. Reset forgets owned state without dereferencing retired objects.
Snapshot application retains authoritative health/immunity and no longer repeats
the stop/pose operation. Downed movement publications and remote interpolation
use the authority's anchor with zero velocity; old remote attack animations are
ignored while that captain remains downed. Camera and UI input are not globally
disabled, and standing captains retain their ordinary control paths.

The legacy `WORLD_TIME` packet is a quaternion, despite its clock-like name.
Static inspection confirms `TimeOfDay_GetObject=1021110` returns the active
planet, while `TimeOfDay_Unavailable=1020F30` reads its current context. Native
`cTimeOfDay::SetTimeOfDay=BC3070` is thiscall `(float time,Vector3* position)`,
`ret8` at `BC30D0`; helper `BC2DC0` computes the planet orientation from both
inputs, then calls spatial `SetOrientation` and `1021630` to apply it. Thus a
host's planet quaternion is not a location-independent time value appropriate
to every captain's local lighting. Scenario no longer sends or applies this
legacy packet, including packets delayed across entry. Each native adventure
retains authored/local lighting. The shared adventure simulation clock used by
objectives, holds, boosts and revival remains authoritative and unchanged.
Other stages retain the legacy time synchronization behavior.

The Win32 `test-controls-windows` fixture exercises the actual four wrappers,
including 10,000 blocked-control/update iterations, raw argument/return ABI,
one-shot transition cleanup, native-update velocity cleanup, displacement
correction, independent remote life state, incarnation replacement and reset.
The actual game Update wrapper fixture checks balanced before/after observation
and no observer calls for rejected updates. Portable tests cover the Scenario
packet policy and downed movement normalization. These fixtures passed; native
Town movement, camera, lighting and flicker verification is recorded separately.

Native acceptance on 2026-10-25 used hidden, muted, isolated instances 71/72,
candidate 3 of preview 8, loaded shadow SHA256
`c0ae5bbc3ab30af32cb558d00eb8fb0410d0ebf4a2f43378173a20bbbd5558fc`.
Both origin cases in
`out/windows-lab/ga_preview8_native_downed_final/result.json` passed in populated
Adventure Town act 1 after the actual opening MoveTo objective. Ordinary input
moved the guest 2.70 m and host 2.91 m before knockout. During Downed, the raw
main-thread `coop.cam` avatar position stayed at its anchor under held movement,
jump and mouse input; the native control counters advanced and camera input
remained usable. Each knockout entered once. Native radius exit and rescuer
damage reset progress, revival restored half health and ordinary movement, and
the two-metre ring/countdown appeared. A native host pause held progress and
the native host clock exactly fixed. This exercises the shared clock pause
predicate, not a combined conversation-and-revival scene.

External asynchronous process reads can see physics integration before the
after-Update correction, including a sampled 0.82 m transient in one placement.
They are retained as diagnostics; frame-boundary acceptance reads native
`game::position`, not normalized network publications. Sixteen rendered frames
under held movement alternated `coop.overlay off/on`; frame-boundary position
was constant and the camera settled monotonically to a native look-ahead offset.
The straight ground shading edge persisted with the overlay off, after revival
at 45 HP, and while alive at 1 HP. That edge is independent of ring drawing and the
Downed control fence. See `ga_preview8_native_downed_render_isolation` and
`ga_preview8_native_downed_health_isolation` under `out/windows-lab`. These
sampled frames do not establish uninterrupted visual or full-playthrough coverage.

`out/windows-lab/ga_preview8_native_lighting_final_verified/result.json` records
the actual twenty-second lighting case. The host moved 63.01 m while the guest
drifted 0.00201 m. The host planet orientation changed 4.95169 degrees; the stationary
guest changed 0.000126 degrees. Real per-peer UDP counters observed zero type 22
packets in either direction. A valid 51-byte legacy type 22 packet used the live
host connection header while the guest was natively paused: a native
`TimeOfDay_Apply` probe stayed at zero calls and the raw planet quaternion/camera
remained identical. The shared host clock advanced 3840 to 26252 ms. No native faults
were recorded, and the handoff report verifies menu/application-state recovery,
released injected keys and no remaining probes. Host/guest quaternion equality
is deliberately not an expectation for location-dependent lighting.

## Party protocol

Protocol version 12 reserves message 52 for adventures. Coordinator messages use
subkinds 0..63, content 64..71, world 128..159, party scenes 160..163, and captain
models 192..223. Unanimous pause uses subkinds 164/165.
The host freezes the roster and difficulty when Play is selected. Every roster member
must verify the content manifest, select the required/personal captain, confirm
readiness, load the same adventure and acknowledge the loaded captain before commit.
New joins and reconnects wait for the next attempt. Disconnecting during preparation
cancels the attempt; a disconnected captain cannot rescue or continue acting.

The portable model is `net/adventure.*`; the runtime coordinator is `adventure.*`.
All native loading, picker and result operations are queued outside Present. Monotonic
run IDs, sender identity, phase checks and event/snapshot revisions reject stale or
replayed messages. Intro/Talk holds and native non-menu pause reasons retain
their existing clock behavior. Local user/menu pauses follow the unanimous
party policy below.

## Unanimous user/menu pause (dc04, preview 9)

Preview 8's coordinator read only the host time manager's pause bit. An isolated
baseline on the same executable reproduced a stopped host clock while a guest
captain moved 0.5154 m under ordinary input. Host snapshots also kept resetting
the guest's displayed clock, so that displayed clock alone does not establish
whether guest native input or physics continued. The baseline ended normally at
connected menus: `out/lab/preview9-pause-baseline/result.json`.

The installed executable establishes the native counter and flag behavior:

* `cGameTimeManager::Pause=B32330` and `Resume=B32360` are thiscall
  `(manager,uint32 reason) -> int`, `ret4`. They increment/decrement the selected
  signed counter, call `B320B0`, then return the preserved reason counter.
* `Toggle=B32390` changes only counters equal to zero or one, then uses the same
  recalculation. Native speed changes also call it at `B32419` and `B324EA`.
* `RecalculatePauseFlags=B320B0` is thiscall `(manager)`, with no stack arguments.
  It selects the **first** positive counter's flags; it does not OR all reasons.
  A game-pause transition updates the native clock and publishes `03867294`;
  audio changes publish `0546BBB8`, and effect changes invoke the native effects
  manager's virtual `+2C/+30`. Its ordinary return is `B321DF`; effect paths
  restore the stack and tail-call those virtual methods.
* `B32500` registers UserToggle `04BF38A8` with flags 7, UIToggle `04BF38A4`
  with flags 3, and ModalDialog `04BF38A9` with flags 3. Esc/menu entry `E18FB0`
  calls Pause with UIToggle. Gameplay `04BF38A7` is a separate preserved reason.
* `GetPauseCount=B321E0` and ordered-map lookup `E5C780` establish map `+4C`,
  root `+58`, node key `+10`, value/index `+14`, left `+4`, right `+0`.
  The vector at manager `+68/+6C` contains eight-byte counter/flags entries.

`adventure_pause_native` hooks only that verified recalculation. During a running
co-op adventure it reports the three local user/menu counters as pause intent.
Until the host reports unanimous intent, those positive counters are excluded
temporarily from native priority selection, then restored. Signed counters,
native Pause/Resume return values and nested counter deltas remain intact;
Cinematic, AdventureDialog, Gameplay and every other native reason keep their
ordinary priority. The original transition performs native clock/audio/effects
cleanup. Outside the active run, the original recalculation runs unchanged.
Every map/vector read and traversal is bounded; an unexpected layout retains
native behavior. The executable verifier checks the hook, caller edges,
counter lookup layout and reason registration bytes before generating symbols.

The host owns both the pause snapshot and its connected participant subset.
Every participant starts with an unknown/unpaused intent. Only all confirmed
positive intents produce the shared hold. One Resume ends it; a client also
requires acknowledgement of its current local intent serial, so a delayed
all-paused snapshot cannot approve a later menu visit. Guests validate snapshot
IDs against the immutable run roster and require host plus self, without
waiting for their local Session roster to observe a matching departure. This
prevents a reordered peer-leave event from stranding an old shared hold.
New joins and retired/recycled participant IDs cannot contribute to the run.
Validated current-run intent/snapshot reception also initializes the bounded
pause state before its first ordinary frame. This closes the keepalive window
where Running can commit and acknowledge a guest's initial intent before native
tick has initialized the run; stale runs and wrong senders cannot trigger that
initialization. The host's aggregate is cached, so native per-creature `held()`
queries do not copy or sort player rosters.

The shared hold gates native play/creature updates, vehicle updates, powerup
timers and authoritative objective/revival time. Existing Intro/Talk holds stay
independent. A single local pause instead fences only that captain's native
brain/input/orders and residual movement, leaving host NPC simulation and the
other captains active. Menu and camera presentation remain native.

Direct puppet driving needs an additional guard because it runs outside native
simulation Update. `AdventurePauseMotion` pins the admitted pose while retaining
nonspatial PlayerState fields and monotonic sender times. The puppet driver
stops applying interpolation during the shared hold; transition cleanup clears
interpolation and locomotion without clearing teleport generations. Old delayed
movement cannot be re-admitted after Resume, and the first resumed state does
not extrapolate across the time spent paused. Native flag recalculation and its
transition cleanup are coalesced outside-frame tasks, invalidated by reset. The
receive/driver guards also fence their own spatial work immediately; no
resource/UI construction is added to Present.

An already queued pickup task rechecks the hold before taking its pending
events, and again between event application and expiry/materialization. Thus an
approved teleport received just before pause retains its generation and native
cleanup until Resume rather than moving a captain during the hold. The bounded
queue still reports overflow outside frame while held; reset/run checks discard
old work. Explosive queued application already had this execution-time hold
check. Win32 pickup/explosive regressions exercise prequeue-then-pause, one-shot
Resume and cancellation of deferred native work.

The portable pause suite and ASan/UBSan cover four-player unanimity, local Resume,
stale/duplicate/malformed packets, run reset, disconnected members, reordered
departure snapshots, retained health, delayed movement and teleport fences.
The Win32 pause fixture exercises the real native wrapper, reason priority,
signed/nested counters, native restoration and cancelled queued work. Updated
control fixtures verify that a local pause blocks its owner while unrelated NPC
brains/orders still run. These pass. Native preview 9 acceptance is recorded
separately; the static/fixture checks do not establish its game-level result.

The two-client native Town act 1 pause regression passed on candidate 5, with
both loaded shadows verified as SHA256
`e5d55c37cfc92efb97208fe34e9c60b9fa49a040173205403a7258edc67565ac`.
The actual MoveTo objective populated act 1 before measurement; saved captains
remained alive at 92/90 HP. Host-only Esc left the guest moving 17.124 m,
host NPCs moving 25.657 m, and the host clock advancing 4.176 seconds, with zero
menu-owner drift. Guest-only Esc left the host moving 20.424 m and its clock
advancing 4.550 seconds, again with zero menu-owner drift; the sampled Town
NPCs naturally idled during this inverse case (0.003 m). With both native menus
open, a settled 2.5-second held-W observation showed identical goals/clocks and
zero captain or NPC displacement on both clients. Closing only the host menu
released both native world clocks while the guest retained its local menu;
puppet steps stayed within owner travel and prior correction error, with at most
0.019 m excess. Both clients returned to connected galaxy menus with zero native
faults. Evidence: `out/lab/preview9-pause-native-candidate5/result.json`.
This establishes the candidate 5 pause core; later queued-combat changes require
their own acceptance. Earlier Mine attempts retained their failed fixture
prerequisites (live-combat knockouts or unwalkable distant placement) and are not
reported as complete pause passes.

## Difficulty

The host edits the lobby's Adventure settings before launch. Defaults are proximity
revival (6 metres, 3 seconds), enemy health `1 + 0.5*(N-1)`, enemy damage 1, and
captain-to-captain friendly fire 0%. Overrides replace the enemy multipliers; they do
not multiply the party default again. N is the frozen launch roster size.

Alternative death modes are any death fails, or proximity revival plus automatic
revival after 30 seconds. Revival restores 50% health at the downed location, with
2 seconds of damage protection. Moving the rescuer away, taking damage, disconnecting
or becoming downed resets progress. One rescuer attends each downed captain; one
rescuer cannot revive multiple captains at once. A full party wipe fails immediately,
including when automatic revival is enabled. Inventory and energy remain native.

Friendly fire choices are 0%, 25%, 50% and 100%, applied by the host to attributed
captain hits. Self damage and allied NPC damage retain their authored amounts.
The native combatant owner conversion is virtual `+0C`, so vehicle/building attackers
are not incorrectly treated as creature subobjects. Native healing damage type 10
uses the authoritative heal path. Authored captain invulnerability is retained;
temporary downed immunity is restored only on the same live captain object.

## Native entry

`ResetRequest=EF1910` is cdecl, plain return at EF1962. Its request at `15ACCC8`
contains adventure key `+0`, auxiliary key `+C`, captain key `+18`, entry kind `+24`
and source kind `+28`. Native normal quick-play uses `(2,1)`; Space uses `(2,2)`.
Space entry and editor preview are outside this feature's entry scope.

`LoadingScreenGet=B3D510` is cdecl. `LoadingScreenShow=E3E350` is thiscall with
`(bool on, uint32 modeID, bool transition)`, `retC` at E3E7ED. The usual arguments
are `(1,01654C10,1)`. **This method displays UI and sends loading messages; it does
not itself prevent a queued scenario transition.** An initial live test blocked it
on a content error but the native state machine still entered Scenario.

The adapter additionally gates `cGameModeManager::SwitchToIndex=7D8670` before the
old mode's OnExit. Registry begin/end are manager `+14/+18`, entry stride `18`, mode
ID at entry `+4`. The switch is thiscall with an index, `ret4`. Public
`SetActiveMode=7D8CF0` accepts a mode ID, returns bool in AL and uses `ret4`.
Replay sets the native request, shows loading and explicitly requests Scenario.
Failed partial loads return to the galaxy menu through this same verified API.

Scenario singleton `16C7AA4` holds data `+74`, play mode `+78`, and mode `+CC`.
Loaded acknowledgement requires mode 2, data loaded `+124`, no missing assets
`+14`, matching resource key at resource `+8`, a live avatar and loading screen
visibility `+370` false. The first allocated avatar can still have provisional
health before the loading screen closes, so its existence alone is insufficient.
PlayMode::Update and creature updates are frozen until every captain has loaded.

## Captain selection and results

ShopperRequest ctor `644AC0` and dtor `644B10` are thiscall, plain returns; Show
`64A900` is cdecl taking a request pointer. The 0x48-byte native request uses shipped
configuration `5D4875D0` (inheriting B6277D21), Adventurer tab `8DF88930`, My Captains
query `54C9C37C`, and model type `4178B8E8`. That model type is not a resource group.
GetAssetModelType `EECAC0` is cdecl taking a ResourceKey pointer.

AssetBrowser getter `401030` returns `[15D0C0C]`. Its unmanaged single-selection
listener is at browser `+1A0`; vtable slot 0 is thiscall `(ResourceKey*)`. Accept
and cancellation both call it; cancellation supplies a zero key. Reset detaches
only owned listeners before destroying them. A native ABI fixture tests this
contract, model/gear transfer, owner-specific remapping and every-peer acknowledgements.
The selected local captain keeps its original key for native progression.

Coordinator results enqueue at most one native result callback per attempt. Downed
connected captains remain eligible; disconnected captains do not. The callback is
guarded by generation and run, so reset/disconnect or a superseding attempt can
cancel queued work. The in-memory claim is not a durable reward transaction, and
the void native adapter does not acknowledge successful progression or saving.
Native SetState 5/6 retains the
normal local results/progression path. Native results function F15770 submits
asset statistics `2D59837B` and `706CCB27` through Pollinator `60E920` (thiscall,
stat ID, key pointer, int64 values pointer, count; `ret10`). During a co-op attempt
the adapter suppresses those two submissions, preserving other native statistics.
The suppression count appears in `coop.adventure` diagnostics. The controlled
success/Done/restart test below verified the native progression path on both
original captains; a full objective-driven adventure playthrough remains separate.

## Validation

`make test` covers portable networking, difficulty, launch barriers, authority,
content validation, health tuning, and the coordinator over real UDP with fake
native adapters. Result regressions cover duplicate/conflicting queued results,
forged and stale packets, downed/disconnected eligibility, and reset cancellation.
The optional Windows ABI targets are `test-captains-windows`,
`test-lifecycle-windows`, `test-actions-windows`, `test-vehicles-windows`,
`test-mirror-windows`, `test-progress-windows`, `test-relationships-windows`,
`test-social-windows`, `test-blocked-update-windows`, and
`test-debug-capabilities-windows`. Their executables run with fake native adapters and do not
launch the game.
`tools/tests/coop_adventure.py` drives isolated native menu/captain fixtures;
`tools/tests/windows_task_lifetime.py` verifies queued callback lifetime across
actual DLL unload. Test artifacts stay under ignored `out/windows-lab` paths.

Native Windows validation on the isolated instances 21 and 22 has passed:

* Sporepedia Adventure Town entry reached the same committed two-player run and
  world `6666E0D1`, with 405 synchronized actors and reciprocal captain puppets.
  The local captain keys remained `29055757` and `29055759`, type `2B978C46`,
  group `40626200`, and native model type `4178B8E8`. Both processes remained
  alive without logged exceptions. The successful pair artifact is
  `out/windows-lab/coop-adventure-pair/result.json`.
* Friendly fire 0 blocked captain damage in both directions. Native friendly fire
  25% passed with host-originated and guest-originated combat across the tested
  source categories. Environmental damage and healing updated authoritative health.
  Proximity revival restored the host to 45/90 HP, damage interrupted revival,
  and the two-second post-revival damage protection held.
* Automatic recovery restored 45/90 HP at the exact downed position after
  30.135 seconds. Artifact: `out/windows-lab/adventure-recovery/result.json`.
  Friendly fire 50% passed from both origins, applying 10 damage for a 20-damage
  captain hit. Artifact: `out/windows-lab/ff50-hit/result.json`.
* Friendly fire 100% applied full damage from both attack origins and preserved
  self damage. With Any Death Fails, one captain's death failed both players,
  applied the native failure result once, and Done returned both to the menu.
  No native faults were logged. Artifact:
  `out/windows-lab/ff100-any-death/result.json`.
* The final world comparison contained 439 host and 439 guest actors with exact
  `(marker,noun,class)` multiset equality, no missing or extra guest actors,
  all 439 guest bindings, and an empty pending set. Each guest vehicle tree,
  weapon, timer and actor suppression counter reached 1130; all four host
  counters remained zero. Artifacts: `out/native-act-reconcile/final-comparison.json`
  and its associated final diagnostic captures. This validates the tested
  act-change cleanup case; broader world behavior remains under test.
* The guest's Move objective advanced act 0 to 1. A native Talk completion
  callback advanced act 1 to 2 after a brief snapshot wait; an act 2 Talk callback
  also passed after waiting for the host to receive the guest's position. This
  validates those routes, not every objective type or a complete adventure playthrough.
* An ordinary guest mouse click on Mr. Mayor opened the native conversation with
  cinematic/text, Next pages and its normal completion control. Finishing the
  conversation advanced both players from act 1 to 2, with zero logged faults.
  This used normal input without a completion injection. Artifact and screenshots:
  `out/windows-lab/native-social-ui/talk-result.json`.
* Ordinary guest conversations completed the Gallery and Playwright partial
  objectives, then the third act 2 objective advanced both players to act 3.
  Carnie's act 3 conversation completed one of that act's two objectives on both
  players. The corresponding `native-social-ui/{gallery-retry,playwright,spoffit-side,carnie}/result.json`
  artifacts report PASS. These are real dialog/Next-control cases, and do not
  establish the remaining Town objectives or a full adventure playthrough.
* Fresh-process `coop.targets objs` queries passed in Menu and Scenario after the
  legacy enumerators gained missing-capability guards. Native relationship
  queries gave the same result for either party captain within each instance,
  and relationship-alias counters increased. This establishes alias routing,
  not autonomous enemy targeting or cross-instance cache equality. Artifact:
  `out/windows-lab/native-social-ui/relations-and-targets.json`.
* The final frozen DLL passed a fresh normal Sporepedia two-player launch
  (`out/windows-lab/coop-adventure-final-retry/result.json`) and guest Move
  contribution (`out/windows-lab/native-final-regression/move/result.json`).
  A controlled host act selection followed by a guest native Talk completion
  callback completed one of three goals; the guest's native HUD displayed the
  green Gallery text and completion check, with one HUD refresh and no pending
  repaint. Artifacts: `native-final-regression/partial-goal/result.json` and
  `22-hud-partial.png`. This isolates the snapshot-to-HUD route rather than
  claiming a new ordinary dialog playthrough on that DLL.
* With dynamic relationship snapshots active, guard marker 140's native predicate
  returned non-hostile for both captains on both processes, and the guest's
  authoritative relationship counter increased. Scenario object diagnostics
  remained available and both processes reported zero faults. Artifact:
  `out/windows-lab/native-final-regression/relationship-hud-targets.json`.
  This checks the previously divergent relationship query without forcing AI
  attacks or establishing every NPC behavior.
* Music's Lotta recruitment passed through an ordinary NPC mouse click and native
  Sing response (key 1), winning the actual social minigame and advancing the host
  and guest from act 1 to 2 through AllyWith. Both processes reported zero faults.
  Artifact: `out/windows-lab/native-social-input/lotta-recruit-sing/result.json`.
  This proves the tested recruitment UI/goal route. A separately found impressed
  flag parity issue required accepting native mode 4's genuine Befriend callback
  before Ally. The final DLL repeated the ordinary recruitment test and native
  read-only inspection confirmed flags `180` and cached relationship 6 on both
  processes, with zero faults. Artifact:
  `out/windows-lab/native-final-social/lotta-parity-retry/result.json`.
* The final DLL passed ordinary neutral Kid Befriend through a real body click
  and native Sing responses. Both processes gained impressed flag `80`; the
  unrelated act 2 Talk objectives remained unchanged and neither process logged
  a fault. No active Befriend objective was present. This establishes the tested
  non-objective social completion route, not every authored Befriend goal.
  Artifact: `out/windows-lab/native-final-social/kid-befriend/result.json`.
* A full party wipe produced the failure ending. Accepting each native ending
  with control `0445C99D` reached the actual Mission Failed results panel on both
  instances, with Replay/Done available. The coordinator was ended, and each
  instance reported one suppressed score submission. The host displayed XP
  0/10 and zero points. No faults were logged in the subsequent check.
  Both players also returned to the menu through the native failure Done button.
* A controlled success reached Mission Complete on both players. After choosing
  an unlocked reward and accepting Done, the original captains each changed from
  XP 0/level 0 to XP 100/level 1. Each score-suppression counter was one, and each
  native result guard reported applied state 5. Both reached the menu. Restarting
  both complete game processes with the same isolated profiles retained XP 100
  and level 1 on each original key. Artifact:
  `out/windows-lab/adventure-results/result.json`, including `restart_persistence`.
  This test used controlled completion and does not establish a full playthrough.
* Cancelling a prepared or loaded attempt returned to the galaxy menu.

At the initial preview's validation point, native Replay navigation, every authored objective type,
simultaneous conversations, a complete adventure playthrough, four-player
sessions and broader adventure compatibility remain validation work. The observed
Done/restart persistence pass does not make the coordinator's in-memory claim a
durable transaction across interruption. These passes do not establish that the
full feature is complete.


## Preview 4 party flow review (dc04)

Source and installed-executable inspection on 2026-10-23 established the following
limits. These findings do not represent an implemented fix or a new live fixture.

* The coordinator commits after every native loaded acknowledgement, without an
  acknowledgement of the beam-down Continue button. `adventure_native::loaded`
  checks assets, avatar, mode and loading-screen visibility, not play state three.
  The native Update jump table at `F1FF60` sends state three to `F1FB3B`.
  Only that branch increments play `C0` and decrements `BC` (`F1FB56..F1FB6C`),
  subject to its native cinematic/browser guards. Host goal snapshots supply
  both authoritative clocks. A guest can enter local play before the host, and a
  host state-three snapshot can set a guest to state three through `apply_goals`.
  There is no all-player Continue barrier. The separate co-op recovery clock
  tests the host time-manager pause flag, not the native play-state field.
* Results message `070B18C7` with payload `+8=2` calls `EF1320` from
  `F1C99B..F1C9A6`. This native replay method performs in-place Scenario teardown,
  data reset and play reset (`EF0FE0`, `F45E70`, `F18F80`, `EF0E00`); it does
  not pass through the Sporepedia loading hooks. The ended coordinator therefore
  does not create a replacement run or restore preparation readiness.
* Talk presentation is local to the initiating client. `count_goal` forwards a
  guest's completed Talk objective to the host, which counts it once and can
  advance the shared act. No conversation-start broadcast or party exit barrier
  exists. Mirrored objective counts do not imply mirrored dialog presentation.
* `net::PlayerState` carries no spatial scale. Scenario remote captains use
  `creature_spawn` and are driven without copying the owner's actual scale.
  This is a missing synchronization input consistent with the reported smaller
  remote models; the precise native captain-to-NPC scale ratio is unmeasured.
* Every party captain has the host-validated pickup/drop/give route. Pickup
  rejects an item held by another captain. Actor snapshots carry transform,
  health/death and social/relationship state, but no dedicated gate lock/open
  state. Act changes and actor removal are synchronized separately. These paths
  alone do not establish correct key consumption, gate animation or collision
  synchronization for every native gate variant.

## Preview 3: native captain preparation and cancelled quickplay recovery (dc04)

Verified against SHA256 dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37.
The evidence manifest in `tools/addrport/verify_dc04.py` checks each new native
entry point, its return sites, and the globals/vtable references before map generation.

- Native front-end singleton is `016A1344`. Its component registry at +124 is an
  RB tree (root +0C, sentinel +4; node right +0, left +4, integer ID +10,
  component pointer +14). Component13 is the native 178-byte quickplay controller,
  primary vtable147EDA0, secondary unmanaged browser listener at controller+8.
  Use bounded lookup, never the inserting native map accessor DE75E0.
- Controller +20 is stage, +24 resume stage, +28 frame countdown, +2C native
  step index. +8C adventure key, +98 pending adventure key, +A4 captain key,
  +7C personal-captain flag, +7F needs naming, +80 launch-animation flag.
  Native stage2 selects/creates, stage3 names/reviews, stage8 awaits metadata.
- DFE1F0 starts the native choose/create/name/review flow (thiscall, no arguments,
  C3 at DFE421). Offline first use may select Welcome automatically; finish that
  lookup before replacing the adventure with the party descriptor.
- E00070 selects adventure metadata (thiscall key*, ret4 E00166). With stage1,
  asynchronous completion parses native captain restrictions before resuming.
  DF9EC0 advances the native step vector (no arguments, C3 DF9F7E/DF9F84).
  DFAAF0 controls wizard visibility (thiscall bool, ret4 DFAC66).
- **The first loading-screen call is too early to publish a captain.** DFAE20
  starts a launch animation; DFC210 emits a loading signal before its countdown
  reaches DFBD40. DFBD40 converts a raw creature to model type4178B8E8, writes
  the entered name, saves it, fills the launch request and changes application
  state. It is thiscall/no arguments, C3 DFBFC7. Intercept after that save, with
  stage0 and the matching active attempt, while still gating Scenario entry.
- Reset DF9AA0 (no args, C3 DF9AF3) hides native step panels. Front-end DE8680
  requests component13 or component2 (thiscall int,bool, ret8 at DE86DE/DE86F3/
  DE8701). Browser64A370 takes three bools (show, auxiliary, notify-cancel),
  ret0C64A8FB. Close only a browser whose listener belongs to our controller;
  detach it first, then call with false,false,false. Hiding the wizard alone
  leaves the shopper visible, as reproduced in the hidden Windows lab.
- Quickplay application state21851EBE is separate from GalaxyGameEntry game mode
  02CCD1D2. Cancellation must switch application state to02CCDA9C even if game
  mode already reports the galaxy menu. Previously both live player sessions
  had quickplay application state while in GalaxyGameEntry, hiding GA controls.
- Cancel/reset invalidates ordinary attempt callbacks. Mandatory native cleanup
  survives that invalidation; new host requests wait until it finishes, and an
  incoming client preparation is deferred behind it. A queued launch that never
  called the adapter uses preparation cleanup; even a failed adapter call uses
  loaded cleanup because it may have partially changed native state.

No network kinds, resource transfer format, friendly-fire rules or read-only actor
memory-validation performance optimizations are changed by this fix.

Saving-animation cancellation additionally needs the native front-end Back path:
DE0840 has no stack arguments and never reads ECX (C3 DE08AA). It resets galaxy
view globals and sends07BE7DF5. DE52E0 takes frontend,bool (ret4 DE536A/DE536F,
or tailcalls effect+8). Native Back calls both at DFE699 and DFE6B5. Without the
view reset, cancellation immediately after Finish can leave application state
and game mode correct but hide the menu controls. `coop.adventure recover` uses
the same cleanup only while inactive in the galaxy, allowing a preview 2 menu to
be repaired after upgrading without discarding a live game process.

Browser wrapper53C6DC80 can remain as a cached empty window after normal native
close. Assert browser+1C is false rather than asserting the wrapper was destroyed.

The decisive launch-animation menu visibility flag is the native front-end
UILayout at singleton+144. DFAE20 hides it via UILayout::SetVisible(false) at
DFAE51, before entering Adventure application state. Restore that layout with
SetVisible(true) after restoring galaxy application state/mode. Switching an
already-current Menu application state is intentionally a no-op and cannot undo
this independent layout visibility flag. The GA button's own visible flag stays
set, but its FFFFFFFF layout root is hidden (12 rather than13), which reproduces
missing controls despite correct application state02CCDA9C and game mode02CCD1D2.
Both wizard preview/effect handles +84/+88 must be stopped/released using the
native DFBD40 finalization convention, without deleting saved creation records.

## Preview 4: shared preparation

The host maintains preparation member serials and a readiness epoch independently
of cosmetic progress. Every participant starts unready. A replacement is granted
only during preparation and only for its current serial, then clears all readiness
and the owner's captain barrier before opening native UI. Ready carries both epoch
and serial; the host additionally checks the completed, acknowledged captain
transfer before accepting a captain key. Client progress never validates a model.

Native Back from captain selection goes to stage1 (Choose an Adventure), rather
than stage0. During party preparation either stage cancels the picker because the
adventure is fixed by the party. Replacement cancellation republishes the previous
saved captain under the new serial and leaves everyone unready. Initial cancellation
uses preview3's full application-state/game-mode recovery. Delayed install work,
model offers and callbacks are checked against the new serial before applying.

Preparation UI reads are outside-frame, cached by actual local resource key. The
host snapshot carries original keys; remote presentation resolves the already
owner-scoped key, including property/PNG companions. Authored locks use the installed
story creature directly and never require it to have personal-captain model type.

## Native Intro cinematic ownership (dc04, preview 8)

The caption check mark is control `0445C99D`. Its native handler at `AD94A0` is
thiscall `(listener, window, message)`, returns bool in AL, and uses `ret8` at
`AD9505` and `AD9538`. A click with `message+8 = 287259F6` sends `0445F729`.
`HandleCinematicMessage = AE0DB0` is thiscall `(manager, messageID, payload)`,
returns bool in AL, and uses `ret8` at `AE0E37`, `AE1D29`, `AE1EC0` and `AE1ED4`.
Its Continue branch at `AE1CE6..AE1D1D` requires the native waiting flag at
`manager+3A`, clears flags `38/3A/3B/3C`, changes caption modal state through
`AD7880`, and resumes the native caption layout. This can retire the caption
before the eventual cinematic-end call. The other inspected branches process
caption OK `044EE075` through `AE1EC3 -> ADFF50`, and skip `044EE076` through
`AE1EAF -> AE0000`. These three branches never read their payload pointer.

The existing upstream symbol `SporeCoop::cCinematicManager_EndCinematic = ADFF50`
is thiscall `(manager)`, with no stack arguments and a plain return at `ADFFF0`.
Other verified callers are `AE001E` (skip tail-jump), `DE47F8` (tail-jump),
`DEBC95` and `DEBE02`. They use the current native manager, including fresh calls
to `Simulator::cCinematicManager::Get = B3D5D0`, cdecl(), plain return at
`B3D5D5`, which returns singleton `167EB50`. No unverified AddRef or UI-object
lifetime is assumed.

EndCinematic tests active state at `manager+2C`, then calls `ADF7C0` at `ADFF60`.
Cleanup stops the cinematic, hides its caption/letterbox UI, restores camera and
audio state, and clears active state. EndCinematic then removes cinematic
effects, decrements `GameInput+110`, clears the running ID at `manager+148`, and
sends `044F1189` with the ended ID in `payload+8` at `ADFFDA/ADFFE1`. The stack
message is destroyed immediately after synchronous MessageSend returns. Gating
the play handler `F1C570` therefore happens too late. Native candidate 1 reproduced
the disappearing UI after local confirmation despite frozen adventure clocks.
Candidate 2 held EndCinematic and retained its identity token, but the native
caption still disappeared: gating that end entry alone was also too late.

Candidate 3 gates the three reviewed cinematic messages before native caption
mutation, only for a current co-op Intro with play state 1 and the current manager.
Continue also requires the native waiting flag. It records a non-owning token
(run, manager address, running cinematic ID, message ID), confirms locally and
consumes the message. EndCinematic remains a fallback for direct end paths.
Group release runs outside Present, reacquires the singleton, verifies its
address, active state and ID, then replays the original handler with the saved
message ID and a null payload. The verified branches need no retained payload.
A released-run bypass remains until reset/new entry because native caption
advancement can call EndCinematic on a later frame, after the release callback
returned. That native end and its play message must not enter the barrier again.

Cancelled or replaced attempts clear the token and advance its generation; no
stored UI pointer is dereferenced later. A changed cinematic cancels for a clean
retry rather than ending an unrelated scene. The old play-message hook remains
a fallback for an unreviewed publisher, with no claim that it can recreate UI
that has already been destroyed.

`adventure_flow_tests` exercises capture without cleanup, duplicate clicks,
single release, synchronous and delayed-end bypasses, legacy-message fallback,
reset/cancellation, stale runs/generations and manager/scene replacement. The
Win32 adapter compiles with warnings as errors, and installed-dc04 verification
checks the three named entries and their call edges. Native two-player black-bar
and camera retention is separate acceptance evidence, pending the final test.

### Native Talk final-page ownership (dc04, preview 8)

The same early caption boundary applies to Talk. Native `TalkBegin` at
`F1A000` starts cinematic `0148C248` through `AE08B0` and stores the returned
cinematic ID in `play+D8`. `HandlePlayMessage` at `F1C764–F1C779` receives
`044F1189`, compares its payload's `+8` ID with `play+D8`, then calls
`TalkFinish` at `F1C1D0`. The cinematic has already cleaned its blackbar and
camera before publishing that end message. Holding only `TalkFinish` therefore
holds the objective, but cannot retain the native confirmation UI.

Both the Finished check (`0445C99D`) and Next button (`062EF353`) publish
`0445F729` through `AD94A0`. The native caption constructor binds the check to
`caption+20` at `AD839C–AD83C0`, and Next to `caption+28` at
`AD8401–AD8425`. The cinematic update's `this` is `manager+4`: its branches at
`AE029A` and `AE02E7` use `manager+3B` to show Finished and `manager+3C` to
show Next, respectively. The shared Talk gate consequently requires
`manager+3A && manager+3B` before holding `0445F729`; intermediate Next pages
pass through unchanged. Direct caption OK (`044EE075`) ends the cinematic;
Skip (`044EE076`) is held only when `manager+154 == -1`, the verified direct
end branch of `AE0000`. Neither captured branch reads its payload.

On party release, replay the captured manager handler with the original message
ID and a null payload. Caption advancement may publish the matching end message
on a later frame. The adapter retains a run/scene-serial/cinematic-ID token and
keeps simulation clocks and queued Talk presentation held until that message
arrives. It clears the pending token before native Talk cleanup, suppresses its
native objective callback, then commits the host objective through the existing
single authoritative path. Duplicate end IDs are consumed. Cancellation and a
new native entry invalidate the token by generation; a ten-second native cleanup
timeout cancels the attempt for retry. Intro uses the same settling fence until
its native message has entered playing state. Cinematic-manager updates and
messages continue while the scenario play update is held.

Guest Release receipt also sets a flow-owned pending flag before queuing native
release work. This closes the receive-pump gap in which a host result can arrive
before the adapter has established its native settling token. The coordinator's
optional settling callback reads only those run-scoped flags, without calling
`held()` or `running()` recursively. It retains the first authoritative result
in one pending slot and keeps the run active until local cleanup completes;
duplicates and conflicting results cannot replace it. Reset clears the slot.

Guest goal batches received during a held scene are likewise coalesced to one
newest validated `(run, revision, act)` batch. Native `SetCurrentAct`, goal writes
and terminal-state changes wait until local cleanup releases the hold. The next
outside-frame application revalidates the run and revision. This preserves the
old dialog's goal/target fields when the host advances an act first. Reset drops
the pending batch, and delayed prior-run work cannot modify a replacement scene.
This fence uses `native_scene_held()`, which excludes an unpresented future Talk
once old native cleanup has finished. Its prerequisite act batch can then apply
before native presentation, while the ordinary `held()` clock pause remains in
effect. A future Talk snapshot arriving before the old end cannot deadlock its
own required act transition.

The candidate 6 native one-act Puzzle fixture exposed an additional terminal
ordering issue: both clients reached Mission Complete once, but the host's final
Talk goal was complete/count 1 while the guest retained incomplete/count 0.
The periodic goal sender stopped after the coordinator entered its ended phase.
The host terminal `SetState` hook now sends one reliable existing-format Goals
batch before invoking the coordinator result callback, after native CountGoal
has updated that final objective. The following `K_RESULT` therefore cannot
overtake the completed fields in the peer's reliable queue.

Validated guest Goals are coalesced into one run/revision-scoped slot and one
generation-scoped outside-frame callback. The coordinator's result fence also
reads `goal_application_pending()`, covering queued, deferred and actively
applying batches. It remains in the running phase until those final fields have
applied. Goals can update the act, counts, completion flags and HUD, but cannot
enter native terminal states 5/6 or award progression; the authoritative
coordinator result alone commits that transition afterward. Reset invalidates
queued callbacks without clearing newer-run ownership. Portable coordinator and
Win32 world fixtures exercise same-pump Goals/result ordering, duplicate
revisions, coalescing, and cancellation. The native terminal test now requires
matching final goal counts as well as one-shot results on both clients.

The portable gate/flow fixtures cover asynchronous replay, queued Talk,
duplicate end IDs, replacement tokens and cancellation. Final native UI
acceptance remains a separate check in the preview 8 validation report.

## Preview 5: shared scenes and replay (dc04)

This implements the earlier preview 4 party-flow findings. The new scene layer
uses `MSG_ADVENTURE` subkinds160 Snapshot,161 Confirm,162 Release and163 RequestTalk.
Every scene is scoped by run and monotonically increasing scene serial. The host
counts current connected run participants, validates requests, ignores duplicate
or replaced confirmations and releases exactly once. Cleanup invalidates queued
native work by generation. Late joins and reconnected IDs remain excluded from
the current run. Dialog/UI operations run outside Present.

- `HandlePlayMessage` F1C570 is thiscall(play,messageID,payload), bool, ret8.
  Message044F1189 while play+90=1 directly enters playing state without calling
  SetState. Preview 5 intercepted the message, recorded local confirmation and
  replayed the original handler on party release. That holds gameplay clocks,
  but does not preserve the native intro UI: the publisher already cleaned it
  up before sending the message. Preview 8 gates the earlier native cinematic
  end entry instead, as documented above. Writing state3 directly also skips
  normal native completion. Early confirmation during the loaded barrier is
  retained for the matching run. Native intro animation still ticks, while
  state3 Update, actor AI, vehicle timers/weapons, damage, goals and co-op clocks
  are held until everyone confirms.
- TalkBegin F1A000 is thiscall(play,goal,target), ret8. TalkFinish F1C1D0 is
  thiscall(play), ret. Native play+C8/CC/D4/D8 hold dialog data/page/target state.
  The host checks active act, unfinished Talk goal, target class/incarnation,
  alive initiating captain and range25. Native presentation starts locally on
  all participants. Native Finish cleans up each camera/dialog independently,
  with CountGoal suppressed. Only party release commits the host's Talk goal.
  Old GoalEvent133 Talk completion is rejected. Additional valid requests queue
  and are revalidated against current act/goals before presentation.
- Results message070B18C7 payload+8 is1 Done,2 Replay. Native results save the
  local captain before sending it. Intercept Replay before EF1320's in-place
  single-player teardown. Coordinator17 ResultsAccepted and18 ReplayPrepare wait
  for every remaining participant to accept saved results. Any participant can
  request replay. Cleanup restores galaxy application state and game mode before
  a fresh preparation/run. Saved original captains are republished, all readiness
  starts false, and old callbacks cannot launch a later attempt. A Scenario exit
  belonging to cleanup must not cancel the new preparation.
- PlayerState now carries actual spatial scale, run and captain selection serial.
  Scenario replicas require the current validated mapped model, matching owner
  original species, run and serial. Do not fall back to the previous creation
  cache or the local avatar's profile while transfer is incomplete. Rebuild the
  puppet if its mapped key or run changes, and set the exact owner scale.

The final hidden, muted four-client fixture passed intro confirmation with both
native clocks unchanged while waiting, independent native conversation pages and
group objective release. Two-client tests passed Replay twice, including guest
Replay with host Done, and new guest captain replacement followed by selecting
the previous saved captain in the next run. The actual puppet species matched
the current mapped resource and scale. Portable flow tests cover queued scenes,
wrong/stale confirmations, disconnects and invalidated native callbacks.
Native cancellation and disconnects during intro and partly confirmed Talk also
passed, with native menus restored and immediate retry in the same processes.
Evidence: `out/windows-lab/preview5-release-{intro,talk,replay,models,flow-recovery}`
and `out/preview5-tests-final.log`. These are partial native gameplay checks, not
a complete adventure playthrough.

## Preview 6: collector upgrades and independent teleporters (dc04)

These entries were verified against the installed executable hash recorded above.
`tools/addrport/verify_dc04.py` checks their prologues, returns and call sites.
The adapter is `adventure_pickups`; it deliberately splits recipient effects from
the native helpers' party/follower behavior. Native gameplay validation is
reported separately from the ABI tests below.

| Native entry | ABI and verified behavior |
| --- | --- |
| SporeCoop::Adventure::GetFloatDefault 4E1CE0 | cdecl(PropertyList*,propertyID,float default), float, ret at4E1D1A. Native scalar boost-property reader. |
| ActivateMedKit F225E0 | cdecl(powerup,captain), ret F227F0. Restores maximum native HP, then also heals the local posse. |
| ActivateSpeedBoost F22ED0 | cdecl(powerup,captain), tailcalls UpdateGoals at F230E8. Modifier7 and authored duration, including posse iteration. |
| ActivateDamageBoost F22C20 | cdecl(powerup,captain), tailcalls UpdateGoals at F22EC2. B10 enabled/B14 amount, including posse iteration. |
| ActivateArmorBoost F22970 | cdecl(powerup,captain), tailcalls UpdateGoals at F22C12. B18 enabled/B1C divisor, including posse iteration. |
| SetSpeedModifier C0D220 | thiscall(captain,int,bool), ret8. Modifier7 changes FA4 bit128 and recalculates native movement speed. |
| PickupEffect F22500 | cdecl(captain,effectID), ret F225D2. Creates/refreshes this creature's scaled native effect. |
| OnCollect F1BCF0 | thiscall(play,object,captain), ret8 F1BEF1. Native collect/use objective bookkeeping. |
| ActivateTeleporter F23280 | cdecl(powerup,captain), ret F234A9. Owner teleport, E60 two-second cooldown, then a separate posse teleport loop. |
| TeleportDestination F211E0 | cdecl(powerup,Vec3*,Quat*), bool AL, ret F21279/F212BA. Native authored exit validation. |
| TeleportCaptain F231D0 | cdecl(powerup,captain,Vec3*,Quat*,bool playerEffect), ret F23271. One captain's clamp, teleport, locomotion cleanup and authored effect. |
| ApplyCreatureAbility C20390 | thiscall(captain,targetCombatant,abilityIndex,Vec3* hit), ret0C C21180/C2118E. Native damage calculation; forwarded by C02B59/C24024. |

`GetFloatDefault` is deliberately a GA-specific symbol. Upstream's
`App::Property::GetFloat=40CF10` is a separate bool-returning helper with a
`float* out` argument, used by Space trade. Both accessors must stay available
under distinct names; the pickup ABI fixture supplies both and verifies that
boost properties use only the float/default reader at4E1CE0.

`ReactToPowerup` F23BF0 rejects a dead creature at B5E. For medkit1, speed3,
damage4 and armor5 it also requires B58 avatar flag512. The co-op hook accepts
an owned, living party captain instead, validates the current host object
identity/type and native spatial range, then claims that incarnation once.
Only the collecting captain gains an effect. Do not replay the complete native
activation on each client: each helper iterates the local posse too.

`powerupDuration` is AAC3B137 and `powerupAmount` is6219412B, verified from their
native initialization strings. The installed stock speed/damage/armor palettes
have duration15 seconds; damage and armor amount2. Speed uses the fixed native
modifier7, not the amount property. A medkit restores native maximum HP rather
than treating amount as a partial heal. The class-act explosion amount/radius
are different tuning inputs and must not replace these palette boost values.

Native ApplyBoost F20290 refreshes the same creature/callback entry by resetting
elapsed time to zero and replacing duration. The pool expires when elapsed is
greater than duration. Co-op mirrors that refresh behavior using the host's
shared simulation clock; group holds pause it. Only the host publishes expiry.
An expiry stores the grant revision even if it arrives before the grant, so an
older grant cannot re-enable a spent effect. Boosts persist across act changes;
delayed older-act grants still retire their original pickup and apply the effect.

The important damage boundary is before TakeDamage: native C2064F multiplies by
attacker B14 when B10 is enabled, then C206AE divides by victim B1C when B18 is
enabled. Keep the outgoing damage fields native and do not multiply forwarded
hits again. For party captains, keep B18 false and apply the saved armor divisor
once at the central host captain-health authority. This avoids double reduction
and covers environmental explosions which bypass the creature ability calculator.
The native player effect still renders through PickupEffect. Player effect
property72AEEDA0 and NPC effect property6C3BB950 select different artwork.

OnCollect's ordinary collect-goal path tests B58 flag 512 directly. Scope that
flag around a host callback for a remote captain, restoring the previous flags
afterwards. The host then destroys the consumed object and calls UpdateGoals,
matching the native completion order. Guests retire the host marker/incarnation
without firing independent objective callbacks. Authored respawns are new
incarnations, even if they reuse the same placement or allocation address.

Teleporter destinations are not class links. The powerup's marker provides
position+1E8, orientation+1F4 and scale+204. With default secondary graphics,
F20CB0 also applies the exit ornament's model-bone offset; a secondary graphics
override uses the raw authored position. F211E0 rejects a destination at most
`3*(entry spatial scale+1)` from the entry. The owner helper F231D0 applies the
native planet clamp through F20930, uses spatial Teleport, clears ground support
at captain+137 and zeros locomotion velocity through C41EC0 before StopMovement.
Captain+137 is spatial+77 `mbSupported`; normal physics can set it true again
after checking the new terrain. It is not a persistent flight flag. The helper
has no posse loop. Co-op calls this helper and
retains E60's two shared-simulation seconds per captain, with no group check.

Teleport acceptance must publish the movement-generation fence before requiring
an observer's puppet/source binding. Otherwise new-generation UDP state cannot
create the puppet which native cleanup is waiting for. Accepted and completed
cleanup revisions are separate. Delayed cleanup waits for native objects, runs
once, and cannot rewind a newer accepted teleport. Timed upgrades likewise retain
the last applied pointer/incarnation/revision/state and their artwork. A late or
recreated puppet receives the current active fields/effect once, rather than
replaying the effect or rewriting fields every frame. Expiry deferred by a
missing captain clears it once when that binding returns.

Wire subkinds 152 Claim, 153 Grant, 154 Expire and 155 Teleport are scoped by run,
revision, act, recipient and object marker/incarnation. Host requests are bounded
at 128 before acceptance. Per-peer accepted event queues are paced four per drain
while transport pending is below 64; claims stop before mutation at 496 queued
events, reserving space for expiry. Guest accepted events have a 1024-entry bound.
Overflow cancels the attempt once through an outside-frame callback, including
during a group hold, instead of silently acknowledging and discarding a valid
grant or teleport. Distinct consumed identities are retained rather than merged
away. The 1024-entry contact throttle prunes expired 0.3-second entries so many
authored respawns cannot permanently disable later pickups.

`tests/adventure_pickups_tests.cpp` runs the actual Win32 adapter against fake
verified-signature native functions and real host/guest Sessions. It passes
collector-only health/speed/damage/armor, claim races, refresh, host-only expiry,
stale/ordered act and revision handling, native field restoration, teleport
cooldown and missing-observer fences, late model bindings, reliable queue bounds,
overflow cancellation/reset and more than 1024 distinct contact identities. It
reported `adventure pickups: 0 failures`. This is ABI/unit evidence; the native
integration harness and its result JSON establish separate gameplay evidence.

The two-player native suite passed all five cases against preview 6 build 5b,
DLL SHA256 `6d97642842f1992e06cf406d1cac18294fe4e1b9d0756f73b1017fd773dbee8f`.
Its evidence is `out/windows-lab/preview6-pickup5-final2/result.json` with recorded
isolated profiles 31/32. Both captains used native Bite index 5, type 30: base
damage 1.5, boosted damage 3 and expired damage 1.5. Armor reduced an environmental
12-point hit to 6 once, then restored 12 after expiry. Native Talk held shared
clocks and the active boost for three wall-clock seconds. Teleport tests used
actual contact, an authored native exit, invalid-exit rejection, simultaneous
captain activations, native velocity cleanup and a captured distant pre-teleport
UDP position resent with a current timestamp and old movement generation.
Both games ended with zero native faults and returned to native galaxy menus.
This is two-player fixture evidence, not a complete adventure playthrough.

Final candidate 8, DLL SHA256
`075ee6000269235d8a3c41572e01296b60a71e953dc70806a4d3c1ad3cc06f75`,
then passed the native health/speed/teleporter routing smoke in
`out/windows-lab/preview6-pickups-routing8/result.json`. It covers subkinds
152..155 after the Replay and held powerup setup changes. During its native
Talk hold, elapsed time 4052 and boost remaining 14.6732 stayed unchanged for
three wall-clock seconds, followed by normal refresh and expiry. The same hidden
pair passed two complete open-captain Town Replay rounds in
`out/windows-lab/preview6-replay8/result.json`, retaining saved captains, explicit
readiness, shared Continue/clocks, model scales and complete rosters without
native faults. These final routing checks retain the separate build-5b full
pickup/damage/armor acceptance rather than claiming those two cases were rerun.

Candidate 8 repeated native health, speed and teleporter routing after coordinator
cleanup routing and held PowerupUpdate initialization changed. Both loaded shadow
DLLs had SHA256 `075ee6000269235d8a3c41572e01296b60a71e953dc70806a4d3c1ad3cc06f75`.
`out/windows-lab/preview6-pickups-routing8/result.json` passed all three cases,
covering Claim, Grant, Expire and Teleport. Native Talk held elapsed time at 4052
and boost remaining time at 14.6732 seconds across three wall-clock seconds;
native Next/Finished then released the group, and the boost expired normally.
`out/windows-lab/preview6-replay8/result.json` also passed two native open-Town
Replay rounds with saved captain reuse, fresh explicit readiness, shared Continue
barriers, paused clocks, reciprocal models/scales and complete actor rosters.
Both clients had zero native faults and returned to connected native menus.

### Preparation dialog and native loading completion (preview 8)

On the installed GOG executable, the copied Eighth Day adventure remained in
native loading state 2 with the co-op preparation dialog visible. Clicking its
native close control on both test clients immediately allowed loading to finish.
The fix hides the owned dialog synchronously, outside the render frame, before
entering the native adventure loading screen. It uses the existing visibility
API and adds no native entry point or loading-state bypass. F9 remains available
after launch. Cold transferred metadata and images retry bounded negative cache
lookups once per second; successful metadata stays cached for the current run.

### Revival HUD device ownership and layout evidence (preview 8)

The cached D3D9 state block belongs to the device that created it. SporeKit's
CreateDevice hook can replace its current device without emitting a Reset pair,
so retaining only the block is insufficient: capturing/restoring the old device
would not restore state changed on the new one. The HUD now records the owning
device, releases/rebuilds the block outside Present when that identity changes,
and skips drawing until the current device matches. Lost-device handling releases
the block before Reset; unload also releases it through owned queued cleanup.
Each draw captures and restores the current state rather than reusing stale
captured values. Native Reset and CreateDevice replacement with an active revive
ring remain unexercised; static lifecycle review is distinct from native evidence.

Portable confirmation-layout tests check rectangle bounds and non-overlap for
one to four participants at widths 800, 1024 and 1920, plus minimum-height and
name/countdown cases. They do not measure native font glyphs, prove long-name
readability, exercise control-reference retirement or establish a native
four-player party. Those claims require their recorded UI/gameplay checks.
