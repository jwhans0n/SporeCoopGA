# Galactic Adventures fork integration

This report records historical preview 7 evidence. Current preview 8 uses
protocol 11; its [separate validation report](validation-ga-preview.8.md) tracks
new music, item, confirmation, downed and lighting changes. The passes below
apply to the exact preview 7 binaries listed here.

`0.7.3.0-ga-preview.7` integrates the complete Galactic Adventures preview 1-6
history with upstream SporeCoop 0.7.2.1. Use the same fork build on every
participant; this fork uses protocol 10. The completed checks cover the
integrated source: native captain preparation, shared scenes, Replay, SporeKit
0.3.4, collector upgrades, teleporters, representative explosives, static
replication and the large-adventure FPS regression. All 13 native acceptance
groups passed with zero recorded faults. Historical preview 6 results remain
separate from this integration evidence.

## Source ancestry

| Input | Commit | Scope |
|---|---|---|
| Upstream SporeCoop 0.7.2.1 | `eb63c11133b9d2474cfc942ac18566edbaf7c4f1` | Space features, GOG/Steam recognition, frame-loop support and SporeKit 0.3.4 diagnostics |
| Galactic Adventures preview 6 | `b69164aad62cb678500555dd33a8d8c67ea4199f` | Complete linear preview 1-6 history, native Windows support, performance fixes and regression tools |
| Common base | `7765a74` | Upstream SporeCoop 0.6.0 |
| CI-validated integration | `cd051d85409d4f74cc64eb6423443c4ee8155de2` | Combined product source, fork packaging/workflow and preparation-driver wait |

Both source histories are preserved. Downloads, update checks and new problem
reports use [jwhans0n/SporeCoopGA](https://github.com/jwhans0n/SporeCoopGA).
Historical upstream reports and attribution retain their original links.

At validation time the fork reports `has_issues: false`: GitHub Issues is
currently disabled. Diagnostic/report URLs point to the correct fork, but they
cannot accept new issues until the owner enables **Issues** under
**Settings > General > Features** in that repository. This merge does not change
repository settings.

## Compatibility and merge decisions

Base-stage game recognition retains upstream GOG 2016, GOG 2024, Steam DRM and
verified same-code handling. The native GA adapter additionally requires the
actual exact-file GOG SHA-256
`dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`.
The GA notes call this March 2017 after the ModAPI address map; upstream calls
this same executable GOG 2024. Steam recognition does not enable the GA adapter.
No native Steam adventure compatibility is claimed.

Protocol 10 preserves adventure/captain resource-bundle formats and the GA wire
state while carrying the upstream base-stage features. Upstream protocol 7
clients cannot join. This merge retains the GA memory-validation cache, native
captain identity/scale handling, explicit readiness, cancellation cleanup and
Replay ordering. Creature/NPC ownership and GA update filters remain composed.

The symbol merge keeps the upstream boolean/out-parameter property reader
`App::Property::GetFloat` distinct from the GA float/default reader
`SporeCoop::Adventure::GetFloatDefault`. Reviewed GA overrides load after the
upstream map, and the verifier checks the effective complete load order.
The upstream Space map's non-standard `M_PI` use was replaced with C++20
`std::numbers::pi` after the local LLVM-MinGW headers rejected it. The expression
and geometry are otherwise unchanged.

## Build, ABI and package evidence

The [GitHub Actions run for `cd051d8`](https://github.com/jwhans0n/SporeCoopGA/actions/runs/40149842358)
passed the complete Windows-target build with the pinned LLVM-MinGW 20261006,
portable tests and knowledge checks, packaging and artifact upload. This is an
Ubuntu 22.04 cross-build; native game checks run separately on Windows. The run
provides the `sporecoop-zip` artifact and published no release.

| Gate | Result and artifact |
|---|---|
| Local full loader/SporeKit/coop/resource/setup build | PASS: `out/lab/fork-ga-integration/build-final.log`, LLVM-MinGW 20261020 |
| Portable network, adventure, asset, runtime, flow, UI and knowledge tests | PASS: `out/lab/fork-ga-integration/portable.log`; 301 referenced symbols, zero lint problems |
| Thirteen Win32 native-adapter ABI fixtures | PASS: all 13 return zero; executable hashes and outputs in `out/lab/fork-ga-integration/abi-results.json` |
| Installed dc04 static native verification | PASS: 151 functions and 4 globals, effective symbol load order of 2714 names; `out/lab/fork-ga-integration/symbols-static-package.json` |
| Fork zip/update-pack contents and destinations | PASS: all 26 zip files and 14 SCUP entries; `out/lab/fork-ga-integration/package-inspection.json` |

Native testing uses these unstripped candidates:

| Binary | SHA-256 |
|---|---|
| `coop.dll` | `03a4680404793dd2dd9fabb25dd4103203503fa9536aad886133331435ec4e4f` |
| `sporekit.dll` | `c563382a57f9e6ac3d58ac9fbceb10183c9a3b7c29ce135bee9f1d0dfd088a4b` |

The package script strips only copies in its stage. All five original build
artifacts kept identical hashes before and after packaging; see
`out/lab/fork-ga-integration/package-provenance.json`. Every packaged PE `.text`
section matches its original build counterpart. Whole-file hashes differ after
stripping and are recorded separately in the package inspection report.

The local WSL environment lacked `zip`, so `tools/package.sh` prepared the stage,
pack and setup, then Python's standard `zipfile` module archived that exact
stage. The archive passed CRC, required-content, safe-path, all-symbol-map and
SCUP payload/hash checks. The CI packaging step passed with its normal archiver.
The local archive is `out/dist/SporeCoop-0.7.3.0-ga-preview.7.zip`, SHA-256
`b35664196a832af02c282aa2b8b8eee0515a7bea1f950122d3ad1adc6f737754`.
The local and CI archives are separate builds; no identical whole-file hash is
claimed between them.

## Native Windows evidence

Checks use the exact GOG dc04 executable, isolated profiles
`winlab_ga_upstream_merge_41` and `winlab_ga_upstream_merge_42`, a private game
installation, hidden windows and muted audio. The completed cases below report
zero recorded native faults. Artifact paths in this table are relative to
`out/lab/fork-ga-integration/` unless shown otherwise.

| Case | Observed result | Artifact |
|---|---|---|
| New captains and preparation recovery | PASS: both players create and save distinct named captains, then enter the same adventure. Six checks cover selection/naming/save cancellation, stale work, disconnect and immediate retry. | `native-preparation-confirmed/result.json` |
| Town beam-down confirmation | PASS: guest-first Continue leaves both clocks at zero until the host confirms; gameplay then advances. All 405 host actors are mirrored and rostered on the guest. | `native-intro/result.json` |
| SporeKit 0.3.4 diagnostics and frame fallback | PASS: both clients generate valid report archives containing environment, co-op state, faults, modules and logs. Forced guest game-loop callbacks continue advancing the shared adventure, then revert to Present. | `native-diagnostics/result.json` |
| Shared Replay | PASS: two successive rounds return to the shared lobby, preserve both saved captains, require explicit Ready and Continue, and restore complete 405-actor rosters. | `native-replay/result.json` |
| Captain replacement and scale | PASS: a new guest captain replaces the prior selection; the next run uses the explicitly selected older captain. Remote native profiles match transferred resources and owner scale. | `native-models/result.json` |
| Mr Puzzle's Magical Gates | PASS: the stock authored captain/music adventure loads, preserves its captain scale, and shares Continue with matching 351-actor rosters. | `native-puzzle-intro/result.json` |
| Shared objective conversation | PASS: an ordinary NPC click opens the conversation on both players; pages remain independent, Finished waits for the group, and both clients advance to the next act. | `native-talk/result.json` |
| Queued callback DLL lifetime | PASS: callbacks in both main and outside-frame queues execute after test-mod unload, delete their payloads and finally release the retained DLL. | `out/windows-lab/ga_upstream_merge/task-lifetime-41.json` |
| Collector upgrades and teleporters | PASS: all five health/speed/damage/armor/teleporter cases, collector-only grants, refresh/expiry/shared holds, shared claims/respawns, authored teleport cleanup/cooldowns and rejection of pre-teleport movement. | `native-pickups/result.json` |
| Representative explosives under packet loss | PASS: mine, barrel and 15-second bomb with repick, friendly fire 100, two players, 3% loss and up to 40 ms jitter. | `native-explosives/result.json` |
| Static replication and reconstruction | PASS: unchanged static transforms stop recurring; cached guest-pose repair, host movement promotion, continued spatial updates and pose restoration succeed. | `native-town-bandwidth/static/result.json` |
| Large-adventure FPS and actual bandwidth | PASS: 405 actors on both clients, both Present FPS samples exceed 20, and stationary-state traffic stays silent during measurement. | `native-town-bandwidth/result.json`, `native-town-bandwidth/performance.json` |

The authoritative aggregate is
`out/lab/fork-ga-integration/native-summary.json`. It records all 13 passing
acceptance groups, the exact candidate hashes, source commit, metrics, scope
limits and shutdown results. Both owned games returned to verified native menus
and then quit with process exit code 0, with zero faults. The long-running lab
watcher returned 1 after detecting those intentional exits; that watcher status
is not a game crash or a failing native test. No lab game remains running.

### Town traffic and FPS

The final Town run uses the frozen candidate listed above. The 10.034589-second
window has no injected loss or jitter. Proxy traffic counts actual per-peer UDP
datagram payload, including SporeCoop headers, acknowledgements and
retransmissions; it excludes UDP/IP/Ethernet overhead.

| Measurement | Result |
|---|---:|
| Host and guest actors | 405 each |
| Host to guest payload | 56,703.96 B/s |
| Guest to host payload | 1,714.37 B/s |
| Unchanged static-state packets during the window | 0 |
| Spatial updates per moving actor | 7.175181/s (72 updates for each of 113 actors) |
| Host Present FPS, eight-second sample | 21.363465 |
| Guest Present FPS, eight-second sample | 21.488691 |

The observed moving-actor cadence matches the earlier hidden-lab baseline of
about 7.18 updates/s. Configured creature/vehicle spatial frequency remains
10 Hz. The FPS regression requires at least 400 actors, advancing native time
and 20 FPS; both clients pass with Present restored. These hidden-session
measurements are not a universal visible-session FPS guarantee or a new
before/after bandwidth campaign.

The pickup suite exercises the distinct native float/default accessor with its
authored magnitude and duration. Native Bite damage follows 1.5 -> 3.0 -> 1.5
on boost/expiry for both collector origins; shield damage follows 12 -> 6 -> 12.
These checks guard against applying either modifier twice.

The frame-loop test measures callback and adventure-time progress, not rendered
FPS. Report generation verifies the existing diagnostics without deliberately
crashing either game. Diagnostic text and archive README links point to the
fork's issue tracker.

The first preparation driver attempted Ready before asynchronous native saving
finished. Its failed attempt remains under `native-preparation/`; the confirmed
six-check run waits for the saved/validated captain and is the passing result
listed above. An incomplete or exploratory artifact is not treated as a pass.

## Limits and delivery

This evidence does not establish a complete adventure playthrough, every custom
palette/objective, native Steam GA support, Space-stage adventure entry or a new
full base-stage campaign. The native integration cases listed here use two
players. Earlier preview 6 four-player, impaired-network, explosive and bandwidth
results remain historical evidence for that exact preview build. They are not
relabelled as fresh integration tests. The new explosive smoke covers three
representative kinds at friendly fire 100, not the whole preview 6 matrix.
Static reconstruction/promotion was exercised in Town act 0; forward-act
static replication was not rerun in this integration smoke.

The player's running saves and profiles are outside the private lab. Preserve
the upstream GPU-load limits and Windows isolation rules in
[testing](06-testing.md). No personal session restart or game installation is
required to merge this fork.

No tag or published release is part of this integration. A future hyphenated
preview tag is marked `--prerelease` by the workflow; numeric release tags retain
regular release behavior. Install GA previews from their matching zip/local
pack, since automatic suffix ordering and GitHub latest-release discovery do
not cover prereleases. The merge itself does not publish the CI artifacts as a
GitHub release.
