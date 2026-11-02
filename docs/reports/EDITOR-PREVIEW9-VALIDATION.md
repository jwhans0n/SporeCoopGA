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
