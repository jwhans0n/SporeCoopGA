# Native Windows qualification adapter

This tooling continues BUG-1259 (renumbered BUG-1259 during 0.7.15 integration)
and the startup investigation BUG-1031 (BUG-1276). It does not qualify a payload
by itself. Both tickets stay open until their real native acceptance checks pass.

## Explicit lane and original assertions

Native results also use the existing `out/lab/<timestamp>-windows-.../results.json`
schema, source `head`, per-case verdict, copied `<case>.<role>.game.log` names
and `summary.md`. The same `labcov` and recorded `loggates` readers consume them.
An observed INCONCLUSIVE/SKIP is never a pass. The supervisor rejects a config
whose payload source commit differs from the current test checkout HEAD, so
different source revisions cannot inherit a final smoke pass.

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

### Native startup investigation, 2026-11-20

The intermediate integrated 0.7.15 payload reproduces the strict failure in
`out/windows-lab/native0715-startup`: closeFFFFFFF2 has native command0 under the
expected registration owner. Matching payload/map hashes are retained separately
in `intermediate-payload.json`. This is not final source qualification.

Two fresh hidden profiles (`native0715-registration-probe2` and `probe4`)
verify that an ordinary click on that freshly resolved, revalidated native
control removes registration directly. Actual after trees and screenshots show
the Galaxy menu with native Play/Create/GA controls. No quit warning occurs.
The earlier 0.7.13 probe waited only for that warning; that timeout did not prove
the native close failed. The probe3 diagnostic had a multiline-memory reader
error and is explicitly not passing evidence.

Read-only native getter inspection resolves vtable slot0x20 exactly to named
`UTFWin::Window::GetCommandID`. Its bytes `8B8180000000C3` read object+0x80;
there is no evidence of a wrong getter ABI. The helper now recognizes only the
observed command-zero embedded-browser family, checking its modal dimensions and
flags, close control type/geometry/flags/empty caption, exact native content and
Login companions, and all existing owner/identity guards. Command3 and the native
quit warning retain their existing checked paths. Arbitrary command-zero close
buttons remain rejected.

Twenty-three startup contracts pass, including altered family rejection,
duplicate close identities, stale snapshots, unrelated modals and bounded input.
The production helper itself reaches a verified clear Galaxy menu on the new
intermediate native payload (`native0715-startup-fixed`). Final committed hashes
must repeat that check before BUG-1031 / GitHub issue10 is closed.

The single-game renderer resize experiment reports1440x1080 while its native UI
canvas remains1024x768. It is not accepted as a campaign bootstrap. Options11 and12
both cold-launch1024x768 on this adapter/monitor; ordinary native Graphic Settings
and a cold preference readback are the next viewport qualification step.

That viewport blocker was resolved through a verified native launch switch,
without introducing a renderer hook or changing the user's display. Read-only
Configure0x7E7430 inspection finds native `CommandLine::FindSwitch("r")` at
7E758B and wide `%dx%d` parsing at7E75A9, followed by the original renderer create.
The ordinary `-r:1440x1080` launch argument produces both observed renderer
1440x1080 and native main UI canvas `[0 0 1440 1080]` in a cold hidden game.
`native0715-viewport1440/result.json` and its screenshot/commands retain this PASS.
The fixed registration helper also reaches actual Galaxy controls at that size.
WindowsLab accepts only the two audited viewport sizes for this option; every
backend launch separately verifies both renderer dimensions and the actual
native UI canvas before any fixed-pixel scenario input.

Seventeen backend contracts now also cover the upstream1070 ownership rule on
Windows. An exclusive kernel byte lock owns the private installation before any
profile reset/deployment and remains held across owned game restarts. A second
run is rejected before it can touch another active run's profile. Cleanup is
idempotent, and the kernel releases ownership if the worker exits. The initial
new lock test exposed a double-close ownership reacquisition; its failing and
passing runs were retained during development.

An old private installation copy can retain symbol maps removed in v15. Prepare
and deploy retire only obsolete `.sym` files inside that explicitly marked
private symbol directory, preserving unrelated diagnostic files. Exact deployed
payload and symbol-set verification must then succeed before any native launch.
