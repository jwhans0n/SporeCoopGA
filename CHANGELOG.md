# Changelog

## [Unreleased]

## [0.7.3.4-ga-preview.11] - 2026-10-25

- Fix authored player-team enemy targets rejecting eligible guest captains.
  Remote puppets keep their native identity; a narrow host-side match recognizes
  living party captains while preserving native sensing, hostility, authored
  nonparty targets and one normal AI tick.
- Keep native sensor candidate lists and ownership unchanged. The initial
  perception-record workaround did not repair the actual authored target check
  and is excluded from the minimal fix.
- Use protocol 14 for matching native targeting behavior. Actor packets,
  creation bundles, native waypoint followers and static-object handling retain
  their existing formats and behavior.
- Pass targeted two-player **Defend the Crystal Mine** checks: native guest and
  host targeting, natural damage, and guest/host/guest target switching. See the
  [preview 11 report](docs/validation-ga-preview.11.md). Broader compatibility,
  residual movement lag and ordinary guest click/key combat input remain limits.

## [0.7.3.3-ga-preview.10] - 2026-10-25

- Follow replicated host creature and vehicle positions with native waypoints using native locomotion between
  network samples, with bounded prediction and catch-up. Reserve hard pose
  corrections for initial binding, severe divergence, stuck movement and
  confirmed stationary disagreement.
- Keep NPC AI, combat, health and objectives host-authoritative. Preserve native
  attachment behavior, existing attack/unknown clip handling, party holds and
  run/act/incarnation resets.
- Use protocol 13 so all participants run matching follower behavior. Actor
  packet layouts, creation bundles and static-object optimization are unchanged.
- See [preview 10 validation](docs/validation-ga-preview.10.md) for evidence and
  limits, including residual tracking lag. Ordinary guest combat input remains
  unverified.

## [0.7.3.2-ga-preview.9] - 2026-10-25

- Keep client NPC visual updates running while the host owns NPC AI and damage.
  Share host NPC visual clip requests without replaying their gameplay effects.
- Pause shared adventures only when all connected participants are paused.
  One player resuming releases the pause; scene confirmation holds stay separate.
  Retain queued damage, item/throw and social actions through the hold and
  revalidate them once on Resume.
- Increase teammate revival distance from two to six metres, including its ring
  and instructions. Keep the three-second duration and interruption rules.
- Use protocol 12 for run-scoped pause and NPC animation state. Resource bundle
  formats and the existing static-object optimization remain unchanged.
- See [preview 9 validation](docs/validation-ga-preview.9.md) for evidence and limits.

## [0.7.3.1-ga-preview.8] - 2026-10-25

- Resolve act music through native audio descriptors and inherited sample
  references, including The Eighth Day's music. Preserve authored IDs and reject
  unsupported custom audio overrides instead of adding per-adventure allowlists.
- Route guest item giving through native NPC/container receive behavior and share
  complete held-item attachments, including NPC tools and replacement bindings.
- Synchronize ordinary handheld throws and host-validated catches. Timed bombs
  retain their existing fuse; contact grenades keep native impact behavior.
- Show group beam-down/conversation confirmations in the native bottom black bar
  beside its check mark. Add a two-metre revive ring, progress bar and timer.
- Fence downed movement and orders while retaining native visual updates. Keep
  adventure lighting local instead of copying the host's planet orientation;
  shared gameplay clocks remain authoritative.
- Use protocol 11 for item attachment and throw/catch messages; all players need
  this preview. Adventure/captain resource bundle formats remain unchanged.
- Record targeted two-player native acceptance and four-player beam-down UI
  checks in [the preview 8 report](docs/validation-ga-preview.8.md), with remaining
  coverage and delivery details.

## [0.7.3.0-ga-preview.7] - 2026-10-24

Fork integration of upstream SporeCoop 0.7.2.1 and Galactic Adventures previews 1-6.
The fork is maintained at [jwhans0n/SporeCoopGA](https://github.com/jwhans0n/SporeCoopGA),
with the original SporeCoop project and its authors credited below and in README.md.

- Retain upstream SporeKit 0.3.4 diagnostics, crash/hang reports, Steam DRM and
  frame-loop support, and the existing non-adventure stages.
- Include shared Sporepedia adventure preparation, saved captain creation/transfer,
  explicit party readiness, group scenes, Replay, actor/combat/objective replication,
  static-object traffic reduction and shared native interactions from preview 6.
- Use protocol 10: every participant needs this fork build. Upstream 0.7.0-0.7.2.1
  uses a different protocol and cannot join this preview.
- Keep the native adventure adapter restricted to exact GOG executable SHA-256
  `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`.
  Upstream Steam and same-code recognition remains available for other stages;
  it does not establish Steam Galactic Adventures support.
- Direct fork downloads, update checks and new problem reports to SporeCoopGA.
  No tag or release is created by this integration.

### Earlier Galactic Adventures previews

### Shared static objects and interactions: 0.7.0-ga-preview.6

- Send verified stationary buildings and props once, then send reliable changed
  health/state. Continue observing movement and promote moved, carried and thrown
  objects to spatial replication. Creature and vehicle update rates stay unchanged.
- Let the host own native mine, barrel, bomb and grenade activation, delayed
  chains, damage and removal. Treat their explosions as environmental hazards
  regardless of friendly fire. Preserve harmless crate behavior and native fuses.
- Keep initial powerup spawning during group holds. Allow a timed bomb to be
  picked up and thrown again while its original fuse continues.
- Share pickup consumption and authored respawns. Apply health, speed, damage and
  shield upgrades only to the collecting captain, with shared simulation expiry.
- Validate authored teleport exits per captain, synchronize native cleanup and
  cooldowns, and reject movement from before the teleport correction.
- Preserve preparation message order during Replay cleanup so new content and
  captain transfers reach the new run, including open-captain adventures.
- Preserve shared keys/gates, saved captain creation, group scenes, Replay and the
  existing performance fix. Protocol 10 requires the same preview on every player;
  adventure/captain bundle formats and asset caching remain unchanged.

### Adventure party flow: 0.7.0-ga-preview.5

- Require every participant to confirm beam-down Continue before gameplay and
  adventure clocks advance. Show who is still confirming.
- Present objective conversations to the whole party, retain independent dialog
  pages and complete the objective only after everyone presses Finished.
- Route native Replay back through the shared lobby with saved captains and
  everyone unready. Any participant may request it after accepting results.
- Render remote captains from the current validated selection and copy their
  owner's actual scale. Reject old run/selection packets and rebuild stale models.
- Validate shared colored keys and gates on the host, synchronize native opening
  and key consumption, and reconstruct the correct open gate after replica loss.
- Retain preparation recovery and the large-adventure FPS fix. Protocol 9 requires
  this preview on every participant; resource bundle formats remain unchanged.

### Shared adventure lobby: 0.7.0-ga-preview.4

- Accept native music event 1 used by Mr Puzzle's Magical Gates, preserving the
  authored setting and retaining validation of unsupported custom dependencies.
- Show the adventure artwork, title and author with four shared saved-captain
  cards, native portraits, saved captain names, transfer progress and readiness.
- Require explicit Ready from the host and every guest. Captain replacement
  clears party readiness; cancelling replacement keeps the saved captain.
- Synchronize preparation with run/revision/selection guards. Protocol 8 requires
  the same preview on every participant. Keep preview 3 recovery and the FPS fix.

### Galactic Adventures captain recovery: 0.7.0-ga-preview.3

- Guests use native captain creation, naming, review and saving before sharing
  their captain. Existing saved captains and authored captain restrictions remain.
- Gate early loading animations until the native captain save completes. Readiness
  reflects validated captain preparation and required transfer acknowledgements.
- Cancelled preparation closes owned native pickers and restores both galaxy
  application state and game mode. Clear pending callbacks and readiness on
  cancellation/disconnect so the same adventure can be retried without restarting.
- Retain preview 2's large-adventure performance fix and the existing wire format.

### Galactic Adventures performance fix: 0.7.0-ga-preview.2

- Batch memory-region validation during read-only actor traversals instead of
  querying Windows for every field of every actor. Mapping checks remain fresh
  between traversals, native mutations and frames.
- Remove the guest's duplicate per-frame actor enumeration and keep native
  result bookkeeping out of frequently called eligibility queries.
- Add a two-session, 400-plus-actor FPS regression and native tests for guarded,
  inaccessible and released memory. Network format and friendly-fire rules are
  unchanged from preview 1.

### Galactic Adventures preview: 0.7.0-ga-preview.1

- Start a party adventure through the host's normal Sporepedia Play button, choose
  personal captains, transfer supported missing adventure/captain assets, and wait
  for every participant to finish loading before play begins.
- Share host-authoritative actors, combat, objectives and act changes. Configure
  enemy health/damage, friendly fire, teammate revival, automatic recovery or
  failure on any death in Adventure settings.
- Retain each player's local captain for native XP, progression and saving while
  suppressing co-op submissions to the single-player adventure leaderboard.
- Two isolated native Windows instances verified launch, guest objectives, actor
  cleanup, friendly-fire settings, death/revival rules, native success/failure UI,
  and captain progression persisting across restart. This is not a complete
  adventure playthrough or four-player validation.
- The native GA adapter is verified only for GOG March 2017 executable SHA-256
  `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`.
  Other stages on that executable remain unvalidated. See
  [the preview guide](docs/GALACTIC-ADVENTURES.md) for setup and remaining limits.

## [0.7.2.1] - 2026-10-24

Finding problems: crash reports ready to send, kept logs, the version in the game. 0.7.2.1 plays
together with 0.7.0-0.7.2 (the network protocol did not change).

### Finding problems
- **Crash reports, ready to send:** when the game crashes, freezes for 30 s or closes right after an
  error, SporeCoop packs `mods\logs\SporeCoop-report-<date-time>.zip` by itself (the crash report:
  what crashed, where, the call chain, the co-op session, other overlays and mods; a minidump; the
  logs; the settings), and the next start of the game tells the player its name.
- **Logs are kept:** the logs of the last 10 runs stay in `mods\logs\previous\` (restarting the game
  after a crash used to overwrite the crash's log).
- **"Report a problem"** in the co-op window (F9) packs everything into one file to send,
  `mods\logs\SporeCoop-report-<date-time>.zip` (the report, this run's and the last runs' logs, the
  newest crash reports with minidumps, the mod settings with the last joined address hidden), and
  opens its folder with the file selected.
- **The version in the game:** under the Co-op item of the main menu and at the bottom of the co-op
  window (with the game build: GOG 2024, Steam, ...).
- The log starts with the environment: Windows or Wine, memory and the 4GB flag, the graphics card,
  and other programs that hook the game (Steam / Discord / NVIDIA overlays, RivaTuner, ReShade, ModAPI).
- A fault inside SporeCoop's own code is told in the game once, with where the report is.
- SporeKit 0.3.4: `env`, `report`, `crash.test` console commands; mod API `kit_version`, `build_name`,
  `diag_report`, `diag_context`, `logs_dir`, event `SK_EV_FAULT` (docs/02-modkit.md 2.7).
- Tests: `diag_check.py` (suite `diag`), `coop_creature_death.py` (dying alone and skipping the
  respawn cutscene, a player report that did not reproduce) with the `coop.team die` test command.

## [0.7.2] - 2026-10-24

**No Co-op item on Windows with the Steam overlay** ([issue #4](https://github.com/WoodyWizard/SporeCoop/issues/4)).
The mod loaded, but after the menu came up SporeKit got no frames from the game: another overlay
(most likely the Steam overlay while Steam is online) took over the game's `Present` call, which
is where SporeKit's frame begins. Everything the mod does each frame stopped: no Co-op item in the
menu, F9 did nothing, no network. The log shows it as `WATCHDOG (no frames, ...)` while the game
still draws.

- SporeKit 0.3.3 has a second frame source: the game loop (`App::Canvas::Tick`). When `Present`
  has not reached SporeKit for 2 s while the game window has the focus and is not minimized, the
  frames come from the game loop, and back from `Present` when it returns. The log says
  `frames: Present has not reached SporeKit ...`. In that mode the mod's on-screen labels (names
  above players, notes drawn over the picture) are off; the menu item, the co-op window, F9 and
  the whole co-op work.
- Tested on Windows 11 with the Steam version (offline mode) and `[sporekit] frames=loop`, which
  ignores `Present` the way such an overlay does: the Co-op item appears, F9 opens the window, and a
  Steam guest in that mode played with a GOG 2024 host in the Creature stage (world transfer,
  puppets, NPC damage through the host, shared DNA), no faults. In the normal mode loads of saves
  (Creature, Space) and stage changes never switched to the game loop. Not tested with the Steam
  overlay online (the case of the issue): please report how it goes.
- First test of the Steam version on Windows 11 (0.7.1 and 0.7.2): every feature on, Steam and GOG
  players together.
- `sporekit.ini`: `background_mode` was under `[mods]` in the template (no effect when uncommented);
  new `frames=auto|loop`. Console: `frames.source [auto|loop]`; `info` names the frame source.
- `tools/tests/coop_frames_loop.py` (smoke): co-op with the guest's frames from the game loop.

## [0.7.1] - 2026-10-24

**The Steam version works.** Steam's `SporeApp.exe` (since its October 2024 update) is the GOG 2024
game wrapped in Steam's DRM: the game code is encrypted in the file and unpacked when the game
starts, so SporeCoop found no addresses for it and turned co-op off ("no symbol database for this
SporeApp.exe build", [issue #4](https://github.com/WoodyWizard/SporeCoop/issues/4)). Now SporeKit
recognises the exe by its data, waits for the unpacked code, checks that it is exactly the GOG 2024
code and then uses the GOG 2024 addresses. Steam and GOG players can play together.

- Tested on Linux with Steam (offline mode) and Proton: every feature on, a Steam guest joined a GOG
  2024 host in the Creature stage (world transfer, own creature, the host's NPCs), no faults. Not
  tested yet on Windows.
- Install as for GOG: into the Steam game's `SporebinEP1` (`SporeCoopSetup.exe` and `install.bat`
  now name the Steam build). 0.7.0 and 0.7.1 can play together (the network protocol did not change).
- SporeKit 0.3.2: `data_sha256` / `text_sha256` in symbol files, the late boot for packed exes,
  `[sporekit] defer_symbols=1` for tests; `tools/exe_info.py` prints both hashes.

## [0.7.0] - 2026-10-23

"A galaxy together": the Space stage becomes a game of two (to four) rulers of one galaxy who see
each other, help each other, trade and finish the game together. Everything is on the new
**Space** page of the co-op window (F9 in the Space stage, the "Space >" button).

**0.7 cannot play with 0.6.x** (the network protocol changed): every player needs 0.7.0.

**0.7.x is the last version for the old GOG 2016 build** (`SporeApp.exe` `922af7c7…`). From 0.8 only
the current GOG build (the 2024 update, `dc04aee5…`, the code of the March 2017 patch that Spore
ModAPI also targets) is supported: update the game through GOG.

### Space: new
- **Partners on the galaxy map**: a name tag in the partner's colour on their star (or on their
  ship while they fly in the galaxy view), an arrow at the screen edge when they are out of view;
  a click on the tag flies you there. The Space page lists where everybody is ("Bob: at Fibucius
  [Fly there]").
- **Your own territories**: the stars with a partner's own colonies get a ring in the partner's
  colour, and the Space page lists them one by one with "Fly there".
- **Partner colonies as an allied empire**: a partner's colonies founded after you joined exist in
  your galaxy too, as the cities of "Alice's empire", an ally you have already met. You can fly
  there, land at their city, and fight off a raid on that colony in your own game: winning it
  counts as defended in the owner's game. The mirrored empire never reaches your save.
- **Tool gifts**: give charges of consumable tools (bombs, terraforming charges, the colony tool,
  monoliths, colony boosters and turrets, fireworks ...) to a partner. Permanent tools (weapons,
  drives) stay with their owner.
- **Trade between players**: at a star with a partner's colony, sell spice to it for twice the
  market value (an export bonus paid by the galaxy into the shared budget, up to 30 units per
  colony every 10 minutes); the spice arrives in the partner's cargo.
- **Co-op contracts**: every 10 minutes the host's game offers a job made for two: a **pirate
  armada** that attacks one of the host's colonies only while two ships of the crew are there
  (two waves of tougher raiders), or a **joint survey** of 2-3 nearby stars that the whole crew
  has to visit together. Money and a tool for everybody in the crew.
- **Shared diplomacy**: the players are one people for the NPC empires. A first contact, a gift,
  a mission, a trade route, an attack, an alliance, a war or a peace by one player changes every
  player's galaxy the same way, with a notice. The host's relation table keeps all copies equal.
- **The galactic core together**: partners are told when someone reaches the Grox zone or the
  core, and can fly to a partner who is there. The core waits until everybody in Space has
  arrived (or "Enter now"); then everybody sees the ending and gets the Staff of Life. A partner
  who was not there gets the gift too, also when joining later.
- **Rivals mode** (a host option): every player becomes a separate empire with an own budget (the
  shared money is split; switching back adds them up). Players can propose alliances, declare war
  and make peace on the Space page; at war, ships really damage each other (with the game's own
  death and respawn), and an enemy never repairs your ship.

### Fixes
- A guest who joined (or a host who loaded) in the first ~16-26 s after the galaxy menu came up
  could be stuck without a HUD in every stage: the menu's intro cinematic was left playing by the
  world load and took every click. The mod now ends it.
- The jump to a partner (and every co-op jump) into a star system nobody had visited arrived
  without planets and without first contact. It now travels like a player through the galaxy view:
  planets load, empires meet you.
- Two games generated different planets for the same unvisited star (rarer now, see known issues).
- A jump to an id that is not a star of the galaxy crashed the game (GetStarRecord).
- The Space page and the jump buttons handle many rows (pages with "<" and ">").
- Rescue on a planet: a partner flying over your disabled ship at another zoom height could not
  repair it (the distance was measured in 3D, and a ship's height on a planet is its zoom). The
  repair distance is now measured along the ground.
- A click on the main menu's **Co-op** item while the galaxy menu's intro plays (about 16-30 s
  after the menu appears) did nothing: the mod now skips the intro (like Esc) and opens the window.
- LAN discovery asks 20 ports (it found hosts on ports 47810-47819 only).

### Testing
- New two-game tests for every feature (`coop_space_map`, `_allies`, `_trade`, `_contracts`,
  `_diplomacy`, `_finale`, `_rivalry`, `_jump`, `_guest_ui`, `_page`), all with real clicks on the
  Space page, on both supported builds (GOG 2016 lanes and the new GOG 2024 lanes 9 and 10 under
  Wine). All fixtures were made anew for the GOG 2024 install, plus `space_empire` (drive,
  colonies, empire contacts).
- `tools/re_lint.py` checks every symbol the code uses against every supported build.
- Release gate: the full suite (45 tests) on 6 lanes, three GOG 2024 and three GOG 2016: 43 of
  45, and the two others (`space_rivalry`, `space_puppet`: the GOG 2024 landing delay after a
  load, now retried by the tests) passed 4 of 4 on GOG 2024 lanes afterwards.

### Known issues
- On GOG 2024, zooming in to land on a planet can do nothing for up to ~80 s right after a world
  load; then it works.
- Two games can still generate different planets for a star nobody visited (rarely).
- While a game panel that waits for OK is open (for example the badge points after a raid), the
  co-op window gets no clicks: press Enter or OK first.
- Details and more: [docs/known-issues.md](docs/known-issues.md).


## [0.6.2] - 2026-10-23

Builds with the same code as a supported one, and a list of where SporeCoop has been tested.
0.6.0, 0.6.1 and 0.6.2 can play together (the network protocol did not change); all players need
the same game build.

### Where SporeCoop has been tested

| Game build | System | Tested |
|---|---|---|
| GOG 2016 (`SporeApp.exe` SHA-256 `922af7c7…`) | Linux, Wine / Bottles | Yes: developed on it; every stage, 2-4 players, the stability runs of 0.6.0 |
| GOG 2024, the current GOG download (`dc04aee5…`) | Windows 11 | Yes (0.6.1): every stage as host + guest, two game instances on one PC |
| GOG 2024 with a changed header (same code) | Windows 11 | Only with a modified copy of the GOG 2024 exe (0.6.2) |
| GOG 2016 | Windows | Not tested (0.6.0 and older could not work on Windows, fixed in 0.6.1) |
| GOG 2024 | Linux, Wine | Not tested |
| Steam, EA app / Origin, disc | any | Not tested: co-op stays off unless the exe's code matches a build above |
| Two different PCs over the internet or a VPN | Windows | Not tested with 0.6.1+ on Windows |

The same list, kept up to date: [INSTALL.md, section 2](https://github.com/WoodyWizard/SporeCoop/blob/main/docs/INSTALL.md#where-sporecoop-has-been-tested).
Tried another build? An [issue](https://github.com/WoodyWizard/SporeCoop/issues) with
`mods\logs\sporekit.log` (it names the exe's `sha256` and `code_sha256`) helps.

### Game builds
- A `SporeApp.exe` that differs from a supported build only in the PE header, the resources or a
  signature now gets that build's addresses: symbol files carry a `code_sha256` (the code and
  data sections and their addresses, `tools/exe_info.py`), and SporeKit uses it when no file
  names the exact exe. Such a player counts as that build when joining. Tested with a modified
  copy of the GOG 2024 exe (loaded at another base, joined a GOG 2024 host); a copy with one
  changed code byte is still refused.
- SporeCoopSetup tells such a build apart ("same code as the GOG 2024 build").
- INSTALL.md lists which game builds and systems SporeCoop has been tested on.
- SporeKit 0.3.1: `SkHost::symbols_build`.

## [0.6.1] - 2026-10-23

Support for the current GOG build of Spore, and a fix that made the mod work on Windows at all.
Tested on Windows 11 with the GOG 2024 build: host and guest (two game instances on one PC) played
every stage: cell, creature, tribe, civilization and space (joining, world transfer, puppets,
NPC damage through the host, shared DNA / money / food, civ purchases, buildings and orders, tribe
orders, ship damage, saving), with no faults.

If you installed 0.6.0 on the current GOG build, the Co-op item was missing and
`mods\logs\sporekit.log` said "no symbol database for this SporeApp.exe build". Install 0.6.1 with
`SporeCoopSetup.exe` or `install.bat` (the in-game update cannot start there, because the mod was
off). 0.6.0 and 0.6.1 can play together (the network protocol did not change), but all players
need the same game build.

### The current GOG build and Windows
- The current GOG download of the SPORE Collection (the 2024 update, `SporeApp.exe` 24 895 536
  bytes, SHA-256 `dc04aee5…`) is supported: it has its own symbol files
  (`march2017_dc04aee5.sym`, `overrides_dc04aee5.sym`). Before, SporeKit found no symbols for it
  and the mod turned itself off. Players of one game still need the same build.
- Windows: every game address was off by the exe's relocation when Windows loaded the exe at
  another base (ASLR, e.g. 0x410000). SporeKit now reads the exe's preferred base from the file,
  not from the header in memory, which Windows rewrites. Wine does not move the exe.
- The last hard-coded game addresses in the mod (vehicle, citizen, tree and drawable vtables, the
  civ growth and city planner return addresses, the allocator) are symbols now.
- `tools/build_windows.sh` builds the DLLs on Windows (Git Bash + llvm-mingw, no make).

## [0.6.0] - 2026-10-23

Installer, updates from GitHub, and a full stability campaign: bots played every stage for hours,
4-player sessions with rejoins, long chaos runs and bad networks. About 40 bugs found and fixed;
the numbers are in [docs/stability-report.md](https://github.com/WoodyWizard/SporeCoop/blob/main/docs/stability-report.md).

All players need 0.6.0 (the network protocol changed; 0.5.0 cannot join).

### Install and update
- `SporeCoopSetup.exe`: finds the game, tells whether the build is the tested one, installs,
  updates and uninstalls with one button; downloads the newest release from GitHub (or uses the
  update pack next to it, offline).
- Updates in the game: SporeCoop checks this repository's releases at the start; the Co-op window
  shows an Update button; files are checked against the release's SHA-256 sums and installed for
  the next start (`[update] check=0` in `mods\coop.ini` turns the check off).
- Releases also carry `SporeCoop-<version>.pack` (the update) and `SporeCoopSetup.exe`.

### Crashes and freezes fixed
- A guest loading the host's world could crash in the galaxy menu's planet view, or come up on a
  planet without its height map (ground 6 m low, animals falling through). Loads now always run
  outside the game's frame.
- A guest could hang in the loading screen for good when it left to the main menu during an
  evolution vote (a save written while the editor opened); saves and editors now wait for each
  other. The host's own save no longer breaks when it leaves the game while a guest joins.
- Tribe: a rare fault when the stage was set up right after a load.
- Space: a fault when the respawn caption ended after the ship had left the planet; the host could
  crash when the game removed a partner's ship.
- Civilization: a guest could crash and freeze on a vehicle the game had removed.
- Creature: a guest fault in the creature event handler.

### Sync fixes
- Busy Tribe and Civilization planets (24+ citizens, many vehicles) no longer lose their state on
  guests: snapshots were larger than one packet and dropped every time.
- Animals stand where the host has them on every guest (some proxies were never updated).
- Cell: the host no longer destroys cells nobody ate; mirrored cells no longer fly away; the
  partner's cell no longer blinks.
- Creature and Tribe: killed animals no longer fly into the sky.
- Civilization: NPC cities, captures, ruins, spice claims and new cities match on every machine;
  money from derricks is no longer counted twice.
- Creature: a guest's kill pays DNA; guests keep the animals while the host's game is paused;
  goal cards no longer call species extinct.

### Gameplay fixes
- The host no longer leaves the game by itself: the mod used to answer "Do you want to save your
  game?" for 30 s after each save. Only the "game saved" box is confirmed now.
- Cell: the host's creatures hunt every player, not only the host.
- Creature: guests can befriend animals and make alliances; when the host goes to the main menu,
  guests are told and play on; name labels hide under menus.
- Tribe: guests can gather from fruit trees; gifted members can no longer be ordered by the giver;
  a knocked-out chief is replaced.
- Space: a disabled guest ship can be repaired in real fights; partners' ships no longer fire on
  their own.

### Testing
- Bots that play each stage through real input (`tools/tests/play_*.py`), 4-player sessions
  (`coop_multi.py`), longer chaos runs, regression tests for every fix; tests run on an invisible
  display. See [docs/06-testing.md](https://github.com/WoodyWizard/SporeCoop/blob/main/docs/06-testing.md).

### Known issues
See [docs/known-issues.md](https://github.com/WoodyWizard/SporeCoop/blob/main/docs/known-issues.md). The ones players can meet: the
host's capture cinematic in Civilization pauses everybody until the host presses OK; in Cell a
guest far from the host swims in an empty pool; the guest's social game in Creature is simplified.

## [0.5.0] - 2026-10-23

The first public release: co-op in every stage of Spore, with a shared world, everybody's own
creations and mechanics that only work together.

Tested with the GOG "SPORE Collection" build (`SporebinEP1\SporeApp.exe`, SHA-256 `922af7c7…`).
Install: [docs/INSTALL.md](https://github.com/WoodyWizard/SporeCoop/blob/main/docs/INSTALL.md) - extract the zip into `SporebinEP1`, run
`install.bat`. All players need this same version.

### Playing together
- **Co-op** item in Spore's main menu: host or join (LAN discovery, address, VPN), lobby with
  players, stage, ping, readiness and chat; **F9** in game. Notifications slide in as native panels.
- One world in every stage: the host simulates, guests mirror it and act through the host; the
  host's world is sent to guests and loaded automatically, including after evolving.
- Evolution by vote: everybody presses *I'm ready* before the editor or the next stage.
- Own creations: every player plays their own creature, cell and ship from *My Creations*.

### Stages
- **Cell: Symbiosis:** shared tide pool and food; a knocked-out cell is revived by a partner
  right next to it, otherwise it respawns small next to the partner.
- **Creature: Pack:** joint hunting +25 %, the pack saves a dying player, healing near a partner.
- **Tribe: Chiefs:** members split between players, a chief per player with an aura, pincer
  attacks +50 %, duet performances, healer food, epic creatures at the village, gifts of members.
- **Civilization: Governors:** own cities and vehicles, military / economic / religious roles,
  joint sieges, city alarms with beacons, superweapons by vote, trade and gifts between players.
- **Space: Squadron:** shared enemies, rescue of a disabled ship, jump to a partner's star, raid
  alerts, mission reward tools for partners, terraforming together, spice gifts.

### Fixed in this release
- Loading a world while the Space stage ran crashed the game (every guest rejoin in Space); loads
  now leave to the galaxy menu first.
- The host's "game saved" box could stay open and pause the host.
- The Co-op menu item could hang the game after several returns to the galaxy menu.
- Cell teleports and respawns landed metres away.
- World transfer is about 3× faster (128-packet acknowledgement window).

### Known issues
See [docs/known-issues.md](https://github.com/WoodyWizard/SporeCoop/blob/main/docs/known-issues.md): a rejoin over a very lossy connection can take
long; the guest's *Done* button in its own editor does nothing (*Cancel* works).

## [0.2.0] - 2026-10-21 (not published)

Co-op presence in every game mode: other players visible in Cell, Creature, Tribe, Civilization,
Space and Adventures, shared DNA, food and money, host world transfer.
