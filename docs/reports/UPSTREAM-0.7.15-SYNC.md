# SporeCoop 0.7.15 integration

## Candidate and scope

The source candidate is `sync/upstream-0.7.15-editor-main`, based on the consolidated
0.7.13 editor candidate `b8f9a3ffee2d0457ea928850e776c09d3709a713` and upstream tag
`v0.7.15`, commit `66dc8640ed001fff4d5bb38f894a8c2d55530660`. The annotated tag object
is `40db448a5839850b2968c62dd6798e2829075276`. Later upstream main is outside this merge.
The source identifies as `0.7.15.0-ga-editor-preview.16`, protocol **30**.

This is source synchronization and qualification. The installed preview14, launcher,
settings, profiles, rollback payload and exact 4 GB executable remain unchanged.
No public release, release tag or installation is authorized by this delivery.

## Integration decisions

- Preserve upstream HOST_BUSY=70 and TRIBE_ANIM=71. Adventure=72, Editor=73 and
  Activity=55 have distinct manifest routes and sender/receiver directions.
- Session family 2.0 requires the strict 103-byte player state. Adventure and Editor
  families 11/12 are mandatory even when optional family checking is disabled.
  Validate them before token rejoin can evict an existing peer. Protocol 7, 28 and
  29 clients cannot join this candidate. Native saves and recovery layouts stay compatible.
- Port the fork runtime into upstream's split core files. Preserve run, captain,
  movement, act, scene, participant and retained-callback fences. Register each
  service and command once. One outside-frame adventure command handles fork
  status/readiness/settings and upstream native act/place diagnostics.
- Preserve the fork's unconditional Scenario WORLD_TIME exclusion independently
  of upstream's optional sky fix. Campaign host pause applies only to campaign
  stages. Galactic Adventures keeps unanimous party pause.
- New autosave and persistent-store services recheck native mode, session, world,
  scene and authoring holds. Queued saves/collections cannot apply to a replacement
  world. Retain campaign records during independent creation and GA sessions.
  Native busy-pause work and lifecycle destruction drain outside Present.
- Merge SporeKit 0.3.18 while retaining the fork's bounded value FIFO, retained
  lifecycle/input owners, exclusive input policy and read-only memory guards.
  Supported original/LAA GOG 2024 images stay supported; retired GOG 2016 code
  stays removed and its obsolete files remain in uninstall cleanup inventory.
- Replace weak adventure object liveness checks with current noun membership and
  recorded native serials. A pickup retire callback cannot destroy a new object
  that reused its address.
- Keep the fork updater destination and prerelease identity. Pin shell entry
  points to LF so the same Windows checkout runs correctly under WSL and Linux CI.

## Ticket identity preservation

Upstream occupied IDs through 1238 at fetch time. The 39 independently created
fork tickets BUG-0994 through BUG-1032 now occupy BUG-1239 through BUG-1277 in order
(new ID = old ID + 245). Each ticket retains its legacy alias. Upstream identities
and commit history are unchanged. The detailed mapping is retained in
`out/upstream0715/ticket-mapping.json` in the primary workspace and in ticket aliases.
BUG-1275 tracks integration failures; BUG-1259 and BUG-1276 retain native smoke and
startup blocker history. GitHub issue numbers are independent of these ticket IDs.

## Validation evidence

Intermediate evidence is local under the integration worktree's OUT directories;
final native results must bind the tested source commit and payload hashes.

- Full Windows loader/SporeKit/coop/resources/setup build passes with the pinned
  llvm-mingw 20261006 toolchain and warnings as errors. Initial build failure for
  a write-only native selection cache was retained before removing that dead cache.
- Eight portable network shards, actual Win32 UDP routing/rejoin, all 256 routing
  golden comparisons, fake peers, seven proxy tests and sixteen packaging tests pass.
  Missing/duplicate/mismatched mandatory-family and stale reconnect failures were
  reproduced before correction. See `out/upstream0715/network`.
- Eight SporeKit Win32 fixtures pass, including actual command draining, retained
  images, input hook races and outside-frame FIFO behavior. Six production kit
  objects compile cleanly. See `out/kit-merge/fixture-results.json`.
- Twenty isolated actual autosave/store/host-busy Win32 cases pass after retained
  failing cases. See `out/kit-merge/campaign-qualified-results.json`.
- Clean-checkout creation contract workflows create their owned OUT parent. Three
  regressions and all 728 original contracts pass; old failing evidence is retained.
- Source, manifest and ticket checks and repository tests are required on the final
  candidate after the merge commit makes upstream ticket history reachable.
- Original Windows smoke assertions, private native fixtures, focused two-player
  editor/adventure checks and a short four-player check are still qualification
  gates. Missing fixtures, crashes, hangs and skips are not passes.

Main promotion and absorbed branch/worktree cleanup wait for repository tests and
real Windows smoke. Broader creation matrices, sustained performance and overnight
acceptance remain deferred. Known editor issues remain in GitHub tracking and the
active known-issues document; this synchronization does not silently close them.

## Qualification progress, 2026-11-20

The selected Windows fixture matrix now passes all 35 executables plus eight
source-identical SporeKit fixtures. The full editor session fixture passed 24,360
checks in 113.933 seconds; the full runtime fixture passed 124,395 checks in
217.561 seconds. Their initial 90-second harness bounds were retained as failures,
then replaced by one measured 300-second retry each; no assertions were relaxed.
Ten focused runtime groups also pass. `out/win0715fixtures` records binary hashes,
651 tracked source-input hashes and unchanged expanded compile/link recipes.
The pickup address-reuse regression fails against the retained old adapter and
passes against the current adapter; stale consumed contacts remain intercepted.

The startup command-0 variant is now verified in separate cold profiles. Its
native command getter is correct; clicking its exact validated close control
returns directly to native Galaxy controls without the warning the old harness
waited for. Keep the command-3/warning path and stale-window guards. A normal native
`-r:1440x1080` startup option gives both that rendered viewport and UI canvas at
cold launch, avoiding a late renderer reset. The Windows backend owns a whole-run
kernel lease, retains failed cleanup handles, kills only its owned processes and
verifies the exact deployed symbol set. Its eighteen contracts pass.

Ticket checks preserve every ancestry, release, ambiguous-object and message
rule while batching their Git history reads. Real temporary repositories compare
identical verdicts and notes with the old implementation. The complete 1,270-ticket
check passes in 11.35 seconds on Windows and 26.22 seconds through the unchanged
WSL Git adapter, replacing repeated history walks lasting over ten minutes.

The published review branch is on the user's fork. GitHub's fuzzing job passes;
its build/test workflow and real campaign/editor native qualification are pending.
The GitHub issue-write endpoint still returns 403 despite a connected GitHub app.
Prepared issue 1-10 link corrections preserve concurrent bodies and stay local
until issue-write access works; old issue-linked sync branches must remain.


Native group preparation caught a duplicate pickup detour before any launch:
upstream player_rules and the Adventure adapter owned two names for the same
verified native function. The adapter now uses the sole existing hold/release
owner through typed forwarding/interception; actual-handler RED/GREEN and mirror
fixtures pass. Editor observation also revealed repeated campaign-avatar reset
work during authoring; that observer is now gated until gameplay resumes. Cold
native availability, pickup behavior and idle logging must be verified on the
rebuilt payload before smoke/promotion.
