# Galactic Adventures co-op entry and rules

The implementation targets the installed March2017 Galactic Adventures executable,
SHA256 `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`.
See [build-dc04.md](build-dc04.md) for compatibility and isolated native Windows
testing, and [adventure-world.md](adventure-world.md) for content and world interfaces.
Addresses below are preferred PE addresses. Runtime code resolves named symbols.

## Party protocol

Protocol version 7 reserves message 52 for adventures. Coordinator messages use
subkinds 0..63, content 64..71, world 128..191, and captain models 192..223.
The host freezes the roster and difficulty when Play is selected. Every roster member
must verify the content manifest, select the required/personal captain, confirm
readiness, load the same adventure and acknowledge the loaded captain before commit.
New joins and reconnects wait for the next attempt. Disconnecting during preparation
cancels the attempt; a disconnected captain cannot rescue or continue acting.

The portable model is `net/adventure.*`; the runtime coordinator is `adventure.*`.
All native loading, picker and result operations are queued outside Present. Monotonic
run IDs, sender identity, phase checks and event/snapshot revisions reject stale or
replayed messages. The host's simulation clock pauses with the native game.

## Difficulty

The host edits the lobby's Adventure settings before launch. Defaults are proximity
revival (2 metres, 3 seconds), enemy health `1 + 0.5*(N-1)`, enemy damage 1, and
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

Native Replay navigation, every authored objective type,
simultaneous conversations, a complete adventure playthrough, four-player
sessions and broader adventure compatibility remain validation work. The observed
Done/restart persistence pass does not make the coordinator's in-memory claim a
durable transaction across interruption. These passes do not establish that the
full feature is complete.


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
