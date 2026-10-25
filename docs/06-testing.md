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
The caption window "Choose A Stage" exists (and has its own visible bit) while the chooser is
closed; only its parents tell: closed `fl=0x2` at an off-screen rect, open `fl=0x3`. Until
2026-10-23 the check looked for the caption alone, so a first click that did nothing (a fresh
profile right after the galaxy intro) passed it and the wizard clicked into the void (two runs of
two failed); now every window of the `coop.ui find` chain must be visible.

The fixtures (tools/tests/fixtures.py lists planet and checked facts; tests take the planet from
there, never hard-code it: a fixture made anew gets a random planet name). All but `creature` were
made anew on 2026-10-23 (the old ones were lost with the game folder), on lanes 7/8 (GOG 2016 build):

| Fixture | Planet | What it has | Made by |
|---|---|---|---|
| `creature` | Game0/Scalinus | the host's red species at its nest (recovered from lane 1's test profile, so a few minutes played) | (old) |
| `cell` | Game0/Cadian | a new Cell game, ~330 cells in the pool, food 0, no weapon | `new_stage_game.py cell` |
| `tribe` | Game0/Zorispe | the player tribe (3 members) and one NPC tribe (it appears ~40 s after the intro caption, which is confirmed) | `new_stage_game.py tribe`, OK the caption, wait for `coop.tribe` to list 2 tribes |
| `civ` | Game0/Arigine | the player city only, intro text dismissed | `new_stage_game.py civ`, click 1410 1044 |
| `civ_npc` | Game0/Tavael | the player city (with its free vehicle) and three cities of other nations (nearest about 430 m away, the planet radius is about 500), intro text dismissed, $3000 | `new_stage_game.py civ --npc-cities 3` |
| `space` | Game0/Madas | in the home system (PlanetID 00460035), first mission ("Fly to planet Vulpica", P02460035, then scan the wreck there), $100 000, no interstellar drive | `space_setup.py` |
| `space_empire` | Game0/Madas | `space` + drive, $1 100 000, two colonies with spice, contact with two NPC empires (6.15) | `make_fixture.py space space_empire --cmd ...` (6.15) |

After each: `coop.save`, Enter on the "game saved" box, `spore.sh fixture save <name> test`. Both
players' creations (`creations-host.package` / `creations-guest.package`, the host's 11 creatures, the
guest's newest one is the Kirgle with the white bow of coop_guest_editor.py) were recovered on
2026-10-23 from lane 8's test profiles: they were byte copies made by `restore_creations` at the last
cold start (host and guest written in the same millisecond); the game later rewrites a loaded
package (same creations, ~150 bytes of the index differ), so other lanes had a different md5.

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
| Space trade | a tool gift by a real click on the F9 Space page (Antimatter Bomb: 5 of 12 charges, a new tool for the guest), no room (the guest at 97/99 keeps 2, 3 come back), a spice sale to the guest's home colony from the roster (both games' shared money +10 x price, the spice moves from the host's cargo to the guest's), faults; rivals mode reported as untested without space_rivalry; about 1 min after the 45 s cold start | `coop_space_trade.py` |
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
citizens, vehicles, ships). It first waits until neither game's time is paused
(`cGameTimeManager +0x48` bit 0, read through the console): the GOG 2024 guest keeps its game time
paused for ~7 s after its world load in the tribe stage (black screen, no citizen `Update`, ~23 fps;
the 2016 guest is already running when `start()` returns; both builds pause ~5 s in the creature
stage). Each round reads the host twice 0.15 s apart, then the guest, then the host again; a guest
pose counts as right when the host had it within that 0.15 s window (`LAG_WINDOW`: the 0.10 s
interpolation delay plus one 20 Hz snapshot interval), i.e. its position is compared with the host's
path and its facing with the host's turn over the window. The guest shows remote objects that late
on purpose; a desync (wrong pose, stuck or drifting object) is far from the whole path and still
fails. The old measure (no window, lag included) is printed as `raw p90`.

Measured 2026-10-24 on the new fixtures (p90 windowed / raw), GOG 2024 lane 10 (3 runs) and GOG 2016
lane 3: cells 0.00 m; cell players 0.01 m, 0.3-0.9° (raw 0.22-0.30 m, 16.5-21.5°: the host's cell
turns every 4th round and the guest shows the turn ~0.1 s late, so one sample in eight sits at
~20°, enough for the old p90 of two players to fail on both builds); creature NPCs 0.3-0.6 m, players
0.4-0.9 m; tribe animals 0.4-0.8 m, citizens 0.4-0.6 m; civ vehicle 0.1-2.0 m, 0.9-5.8° (raw up to
2.0 m, 29°: the fixture has one vehicle, it turns in place when the test reverses it); ships
0.3-2.0 m (raw 1-11 m).

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
* `movie=1` that never goes away (no HUD, Enter does nothing, clicks lost) right after a join was
  the galaxy menu's intro cinematic GGE_Intro carried into the stage by a load from the menu
  (docs/re/space.md 7.9, fixed in world.cpp). The mod ends it now; `coop.status` prints "cinematic
  <id> playing (menu intro)" while it runs and "menu intro ended N" after. The intro starts ~16 s
  after the menu appears and runs ~10 s, so a test that joins or loads in that window meets it:
  `twoinst.start(fixture, join_in_intro=True)` starts the guest once the host is in its world and
  joins at the start of the guest's intro (deterministic; `coop_space_guest_ui.py`, ~1 min to ready,
  1.5 min in all). A host that loaded before the intro gets it in Space ~17 s after its menu (ended
  by the mod after 1 s).
* `space` fixture, Vulpica (P02460035, the open first mission "scan the wreck"): landing there shows
  ~5 tutorial captions ("Something is showing up on the ship's radar...", `movie=1`, a next arrow
  at the bottom right) on each game, and while they are up the mouse wheel does nothing, so the
  ship cannot take off. `coop_space_puppet.py` failed rounds 2-4 that way whenever its random
  planet pick was Vulpica (lab 20261023-224651, lane 8: both ships stayed on the planet, the mod was
  fine). `Player.take_off()` now confirms captions (`captions()`, Enter) before zooming out, and
  the puppet test lands on a fixed planet per round (P01, Vulpica, P03, home).
* GOG 2024 lanes: for up to ~80 s after the load, zooming in at the home planet does not land
  (measured on lane 10: the first zoom that landed came 50 s after `start()` returned, also with the
  intro fix disabled; not investigated further). `coop_space_rescue.py` retries the first landing
  for 150 s. The 2024 lanes run at ~66-69 fps, the 2016 lanes at ~31 fps: `wait.frames N` waits half
  as long on 2024.
* The galaxy menu drops every click while its intro plays (~16-26 s after the menu came up) and
  for ~2-3 s after it (docs/re/space.md 7.9). On GOG 2024 `wait.frames 700` after the menu is ~10 s,
  so `coop_ui.py` clicked "Co-op" inside the intro and the host's window never opened (lab
  20261023-225702). The mod now skips the intro (Escape) when Co-op is clicked during it and opens
  the window 3 s after; `coop_ui.py` clicks the host's item while the intro plays on purpose (checks
  "clicked during the menu intro", "the menu intro was skipped") and the guest's 4 s after its intro.
  It types the lane's port before Host: the port field shows the lane install's saved `host_port`
  (mods/coop.ini, 47810 on lane 2 because the lane was copied from lane 1), and hosting failed with
  "bind() failed on port 47810" whenever lane 1 hosted at the same time (lab runs).
* Ships on a planet fly at the height of their own zoom (docs/re/space.md 7.10): two bots that landed
  with different numbers of zoom-ins are 100-450 m apart in height, and a partner far above is not on
  the screen at all. `coop.space dist` prints `repair <m>` (the ground distance) and `alt <m>`;
  `Player.match_height()` zooms toward it step by step (one notch is ~15-30 m on GOG 2024, ~45 m on
  GOG 2016) and `Player.rescue()` stops inside 40 m of ground distance. With the screen distance
  alone (< 25 px) the host flew past the disabled guest or orbited it 60-150 m away.
  `coop_space_rescue.py` zooms the guest out 6 notches in round 1 so the rescue is always tested at
  different heights (it failed every time with the old 3D repair distance: lane 1, GOG 2024).
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

* Rivals mode (`coop_space_rivalry.py`, ~6 min on lane 6 after a 45 s start): both land on the
  fixture's home planet, the buttons of the F9 Space page are found by their caption in
  `coop.space rows` (row n = widget `sbtn<n>`) and clicked for real. Real PvP shots: the host flies
  to the guest (`Player.regroup`, < 80 m), palette "combat", key 1, clicks on the guest's point from
  `coop.targets all`; about 1 damage per hit, ~100 hits in 20 s. `coop.space rivalry` prints mode,
  own money, relations, every player's last budget and the hit counters; `coop.space rivalry shoot
  <id> <dmg>` is a hit of the own ship on a player's puppet through the hooked TakeDamage (the lethal
  hit of the test), `pol` the political ids of the puppets (FFFFFFFF = at war). Money is changed with
  `coop.res add money <n>` (local, as if earned; shared again in allies mode).

## 6.12 Space game bots (play_space.py)

* `coop.targets all` lists what a bot can fly to: in a solar system its planets (`obj P/C<PlanetID>`),
  on a planet its cities (`obj T<n>` ours, `X<n>` others), in the galaxy view the stars near the ship
  (`obj S/H<StarID>`), and partners off the screen as `far player <id> x y` (their projected point).
  `coop.space star` prints `active <PlanetID>`, the planet a click sent the ship to (a moon next to a
  planet often takes the click). Inputs per view: docs/re/space.md 9.2.
* The space fixture's ship has no interstellar drive: the galaxy view stays locked. Tests give it
  with `coop.space tool add interstellardrive` (a badge cinematic and a tutorial caption follow:
  confirm them with Enter). `space_empire` (6.15) has it, plus colonies, contacts and money.
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

* Co-op contracts (`coop_space_contracts.py`): `coop.space contracts` prints the state as
  `key=value` (state, crew, wave, ships_left, ships_up, targets, money, tool, rewards ...);
  `contracts period 0` stops the automatic offers, `limit <s>` shortens the time limit of the next
  contracts (the timeout case), `offer armada here` (a raid on the colony the host is at or the
  colony of its star) / `offer survey <n>`, `ships` prints the pointers of the armada's raid ships
  that `coop.fleet list` prints next to the ids `coop.fleet shoot` takes, `notices` the
  last notices of the feature, `money` the empire's money, `tool <hex>` whether a tool is in the
  cargo. The rows of the F9 Space page are `coop.space rows`; row n is the widget `sbtn<n>` of
  `coop.uistate` (a real click: coop_ui.Ui.click). Landing shows tutorial captions and a defended
  raid shows a badge panel: both take the clicks of the co-op window, close the window and press
  Enter first (the test's `captions()`). One run takes ~6 minutes (cold start ~45 s, armada ~90 s,
  survey ~60 s, timeout 25 s), 2026-10-23 on lane 3.

### Shared diplomacy (coop_space_diplomacy.py)
* `coop.space diplomacy list` (the empires with a name, events or a state; `list all` every one,
  60 lines at most), `near [n]` (the NPC empires nearest to the home star: `home=<StarID>` to fly
  to), `table` (records with events: `rec <pol> flags=<1 war, 2 alliance, 16 met, 32 contacted> rel=..
  <event>=<value>...`; compare two games with it), `log`, `sync`, and the game's own functions:
  `apply <pol> [event] [scale]` (default the gift event), `war|ally|peace|break|contact <pol>`;
  `local ...` does the same without sharing (tests: make the copies differ).
* The test runs on `space_empire`: it checks the fixture's two contacts in both copies, then flies
  to the two nearest empires not met yet; the host's first-contact talk is `coop.space empire
  contact <StarID hex>` (spacecoop.cpp, the same calls as the game's handler, shared by the hooks).
* A cold start under load once left one caught fault on both games at 0x11ECE4F (a null object in
  code called from 0xA0F687, a logger-like routine, outside any diplomacy code); the next cold run
  had none. `ck.faults` counts it because `exc` counts since the process started.
* The test takes ~3.5 min after a cold start of the pair (~45 s): the jumps to two empires' home
  stars take ~5 s each, the rejoin ~15 s. It passed twice in a row on lane 4 (GOG 2016).
* Use an event that stays in the map to make copies differ: CommunicatedMean 0x5F62736 applied by
  hand changed the relation once and left no event, so the tables looked equal again.
* The first contact shows a tutorial caption on the player who made it ("Drake's equation was
  right... open your Communications Panel", with OK) and the caption's input lock drops the clicks
  on the co-op window ("timeout waiting for Space page", lab 20261023-225702 lane 9 and an earlier
  run on lane 2): the test confirms captions (Enter) before its clicks.
* An alliance needs a relation of at least 3: below it the game's own periodic check breaks it in
  either copy (docs/re/space.md 12.5); with one gift the relation was 2.44 and the alliance was gone
  ~1 min later ("after the rejoin the guest is still allied" failed on both builds). The test gives
  a gift worth 3 before the alliance.
* The `diplomacy list` output of all 2120 empires is cut by the console (~16 KB): the command
  shows only the interesting ones.

## 6.12.1 The galaxy map test (coop_space_map.py, ~3.5 min on a cold lane)

* `coop.space map roster [id]` prints the rosters (`own` / `shared` colonies), `map tags` the tags
  drawn in the last frame (box and the projected star point), `map project <star>` a star's pixel,
  `map planets [star]` the stars near the ship with their planet records (tech, spice, nodes),
  `map cost <star>` the game's flight price, `map go <id>` / `map fly <star>` what a click does.
* A colony for the guest: `coop.space tool add placecolony`, land on a planet of the home system
  (Vulpica in the `space` fixture), `coop.space map colonize` (the colony tool's OnHit at the ground
  under the ship, docs/re/space.md 13.3), then confirm the drop's caption with Enter: the colony is
  in the host's roster of the guest ~14 s later. The real aiming refuses every planet of the fixture
  but the home planet ("near spice").
* The tags vanish under the pause menu (Esc in the galaxy view opens it at once) and come back with
  a second Esc.
* Runs (2026-10-23): lane 2 (GOG 2016) passed 6 times in a row, the last with the final code (~3.5 min each). Lane 1 (GOG
  2024) once: rosters, the tags in the galaxy view, hiding under the pause menu and the rejoin
  passed; the guest's part failed: after the drive's badge a caption with letterbox bars stayed on
  the guest for over a minute through Enter (`a cinematic is playing`), so it never picked the
  planet. The test now also clicks the caption's check button (1410, 1048); not run on GOG 2024 since.
* Before the rejoin the host goes back to a star system: a guest that joins while the host is in the
  galaxy view is not "together" with it, `wait_guest_in` times out.

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

## 6.15 The `space_empire` fixture (Space features beyond the tutorial)

Made 2026-10-23 on lane 8 from `space` with one command (the planet and star ids belong to the
`space` fixture's galaxy; after a new `space` fixture pick new ones, see below):

```bash
SPORE_LANE=8 python3 tools/tests/make_fixture.py space space_empire \
  --cmd "coop.space tool add interstellardrive" --cmd "coop.space tool add interstellardrive_upgrade4" \
  --cmd "coop.space tool add energyupgrade5" --cmd "coop.space tool add placecolony" \
  --cmd "cheat moreMoney" \
  --cmd "coop.space empire colony 01460035 25" --cmd "coop.space empire colony 00460014 40" \
  --cmd "coop.space empire contact 0046102D" --cmd "coop.space empire contact 00460038" \
  --cmd "coop.space spice make 20" \
  --cmd "wait.frames 300" --cmd "key enter" --cmd "wait.frames 200" --cmd "key enter" --cmd "wait.frames 200"
```

What it has (checked by the facts in fixtures.py, `coop.space empire` prints it all):
* the ship in the home system (Madas, PlanetID 00460035), the interstellar drive (the galaxy view
  opens), drive upgrade 4, energy upgrade 5, the colony tool (one charge), $1 100 000;
* two colonies besides the home planet: P01460035 (home system) and Gabar P00460014 (the tutorial's
  beacon star, now an own star `H00460014` in `coop.targets all`); after a load they hold 10 and 5
  spice (the game clamps the stored spice at load: 5 per city, the value given is lost), the home
  planet 11; visiting P01460035 shows a generated colony (several cities, a spice geyser);
* first contact with the empires 80001FCC (home star 0046102D, 5.4 galaxy units away) and
  80001F53 (00460038, 5.9), the two nearest NPC empires; no comm screen was seen, no war, no
  alliance, no missions from them;
* 20 spice of the home planet's kind in the cargo (`coop.space spice`).

What it does not have, and why:
* **The tutorial is not done**: the mission is still "scan the wreck" on Vulpica (P02460035).
  The first zoom-out to the galaxy plays the tutorial caption about the beacon (Enter). While the
  mission chain is open, galaxy clicks on stars that are not own stars show a red "Distance: 1 (pc)"
  and the ship does not go there (also with drive upgrade 4); the beacon star and own stars work. ?
  whether this is the tutorial lock or the drive range.
* **Colonies only through `coop.space empire colony`**: with the colony tool the game refused every
  spot on Vulpica ("Colonies must be placed near spice."): unvisited planets have no spice nodes;
  planets far from home get a spice kind (record +0x198) only when generated.
* **Contacts only through `coop.space empire contact`**: first contact the game's way needs a flight
  into the empire's system and a talk on the comm screen; since fix/space-jump `coop.space jumpto
  <StarID>` does that flight (docs/re/space.md 7.3, `coop_space_jump.py` makes two contacts by clicks),
  but the fixture was made before.
* No raids, no missions of other empires, no trade route, no terraforming tools.

New ids for a new `space` fixture: planets of the home system with `coop.targets all` in the
system view (`obj P<PlanetID>`); a planet of another star needs a generated record (a star near
home that the tutorial points at, or one the ship visited); the nearest NPC empires: walk
`cStarManager` +0x150 (docs/re/space.md 9.6).

## 6.16 `coop_space_allies.py` (a partner's colonies as an allied empire)

* Fixture `space`; takes ~7 min on a lane (start 40 s). Runs 2026-10-23: lane 9 (GOG 2024) 6 passes
  in a row with the last code, lane 8 (GOG 2016) 3 passes. The host founds the colony itself with
  `coop.space allies colonize` (record level, a planet of the system it is in) in a free star near
  home (`allies free`; the nearest one, 4587540, is the tutorial's ruined colony: the test takes the
  second). The colony must be founded **after** the guest joined: the guest plays a copy of the
  host's save, so earlier colonies are its own already (shown as `skipped ...: your own star system`).
* `coop.space allies star <StarID>` prints the star record as the guest's game has it (empire id,
  owner name, tech level, each planet's tech level and cCivData pol / cities); `allies empire
  <pol>`, `allies cities` (with distance and screen point of each city), `allies raids` (target,
  state, raider ships), `allies raid <PlanetID> [n]` (the game's `raid -start` on a planet of my
  galaxy), `allies fight [dmg]` (the game's damage on the raiders of my raids on mirror colonies),
  `allies fake <name> <PlanetID>...` (a mirror without a partner), `allies remove|apply|drop`.
* Captions: the fixture's system starts with an intro (the camera orbits the star, cinematic
  manager busy) that only Esc ends; an Esc while landing left the planet view black (HUD only), so
  later captions get Enter only. The interstellar drive's caption needs Enter too.
* A hot reload of coop.dll (`lab.py reload`) while a mirror is applied leaves it in the game with
  the mod no longer knowing it (the next mirror skips that star: "a star system of another empire
  here"): `coop.space allies drop` first, or restart the games.
* The test steers over the city with `allies cities` (distance and screen point; `coop.targets
  all` did not list it in the test runs).
* The guest's trip goes into a system it never visited: there the planet view stayed black after
  the landing in every test run (HUD drawn, the cities without a position: `dist -nan`), the jump bug
  of docs/known-issues.md (fixed on fix/space-jump). The same trip into a system visited before
  shows the colony city (out/shots `allies_city`, manual run). The checks read the game data.

## 6.17 The galactic core (coop_space_finale.py)

* Uses the `space` fixture; both players get the interstellar drive with `coop.space tool add
  interstellardrive`. The host zooms out with the mouse wheel (`wheel 720 540 -5`, ~6 notches),
  `coop.space finale goto near|core` flies like "Fly to" does (galaxy view: the free instant travel
  of SpaceTeleportTo; a system: the Space page jump), the wheel in at the core enters its system,
  a click at (720, 480) flies the ship into the centre and the wheel in opens the core.
* Captions: confirm a caption only after it has been up for ~6 s, alternate Enter and a click on
  the green check (1410, 1048); the core cinematic needs an Enter about every 8 s (scrolling texts)
  and takes ~2 minutes. `coop.space finale` prints ctx / busy / cin (cinematic id, 2F7C7BDA =
  SPG_CenterOfGalaxy), zone (1 far, 2 near, 3 gate), entered / staff / charges.
* The core opens once per save, so every run needs a fresh fixture (a cold start restores it; a
  warm reset reloads the fixture too). A run takes ~8-10 minutes.
* `--alone`: the host presses "Enter now"; the guest is near the core but not at the gate and gets
  Steve's gift by message.

## 6.18 `coop_space_jump.py`: jumps into unvisited systems

`SPORE_LANE=N python3 tools/tests/coop_space_jump.py` (suite `space_jump`, ~5 min with the start, the
`space_empire` fixture). It picks unvisited stars near home with `coop.space empire near [n]` (the n
nearest NPC empire stars and free stars around the active star, with the contact flags, visited and
planet count, plus a `here` line for the current star) and plays: guest system -> NPC system (planets,
active planet, empire flag 0x20, first contact through the comm by clicks), host follows with the F9
Space page "Jump there", guest lands and jumps from the planet into an empty system, host zooms out
(wheel) and jumps from the galaxy view with the page button, guest jumps from the galaxy view into a
second NPC system (second contact). It checks the loaded planets (`coop.space planets`: all of them,
`coop.targets all` only shows those on the screen), the partner rows ("in another star system" /
"in your system"), the jump counters and no faults, and prints whether both games made the same
PlanetIDs for the new stars (docs/known-issues.md).
* Captions on the way that wait for OK (6.9): SPG_FirstContact_Intercept in the first NPC system (once
  per game), the tutorial's beacon caption on the host's own zoom-out to the galaxy (the jump's own
  galaxy pass skips it, docs/re/space.md 7.3); `settle()` presses Enter while `coop.space hp` says
  `movie=1`.
* The comm clicks (1440x1080): the comm tab above the empire's card at (288, 914), the first answer at
  (720, 725) three times, Goodbye at (424, 882); the contact flag (bit 1) is set at the end of the talk.
* The old jump stays reachable for comparisons: `coop.space jumpto <StarID> force old`.
* After several hot reloads (`lab.py reload`) the co-op session of the running games was gone ("offline",
  seen once 2026-10-23): restart the games for checks on both games.

## 6.18.1 The Steam build (manual, docs/05-re-notes.md 5.1.2)

Not part of the lab: the Steam exe starts only through the Steam client. Procedure (2026-10-24,
passed): install the mod into the Steam game's `SporebinEP1` by hand (INSTALL.md section 5), set
`background_mode=1` in its `mods\sporekit.ini`, `steam steam://rungameid/24720`, `agent-ws assign
<window of steam_app_24720> sporecoop`, a GOG 2024 lane host (lane 2: instance 3, co-op port 47812)
in the creature fixture, then on the Steam game (console instance 1) `cheat coop join
127.0.0.1:47812 Steamy`: the world loads, `coop.avatar` controls the creature, `coop.status` shows the
host's NPC proxies, `exc` 0 faults on both. Remove `background_mode=1` afterwards.

## 6.19 GPU load: a GPU reset took the whole desktop down (2026-10-24)

* With up to 18 test games at once (9 lanes; the GOG 2024 lanes draw ~66 fps, the GOG 2016 ones
  ~31), one game's DXVK submit thread hung the graphics ring of the RX 7900 at 00:18 on
  2026-10-24: kernel `amdgpu: ring gfx_0.0.0 timeout ... Process SporeApp.exe ... thread
  dxvk-submit`, `Illegal opcode in command stream`, the ring and MES resets failed, then a full
  `GPU mode1 reset` with `VRAM is lost`. Every GPU client aborted: the headless test display's
  Xwayland and gnome-shell, and also the user's own GNOME Shell (the session restarted, other
  apps crashed). The mod builds no GPU commands: it is a driver / DXVK fault under load, but the
  test load caused it.
* Rule: at most 6 lanes (12 games) at once on this machine (`tools/lab.py run ... --lanes 6`),
  fewer when the user works on the desktop. `journalctl -k | grep amdgpu` shows such resets;
  `coredumpctl list` the aborted programs.

## 6.20 Native Win32 adapter fixtures

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

Saved-captain selection first clicks the visible native My Creations tab.
All Assets may expose a matching caption far outside the viewport, so finding
the name alone does not prove that its card can receive a click.
The browser also retains its window tree after closing: acknowledge opening
with the native AssetBrowser +1C active flag, never its cached control presence.

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
The later candidate-8 health/speed/teleporter routing suite also passed at
`out/windows-lab/preview6-pickups-routing8/result.json`; the loaded shadow DLLs
matched SHA256 `075ee6000269235d8a3c41572e01296b60a71e953dc70806a4d3c1ad3cc06f75`.
Its clean pair handoff is `out/windows-lab/preview6-pair8-handoff/handoff.json`.

After cleanup message routing and held initialization changed, candidate 8 passed
`out/windows-lab/preview6-pickups-routing8/result.json` for health, speed and
teleporters, covering all four pickup wire kinds. Speed also exercises the native
Talk hold and timed expiry. The same DLL passed two open-Town native Replay rounds
in `out/windows-lab/preview6-replay8/result.json`. Both loaded shadow DLL hashes
and the clean connected-menu state are recorded in
`out/windows-lab/preview6-pair8-handoff/handoff.json`.

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
