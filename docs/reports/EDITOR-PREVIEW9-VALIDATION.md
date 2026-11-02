# Shared Adventure Editor preview 9 validation

Development candidate: **`0.7.5.0-ga-editor-preview.9`**, protocol **23**,
branch `feat/ga-editor-multi-act`, 2026-11-02. The original supported GOG 2024
executable and its exact 4 GB LAA variant remain supported. Older protocol
clients are explicitly rejected. This report records development evidence,
not completed native acceptance, a main merge or a public release.

## Candidate changes

- Crew roster/capacity has a separate settings identity and ownership scope.
- Host-ordered terrain records have stable identities, checked replacement and
  deletion, and one Undo item per stroke. Concurrent appends do not take a global
  planet lease. Native application preserves terrain-derived placement grounding.
- Asset-editor sessions are local to each participant. The six implemented entry
  families are Creature, Building, Vehicle, Spaceship, Plant and Cell. Saved
  creations use dependency validation and immutable content replacement under a
  separate Content lease, preserving current cast properties.
- Canonical, durable and rendered scene revisions are distinct. A detached
  participant can receive a newer durable draft without claiming its adventure
  scene has rendered it. Shared testing waits for required scene readiness.
- Staged commits keep acceptance behind persistence completion. Owned encoding,
  hashing and durable writes can run outside the editor thread; sparse journals
  avoid storing a full unchanged native base per gesture. Pure placement
  preparation and exact operation-scoped validation reuse retain opaque-change
  checks. No new measured frame or confirmation budget is claimed yet.
- Optional Extended complexity permits the candidate 4,096-placement bound while
  retaining the native 1,024-class limit. Conflicting patch signatures are
  rejected. Publication eligibility and other complexity mods are unverified.

These are implemented candidate paths. They require the native checks below
before being treated as accepted gameplay. Authoring remains paused during
Solo or Test Together. Asset editors do not coauthor the same native creation
simultaneously, and this change does not add automatic publication.

## Completed checks

| Check | Actual result and receipt |
| --- | --- |
| Full portable repository suite | The final `make test B=out/editor-preview9-final` run exited 0. `out/preview9-final-portable-tests.log` includes document 2,898, protocol 8,946, authoring extensions 328, terrain codec 100, cast 377, sparse store 2,015, project 5,634, UDP/controller 29,732 and accounting 3,425 checks. The portable terrain run has no captured native artifact; the Windows artifact checks are recorded below. The earlier complete run remains at `out/preview9-portable-tests.log`. |
| Portable tool and symbol checks | The final portable log passes commit-performance parser 12, existing performance parser 20, network proxy 6, memory-accounting collector 19 and package 16 tests. Creation, lifecycle, Save/reopen and native-audit self-tests pass. The later compiler-quiescence gate passes 14 commit-performance tests in `out/preview9-commit-tool-gate-selftest.log`. Final RE lint reports 393 used symbols, 3,015 known and zero supported-build problems; 142 missing symbols for frozen unsupported 922af7c7 are informational. |
| Portable boundary failures | The first two attempts exposed old protocol-22 and 1,024-placement test expectations. Commit `c73ec00` preserves explicit protocol-22 rejection and checks the named 4,096-placement bound plus one. Failed logs remain as `out/preview9-portable-tests-attempt1.log` and `attempt2.log`; the final complete run passes. |
| Worker-prepared resource publication | Commit `80013c3`: strict Linux and i686 builds pass. Both focused durability runs pass 340 checks; the full Linux UDP/controller suite passes 29,802 checks. The tests cover worker-owned full/delta buffers, private hash provenance, stale/moved-from/guest rejection, live transfer limits, cache-only updates and complete late joins. Receipts: `out/preview9-prepared-resources-session.log` and `out/preview9-prepared-resources-win32.log`. These are controlled network tests, not measured native commit performance. |
| Win32 controlled fixtures | `out/preview9-final-win32.json` records all 41 executables exiting 0, with individual executable hashes. Counts include authoring extensions 328, cast 393, native cancellation/terrain/cache 8,670, terrain 118 using a genuine 162-record recipe with 30 properties and 1,027 preserved adventure bytes, collector 101, creation lifecycle 211, complexity 32, native Save 444, reopen identity 150 and UDP/controller 21,495. The runtime fixture is separate from this batch. These checks substitute native game APIs. The initial 25-of-26 batch and corrected stale reopen expectation remain retained in their earlier receipts. |
| Separate runtime fixture snapshots | The latest `out/editor-preview9-creation/runtime-full-readiness.log` passes 21,450 checks, including committed foreign-durable-input and baseline-readiness regressions plus the 24 MiB retained-closure stress test. Twenty structural edits plus Save send 124,734 host / 53,195 guest bytes and grow CAS by 42,798 bytes; cached rejoin sends 33,733 / 754 bytes. Current focused receipts pass durable input 592 (`runtime-durable-input.log`), baseline readiness 173 (`runtime-baseline-readiness.log`) and combined native flow 2,057 (`runtime-native-flow-readiness.log`). Earlier full snapshots remain in `runtime-full-async-gates.log` (21,318) and `runtime-full-current.log` (20,655). These controlled fixture results do not establish native startup safety. |
| Cross build and frozen diagnostic payload | `out/preview9-candidate4-build.log` records the candidate 4 co-op DLL and affected fixture cross build. `out/preview9-candidate4-deployment/receipt.json` records checkout HEAD `dbe2012` and pins DLL `53a1c9bc234b2c7c1161e65e5f57bb19f7838c2a51de2e3a88aef0a8fe29df59`, the exact LAA executable and installed map hashes. It was deployed only to the private lab installation. Candidate 4 is a frozen diagnostic payload with the native crash below, not an accepted release; the receipt's HEAD is not a substitute for recording any uncommitted build inputs. |
| Hidden Windows render probe | `out/preview9-hidden-probe.log` reports a working hidden-window D3D adapter, three successful presentations and unchanged input desktop. It establishes fixture capability only, not game launch or authoring acceptance. |
| Initial private native startup | `out/preview9-two-runner.log` records two private processes loading the exact LAA executable and SporeKit; its loaded-map list omits the separate creation/complexity maps. Inspection of `sporekit/symbols.cpp` found discovery requires the executable header within ten 512-byte `fgets` reads. The separate maps lacked a usable header. Corrected-map startup is recorded below; neither startup alone establishes feature acceptance. |
| Corrected-map private startup | Diagnostic candidate `46cf1229e39cc88f1221a3048c67e2bdf1a15df3787820e4c49c2ef40bad39b1`: `out/preview9-commit-c2-preflight.json` verifies owned PIDs/listeners, hidden and muted windows, both exact LAA images, preview 9, 2,897 symbols, 1024x768 viewports, zero faults and `creation_editor available=1`. This establishes adapter availability, not native creation entry. |
| Native signature verification | `out/editor-creation-re/verified-creation.json` verifies six symbols/six callers; `verified-complexity.json` verifies six symbols/twelve callers on the exact LAA executable. Static signatures do not establish live editor or publication behavior. |
| Package self-test | `py -3 tests/editor_preview_package_tests.py` and WSL `python3 tests/editor_preview_package_tests.py` pass 16 tests using temporary synthetic binaries and documents. Manifest, pack version, hashes, archive contents, standalone links, allowlisted executable variants, actual source identity, symbol-header discovery (including CRT CRLF handling) and rejection guards are covered. Receipts: `out/preview9-package-selftest.log` and `out/preview9-package-selftest-wsl.log`. `tools/package_editor_preview.py --help` passes. No real candidate archive was created or installed during this check. |

The package recipe requires source preview 9/protocol 23 and a payload DLL that
identifies preview 9. The editor map must expose its verified executable header
within SporeKit's discovery window, and required creation/complexity symbols must
exist in executable-matched maps. Its manifest keeps
`complete_release_acceptance: false`.
Original personal saves, installations and launchers are not altered by these
packaging self-tests.
Receipts apply to their respective compiled snapshots. Later source changes
require the final affected checks again before a candidate is frozen.

### Native diagnostic failures retained

The first c2 commit-diagnostic bootstrap timed out at the Adventure Town browser
card because fresh profiles had not completed captain onboarding; the native
Welcome authoring buttons were disabled. Both private profiles then completed
ordinary native onboarding, saved their private captains and returned normally
to Galaxy with zero faults. Receipts: `out/preview9-c2-onboarding/result.json`
and `out/preview9-commit-diagnostic-c2/result.json`.

The retry selected Adventure Town through the ordinary native picker, but shared
startup failed before a durable baseline: the host reported that its native
context was not ready to restore the local view. A subsequent read-only
projection inspection reported a missing structured class-property reference.
Both games still reported zero faults. The original result, input logs,
screenshots and full failure snapshot are under
`out/preview9-commit-diagnostic-c2-r2`, including
`sharing-failure-evidence.json`. No warm-up or measured performance gesture ran.
The initial tool's browser-selection receipt was based on requested setup;
the tool now records successful native card selection separately, including
when later startup fails. The original failed receipts are retained unchanged.

Both processes left the party and returned normally to Galaxy before their
owning runner received its cooperative stop request. Their recorded exit code 1
is runner cleanup, not an observed native crash. The onboarded private profiles
were retained for the corrected candidate. Receipt:
`out/preview9-c2-retire/result.json`. Native sharing must succeed before any
commit-performance or crew/terrain acceptance result can be collected.

Candidate 3 (`e92986c136975e4223242faeade831bd4219df70c7105a9879ce1715f73a6748`)
passes owned-process, viewport, availability and zero-fault preflight in
`out/preview9-commit-c3-preflight.json`. Its Adventure Town startup no longer
reports the immediate camera-restore error, but fails structured-reference
validation. The exact post-failure inspection is
`owner=21474836494:1 property=1128354644 reference=8589935701:1`, corresponding
to class settings 14, the CAST property and target class 1109. Receipt:
`out/preview9-commit-diagnostic-c3/initial-host-inspection.json`. That inspection
was collected after startup failed; it is not an untouched original baseline.
The fixture again timed out before any benchmark gesture. The paused scene was
retained for read-only native investigation. The tool now captures its original
baseline before title input/hosting; 12 offline tests pass after this follow-up.

The read-only audit of the retained failed scene examined all 118 classes and
590 class/act rows. It found zero missing active references and five inactive
pickup references to class 1109. Native selectors use their standard target only
when the matching mode is active, or custom rules when the custom flag is set.
Commit `4708c4d` makes projected reference metadata follow those native selectors,
preserves the native field bytes and permits exact old metadata during recovery
migration. Receipt:
`out/preview9-commit-diagnostic-c3/active-class-reference-audit.json`. This finding
identifies an inactive-reference metadata rejection; the post-failure restore
count does not establish that restoration changed the data. A rebuilt native
sharing attempt is still required.

Candidate 4 (`53a1c9bc234b2c7c1161e65e5f57bb19f7838c2a51de2e3a88aef0a8fe29df59`)
passes its owned-process, hidden/muted, viewport, adapter and zero-fault preflight
in `out/preview9-commit-c4-preflight.json`. Its original Adventure Town projection
now succeeds before title input or hosting: 419 placements, 118 classes and five
acts, native SHA256
`76d3ac97f983da930f414c26ee0f0236ac5bb096c1653f2bb85b06a40bc5016e`.
The host reaches shared revision 0, but the guest terminates with `0xC0000005`
during initial native entry: `Simulator::cPlanetModel::IsInWater+0x57`, IP
`008AE587`, reading null with ECX zero. The guest had registered the two-resource,
60,846-byte baseline 32 ms earlier; its last reported editor state was Joining,
not yet editing or ready. The runner then retires the host. No warm-up or measured
gesture ran. Input logs, crash text, report archive, actual Windows minidump and
hashed evidence receipt are retained under `out/preview9-commit-diagnostic-c4`.
The logged symbol label is not the verified function identity: relocation maps
the fault to native preferred address `00B7E587`, in helper `00B7E580` with a null
planet-model member at `+24`. Static investigation found that Joining ownership
gates blocked native Terraform initialization. Commit `dba4afe` adds narrow
initialization guards around the verified native load and activation boundaries.
The rebuilt native result is recorded below. The performance
tool's optional exact-candidate gate now pauses after warm-up until compilers are
quiescent; 14 offline tests pass in `out/preview9-commit-tool-gate-selftest.log`.

Candidate 5 (`e9ade4004d31f505b983e1e008a42ea6802ecb1779407f79b79dc676a40627d1`)
passes the owned hidden/muted preflight with 2,901 loaded symbols and zero faults
in `out/preview9-commit-c5-preflight.json`. Both players enter Adventure Town at
shared revision 0 and native ready 1, crossing the earlier crash boundary.
Read-only convergence checking then finds a guest quaternion mismatch before any
gesture: object `4294967549` has equal position, scale and property counts, but
rotation `-0.588576794,-0.170447201,0.266058296,0.744144619` versus committed
`-0.588576674,-0.170447186,0.266058296,0.744144559`. The host converges and the guest
diverges; both report zero faults. No retirement input or warm-up/measurement was
issued after this check. Native normalization/restoration remains an acceptance
blocker. Exact outputs and log snapshots are retained in
`out/preview9-commit-diagnostic-c5/native-baseline-check.json` and
`baseline-artifacts.json`.

The c5 fixture also exposed a separate tool error: it waited for an explicit
offer after automatic native joining. Commit `76cd86e` handles explicit offers
and already joining/ready participants for two and four players, requiring exact
project/epoch identity before and after convergence. The next payload exposes
its epoch in public status. Sixteen offline tests pass in
`out/preview9-commit-tool-auto-join-selftest.log`. The original timed-out fixture
receipt is retained and is not counted as a native convergence or timing pass.

## Required native acceptance

Actual two- and four-player candidate runs remain pending for crew edits,
terrain tools and grounding, concurrent/disjoint strokes, every asset-editor
family, native Done/Cancel/save/bake, replacement and remote reopening, and
independent returns while the host or all other participants remain in editors.
Cover ownership conflicts, stale callbacks, disconnects, retry, Undo/Redo, late
join, recovery and native Save/reopen. Verify Solo and Test Together after edits
and retain existing captain, teleporter, gameplay and result regressions.

Early candidates could require retry for a quick planet/flora value completed
before its lease grant. Follow-up `2f1abaa` implements copied deferred values,
bounded gradients and current-control replay with context/ownership cancellation.
Native pre-grant UI coverage remains pending; the earlier placement-drag result
does not establish it.

The release-stall report still needs measured real native gestures on the
configured large adventure, 30 gestures per author with two and four players.
`tools/tests/coop_editor_commit_performance.py` records QPC release, capture,
validation, inverse, durability, confirmation and scene readiness stages plus
frame distributions. Proposed limits remain frame-gap p95 at most 100 ms and
maximum at most 250 ms, with confirmation p95 at most 200 ms LAN (RTT at most
20 ms), 500 ms WAN (100 ms RTT/1% loss), and 750 ms during a 2 Mbit/s
asset-transfer contention on that WAN profile. A shaped
2 Mbit/s run without concurrent bulk traffic does not satisfy that last gate.
Large-fixture native FPS and retained-memory comparisons also remain pending.

Extended complexity requires native threshold, Undo/Redo and Save/reopen checks.
No claim is made that an over-limit adventure remains publishable, or that
unreviewed third-party executable patches can coexist with this preview.

## Lab, delivery and remaining limits

Linux lab smoke is unavailable. The read-only receipt
`out/preview9-lab-smoke.log` shows no Wine, wine64, bottles-cli, Flatpak or
base Games bottle in WSL. The runner resolves its installation to `/mnt/d/Code`,
which has no expected SporeApp.exe; lane creation would copy unrelated code
directories before checking runtime prerequisites. That path was not executed.
No lanes, installations or games were changed by this lab preflight. A reviewed
equivalent Windows lab smoke or the supported Linux lab remains required before
merging. The render probe is not a substitute for it.

Native acceptance, final candidate hashes, verified packaging, shortcut/install
hash checks and rollback delivery remain separate pending steps. Sustained and
overnight acceptance is reserved for a release window. Historical preview
results in the [native-flow report](EDITOR-NATIVE-FLOW-VALIDATION.md) retain their
original binaries and limits; they are not relabelled as preview 9 passes.
