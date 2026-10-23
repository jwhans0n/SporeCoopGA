# Changelog

## [Unreleased]

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
