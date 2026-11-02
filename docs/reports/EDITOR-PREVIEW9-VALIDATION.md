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
