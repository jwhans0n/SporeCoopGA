# Native Windows qualification adapter

This tooling continues BUG-1259 (renumbered BUG-1259 during 0.7.15 integration)
and the startup investigation BUG-1031 (BUG-1276). It does not qualify a payload
by itself. Both tickets stay open until their real native acceptance checks pass.

## Explicit lane and original assertions

`tools/lab.py run smoke --no-build --windows-config <absolute-config.json>`
runs the original `tools/tests/suite.py` smoke cases, in fresh case processes,
through `tools/windows_native_suite.py`. No replacement test stubs are used.
The original Checks, invariant monitors, fixture facts and log gates stay active.
The real repository `make test` command is an explicit argv adapter in the config.
The native path deliberately runs serially, including exclusive LAN discovery.

Each case waits until its supervisor owns a Windows kill-on-close Job before
starting games. Timeout and interruption cleanup close that Job, rather than
finding processes by image name or trusting a later reused PID. A five-second
post-check window tests owned process/console liveness and new crash/hang reports.
Skipped cases, missing result lines, gate errors, crashes and missing receipts
cannot pass. Case logs, report copies and source/payload identities are retained.

Schema 1 config fields are `id`, `installation`, `output`, `fixtures`, `build`,
`symbols`, `source_commit`, `payload_hashes`, `symbol_hashes`, `fixture_hashes`,
`host`, `guest`, `port`, `viewport`, `screen_size`, and `unit_command`.
Paths are absolute. Installation and output must be private children of this
checkout's `out/windows-lab`. Payload hashes name `steam_api.dll`, `sporekit.dll`,
`coop.dll`, `SporeCoop.package`; every deployed `.sym` is pinned separately.
The config binds the tested source commit, not merely a mutable version string.
`windows_lab.prepare(..., symbols_source=...)` accepts the matching build's
symbol directory explicitly, so a tooling worktree cannot silently deploy its
older maps beside a newer DLL.

Console identities (for example 49/50) are independent of the discoverable UDP
slot (47810..47818). Private profile names are derived from the config identity
and role, and carry a checked ownership marker. Existing unmarked profiles,
unrelated profile paths, junctions and unverified hashes are rejected.

Campaign scripts require an observed native 1440x1080 viewport; adventure/editor
scripts use 1024x768. `screen_size` is a measured native preference ordinal,
never evidence of actual dimensions. A game must report the selected dimensions
through `coop.uistate` before fixed-pixel test input is allowed. Establish a
campaign ordinal with one hidden game before using a two-game lane; a renderer
reset while two hidden games are active is not a qualified bootstrap strategy.

## Minimum fixtures and cold provisioning

The 14 original smoke cases use exactly five stock-based archives: `cell.tgz`,
`creature.tgz`, `tribe.tgz`, `civ.tgz`, and `space.tgz`. Their actual bootstrap
calls were checked, including `space_play`, `space_page` and `chaos_tribe`.
`space_empire.tgz` belongs to cases selecting that enriched fixture. It is not a
prerequisite of the original smoke selection.

Stock smoke does not select saved custom creations; verified Maxis defaults
resolve from shipped packages. Role creation packages are real additional
dependencies only when the selected fixture or creation/import/replacement
scenario requires them. No empty package is fabricated. A cold profile must
prove native model/dependency loading; an existing cache is not that evidence.
`windows_smoke_preflight.py --tests ...` reports these selected dependencies.
READY means prerequisites only, and never means native smoke PASS.

Provision new stage worlds through the real `new_stage_game.py` stage chooser,
using the existing named planets from `fixtures.py`. Perform native Save, close
the game, then `WindowsLane.seal` the private Games tree. Sealing records hashes
and source identity but leaves `qualified=false`. Cold reopen the archive in a
different empty private profile, assert the declared stage/avatar/world facts
and visible loaded models, then admit its hash to the lane input manifest.
Creature/Tribe stock asset choices, Civilization city construction and Space
home-system prerequisites must be verified natively, not inferred from a file
extension or package index. Missing/incomplete worlds stay UNAVAILABLE.

Spore planet saves are `.spo` files, not planet directories. Six identical
private Elidegani.spo files were found (SHA256
`146b78e9ea933839de8bfc4a52266f9ff06380c0d3b570a87917a0c2ecf7934e`).
Their stage is unclassified; they are not five qualified campaign fixtures.
Actual creation packages retained in previous private GA profiles may support
separate dependency-transfer checks after their native receipts and closure
are verified. They do not replace base smoke archives.

Archive admission checks SHA256 before touching committed Games, rejects links,
traversal, Windows reserved paths, case-fold duplicates and extraction overflow.
Warm restore uses only a fresh absent nonzero slot and never modifies a loaded
or reused slot. Native save sealing refuses an active game or an existing archive.

## Evidence and remaining gates

`out/windows-lab/backend-contracts/results.json` records baseline absence of
the backend, then passing adapter contracts and the existing registration
negative tests. Fourteen adapter contracts pass on native Windows, including a
kernel Job test proving an unrelated owned process remains alive. Twenty strict
registration tests pass. These are tooling checks, not gameplay acceptance.

The existing registration helper still requires the exact modal owner, ancestry,
native control signature, fresh identity and a repeated checked snapshot. It is
not changed to accept command 0 or to click an old fixed coordinate. Reproduce
the startup failure on the integrated 0.7.15 payload before classifying its cause.
Only a verified cause can justify a helper change, followed by a cold two-game
startup and the actual original smoke plus focused GA/editor acceptance.

No installed preview, shortcut, user profile, rollback payload, executable or
public branch is changed by this adapter work.
