# Creature stage (RE notes)

Marks: ✔ verified (code read or seen at runtime), ~ likely, ? open.
Structures of `cCreatureAnimal` and the NPC sync: [05-re-notes.md](../05-re-notes.md) 5.4-5.6.

## 1. Playing through input (test bots, 2026-10-23)

* ✔ A left click on a creature walks the avatar to it and selects it (its health bar and species card
  appear); a click on the ground walks there. A click on the partner's puppet does not move the host
  (the selection of puppets is guarded, `CreatureSelectionUI`).
* ✔ The ability bar (bottom centre) has two stance buttons above it: social (green) at
  `(W/2 - 30, H - 82)` and combat (red) at `(W/2 + 43, H - 82)` (1440x1080: 690,998 / 763,998).
  The keys 1..4 use the abilities of the current stance. The fixture's creatures have one ability
  per stance (combat: bite / strike, social: sing). The stance a save starts in is social.
* ✔ Combat: the selected target takes ~1 damage per key press at melee range (Kook: 6-7.5 hp, the
  host's creature 10 hp, the guest's Kirgle 16 hp); a Kook nest fights back as a group.
* ✔ Social: key 1 on a Kook (social stance, next to it) impresses it ("Impress 3 Kook to ally" card,
  1/3) and pays 10 DNA on the host.
* ✔ Kill DNA: Kook 7.0-8.8 DNA, paid inside `TakeDamage` (below).

## 2. Kill credit in `cCreatureAnimal::TakeDamage` (0xC075A0)

* ✔ The DNA of a kill is paid inside TakeDamage: `AddEvolutionPoints` is called at 0xC07BAC
  (return address 0xC07BB1, seen at runtime) with the value of `0xC04260(victim, 0)` (thiscall,
  returns the victim's DNA worth as a float). Right after it 0xC75820 (via 0xF67D40) gets the same
  value (~ the "+N DNA" popup / statistics).
* ✔ It is paid only when the credit flag (`[esp+0x80]`) is set at 0xC07A78: the attacker creature
  (`ebp`, the attacker combatant's owner when its noun is 0x18EB45E, cCreatureAnimal) answers true
  to its spatial vtable +0x58 (~ "is the player's avatar"), or its species' relation to the player
  (0xB1FCD0 / 0xBA40C0) is 6 (~ ally) and a meter at victim+0xA38 passes a threshold.
* ✔ Consequence for co-op: a guest's hit is applied on the host with the guest's puppet as the
  attacker, the puppet is not the avatar, so guest kills paid nothing (no DNA for anybody, no goal
  progress). Fixed in `npc.cpp`: a lethal guest hit is applied as the host avatar's
  (`guest_kills=` in `coop.npc` on the host); regression check in `coop_team.py`.

## 3. The push vector of `TakeDamage`

* ✔ The 4th argument (`const Vector3*`) is the hit's push (knockback impulse), not a position: a
  bite or strike of the player (type 2) passes (0,0,0) (runtime log of every real call,
  2026-10-23). Our synthetic hits passed the victim's position (|v| ~ 505 on Scalinus): a forwarded
  guest hit pushed the NPC on the host, a 1000-damage kill threw the corpse ~80 m up, and the guest's
  local death replay (1e6 damage) threw its proxy the same way; the proxy corpse then hung 15-80 m
  away from the host's corpse until it was removed (`teleport` / `set_position` do not hold a dead
  creature). `game::apply_damage` / `game::hit` pass (0,0,0) now; the death replay uses a just-lethal
  hit. Regression: the corpse check in `coop_team.py`.

## 4. The avatar-event handler and +0xE84

* ✔ `Simulator::CreatureStageEventHandler` 0xD47FC0: a vtable method (pointer at .rdata 0x147B680),
  `thiscall(self, u32 msgId, msg*) ret 8 -> bool`; it handles the messages 0x61B1320, 0x49B56D7,
  0x5371F11, 0x60B4123, 0xD335362C, 0xD3353635, 0xD335363A when `msg+0` is the avatar (checked with
  0xB1FCD0). On the 0x60B4123 / 0xD335362C path it walks creatures and, for an adult (`+0xB34 == 1`,
  0xC0B890) creature of another species with `+0xB54` set, reads `(+0xE84)->+0x32C` (0xC0C2C0 is
  the getter of +0xE84) without a null check: our puppets and NPC proxies have no +0xE84 object, the
  guest faulted at 0xD48634 (data 0x32C) while it fought proxies. Same field as the
  `CreatureSelectionUI` crash (coop.cpp guards both: during this handler the puppets and proxies
  get a blank object whose +0x32C is -1, "none"). +0xE84 is the creature's archetype tuning, set by
  `Create` from the herd's archetype (docs/re/tribe.md E: +0x388 = isEpic); our herds have none.
  ? the meaning of tuning +0x32C.
* ✔ 0xC0B890: `+0xB34 == 1` (age: 1 = adult ~). 0xC04260 `thiscall(victim, 0)` -> float: the DNA a
  kill of this creature is worth.

## 5. What a player meets that tests did not do (play_creature.py)

* ✔ The pause menu (Esc) is "Options": Resume Game, Spore Guide, Spy Glass, Ban, Settings, Save,
  Exit to Main Menu, Quit Spore. `coop.ui find "<caption>"` lists a window with its parents
  (rectangles relative to the parent, flag 0x1 = visible); `tools/tests/uifind.py` turns that into a
  screen point. "Exit to Main Menu" asks "Do you want to save your game?" (Don't Save / Cancel /
  Save), then "Are you sure? All progress will be lost!" (Don't Save again). The menu stays
  "visible" under the dialogs it opened: check for the dialog, not for the menu to go away.
* ✔ A guest that exits to the main menu stays in the session (galaxy menu, co-op lobby open in the
  Co-op item); since 2026-10-23 the host sends its world again after 5 s (back in ~14 s).
* ✔ The mating call is Backspace (blue bar, middle button "Mating Call [Backspace]"). With DNA 0
  nothing happens; with DNA the "Mate: Locate your Mate!" card appears and a thought bubble shows
  over the nest. Clicking the nearby family members did not start the editor within 30 s (not
  explored further; the evolution vote is tested through `coop.vote editor`).
* ✔ The ability bar keys work on an unfocused test window (background mode); WASD and the arrow
  keys move the avatar; a right-drag turns the camera.
* ✔ On the guest, the goal card of a species said "The <species> species is extinct!" while the
  host's card said "Impress 3 <species> to ally": the guest counted the replayed deaths of mirrors
  as kills (section 8.1). Fixed 2026-10-23.

## 6. Herds of our own creatures fill themselves up

* ✔ `CreateHerd(pos, species, 0, 0, 0, 0)` (puppets of players with their own creature, NPC proxies
  on the guest) gives a herd with `mTargetHerdSize` (+0xF4) = 5 and `mbEnabled` (+0x218) = 1 a
  moment later (runtime, 2026-10-23; docs/re/tribe.md E has the herd fields). The game then spawns
  members up to the target: on the host 4 clones of the guest's Kirgle stood at the nest within
  seconds of the join, as real NPCs, mirrored to the guest. On the guest the proxies' herds did the
  same, the update filter removed them again (`local_removed` grew all session). `game::herd_quiet`
  sets the target to 0 and disables such herds (at creation and on every puppet update).
  Regression: `coop_team.py` ("no NPC clones of the guest's creature").

## 7. When the host's simulation stands still

* ✔ In the host's pause menu, in the death caption ("You were killed in battle! ..." with a green
  check that waits for OK, like the space captions in docs/06-testing.md 6.9) and in cinematics no
  creature is updated (`cCreatureAnimal::Update` is not called: the registry of the frame is
  empty). `npc.cpp` used to forget every NPC 2 s later and the guests lost them all until the host
  went on; a frame with no creature update now counts as a pause (the NPCs stay mirrored, standing
  still). Regression: `coop_exit_menu.py` (host paused 6 s: 21 -> 0 NPCs before, 21 -> 21 after).
* ✔ The death caption is `coop.ui find "You were killed"`; `key enter` presses its OK.

## 8. Social games, the species relationship table, goal cards (2026-10-23)

Static analysis: a full Ghidra analysis of the exe in a private project (`analyzeHeadless`, ~15 min,
then queries by a small script in seconds); runtime: `probe`, memory diffs, `call`.

### 8.1 The species relationship table

* ✔ `cSpeciesRelationshipManager::Get` 0xB3D590 (cdecl) -> manager. The table is a hash map at
  manager +0x20 (buckets +4, bucket count +8); a node is `{ResourceKey species, record {relation,
  ?, impressed, killed, -1}, ..., next at +0x20}`. `SpeciesRelationMap_Record` 0xBA3B20
  (thiscall on manager +0x20) returns the record, inserting a zero one.
* ✔ `GetRelationship` 0xBA40C0 `thiscall(mgr, key*, a, b) ret 0xC`: the record's relation, 0 = not
  set (then the archetype tuning +0x390 decides). Creature stage values: 6 allied (and the own
  species), 5 extinct, 4 / 3 impressed some (~), 1-2 hostile (~).
* ✔ `ApplyEvent` 0xBA49E0 `thiscall(mgr, int event, key*, creature) ret 0xC`: event 3 = one
  creature impressed (record +8 += 1; ally at tuning +0x40C, 4 at +0x408), 0 / 5 = killed (+0xC;
  extinct at tuning +0x400 via 0xBA4730), 2 = attacked, 6 / 7 nest events. A change refreshes the
  creature UI (`CreatureGoalUI_Get` 0xD37CB0 -> [0x169F210], vt 0x28(0)).
* ✔ 0xBA4730 "the species is extinct": event-log message, and every herd (noun 0x01BE418E) of the
  species gets 0xC6AEA0 (~ stopped). 0xBA4820 on a change: allied -> every herd of the species
  0xC6A1E0(creature).
* ✔ `TakeDamage` calls ApplyEvent 2 for every hit and 0 / 5 for every death (also without a
  player attacker; a killed creature of an allied species breaks the alliance: Kooks went from
  (6, 3 impressed) to (1, 0 impressed, 1 killed) when one of them died, on the host). On a guest every replayed death of a mirror counted as a kill of the species,
  and a mirror that re-entered the relevance radius dead replayed it again: three of them made the
  species extinct on the guest ("Hunt: The Kook species is extinct!" on its card). Fix: the guest
  drops relationship events on mirrors and puppets (`social.cpp` hook), the host sends its table
  (MSG_SPECIES, on change and every 10 s) and the guest writes it into its own every 0.5 s.
* ✔ Kook tuning (archetype tuning +0xE84 of the creature): +0x3FC 1, +0x400 3 kills to extinct,
  +0x404 1, +0x408 2, +0x40C 3 impressed to ally ("Impress 3 Kook to ally"), +0x418 25 (ally DNA),
  +0x420 20, +0x424 1 social round, +0x428 5.0 s, +0x42C 0.05.

### 8.2 Goal cards

* ✔ A species card ("Impress 3 Kook to ally", "Hunt ...") is a game object, noun 0x1406A572, vtable
  0x146BED8. Init 0xC30300 copies the species key and the tuning thresholds; `SpeciesGoal_Update`
  0xC2EE60 `thiscall(goal, out, out) ret 8` reads impressed / killed / relation and, when the species
  just became allied (6) or extinct (5), pays the card's DNA (AddEvolutionPoints) and posts
  0x43715491 / 0xB4987E04; `SpeciesGoal_Refresh` 0xC2FB90 runs it and sets the texts. It runs when
  a relationship message reaches the card (handler 0xC2FD40): a table written by the mod needs a
  call of 0xC2FB90 on every such object. A card exists only for a species the player selected
  (a click on one of its creatures shows it), and its init caches the current counts: a card made
  after the species became allied pays nothing (✔ runtime: cached +0x8C impressed 3, +0xA0 relation
  6). Field ints: +0x7C state, +0x80 species key, +0x8C / +0x90 cached impressed / killed, +0x94
  ally DNA (25), +0x98 hunt DNA (20), +0xA0 cached relation, +0xB0..+0xBC the tuning thresholds.
* Co-op: whichever machine's card pays the ally / extinction reward, it goes to the host, which pays
  it once per species and outcome into the shared pool (MSG_SOCIAL kind 3; `goal_dna_paid` in
  `coop.social` on the host). Seen: Kooks allied by the guest, 2 x 10 DNA for the impressions and
  25 for the ally, once.

### 8.3 How the native social game runs

* ✔ The avatar's click on an NPC in the social stance posts the social stimulus 0x10000 (value 5)
  to the NPC's AI (`BT_PostStimulus` 0xBC9940 `thiscall(ai(+0xB4C)+8, maskLo, maskHi, float,
  sender) ret 0x10`). The NPC's behaviour tree answers: node 0x341BD4BE (table at 0x15943A0: start
  0xDA43D0 "SocialPropose", tick 0xDA49C0, end 0xD9F810) builds the session (rounds = tuning +0x424,
  +1 when +0xB58 bit 0x1 is set: ~ the alpha, its card reads "Alpha Kook"; the selection guard in
  coop.cpp treats the same bit as "pack member"), the meter UI (0xE31xxx) and the prompts. The player's ability keys
  answer it; without such a session the keys do nothing (no animation either).
* ✔ The win: `CreatureSocialSuccess` 0xD9BC40 `thiscall(session, npc, other) ret 8` (called at
  0xDA4A6B when the meter is full): ApplyEvent 3, DNA = `CreatureDnaWorth` 0xC04260(npc, 1) (Kook:
  10) + a meter bonus, popup, message 0x60B4123 {avatar, other, 0} via 0xD39360, npc +0xB58 |= 0x80
  (impressed: green face), UI refresh. Returns the reaction animation (by relation: 1 0xF37DB906,
  2 / 5 0xD37DB8F6, 3 0x538BF747, else 0x738BF75B) that the tick plays with `CreaturePlayAnim`
  0xC122B0 `thiscall(c, anim, 1, -1) ret 0xC`.
* ✔ `CreatureSocialMode` 0xC0E7C0 (thiscall, no args): 0 = no social game with this NPC.
* ✔ Abilities: creature vt 0xB0 count, vt 0xB4 GetAbility(i); ability +8 type, +0x10 is 1 on the
  social ability (~: sing, type 0x25, index 4 on both test creatures). `PlayAbility` 0xC1E580
  `thiscall(c, index, 0) ret 8` plays it (-1 while cooling down). The ability mode (stance) is the
  global 0x169F374: 0 social, 1 combat.
* ✗ Letting a mirror run its own AI on the guest (to get the native session there) does not work:
  the mirror left its herd (+0x1674 = 0) and was destroyed by the game a minute later (a dangling
  proxy, a fault in coop.dll; 0xBA4730 then faulted on the null herd). Not pursued.
* Co-op (social.cpp): the guest's click engages the mirror (the stimulus is seen in the hook), a
  social key plays the guest's ability and sends it to the host (MSG_SOCIAL), the host counts the
  rounds the native game would ask for and applies the win like 0xD9BC40 without its meter (event
  3, DNA, impressed flag, reaction animation on both). Not done: the meter and the NPC's prompts on
  the guest, message 0x60B4123, abilities used by a click on the ability bar.

### 8.4 Mirrors in the nest herd

* ✔ Herds are game objects, noun 0x01BE418E; species profile at +0xA4, nest at +0x160. Mirrors on
  the guest go into the nest herd of their species (from the local NPCs the update filter removes,
  else the first nest herd of the species among the game objects): `Create` gives them the herd's
  archetype tuning (+0xE84), which the social code and the avatar-event handler (section 4) read.

### 8.5 The pause menu window

* ✔ The pause menu "Options" is one window (control id 0x0615B50D) that stays loaded: shown when
  its flag 0x1 and its parents' are set. coop.cpp looks it up 5 times a second (outside the frame)
  and does not draw the world labels while it or the co-op window is up.

## 9. Saves made while an editor opens do not load (2026-10-23)

Found on branch fix/vote-load-hang (regression: `coop_exit_menu.py` part 4; repro scripts and the
broken save were kept in `out/vote-load-hang/`, not in git).

* ✔ What happened in play (docs/stability/creature.md, "a guest in the galaxy menu when an
  evolution starts hangs in the loading screen"): the host's evolution vote ran, the guest left to
  the main menu; 5 s later `follow_phase` sent the host's world again (`left_to_menu`), which first
  **saves** (`save_game`: MessagePost 0x1CD20F0, written by the game over ~2 s, the "saved" box
  after 1.7-2.0 s). The vote ended at the same moment and the host's editor request was submitted
  (outside the frame) while the game wrote that save. The guest received it and stayed in
  `LoadGame` 0x1654C08 for good: frames running (30 fps), no fault, no watchdog stack (the main
  thread is not stuck, the load mode never switches to the stage); the screen shows a space
  backdrop with an asteroid. The next world was then cancelled by `via_menu_tick` after 60 s ("the
  game did not go to the galaxy menu": the game never leaves LoadGame).
* ✔ The save itself is the cause, not the guest's state: the same files copied into the host's
  profile (`coop.load Game5 Scalinus`, in a game that had just loaded the good save) hang the host's
  game in LoadGame the same way.
* ~ What differs in the broken `Scalinus.spo` (DBPF; resource 0x0179D310 is a table of noun ids
  and their counts, the other resources are one per noun type): no cVisiblePlanet (noun
  0x44462A6, 4 in a good save), new cRock 0x2A8FB3F (1) and cInteractiveOrnament 0x3A2511E (12),
  far more 0x06ABEE7E (0x686 instead of 0x1C2) and cFruitGroup 0x2E96892 (0x3EC instead of 0x360):
  the save holds the world half torn down / rebuilt for the editor. Which of these keeps the load
  mode from finishing is not known (?); the mode switch handlers 0xB336B0 / 0xB33A50 (old, new)
  call 0xB28AA0 (persistence manager: load, [mgr]+0x20 = LoadGame, `SimulatorModeMgr_SetMode`) on
  the way into LoadGame and 0xB33210 / 0xB331C0 on the way out (not decompiled further).
* ? Not checked: whether a save made with the editor fully open (mode 0xDBDBA1) loads; SporeCoop
  does not try it any more.
* What SporeCoop does: `world::save_game` asks coop.cpp's `save_blocker` first and does not save
  while an editor is open (0xDBDBA1 / 0x1654C06), the game loads (0x1654C08), an editor request
  went to the game less than 5 s ago or is held, or the simulator's mode switcher has a switch
  pending (`SimulatorModeMgr_ptr` [0x167FACC]+0x28); a waiting share retries every 2 s (after the
  editor: once the host is back in the stage). Editor requests (the vote's, and the game's own when
  the host is alone) wait while a save of ours is being written (`world::save_busy`: saved for a
  share and not shared yet, or the saved box not seen within 10 s) or a share still waits for its
  save (`world::save_pending`: otherwise, when the vote ended a frame before the share posted its
  save, the editor opened first, the save was refused and the guest waited in the galaxy menu for
  the whole editor session; seen once as a `coop_exit_menu.py` part 4 failure "back in the host
  world" with "together" only after the host's evolution); at most 15 s, dropped if the game
  left the stage meanwhile. A share whose save saw the game change modes is saved again.
  `coop.vote` prints "editor held for a save N".
* Timing seen with the fix: "back to the world: sending it to Bob" at t, the vote ends 0.06 s later,
  the save's box at t+2.0 s, shared at t+2.6 s, the editor opens at t+2.6 s; the guest is back in
  the creature stage 7 s after the vote ended.
* The guest's own editor at the end of a vote (MSG_VOTE "ended") is opened only when the guest is
  still in the creature stage, no mode switch is pending and neither the pause menu nor one of the
  game's message boxes is up when the outside-frame task runs. ✔ Before: a guest in the "Are you
  sure? All progress will be lost!" box when the vote ended got its editor opened under the box,
  and the "Don't Save" click right after left it in Editor mode (0xDBDBA1) with a black screen for
  good. The pause menu's boxes ("Do you want to save your game?", "Are you sure?") use the same
  container control id 0x06318E78 as the "game saved" box; the pause menu window (0x0615B50D) is
  hidden while they are up.
* Neighbours checked on lane 3 (`out/vote-load-hang/neighbours.py`, not in git), all pass with the
  fix: A) the guest leaves to the menu while the host is in its editor: it gets the evolved world
  when the host is back (8 s); B) the guest is in the exit boxes when the vote ends (above);
  C) the host leaves to the menu (`coop.menu`, the quit message) the moment a guest joins.
* ✔ C before the fix: our share save (posted for the join) and the quit message's own save ran
  together; the host's slot was broken and its next `coop.load` of it faulted at 0xBA7F87
  (`cStarManager::GetStarGridPosition+0xD57`, data 0x1188, called from 0xB27DA6 with 0x1654C08 on
  the stack) and stayed in LoadGame. A save made in the galaxy menu (a share that waited there) did
  the same. Now: `save_game` refuses outside a stage (adventures 0x1654C10 allowed), a waiting share
  saves only once the player controls something again, and a hook on `Simulator_HandleMessage`
  0xB63610 posts the quit message 0x4715068 again while our save is written (`world::save_busy`),
  as the game itself does while a mode switch is pending (`coop.vote`: "quit held for a save N").

## Adventure follower locomotion and clip continuity (dc04)

Verified installed executable SHA256:
`dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`.
The creature spatial vtable is `1469EC0`.

| Spatial slot | Installed entry and ABI | Follower behavior |
| --- | --- | --- |
| `+C4` | `C11570`, thiscall `(spatial,float speed,int)`, `ret8` at `C115BA`/`C115D4` | Resolves creature speed through `C0D1B0`, writes desired speed at spatial `+1E0` using native creature flags. |
| `+C8` | `C41EA0`, thiscall `(spatial)->float`, `ret` at `C41EA6` | Reads desired speed at `+1E0`. |
| `+CC` | `C42080`, thiscall `(spatial)->float`, `ret` at `C42086` | Reads native standard speed at `+1E4`. |
| `+E0` | `C435F0`, thiscall `(spatial,Vec3*,float goalStop,float acceptableStop,bool)`, `ret10` at `C437F4` | Builds native movement request and dispatches spatial `+DC`; false-bool path retains full 3D destination. |
| `+EC` | `C43800`, thiscall `(spatial)`, `ret` at `C438E8` | Dispatches a native stop request and sets desired speed zero. |

`MoveTo` restores standard speed itself: `C437C8` obtains `+CC`, then `C437E3`
invokes `+C4` with that result and zero as the second argument. Issue MoveTo
first, then the desired-speed adjustment. The older driver.h order allows
MoveTo to overwrite its adjustment. No behavior-tree or combat order is needed.
Native movement strategy, collision, water/flying and scale handling remain in
the engine; representative native movement compatibility still needs testing.

Preview 9 mirror() applies combined SetTransform to every cached dynamic
creature each mirror frame, even without a new packet. It marks animation-root
pose dirty (adventure-world.md), preventing native locomotion from advancing
freely between samples. Reserve it for initial binding, severe divergence,
confirmed stationary errors or a stuck path. Update native destinations at a
bounded cadence, preserve the NPC AI fence, and avoid per-frame pose nudges.

### Read-only current clip sampling

Concrete AnimatedCreature vtable `1448AF0` has `+08=A0C5D0` (load),
`+18=A029D0` (start) and `+58=A05090` (current clip). The last is thiscall
`(animated,uint32* ID,float* time,int* extra,int* index)`, `ret10` at `A050FC`.
A sampler can reproduce it without a new native call:

1. Read creature `+B54`, then the query pointer stored at AnimatedCreature
   `+184`. The query is not inline.
2. `9FF690` reads layer at query `+FA4`, requiring less than 16; slot index is
   query `+F0C+4*layer`, also less than 16. Slot is `query+8+F0*slotIndex`.
3. The selector accepts nonnull slot `+0`, or a subindex at `+68` below 8 with
   a nonnull pointer at slot `+8+12*subindex`.
4. `A050B1` reads clip ID at slot `+D8`; `A050E2` reads playback time as a
   double at `+A8` and returns a float; `A050F2` reads an extra int at `+B0`.
   Slot `+B4` is the generation used by indexed lookup `9FF630`.

Bounds-check all pointers and indexes. Record slot/index/generation with ID and
clock: clock wrap alone does not distinguish a loop from a restart. The existing
PlayAnimation C12470 path loads and starts a clip, so it is not an idempotent
"ensure gait" API. A packet request or its return value does not prove restart.

Installed classifier `A026E0` contains the idle-walk family from upstream ModAPI.
`04866DAB` is idle_walk_lookat_loop; `04866D8E` is its start and `04866DB9` one
of its stops. Adjacent classifier `A02740` also accepts `02481DE5`, `027844AD`,
`04330667` and `05B6DDAE`, then delegates to that idle-walk classifier. The exact
authored name of `02481DE5` is unverified here. Preserve unknown and attack clip
semantics; do not blanket-deduplicate repeated IDs.

Hidden baseline `out/lab/smooth-actors-preview9-native-motion/result.json`
sampled current clip state about every 53 ms. Mine marker 138's host gait
04866DAB progressed for the first 8.15 seconds; its guest generally retained
02481DE5 despite spatial following over about 174 metres. A brief replicated
host gait at time zero was replaced about 53 ms later. Across 20 creatures,
there was one same-ID guest slot replacement and none on the host. This proves
a locomotion/gait mismatch, not pervasive same-ID restart. Compare actual clip
clocks, model increments and host controls again on the corrected build.

The portable adventure_actor_motion.h fixture runs a native-style waypoint
walker between 10-Hz packets with arrival jitter/loss, checks bounded prediction,
confirmed stationarity, stale stop, stuck/severe corrections, scoped identity
resets and hold/resume. Its results establish policy behavior; native appearance,
authored movement compatibility and attack playback remain integration checks.

For read-only braking diagnosis, the current movement request is inline at
spatial `+1F4`, not a pointer. `C42020` returns that address. Creature spatial
`+DC=C02AD0` delegates to `C43FA0`; `C44020` copies its request through `C43F40`
to `spatial+1F4`. Request `+14` is the full destination Vec3, `+20` the goal-stop
float, `+5C` the request mode (MoveTo false-bool uses 1, Stop uses 0), and `+60`
the acceptable-stop float. `C435F0` constructs these fields and `C43F40` copies
them; `C17F40` reads that same destination when request mode is nonzero.
Velocity is spatial `+1C8`, confirmed by getter `D20610` and writer `C41EC0`.
Creature-relative offsets are destination `+2C8`, goal-stop `+2D4`, mode `+310`,
acceptable-stop `+314` and velocity `+288`. These fields permit comparing actual
native speed and remaining route distance without issuing movement calls.

The slow-drift policy regression exposed a separate stationarity error:
per-packet motion below 0.025 m accumulated into real walking but was treated
as stationary. The policy now compares a fixed quiet anchor and its net drift
over time, and preserves observed 0.05/0.1 m/s speeds instead of falling back
to full standard speed. Both slow cases, cumulative anchor displacement and
bounded idle jitter pass alongside the original natural-speed tests. Native
goal-stop radius and navigation lookahead remain separate integration concerns.
