# Shared Adventure Editor preview 9 validation

Development candidate: **`0.7.5.0-ga-editor-preview.9`**, protocol **23**,
branch `feat/ga-editor-multi-act`, updated 2026-11-03. The original supported GOG 2024
executable and its exact 4 GB LAA variant remain supported. Older protocol
clients are explicitly rejected. This report records development evidence,
not completed native acceptance, a main merge or a public release.

The user-requested testing installation is C49, co-op DLL SHA-256
`3741ec661fd54588f531cda059d708d5d4971c946a278f58c1a3e165f3a337dc`,
with Kit `0f25bb7a2943a4e1ce3ad7d44a077ea3e8bd41a230c1130b8f2957b10cf6ecfc`.
It was installed on 2026-11-03 before deferred release acceptance, as requested.
Cold recovery now passes the original revision-22 draft in both games.
Host editing and replacing a guest-created Creature also passes.
Guest New Creature Save while the host designs another Creature passes; the
host's later discard returns to the latest revision without a history change.
C46 fixes the generated-cache dependency rejection: Native adventure Save and
shared cold reopen now pass the revision-24 Creature draft. Exact cold comparison
identifies four local asset machine-ID resets; every other decoded field matches.
New UFO Save also passes independently from both roles.
C47 also verifies a baseline Save, new creation and subsequent ordinary Save,
closing the inherited-clean native dirty-state failure. All 129 classes survive
fresh native cold reopen and shared materialization. The two-frame preparation
experiment remains default-off and unqualified for performance.
C38 verifies all five Crew control families from all four participants and
independent four-player creation-editor entry and cancellation.
C35 closes the new-vehicle Native Save/cold-reopen dependency reproduction.
C39's quiet short two-player timing probe measures host/guest maximum
submission-to-confirmation at 51/171 ms and commit-window frame gaps at 107/142 ms.
The six-edit probe is diagnostic only, and frame gaps still exceed 100 ms.
Full performance acceptance and the remaining feature matrix are open.
The personal shortcut now selects preview 9/protocol 23. The exact 4 GB
executable, settings and player launchers remain unchanged; preview 8 is backed up.

## Testing handoff and revised priorities (2026-11-03)

The user explicitly requested installation before further work, followed by
upstream synchronization, then complete Terraform and Extended Complexity
checks. Broader four-player creation/replacement/failure/recovery tests,
performance acceptance, final regressions and required release smoke are
deferred. This authorization permits a testing installation, not a main merge
or a claim that the deferred checks passed.

C48 armed diagnostics identify one outside-task pump consuming 184 callbacks
over 520 ms while idle frame medians are about 46 ms. Capture frame maxima reach
559 ms. Commit `28536e7` bounds each pump by its initial FIFO sequence and an
8 ms budget checked between callbacks, retaining pending work for another pump.
Focused host/i686 queue, actual SDK pump, metrics, pulse and lifecycle checks
pass; the exact C48 console reproduces the new-work-deferral failure. Actual
frame improvement remains unmeasured. Evidence:
`out/preview9-c48-pump-probe/diagnostic.json` and
`out/editor-preview9-outside-batch/{receipt,postcommit}.json`.

C49 retains 253 production inputs with zero drift and builds both DLLs.
Installation verifies every copied DLL/map, marker version and shortcut target.
The 24,895,536-byte executable retains SHA-256
`47f60c4db38fda1f15c5a3525617e171b0a98d728d99d645b035aa794a9199ab`.
No game was running during installation; no player profile was written. INI
files, both player launchers, forwarding DLL and data package retain their prior
hashes. The two-frame pose experiment remains default-off. This direct testing
handoff does not include a new native startup run or a distribution archive.

Installed build source is `28536e76ab13f12040dbadbd6c89a0163ebf9cc6`.
Receipts: `out/preview9-candidate49-build-receipt.json` and
`out/preview9-delivery/preview9-testing-installation.json`.
Rollback files and their checked restore manifest are at
`out/preview9-delivery/preview8-rollback-testing-20261103-151411/`;
the earlier full preview-8 rollback payload is retained as well.

## Candidate changes

- Crew roster/capacity has a separate settings identity and ownership scope.
- Host-ordered terrain records have stable identities, checked replacement and
  deletion, and one Undo item per stroke. Concurrent appends do not take a global
  planet lease. Native application preserves terrain-derived placement grounding.
- Asset-editor sessions are local to each participant. The four verified
  adventure-compatible entry families are Creature, Building, Vehicle and
  Spaceship, with native subtype resolution. Direct Plant and Cell entry rejects. Saved
  creations use dependency validation and immutable content replacement under a
  separate Content lease, preserving current cast properties.
- Canonical, durable and rendered scene revisions are distinct. A detached
  participant can receive a newer durable draft without claiming its adventure
  scene has rendered it. Shared testing waits for required scene readiness.
- Staged commits keep acceptance behind persistence completion. Owned encoding,
  hashing and durable writes can run outside the editor thread; sparse journals
  avoid storing a full unchanged native base per gesture. Placement preparation
  reuses exact operation-scoped proofs while retaining opaque-change checks.
  C33 measured host confirmation improvements; guest confirmation and observer
  frame budgets still fail acceptance.
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
issued after this check. Native normalization/restoration blocks acceptance of
this candidate. Exact outputs and log snapshots are retained in
`out/preview9-commit-diagnostic-c5/native-baseline-check.json` and
`baseline-artifacts.json`. The failed phase-4 scene was not retained after the
helper timed out: closing its UDP proxy automatically ended the guest session.
Crew's later read-only inspection found phase 0, generation 4 and two restores;
the second teardown restore had repaired the quaternion. That later state does
not establish the raw quaternion in the original failed joined scene.

The c5 fixture also exposed a separate tool error: it waited for an explicit
offer after automatic native joining. Commit `76cd86e` handles explicit offers
and already joining/ready participants for two and four players, requiring exact
project/epoch identity before and after convergence. The next payload exposes
its epoch in public status. Sixteen offline tests pass in
`out/preview9-commit-tool-auto-join-selftest.log`. The original timed-out fixture
receipt is retained and is not counted as a native convergence or timing pass.
LAN-only bootstrap now uses direct game-owned transport so helper cleanup cannot
end the retained party. Ready/durable native divergence stops the fixture at its
first failed check; WAN profiles still require owned impairment proxies. The
next reproduction must retain the same project, epoch, native generation and
restore count through the bounded raw-marker evidence collection. Nineteen
offline routing, join, divergence and timing tests pass in
`out/preview9-commit-tool-direct-lan-selftest.log`.

### Candidate 6 native baseline and measured release lag

Candidate 6 DLL
`82afd4a885c30b381d0198dc3e9bf4956c1a6cccde20e5e672d37da23403a94a`
passes the exact hidden/muted process preflight and direct-LAN Adventure Town
bootstrap. It includes capture of the loaded native terrain before the first
baseline restore; equal terrain identity avoids unnecessary regeneration. Both
players converge with identical native snapshot SHA256
`2fcb508d4572c04bcd075cd55f4f410b5f866e5e0f8dda0a4eb59035c46f57c7` and matching
project/epoch. The 419-placement, 118-class, five-act adventure is unchanged at
this baseline. Receipts: `out/preview9-commit-c6-preflight.json` and
`out/preview9-commit-diagnostic-c6/result.json`.

The first automatic warm-up target was under native UI at the overview camera;
three gestures produced no selection or canonical edit. Ordinary native Locate
on the Lead Actor card and camera settlement exposed placement `4294967303:1`.
The attach retry explicitly verifies the baseline hash and uses that placement.
Warm-up produces one synchronized history item per author. With all compilers,
fixtures and other game diagnostics paused, the run then completes 30 measured
gestures per author, each with one exact operation and all-player convergence.
No denied retry counts as a measured sample.

The LAN timing gate **fails**:

| Metric | Host 95 | Guest 96 |
| --- | ---: | ---: |
| Own submit to confirmation p95 | 375.14 ms | 933.59 ms |
| Own release to confirmation p95 | 801.43 ms | 1,359.88 ms |
| Commit-window maximum frame gap p95, 60 windows each | 415.02 ms | 417.26 ms |
| Worst individual window's native frame-gap p95 | 329.73 ms | 371.70 ms |
| Largest frame gap | 416.89 ms | 460.42 ms |
| Own capture p95 | 153.20 ms | 156.05 ms |
| Own supported-change validation p95 | 161.77 ms | 148.49 ms |
| Own inverse preparation p95 | 82.94 ms | 83.51 ms |
| Own native apply p95 | 299.05 ms | 300.12 ms |
| Own persistence preparation/completion p95 | 102.74 ms | 105.24 ms |
| Own durable interval p95 | 48.48 ms | 50.69 ms |

For guest-authored operations, host request receipt to decision queue p95 is
376.79 ms. The largest measured spans are native capture, validation, inverse
preparation and application; durable I/O alone does not account for the release
lag. These stage spans identify where to profile next and do not identify one
unmeasured native subroutine as the cause. Confirmation limits remain 200 ms LAN
and frame limits 100 ms p95 / 250 ms maximum.

At the end both players have revision 62, durable revision 62, 62 canonical
history items (31 per author including warm-up), the same converged hash and zero
native faults. The direct party remains connected after the failed timing gate.
All samples, exact QPC stages, frame distributions, baseline and final proof are
under `out/preview9-commit-diagnostic-c6-r2`, including `measured-summary.json`,
`warmup-history-proof.json` and `final-state-proof.json`. The run is diagnostic;
it does not pass performance acceptance or establish four-player, WAN,
concurrent asset-transfer or feature-UI acceptance.

## Candidate 6 creation UI probe: return failed

After the measured run, the same owned hidden/muted candidate 6 pair remained
at revision 62. The normal visible New asset / New creature controls opened
the native creature editor. Native mouth placement, naming
`Preview9CreationProbeC6` and green Done saved the local creation; the host
advanced once to committed/durable revision 63. Return then froze with
`Waiting for the active native Adventure Editor camera`. Its viewport was
black. The guest remained at committed/durable revision 62 and froze with the
host-baseline-recovery notice. Both native fault counters remained zero.

The read-only camera receipt confirms the host still selected native creation
camera `A50C91D5` at index 29, while the existing Adventure Editor orbit camera
`00EBB801` was index 40. The guest remained on index 40. Both games reported
Scenario mode and native readiness. Native readiness alone did not establish
that the returning scene's intended camera had resumed.

Evidence is under `out/preview9-creation-probe`, including `result.json`,
`commands.json`, `camera-native-readonly.json` and the native screenshots.
Inputs stopped at failure. No recovery, Undo, memory write or direct native
function call was used to repair or conceal it. This probe establishes visible
toolbar and creature-editor entry, and exposes a blocking return failure.
It does not pass creation workflow, cancellation, remote editing, immutable
distribution or independent-return acceptance.

Static `NotifyResult` code does not read the cancellation flag, but that fact
alone does not prove all result listeners are independent of it. Reviewed
Adventure result listeners use cancellation to guard asset changes. The
browser's Scenario-specific request field `39` controls editor UI visibility,
not camera resume. Native activation/cached-camera synchronization requires
further scoped implementation and an actual Done/Cancel rerun; no generic
camera-selection repair is claimed here. Details are in
[the creation RE notes](../re/creations.md#9-native-creation-return-failure-candidate-6-2026-11-02).

The subsequent native message audit found a separate result-suppression defect:
NotifyResult posts its listeners through MessagePost. Candidate 6 restored the
native cancellation byte before those listeners ran. The corrected hook keeps
only its exact owned native result cancelled for the full queued lifetime and
retains the real saved completion separately for the coordinator. A strict
i686 lifecycle fixture with retained queued messages passes 308 checks,
including delayed listeners, native mode-return ordering, shutdown, balanced
references and unrelated requests. Receipt:
`out/editor-preview9-creation/creation-posted-result.log`. The earlier
synchronous fixture did not cover this boundary. The fix and scoped camera
activation still require an actual native Done/Cancel rerun that also verifies
the Adventure Editor controls and visible scene have returned.

### Post-c6 commit and creation-return hardening

The c6 timing failure led to three checked optimizations, rather than a change
to the acceptance limits. Commit `cd6b153` caches immutable cast projections
under an exact native whole-roster serialization fingerprint, including schema,
context, act ordering and resolved asset witnesses. The cache retains at most
eight versions and 32 MiB. Commit `d8483af` retains an owned placement gesture's
native result until acceptance instead of applying its inverse and then the
same accepted result. Foreign commits first restore the checked canonical
state, and persistence rejection still restores that state. Commit `a6ea123`
reuses equal native bytes only when act, bindings, captain, captain lock and
metadata sidecars also match; typed projection validation remains mandatory.
These are implementation changes, not measured native performance passes.

The lifecycle follow-up `4b417f1` restores tentative input before same-revision
Save checkpointing, fences End/Leave against source and binding replacement,
withdraws scene readiness during native gestures and preserves restoration
failure diagnostics. Its frozen runtime SHA256
`4442dcdcd782f28487424a7df6f1904fa9291e063ecf298c189d3356a8531c46`
passes 1,803 lifecycle, 3,950 adoption and 25,760 full runtime checks. Exact
compiled inputs and executable hashes are in
`out/editor-preview9-adoption/lifecycle-stable-receipt.json`.

The exact-byte follow-up runtime SHA256
`e5bff61b02cfd57321f4fd659d7fb9e25302d196141202618ffb3acd5c3f0c78`
passes 989 byte/sidecar, 717 tail, 4,952 adoption and 26,980 full runtime checks.
The driver reports source-header drift because the next creation API header was
edited after this executable was compiled; its runtime source, test source and
executable hashes remained unchanged. Results apply to the recorded compiled
header, not the later API. Receipts and that qualification are in
`out/editor-preview9-adoption/byte-guards-evidence.json`.

Commit `08c9283` fixes owned creation-result suppression for Spore's queued
message delivery. The native result remains cancelled for its posted lifetime;
the module separately retains the immutable actual saved key and cancellation
result. Its Win32 fixture now models retained queued results and delayed mode
return, passing 308 checks in
`out/editor-preview9-creation/creation-posted-result.log`.

Native return commits `9ec2a10` and `efe6976` add a value-only mode/data/resource
owner witness and read-only initialization, pending-entry, transition and UI
readiness checks. The guarded paired camera activation verifies both the active
registered camera and Simulator's cached orbit interface. It does not substitute
for completing ordinary native resume. The exact native fixture passes 9,933
checks (`out/editor_native_creation_resume.exe`), and the GOG ABI verifier passes
90 entries (`out/editor-creation-resume-abi.json`). Live Save/Cancel acceptance
is recorded separately from these controlled native substitutes.

Candidate 7 DLL
`ab5fba723390806add3a1feb61c383225c6a91dd8fd116e05f4a070cf23369a4`
is frozen in `out/preview9-candidate7-source.json` and installed only in the
private lab copy. Its owned hidden/muted pair passes startup preflight with
2,907 symbols, the exact LAA executable and zero faults. Adventure Town's
unchanged original native baseline is again
`76d3ac97f983da930f414c26ee0f0236ac5bb096c1653f2bb85b06a40bc5016e`.
Both players reach revision 0, durable revision 0 and matching convergence
hashes with initialization 4, entry pending 0, transition 0 and world visible 1.
Receipts: `out/preview9-commit-c7-preflight.json` and
`out/preview9-c7-bootstrap/result.json`. This startup is not feature or timing
acceptance. The personal shortcut and installed preview 8 remain unchanged.

### Candidate 7 native movement timing: 60 gestures, limits failed

The same candidate 7 pair subsequently measures 30 ordinary placement gestures
per author in the private Adventure Town scene, with 419 placements, 119 cast
classes and five acts. One earlier native creature Save accounts for the extra
class. A first timing attempt hit an occluding wall instead of the requested Lead
Actor; that attempt provides no timing acceptance result. Three resulting wall
edits remain in this private diagnostic scene and are recorded separately.

The corrected fixture first uses the native Lead Actor card's Locate action,
then verifies the actual selected placement and a granted placement lease after
mouse-down, before sending movement. Both warm-ups and every measured gesture
must move placement `4294967303:1`, preserve the other bounded placement poses,
advance exactly one canonical revision and converge natively on both players.
A wrong target cancels with Escape and release before movement, checks the
unchanged revision and stops the run. Offline fixture coverage passes 26 tests.

The measurement gate opens at 05:03:06 local on 2026-11-02 with the exact candidate
hash. All other agents pause builds, fixtures, copying, hashing, game diagnostics
and input during the window. Direct LAN transport belongs to the games; the
fixture's exit does not disconnect them. All 60 valid samples complete, but the
LAN timing gate **fails**:

| Metric | Host 95 | Guest 96 |
| --- | ---: | ---: |
| Own submit to confirmation p95 | 261.88 ms | 704.06 ms |
| Own release to confirmation p95 | 526.34 ms | 946.55 ms |
| Commit-window maximum frame gap p95, 60 windows each | 459.52 ms | 275.74 ms |
| Worst individual window's native frame-gap p95 | 54.32 ms | 187.71 ms |
| Largest frame gap | 547.19 ms | 315.97 ms |
| Own capture p95 | 93.25 ms | 81.72 ms |
| Own supported-change validation p95 | 150.20 ms | 144.57 ms |
| Own inverse preparation p95 | 0.0004 ms | 0.0004 ms |
| Own apply work p95 | 158.21 ms | 152.07 ms |
| Own persistence preparation/completion p95 | 106.90 ms | 103.57 ms |
| Own durable interval p95 | 51.84 ms | 52.59 ms |

The inverse interval is effectively eliminated, and own confirmation delays
improve from candidate 6. The frame limits still fail on both players, including
an isolated host gap larger than candidate 6. For guest-authored operations,
host request receipt to decision queue p95 is 284.90 ms. Host remote apply-work
p95 is 202.63 ms, while observer remote apply-work p95 is 195.91 ms for host
gestures. In the retained median examples the actual native pose write takes
about one millisecond, or is skipped for tentative adoption; most apply-work
time precedes it. Capture, supported-change validation and accepted-image work
remain measured costs. These intervals do not establish the cost of individual
unmeasured native calls. Direct-LAN request/decode scheduling in the median guest
example also contributes about 82 and 88 ms respectively; physical network
latency is not measured separately.

Final read-only proof finds both games ready and unfrozen at committed/durable
revision 66, with zero native faults, no active gesture and the same strict
convergence hash
`16ee0673da985a94f6c60388944f1f5bac5e964019bacf6cc62103e39488094d`.
Both canonical histories contain exactly 66 applied items, no Redo entries and
author counts 34 host / 32 guest: one prior asset creation, three retained
calibration edits, two correct warm-ups and 60 measured gestures. The connected
scene remains available after the failed gate.

Raw samples, every participant's QPC events and frame measurements, warm-up
history, aggregate stage timings and representative timelines are under
`out/preview9-commit-diagnostic-c7-r2`. Final independent public native proof is
`out/preview9-c7-r2-final-native-proof/receipt.json`. No four-player, WAN,
asset-transfer contention, Motel of Melora or release performance pass is
claimed. Limits remain 200 ms LAN confirmation and 100 ms p95 / 250 ms maximum
frame gaps.

## Candidate 8: exact gesture witnesses and return metadata

Candidate 8 DLL is
`3e9ebfa9decf0e46bf710e60d68af355f83cf6c6450fa0877a3b353bc9b4594d`.
`out/preview9-candidate8-source.json` verifies unchanged tracked build inputs
before and after compilation; its private deployment receipt pins the installed
maps and unchanged 4 GB executable. No personal installation was updated.

The native queue now copies metadata independently at gesture Start and Commit.
Previously those queued snapshots omitted metadata, defeating the existing
exact-byte shortcut against the canonical snapshot and causing extra opaque
normalization. Known metadata changes still reject, and legacy missing metadata
remains explicitly unknown. Scoped validation reuse covers the synchronous
capture callback. These changes require new native timing evidence; they do not
turn candidate 7's failed measurements into a pass.

Typed Captain resource/lock values now hydrate atomically from the authoritative
document in Project construction/loading and verified native projection. The
native guard resolves server-backed Captain keys before comparing sidecars.
Metadata restoration sends the verified synchronous native NameChanged message
to refresh the bound title controls while preserving dirty flags and scene
ownership. The queued-message lifetime and creation-return fixes remain intact.

The full current portable repository suite exits 0 in
`out/preview9-portable-current.log`. The current full Win32 runtime fixture
passes 30,059 checks; its executable and source hashes match the separately
verified build receipt (`out/preview9-current-full-test-receipt.json`). The
Project fixture passes 5,683 checks. The real native adapter fixture passes
10,394 checks, and exact ABI verification passes 91 entries. These substitute
native APIs and do not establish real terrain, creation or timing acceptance.
The four-player candidate 8 native run rejected a legitimate Captain before
guest native entry. The first guest log at 05:48:37.177 reports
`The native captain identity differs from its authored sidecar`; guest capture
and restore counters both remain zero. `validate_entry` privately projected the
parsed resource's typed Captain but left its temporary Snapshot sidecars at
their empty defaults before invoking the new strict guard. This is a native
initialization regression, not evidence of a corrupt transported document.
The host's durable revision-0 checkpoint independently contains the expected
Captain key, source bytes and metadata.

No warm-up or performance gesture occurred. All four fault counts remained zero;
the ordinary retirement script returned all four to Galaxy before the owning
runner retired its children. Failure receipts are under
`out/preview9-c8-four-town-bootstrap-inspection` and
`out/preview9-c8-retire/result.json`. The earlier registration timeout was a
separate navigation-harness error: the ordinary bottom-left Login menu control
was mistaken for an active registration dialog. Its bounded visible-menu guard
now has 28 offline checks. Neither failed attempt counts as a benchmark.

Candidate 9 fixes the temporary entry Snapshot in `c4ece38` and consistently
hydrates native snapshot producers. Its strict native fixture passes 10,447
checks, including Adventure Town's actual nonzero Captain, entry validation
from Galaxy, unchanged scene/history and retained forged-key/lock rejection.
ABI verification remains 91 entries. DLL
`4c69fc31744c9b75a3e8779199cabb38a9bf89530a8318fbe1f940d8dfa58aa4`
has before/after build-input and private installation receipts. All four
participants then joined the same revision-0 native Town document, with zero
faults and correct visible bottom title captions. This establishes corrected
entry and title refresh; the candidate 9 gesture measurements and expanded
feature acceptance were separate gates.

## Candidate 9: four-player native commit measurements

The same candidate 9 DLL completed 120 checked native movement gestures in
Adventure Town: 30 per author, four participants, 419 placements, 118 classes
and five acts. All used Act 1 and the same Lead Actor placement. Before each
measured movement, the harness verified the exact native selection and granted
ownership; every release advanced the canonical revision once. Four preceding
warm-ups established one matching history item per author. The native title
caption read Adventure Town on all four participants before those warm-ups.

The direct-LAN measurement gate opened at 06:03:48.350 local after other agents
stopped builds, tests, copies and game diagnostics. Profile impairment was zero;
this was neither a WAN test nor a concurrent bulk-transfer test. Raw operation
QPC events, frame windows, screenshots, aggregates and representative timelines
are under `out/preview9-commit-diagnostic-c9-four-town`.

| Participant | Own submit-to-confirm p95 | Own release-to-confirm p95 | Commit-window largest frame gap p95 | Largest frame gap |
| --- | ---: | ---: | ---: | ---: |
| 95, host | 183.505 ms | 382.422 ms | 277.625 ms | 323.366 ms |
| 96 | 627.925 ms | 810.351 ms | 235.484 ms | 276.284 ms |
| 97 | 691.634 ms | 889.626 ms | 280.410 ms | 281.683 ms |
| 98 | 621.117 ms | 847.365 ms | 277.794 ms | 283.072 ms |

The host passes the 200 ms LAN confirmation threshold. Guests and all frame-gap
gates fail the unchanged limits. The tool exits 1 for those budgets after
retaining all 120 valid samples; this is a correctness pass and a performance
failure, not complete acceptance.

Own capture p95 is 80.7-90.5 ms, supported-change validation 97.3-130.2 ms and
tentative adoption 72.1-88.3 ms. Inverse and author-side native reapplication
remain skipped. Observer apply-work p95 is 206.5-229.5 ms, although the actual
native pose write takes about 1.0-1.6 ms. Total persistence p95 of 71.2-109.7 ms
includes worker preparation, durable storage and main-thread completion polls;
it does not measure only main-thread copying. The durable interval is about
48.8-51.9 ms p95 and includes the completion poll's scheduling delay.

Representative guest timelines contain approximately 96-100 ms from local
submit to host request processing and 87-90 ms from host decision queueing to
guest decode. Existing stages span application queues, UDP send/receive and
outside-frame dispatch, so they do not identify physical network latency.
The source audit finds that outside-frame editor pumping occurs after the
frame's network send pass, while initial reliable data is immediately eligible
for sending; the 100 ms retry floor applies only to retransmission. A bounded
send-only pump is being tested separately. Candidate 9 timings are retained
without attributing the whole delay to that scheduling gap.

All four finish Ready and unfrozen at committed/durable revision 124, with no
active native gesture and zero native faults. Strict native checks converge to
`e52db6cd412e42cead533ffd43daa170bee33d0573b12dfe4d4c2335152bea71`.
Final public proof is
`out/preview9-c9-four-town-final-proof/receipt.json`. The console truncates its
long history output; independently stat-fenced copies of all four durable
stores were therefore opened through the repository Store and wire readers
outside the games. Each checked recovery contains exactly 124 applied history
items, no Redo entries, and 31 per author: one warm-up plus 30 measured moves.
That full proof is `durable-history-proof.json` in the same directory.

No Motel of Melora, WAN, transfer-contention or release performance pass is
claimed. Feature-specific native acceptance remains separate.

## Candidate 10: frozen source, affected checks and bootstrap

Candidate 10 DLL is
`5caceb0a81e216653e23a8a2771b726790572adc51ad5e3189a0c91b3b25561f`.
`out/preview9-candidate10-source.json` pins 187 build inputs and the payload;
the affected-check reconciliation confirms that all current inputs still match.
The full portable repository command
`make -j4 B=out/preview9-final-portable HOSTCXX=clang++ test` exits 0, with
matching critical source hashes in `out/preview9-final-portable-receipt.json`.
The log includes document 2,898, protocol 8,946, binary delta 6,410, project
5,683, store 2,015, UDP/controller 30,094, transport 3,898 and accounting 3,425
checks. RE lint reports 400 symbols used, 3,024 known and zero supported-build
problems. Unsupported frozen-build notices remain informational.

The exact current runtime/executable receipt preserves the full 30,616-check
Win32 pass, including the 24 MiB closure case. The focused Crew integration
passes 1,555 checks again. The native adapter fixture passes 12,458 checks again;
its source/header/helper/executable hashes match its recorded pass and candidate
inputs. Independently rebuilt strict Win32 creation and creation-UI fixtures
pass 489 and 60 checks. The earlier broad network suite remains green against
the unchanged transport implementation. Commands, logs, executable hashes and
source comparisons are consolidated in
`out/preview9-c10-affected-checks/receipt.json`. These are controlled fixtures;
their native scene substitutes do not establish live feature acceptance.

Four owned, hidden and muted native processes then bootstrap Adventure Town
under the exact candidate 10 DLL. All four show the correct native title,
Ready at committed/durable revision 0, empty canonical history and zero native
faults. Strict checks converge to
`5864aafbd5d182c4b465bf6244a0affce68da731525ba847f9846a3f2bf4277b`.
The bootstrap-only driver performs no warm-up or measured gesture and preserves
the direct party for subsequent feature checks. Its result and caption evidence
are under `out/preview9-c10-four-town-bootstrap`. This establishes current native
entry and baseline convergence; candidate 10 commit timing and broader native
feature acceptance remain separate gates.

The diagnostic archive at `out/preview9-candidate10-package` has SHA-256
`120e8cd9df669cd637ef892b944134d856c16eb6f12b2c2a8151a5e0bc6687a8`.
Its `SHA256.json` pins this candidate DLL, protocol 23 and both supported
executable identities, and retains `complete_release_acceptance: false`.
That archive contains the report snapshot from before this appendix and must
be regenerated for a final delivery. No personal installation was changed.

The live four-player Crew capacity-plus action succeeds at revision 1 with one
Crew Undo item and zero native faults
(`out/preview9-c10-crew-capacity/result.json`). Selecting the unique visible
Spice Crawler card then rejects locally with
`A verified local creation is required for shared content versioning`.
All four return Ready at committed/durable revision 1, with zero faults and
strict convergence hash
`11210af23cae60837323b4eab2d65644fceb6f030a9961c4eed95b8f6c6920db`.
The rejection evidence is
`out/preview9-c10-crew-select/rejection-all-four.json`. No rejected after-image
was persisted, and this result does not assert a separately captured raw key
for that selection.

Native projection intentionally resets its Document's coordinator metadata.
The new runtime path incorrectly supplied that empty projected project ID to
immutable versioning instead of the fenced canonical session context. The
existing controlled fixture had retained canonical metadata and missed this
integration error. The narrow candidate 11 correction pins the current context
before collection and tests ordinary empty native project/epoch/revision fields
for both authors. It does not remap legacy custom references already present
in an initial adventure baseline; those retain independent collision checks.
Live selected-Crew versioning remains pending the corrected native candidate.

The corrected runtime is committed as `2e52bd7` and compiled into candidate 11
DLL `2cd5ba3b81f38026a7d7a40624939a592cd2345e72313e81ce8418445f937431`.
Its focused Crew run passes 1,825 checks and the full Win32 runtime run passes
31,130, including the retained 24 MiB closure case. Both native-style empty
project projections now admit the intended version through the current canonical
context. Missing authority rejects before collection, and a scene replacement
during collection produces no restore/apply writes to the replacement scene.
`out/editor-preview9-adoption/crew-context-receipt.json` records matching frozen
candidate runtime/header hashes, fixture executable and actual logs. These
controlled results do not replace the pending live Crew selection check.

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

## Candidate 9 Crew selection failure

After the four-player movement run, ordinary native Crew capacity changed to
one and converged on all four participants. Host selection of the existing
custom creature `Spice Crawler` then failed dependency admission on guests 96
and 98 at 06:40:43.481 and 06:40:43.443 respectively. Host 95 and its
profile-derived observer 97 reached canonical revision 2; the two other
participants retained revision 1 and froze for explicit baseline recovery.
All four reported zero native faults. Public status and exact conflict logs
are retained in `out/preview9-c9-crew-failure-probe/`.

The first rejected key was summary
`40626200!29058782.02D5C9AF`, but this was not merely a derived-cache mismatch.
A read-only, header-stable sample of both profiles' Pollination package indices
and target records found different authoritative BEM bodies under
`40626200!29058782.1A99B06B`: profile 95 has 2,425 bytes, SHA-256
`2e6c4652624f1dda801fc6768e9d92860e4d682a0a26034834cf4c37e9e7849d`;
profile 96 has 7,199 bytes, SHA-256
`7b160a50a6700080833a2a900353cf622bc1295a68d1cd6d0e72c127ed071f5e`.
Metadata, PNG and derived creature-data records also differ. Full offsets,
hashes and comparison evidence are in `offline-source-records.json` in that
directory. The installed-byte conflict guard correctly prevented shadowing a
different local creation; simply omitting generated summaries is insufficient.

The follow-up versions newly selected custom Crew assets into immutable
project keys and rewrites only checked Crew references in an owned snapshot,
preserving the original ownership, flags, lock, order and pose. Original
gesture validation and resource-conflict rejection remain required. Native
candidate 10 Crew Add/Replace, observers, Undo/Redo, recovery and Save/reopen
acceptance remain pending; this failure does not establish those workflows.
The creation helper fixture now passes 489 strict i686 checks, including two
different authoritative BEM bodies sharing one original key, distinct
immutable versions with unchanged model bytes, and atomic missing/invalid-BEM
rejection. Receipt:
`out/editor-preview9-creation-routing/lifecycle-crew-collision.log`.

## Candidate 11 independent creation editors and Building Save

On candidate `2cd5ba3b81f38026a7d7a40624939a592cd2345e72313e81ce8418445f937431`,
the corrected toolbar was visible on all four native Adventure Editors. Ordinary
New actions independently entered Building on host 95, Creature on guest 96,
Vehicle on 97 and Spaceship on 98, without changing revision 1, cast or history.
A bounded ReadProcessMemory-only sample verified each owned native request and
the actual model subtypes BuildingCityHall, AnimalCreature, VehicleMilitaryLand
and UFO. The initial sampler rejected its DLL module-list assumption before
sampling: SporeKit's payload was absent from both native loader enumerations.
The corrected test pins the exact installed candidate and owner-controlled
launch, requires the private executable module and verifies native editor
identity; its optional DLL roster witness is explicitly qualified. The original
failed receipt is retained. Native entry evidence is in
`out/preview9-creation-probe-c11/independence/` and `independence-resumed/`.

Guest 96 explicitly Cancelled and returned with settled native camera and UI
while all three others remained in their creation editors. Participants 98 and
97 subsequently Cancelled independently while host 95 remained in Building.
Guarded movement attempts selected an occluding wall instead of LeadActor; no
movement was delivered, and canonical revision remained 1. These are input
calibration failures, not successful placement edits or ownership failures.
The guest then used the verified native Crew capacity-plus control while the
host remained in Building. That produced one `Edit adventure crew` history
item, revision 2/durable 2, identical canonical hashes on all four controllers
and zero faults. Receipt:
`out/preview9-creation-probe-c11/guest-target/guest-capacity-host-away.json`.

Building Done displayed the native error `You need at least 3 parts to Save`.
The sampler confirmed the explicitly selected BuildingHouse subtype was allowed,
the early owner flag was zero and the computed finish state was 2 with blocking
errors. Some attempted part drags were not accepted; visible construction and
the actual native Done result were required. After three accepted stacked body
parts and the name `Preview9BuildingProbeC11`, normal Done saved and returned.
The canonical cast grew from 118 to 119 classes, placements stayed at 419, and
exactly one `Create adventure asset` item advanced revision 2 to 3/durable 3.
All four strict native checks returned
`4956f918068f4cec43edce06ff147c3c8a6f9df1ca7931b7cc3a69cb4b1e4e7e`.
Their native resource bytes were identical, while each retained its local source
identity. The newer guest Crew change survived the host's return. All four
reported zero faults and visible creation toolbars. Masks, screenshots and
the full final receipt are in `out/preview9-creation-probe-c11/building/`,
including `building-save-returned.json`.

All four then left co-op and returned through ordinary native Cancel and
Quit/discard to Galaxy, phase 0 with zero faults. The runner was not stopped by
the feature probe. See `out/preview9-creation-probe-c11/retirement/`. This proves
four-family entry and independent Cancel, authoring while the host designs,
and this valid native Building Save. Vehicle/Spaceship Save, remote creation
Edit, complete adventure Save/reopen and the Crew custom-content import fix
remain separate pending acceptance.

## Candidate 12 Crew, movement timing and first terrain placement

Candidate `13410f7065550c9a7b525f3a99c11893d57bce01110fbd4da32b00e639a3b8a1`
passed the four-player native Crew capacity, custom Add, host Undo/Redo and
guest lock/Undo/Redo checks. The original conflicting creature was assigned an
immutable project key. All four participants converged durably through revision
7, with exactly three applied Crew history items and zero native faults. Receipts
are in `out/preview9-c12-crew-add/`, `crew-history-host/`, `crew-lock-guest/` and
`crew-history-guest/` with the same `preview9-c12-` prefix. The visible Crew card
portrait was blank, so complete Crew acceptance failed. Offline native inspection
identified the missing owner-group PNG in the immutable dependency closure.
The subsequent sanitizer and its independent tests are documented in the
creation RE notes; a new native candidate must verify the visible portrait.

A clean two-process, two-player Adventure Town run completed two warmups and
30 measured moves per author. Every move converged, both durable revisions
reached 62, and the complete native history contained 31 moves per author with
zero faults. Host submit-to-confirmation p95 was 165.56 ms, within the 200 ms LAN
target; guest p95 was 518.41 ms and failed. The per-gesture maximum frame-gap
p95 was 275.99 ms on the host and 233.61 ms on the guest; observed maxima were
317.02 ms and 234.42 ms. The frame-gap acceptance target therefore failed.
Capture p95 was about 80 ms, supported-change validation 100-105 ms and remote
application 195-198 ms, although the native pose write itself remained below
1.7 ms. No compile, test, deployment or unrelated game calls ran in the measured
window. Raw results are in
`out/preview9-commit-diagnostic-c12-two-town-fresh/result.json`; exact native and
durable history proof is in `out/preview9-c12-two-final-proof/result.json`.
This is direct LAN evidence only, not WAN or four-player acceptance.

After timing ended, normal navigation opened Terraform and selected Basic
Tools. A ground-only press did not start a native stroke or change the draft.
Dragging the verified `Raise Round Terrain` card into clear terrain did start
native history. On release, the host rejected an ambiguous terrain record
identity and both participants froze for explicit baseline recovery at unchanged
canonical/durable revision 62. Neither process reported a native fault. Read-only
inspection retained the host's 181 effect records and the observer's original
180, including every transform and companion column. The failure is retained in
`out/preview9-c12-terrain-card-drag/` and
`out/preview9-c12-terrain-rpm/result.json`. Simultaneous terrain and Terraform
Undo/Redo remain unaccepted until this native discrepancy is resolved and rerun.

The explicit recovery action restored revision 62 in a new epoch and both
participants returned to Ready without faults. The observer's native check
converged, but the host's native check diverged, so complete recovery acceptance
failed. The recipe row counts returned to the original 180 effects and 102
models. `out/preview9-c12-terrain-recovery/` and
`out/preview9-c12-terrain-recovery-rpm/` retain both outcomes. Both participants
then left co-op and used normal native Quit/discard to Galaxy with zero faults
before their owning runner stopped them; see
`out/preview9-c12-terrain-retirement/result.json`.

The full retained comparison identified transform bookkeeping-counter changes
and small position changes on three prior effects and seven prior models.
Native inspection traced position feedback from reconstructed editor proxies
into the recipe during terrain regeneration. Approximate coordinate matching is
not accepted as an identity fix. Follow-up validation must prove the exact
owned input/output relationship, preserve real authored changes and cover
recovery as well as new terrain placement.

## Candidate 13 immutable native projection proofs

The next frozen source removes repeated native parsing and projection when an
exact owned image has already passed native Read and typed validation. The
bounded cache retains copied bytes and values, with generation, project/epoch,
participant, source, binding, captain and metadata fences. It rechecks asset
resolution and logical act/settings identities. An audit rejects a resolver
that changes a server ID during projection, including a change that later
reverts. No native pointer is retained between operations.

Live reuse performs fresh native Write witnesses and post-Write placement,
companion, captain and metadata checks. A mismatch takes full capture; expired
ownership aborts. Placement-only preparation reverses its supported primitive
writes in the actual serialized after image and requires exact recovery of the
validated base bytes. Otherwise it keeps the full opaque-field comparison.
Durable storage still precedes confirmation, and the timing budgets are
unchanged. The one-row `coop.editor asset <class-id:generation>` diagnostic
avoids the truncated complete cast listing without accessing native objects.

The first complete runtime run found a real Save-alias regression: after a
native Save, the guest may retain its local editor resource while the canonical
save root changes. Treating that mismatch as lost ownership froze the replica.
It now requests full capture and never borrows a proof for a different source.
The focused same-revision Save test passes 120 checks, including a subsequent
accepted movement while the guest keeps its native alias. Tentative source and
binding replacement still reject without writing to a replacement scene.

Current strict i686 controlled results are 1,205 focused native proof checks,
13,423 complete native adapter checks, 4,922 tentative/runtime guard checks and
29,632 complete runtime checks. These include both author roles, final-Write and
resolver callback mutations, reordered logical act identities, rejected durable
work, intervening edits, Undo/Redo, teardown and recovery. All existing runtime
cases ran; fewer fake capture/validation calls change the instrumented check
count. The retained 24 MiB dependency stress sent 97,213 host and 20,334 guest
bytes for 20 structural edits plus Save, with cached rejoin at 27,076/754 bytes.

`out/preview9-recent-proof-receipt.json` records source, executable and log
hashes. Its critical source hashes match the candidate 13 input fingerprint.
Runtime logs are under `out/preview9-recent-runtime-`; native proof logs are
under `out/editor-preview9-recent-creation/`. These are controlled fixtures,
not a running-game performance pass. Actual candidate 13 measurements,
terrain regeneration/recovery and the remaining creation acceptance checks
are still required before release.

## Candidate 14 native validation and remaining failures

Candidate `c7ac5660040ee00bda9d3a245cb01bab483dfce7f537014264e939a1d04a057a`
completed the portable repository tests with its 186 binary input fingerprints
unchanged. The final Windows run passed 19 fixture executables and six native
ABI verifiers. Native adapter checks numbered 13,586, including 1,205 focused
projection checks and 163 materialization checks. The independent PNG decoder
passed 2,234,201 checks against the retained custom creature image. Receipts are
`out/preview9-c14-portable-receipt.json` and
`out/preview9-c14-win32/receipt.json`. These results precede the later temporary
terrain-preview Update fix and do not establish that fix's game acceptance.
The ABI verifiers used a private, never-executed original-byte copy derived
from the exact supported LAA executable; installed executables stayed unchanged.

A clean LAN run completed two warmups and 30 movements per author on two
hidden, muted Adventure Town processes. All 60 measured movements converged;
both durable revisions reached 62, with 31 movement history items per author
and zero faults. Host submit-to-confirmation p95 was 185.23 ms, within the
200 ms target; guest p95 was 471.65 ms and failed. Maximum frame-gap p95 was
270.51 ms on the host and 187.57 ms on the guest, with observed maxima of
272.78 ms and 188.80 ms. Both p95 frame targets and the host maximum failed.
Capture p95 was 62.12/65.55 ms and supported-change validation was
74.82/79.35 ms. Native pose application stayed below 1.6 ms, so the retained
profile still identifies validation and scheduling overhead. No compilers,
tests, deployment, hash sweeps or unrelated game calls ran during measurement.
The complete fixture is
`out/preview9-commit-diagnostic-c14-two-town-fresh/result.json`; final exact
native, durable and history evidence is
`out/preview9-c14-two-final-proof/result.json`. Four-player, WAN, transfer and
Motel timing remain pending for the final candidate.

The custom Spice Crawler Crew portrait now renders on both participants.
Host capacity/Add/Undo/Redo and guest lock/Undo/Redo converged through durable
revision 69 with three Crew history items, preserving the earlier placement
history and reporting zero faults. Receipts are
`out/preview9-c14-crew-add/`, `crew-guest-portrait/`,
`crew-history-host/`, `crew-lock-guest/` and `crew-history-guest/`, each with
the `preview9-c14-` prefix. Replacement/removal, other slots, recovery and
Save/reopen are separate pending checks.

A terrain fixture initially clicked an already-selected hammer and thereby
closed the native Basic Tools palette. Its subsequent source was a world
object, not a terrain card, and neither gesture began. That failed calibration
is retained in `out/preview9-c14-terrain-simultaneous/`. After restoring and
verifying the palette, a single real Raise Round Terrain drag opened native
history but produced no appended row or synchronized edit. Both scenes stayed
at revision 62 with 180 effects and 102 models and no faults. The retained
evidence is `out/preview9-c14-terrain-card-drag/` and
`out/preview9-c14-terrain-rpm/result.json`. Native inspection identified a
new preview proxy rejected before the native append routine could allocate its
recipe index. The narrow follow-up preserves strict existing-row Update/Remove
bounds and passes 476 focused and 13,899 complete native checks. See
`out/preview9-c14-terrain-preview-receipt.json`; running-game acceptance is
pending.

Guest 96 then entered the native Vehicle editor independently, placed a body,
cockpit, paired wheels and weapon, and named the creation
`Preview9VehicleProbeC14`. Read-only native sampling verified the allowed
VehicleMilitaryLand subtype, changed model/metadata and finish state zero before
ordinary Done. Saving admitted one new class at durable revision 70, but return
failed its terrain consistency check before applying that revision to the
guest's scene. The creation remains saved locally. The guest froze for explicit
recovery, while the host retained revision 70 and reported that a participant
needed recovery. The host's strict native check additionally reported different
cast properties. Neither process faulted. This is a failed complete Vehicle
workflow, not a successful creation-return result.

The staged construction and save evidence is under
`out/preview9-c14-vehicle-*`. Raw native recipes before and after the failure
are retained separately; all columns and counts are byte-identical for each
participant. Both private checkpoint/journal sets were copied without modifying
their live sources into `out/preview9-c14-vehicle-store/`, with hashes and stable
double-read verification. The scene, generations, history, native checks and
fault counters are in `out/preview9-c14-vehicle-failure-full/result.json`.
Creation-return materialization and the host cast discrepancy must be fixed and
rerun before release. No personal preview payload or 4 GB executable was
replaced, and no main merge was performed.

## Follow-up: one verified projection for native pose captures

The candidate 14 profile exposed a repeated projection even when the live-scene
comparison eventually reported a cache hit: the first validation of a captured
after-image still decoded and projected that image again. The follow-up projects
one owned native Read of the serialized pose image and retains its immutable
value proof. It checks the original marker class, position, quaternion, scale,
relative scale, elevation, sound and destination primitives against that
projection, plus Captain primitives, metadata, exact bindings and fresh resolved
class/Captain/Crew identities. A second native Write closes callback changes.
Only the seven explicitly witnessed placement companion properties may differ;
their presence must stay unchanged. Unrelated structured properties and all
generic class, Crew, terrain and creation changes keep the original full path.

Read-only comparison work can borrow at most two parsed owners within the same
synchronous native drain callback, with a 16 MiB retained serialized-input cap.
The lifetime bound counts borrowed owners even after index eviction. Native
owners die on the game thread at the outer callback boundary; workers and
recovery/network snapshots receive only owned bytes and values. Immutable
projection-cache hits also avoid copying the entire retained document.

The value-proof identity is separate from gesture authorization. It remains
available during a submitted operation and an owned Ready, Preparing, Playing
or Returning context with the same attached project, epoch, local participant
and transport. Save, export, recovery, teardown and incomplete contexts still
withdraw it. Native generation, actual scene owners and source keys remain
independent exact proof fences. The real native adapter test withdraws the
authorizing scope during submission, verifies zero additional Reads for the
existing proof, and confirms native gesture input is still blocked. Exact
terrain materialization witnesses use that read identity too; twelve nonempty
map cases retain normalization during an input hold and retire it on an actual
project, participant, generation or scene replacement.

Local cumulative QPC counters now separate native Read, Write, projection,
asset resolution and supported-change work. These spans are inclusive and must
not be added together. Deferred parsed-owner destruction is outside the inner
supported-change span; actual frame measurements still include that cost.
The performance harness checks zero retained owners at external callback
boundaries and records per-gesture counter deltas outside its measured frames.

The independent review found and corrected an initially overbroad placement
property copy before candidate delivery. Controlled tests now reject an unknown
structured-property change alongside movement, reject normalized live pose or
Captain primitives, and retain opaque marker-act rejection. They also cover
pool holes, both teleporter endpoints, sound companions, quaternion changes,
resolver races, stale scope/generation, nested Read reentry, allocation failure,
history routing and native-owner destruction. The strict actual-i686 run passed
2,122 focused pose-capture checks, 4,671 focused terrain materialization checks
and 20,198 complete native adapter checks, with 48 compile inputs unchanged.
Resolver callbacks which fail while replacing an owner or context produce no
gesture and cannot enter the generic fallback. Source, executable and log hashes are in
`out/preview9-phase1-receipt.json`; logs are
`out/editor-preview9-phase1-{build,focused,full,terrain}.log`.

The retained candidate 14 counter asymmetry also identified a legitimate local
native root alias differing from the canonical resource-envelope key. Its exact
fallback cost is not established by those counters, since the fallback can
itself reuse an existing projection. Separating a pinned local scene identity
from the canonical envelope remains a reviewed follow-up, not part of this
patch. The new capture work still needs measured two/four-player validation;
the failed candidate 14 timing budgets have not been waived.

## Candidate 15 measured commits and terrain append failure

Candidate DLL `fd3626b573bfc8c389d3694881916e39c6406e7db4854ed155e6b652c4a0fabd`
was built with 186 binary inputs unchanged and installed only into the private
lab. The combined strict Win32 runtime suite passed 31,106 checks, including
635 focused model-admission checks and 141 read-scope checks. Receipts are
`out/preview9-candidate15-{source,tests}.json` and
`out/preview9-candidate15-deployment/receipt.json`. These checks complement the
native and creation receipts above; they do not establish release acceptance.

A fresh two-player Maxis Adventure Town run performed two warmups and thirty
ordinary native drags per author. All sixty measured operations completed;
both participants reached durable revision 62 with 62 history items, exactly
31 per author, matching strict native checks and zero faults. The measurement
window excluded builds, tests, payload copies and diagnostic native captures.
The LAN timing gate still **failed**:

| Metric, milliseconds | Host | Guest |
| --- | ---: | ---: |
| Submit-to-confirmation p95 | 160.7572 | 412.2521 |
| Per-window maximum frame gap p95 | 270.6827 | 187.8091 |
| Maximum frame gap | 271.7981 | 223.3531 |
| Gesture capture p95 | 95.4468 | 92.7747 |
| Supported-change check p95 | 71.3120 | 65.0489 |
| Author apply prelude p95 | 54.1166 | 51.3425 |

The immutable pose path was used for every measured author operation, with no
fallback. Parsed owners were zero at external callback boundaries; the peak
was two owners and 121,308 serialized input bytes. Inclusive QPC counters show
ten native Writes per author operation, compared with two Reads and one
projection. Observers performed six or seven Writes. Repeated serialization
therefore remains a measured cost; the counters do not establish that resource
aliasing or Reads dominate it. Timing/correctness evidence is retained in
`out/preview9-commit-diagnostic-c15-two-town-fresh/`,
`out/preview9-c15-two-analysis.json` and
`out/preview9-c15-two-final-proof/`. Four-player and WAN timing remain pending.

The subsequent simultaneous Basic Tools test freshly verified both 1024x768
viewports, the enabled `073933B8` palette card, clear visible rock at both drop
points, and an open native history on each participant before either release.
Both native recipes appended one distinct effect, from 180 to 181, while model
counts remained 102. Submission then failed with
`A native terrain gesture changed an ambiguous record identity`; no terrain
transaction was accepted and editing froze behind the recovery safeguard.
This is a **failed** simultaneous-terrain check, not an accepted rejection test.

Read-only process snapshots retain the actual live recipes. The frozen native
byte diagnostic instead returned the committed 180/102 recipe, so those two
sources must not be confused. The baseline has no duplicate record identities.
Old raw rows show additional derived position differences in three effects and
seven models beyond counter changes. This finding requires an exact native
materialization-witness fix; no float tolerance or guessed identity is allowed.
Evidence is in `out/preview9-c15-terrain-{calibrate,simultaneous,failure-full,rpm}/`
and `out/preview9-c15-terrain-comparison.json`. Both private games returned to
Galaxy through ordinary Leave and native discard, with zero faults, before a
fresh Crew-only draft was opened. The personal installation, shortcut, 4 GB
executable and preview 8 rollback remain unchanged.

## Candidate 15 two-player Crew controls and visible Redo failure

A fresh private Town draft, separate from the failed terrain session, exercised
host capacity, custom Spice Crawler addition, replacement with Spoffit, and
removal. Guest checks exercised replacement with Ms Spoffit, stock addition,
removal, lock and permitted crew size. Author-owned Undo/Redo preserved matching
strict native checks on both participants; the draft reached durable revision
24 with zero faults. Evidence is retained under `out/preview9-c15-crew-`:
`add`, `replace-host`, `history-native`, `replace-guest-native-picker`,
`remove-host`, `add-guest`, `redo-focused`, `remove-guest-hover-native`,
`add-guest-final`, `lock-guest` and `capacity-guest`. This is two-player control
coverage; four-player, concurrent unrelated edits and Save/reopen remain pending.

The driver needed corrections supported by live hit trees and screenshots:
Crew's title is a sibling above its card body, native search retains text and
does not honor the fixture's posted Ctrl+A, multi-word text requires quoting,
and Enter can accept the current selection instead of merely filtering. The
fixture now pins the title/card rectangles, verifies the actual picker title at
its screen position, uses the measured native search-clear button when needed,
and selects the card after filtering without Enter. Twenty-five offline guard
checks pass on Windows and Linux. Failed pre-correction runs remain retained.

Host removal followed by Undo/Redo correctly ends with the member removed.
The resulting generic blue placeholder has no Remove button. Independent
read-only native roster inspection confirmed zero members on both participants
at revision 11 (`out/preview9-c15-crew-rpm/`); this was not missing asset content.

One actual toolbar defect remains. After guest Add and toolbar Undo reached
revision 13, the global `where F006EFA5` diagnostic reported an enabled Redo
button, while `at 685 751` reported the visible button disabled. Runtime history
still advertised Redo available. The first toolbar Redo did not run; a guarded
retry stopped before sending input because the visible button was disabled.
Returning focus to clear Adventure viewport and sending native Ctrl+Y restored
the exact shared Crew at revision 14. The toolbar path is therefore **failed**,
while the canonical author-owned Redo path is verified. Receipts are
`out/preview9-c15-crew-{add-guest,redo-retry,redo-focused}/`. The current native
toolbar refresh uses the first layout lookup, which warrants a scoped owned
layout audit; a cached duplicate is not yet established from these diagnostics
alone. No global UI sweep or relaxation of history ownership is justified.

## Exact before-history pose proof and owned toolbar refresh

Candidate 15's measured serialization cost motivated a narrower supported-change
proof for existing-placement movement. During the synchronous native gesture
callback, the drain already owns an independent before-history resource whose
serialization produced the exact before image. The new optional proof borrows
that owner only within this callback, changes the checked pose and seven known
companions, and requires one native Write to reproduce the actual after bytes
exactly. It neither constructs another native copy nor trusts NativeRead to
preserve unknown authored bytes. A scope guard restores every changed primitive,
the legacy relative scale and exact flag bits before the callback returns,
including after exceptions, nested validation, context replacement or shutdown.
There are no retained native pointers or cross-frame owner lifetimes.

Fresh resolver, Captain, metadata, roster, binding, source, scene-owner,
generation and shared-context checks remain required. Missing evidence or any
non-pose difference uses the existing full comparison; an expired identity
cannot fall back against a replacement scene. The generic prepare path and
durable-before-confirmation behavior are unchanged. As with the existing native
capture path, this relies on NativeWrite's synchronous snapshot contract;
arbitrary opaque mutation after its stream emission is not a newly established
guarantee.

The actual adapter fixtures cover unknown marker-act changes, both directions
of NativeRead normalization, all seven companions, sparse bindings, camera-only
fallback, resolver/context expiry, nested generic validation, allocation denial,
Write failure/exception and shutdown during Write. Every temporary pose branch
checks the original private resource's exact bytes afterward. The focused
success case requires precisely one Write and no native Copy or allocation.

The toolbar audit verified offline that `810650` searches the current
`display+24` UILayout's owning object list through `8102A0`, returning the first
matching window. `GetContainerWindow` instead returns a shared parent layer.
The refresh now collects all Undo/Redo matches only from that exact owned list
and its checked child links, with de-duplication, cycle detection and bounded
work. It samples the two history-availability values once and rechecks the
active session, generation, source, scope, mode, data, display and layout before
writing. It stops after any expiry. An unrelated same-ID window is untouched.
The passive `history_ui_*` status fields report completion and collection counts.
This is a controlled fix for the reported lookup weakness; the actual toolbar
path still requires a fresh native check.

The final strict actual-i686 build passed 248 toolbar checks, 2,720 focused pose
checks, 14,466 terrain materialization checks and 30,839 complete adapter checks.
All 51 compile inputs were unchanged across the build and runs. The executable
hash, inputs and logs are recorded in `out/preview9-phase2-toolbar-receipt.json`.
The prior pre-toolbar checkpoint is retained in `out/preview9-phase2-receipt.json`.
The tests also corrected a fixture-only failure-exit teardown problem: a failing
CHECK now flushes its assertion and exits the isolated process without running
unrelated static double registries. The retained ASan diagnostic is
`out/editor-preview9-phase2-asan.log`; it does not establish a production heap
fault. No measured candidate 16 timing result is claimed. Candidate 15's failed
frame and guest-confirmation budgets remain release gates.

## Candidate 16 actual shared-editor startup hang

Candidate 16 built successfully from 187 unchanged production inputs. Its DLL
is `39fa1c4380a232104596ac205f838b9688297d6fecf18cdaef4c5cdea77882d1`
(89,907,200 bytes). The exact private installation retained the supported 4 GB
executable unchanged. Build and deployment receipts are
`out/preview9-candidate16-{source,build-receipt}.json` and
`out/preview9-candidate16-deployment/receipt.json`.

The fresh two-player Town bootstrap reached matching revision-zero native
documents, then both games stopped presenting frames. The screenshot request
timed out before any gesture or timing sample. Both watchdog reports recorded
zero native faults. The reports are `report-20261102-115918-i95.txt` and
`report-20261102-115936-i96.txt` in the private installation's logs; their report
archives are retained. The failed workflow is
`out/preview9-c16-two-town-bootstrap/result.json`.

Three bounded main-thread samples per process found active CPU use and stacks
through VirtualQuery, native history-toolbar collection and its current-owner
checks. The sample helper temporarily suspended and resumed each owned thread
with balanced counts, without changing thread context or memory. Evidence is
`out/preview9-c16-thread-samples/result.json`. A separate passive read verified
stable mode, display, layout, resource, source and owning object list: each
participant's list contained exactly one non-null object, with no duplicates
(`out/preview9-c16-toolbar-owned-rpm/result.json`). This rules out a large owning
root list in this reproduction; it does not establish the full child topology
or the precise source of the repeated work.

The controlled fixtures did not reproduce this native startup failure.
Candidate 16 therefore fails actual acceptance. No candidate 16 terrain,
toolbar, performance or creation-save pass is claimed, and it has not been
installed into the personal game or desktop shortcut. The next toolbar fix
must bound elapsed work as well as object counts and retain fresh ownership
checks around native callbacks.

## Candidate 17 existing-call toolbar correction

The completed passive native inventory found 429 windows and 428 child edges
on each participant, with no cycles or errors and exactly one Undo and one Redo
in the current layout. Their parent is `0578EC50`. Thus the global duplicate
reported by `where` did not establish a duplicate within the current layout.
The topology and exact interface evidence are retained in
`out/preview9-c16-toolbar-{cast,topology}-rpm/result.json`.

Native analysis then identified the actual history-availability mismatch:
`UpdateDisplay ED6440` inlines the native history cursor checks before setting
both button flags. It bypasses the hooked HasUndo/HasRedo functions and can
overwrite an outside-frame flag refresh. There is no proven outside-render
boundary for this updater. The recurring full-tree collector was removed,
including its additional UI calls; no timed partial traversal is shipped.

The replacement scopes the original updater and changes only the enabled
argument of its two existing `964560` SetFlag calls, at exact returns
`ED65C1` and `ED660B`, for flag 2 and the matching window control ID. It uses
fresh cached shared-history availability and pending-cancellation state.
Project, epoch, participant, generation, mode, data, resource, source, display,
layout and layout-owner checks fence the read-only callbacks. Nested updaters
restore their prior stack scope. Every other call retains its original argument
and forwards exactly once. The hook adds no window lookup, UI getter, setter,
allocation or native history-cursor mutation. Its memory-query cache ends before
every callback. The 96-entry native ABI receipt is
`out/preview9-c17-toolbar-abi.json`.

The queue audit also found a concrete feedback route: a frame stall enables the
100 ms keepalive task, which calls the editor tick; a slow fence can leave its
queue-time 200 ms interval already expired when that keepalive runs. The kit
drains up to 256 outside-frame tasks including newly queued work. The fence now
retains its queued flag through the whole callback and timestamps completion,
preventing both reentrant duplication and immediate keepalive replenishment.
A deterministic controlled regression advances the clock during the callback
and verifies that the queue drains before the next eligible fence.

The final strict actual-i686 fixture passed 368 focused toolbar checks and
30,959 complete native checks, with all 41 compiler and Make inputs unchanged.
Coverage includes the inline-empty-native-history/shared-Redo case, wrong
callsite, mask and control ID, unavailable authority, cancellation changes,
layout-owner and other identity replacements, nested callbacks, exceptions,
exact native-call forwarding and zero added UI/layout calls. Evidence is in
`out/preview9-c17-fixture-receipt.json`. Candidate 17's second production build
passed its 187-input integrity check (`out/preview9-candidate17-build-receipt.json`);
the earlier attempted build was retained undeployed after detecting the final
layout-owner fence change.

The exact candidate then passed private two-player Adventure Town startup with
both games responsive at revision 0, matching strict native checks and zero
faults. A guest changed crew capacity and used the visible native toolbar for
Undo and Redo. Both participants finished Ready at revision 3, durable revision
3, with one correctly attributed crew history item, matching native bytes and
strict check `89665d601d29e45c3007d262427c13e72f88b6d4595148e156b9c998c19c7419`.
The native history-update and Undo/Redo override counters advanced with zero
expired scopes. The visible toolbar result and screenshots are retained in
`out/preview9-c17-crew-capacity-toolbar/result.json`. This is a functional
two-player pass; the unchanged frame/confirmation timing gates, terrain and
four-player acceptance remain pending.

## Candidate 17 simultaneous terrain rejection and inexact restoration

After the successful Crew control check, both participants independently located
Lead Actor 7, opened BasicTools and held a measured Raise Round Terrain stroke
before either release. Both native histories reported an active gesture while
the shared revision stayed 3. Fresh screenshots and hit trees were reviewed in
`out/preview9-c17-terrain-calibrate/`; the failed simultaneous workflow is
`out/preview9-c17-terrain-simultaneous/result.json`.

Both strokes were rejected before Session submission. Capture, projection and
terrain identity checks reached the final strict unsupported-change comparison;
the earlier ambiguous-counter error was not the failing stage in this run.
The optional diagnostic printed `decoded=0 size=0/0`. These are diagnostic
extraction buffers, not the serialized snapshot lengths. Retained Town native
data contains a valid decoded DBPF body larger than that diagnostic's 1 MiB cap.
The actual unsupported difference still needs a bounded larger diagnostic.

The rejection message lost its detail because a successful scene-owner query
cleared the shared error string. Candidate 18 preserves the original reason and
raises only the diagnostic extraction cap to 4 MiB. Its fixtures retain strict
comparison rules; this does not enable the rejected terrain operation.

The final games were responsive, Ready, gesture-free, and still at durable
revision 3, with 180 effects and 102 terrain models. However, **both strict
native checks diverged after restoration**. The log identified placement
4294967549: its position, scale and property counts matched, while quaternion
components differed by one or two float ULPs. Expected bits were
`BF16ACF6/BE2E89B5/3E8838CB/3F3E8042`; observed bits were
`BF16ACF8/BE2E89B6/3E8838CB/3F3E8043`. Passive inspection of native slot 253
found the same observed bits in live marker memory. The helper does not decode
the adapter's logical binding map; that limitation is retained in its receipt.

Evidence is `out/preview9-c17-terrain-failure-full/result.json`,
`out/preview9-c17-terrain-properties-after/result.json` and
`out/preview9-c17-marker253-rpm/result.json`. These results do not establish
which native call changed the quaternion. No tolerance or additional supported
field is justified yet. Restoration needs a settled strict verification before
local editing is released; Ready flags alone are insufficient. Candidate 17
therefore fails terrain and restoration acceptance and remains private.

## Candidate 18 focused terrain reproduction

Candidate 18 (`236d4f9526deea31ec1c0de443a2964e2e84f730edf3f26541a7dc6055bc6d6a`)
passed fresh two-player Town bootstrap at revision 0, with no preceding Crew
changes. Both measured BasicTools histories again opened before either stroke
was released, and both strokes were rejected before Session submission. The
preserved rejection identifies normalized offsets 21624 and 21632. The larger
diagnostic successfully decoded both 1,229,480-byte DBPF bodies and found them
byte-identical. The differing bytes are outside that body, within property
`040539D4` in the native PropertyList prefix.

Native writer and reader analysis establishes that this exact property's
type-None, flags-zero, 16-byte placeholder payload is uninitialized storage;
the typed key-array reader rejects it before reading that payload. Candidate 19
preserves the before-image payload only when both properties have that exact
shape. Presence, type, flags, typed arrays, other properties and the DBPF body
remain strictly checked. Controlled artifact regressions do not establish an
actual successful terrain commit.

Both Candidate 18 games remained responsive at durable revision 0 with no
history item and zero native faults, but both strict native checks again
reported placement divergence after rejection restoration. Candidate 18 remains
a failed terrain/restoration candidate. The additional Candidate 19 restoration
hold must prove the settled live scene before releasing local editing.

Evidence: `out/preview9-c18-two-town-bootstrap/`,
`out/preview9-c18-terrain-calibrate/`,
`out/preview9-c18-terrain-simultaneous/result.json`, and
`out/preview9-c18-terrain-failure-full/result.json`. The private games and payload
are separate from the unchanged personal preview 8 installation.

## Current portable regression run

`make -j4 B=out/editor-preview9-c19-portable HOSTCXX=clang++ test` exited 0.
`out/preview9-c19-portable-tests.log` includes document 2,898, protocol 8,946,
project 5,683, cast 393, authoring extensions 328, terrain 414, binary delta
6,410, store 2,015, UDP/controller 29,322 and accounting 3,425 checks. Tool,
transport, package and supported-build RE checks also pass; RE lint reports
405 used symbols, 3,031 known and zero supported-build problems.

This run covers the portable code and exact-shape placeholder handling. It
does not compile the Windows Runtime/Native adapters, which were receiving
additional callback-fence changes independently. Their separate affected
Win32 results and a frozen production build are required before native
acceptance or delivery. The personal executable and preview 8 DLL still match
their original hashes; all 19 retained rollback payload hashes were verified
in `out/preview9-delivery/c18-personal-unchanged.json`.

## Candidate 19 native terrain and restoration result

The private diagnostic DLL
`663dbb8dce28bb1ae6c2ea3c44934c4ba3f5bb894d346a2987ac4da46c6916e6`
built with all 187 production inputs unchanged. Fresh two-player Town bootstrap
passed at revision 0 with strict convergence and zero faults. Both measured
BasicTools strokes opened native histories before either release.

The placeholder rejection no longer occurred. Both gestures instead reached
the placement ownership guard and were rejected for missing placement
ownership, before Session submission. No shared transaction or history item
was created. The original gesture's exact changed placement fields still need
a bounded diagnostic; rollback orientation logs alone do not identify that
original delta. The placement ownership guard remains intact.

The new native checkpoints narrow the restoration orientation drift: there
were no mismatches after parsing, native restoration or act selection. The
first mismatches appeared after the subsequent terrain load/rebuild sequence,
on markers 253, 265 and 293. Marker 253 again changed from canonical
`BF16ACF6/BE2E89B5/3E8838CB/3F3E8042` to
`BF16ACF8/BE2E89B6/3E8838CB/3F3E8043`. This identifies the load/rebuild boundary;
it does not yet isolate an instruction within that sequence.

The new restoration hold correctly refused to report success. Its settled
verification found differing native act/captain settings, froze the recoverable
draft at durable revision 0, and notified the guest. Both participants retained
zero native faults. This passes the failure-detection behavior for this attempt,
while terrain commits and successful restoration remain failed acceptance.

Evidence: `out/preview9-candidate19-build-receipt.json`,
`out/preview9-c19-two-town-bootstrap/`, `out/preview9-c19-terrain-calibrate/`,
`out/preview9-c19-terrain-simultaneous/result.json` and
`out/preview9-c19-terrain-failure-full/result.json`. Both games left the party
and returned normally to Galaxy with zero faults before their owned runner
exited successfully (`out/preview9-c19-terrain-retirement/result.json`). No
personal payload or shortcut was changed.

## Candidate 20 exact rollback and remaining terrain-cache failure

The diagnostic DLL
`66e4a786d9d3143d00a275a4952c2fb9179a7b4e1a8bd4fe4e217d43ed4a348c`
built with 188 unchanged production inputs. The current strict fixtures passed
45,572 Native checks and 40,926 Runtime checks; their exact input receipts are
`out/preview9-c20-native-rotation-receipt.json` and
`out/preview9-c20-placement-runtime-receipt.json`.

Initial bootstrap was interrupted by the native offline/registration message
before shared editing. After that message cleared, the retained second attempt
passed two-player Town bootstrap at revision 0, with zero faults. Evidence is
`out/preview9-c20-two-town-bootstrap/`, `out/preview9-c20-startup-popup-r2/` and
`out/preview9-c20-two-town-bootstrap-r2/`; the failed attempt is not a terrain
or performance measurement.

The measured simultaneous strokes again reached the unchanged placement
ownership rejection. Bounded owned-value diagnostics now identify the original
delta: only rotations on markers 188, 253, 265 and 293 changed, with identical
before/after bits on both roles. The proposed next change requires exact native
rebuild provenance; this result does not justify general rotation tolerance or
removing placement ownership.

The restore-only quaternion repair succeeded for this attempt. Both ordinary
native checks returned
`CONVERGED b387680e07950034d14613b3d99d5dee27c89e7433464348831e451004febd52`.
However, the stricter active-tool draft capture still failed and correctly kept
the recovery hold. Its diagnostic identifies Planet object `47244640256:1`,
missing property `00B2CCCB`, rather than a Captain field. This property is the
native PropertyList parent reference. The serialized resource retained it, but
the freshly parsed terrain-load input had no resolved runtime parent; native
TerrainLoad consequently cleared that parent in its tool cache. Ordinary
resource convergence does not establish active-tool readiness.

Both participants remained at durable revision 0, history count 0 and zero
faults. No terrain transaction was accepted. Evidence is
`out/preview9-c20-terrain-calibrate/`,
`out/preview9-c20-terrain-simultaneous/result.json` and
`out/preview9-c20-terrain-failure-full/result.json`. Both games subsequently
returned normally to Galaxy with zero faults, and the owned runner exited 0
(`out/preview9-c20-terrain-retirement/result.json`). Personal preview 8 remains
unchanged. Native parent hydration and gesture provenance are the next scoped
fixes; full Terraform, Save/reopen and timing acceptance remain pending.

### C19 controlled rejection-restoration hold

Strict actual-i686 Runtime fixtures pass 40,609 full-suite checks, 7,312 focused
restoration checks and 1,508 captured-identity checks. All 71 pinned compiler
inputs remain unchanged; receipt `out/preview9-c19-runtime-receipt.json` retains
the source hashes, executable and build/test log hashes, including the two
initial O0 findings and their corrected regression cases.

Rejected edits now hold only their local scene while canonical validation,
history and durability continue. Two real UDP controllers verify foreign
placement and structural commits from either role while the other scene stays
unready, then one restoration of the latest revision. Local input, Save, Test
and scene-ready reporting resume only after view/selection restoration, a
later frame and strict typed/opaque live-scene comparison. Failed comparison
freezes the recoverable draft; expiry cannot authorize a replacement scene.
The full suite also retains large-closure Save/recovery, cached join, creation
editor, playtest-return, cancellation and teardown coverage.

These are controlled Runtime tests with native doubles. The real adapter has
separate native callback-fence fixtures. The actual C19 attempt above failed;
these controlled successes do not establish native acceptance. The failed
actual C18 evidence also remains unchanged; no quaternion tolerance or
authored-field comparison was loosened.

### C20 controlled recovery and owned placement diagnostics

The latest strict actual-i686 Runtime suite passes 40,926 checks, with 7,540
focused restoration/diagnostic checks and 1,508 captured-identity checks.
All 72 pinned inputs remain unchanged in
`out/preview9-c20-placement-runtime-receipt.json`. The preceding recovery-only
checkpoint and its 40,689 full checks remain separately recorded in
`out/preview9-c20-runtime-receipt.json`.

The failed hold's exact original owner can recover from published Frozen
state while retaining every source, generation, project, epoch, participant
and transport check. A real UDP case keeps both old checkpoints while the
new recovery scene is unready, then requires strict publication and guest
rejoin. Replacement-owner cases still perform no recovery writes.

At the existing placement ownership rejection, bounded diagnostics now show
actual changed owned fields, raw value words, serialized snapshot marker IDs
and semantic `terrain_delta`. They add no native reads, permission bypass or
normalization. The original error survives a failing logger. Actual C19
terrain and restoration failure above remains valid; actual C20 acceptance
still needs the next frozen native payload and focused reproduction.

### C21 owned terrain-parent hydration and actual rejection recovery

Parent hydration is committed in `c2d340e`. Strict actual-i686 fixtures pass
54,041 checks, including 8,445 parent checks, 8,688 restore-fence checks,
14,472 terrain-materialization checks and 1,293 rotation-restoration checks.
All 43 compiler inputs remain unchanged in
`out/preview9-c21-native-parent-receipt.json`. The three owned-input paths
resolve the serialized native parent before any live restoration, explicit
terrain Load, or deferred-preset history/Build mutation. Export remains strict;
the helper neither supplies missing exported fields nor edits authored data.

The private C21 production DLL is
`a10103c03f20c05aa9fa74c92ea4bf451695458f4a31fa7051a32c3bd585b558`
(90,415,104 bytes), built with all 188 production inputs unchanged. Receipts:
`out/preview9-candidate21-source.json`,
`out/preview9-candidate21-build-receipt.json` and
`out/preview9-candidate21-deployment/receipt.json`.
The source includes Native `76b76be66e8627af0305b96a1ffe80689b276ed96dc8525a2563897fff055455`
and Runtime `bc9aafb7bb7e766e616a2ba127ba4fd1d29ed61ccfdff78e77ae3906a3ac6908`.
The private exact 4 GB executable is retained; personal preview 8 is unchanged.

Both fresh hidden/muted participants loaded Adventure Town through its native
browser, joined at revision/durable 0 with history 0 and no faults, then used
freshly inspected Raise Round Terrain cards and clear-ground targets. Both
native histories were held before either release. The known incidental
placement-rotation changes still rejected each stroke, as expected before the
separate provenance fix. Both scenes then restored and remained Ready in phase
4, with revision/durable 0, history 0 and zero faults. Their semantic planet
fields match their pre-press values, and both strict checks report
`CONVERGED 0cd414c54a81ceabdb461eaa36f721398a6557963018ad99f697af5d0b1d1f8d`.
This exercises the final active-tool-cache comparison that failed in C20.

Evidence: `out/preview9-c21-two-town-bootstrap/result.json`,
`out/preview9-c21-terrain-calibrate/` and
`out/preview9-c21-rejected-terrain-recovery-r2/result.json`.
The first recovery helper reached Ready but refused its own diagnostic `log`
command; that incomplete fixture result is retained at
`out/preview9-c21-rejected-terrain-recovery/result.json`. A second fresh pair of
strokes used the corrected bounded diagnostic allowlist and passed. No failed
artifact was rewritten. This is rejection-recovery acceptance only; it does
not establish simultaneous Terraform transaction acceptance or timing targets.

### C21 movement timing after terrain rejection and fresh-scene follow-up

With compilers and other fixtures paused, both roles completed 30 measured
ordinary movement releases in Adventure Town after the rejected terrain
workflow above. Two warmups plus 60 measured releases produced durable revision
62, history count 62, no faults and strict native convergence on
`98e8df2ead0eb96c06a97a8c732b7aeb2e2cb9228721051d7f9ad9392ed7094b`.
Both subsequently returned normally to Galaxy. Evidence:
`out/preview9-commit-diagnostic-c21-two-town-r2/result.json`,
`out/preview9-c21-two-analysis.json`,
`out/preview9-c21-movement-final-proof/result.json` and
`out/preview9-c21-movement-retirement/result.json`.

| C21 post-terrain metric | Host 95 | Guest 96 |
| --- | ---: | ---: |
| Submit-to-confirmation p95, ms | 150.3827 | 474.0727 |
| Release-to-confirmation p95, ms | 344.7559 | 667.3927 |
| Capture p95, ms | 78.0423 | 74.8518 |
| Supported-change validation p95, ms | 108.2777 | 108.6537 |
| Author apply prelude p95, ms | 35.6246 | 35.5112 |
| Measured frame-window maximum p95, ms | 273.9731 | 228.3983 |
| Absolute measured frame maximum, ms | 278.4468 | 231.9393 |

The LAN guest-confirmation target and both frame p95 targets fail; the host also
exceeds the 250 ms maximum. Author releases use 11 native Writes, two Reads and
two projections, with zero pose-capture attempts. Source inspection explains
this as the deliberate fast-path exclusion when `terrain_grounding` is
nonempty following terrain restoration. These results are not a fresh-scene
comparison of the pose optimization. The first attached timing helper omitted
the Build-tab precondition and timed out during Locate without starting a
measured gesture; that setup failure remains in
`out/preview9-commit-diagnostic-c21-two-town/result.json`.

A normal native reload then started a new project without any Terraform input.
Its warmups and four host samples completed, using the intended pose/opaque
proof path (one pose-capture hit, nine Writes and one projection per author
release). The next ordinary-input attempt invoked ToolAction 11 and was
correctly blocked by structural ownership. The helper timed out waiting for a
movement transaction. Both participants remained Ready, unfrozen, at durable
revision 6 with zero faults and strict convergence on
`672451c9466777ef28324372c21bc4017535a61dbae882405f673f86fdb41672`.
This is an incomplete fresh performance run, not a passing timing result or a
durability failure. The exact input cause remains unverified; the native clone
affordance and physical modifier state require a better fixture guard.
Evidence: `out/preview9-c21-fresh-movement-bootstrap/`,
`out/preview9-commit-diagnostic-c21-two-town-fresh/result.json` and
`out/preview9-c21-fresh-movement-failure/`. Both games returned normally to
Galaxy with no faults, then the owned runner exited 0 before C22 deployment
(`out/preview9-c21-fresh-movement-retirement/result.json`).

### C22 exact rotation capture advances, but actual terrain inverse fails

The q-capture checkpoint `8e3b2fa` passes 55,775 strict actual-i686 Native
checks, including 1,465 focused capture checks, with 59 inputs unchanged
(`out/preview9-c22-native-q-receipt.json`). It brackets verified Rebuild calls
with bounded exact placement/source/revision witnesses. An owned native copy
must serialize identically before normalizing witnessed rotation values, and
both original and normalized bytes close again after projection. No quaternion
tolerance, placement permission, or unrelated authored field was relaxed.

Private C22 DLL
`b8f506627177d9e306246daa1db9781c03d4d43a4d9d183161ecd2605365f92c`
(91,568,640 bytes) built with all 191 inputs unchanged. It includes the guest
unused-envelope optimization, whose preceding strict Runtime suite passed
1,306 focused and 42,086 full checks, and frozen syntax-checked capability
wire/session scaffolding. Runtime capability publication was not integrated in
this checkpoint; this actual test used the native complexity default. Receipts:
`out/preview9-candidate22-source.json`,
`out/preview9-candidate22-build-receipt.json`,
`out/preview9-candidate22-deployment/receipt.json` and
`out/preview9-c22-replica-persistence-receipt.json`.

Both fresh participants loaded Town and strictly converged at revision 0 with
zero history/faults. Root reviewed both fresh camera/card/ground screenshots.
The driver held both native stroke histories before releasing either. The
attempt failed: host 95 accepted its own sequence 1 as one Terraform item at
durable revision 1, while guest 96 remained at revision 0 and froze for explicit
baseline recovery. The original unowned-placement rejection did not occur.
The first reported validation error instead compared marker 188's derived
rotation against the committed rotation. Host checked restoration subsequently
finished Ready at revision 1; this does not establish party convergence.

Final strict checks are host
`f403866dd805b21d397a596c01476141ccc5fe4b6a721b679bf8c8cebb8bfaaf`
and guest baseline
`b56f1117aa55f00fc650d9ceb03062e7055ebb917d11f239fc73cfc32fefe96e`.
Both report zero native faults. Host inspection also reports an invalid terrain
record/order/revision, requiring a separate projection-revision audit. The
guest's restoration hold expired after the shared recovery requirement; its
retained recovery state was not discarded.

Evidence: `out/preview9-c22-two-town-bootstrap/`,
`out/preview9-c22-terrain-calibrate/`,
`out/preview9-c22-terrain-simultaneous/result.json` and
`out/preview9-c22-terrain-failure-full/result.json`. Both games returned normally
to Galaxy with no faults and runner 77086 exited 0 before any subsequent
deployment (`out/preview9-c22-terrain-retirement/result.json`).

Follow-up controlled reproduction finds the actual generic inverse route
differs from the previously tested all-targets prepared application. Generic
apply can compare normalized fields as unchanged, retire the rotation ticket,
and leave derived live rotations; it also has no terrain-roster inverse branch.
The new regression reproduces generic apply returning success followed by
strict validation failure (`out/preview9-c22-q-inverse-o0-focused.log`). The
planned repair uses checked restoration only from an independently canonical
artifact whose complete document matches the requested inverse target, with
the original scene/scope/generation fences. Actual acceptance remains pending,
including the zero-placement terrain case. Personal preview 8 remains installed.

### C23 controlled checks, retained failures, and C24 follow-up

Private C23 built successfully as
`35411cc3e3cdccfbf2f4f54677a74ba78bef0ba029c10e9db1134876fa48c541`
(91,841,024 bytes), with all 191 production inputs unchanged. It was not
deployed. Build and source receipts are
`out/preview9-candidate23-build-receipt.json` and
`out/preview9-candidate23-source.json`; neither establishes release acceptance.

Controlled Native tests reproduce the real generic terrain inverse, including
its independently canonical artifact, rather than substituting prepared pose
application. Restoration now covers the terrain recipe and derived placement
rotations together. Projected native terrain rows use revision zero to match
their projected document; canonical row revisions and checked history remain
unchanged. Focused strict checks pass for rotation inverse (3,028), terrain
materialization (16,312), and complexity (648).

The first optimized Native full run failed the existing held-preset assertion
`!held_terrain_preset && gets == 1`. The unchanged executable subsequently
passed its full run (59,511 checks), parent-focused run (8,445), and O0 full
triage (59,511). The initial failure remains in
`out/preview9-c23-native-full.log`; its cause is not established by those
reruns. The fixture previously sampled real operating-system Escape state.
C24 isolates that fixture input and adds an explicit Escape-down cancellation
case. This is not evidence that physical input caused the original failure.

Strict C23 Runtime focused runs pass complexity (988), captured identity
(2,044), and rejection restoration (9,652). Its full run faults later during a
disconnect case. Inspection identifies a fixture callback capturing two local
stack values that remained registered after their lifetimes ended. The
subsequent fixture repair restores that callback before returning and clears
callback storage between fixture parties; it does not change production
transport code. The failed exact executable, logs, 75 unchanged inputs, and
failure status are retained in
`out/preview9-c23-runtime-complexity-receipt.json` and
`out/preview9-c23-runtime-preserved/`.

C24 also closes two reviewed callback edges before actual testing: it copies
the required dependency validator before inverse callbacks, and checks the
whole canonical revision throughout restoration so a disjoint accepted terrain
edit cannot be overwritten by a stale artifact. Extended Complexity must
compare a fresh local tuning capability with its last authenticated cohort
sample at authorization; its native ratio callback compares an already
captured capability without recursively invoking native getters. Native
default forwarding remains unchanged. These changes still require final
strict fixture receipts and actual two/four-player checks.

The personal preview 8 payload, shortcut bytes and resolution, exact 4 GB
executable, and all 19 retained rollback payload hashes were checked again in
`out/preview9-delivery/c23-personal-unchanged.json`. No personal payload was
updated by these candidate checks. Actual terrain acceptance, performance
targets, remaining creation-editor saves, full Terraform controls, complexity
boundaries, and lab smoke remain open.

### C24 controlled gates pass; actual inverse needs asynchronous settlement

Native commit `bef0fdc` and Runtime commit `ee9b35c` build private C24 DLL
`37a9c1858080bcb1a41350a9b24d571d2e4dd06d778913c3d2ff53099a1227e3`
(91,901,952 bytes), with all 191 production inputs unchanged. Strict optimized
i686 Native passes 61,684 full checks and focused rotation 3,294,
materialization 17,981, complexity 871, with 45 compiler/Make inputs unchanged.
Strict optimized Runtime passes 61,380 full checks and focused complexity
1,209, captured identity 2,044, rejection restoration 9,652, with 75 inputs
unchanged. O0 Runtime passes 70,742 checks. The repaired test callback no
longer crashes the optimized disconnect sequence. Receipts:
`out/preview9-candidate24-build-receipt.json`,
`out/preview9-c24-native-receipt.json`, and
`out/preview9-c24-runtime-complexity-receipt.json`.

The complete portable `make test` run also passes, including 9,218 protocol,
5,683 project, 29,850 Session/UDP checks, package checks, and native-symbol
lint with zero supported-build problems. All 191 production inputs remain
unchanged across that run. Evidence:
`out/preview9-c24-portable-receipt.json` and
`out/preview9-c24-portable-tests.log`. These are controlled checks, not game
or timing acceptance.

Only the private installation received C24. Both participants loaded Town,
strictly converged at revision 0 with history 0 and faults 0, and showed the
correct native adventure title. Root inspected both fresh camera/brush/ground
screenshots before the measured Basic Tools reproduction. One initial recipe
preflight failed reading a UTF-8 BOM before any game connection; the corrected
fresh recipe is UTF-8 without a BOM and retains the original preflight log.

The actual simultaneous attempt still fails. Both native histories were held
before release. The new full canonical inverse restores the recipe and
rotations, but Runtime immediately calls its ready-only scene-owner capture
while native terrain and deferred local view reconstruction are still pending.
It reports the original scene identity expired and freezes both participants
before any transaction is accepted. Both retained states are revision 0,
durable 0, history 0, faults 0, and strict digest
`2cd5d2916840513914c4ded027e276f398d1b9f1949003ee7457ece84bd564e3`.
This improves the inverse itself but is not simultaneous Terrain acceptance.
Evidence: `out/preview9-c24-two-town-bootstrap/`,
`out/preview9-c24-terrain-calibrate/`,
`out/preview9-c24-terrain-simultaneous/result.json`, and
`out/preview9-c24-terrain-failure-full/result.json`.

The next repair must separate exact raw owner identity from asynchronous
readiness, retain one owned unsent gesture, and wait for a later actual frame
and strict baseline proof. An unrelated new canonical head must coalesce
through checked restoration without discarding the original guarded stroke.
Replacing a scene must still reject without stale writes. The ready-only
`capture_scene_owner` contract must remain unchanged.

Both games returned normally to Galaxy with faults 0, then runner 64266
completed exit 0 before another deployment
(`out/preview9-c24-terrain-retirement/result.json`). The personal preview 8
installation, shortcut, exact 4 GB executable, and rollback remain untouched.
C24 is not an accepted release.

### C26 terrain continuation and color controls; C27 checked Redo

The guarded C25 diagnostic build was not deployed: review found a nested
native callback could begin a foreign durable commit before its canonical
revision was published. C26 retains the original immutable request and lease
tokens during asynchronous inverse settlement, waits for that publication,
then coalesces the latest canonical scene before checking and submitting the
original guarded gesture. It uses raw owner identity only while busy;
`capture_scene_owner` retains its ready-only contract. Native view readiness
also excludes deferred camera settlement without blocking its internal drain.
The candidate continues renewing its existing leases while waiting.

Strict optimized i686 Native passes 61,729 checks, including 3,179 focused
view checks, with 45 inputs unchanged (`out/preview9-c25-native-receipt.json`).
C26 Runtime passes 67,495 full checks and focused inverse 6,008, identity
2,044 and rejection 9,657 with 76 inputs unchanged
(`out/preview9-c26-runtime-receipt.json`). The const-State validator removes
the unnecessary whole-State copy in wire validation while preserving the
Coordinator reset semantics. Its Linux/i686 document, protocol, project and
Session fixtures, byte-identical old-wire cases and allocation-failure probe
are retained in `out/preview9-const-state-proposal/receipt.json`.

Private C26 DLL
`7c2ba00e8a54f71daefa653e6cef016399036038fdbf57ca5a3470ec5e39d803`
(91,467,264 bytes) has 191 unchanged production inputs. Its fresh two-player
Town test accepts both distinct Basic Tools strokes, one durable transaction
and history item per author, after both native histories are held before
either release. Host Undo preserves the exact guest record; guest Undo
restores the exact baseline. Guest Redo then fails ownership without a
freeze: both participants remain ready at revision/durable 4, faults 0 and
strict digest `32eaca69586bb8b2fdc37c9e167b6004cec4226c0ee72599b96c9786f9674530`.
This is a failed full history sequence, retained in
`out/preview9-c26-terrain-simultaneous/result.json` and
`out/preview9-c26-terrain-failure-full/result.json`.

All four normally visible Color controls pass on C26: host atmosphere,
guest water, host cliff and guest beach. Each ordinary palette selection
produces exactly one canonical history item, changes its intended color
field, and passes Undo, Redo and final Undo against exact planet field maps.
Both participants strictly converge with faults 0. Evidence:
`out/preview9-c26-{atmosphere,water,cliff,beach}-color/result.json`.
The serialized override checkbox is natively hidden; it was not enabled or
counted as a tested public control. These passes do not cover gradients,
environment, flora or the rest of Terraform.

C27 fixes the actual Redo failure by acquiring a Terrain scope for every
terrain history entry, including an absent record being restored. Core
admission is unchanged: only the exact retained history identity can be
leased, and a foreign lease still denies the operation. Strict optimized
Runtime passes 67,796 full checks and 6,257 focused history/inverse checks;
O0 focus passes 6,252 with 76 inputs unchanged. The fixture first retry raced
an earlier asynchronous denial; the retained failure precedes a fixture
change that waits for that denial before retrying. There is no production
throttle change. Receipts: `out/preview9-c27-runtime-receipt.json` and
`out/preview9-absent-terrain-core/receipt.json`.

Private C27 DLL
`e2efa52b0db07668e45f0b70455185f2eaa215fbf7c54cada3527b9edf868299`
(91,468,800 bytes) has 191 unchanged production inputs. Its fresh two-player
Town sequence now passes: both held histories, two accepted strokes, both
author Undos, both Redos restoring exact record identities, payloads and
order, and final own Undos restoring the baseline at revision/durable 8.
Both participants strictly converge with faults 0. Evidence:
`out/preview9-c27-two-town-bootstrap/`,
`out/preview9-c27-terrain-calibrate/`, and
`out/preview9-c27-terrain-simultaneous/result.json`.

C26 returned normally to Galaxy with faults 0 and runner exit 0 before the
private C27 deployment. Prior candidate DLLs and failure evidence remain
retained. The personal preview 8 installation, shortcut, exact 4 GB executable
and rollback payload are unchanged. C27 is not an accepted release: final
performance targets, expanded creation and Terraform acceptance, complexity
boundaries and lab smoke remain open.

The subsequent C27 host-away case accepts a guest Basic Tools stroke and its
Undo, Redo and final Undo while the host remains in the native Creature
Editor. Canonical/durable revision reaches 12 without returning the host or
changing the guest's unrelated planet fields. The normal host cancellation
then fails restoration and freezes the recoverable draft. The first retained
failure is an exact XYZ mismatch for placement `4294967302`: actual
`-459.84436,116.277344,187.374207` versus authored
`-459.844482,116.277374,187.374252`, with identical rotation, scale and
property count. This precedes the later recipe mismatch shown in the status.
The creation-mode excursion clears the grounding provenance used to project
the pinned entry image. Automatic retries after the first freeze then lose
their projection scope and repeatedly report the secondary recipe error.
Neither error is waived as harmless rounding. Native return must retain and
reverify its original witnessed grounding and binding identities; Runtime
must retain the first terminal failure without further automatic native work.
Evidence: `out/preview9-c27-terrain-host-away/result.json`,
`out/preview9-c27-terrain-failure-full/result.json`, and its full host log.
Both games returned normally to Galaxy with faults 0 before runner exit 0.

### C28 controlled return repair and four-player creation evidence

Native `e436457` extends the existing bounded, one-use creation return ticket
with its witnessed grounding and full raw binding roster. Sparse live slots
remain distinct from compact serialized marker keys. Capture and return check
the exact original owner, scope, serial, entry document, class/marker identities
and witnessed XYZ before reattaching logical act/terrain identities and the
prior normalization map. There is no epsilon, latest-document inference or
marker mutation. Input size preflight precedes copying, and callbacks cannot
publish a ticket after retiring its operation.

The new regression fails against the genuine old C27 source at the missing
raw-binding assertion. The fixed strict optimized i686 fixture passes 6,564
focused grounding, 24,534 materialization and 68,285 full checks, with 46 inputs
unchanged. It covers sparse slots 1/3, compact keys 0/1, nondefault object IDs,
two acts and terrain identities, strict post-return capture, changed positions
and identities, missing witnesses and callback retirement. Evidence:
`out/preview9-creation-grounding-native-receipt.json` and
`out/preview9-creation-grounding-red/receipt.json`.

Runtime `e0be9e9` retains the first terminal return error, ticket, entry image
and committed checkpoint, then stops automatic native return work. Empty-error
readiness waits still progress. Strict optimized i686 passes 5,647 lifecycle
and 8,061 creation checks with 76 inputs unchanged
(`out/preview9-c28-runtime-receipt.json`). The fixture preserves the existing
bounded Controller freeze notification echo; it asserts no subsequent native
capture, validation, registration, restoration, camera or selection work.

Core `057c7bc` removes the redundant private `apply_patch` Document assignment:
its sole caller already owns an exact copy. Revision, guards and admission are
unchanged. The untouched 64 KiB allocation regression fails on the old code
and passes with one copy on the fix. Strict Linux document/Session checks pass
3,119/29,932; i686 passes 3,119/22,685 with 22 inputs unchanged. Evidence:
`out/preview9-const-state-copy/receipt.json`. This is copy-count evidence, not a
measured frame-time improvement.

These changes build private C28 DLL
`efe00db7b05ffe565bd75243d42276f577177a914ff65031fb9c2f4996104089`
(91,561,984 bytes), with all 191 production inputs unchanged
(`out/preview9-candidate28-build-receipt.json`). The exact DLL is retained in
`out/preview9-candidate28-preserved/`; actual return acceptance remains pending.

The separate C27 four-player run passes simultaneous entry into Creature,
Building, Vehicle and Spaceship editors. Each guest independently cancels and
returns while the host remains in Creature; the host then returns last. Every
participant retains revision/durable 0, unchanged history, strict native
convergence and faults 0. Entry screenshots can precede first rendering; the
later all-four-designing screenshots show the actual editor families. Evidence:
`out/preview9-c27-four-independent-creation-cancel/result.json`. This verifies
entry and cancellation, not completed saves or all editor variants. Earlier
bootstrap helper failures (pair/four mismatch, captain onboarding and ambiguous
Maxis author versus sidebar caption) remain retained; the successful run uses
the observed sidebar control and normal native onboarding.

The C27 guest Vehicle completes normal native construction and naming, with
finish mask 0, changed model and metadata, no pending save and a single Done
click. It saves locally but fails shared admission. The first automatic cached
and private validators both report subtype `7D433FAD`, error bit `00000020`,
against Adventure mask `0FF647FF`. No manual model query preceded this evidence.
All four return ready at revision/durable 0, history unchanged and faults 0;
no partial cast entry is admitted. Source and alias BEM bytes are identical.
The bit can mean a missing part list or a part outside the subtype's list; it
does not establish a missing compiled VCL dependency. The exact Native Save,
alias package and first admission logs are retained under
`out/preview9-c27-four-vehicle-finish/`. This remains failed save acceptance
pending the native request/palette and effective part-list investigation.

### C28 actual terrain return and native input qualification

The private C28 host-away sequence now completes: the host remains in the
native Creature Editor while the guest submits a Basic Tools stroke and uses
the native toolbar to Undo, Redo and Undo it. The host then cancels normally
and returns at the latest revision/durable 12. Both participants strictly
converge, remain ready and unfrozen, and report faults 0. This closes the
reported witnessed-grounding return failure without relaxing coordinate or
recipe validation. Evidence: `out/preview9-c28-terrain-host-away/result.json`.

The two-author terrain history also passes through native toolbar buttons:
guest Redo 5, host Redo 6, host Undo 7 and guest Undo 8 reproduce exact recipe
record identities, payloads and host commit order, then restore the baseline.
Evidence: `out/preview9-c28-native-button-history/result.json`.

These are functional results, not qualified ordinary-input or timing results.
The external input observer records physical Shift activity during both the
original simultaneous keyboard-history attempt and the host-away sequence.
SporeKit combines physical key state with synthetic input. The original
keyboard Redo timed out; its contaminated evidence is retained and is not
reclassified as a successful ordinary gesture. The later native toolbar
sequence succeeds. The source of the physical activity is not established.

The native temperature slider accepts exactly one planet-settings transaction
at revision 13. Its subsequent native toolbar Undo does not dispatch, even
after normal Escape clears selection. The same public Runtime Undo succeeds
immediately at revision/durable 14 and restores the committed temperature;
both participants strictly converge with faults 0. This isolates the pending
failure to native input/command dispatch rather than Core history. Native
dispatch also checks physical/injected mouse state and separate tool/context
interaction fields, which are not established by the current selection
status. No native guard is bypassed or raw state cleared. Evidence remains in
`out/preview9-c28-two-temperature/`,
`out/preview9-c28-temperature-history-clear-selection/`, and
`out/preview9-c28-temperature-public-undo-diagnostic/`.

### Specialized native creation requests

The failed C27 Vehicle used unconstrained generic editor `99F87089`, but the
native adventure browser requests a specialized editor selected by its asset
category. Its two rejected gun parts belong to Military Air's part list 38,
while the saved Military Land subtype requires list 39. Both are genuine
installed native lists; this is not a missing compiled configuration resource.

Change `368762b` routes New Building through four verified variants and New
Vehicle through nine. The default routes are City Hall and Military Land;
existing Edit retains the native saved-model subtype resolver. Creature and
Spaceship routes remain direct. Strict optimized i686 routing/UI fixtures
pass 1,446/204 checks with 40/27 unchanged inputs, and the previous generic
Vehicle default fails the new regression. Evidence:
`out/preview9-creation-routes/receipt.json` and
`out/preview9-creation-routes/ACCEPTANCE.md`. Actual specialized-palette native
save acceptance remains pending; the invalid generic-palette creation was
not admitted or advertised as successful.

### C28 Save checkpoint failure and retained native carrier

The passive, stable double-read at revision 14 finds native Context+0C and
ToolManager+5 both zero on both peers, with no external mouse/modifier held
at those samples. This rules out only those two busy fields at sampling time.
A monitored native Redo then passes at revision/durable 15 with no external
input observed. Native Undo times out while an external left mouse press
overlaps the attempted dispatch; another native Undo reaches revision/durable
16, also with external mouse activity during its window. Both remain ready,
strictly converged and fault-free. Ordinary-input qualification remains open.
Evidence: `out/preview9-c28-history-busy-rpm/`,
`out/preview9-c28-temperature-native-retry/`, and
`out/preview9-c28-temperature-native-undo-retry2/`.

The subsequent ordinary native Save really persists Town locally, but the
shared post-Save terrain proof freezes the recoverable party at committed
revision/durable 16. The first mismatch is placement `4294967302`, with the
same witnessed rendered/authored XYZ pair from the creation-return failure:
`-459.84436,116.277344,187.374207` versus
`-459.844482,116.277374,187.374252`. Rotation, scale and property count match.
Ordinary native checks after this failure still converge on both peers.
`normalize_saved_terrain()` validates the raw persisted image before the
existing saved-grounding normalization. The transition ticket must carry
and verify its original exact grounding provenance before this validation;
no tolerance or later-document inference is justified.

The first Save-driver attempt stops before clicking because its at-point
guard incorrectly requires a caption container whose rectangle excludes the
protruding confirmation button. Fresh screenshot and tree/where/at evidence
establish the real saved dialog. A resumed driver acknowledges that same
dialog, without issuing a second Save, then retains the real checkpoint
failure. Evidence: `out/preview9-c28-native-save-carrier/`,
`out/preview9-c28-native-save-carrier-r2/`, and
`out/preview9-c28-save-failure-full/`.

Read-only stable acquisition retains the genuine 53-record Native Save DBPF
containing root `408A0000!2912D504.366A930D`, SHA-256
`2cf6183dd727ca09a284d762779293a8ae9157c33f9253b8886024e910d7ba41`.
Its persisted 419-placement/118-class/5-act root is 60,668 bytes, SHA-256
`aa74e1af6e895cb28f0e5758c5983cd7e175e91249203a7400ce60fa805326c7`.
The source remains identical after normal return to Galaxy and runner closure.
This proves local persistence only; shared SavedApplied remains failed.
Evidence: `out/preview9-c28-saved-local-carrier/`.

Offline checked replacements now prepare 1,025/4,095/4,096-placement DBPF
carriers from that exact genuine source, preserving all other records. They
remain unmounted and do not establish native complexity eligibility, cold
reopen or publication. Evidence:
`out/preview9-complexity-genuine-town/receipt.json`.

Both C28 games return normally to Galaxy with faults 0 before the runner ends
with exit 0; child exit codes are 1 from runner teardown. The personal preview
8 installation, exact 4 GB executable and shortcut remain untouched.

### Isolated input, environment controls and Water failure

Change `c2a2629` adds an explicitly requested, immutable private-lab input
policy. It requires an invisible game, an exact installed `winlab_*` profile
and an instance of at least 2. Both input hooks must install before game
initialization. Native key polling then reads the fixture's virtual key state;
ordinary personal launches retain their existing input policy. The pure
Windows/Linux contracts pass 55 checks each and the actual i686 input-hook
fixture passes 463 checks, with 45 unchanged inputs. Evidence:
`out/preview9-exclusive-input/receipt.json`.

The C28i and C29 actual games report `exclusive-virtual requested=1 hooks=2/2`
on both participants. External keyboard activity therefore no longer
disqualifies these Native gesture windows. The private installation receipt
retains the previous Kit and confirms the unchanged game executable;
`out/preview9-exclusive-kit-deployment/receipt.json`. The personal shortcut
and its payload are unchanged.

With isolated input, native Temperature and Atmosphere sliders each pass a
host edit/Undo/Redo/Undo and a guest edit/Undo/Redo/Undo. All 16 transactions
converge strictly with one changed planet field, unchanged unrelated data and
faults 0. Evidence: `out/preview9-c28i-two-temperature/result.json` and
`out/preview9-c28i-two-atmosphere/result.json`. The native inspector is stale:
after Temperature Undo, its thumb remains at 897 instead of baseline 859,
including after category navigation. Canonical values are correct; this is
an actual UI refresh failure, retained under
`out/preview9-c28i-environment-refresh-probe/`.

Water is an actual failure. A native drag from `(839,574)` to `(859,574)`
freezes the host before a transaction at revision 16 with "The native terrain
rotation ticket lost its editor or placement identity". Placement
`4294967296` has legitimately grounded XYZ
`-490.638794,112.002136,96.5438004` instead of committed
`-485.206055,110.761955,95.4747925`; its quaternion, scale and properties
match. The guest retains the baseline. Evidence:
`out/preview9-c28i-two-water.measured.json` and
`out/preview9-c28i-water-failure-full/result.json`.

Read-only native inspection verifies a separate Water grounding boundary at
`F335B0`, called by the Environment completion path before commit. It bypasses
the existing terrain rebuild hook. Environment refresh is separately verified
at `F3AF60`, including native non-notifying slider updates; Flora's slider
SetValue ABI takes both value and notification arguments. Symbols and proof
are committed in `a2a2482`, with all 101 verifier symbols matching the combined
map and RE lint passing. Evidence:
`out/preview9-water-boundary/final-receipt.json`. These signatures alone do
not establish successful live Water, refresh or Flora acceptance.

### C29 commit staging and remaining performance failure

Private candidate C29 is SHA-256
`9a1809fa987a68b52993ab72f7438d3e301498e748b8a43b0dc7951529e46e44`.
Its successful build has 191 unchanged production inputs. The actual DLL
and all 191 input files are retained under
`out/preview9-candidate29-preserved/`, independent of subsequent source edits.
The Native Save grounding fixture passes 17,403 focused checks, and its full
i686 suite passes 87,701; Runtime staging/transition fixtures pass 8,213 focused
and 97,224 full checks. Their exact receipts are
`out/preview9-save-grounding-native-receipt.json` and
`out/preview9-c29-runtime-receipt.json`. Session Saved readiness and FIFO
fixtures also pass; `out/preview9-saved-readiness-final/receipt.json`.

The two-player Town benchmark is incomplete and fails acceptance. It retains
30 host and 19 guest movement samples, then command-artifact writing fails
with Windows `EINVAL`. The cause is unproven. All 49 completed transactions
remain durable and strictly converged, without a native fault or extra commit.
Evidence: `out/preview9-commit-diagnostic-c29-two-town-fresh/result.json` and
`out/preview9-c29-after-io-probe/result.json`. This is not a completed
30-per-author benchmark.

The host does not enter staged capture in any measured sample. Its partial
submit-to-confirm p95 is 182.1 ms, release-to-confirm p95 is 344.9 ms, and
author-window maximum frame-gap p95/max are 273.9/275.1 ms. The guest enters
all six capture phases on consecutive frames for each of its 19 samples.
Its corresponding submit/release p95 are 516.3/1,071.5 ms, and author-window
maximum frame-gap p95/max are 134.9/134.9 ms. The observing host has
230.7 ms frame-gap p95/max and remote application p95 156.9 ms. These partial
results fail the frame and LAN confirmation targets; phase staging is not
reported as a performance acceptance pass.

Read-only checkpoint comparison identifies a concrete reason for the host
opt-out: both canonical documents use source instance `C6A75465`, while the
actual host scene uses `BF0CE0D0`. The guest scene uses `C6A75465`. The native
and state checkpoint bytes match across peers; the resource identity remains
meaningful and cannot be ignored. Evidence:
`out/preview9-c29-canonical-root/receipt.json`. A future explicit attachment
receipt must prove this exact relationship before staged capture can use it.

Change `5471bcf` moves command-artifact flushing outside held-input and
measured frame windows, preserves the original Native error if flushing also
fails, and adds bounded atomic-write retries. It also adds checked optional
six-phase timing parsing with explicit unavailable values, rather than
silently treating missing measurements as zero. Windows and Linux contracts
pass 44 checks each with unchanged inputs;
`out/preview9-commit-artifacts/receipt.json`. These are tooling checks, not
new Native performance measurements.

### C29 Save identity failure

Both roles again pass the Temperature edit/Undo/Redo/Undo sequence, ending at
revision/durable 59 with strict convergence and faults 0;
`out/preview9-c29-two-temperature/result.json`. Normal native Save then
persists locally but fails the exact shared entry guard: "The native Save
grounding proof no longer matches its exact committed entry". Both post-failure
native checks still strictly converge on the same semantic hash. Evidence:
`out/preview9-c29-native-save-carrier/result.json` and
`out/preview9-c29-save-failure-full/result.json`.

The refreshed transition returns a strictly observed snapshot with the actual
host source, while its retained ticket still contains the canonical source.
The checked handoff must retain that same observed snapshot; weakening the
source guard would conceal a separate identity change. This remains failed
live shared Save acceptance until the corrected build is exercised.

Both C29 games return normally to Galaxy with faults 0 before runner exit 0.
Child teardown exits remain 1. No C29 payload is installed into the personal
installation. Water, native UI refresh, completed specialized creation saves,
four-player acceptance, complexity boundaries, performance and official lab
smoke remain open.

### C30 complete two-player measurement and native environment checks

Private C30 is SHA-256
`57ca6c3aabd6447c2be3dc86332e21a67759adc3d270f2915329484ac2e68a24`,
92,716,032 bytes, built with 191 unchanged production inputs. All actual
inputs and the DLL were retained before later changes. Native commit
`1f81342` passes 110,764 full strict i686 checks; Runtime commit `5ef0078`
passes 112,931. Their 49/77 inputs remain unchanged after commit. Evidence:
`out/preview9-c30-native-receipt.json`,
`out/preview9-c30-runtime-alias-receipt.json` and
`out/preview9-c30-independent-review/receipt.json`.

The initial bootstrap fails before authoring because the registration helper
mistakes Galaxy's footer Login for a registration modal. A guarded retry
starts Town normally, at revision/history 0 with strict convergence and faults
0. This is retained as a tooling failure, not a mod or authoring transaction.
The helper correction `aed5b8b` subsequently verifies modal ownership and
passes 59 contracts on both Windows and Linux, with 16 unchanged inputs.
Evidence: `out/preview9-c30-two-town-bootstrap-r2/result.json` and
`out/preview9-registration-helper/receipt.json`.

The full LAN run completes all 60 actual movements, 30 per author, with all
six capture phases advancing exactly once for every gesture. This confirms
the host alias receipt works in-game. The two warmups plus 60 measurements
end at revision/durable 62, strictly converged and fault-free. There is no
artifact I/O interruption in this run. It still fails performance acceptance.

| Metric | Host author | Guest author |
| --- | ---: | ---: |
| Submit-to-confirm p95 | 142.5932 ms | 472.9155 ms |
| Release-to-confirm p95 | 708.8913 ms | 987.5341 ms |
| Own-author window maximum frame-gap p95 | 98.8059 ms | 97.4792 ms |
| Own application p95 | 64.1857 ms | 65.0088 ms |

The own-author frame p95 meets the target. Observing the other author's commit
still stalls: participant 95/96 combined-window maximum frame-gap p95 are
186.8505/234.1123 ms, with maxima 188.5133/276.7046 ms. Guest confirmation
also exceeds 200 ms LAN. Source data and the separate author/observer timing
distributions are retained at
`out/preview9-commit-diagnostic-c30-two-town-fresh/result.json` and
`out/preview9-c30-runtime-metric-analysis.json`.

Native aggregate supported-work p95 is 40.3725/39.7124 ms and includes two
calls, including the earlier producer proof. The comparable Runtime
SupportedBegin-to-End p95 is only 1.5241/1.4475 ms. Comparing the aggregate
counter to C29's Runtime span would falsely imply a seal reuse regression;
no such defect was found. Each gesture still uses 12 Native Writes, two Reads
and one projection. Observer preparation is the next measured optimization
target; no improvement is claimed before a new actual benchmark.

Both roles' Temperature edit/Undo/Redo/Undo pass at revisions 62 through 70.
Both roles' Water sequences also pass, ending at 78 with one transaction per
action, strict convergence and faults 0. This closes the actual C28i Water
freeze. Evidence: `out/preview9-c30-two-temperature/result.json` and
`out/preview9-c30-two-water/result.json`.

The C30 Temperature-restored screenshots match the initial slider position.
Root initially misread the screenshot; a pixel comparison and independent
review corrected that finding. The gold-pixel peak is x853 in the baseline,
both restored images and the later settled capture; the genuinely stale C28i
image peaks at x901. A passive double sample at revision 78 also finds both
tools' C8/CC/D0 bytes exactly equal canonical Planet `03A23F98`:
`0000003f295c0f3f3d0ad73e`, or 0.5/0.5600000024/0.4199999869.
The checked actual ISlider values are Temperature 2 in [0,4], Atmosphere 42
in [0,100] and Water 37 in [0,100]. No additional Environment source change
is justified. Evidence:
`out/preview9-c30-environment-passive-r78/result.json` and
`out/preview9-c30-environment-ui-passive-r78-r2/result.json`.

### C30 persisted marker-key failure and Save-only correction

Normal native Save completes locally at revision 78, then shared publication
fails with "The persisted native Save placement identity changed". The host
retains the recoverable frozen draft; the guest retains the same committed
semantic state. Evidence: `out/preview9-c30-native-save-carrier/result.json`
and `out/preview9-c30-save-failure-full/result.json`. This remains a live Save
failure even though the earlier source-handoff guard now passes.

Native inspection identifies two distinct key domains. Live marker+228 is an
editor lookup identity. The native descriptor omits that word from Write;
the constructor initializes it to FFFFFFFF, and Read/pool insertion preserve
that sentinel. Comparing a parsed persisted marker to the original live key
is therefore invalid. Controlled fixtures previously serialized the extra
word and concealed this difference. Exact native evidence is retained at
`out/preview9-c31-eligibility/receipt.json`.

Correction `4ed0810` requires the native constructor sentinel on the private
persisted owner and retains it through every subsequent checked callback and
cleanup. Live key fences remain exact. Class/dense bindings, witnessed XYZ,
unknown bytes, serialized roundtrips and source/context/Release checks remain
mandatory. A Native-faithful writer/reader makes the exact old C30 fail the
new regression; `out/preview9-save-key-red/receipt.json`. The optimized fixed
suite passes 112,147 full checks, including 37,317 Save checks, with 49 inputs
unchanged before and after commit;
`out/preview9-save-key-native-receipt.json`.

The resulting private Save-only candidate C31 is SHA-256
`97a33497c264f4202ab691aaf596a81ab06e3d3e197afe3c4a72a69b68f83401`,
with 191 unchanged retained production inputs. Actual Save acceptance is still
pending. The genuine C30 local Save for root
`408A0000!2912ECD1.366A930D` is retained separately; it is not advertised as a
successful shared checkpoint:
`out/preview9-c30-saved-local-carrier/receipt.json`.

Both C30 games return normally to Galaxy with faults 0 before runner exit 0.
The personal preview 8 payload, its 19-file rollback, exact 4 GB executable
and shortcut remain unchanged; rechecked at
`out/preview9-delivery/c30-prebuild-personal-unchanged.json`.

### C31 actual shared native Save and isolated Environment refresh

The retained Save-only C31 DLL above passes a new two-player native Town
session. Both roles complete Temperature edit/Undo/Redo/Undo at revisions
0 through 8, then Water at 8 through 16. Every action commits once, both
native scenes converge strictly, and both fault counters remain zero.
Evidence: `out/preview9-c31-two-temperature/result.json` and
`out/preview9-c31-two-water/result.json`.

A separate quiet capture after Temperature and before Water or Save waits
30 real frames. Both Environment tools' C8/CC/D0 values match the canonical
Planet bytes, and both actual Temperature sliders show 2 in [0,4]. This
checks Temperature refresh independently of the later controls and Save.
The passive observation uses no memory writes or direct native function
calls: `out/preview9-c31-temperature-only-settled-r8/result.json`.

One ordinary native Save and its owned success confirmation now complete
shared publication. Revision/durable/saved revision remain 16, SavedApplied
is 2/2, both participants are ready and unfrozen, and history is unchanged.
The logical saved source is `408A0000!2912F36C.366A930D` on both. The guest's
separate local native source alias is accepted only through the existing
exact typed and opaque proofs. No recovery action or seeded movement is
needed. Evidence: `out/preview9-c31-native-save-carrier/result.json`.

The genuine saved private EditorSaves.package is retained after two stable
full reads and another source-stability check: 3,116,086 bytes, 74 records,
SHA-256 `3443cc6a509094dce8066e54b04f34ae141edd65d410d61d069955761aae2619`.
Its exact persisted root is 60,654 bytes with 419 markers, SHA-256
`b71e27dca80b17fe4495a587c2acdda7429d598910f5b9545e99b19c9e9ef1be`.
Both retained files are in the Save evidence directory. This establishes
shared Save acceptance for this scene; cold native reopen is still pending.

Both games return normally to Galaxy with faults zero before runner exit
zero. Child teardown exit codes are 1, consistent with previous normal
native shutdowns. Evidence: `out/preview9-c31-retirement/result.json`.
This Save-only change does not resolve the C30 observer frame stalls or
guest LAN confirmation target. Four-player coverage and the remaining
feature, performance, smoke and delivery gates remain open.

### C31 four-player creation cancellation, Environment and shared Save

Four fresh private profiles receive only the closed C27 profiles' native
Spore subtree; no shared project store is copied. Their C31 DLL and isolated
input kit are verified before launch. Two bootstrap attempts stop before
authoring: the first invokes a pair-only helper with four games, and the
second encounters a truncated startup UI tree after registration opens its
quit warning. Both are retained tool failures. Correction `d86f9da` uses
bounded, ownership-checked modal snapshots, recognizes the exact native
warning over registration, and rejects unknown dialogs. Windows and Linux
each pass 64 helper contracts with zero input drift.

The corrected four-player bootstrap reaches revision/history zero with
matching native scenes and zero faults:
`out/preview9-c31-four-town-bootstrap-r3/result.json`. All four then enter
different native creation editors, including the corrected Building and
Vehicle subtype routes. Every guest cancels and returns independently
while the host remains in Creature Editor; the host returns last. All four
retain identical revision/history zero and no cast entry is added. This is
entry/cancellation coverage, not completed asset-save acceptance:
`out/preview9-c30-four-independent-creation-cancel/result.json`.

Host Crew capacity commits once at revision 1 and converges on all four.
Adding the previously measured custom crew member stops without an edit because this profile's
native picker finds no Spice Crawler card for the measured query. Ordinary
Escape closes it, with revision/history unchanged and zero faults. This
does not establish Crew Add coverage. Evidence:
`out/preview9-c31-four-crew-enable/result.json`,
`out/preview9-c31-four-crew-seed/result.json`,
`out/preview9-c31-four-crew-picker-diagnostic/95-after.png` and
`out/preview9-c31-four-picker-cancel/result.json`.

Host and first guest each complete Temperature and Water edit/Undo/Redo/Undo
with all four participants checked after every action. Temperature advances
revision 1 to 9 and Water 9 to 17; each action produces exactly one revision,
all native states converge and every fault counter stays zero. The other
two participants are observers in these particular sequences, not authors.
Evidence: `out/preview9-c31-four-temperature/result.json` and
`out/preview9-c31-four-water/result.json`.

One native Save now completes SavedApplied 4/4 at revision 17 without a
semantic or history change. All logical sources are
`408A0000!2912F682.366A930D`; guests retain checked local aliases. The exact
saved native package is retained: 618,750 bytes, 12 records, SHA-256
`29063630af1b680abd6f663be10f45aa510aba17bd3095555e766377208864b0`.
The persisted root has 419 markers and 60,655 bytes, SHA-256
`14a14c8e35844e64971cc5a6945b60f626cdd2bef4bdeb0e0a35acd0be47d721`.
Evidence: `out/preview9-c31-four-native-save-carrier/result.json`.
All four normally return to Galaxy, fault-free, before runner exit zero:
`out/preview9-c31-four-retirement/result.json`. Cold reopen remains pending.

### C32 observer preparation work reduction, pending native timing

Commit `7d9b346` adds a private, synchronous pose-preparation proof. It
retains the full BASE validation and genuine cached projection. It only
uses the reduced path for checked transforms and the seven verified native
companion fields. A unique AFTER owner must write back byte-identically;
reversing only recorded primitives must then reproduce the complete BASE
bytes. Native owners retire before value-proof publication. Context, source,
scope, generation, bindings, resolver, captain and unsupported-data checks
remain mandatory; uncertain shapes use the conservative path.

The genuine old owning fixture succeeds with three Reads, one projection
and four Writes, then fails the new work-count regression. The fixed fixture
uses two Reads, no projection and three Writes with populated Act/Crew and
opaque class data. Strict actual-i686 checks pass 6,300 focused and 118,668
full, including the unchanged Save, Water, Environment and materialization
regressions. All 51 compiler/Make inputs remain unchanged after commit.
An independent source review closes the callback/Release and row-lifetime
findings. Evidence: `out/preview9-c32-pose-prepare-red-receipt.json`,
`out/preview9-pose-prepare-native-receipt.json`,
`out/preview9-pose-prepare-native-postcommit.json` and
`out/preview9-c32-independent-review/receipt.json`.

Offline inspection of the genuine C31 Town Save verifies version 17, empty
legacy text in all 590 ClassActs and empty deprecated distances in all 419
markers. These conditions do not force conservative fallback; actual memory,
cache availability and timing still require in-game observation:
`out/preview9-c32-town-eligibility.json`.

The guarded C32 production build retains all 192 actual inputs before
compilation, with zero drift. DLL SHA-256 is
`7bdd6e56c73c4639a4268d8b52a83577f6a7a8f9fa9aa526accd78ea3aa245c5`,
93,058,560 bytes. It is installed only into the closed private lab after C31
retires. This is not a performance or release-acceptance claim:
`out/preview9-candidate32-build-receipt.json` and
`out/preview9-candidate32-deployment/receipt.json`.

### C32 native performance and portable regression results

The private two-player Town benchmark completes all 60 real movement
gestures, 30 per author, through revision 62. The reduced Native preparation
path is used: observers perform two Reads and no projection. Functional
convergence passes, but performance acceptance fails. Host and guest own
confirmation p95 are 143.929 and 422.755 ms. Observer commit-window frame-gap
p95 are 140.677 and 188.690 ms, above the 100 ms target; own-author windows
are 96.126 and 97.764 ms. Maximum observed frame gaps remain below 250 ms.
Full release-to-confirmation p95 are 615.869 and 894.650 ms; those include
the separate staged gesture capture and must not be presented as commit
confirmation. The driver exits at its budget assertion before its final
configuration checks. Subsequent Crew baselines independently confirm both
scenes healthy at revision 62. Evidence:
`out/preview9-commit-diagnostic-c32-two-town-fresh/result.json` and
`out/preview9-c32-full-performance-analysis.json`.

Marker mutation itself takes about 1 ms; most observer apply time precedes
it. Counterfactual timing also shows that eliminating Native apply alone
cannot satisfy guest LAN confirmation. Worker completion scheduling remains
a separate bottleneck; faster acknowledgement without durable completion
is not an acceptable substitute. Scene-ready, durable and author-confirmed
timestamps remain distinct. Evidence:
`out/preview9-c32-critical-path-counterfactual.json` and
`out/preview9-c32-all-peer-completion-analysis.json`.

The complete portable repository target passes on the frozen C32 source:
`make -j2 test B=out/editor-preview9-c32-portable HOSTCXX=clang++`, exit 0,
341.565 seconds, 36 executable hashes retained. All 192 production and 539
monitored inputs remain unchanged. This includes Session UDP adversity,
Python contracts and RE lint; it does not replace Native or lab smoke.
Evidence: `out/editor-preview9-c32-portable/receipt.json`.

### C32 Crew controls from both roles

Both roles now complete Add, Replace, Remove, Lock and Capacity using real
native controls and observed Maxis assets. Every history cycle checks exact
S0 after Undo, S1 after Redo and S0 after a final Undo, without growing the
history entry count. Host changes coexist with the guest's unrelated
placement selection/lease. The sequence advances revision 62 through 105,
with strict native convergence and zero faults on both participants.

Evidence is split across
`out/preview9-c32-two-crew-enable/result.json`,
`out/preview9-c32-two-crew-choose-ace/result.json`,
`out/preview9-c32-two-crew-replace95/result.json`,
`out/preview9-c32-two-crew-matrix/result.json` and
`out/preview9-c32-two-crew-add96-aepox/result.json`.
The matrix artifact itself reports failure because its last guest Add
queries Ace, absent from that guest's catalog. That attempt makes no edit;
ordinary Escape closes it before the separate Aepox Add succeeds. The
composite control coverage must not be described as a passing matrix file.

The prior four-player missing Spice Crawler seed is also a fixture catalog
issue: it is a custom creation, not a stock asset. The fresh native profile
has one My Creations entry instead of the older profile's 1,349. This does
not establish a picker filtering regression or four-author Crew coverage.
Evidence: `out/preview9-c31-four-crew-seed-audit.json`.

### C32 native vehicle creation and shared Save failure

The host designs and names a valid Military Land vehicle using its native
specialized route `7D433FAD`. Body, cockpit, wheels and weapons are placed
through native input; the native finish validation mask reaches zero.
One Done action bakes and admits the saved asset, producing exactly one
Create adventure asset history item at revision 106. Both peers grow from
118 to 119 classes, retaining 419 placements and five acts. Ordinary Undo
and Redo at revisions 107 and 108 remove and restore that class with exact
convergence and no extra history item. Evidence:
`out/preview9-c32-host-vehicle-save/result.json` and
`out/preview9-c32-vehicle-class-history/result.json`.

The initial entry probe incorrectly expects a generic Vehicle request.
It stops after the correct native editor has already opened. A retained
read-only specialized-request proof establishes the actual Military Land
request before further input; entry is not replayed. This is a tool failure,
not a failed native route. Evidence:
`out/preview9-c32-host-vehicle-enter/verified-specialized-route.json`.

Subsequent ordinary adventure Save exposes a product failure. Spore saves
locally and rotates the source to `408A0000!2912FB0B.366A930D`, but sharing
the terrain proof stops with "A native asset resolution witness is
unavailable". Both peers freeze at durable revision 108 with zero native
faults; shared SavedApplied coverage is not achieved. No retry or recovery
is issued. Evidence:
`out/preview9-c32-vehicle-native-save-carrier/result.json` and
`out/preview9-c32-save-failure-diagnostic/result.json`.

Both games return normally to Galaxy with zero faults before runner exit
zero. The closed private Stores, dependency packages and genuine saved
DBPF are retained with repeated stable reads. The package is 3,733,324
bytes, 85 records, SHA-256
`cfecf0115c7525efe394792e94f9e73493b5e3d3ee44f3a0a08d15dcad4aea45`;
the 60,756-byte root has 419 markers, SHA-256
`0fe8688354ffe5d755f9368335c2797166ce58f53cbe8c8668c2f7f0dc6c7087`.
This proves local persistence only; the shared Save defect and cold reopen
remain open. Evidence: `out/preview9-c32-retirement/result.json` and
`out/preview9-c32-closed-retention/receipt.json`. Personal preview 8 and its
4 GB executable remain unchanged. No release installation or merge occurs.

### C33 durable completion scheduling and native measurement

Commit `08d56bd` adds game-thread completion polling when owned Encode/Flush
workers finish. Workers carry immutable wake identities and owned bytes;
publication and resource-capacity validation still precede durable Flush.
Callbacks check the exact job, phase, project/epoch, transport, participant,
native generation, source and attachment. Early or stale notifications fall
back to normal polling. Failure and uncertain outcomes remain on the ordinary
rollback path. Completion polling cannot drain inbound/control/native work.
The worker destructor joins before any owned input or result is destroyed.

Strict actual-i686 O2 passes Runtime 114,743 full and 2,225 focused, Session
23,766 full and 480 focused, and metrics 132 checks. All 80 pinned inputs and
three executable hashes match after commit. The genuine C32 regression fails
the new outside-frame completion assertion while retaining successful normal
tick polling. Evidence: `out/preview9-c33-completion-receipt.json`,
`out/preview9-c33-completion-postcommit.json`,
`out/preview9-c33-completion-c32-red-receipt.json` and
`out/preview9-c33-completion-independent-review.json`.

The guarded production build retains 192 inputs with zero drift. DLL is
93,087,744 bytes, SHA-256
`e4a946256980a200729faa1694385ec9cefd0e10ee6d36c5f971ef4cff35e63b`.
It is deployed only into the closed private installation. The first native
bootstrap stops on a transient Connecting modal without an authoring edit.
It clears normally; a fresh bootstrap reaches revision/history zero and zero
faults. Evidence: `out/preview9-candidate33-build-receipt.json`,
`out/preview9-candidate33-deployment/receipt.json`,
`out/preview9-c33-startup-inspect/result.json` and
`out/preview9-c33-two-town-bootstrap-r2/result.json`.

All 60 movement gestures complete, 30 per author, reaching revision 62.
Host confirmation p95 improves from 143.929 to 98.366 ms and guest from
422.755 to 289.014 ms. Maximum confirmations are 100.199 and 291.164 ms.
Own-author frame-window p95 are 96.898 and 95.791 ms. Observer frame-window
p95 remain 141.415 and 187.153 ms, with maxima 142.487 and 187.309 ms.
Guest confirmation and observer p95 therefore still fail acceptance.

New stage samples separate observer Native preparation, p95 90.007/85.972
ms, from live-integrity work before it, 26.628/53.404 ms, and marker mutation,
1.206/1.365 ms. Actual Encode is about 1-2 ms and Flush about 5-10 ms; most
completion observation is prompt, with occasional frame-sized waits. The
trace alone cannot distinguish an early-future wake from dispatch/context
fallback. Even subtracting the entire measured host preparation interval
leaves an optimistic guest confirmation p95 of 204.142 ms. This is not enough
margin for the target and does not justify weakening opaque validation.
Evidence: `out/preview9-commit-diagnostic-c33-two-town-fresh/result.json` and
`out/preview9-c33-full-performance-analysis.json`.

After the budget assertion, independent native checks confirm both peers
healthy, strict-converged and durable at revision 62 with zero faults.
The idle Store checkpoint/journal/object bytes are retained before leaving;
the first read attempt stops on the intentionally exclusive project.lock.
A corrected read excludes that transient lock. Both games then return
normally to Galaxy, fault-free, and the runner exits zero. Closed source
reads prove all six Store files byte-identical to the idle copies. These
owned files preserve genuine consecutive movement images for a future
exact-output optimization oracle. Evidence:
`out/preview9-c33-idle-retention-r2/receipt.json`,
`out/preview9-c33-retirement/result.json` and
`out/preview9-c33-closed-retention/receipt.json`. No personal installation or
release-acceptance claim follows from this intermediate build.

### C34 creation identity correction and shared Save acceptance

The genuine C32 failure is a copied metadata identity. The accepted Vehicle
alias pollen still carries its source online ID at serialized offset 4.
Native Save reads this ID, clears the alias instance and emits a server-only
reference that the local resolver cannot find. The entire decoded difference
is that four-byte instance and eight-byte server ID; no model corruption is
established. Native v13 reader/writer, getter and Save conversion call chain
are independently verified on the supported executable. Evidence:
`out/preview9-c32-save-witness-audit/receipt.json` and
`out/preview9-c32-save-witness-independent-native.txt`.

Commit `9d7bd6c` clears only the owned alias pollen's online ID to UINT64_MAX
after the existing version-13, minimum-24-byte and exact self-key checks.
The self key is then remapped as before. Foreign/ancestry IDs, old immutable
aliases and source records remain unchanged. Strict resolution witnesses are
unchanged. Genuine old-source i686 reproduction fails the new identity check;
the fix passes 317 focused and 1,763 full checks, with all 35 compiler/Make
inputs and the executable unchanged after commit. Evidence:
`out/preview9-c34-alias-online-red-receipt.json`,
`out/preview9-c34-alias-online-receipt.json` and
`out/preview9-c34-alias-online-postcommit.json`.

The guarded C34 production build retains all 192 inputs without drift. DLL
is 93,088,768 bytes, SHA-256
`9d50eb8c823da45f9ffe5bb1c8f50b6c55074355696242d9ab92f38360455e11`.
After private-only deployment, a fresh native two-player Town bootstrap
reaches revision/history zero and zero faults. The host designs a new valid
Military Land vehicle through the same measured native route. Done admits
one class and one history item at revision 1; native Undo/Redo at 2/3 remove
and restore it on both peers. Evidence:
`out/preview9-c34-host-vehicle-save/result.json` and
`out/preview9-c34-vehicle-class-history/result.json`.

Ordinary adventure Save now succeeds at revision 3 with SavedApplied 2/2,
strict convergence, zero faults and unchanged history/semantics. Logical
source is `408A0000!29130B6D.366A930D`. The retained native package is
4,357,385 bytes, 96 records, SHA-256
`b51eed6eb1c400d1129ce9425102bbaffd5d1a88a40570bf08e37855592ddeb1`;
its 60,745-byte root has 419 markers, SHA-256
`9e75c45070fbceb8d45a98423ca1609df7a9fd8e1d64e6519c8e99ddac0cddec`.
Independent decoding verifies class 1342 retains local alias
`40646200!7DD6BE67.24682294` and UINT64_MAX server ID; both retained Store
envelopes contain its exact 193-byte pollen with UINT64_MAX own online ID.
Evidence: `out/preview9-c34-vehicle-native-save-carrier/result.json`,
`out/preview9-c34-saved-retention/receipt.json` and
`out/preview9-c34-save-witness-independent-confirmation.json`.

### C34 fresh-process dependency availability failure

Both games return normally to Galaxy and close fault-free. New processes
open the native My Creations adventure picker without Recover. Selecting
the newest Adventure Town card stops at "Some creations in this Adventure
were banned. Some creations will be replaced." No replacement is accepted;
the editor has not loaded, so the attempted source cannot yet be confirmed
through the ready-scene key check. Evidence:
`out/preview9-c34-cold-native-open/result.json` and
`out/preview9-c34-cold-diagnostic-r2/95-cold-open-stopped.png`.

The generic warning is not evidence of an actual ban. Independent static
inspection shows a missing local-key ResourceManager resource is sufficient
for mask 0x800, mapped by stock message data to this warning. Canonical
alias records are present on disk but no cold initialization enumerates or
registers retained packages. Warm registration alone therefore does not
establish persistence acceptance. The closed package inventory and accepted
Store envelope contain identical alias BEM, metadata and both PNG records;
neither EditorSaves package embeds those dependencies. All 197 files,
17,842,767 bytes, are retained after repeated stable reads. Evidence:
`out/preview9-c34-cold-dependencies-r2/receipt.json` and
`out/preview9-c34-cold-dependency-audit/receipt.json`.

Normal Cancel returns to Galaxy and the saved package remains byte-for-byte
identical. Both games again close normally with zero faults and runner exit
zero. No profile/database repair, recovery or replacement was performed.
Evidence: `out/preview9-c34-cold-cancel/result.json` and
`out/preview9-c34-cold-retirement/result.json`. A selected-file dependency
restoration path before Native's original availability checks remains to
be implemented and validated; blanket-mounting historical cache packages
would exceed existing admission limits and is not an acceptable remedy.

### C35 focused cold-Save checkpoint

The next cycle is deliberately limited to dependency persistence. Kit 0.3.5
queues interactive lifecycle work outside Present; direct loader detours retain
their DLL return stacks through nested unload. The Saved codec/store and Native
issuer bind one exact Saved image and complete raw reference roster to only its
selected admitted content. Group 0 keys and optional creation companions retain
the existing Collector policy. Runtime publishes the immutable receipt only
after its Save/terrain checks and before Saved confirmation. Native restores
that receipt during genuine owned state1/phase0 and then runs every original
preflight check. No old transport roots or historical directory-wide mounts
are used. Implementation commits: 6e4f5ea, af16cf1, 25a7555, d110796, d8d9e28.

Focused actual-i686 tests pass: loader gate 17718; Native view/owner 3226;
collector 202; Runtime native flow 11128; recovery 2026; logical Saved root 158;
alias capture 241. The codec/store passes 113 codec cases on Linux/i686 and
24/23 real-filesystem cases. Windows directory symlink creation alone is
unavailable without its privilege. A preexisting export-cancel/rebuild failure
also reproduces in the retained C33 executable and is fixed without weakening
the separate readiness requirement for actual restoration. Evidence:
`out/preview9-c35-saved-native/receipt.json`,
`out/preview9-c35-root-focused-r2/receipt.json`,
`out/preview9-c35-root-focused/c33-terrain-red.json`, and
`out/editor-preview9-saved-dependencies-group0/receipt.json`.

The private C35 production DLL is 96,093,184 bytes. All 202 actual production
inputs remain unchanged during build and after commit. Its retained Kit 0.3.5
DLL is SHA-256
`8856145ec32cbf7cbfcc7ce6381fd860e86086439e2becca4454cffd222af8df`.
Only the isolated installation is updated. Initial startup stops before any
authoring input because a changed monitor/refresh list maps old mode index 11
to 1600x1000. Native Settings independently identifies index 2 as 1024x768@60Hz
on both profiles; normal settings application plus a clean restart restores
the verified viewport. The private runner now requires that measured index
explicitly. Both supported executable hashes and physical input isolation are
unchanged. Two-player bootstrap then passes at revision/history 0 and fault 0.

The host makes a valid new Military Land vehicle through the already measured
native recipe. Done commits one class and one Create adventure asset history
item at revision 1. Both scenes converge with 419 placements, 119 classes and
5 acts. Native adventure Save completes with SavedApplied 2/2, unchanged history
and authored semantic hash, logical source `408A0000!29132851.366A930D`.
The host's exact persisted record SHA-256 is
`d493c1f444ac9056426863479d81def55ddc94b9763d1e620d04b9a988300a80`.
Its 353,916-byte receipt survives two identical reads, SHA-256
`8f15a0f3d44e9e66d0d29bd9db15fbb643b733ef176871aa88d8203bb003e6af`.
Evidence: `out/preview9-c35-host-vehicle-save/result.json`,
`out/preview9-c35-vehicle-native-save-carrier/result.json`, and
`out/preview9-c35-saved-dependency-witness/receipt.json`.

Both participants leave normally to Galaxy with zero faults before their runner
closes. Fresh processes 7124/45508 open the newest exact-title Saved adventure
through the native My Creations picker. The ready-scene key independently
matches `408A0000!29132851.366A930D`; 419 placements, 119 classes and 5 acts and
the complete authored semantic hash match the warm Save. Native reports
`deferred=1 resumed=1 restored=1 cancelled=0 failed=0`, one registered dependency
package of 329,860 payload bytes and zero faults. No Recover, memory write,
manual profile repair or replacement-warning confirmation occurs. Evidence:
`out/preview9-c35-save-retirement/result.json` and
`out/preview9-c35-cold-native-open/result.json`. This closes the reported C34
cold dependency reproduction, not the entire preview 9 acceptance matrix.

### C35 focused commit-latency baseline

The cold-opened adventure is reused directly for timing, with two warm-ups and
30 actual movement gestures per author. All 60 measured commits complete; a
final independent read-only check finds both participants ready at canonical
and durable revision 62, the same strict convergence hash, 419 placements,
119 classes, five acts and zero faults. This fixture contains one additional
unused vehicle class compared with C33, so it is not an identical-fixture
before/after comparison. No broader feature matrix is repeated in this cycle.

The LAN performance gate fails. Host submit-to-confirmation p95 is 67.8128 ms;
guest is 330.0971 ms against 200 ms. Release-to-confirmation p95 is
541.3905/801.937 ms. Commit-window frame-gap p95 is 185.8226/187.0712 ms against
100 ms; maximum is 187.1876/189.672 ms, within the separate 250 ms maximum.
The observer path remains the principal frame-gap problem. Native work retains
12 Writes on each author, five on a guest observing the host and four on a host
observing the guest. Removing an opaque-state serialization check is rejected:
the existing fixture changes unknown marker bytes after the first emitted
Write, which the second Write must still detect.

Evidence: `out/preview9-c35-commit-baseline-two-town/summary.json`,
`native-work-summary.json` and full `result.json` (SHA-256
`eaabcf50c4be12ff71555d6f96f1b755469ceca469370494730c66122b57a859`);
`out/preview9-c35-commit-final-proof/result.json`. Successful functional
convergence is recorded separately from the failed performance acceptance.

### C36 bounded compression reuse and editor service

Commits 4b2e07c and 74b7be4 retain every Native Write and opaque-state check.
Kit 0.3.6 optionally reuses exact original-compressor output for complete,
identical input under the verified Native caller/profile. Two owned entries
and staging remain within 32 MiB. The new shared-editor service uses one
run-owned queued ticket at five-millisecond intervals, with passive project,
epoch, participant and transport identity. Native busy scopes span final owner
releases; Runtime drains reject nested entry and retry pending work afterward.
The existing greater-than-500-ms modal transport fallback remains, while editor
Native work waits for those scopes to exit.

Strict actual-i686 compression and inherited lifecycle tests pass 73 and 2,610
checks, with 21 unchanged inputs. Pulse tests pass Linux/i686 138 each, Native
busy/final Release 28, real-UDP Runtime pulse/reentry 144, Native staged 1,662,
Native view 3,226, Runtime worker wakes 2,285 and Runtime staged 8,790. All 164
pinned pulse inputs remain unchanged through postcommit. The old staged fixture
waited for prohibited nested durability; its corrected contract retains both
operation IDs once each, both unrelated edits and durable revision 2 after the
outer callback returns. Earlier fixture-only declaration/vtable setup failures
are retained separately; they were not game crashes. Evidence:
`out/editor-preview9-refpack-memo/receipt.json`,
`out/preview9-editor-pulse-receipt.json` and
`out/preview9-editor-pulse-native-fixture-setup-failure.json`.

The production build and postcommit check retain all 247 inputs without drift.
Co-op DLL is 96,236,032 bytes; Kit is 7,497,216 bytes, SHA-256
`c42d792ab743f273a6554825faef55a8bfea43c51461585ef8d77fa91e4c4ab6`.
Only the private installation is updated; its previous payload and INI are
retained. Compression is enabled explicitly there. Fresh processes 58956/13636
cold-open the exact C35 Saved source `408A0000!29132851.366A930D`, preserve
authored semantic hash, 419 placements, 119 classes and five acts, then join a
new shared draft at revision 0. Both report verified memo enablement, matching
strict draft state and zero faults. Evidence:
`out/preview9-candidate36-postcommit.json`,
`out/preview9-candidate36-deployment/receipt.json` and
`out/preview9-c36-cold-shared/result.json`.

The iteration tool now permits one to 29 samples only with `--diagnostic`;
acceptance still requires at least 30 gestures per author. Its 45 offline tests
pass. C36 runs two warm-ups and three movements per author. All six measured
edits complete; final independent checking finds canonical/durable revision 8,
equal strict convergence, unchanged counts and zero faults on both players.
Memo counters report zero failures, contention and bypass, with two retained
entries totaling 2,548,912 bytes per process after the check.

This short probe fails the frame budget. Submission-to-confirmation maximum
(also p95 for three samples) is 48.3296 ms host and 159.3712 ms guest. Frame-gap
maximum across six windows is 139.6224/139.2381 ms, above the 100-ms p95 target
and below the separate 250-ms maximum. Release-to-confirmation maximum is
567.9037/677.8263 ms. These are development observations, not a statistically
qualified comparison with C35's 30-per-author baseline. The same authored Saved
fixture is reused in a new session; the full benchmark is deferred until the
remaining frame work is reduced. Evidence:
`out/preview9-c36-commit-probe-two-town/summary.json`, full result SHA-256
`f1f7ad230060879b71a5fce685ab8dcf179c50d958e7296fd78222294c70cedf`, and
`out/preview9-c36-commit-final-proof/result.json`.

The later full portable `make test B=out/editor-preview9-final` exits 0.
Counts include document 3,119, protocol 9,218, project 5,683, store 2,015,
Session UDP 31,704 and accounting 3,425. Saved codec/store pass 113/24 cases;
pose format passes 28 genuine pairs and 113 atomic negatives. The log is retained
at `out/preview9-c36-repository-tests.log`, SHA-256
`3f39c6b3b295f3cf9833ef59a16b2aa9c249d17a5c87f0355a89a2785b1b8684`.
This portable run is separate from the Native fixtures and does not qualify
later Native snapshot-copy changes. The timing parser's later optional copy
span update passes 46 offline tests and remains compatible with older counters.

### C37 copy-cost measurement and independent creation return

Commits 07ca1b4 and 780236e remove two overwritten Snapshot copies, preserve all
nine fields explicitly and retain two Native Reads, three Writes and the same
opaque proof in the positive preparation fixture. Future Snapshot members fail
the helper's structured-binding compilation until accounted for. Focused i686
checks pass 6,322 Native cases and 12 value/allocation cases. No authority,
Native-call or frame fence is removed. Evidence:
`out/editor-preview9-snapshot-values/receipt.json` and `make-receipt.json`.

The production DLL is 96,255,488 bytes with 248 unchanged retained inputs.
Kit remains the exact C36 0.3.6 payload. Fresh private processes 42564/51232
cold-open the C35 Saved adventure, retain authored semantic hash/counts and
join at revision 0. The next two-warm-up/six-edit diagnostic completes and
independently converges at canonical/durable revision 8 with zero faults.
Host/guest submission-to-confirmation maxima are 48.5394/159.1009 ms;
frame-gap maxima are 141.2133/139.2555 ms. Release-to-confirmation maxima are
564.7227/671.4781 ms. Frame acceptance still fails. The two observer copy spans
total only 0.4066-0.5512 ms, so no frame improvement is attributed to this
allocation cleanup and further copy tuning is not pursued. Evidence:
`out/preview9-c37-cold-shared/result.json`,
`out/preview9-c37-commit-probe-two-town/summary.json`, full result SHA-256
`64d1a0df512c6758b32dac9e74449fbf457286614d55d1d816d0e52e1ce40caf`, and
`out/preview9-c37-commit-final-proof/result.json`.

A separate functional check enters native City hall on the host and Military
land on the guest simultaneously. The guest cancels and independently returns,
then commits checked Undo and Redo at revisions 9/10 while the host remains in
its creation editor. Host cancellation restores the latest revision 10. Both
participants return ready with the same strict convergence, no faults, unchanged
history count, 419 placements, 119 classes, five acts and the restored authored
semantic hash. Neither unsaved design creates a cast entry. Evidence:
`out/preview9-c37-two-independent-creation-cancel-r3/result.json`.
The initial fixture stops before input on a truncated asset diagnostic; the next
attempt waits at the building subtype menu. Both are retained tool failures,
not game crashes. The fixture's entry route now selects the measured first
subtype for Building and Vehicle before waiting for native entry.

The Make checkpoint c8da0ee converts 19 unconditional portable compile commands
to independent incremental targets, preserving exact flags/source order. All
26 executables pass, including the existing pulse test. An unchanged rerun
executes zero compiler or dependency-generation commands; a simulated shared
header change rebuilds exactly three binaries. Failed dependency generation
preserves the previous dependency file and forces a clean retry. The Session
executable is byte-identical to the old build; its UDP polling CHECK count is
timing-dependent. Production Make recipes remain byte-identical. Receipt:
`out/editor-preview9-make-incremental/receipt.json`.

System CPU sampling was attempted with the uniquely named WPR instance
`SporeCoopC37Diagnostic`. WPR rejects profiling-policy enablement with
`0xc5585011`; subsequent status confirms no recording remains. This sample is
unavailable and provides no native hot-function evidence. Existing QPC stages
still identify observer validation/private preparation as the remaining frame
cost. No full 30-per-author benchmark is repeated after the failed short probe.

### C38 bounded diagnostics and four-player Crew coverage

C38 adds read-only canonical asset pagination. Each reply contains at most 24
rows and identifies project, epoch, canonical revision, durable revision and
document hash. The creation fixture accepts the assembled listing only when
every page has the same identity and every row is present exactly once. The
legacy unpaged listing explicitly reports truncation. Focused checks pass 2,677
cases on Linux and i686 plus 28 paging contracts on each platform. Receipts:
`out/preview9-assets-pages-receipt.json` and
`out/preview9-assets-pages-postcommit.json`.

The 96,364,544-byte DLL retains Kit 0.3.6 and the exact supported LAA executable.
Two fresh private games cold-open the C35 Saved adventure and share the same
419 placements, 119 classes, five acts and authored semantic hash. Both native
asset listings retrieve all 119 classes in five bounded pages, without changing
revision or history. Evidence: `out/preview9-c38-cold-shared-r2/result.json` and
`out/preview9-c38-assets-native-proof/result.json`.

The initial launch used a stale resolution-list index and selected 800x600.
The cold-open fixture stopped before browser input. Normal native Options
identified the current 1024x768@60 index as 11, followed by normal retirement
and restart. Resolution indices depend on the current monitor mode list; native
fixtures must verify actual viewport dimensions before input. No native fault
occurred. The subsequent two-player run returned normally to Galaxy before
cooperative runner shutdown.

Four private games then open stock Adventure Town with 419 placements, 118
classes and five acts. All four simultaneously enter different native editors:
Creature, City hall, Military land and Spaceship. Each guest cancels and returns
independently while the host remains designing. Host cancellation returns last.
All four retain revision/history zero, strict convergence and zero faults.
This verifies entry/cancellation and independent return, not creation saving.
Evidence: `out/preview9-c38-four-independent-creation-cancel/result.json`.

All four participants separately exercise Crew capacity, add, replacement,
lock and removal through native controls. Every case creates one transaction
and one history item, then verifies Undo, Redo and Undo again against the exact
previous Crew subtree while all four scenes converge. Capacity runs through
revision 16; retained setup selects capacity and an Ace roster member.
Add finishes at revision 33, replacement at 50, lock at 66 and removal at 82.
All cases retain zero native faults. Replacement uses Aepox and restores Ace.
Evidence: `out/preview9-c38-four-crew-capacity/result.json`,
`out/preview9-c38-four-crew-add-r2/result.json`,
`out/preview9-c38-four-crew-replace/result.json` and
`out/preview9-c38-four-crew-lock-remove/result.json`.

The initial Crew add fixture searched for an unavailable stock card and left
the picker open without committing. Normal cancellation restored revision 17.
The fixture now selects native Name sorting before searching for a measured
available stock asset. This is a fixture correction; it does not change product
admission or Crew behavior.

C37 also verifies native water changes from host and guest with checked
Undo/Redo, ending at revision 18 with strict convergence and zero faults:
`out/preview9-c37-two-water/result.json`. Its later environment fixture did not
execute because the bounded runner lease had expired. Runner termination is
recorded separately from a native crash; the last revision was already durable.
This provides no acceptance evidence for other Terraform control families.

C38 runs the measured Basic Tools raise-terrain recipe with four participants.
Host and guest both hold their native strokes before either release; the draft
does not advance during either tentative stroke. Releases create exactly two
ordered effects and one history item per author. Per-author Undo preserves the
other accepted effect byte-for-byte; Redo restores native recipe order. Cleanup
returns the exact baseline planet fields at revision 90. All four participants
remain strictly converged with zero faults. A second check leaves the host in
the Creature Editor while the guest commits, undoes, redoes and undoes a stroke.
Host cancellation materializes the latest revision 94 and all four scenes agree.
Evidence: `out/preview9-c38-four-terrain-simultaneous/result.json` and
`out/preview9-c38-four-terrain-host-away/result.json`. These measured controls
do not establish acceptance for the full Terraform tab.

The two remaining guests repeat the overlapping-stroke check as authors 97/98,
with host and first guest observing. Both held histories, exact per-author
Undo/Redo and unchanged unrelated planet fields pass. All four converge at
canonical/durable revision 102 with zero faults. Evidence:
`out/preview9-c38-four-terrain-guests/result.json`. Thus each of the four roles
authors the measured Basic Tools effect in this batch.

The four-player batch leaves the party and returns every native editor normally
to Galaxy with zero faults before cooperative runner shutdown. The runner exits
0 within its lease. Evidence: `out/preview9-c38-four-retirement/result.json` and
`out/windows-lab/preview9-c30-four/processes.json`.


### C39 serialization timings and creation-save calibration

C39 retains 249 production inputs without drift. Its co-op DLL is 96,400,896
bytes; Kit SHA-256 is
`2b3b894921d05483e1893be44dc7345fd781cff5d0e91638d9b7f1502fc54046`
(7,501,824 bytes). Kit still reports version 0.3.6; exact payload hashes identify
this candidate. The optional exact-byte RefPack memo now retains up to eight
streams within the same 32 MiB staging-inclusive limit. Actual-i686 extracted
original/memo checks pass 104 cases, including LRU and byte-budget eviction,
allocation failure, contention, reentry and unchanged original output. Evidence:
`out/editor-preview9-refpack-working-set/receipt.json`. This is no relaxation of
Native ownership, unsupported-change detection or durable confirmation.

Seven passive QPC/frame spans distinguish the existing two integrity Writes,
BASE Read, producer Write, AFTER Read, fixed-point Write and inverse Write.
They add no Native calls or authority. All status fields fit the bounded console
reply on the retained real baselines. Source/production receipt:
`out/preview9-candidate39-build-receipt.json`.

Fresh private processes 46192/39952 cold-open the exact C35 Saved source with
419 placements, 119 classes, five acts and the same authored semantic hash,
then share at revision zero without a warning or recovery action. Both memo
instances are verified with no failures or bypasses. Evidence:
`out/preview9-c39-cold-shared/result.json`.

The first two-warm-up/six-edit timing probe overlaps a focused Native-fixture
compiler. Its functional records are retained, but `qualification.json`
explicitly disqualifies its performance. The replacement runs after compilation
is stopped. Host/guest submission-to-confirmation maxima are 51.3873/171.1228 ms;
release-to-confirmation maxima are 526.604/685.964 ms. Worst commit-window frame
gaps are 106.6277/142.0186 ms, still above 100 ms. The larger cache does not close
the frame budget. No full 30-per-author run is repeated. Both games independently
prove strict convergence at canonical/durable revision 16 with zero faults.
Evidence: `out/preview9-c39-commit-probe-two-town-r2/summary.json`, full result
SHA-256 `d3a1f59ed19ce4851b1991c8c1955adb45513b0983ba7c52516774dda2d4629e`,
and `out/preview9-c39-commit-final-proof/result.json`.

On the guest observer, BASE/AFTER Reads each cost about 8.2-8.7 ms, the producer
Write about 20.1-21.1 ms and the remaining measured Writes about 8.8-10.2 ms each.
These are individual inclusive call spans, not disjoint end-to-end accounting.
The host observer uses the existing recent-proof path, so the two specifically
instrumented integrity call sites record zero there. Frame acceptance remains
open; the extra timings locate work rather than claiming a speedup.

The promoted staged creation fixture preserves process/profile/project/epoch,
participant generations, creation serial and Native request identity between
stages. A workflow-head digest rejects stale or replayed checkpoints. Ordinary
Done is attempted only after fresh Native zero-blocker validation. These checks
remain fixture controls, not a different product Save path.

The historical C7 instantaneous Creature drag recipe executes but leaves C39
Mouthless with no changed flags or name. No Done is clicked. A fresh visible
card/body/name calibration and ordinary held drag instead attach the mouth and
name the creature; Native finish state becomes zero with changed flags 1/1.
The host then saves normally, adding exactly one immutable class and one
history item at revision 17. Existing cast entries, 419 placements and five acts
are preserved; both returned scenes strictly converge with zero faults.
Evidence: `out/preview9-c39-host-creature-design/result.json`,
`out/preview9-c39-host-creature-design-r2/result.json` and
`out/preview9-c39-host-creature-save/result.json`. This closes the host Creature
save path only; other family/role and replacement checks remain separate.


The same freshly calibrated Creature recipe passes native guest Save, adding
one class/history item at revision 18 and preserving all prior cast entries.
Both scenes strictly converge with zero faults:
`out/preview9-c39-guest-creature-save/result.json`. Guest Undo/Redo removes and
restores only that class at revisions 19/20. The initial history fixture wrongly
required an unchanged content revision on Redo; the product correctly advances
it to 20 while restoring the same immutable key, behavior and settings. The
original failed fixture remains retained. Read-only validation of both original
participant logs proves the corrected expectation without repeating inputs:
`out/preview9-c39-creature-history-audit/result.json`. Host Undo/Redo then passes
at revisions 21/22, preserving the guest class byte-for-byte:
`out/preview9-c39-creature-author-history-r2/result.json`.

A later diagnostic mistake interrupts the batch before shared-asset replacement
is exercised. Root supplies control ID `05D17546` as the window-pointer argument
of `coop.ui tree 3 visible 05D17546`. The existing command passes it directly to
`dump()`, which calls `sui::visible` on the invalid candidate. The host exits
with access violation `0xC0000005` at co-op RVA `0x2F2964`, ECX `05D17546`, reading
`8CB605F9`. Symbolization resolves `sui.cpp:180` via `flags()`/`visible()`, reached
from `c_ui` at line 645. This is an invalid diagnostic invocation, not a native
creation, paging or gameplay reproduction. The runner terminates the guest
with code 1 after observing host exit. The previous observed revision 22 was
durable on both peers; both exact private recovery directories are copied after
process closure (12 files, 5,467,262 bytes). No personal installation/profile is
used. Crash/dump/log and recovery evidence:
`out/preview9-c39-ui-root-crash/receipt.json` and `recovery/receipt.json`.
Explicit-root membership validation is required before further scoped UI reads.


### C40 inspector fix and cold-recovery grounding failure

C40 SHA-256 is
`c6792dfa3692164b88bdf795fc545ad2186609283b369f1498dcde50ab7a5a62`
(96,499,200 bytes); Kit is unchanged from C39. All 250 retained production
inputs remain unchanged through build. Explicit diagnostic roots now require
strict parsing and two bounded membership walks from the current native main
window, without dereferencing the supplied candidate. Linux and actual-i686
fixtures each pass 134 checks; the affected Sui object builds. Evidence:
`out/editor-preview9-c40-sui-root/receipt.json`.

Private processes 22536/43896 verify the exact formerly crashing command is
rejected, null is rejected before inspection, and both a fresh main pointer and
a fresh child pointer remain inspectable. The native fault count stays zero and
the document revision stays 22. Evidence:
`out/preview9-c40-ui-root-regression/result.json` and
`out/preview9-c40-ui-root-positive/result.json`.

Cold recovery of the preserved C39 draft reaches revision 22 and 121 classes,
but independent placement validation freezes publication. Placement 4294967302
has raw XYZ `-459.84436,116.277344,187.374207` instead of committed
`-459.844482,116.277374,187.374252`; quaternion, scale and property count match.
This is the exact previously observed C28 grounding pair. The cold transition
performs an explicit terrain Load/Rebuild after canonical restore; the existing
grounding observation precedes that additional rebuild. No tolerance or changed
committed position is accepted. Subsequent automatic retries consume the failed
one-shot transition and obscure the first mismatch with an expired-ticket error.
Evidence: `out/preview9-c40-recover-creations/result.json` and
`out/preview9-c40-recovery-failure-diagnostic/closed-evidence/`.

Both players' checkpoint and journal bytes remain identical to the preserved
C39 files, verified twice while running and again after closure. Explicit End
retires the failed attempt; the host discards the private native scene and both
games return normally to Galaxy with zero faults. The runner exits 0. Evidence:
`out/preview9-c40-recovery-failure-diagnostic/checkpoint-preservation.json`,
`closed-evidence/receipt.json`, and
`out/preview9-c40-frozen-retirement/result.json`. Shared replacement and later
family saves are deferred until cold recovery passes.


### C41/C42 cold-recovery continuation

C41 builds with 250 unchanged inputs but is never deployed: its new complete
comparison initially rejects an intentionally absent legacy metadata section in
a controlled zero-placement fixture. The immutable qualification receipt marks
it ineligible. The corrected C42 retains known legacy metadata rather than
silently omitting its guard. Controlled Native checks pass 71,666 cases and
Runtime transition checks pass 2,661, including original Native RED, cold resume,
unrelated mutation rejection and latching the first terminal failure. Final
optimized fixture/source receipts remain separate from these initial focused
results. Native source and build receipt:
`out/preview9-candidate42-build-receipt.json`.

C42's 96,532,992-byte DLL retains the C39 Kit hash. Native recovery of the same
preserved revision-22 draft gets past the original position mismatch but fails
at the next different field, placement 4294967549's quaternion. Actual is
`-0.588576794,-0.170447201,0.266058296,0.744144619`; committed is
`-0.588576674,-0.170447186,0.266058296,0.744144559`. XYZ is exactly
`-296.423096,410.628448,130.238693` on both sides, scale is 2 and property counts
are 4/4. The new failure latch retains the first placement error without
expired-ticket retry spam. No authored quaternion or tolerance is changed.
Evidence: `out/preview9-c42-recover-creations/result.json` and
`out/preview9-c42-recovery-quaternion/result.json`.

Both games again End and return normally to Galaxy with zero faults before the
runner exits 0. Both original checkpoint/journal pairs remain byte-identical
after shutdown. Evidence:
`out/preview9-c42-frozen-retirement/result.json` and
`out/preview9-c42-recovery-quaternion/closed-evidence/receipt.json`.
A complete exact rotation receipt across the canonical rebuild is still needed.


The later full portable repository run exits 0 with the new Sui and creation
contracts included. RE lint records 419 used symbols, 3,051 known and zero
supported-build problems; missing 2016-build symbols remain informational.
Log SHA-256 is
`f1a6dfa44cf288534018d69dc363ceb74ec5f6f5d5fade0b348112a2bea7f06b`.
Evidence: `out/preview9-c42-portable-after-fixtures.log` and its `.receipt.json`.
Native/Runtime recovery source work continued separately, so this is not a
complete frozen Native acceptance or a lab-smoke result.


### C44 actual recovery and shared Creature replacement

C43 is retained but never deployed; a final raw owner/roster closure is added
after Native Load and before Rebuild. C44 retains all 250 production inputs
without drift, with a 96,569,856-byte co-op DLL and unchanged C39 Kit. The
canonical rebuild now pins the existing rotation owner, immediately samples
derived XYZ/quaternions before policy callbacks and uses the existing guarded
native rotation repair. It restores only the independently canonical artifact;
no persistent gesture rotation authority or float tolerance is introduced.

Final strict actual-i686 Native checks pass materialization 73,241, rotation
1,293 and rotation-capture 3,294. Runtime passes terrain transitions 2,661 and
recovery lifecycle 2,026. All 115 combined pinned inputs and postcommit hashes
remain unchanged. Exact C42 sources reproduce the second quaternion failure
under the new fixture; the corrected source passes that same case and the
post-sample third-quaternion, XYZ and expired-owner negatives. Evidence:
`out/editor-preview9-cold-recovery/receipt.json`, `postcommit.json` and
`c42-red-receipt.json`.

Fresh private processes 57676/60028 recover the original C39 project and its
unchanged checkpoint/journal at revision 22. Host becomes ready and unfrozen;
guest joins and materializes the same 419 placements, 121 classes and five acts.
All class resource keys, content revisions, settings IDs and behavior hashes
match the exact pre-crash asset rows. Both independent Native checks agree,
with zero faults. Recovery intentionally creates a new epoch, so its context
hash differs from the previous session. No manual profile repair or replacement
is used. Evidence: `out/preview9-c44-recover-creations/result.json`.

The host then opens the guest-created Creature's native property panel via two
ordinary held card taps and uses Edit. Its active Edit request already contains
the resolved Creature family ID465C50BA, false New flag and exact guest source
key. The initial fixture assumed genericFFFFFFFF and is retained as a tool
failure; the source sampler now accepts only generic or the exact resolved
family while preserving full source/model/owner checks. Windows/Linux contracts
pass 165. Normal cancellation of that first entry preserves revision22; a fresh
measured entry validates and changes the creature name. Native finish is zero.

Done presents the normal Replace/Save New confirmation. Root selects Save New
once to preserve the prior native content. Canonical admission replaces only
class8589935936:1 at revision23, assigning a new immutable key and preserving its
settings/behavior plus every unrelated row. Class/placement/act counts remain
121/419/5; exactly one `Replace shared asset content` history item is added.
Both returned Native scenes strictly converge with zero faults. The first
post-confirmation assertion mixes JSON lists and parser tuples; a separate
read-only normalized comparison proves the actual result without repeating Save.
Evidence: `out/preview9-c44-host-edit-guest-enter-r3/result.json`,
`out/preview9-c44-host-edit-guest-confirm/result.json` and
`out/preview9-c44-shared-edit-final-proof/result.json`.

### C44 guest save with a designing host and Native Save rejection

The host starts a separate New Creature session (serial 3), adds a mouth and
names its unsaved design `Preview9C44HeldHostDraft`. The native save state remains
6, with no pending command. Supported-image inspection verifies states 0 and 6
are idle; 1 through 5 process a save. The creation fixture now accepts exactly
0/6 while preserving request, source, model, session and command fences. Its
final source also handles only the measured four-button Edit Save New dialog,
with one-shot dispatch and a fresh identity closure. Windows and Linux each pass
438 contracts plus 28 pager checks. No production input changes in that tool
commit. Evidence: `out/preview9-c44-edit-confirm-source-final/receipt.json` and
`postcommit.json` (commit `70758da`).

The guest independently creates and saves `Preview9C44GuestHostAway`. Its native
Done returns to the adventure while the host remains in the same serial-3
design/request. Exactly class `8589935937:1` and one `Create adventure asset`
history item are added at revision 24. Both canonical copies are durable and
equal, with 122 classes, 419 placements and five acts. Only the guest's active
adventure is claimed as strictly rendered; the host's Native proof is explicitly
not applicable while designing. Evidence:
`out/preview9-c44-guest-hostaway-save/result.json`.

The host then cancels its unsaved design. The first owner-run return probe waits
without handling the ordinary discard dialog and times out. A fresh read-only
capture identifies the exact native two-button confirmation, not a product
return failure. The original failed probe remains retained. A separate bounded
continuation pins project/epoch, serial 3, the original Native request, idle save
state and pending Cancel command, then confirms Quit once. Both games return
ready at revision 24, with identical 122-class asset rows, matching strict Native
checks, unchanged history and zero faults. Evidence:
`out/preview9-c44-held-host-return/result.json`,
`out/preview9-c44-return-diagnostic/result.json`, and
`out/preview9-c44-held-host-confirm-return/result.json`.

One ordinary adventure Save then completes its native success dialog. Sharing
the new recovery checkpoint freezes the host with
`Selected custom dependency was not admitted immutably`. The guest retains the
committed revision-24 scene and strict convergence; both fault counters stay
zero. The host's saved source is `408A0000!2913E324.366A930D`; its independent
check detects the changed native save bytes. This is an actual unresolved
dependency-admission failure, not a completed Save or cold-reopen check. No
second Save is issued. Evidence:
`out/preview9-c44-creatures-native-save/result.json` and
`out/preview9-c44-save-diagnostic/result.json`.

Explicit End and normal native discard return both private games to Galaxy with
zero faults before the bounded runner stops (runner exit 0). After process
closure, both recovery directories and the original 5,293,292-byte host
`EditorSaves.package` are retained with two identical reads. The DBPF has 121
records and one exact saved-root match. Package SHA-256 is
`65935d32232541c7fce5012f1d5af8ee2aa86863bb0d69944b104044a104524f`;
raw saved-root SHA-256 is
`7183cc0ff46f66dbc0390db15affffe45b25c29ae8ee4a1726515fda5aeb2ebe`.
Initial artifact probes stop on the live project lock, the old global
64-package inventory bound and a graphics-cache size bound; the successful
closed probe reads only the exact native `Spore/EditorSaves.package` established
by the C35 carrier. No profile writes, conversions or enlarged bounds are used.
Evidence: `out/preview9-c44-frozen-retirement/result.json` and
`out/preview9-c44-save-diagnostic/closed-final-r2/receipt.json`.

### C45/C46 generated owner caches and actual cold reopen

Diagnostic-only C45 preserves validation and names the rejected record:
`40626200!7D8C23AC.0F43029A missing admitted record`. A fresh recovery of revision
24 passes all 122 classes before one ordinary Native Save reproduces that exact
failure. Both games retire normally with zero faults and runner exit 0. The
original DBPF and recovery files remain retained. Evidence:
`out/preview9-c45-recover-creatures-r24/result.json`,
`out/preview9-c45-creatures-native-save/result.json`,
`out/preview9-c45-frozen-retirement/result.json`, and
`out/preview9-c45-save-diagnostic/closed-final/receipt.json`.

Commit `db00f52` aligns Saved dependency collection with the immutable alias
producer's existing generated-cache omissions. A missing primary/Summary/
`0F43029A` record can be omitted only for the exact selected private immutable
owner with a matching selected and admitted BEM body. Every admitted record
still requires byte equality; unknown or foreign records, malformed sidecars,
missing/changed bodies and conflicting retained companions reject. Both Windows
and Linux pass 145 codec cases/21,232 checks; affected Native gate passes 17,718
and Linux real-filesystem storage passes 24 cases/15,015 checks. The exact
pre-fix source fails the new generated-cache case. Source/input receipts:
`out/editor-preview9-saved-cache/receipt.json` and `postcommit.json`.

C46 freezes 250 production inputs without drift, producing a 96,595,456-byte
DLL with unchanged Kit. Actual recovery again reaches revision 24 and strict
122-class agreement. One Native Save now succeeds with SavedApplied 2/2,
unchanged collaboration history and unchanged authored projection. Exact saved
source is `408A0000!2913EE8A.366A930D`. The original 5,656,694-byte DBPF has
SHA-256 `06a67d4b42c3373ea5ed098cb0f22e63a381b236d4139f52de0ad1daa4bc0748`;
its 60,918-byte raw root has SHA-256
`b70a1968f457c56bc932892b7fcca6da5bbf17044f8424f4e3a14f1ae7504dea`.
Evidence: `out/preview9-c46-recover-creatures-r24/result.json` and
`out/preview9-c46-creatures-native-save/result.json`.

The carrier acquisition now reads only the verified native
`Spore/EditorSaves.package`, excluding growing CAS and graphics caches from the
candidate inventory. The focused acquisition check uses the genuine retained
DBPF plus 70 unrelated cached packages, a foreign key and a missing exact native
container; all three cases pass. It does not raise file-size or inventory bounds.
Evidence: `out/preview9-native-save-carrier-focused-check/receipt.json`.

After normal retirement and runner exit 0, fresh games reopen the exact Saved
source without recovery. The first literal projection-fingerprint assertion
fails and remains retained. Independent complete decoding finds exactly 16
changed bytes: the four new local primary assets' machine fields become zero
from `5523CA25`. Their resource keys, model IDs and all-ones server values are
identical. Every other decoded field is byte-identical across 1,259,068 bytes,
including all marker poses, acts, Captain and Crew. Planet semantics match; the
outer changes are the unused scalar-key word and correct compressed-container
length. No production comparator or reference validation is relaxed. A bounded
continuation verifies those exact inputs/transitions before hosting the cold
file; both participants strictly converge at a new revision-0 context, with
122 classes, 419 placements, five acts and zero faults. Evidence:
`out/preview9-c46-cold-shared/result.json`,
`out/preview9-c46-cold-comparison/comparison.json`, and
`out/preview9-c46-cold-shared-continuation/result.json`.

The host then constructs and saves a native UFO, adding class `8589935938:1` at
revision 1. The guest repeats the retained complete 15-step construction and
saves class `8589935939:1` at revision 2. Body and cockpit alone retain blocker 4;
the measured propulsion part clears it and gives native finish zero. Both saves
preserve existing cast rows and add one class/Undo item each, with strict Native
agreement and zero faults on both returned participants. Counts become 124
classes, 419 placements and five acts. Evidence:
`out/preview9-c46-host-spaceship-save/result.json`,
`out/preview9-c46-guest-spaceship-save/result.json`, and
`tools/tests/fixtures/editor_creation/spaceship-minimal-c46.json`.

Host and guest next construct native Houses, including Body, Door and Roof.
The first host body drop above the construction floor retains no part or budget
change and is excluded from the regression recipe. Correct floor placement plus
Door and Roof clear all native blockers. The host adds class `8589935940:1` at
revision 3; the guest independently replays the complete 15-step recipe and adds
`8589935941:1` at revision 4. Both returned participants strictly agree with zero
faults; all prior cast rows and 419 placements/five acts remain unchanged.
Native adventure Save of all 126 classes then passes SavedApplied 2/2, unchanged
history/semantics and original DBPF acquisition. Exact source is
`408A0000!2913EF53.366A930D`, DBPF SHA-256
`05afdc26325320be77d4bea631a1bdf8ecaf5f8943bfcf46f4d2ca6031fc7544`;
raw saved-root SHA-256 is
`ddbbd9a9ed0a08056ad7b69d8ee57c19d34863291daabf649011d0037923d757`.
This later 126-class file has not yet been cold-reopened. Evidence:
`out/preview9-c46-host-house-save/result.json`,
`out/preview9-c46-guest-house-save/result.json`,
`out/preview9-c46-all-new-models-native-save/result.json`, and
`tools/tests/fixtures/editor_creation/building-house-minimal-c46.json`.

The explicit Military Land route also passes from both roles. The guest's
measured body/cockpit/paired-wheel design reaches native finish zero and adds
class `8589935942:1` at revision 5. The host replays the retained 17 steps and
adds `8589935943:1` at revision 6. Both saves preserve every prior cast row,
create one history item each and return strict scenes with zero faults. The
draft now has 128 classes, 419 placements and five acts. Evidence:
`out/preview9-c46-guest-vehicle-save/result.json`,
`out/preview9-c46-host-vehicle-save/result.json`, and
`tools/tests/fixtures/editor_creation/vehicle-military-land-minimal-c46.json`.

Saving this later draft after the earlier Native Save does not open a success
dialog. The original fixture times out; a fresh diagnostic records the actual
reason: `Native adventure save is unavailable (reason 5)`. Both canonical and
Native scenes remain strictly equal at durable revision 6, with zero faults;
native coverage stays at revision 4. Creation admission/return carries the old
clean native dirty sidecar across accepted class changes. This is a separate
open native dirty-state bug, not another dependency or corruption failure. No
second Save is issued. Evidence:
`out/preview9-c46-four-families-native-save/result.json` and
`out/preview9-c46-final-save-observe/result.json`.

Test Together then uses explicit readiness from both participants, with no
preview before the final confirmation. Both games enter one nonzero adventure
run, reach natural native simulation state 3, advance their clocks and retain
live reciprocal captain puppets. Each author moves through ordinary W input;
both participants observe positions within the existing one-metre puppet error
bound. Host Return restores revision 6 and all 128 cast rows with unchanged
history, matching strict Native checks and zero faults. This proves shared
native simulation/movement and return; it does not qualify every objective or
interaction. The first driver hits a Python conditional-variable error after
releasing the host movement key; its measured continuation completes both
movements and returns normally, then hits an incorrectly assumed artifact
field. Both failed tool receipts remain. A separate read-only final check
verifies the recorded movement evidence and the actual restored canonical/hash
and history without relaunching the test. Evidence:
`out/preview9-c46-group-after-creations/result.json`,
`out/preview9-c46-group-after-creations-resume/result.json`, and
`out/preview9-c46-group-final-proof/result.json`.

Both games subsequently return normally to Galaxy with zero faults and the
runner exits 0 before its lease expires. The latest durable project is
`f8df82ea8c71c31574fd6a062a79030f`, revision 6. Both closed recovery directories
(ten files each) and the original native package are preserved with identical
double reads; its exact Saved root is still the previously verified revision-4
root, while the newer two vehicle admissions remain in durable recovery.
Evidence: `out/preview9-c46-final-retirement/result.json` and
`out/preview9-c46-final-closed-preserved/receipt.json`.

### C47 dirty-state verification and default-off preparation experiment

Commit `c617dcf` marks only the accepted prepared Native dirty sidecar as dirty,
including the adopted-live branch. Saved-history counters, metadata/import
flags and rollback's original sidecar remain unchanged. The strict i686 focus
passes 2,675 checks, including clean cancellation, rejected durability, foreign
edits while the host designs, both creation authors and absent optional
sidecars. Real pre-fix Runtime fails the new post-admission dirty assertion.
Evidence: `out/editor-preview9-c47-native-dirty/receipt.json` and `postcommit.json`.

Native producer commit `a7591cc` and consumer commit `53183ad` add a separate
default-off two-frame pose experiment. It retains one private AFTER resource,
keeps persistence behind explicit Native preparation/application, rechecks live
integrity and original owner before mutation, and settles teardown after active
Core callbacks. Source review identifies and fixes borrowed owner identity,
cached-proof scope/generation and final-Release reentry gaps. Strict actual-i686
checks pass Native staged/checked application 10,426 and existing synchronous
poses 6,514; Runtime staged consumer 2,835, dirty 2,675 and legacy Session
durability 480. This is controlled correctness evidence, not a frame-budget
claim. Receipts: `out/editor-preview9-saved-cache/native-stage-o2-receipt.json`,
`native-stage-postcommit.json`, and
`out/editor-preview9-pose-commit-final/receipt.json` with `postcommit.json`.

C47 retains 251 production inputs without drift and produces a 97,309,696-byte
DLL; Kit remains unchanged. Fresh private processes 59996/15820 recover the
latest revision-6, 128-class draft. The initial console probe exceeds its
25-second socket timeout while Native loading is in progress. A fresh read
finds recovery completed and healthy. The bounded continuation does not repeat
Recover; it validates the current project/revision and exact preserved asset
rows, joins the guest and proves strict agreement and zero faults. Native
loading calls then use a longer bounded socket timeout. Evidence:
`out/preview9-c47-recover-all-models-r6/result.json`,
`out/preview9-c47-status-now/result.json`, and
`out/preview9-c47-recover-all-models-r6-resume/result.json`.

One baseline adventure Save succeeds at revision 6. The host then repeats the
verified native UFO recipe and admits one more class at revision 7. The next
ordinary Save, with an already owned native copy, succeeds with SavedApplied
2/2 and exact DBPF retention. This is the actual reproduction that closes the
C46 reason-5 bug rather than relying on recovery's initial Save As. The latest
129-class saved source is `408A0000!291403C5.366A930D`. Evidence:
`out/preview9-c47-dirty-baseline-save/result.json`,
`out/preview9-c47-dirty-ufo-save/result.json`, and
`out/preview9-c47-dirty-post-creation-save/result.json`.

The full portable repository suite subsequently exits 0 with all 251 production
inputs unchanged. Log SHA-256 is
`86112b9d48dc41b87db9029024fee52f7f1bf74796e46d5073dda8d455a248fc`.
Evidence: `out/preview9-c47-repository-tests.log` and `.receipt.json`.

After normal native retirement and runner exit 0, fresh processes cold-open
that exact 129-class file. The reusable value-only comparison verifies complete
decoded, prefix and container-envelope equality except the exact qualified
local machine-ID reset. Both participants then share and strictly converge at
a new revision-0 context, retaining 419 placements/five acts and zero faults.
Both report zero grounding entries and no active rotation ticket. Comparator
checks include genuine C46/C47 artifacts and eight adversarial tests on Windows
and Linux; it changes no production validation. Evidence:
`out/preview9-c47-dirty-retirement/result.json`,
`out/preview9-c47-cold-baseline/result.json`, and
`out/preview9-c47-saved-projection-tool/receipt.json` with `postcommit.json`.

The first two short baseline attempts produce no valid timing samples. They
stop during unmeasured warmup with a misleading placement-visibility error.
Fresh diagnostics show healthy scenes; the actual failing object response is
`Native Adventure Editor is not ready for a snapshot`. The legacy three-second
gesture retry issues another action while staged Native capture still owns
input. Two host warmup moves eventually commit at revisions 1 and 2. These
failed fixtures remain retained; their outputs are not performance acceptance.
Capture's Native phases take about 5-37 ms, but actual frame IDs advance only
every roughly 550 ms in this reproduction. C47 default-off pulse/idle gates
match C46. A possible outside-task queue starvation path is under passive
diagnosis; it is not yet proved or fixed. Evidence:
`out/preview9-c47-commit-baseline/result.json`,
`out/preview9-c47-commit-baseline-r2/result.json`, and
`out/preview9-c47-calibrate-view-r2/result.json`.

The personal installation remains preview 8 and its exact 4 GB executable is
unchanged. The existing shortcut still resolves to its preview-8 launcher.
Read-only proof: `out/preview9-c47-personal-unchanged/receipt.json`.
