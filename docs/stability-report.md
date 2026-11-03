# Stability report for 0.6.0

A full stability, desync and multiplayer campaign run before release 0.6.0 (2026-10-23). The goal:
players can play every stage together for a long time without crashes, freezes, desyncs or broken
co-op mechanics. Everything below ran on the GOG build under Wine on one machine, invisibly (a
headless GNOME Shell, docs/06-testing.md), with up to 16 games at once.

## How it was tested

| Layer | What | Where |
|---|---|---|
| Unit tests | protocol, packets, update packs, symbols lint | `make test` |
| Lab suites | 34 two-game tests (`full`), plus bots and bad networks (`nightly`), 6-8 lanes in parallel | `tools/lab.py run full / nightly` |
| Playing bots | host and guest play one stage for 10-22 minutes through real input (mouse, keys, menus) and measure sync, shared resources, co-op mechanics and faults | `tools/tests/play_<stage>.py`, docs/stability/<stage>.md |
| 4 players | host + 3 guests from two Wine bottles, chaos bot on all, rejoins, a sync / coverage / resource check every minute | `tools/tests/coop_multi.py` |
| Long chaos | 2 players, 10 minutes of random real input per stage with frequent rejoins (churn weight 3) | `tools/tests/coop_chaos.py` |
| Bad network | rejoins and play over a UDP proxy with delay, jitter, loss and duplicates (`SPORE_NET=mobile / awful`) | `rejoin_mobile`, `rejoin_awful` |
| Focused repros | one test per bug found, kept in the suites | see "Bugs found and fixed" |

Invariants checked everywhere: no frozen game (no frame for 10 s; 60 s while a world loads), no
crashed game (console stops answering), no caught fault (`exc`), same shared food / DNA / money on
every machine, every synchronized object of the host present on each guest near it (coverage) and
its position error (90th percentile per kind).

## Results

### Playing bots (two players, real input)

| Stage | Sessions | Faults | Sync (p90) | Shared resources | Co-op mechanics seen |
|---|---|---|---|---|---|
| Cell | 4 x 10-22 min, then 6 after the prey fix | 0 | cells 0.05-0.14 m, players 0.34-0.38 m | food equal in ~500 checks | revives, respawns, healing by food, NPCs hunt both players by the same rules |
| Creature | 5 x 10 min (+4 after the social fix) | 0 | NPCs 0.5-0.7 m, players 1.1-1.6 m | DNA equal in 142 of 142 checks | joint hunts, pack rescue, guest kills pay DNA, befriending and alliances by the guest |
| Tribe | 6 x 11 min (one over `mobile`) | 0 | citizens 0.72-1.42 m, animals 0.72-0.99 m | food equal in every sample | pincer, chief aura, rescues, epic creature killed together |
| Civilization | 13 sessions, 5 of 10+ min | 0 | vehicles 0.9-1.5 m | money equal at every audit | joint sieges, captures, ruins, spice claims, gifts |
| Space | 10 x 10-12 min (2 h 10 min) | 0 | ships 1.2-8.3 m | money equal in ~2200 checks | rescue of a disabled ship, jumps to a partner, deaths and respawns |

### Four players and long chaos

Three rounds of 5 stages x (4 players for 8 minutes + 2 players for 10 minutes of chaos), each
round on the build with the fixes of the round before:

| Round | 4 players | 2-player chaos | What failed, and the fix |
|---|---|---|---|
| 1 | 3 of 5 | 2 of 5 | tribe setup fault 0xB12D32; "frozen" civ guest (a 14 s world load); the mod answered "Do you want to save your game?" and the host left the game |
| 2 | 4 of 5 | 5 of 5 | tribe guests lost NPCs after 4 minutes (snapshot chunk over the packet limit) |
| 3 | 5 of 5 | 4 of 5 | civ guest crashed on a vehicle the game had freed |
| final nightly | (not part of nightly) | 5 of 5 (10-minute runs) | play bots 4 of 5; play_creature passed after its test fix |

After round 2 the tribe 4-player run passed 3 of 3 more times. Typical numbers with 4 players:
NPC / citizen p90 0.2-1.4 m on every guest, coverage 98-100 %, 500-800 actions and 3-8 rejoins
per 8 minutes, 30 fps on all games.

### Lab suites

* `full` on 6 lanes: 32 of 34, both failures were test bugs (fixed): 34 of 34.
* Final `nightly` on 8 lanes with every fix: 50 of 52 in 31 minutes. The two failures were test
  issues: `play_creature` counted no kills (an output format change; 9 kills after the fix) and
  `coop_sync` failed once on ships near its limit (a sampling artefact, see known issues).
* Release gate on the 0.6.0 build: `smoke` 12 of 12.

## Bugs found and fixed

Crashes and freezes:

* Guests loading a received world inside the game's frame: a planet of the galaxy menu was drawn
  after it had been freed (faults at 0xF9E5EC..0xF9E6FE), and a guest could end up on a planet
  without its height map (ground 6 m low, NPCs falling through). Loads are now queued and run
  outside the frame; a guard skips such planet draws.
* A world save written while the editor opened never loaded (a guest hung in the loading screen
  for good after an evolution vote). Saves wait for mode switches and editors wait for saves.
* The host's own save broke when it left the game while a guest joined (quit message and share
  save at once).
* Tribe stage setup with a null simulator camera (0xB12D32), now skipped.
* Space: a respawn caption ending after the ship left the planet faulted (0x7CFCC0), and the host
  crashed driving a partner's ship puppet the game had freed.
* Civilization: guests drove vehicles the game had freed (crash and freeze).
* NPC proxies destroyed by the guest's own game were still driven (and never made again).
* Creature: a guest fault at 0xD48634 in the avatar event handler (puppets have no tuning).

Desyncs and lost state:

* Tribe and Civilization snapshots grew over the 1200-byte packet limit on busy planets and were
  dropped every cycle (the guest's tribe emptied); chunks are sized by bytes now.
* NPC proxies spawned with the guest's own clock: states could all be dropped as old.
* Cell: the host destroyed cells nobody had eaten (about 1300 false reports per 10 minutes),
  guest mirrors flew off to 1e18 m, the partner's cell blinked.
* Creature and Tribe: bodies flew 80 m into the air (the hit vector is a push).
* Civilization: NPC cities grew on guests, money from guests' derricks was counted twice (about
  8000 per 10 minutes), siege progress, spice claims, ruins and new cities did not reach guests.
* Creature: a guest's kill paid no DNA, NPCs vanished on guests while the host's game was paused,
  guest goal cards said species were extinct.

Gameplay:

* The mod confirmed any message box with the "game saved" box's id for 30 s after a save,
  including "Do you want to save your game?": the host left to the main menu or quit Spore. Now
  only the saved box's OK button is pressed, once.
* Cell: host NPCs hunted only the host (the guest's cell kept its starting size on the host).
* Creature: guests can befriend NPCs and make alliances through the host; the host leaving to the
  main menu is announced and guests play on; name labels hide under menus.
* Tribe: guest gather orders on fruit trees were always rejected; gifted members stayed
  selectable by the giver; a knocked-out chief stayed a co-op chief.
* Space: a guest next to a partner was destroyed instead of disabled in real fights; partners'
  ship puppets fired on their own.
* Test infrastructure: game windows overlapped (covered games lag), the test display could fall
  back to the user's desktop, stopping it unmounted the Flatpak document portal, the update test
  could wipe another lane.

## Open issues

docs/known-issues.md lists them with reproductions. The ones a player can meet:

* Civilization: the host's capture cinematic stops everybody's game until the host presses OK
  (everybody is told why).
* Cell: a guest more than ~100 m from the host swims in an empty pool (NPCs exist around the host).
* Creature: the guest's social game is simplified (no native meter; any social ability counts).
* Tribe: knocked-out members stand on the guest; adding a member during the game-over caption
  freezes the host (only reachable by a test command today).
* Seen once each, not reproduced since: a heap corruption crash in creature chaos, one NPC 4 m off
  on every guest, a host freeze while an epic was killed.

## Running it again

```
tools/lab.py run nightly --lanes 6                               # everything; the lab caps at 6 lanes (GPU load, SPORE_MAX_LANES raises it), ~35 min was on 8
SPORE_LANE=1 SPORE_LANE_B=8 python3 tools/tests/coop_multi.py --fixture tribe --players 4 --minutes 8
SPORE_LANE=5 python3 tools/tests/coop_chaos.py --fixture civ_npc --minutes 10 --churn 3
SPORE_LANE=2 python3 tools/tests/play_cell.py --minutes 10 --seed 8
```
