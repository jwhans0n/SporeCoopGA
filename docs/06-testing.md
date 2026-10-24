# 6. Testing

> Running the tests in parallel (lanes, `tools/lab.py`, warm resets, chaos bot, bad network):
> [10-dev-pipeline.md](10-dev-pipeline.md). This page covers the single-lane basics.

## 6.1 Environment

* The game is installed in the Bottles bottle **Games** (runner `caffe-10.0`, DXVK); the Spore registry entries are already there.
* Launch: `tools/spore.sh run [-p profile] [-i N]` → `bottles-cli run -b Games -e .../SporeApp.exe -- <args>`.
  Game arguments go after `--`. Bottles filters environment variables, so settings are passed
  as command-line switches (`-sporekit-profile:test`, `-sporekit-instance:2`).
* Build: llvm-mingw (`~/.local/opt/llvm-mingw-*/bin/i686-w64-mingw32-clang++`), `make`.

## 6.2 Profiles - tests don't touch your saves

`-sporekit-profile:<name>` redirects Spore's user data to
`AppData/Roaming/SporeKit-<name>/Spore`. `spore.sh run -p test` creates the profile from a copy of
your settings, but with a 1440×1080 window and no tutorials or hints. Instance `-i 2` uses the
`inst2` profile automatically. A fixture with a Creature stage save:

```bash
tools/spore.sh fixture save creature test       # capture
tools/spore.sh fixture restore creature inst2   # put into another profile
python3 tools/tests/load_game.py -i 2           # load it via the menu
```

Fixtures for other modes (`out/fixtures/*.tgz`) are made like this:

```bash
python3 tools/tests/new_stage_game.py <cell|creature|tribe|civ|space>  # new game via stage selection
python3 tools/tests/new_stage_game.py civ --npc-cities 3  # civ with cities of other nations
python3 tools/tests/space_setup.py      # space: new game, skip tutorial, leave the planet, save
tools/sporectl.py coop.save             # save (same as the menu button)
tools/spore.sh fixture save <name> test
tools/sporectl.py coop.load Game0 Planet   # load a profile save directly from the menu
```

`new_stage_game.py` wants a test profile without saves (empty `Games` folder) and the 1440x1080
window: it finds a free planet slot by itself (each slot ring of the galaxy menu has a 48x48 UI
window with id 0x02E5A4A8 in `coop.ui tree`; `--slot N` picks the N-th, `--slot X,Y` a point),
hovers before clicking and checks that "Choose A Stage" opened. The wizard after it clicks fixed
points; a run that ends with "FAILED: still GalaxyGameEntry" can simply be repeated.

The fixtures (tools/tests/fixtures.py lists planet and checked facts):

| Fixture | Planet | What it has | Made by |
|---|---|---|---|
| `creature`, `cell`, `tribe`, `space` | see fixtures.py | a game of that stage | `new_stage_game.py`, `space_setup.py` |
| `civ` | Game0/Agrauel | the player city only | `new_stage_game.py civ` |
| `civ_npc` | Game0/Taldon | the player city (with its free vehicle) and three cities of other nations (nearest about 400 m away, the planet radius is about 500), intro text dismissed, $3000 | `new_stage_game.py civ --npc-cities 3`, `coop.save`, `spore.sh fixture save civ_npc test` (2026-10-23) |

A new Civilization game has no other cities at first: they appear one by one when the player has
claimed spice and has money, after a timer (docs/re/civ.md "NPC cities"; about 18 minutes of play
in one test). `--npc-cities N` calls `cCitySimulator::AppearNextCity` N times instead.

Test runs don't get in the way of whoever is at the computer:
* **muted**: `prepare_profile` sets `AudioMuteAll true` and `AudioMasterVolume 0` in test
  profiles (the player's sound settings and PipeWire are left alone: PipeWire would remember
  the mute by application name and apply it to normal play too);
* **invisible**: `spore.sh run` starts the test games on a headless GNOME Shell
  (`tools/headless.sh up`: its own D-Bus session and runtime, config and state folders, a 5760x3240
  virtual monitor; its Xwayland display is passed to Bottles). Nothing appears on the user's
  desktop and the games keep drawing at 30 fps. SporeKit places each window in its own cell of a
  grid that fills the screen (`[sporekit] test_monitor = grid`: two 5760x3240 monitors, 8x3 cells, so
  every instance of lanes 1-8 has its own; a covered game window lags). Keep 5760x3240 per monitor:
  on 5760x4320 Wine offers no 1440x1080 and the games come up at 1920x1080 (fixed click coordinates
  then miss). `headless.sh` takes the display from its live Xwayland, never from a log: a stale
  display once made Bottles fall back to the user's desktop. The bus of the headless session must get
  its runtime folder too (env before dbus-run-session): its document portal otherwise mounts on the real
  /run/user/$UID/doc and unmounts it when stopped, and Bottles fails with "Can't find source path
  .../doc/by-app/com.usebottles.bottles" (fix: `systemctl --user restart xdg-document-portal`). Do not move test
  windows to a hidden workspace of the real desktop: GNOME sends them no frame callbacks, DXVK waits
  and the game stops at frame 1. `SPORE_HEADLESS=0` shows the games on the desktop (then SporeKit
  moves them to a non-primary monitor: `test_monitor = auto|primary|off`). `tools/headless.sh down`
  stops the headless shell. Sharing the real runtime folder with it would unmount the Flatpak
  document portal, so it never does.

## 6.3 Development loop

```bash
make && tools/spore.sh deploy
tools/spore.sh run -p test
python3 tools/tests/load_game.py                 # up to the Creature stage
make coop && tools/spore.sh deploy && tools/sporectl.py mod.reload coop
tools/sporectl.py shot                           # look at the screen
tools/sporectl.py exc                            # caught exceptions (address + symbol)
tools/sporectl.py hangdump                       # main thread stack to the log (can hang under Wine)
tools/sporectl.py stack.here 300 all            # main thread call chain, read ON the main thread
tools/sporectl.py stack.scan 300                 # exe return addresses on the main stack, no suspend
tools/spore.sh logs                              # tail of sporekit.log
```

New symbols in `data/symbols/*.sym` reach a running game only with `sym.reload` before the
`mod.reload coop` (mods resolve their symbols when they load; without it `H->sym` returns 0). A
`mod.reload coop` ends the co-op session (both sides show `offline`): to look at a two-player state
again, start the pair anew (`twoinst.start`, ~40 s). Cell stage: `coop.pool prey [r]` on the host
lists the NPCs within r m of a player with the game's own prey / threat answers for each player
(docs/re/cell.md 9), `coop.pool prey 30 sum` only the totals (play_cell.py samples them).

## 6.4 Test levels

| Level | What | How |
|---|---|---|
| Unit (Linux) | serialization, reliable delivery under 30% loss, localhost session, rejoin through a relay with the goodbye lost, a world-sized transfer over a netem-like awful link, interpolation | `make test` |
| In-game loopback | a puppet mirrors your own avatar | `sporectl coop.echo on 6 0.3` |
| Protocol | host in the game + a fake Python guest | `python3 -I tools/fakepeer.py 127.0.0.1:47810 --mode circle` |
| Integration | two game instances on one machine, host/join, world transfer | `python3 tools/tests/coop_two_instances.py` |
| Modes | any mode from a fixture: guest joins from the menu, receives and loads the world, both puppets, movement | `coop_mode.py --fixture <cell\|tribe\|civ\|space> --planet GameN/Planet`, `coop_mode.py --adventure` |
| Bad network | the guest leaves and joins again (even rounds lose the goodbye), timings of handshake, world transfer and reload | `SPORE_NET=awful python3 tools/tests/coop_rejoin_net.py --rounds 4` |
| Rejoins and loads | the guest leaves and joins again many times, the host world is loaded again through the galaxy menu, both fault logs (`exc`) checked after every round; `--reshare a:b` sends the world again during the load, `--race a:b` rejoins at random moments, `--menu a:b [--clicks]` joins from the galaxy menu, `--local-load a:b --cold` loads a save right after the menu came up in fresh games (the planet-texture fault of docs/re/space.md 8.3: 4 of 17 such loads faulted before the fix); `--stop` ends at the first fault | `python3 tools/tests/coop_rejoin_loop.py --rounds 30 [--reshare 0:2.5]` |
| Manual | real game over the network | two PCs / VPN, package from `tools/package.sh` |

## 6.5 Startup speed

The two-instance test is ready for checks in about 40 s (it used to take about 90):
* both instances start right away (2 s apart), without waiting for the first one;
* instead of long frame-count waits after loading, a readiness check plus a 4 s pause;
* the host's "game saved" box (the host saves before sending the world) is confirmed by the
  mod once it is visible (docs/08-ui.md); `twoinst` also presses Enter when the host's frames
  stall. A box left open pauses the host's game without stopping its frames: the host's cell
  does not move (coop_cell checks that no box is left);
* over LAN/localhost the world is sent with a large window (600 messages in flight at ping
  < 40 ms, 4 MB socket buffers): 3.8 MB in ~2.5 s instead of 7.5 s.

The game caps itself at 30 frames per second (33 ms per frame, `App::FrameLimit_ptr`).
`[sporekit] test_frame_ms` speeds up menu frames in test instances, `frame_ms` does so everywhere;
console: `frame.limit <ms>|auto`. Off by default: cell physics is tuned for 30 fps
(at 130 fps the player's cell drifts on its own), and UI tests wait in frames.

### Sync accuracy

`tools/tests/coop_sync.py` compares, in every phase, the position and rotation of each
synchronized object on the host and on the guest (command `coop.sync`: players, animals, cells,
citizens, vehicles, ships). Each guest sample is compared with the closer of the two host samples
around it. Typical values (90th percentile): cells 0.02 m, animals 0.2–0.7 m, citizens
0.8 m / 6°, partner 1 m / 6° (lag equal to the smoothing time), vehicles 1.2 m, ships
8.5 m at speeds around 100 m/s.

`tools/tests/coop_npc_multi.py` (4 players from two lanes, nobody moves, ~4 min) checks the NPC
sync of every guest over 40 s (p90 of the living NPCs <= 1.5 m; typical 0.25-0.45 m), that each
guest's avatar stands at the host's height for it, and that no game caught a fault. A guest whose
planet loaded without its height map (docs/re/space.md 8.2) fails the height check with about -6 m
and its NPCs at 6-9 m. `coop_multi.py` (chaos) samples one moment per minute, so a fight or a
rejoin can push a single round over its 3 m limit; read its per-round `n p90` lines.

## 6.6 Known Wine quirks

* **Flatpak document portal.** Every Bottles launch bind-mounts `/run/user/<uid>/doc/by-app/com.usebottles.bottles`
  (FUSE of `xdg-document-portal`). It vanishes now and then while many Bottles sandboxes exit at once (seen every 10–20 min during lab runs on 2026-10-22; systemd then shows the portal restarted), and every start fails with
  `bwrap: Can't find source path …` in `out/run.<inst>.log`. `systemctl --user restart xdg-document-portal`
  brings it back; `tools/lab.py` checks it before every test and retries such starts (not counted as flaky).

* MinHook must not freeze threads (`VirtualProtect` may hang).
* In the second process of the same wineserver, blocking sockets/`select()` and starting new threads
  sometimes hang - the console runs on non-blocking sockets in a single thread.
* Spore stops rendering without focus; test runs enable SporeKit's "background mode".
* **A window on a hidden workspace is minimized.** Mutter marks every X11 window on a workspace that
  is not shown as iconic, and Wine then minimizes it (`WM_SYSCOMMAND SC_MINIMIZE`, `IsIconic` = 1).
  A minimized Spore draws nothing: `App::Canvas::Tick` (0x848260) calls the canvas's IsMinimized
  (vtable +0x5C, `IsIconic(GetAncestor(hwnd, 3))`) and sleeps 200 ms per tick, so a test game on
  the agent-ws `sporecoop` workspace stalled at frame 1 to 70 (watchdog "no frames", main thread in
  `Canvas::Tick`, 2026-10-22). Background mode ignores `SC_MINIMIZE` now (log line "minimize
  ignored"); hooking `IsIconic` alone was not enough (the main thread then spun in the message loop).
* **Exiting one instance kills all instances in the bottle** (that's how launching through
  bottles-cli behaves). So tests restart both instances together: the guest first, then the host.
* **Stack of a frozen game.** `stack` and `hangdump` suspend the main thread; under Wine that hung
  the console for good (2026-10-22). When frames stop but the console still answers `game.mode`,
  the main thread is pumping messages in some nested loop: `stack.here 300 all` runs on the main
  thread itself and lists every return address from its own frame up (modules and offsets; coop.dll
  offsets: `llvm-symbolizer --obj=build/coop.dll 0x1000_0000+offset`). `stack.scan` reads the stack
  from the TEB without suspending (stale values mixed in). Filter exe values to real return
  addresses (a call instruction before them) - UTF-16 text looks like code addresses. `winedbg`
  attach from `flatpak enter <wineserver pid>` failed with error 5.
* A hung Wine process can be left half-dead (a zombie leader with live threads)
  and keep holding UDP port 47810. `spore.sh kill` finishes such processes off with `SIGKILL`.

## 6.7 Reading console output in tests

* Tests parse `coop.status` with regular expressions that span lines, e.g. coop_cell / coop_creations
  / coop_vote expect the `puppet pos=… species=…` line **right after** the `remote <id> '<name>'`
  line. New per-remote lines go after the puppet line (the `together <id> <0|1>` line that
  `twoinst` waits for is the last line of each remote's block). On 2026-10-22 a `together` line
  inserted between them made coop_cell report "sync samples taken: samples 0" on every run while the
  partner cell was there and exact - it looked like a missing puppet / timing bug.
* `coop_cell.py` reads `coop.pool` keys with `re.search(key + '=(-?\d+)')` (no word boundary): a new
  field whose name ends in an old key (`own_food=` before `food=`) is read instead of it. On
  2026-10-23 that turned "shared food progress" into "0 -> 0"; the field is `own_points=` now.
* Cross-check before hunting a "missing puppet": the `remote … puppet=<ptr>` value, and in the cell
  stage `local_gameplay_cells` (counts every foreground cell except the own avatar) = mirrors + 1
  when the partner's cell exists.
* A real missing partner (`partner_dist=-1` on the guest, no puppet, bites and food still work)
  was the host's "game saved" box left open (fixed 2026-10-22): look at the host screenshot
  first. A host that "moves" 0.00 m in the sync samples is paused the same way.

## 6.8 Editors in tests

* `coop_vote.py` and `coop_guest_editor.py` open the creature editor through the evolution vote
  (`coop.vote editor`). In the 1440x1080 test window "done" is at `click 1416 1062`, cancel at
  `click 1376 1062`.
* The fixture's DNA is 0, so a test cannot add parts. An edit that costs nothing: drag a part that
  is already on the creature (`drag 1110 480 1060 420` moves the bow of the guest's Kirgle); the
  undo button lights up. An edited creature is saved to My Creations under a new key when the
  editor is left with done (one `Editor_Save` 0x577650 after the thumbnail state machine).
* When "done" seems to do nothing, probe the exit chain (docs/re/creations.md 7):
  `probe 0x5DF4B0` (button), `probe 0x57AAA0` (finish state), `probe 0x4F3C80` (the blocking error
  bit, first argument), `probe 0x58A350` (leaving). `coop.vote` prints `own editor` and how often
  the part-list error was waived.
* Leaving the editor after the host has evolved: the guest is in the stage for ~2 s, then goes to
  the galaxy menu and loads the host's world (~5 s); its own body is set 3 s after that.

## 6.9 Captions that wait for the player

* Some cinematics stop with a caption and a green check button and wait for OK as long as it
  takes, e.g. the Space death caption ("...you return to your homeworld") and the respawn caption
  ("We've built you a new ship..."). The ship respawns only after OK. Tests press it with
  `key enter` (works on an unfocused window in background mode); `coop.space hp` shows `cine=1`
  while the death caption is up and `movie=1` while any cinematic runs. A test that waits for a
  respawn without pressing OK waits forever (that was the "respawn never came" of
  `coop_space_play.py`, 2026-10-22; see docs/re/space.md 7.2.1).
* The player's ship in a star system can not be moved away from a test: writing its position or
  calling the locomotive SetPosition is undone at once (the ship stays near the camera target);
  to test "nobody near", `coop.space repairdist 0` sets the repair range to 0.

## 6.10 Playing bots

* `tools/tests/play_cell.py [--minutes 10] [--seed N]`: host and guest play the Cell stage through
  real input (hold the mouse and steer with `button x y down` / `move` / `button up`, clicks,
  W/A/S/D) and measure sync, shared food, knockouts, revives and damage; results and the bugs it
  found: docs/stability/cell.md. Its eyes: `coop.pool see` (kind, size ratio, distance, screen
  position of what is on screen); `coop.pool why` lists who removed mirrors on a guest.
* A cinematic in the cell stage sets `cine=1` in `coop.pool`: the level-up zoom ends by itself
  (~1-2 s), captions wait for the green check (bottom right) or Enter.
* A 10-minute session takes ~11 minutes with the start; the bot reaches 300-650 food (alone: ~270).

## 6.11 Bots that play through real input (play_creature.py)

* `tools/tests/play_creature.py --minutes 10 [--seed N]`: host and guest play the Creature stage in
  two threads (walk, WASD, right-drag to look around, hunt NPCs found with `coop.targets all`,
  joint hunts, social, eating, pause menu, F9 window with chat, zoom; the guest once exits to the
  main menu). Measurements and findings: docs/stability/creature.md.
* Clicking menus: `tools/tests/uifind.py` (`shown(con, caption)`, `press(con, caption, expect)`)
  finds a window by caption with `coop.ui find` and adds up the parents' rectangles (each is
  relative to its parent; flag 0x1 = visible). A menu stays "visible" under a dialog it opened:
  `press` waits for what the click should open, it does not wait for the button to go away.
* The F9 window: hold F9 for ~0.25 s (`key 0x78 down`, `up`): the mod polls the key state, a tap
  is missed. `coop.uistate` prints every widget with its rectangle. Typing into the chat line: click
  it, wait ~0.5 s (the edit takes the focus a frame or two later), then `text "..."`.
* Ability keys: the stance buttons above the ability bar are at `(W/2 - 30, H - 82)` (social) and
  `(W/2 + 43, H - 82)` (combat); keys 1..4 use the current stance's abilities (docs/re/creature.md 1).
* Player sync in play: standing players match within 1.5 m (p90 ~0.6 m), moving ones trail by the
  interpolation and walking delay (median ~0.5 s); in fights a partner's puppet snaps at 4 m.
  coop.sync's 1.5 m limit for players is for walking (coop_sync.py), not for fights.
* Social games of a guest (`coop_creature_social.py`): social stance, a click on the mirrored NPC
  (`coop.social` shows `engaged=<id>`), then hold the ability key (`key 0x31 down`, 0.3 s, `up`):
  the mod polls keys 1-4 and a bare `key 0x31` tap is missed, like F9. `coop.social act <id> [slot]`
  does the same without input; `coop.social species` prints the species table (both machines must
  print the same). A guest idling at a nest gets attacked: the test sets `coop.team hp 1000`.
* The host leaving to the main menu (`coop_exit_menu.py` part 3): "Exit to Main Menu" goes straight
  to the menu when nothing changed since the last save (the world was just saved for the guest), so
  the "Don't Save" dialogs may not come.
* A game that never leaves `LoadGame` (0x1654C08) keeps its frames running: the monitor and
  `Checks.faults()` see no freeze and no fault, and the watchdog writes no stack. Tests that load
  worlds check `game.mode` themselves (`coop_exit_menu.py` part 4 fails after 30 s in LoadGame; a
  normal load leaves it within about 1 s). The race of part 4 (the vote ends on the frame the host
  saves for the guest) is forced by polling the host's log for "back to the world" and sending
  `coop.vote go` right then.
* Several agents on one machine: each one gets its own lane; temp files belong in a folder of
  your own (subagents of one session share the scratchpad folder: another agent's `up.py` there
  started the cell fixture in this lane, 2026-10-23).

## 6.12 Space game bots (play_space.py)

* `coop.targets all` lists what a bot can fly to: in a solar system its planets (`obj P/C<PlanetID>`),
  on a planet its cities (`obj T<n>` ours, `X<n>` others), in the galaxy view the stars near the ship
  (`obj S/H<StarID>`), and partners off the screen as `far player <id> x y` (their projected point).
  `coop.space star` prints `active <PlanetID>`, the planet a click sent the ship to (a moon next to a
  planet often takes the click). Inputs per view: docs/re/space.md 9.2.
* The space fixture's ship has no interstellar drive: the galaxy view stays locked. Tests give it
  with `coop.space tool add interstellardrive` (a badge cinematic and a tutorial caption follow:
  confirm them with Enter).
* Just after landing, damage to the player's ship is ignored for a few seconds (`coop.space hit`
  and relayed NPC hits alike); wait or retry.
* `coop.space hp` prints `hp <n> max <n> ...` (no `hp=`): a parser that looks for `hp=` gets
  nothing, and `coop.space hit <-1 - x>` then heals the ship (negative damage).
* The tool palette's state (open or closed) is read from a screenshot (tab icons on row H-151 or
  H-51); a blind click on the wrong row fires the selected weapon into the world. Never press Esc
  twice: the second one opens the pause menu.
* The bots run in two threads with their own console connections; the measurement loop uses a
  third pair. Each run prints its seed; `--seed N` replays the same choices (the game itself is not
  deterministic, so the same seed gives a similar, not the same, session).

## 6.13 Playing the Tribe stage through real input (play_tribe.py)

Learned while writing the tribe game bot (2026-10-23, docs/stability/tribe.md):
* `drag` and `click` return at once; the game sees the press a few frames later and a drag takes
  ~15 frames: read the selection (`coop.tribe` `sel=1`) ~1.2 s after a `drag`, not right away.
* Selection boxes are screen rectangles around `coop.targets` points (the feet): a box from
  `y-55` to `y+25`, `x±35` catches a walking member; boxes that start on the HUD (the roster at
  the right edge, x > ~1375, the bottom bar y > ~950) do nothing.
* `coop.targets objs` lists everything on screen: `member <id> <owner>`, `obj t<id>` (citizens of
  every tribe), `n<id>` (animals), `tree` (fruit trees), `o2C9CC91` (small food plants),
  `hut<i>[p]` (huts, `p` = the player tribe), `tribe<i>[p]` (tribe centres, also off screen),
  `fruittree` / `tree` (planet trees with / without fruit left), `o<noun>` for the rest.
* **Hover before a right-click on an object**: the game picks the object under the cursor once per
  frame; `click` jumps the cursor and presses 3 frames later, which often still hits whatever was
  under the old cursor position (the ground: a walk order). `move x y`, ~0.4 s, then `click x y
  right` picked fruit trees 37 of 40 times (base point, 10 and 20 px above it); without the move
  about 1 in 3. `world_to_screen` also projects points hidden behind hills and the horizon: a
  bot filters targets by the distance column of `coop.targets objs` (metres from the planet point
  under the mouse: `move` the mouse to the middle first), e.g. < 90 m for trees around the village.
  Objects in front (the fire pit, the hut, members) take the click instead: skip targets within
  ~70 px of them.
* Camera: the arrow keys pan (~30 m/s), the wheel zooms, a right-drag rotates. The Home key does
  nothing in the tribe stage and **`H` opens the Spore Guide**, which swallows every later click
  (close it at `click 1338 59`). The minimap is the way to look somewhere: a click there centres
  the view on that spot. `coop.avatar` prints the planet point under the mouse (`pos=`), so 3
  clicks calibrate a linear minimap -> world map (~3.7 m per pixel on Cohrach); one correction
  step lands within 30 m.
* The tribe UI interaction mode: social at `click 725 1030`, attack at `click 795 1030`;
  `coop.status` prints `tribe ui mode=0|1`. The tribe palette at the bottom is locked in the
  fixture ("Conquer or ally with other tribes to progress"): nothing can be bought there.
* `coop.status` prints `tribe last order:` (guest: what it sent, host: what a guest order became
  and why it was rejected) and `tribe last local order: #n` (the host's own orders): a click that
  changes neither gave no order (it missed the target).
* `coop.tribe spawn <n>` (host) adds adults to the player tribe: test setup only.
* `coop.tribe select <id> 1` only sets the member's selected flag: a walk order (`coop.tribe
  walk`, a right-click on the ground) uses it, but a right-click on an object takes the terrain
  cursor's selection set, which only a real click / box fills: such clicks then give no order at
  all. Select by `drag` for real-input tests (play_tribe.py `Game.select_idle`).
* Agents running in parallel share one scratchpad folder: give helper scripts unique names (one
  agent's `reload.sh` was overwritten by another's, which then hot-reloaded the wrong lane).

## 6.14 Game bots in the Civilization stage (play_civ.py)

* Find things on screen with `coop.targets` (`city <host index> <owner> x y`, owner -1 = another
  nation, 253 = shared capital; `vehicle <net id> <owner> x y`), buttons with `coop.ui where <id>`
  (screen rect + enabled) or `coop.ui at <x> <y>` (every window under a point), building plots with
  `coop.civ slots <city>`, any object kind with `coop.civ nouns [noun]` (spice nodes: 403DF5C).
* `coop.targets` / world_to_screen do not know the planet: a point behind it still gets screen
  coordinates. play_civ.py projects with the `coop.cam` view rows and tests the ray against the
  planet sphere itself, and pans with W/A/S/D until the point is in a box (`Player.look_at`).
  Wait until the camera stops before reading screen positions (it glides ~0.5 s after a key).
* A box selection (`drag`) only works on what is on screen right now; a right click on water or
  across the sea gives a land vehicle no order (on the host too: compare host and guest rates, do
  not count it as a lost order). Attacks go through the target city's menu (docs/re/civ.md 9.1).
* A capture caption stops the host's world until Enter (`coop.civ` status `movie=1`).
* Console connections are per thread: a bot thread and the audit must not share a `Console`
  (replies get mixed: a `coop.uistate` answer parsed as `coop.civ`).
* `update_check.py` restores lane 6 to the original game (no loader); `spore.sh deploy` installs
  the loader again in such a lane.

## 6.15 Native Win32 adapter fixtures

Optional Makefile targets cross-compile fake native adapter fixtures and do not
start the game. Run their EXEs on Windows. The relationship, social, mirror and
blocked-update targets are `test-relationships-windows`, `test-social-windows`,
`test-mirror-windows` and `test-blocked-update-windows`.

The last target produces `build/host/adventure_tick_dispatch_tests.exe`. The
unmanifested version was refused at process creation with Windows error 740
(elevation required), including after renaming it. The explicit
`tests/native_fixture.manifest` sets `requestedExecutionLevel=asInvoker`; the
rebuilt fixture runs without elevation and reports its actual test result. A
missing console line or unchanged PowerShell `$LASTEXITCODE` after launch failure
is not a passing fixture. Python `subprocess.run` reports process-creation errors
directly when diagnosing this Windows behavior.

`test-debug-capabilities-windows` includes the actual legacy `debug.cpp` command
implementation, then discards unrelated commands at link time. Its `coop.w2s`
cases cover missing native capabilities before any native call, null managers and
viewers, failed ray conversion and the valid fake ABI. It does not launch a game.


### Preview 3 native preparation recovery (Windows)

Run the hidden, muted Windows lab with two isolated profiles, then:

```
py -3 tools/tests/coop_adventure_preparation.py --metadata out/windows-lab/captain-recovery-fixture/processes.json --output out/windows-lab/captain-recovery-cancellation --key 29058784:366A930D:408A0000
```

The key above is the local `<Live or Die>` reproduction fixture, not a shipped
stock key. Supply a resource already present in the isolated profile (or omit
`--key` for Adventure Town). `--play` additionally creates two distinct named
captains and verifies the committed run. Never point this test at player profiles.
Checks cover selection/naming/browser/save-animation cancellation, immediate
retry, disconnect, both native menu states, browser visibility and native faults.
The old cached Sporepedia wrapper can remain after close; browser+1C is the active
flag. Empty UI captions omit quotes entirely, so parse a single tree line before
looking for quoted caption text; a multiline regex can consume the next control.


Preview 3 delivery validation on 2026-10-23: `<Live or Die>` (local adventure key
29058784:366A930D:408A0000) entered a committed two-player run with new captains
2905A7A4 and2905A7A7, both model type4178B8E8 and XP0. Normal native Sporepedia
Play also passed with an existing host captain and a new guest captain. The final
matrix passed new/existing and existing/existing combinations, an authored
captain adventure (6FCBCE96), loaded cancellation and menu recovery. The final
405-actor Adventure Town regression measured21.61 FPS on each hidden session,
with native game time advancing and no recorded native faults. Cancellation,
name/review, open shopper, save-animation and disconnect recovery passed without
restarting either game between attempts. Portable `make test` and11 Win32 ABI
fixtures passed. Artifacts and detailed logs are in out/captain-recovery-re and
out/windows-lab/captain-recovery-* (ignored local test output).

Use a unique output directory for each WindowsLab runner: an older runner can
write its final process metadata after a new runner starts if both share a path.
An automated native Review click can arrive before Finish accepts input; require
the panel to close and retry the mouse tap instead of assuming one tap saved it.

Shared lobby regression (preview4, protocol8):

```
py -3 tools/tests/coop_adventure_lobby.py --metadata out/windows-lab/preview4-final-fixture/processes.json --output out/windows-lab/preview4-final-validation
```

This uses stock Mr Puzzle's Magical Gates and Adventure Town in recorded isolated
profiles, native lobby buttons and captain creation, saved names/artwork, explicit
host readiness, replacement cancellation, retry and disconnect recovery. Native
buttons can ignore an initial click while acquiring focus; retry only after checking
the expected state, especially for toggling Ready. Native Back control05933FE1
returns captain selection to adventure selection (stage1); party preparation treats
that as picker cancellation. `--instances 21,22,23,24 --four-player-only` exercises
the stock adventure card, readiness barrier, authored captains and act snapshots
with four isolated clients. This establishes those routes, not a full playthrough.

Preview4 delivery validation passed the full lobby script with two and four
clients, including four distinct saved captains and simultaneous replacement
cancellation. Mr Puzzle's native act music remained [1,0,0,0] on all four clients.
The final DLL passed ordinary Sporepedia Play, return from settings to the shared
cards, native entry/cancellation and a405-actor regression at21.49/21.61 FPS.
Portable tests, eleven current Win32 fixtures and the115-function/four-global
symbol manifest passed. Evidence is in out/windows-lab/preview4-*.

For the missing-art case a stock scenario was cloned under7F00F004 with metadata
and no PNG, registered through the existing verified packed-database ABI in a lab
process, then transferred normally. Both clients displayed No preview, enabled
Ready and entered the native run without faults. Dropping this fixture into the
lab's Data/DataEP1 directories did not make it visible to the resource manager;
do not assume copying a package registers it in this runner.

### Preview 5 shared scenes, replay, models and gates (Windows)

Use a recorded hidden, muted WindowsLab installation and profiles. These scripts
validate their process metadata and reject player profiles. In sequence:

```
py -3 tools/tests/coop_adventure_flow.py --metadata out/windows-lab/preview5-release/processes.json --output out/windows-lab/preview5-release-intro --instances 21,22,23,24
py -3 tools/tests/coop_adventure_talk.py --metadata out/windows-lab/preview5-release/processes.json --out out/windows-lab/preview5-release-talk --x 557 --y 340 --observers 23,24
py -3 tools/tests/coop_adventure_gates.py --metadata out/windows-lab/preview5-release/processes.json --output out/windows-lab/preview5-release-gates --instances 21,22,23,24
```

Before Talk, move guest22 near marker158 with its actual network player ID and
allow snapshots to settle. Instance numbers and network player IDs differ:
simultaneous joins can give instance22 playerID2. `Game.player_id()` reads the
active network ID. The scripts verify native intro clock holds, every participant's
confirmation, independent dialog pages, exact owner/puppet scales, native keys,
wrong colors, key ownership/consumption and open-gate snapshot reconstruction.
`forgetgate MARKER` is a guest diagnostic that deliberately removes an open gate
for that last fixture; it requires the valid native lifecycle/sibling checks.

After the four-player checks, disconnect and quit observers23/24 normally and
keep21/22 in the party. Run `coop_adventure_replay.py --rounds 2`,
`coop_adventure_models.py`, `coop_adventure_performance.py --seconds 8`, then
`coop_adventure_flow_recovery.py`, passing the same metadata and distinct output
directories. Replay includes guest Replay with host Done. Models creates a new
guest captain, then selects an older saved captain next run; actual puppet
species must match the current mapped resource. Recovery cancels partly confirmed
intro/Talk and disconnects in each, checks both native menu states and retries.
The existing lobby `playing()` helper now confirms every intro before waiting
for native state3. Waiting for one player to enter state3 before confirming the
others deadlocks the new intentional group barrier.

Native Talk starts at dialog index-1 before automatically reaching page0. Wait
for the host's first text page before asserting that guest page clicks leave its
page unchanged. Otherwise the native greeting transition causes a false failure.
The `call` diagnostic target parser expects hexadecimal; use named symbols or an
explicit 0x prefix, never a decimal rendering of an address. Verify the native
signature before calling. `actorsat MARKER LIMIT` avoids ad hoc native probes
when inspecting all respawner/physical incarnations at a placement.

Portable `make test` includes `adventure_flow_tests` with fake native callbacks
and real local sessions. Win32 lifecycle tests additionally prove that a physical
gate uses CreateObjectInstance rather than its same-marker respawner factory.
Final evidence is in `out/windows-lab/preview5-release-*`,
`out/preview5-tests-final.log` and `out/preview5-abi-results.json`.

### Preview 6 collector upgrades and teleporters (Windows)

Use preview6/protocol10 in recorded hidden, muted Windows lab processes. Wait for
the agent owning that lab to hand it over before running another native script.
The pickup harness registers immutable interaction scenarios on the host only;
guests receive their custom root through the real co-op asset transfer. It never
edits a save or registers fixtures in a personal profile.

```
py -3 tools/tests/coop_adventure_pickups.py --metadata out/windows-lab/preview6/processes.json --output out/windows-lab/preview6-pickups
py -3 tools/tests/coop_adventure_pickups.py --metadata out/windows-lab/preview6-four/processes.json --output out/windows-lab/preview6-four-pickups --instances 21,22,23,24
```

`--cases health,speed,damage,armor,teleporter` is the default; use a subset while
debugging, and keep separate output folders for every attempt. The host port is
47830 by default. Each guest joins through its own loopback capture proxy at the
next consecutive port. Startup cancels an existing isolated attempt before
waiting for every process to return to its native galaxy menu. The
helper `adventure_interaction_fixture.py` builds/validates the fixture package and
performs scoped read-only inspection or runtime field tuning.

The accepted two-player build 5b run is
`out/windows-lab/preview6-pickup5-final2/result.json`; all five native cases passed
with zero faults on both clients. Its per-case checkpoint is separate from failed
setup/debugging attempts. The handoff report in
`out/windows-lab/preview6-pickup5-handoff/handoff.json` verifies normal cancellation
to native menus and a direct connected party after the capture proxies closed.

The scenarios clone Mr Puzzle class0 and preserve its native marker layout and
authored five-unit respawn setting. Stable target markers are 108, 110, 112, 116,
117 and 118. A physical consumable may not exist during Intro: the same-marker
respawner9 is a distinct object, and its initial delay is correctly held until
everyone presses Continue. Wait for the expected physical powerup after release.
Do not bind a parent9 to a medkit/boost/teleporter merely because markers match.

The harness uses real native proximity reactions for all accepted collections.
It checks host and guest collector-only grants, shared consumption, simultaneous
health claims, authored respawn/new identity, actual native magnitude, refresh
and expiry. A genuine Talk entry followed by native Next/Finished controls holds
the shared clocks while a boost remains active. Health reads come from both
clients' actual native captains and their replicas.

For outgoing damage, `abilityhit VICTIM_PLAYER ATTACKER_PLAYER ABILITY_INDEX` is
the reviewed outside-frame diagnostic. It invokes ApplyCreatureAbility before
the native boost calculation, with a real native ability and target position.
Compare the unboosted, boosted and expired HP deltas. The older `world hit`
diagnostic starts at TakeDamage after ability multipliers and cannot establish
that a damage upgrade was applied once. It remains appropriate for environmental
hits checking the central host armor divisor. Test friendly fire100 and preserve
authored NPC/faction rules when choosing an actual ability target.

The capture proxy records real protocol 10 PlayerState packets. The stale-movement
case stages on a native surface far from the exit and retains its real UDP state.
After teleport, it preserves that pre-teleport position/generation while using a
current timestamp, preventing an ordinary timestamp check from masking a missing
generation fence. Wait for the legitimate cooldown-probe movement to reach the
exit on observers before injecting; otherwise native interpolation can look like
a stale-packet rewind. A separate real guest transport packet attempts a
claim for host0 and must be rejected. These are tightly scoped loopback faults,
not accepted happy-path claims. Teleport checks also cover the authored exit,
non-entering captains staying put, native cleanup, cooldown, invalid exits,
simultaneous independent captain activations and retained source.

Do not call FX-producing native helpers through generic `call`, which is a
main-thread command rather than an outside-frame command. Read-only reviewed
Cast/GetAbility/property calls are safe there. The pickup rejection probe calls
only the installed preview6 hook, which queues its mutation outside the frame;
refuse to run it against an older DLL. Use named symbols or explicit 0x native
addresses and verified calling conventions. Fixture marker/class addresses from
a private inspection are diagnostic after its resource is released; reacquire
them in the actual loaded attempt before tuning.

```
make test-pickups-windows
```

Run its generated Win32 executable on Windows. This ABI suite includes missing
observer puppets, replacement captain incarnations, expiry-before-grant ordering,
more than 1024 respawn identities, stalled reliable peers and fail-closed accepted
event overflow. `adventure pickups: 0 failures` does not substitute for a native
integration `result.json`. Neither suite establishes a complete adventure
playthrough or every custom palette/ability combination.
