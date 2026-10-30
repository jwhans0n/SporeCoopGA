# Automatic editor sharing and native controls

The preview 3, 4 and 5 evidence is retained below. Preview 6 implementation and
current native validation appear at the end of this report.

Current development candidate: preview **6**, protocol **21**, branch
`feat/ga-editor-multi-act`. See the appended native matrix for the exact candidate
hashes and test limitations. Earlier installation statements describe their
individual validation runs.

Development preview `0.7.5.0-ga-editor-preview.3`, protocol **18**, branch
`feat/ga-editor-native-flow`, 2026-10-27. This is a focused integration report,
not completion of the overnight release acceptance matrix. The published
0.7.4.0 gameplay release is unchanged.

## Implemented behavior

- Automatic sharing from the host's ready native Act 1 editor, including a
  never-saved empty draft. Hosting before or after opening the editor is supported.
- Live terrain capture into an owned scenario copy, separate private project
  identity, retained source provenance and checkpoints without a native save.
- Automatic guest following, baseline validation and application hold. An
  unrelated local draft receives Save/Discard/Cancel protection. Native save and
  exit cancellation preserve that draft; joining Cancel leaves the party.
- Reliable editing/loading/play activity, independent of movement replication,
  in the ordinary lobby and discovery. The separate editor page is removed.
- Native Undo/Redo and keyboard history use per-author transactional validation.
  Host Save uses native copy ownership; guest Save exports a pinned local copy
  under a group hold and restores its shared resource identity and dirty state.
- Guest Play requests host approval. Compact prompts show all participants and
  start everyone unready. Native Build returns the party to the held draft.
  Native host Exit preserves party membership; guest Exit leaves the party.
- Bounded, authenticated editor commands scoped to project, epoch, revision,
  operation serial and participant generation. Existing GA/captain bundles are
  unchanged. Mapping version2 carries bounded live name/description/tags and
  genre; mapping version1 and previous recovery manifests remain readable.

## Deterministic evidence

These Win32 fixtures use controlled native doubles where indicated. They do not
prove native game behavior by themselves. All binaries were rebuilt before use.

| Check | Result |
| --- | --- |
| Focused automatic/native-flow runtime | 346 assertions passed |
| Runtime quick regression | 6,205 assertions passed; intentionally excludes the large closure sweep |
| Native Save/command adapter | 346 assertions passed |
| Native cancellation adapter | 1,683 assertions passed |
| Project resources, metadata, cache and recovery encoding | 5,167 assertions passed |
| Document validation, including empty native draft | 2,898 assertions passed |
| Editor wire codec and command direction/bounds | 8,843 assertions passed |
| Editor controller with real UDP and fake native adapters | Final run: 15,446 assertions passed, including loss, duplication and reordering; traffic-dependent check counts vary |
| Transport generation | 63 assertions passed |
| Lobby page routing | Passed |
| Exact-image Save/native command ABI | 17 symbols, 19 call edges, 30 layout/branch receipts verified |
| Native symbol lint and whitespace | Passed |

Runtime cases cover first copy/subsequent host Save, guest export isolation and
cancellation, wrong export completion identity, automatic start/join, stale scope,
handoff cancellation, asynchronous native Exit, host party preservation and guest
party leave. Blocked Undo changes nothing; existing conflict and lifecycle
regressions remain in the quick suite. The approval reply is allowed after the
host transitions into preparation and is matched to the guest's pending request.

## Actual hidden, muted game checks

Only the exact GOG executable is supported:
`dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`.
Native runs use the private marked lab installation and isolated profiles,
instances 73-76. Ownership verifies PIDs, executable/DLL hashes, hidden windows
and muting before input. All mutations use ordinary native mouse/keyboard input
and public party controls. No active native creation database is rewritten.

- C4 `out/editor-native-flow/probe-c4/result.json`: two players automatically
  opened the same never-saved empty Alpine Valley draft, rendered matching
  terrain, retained zero native-save coverage and showed Adventure Editor in
  the normal lobby. Native host Exit returned both to the menu with their party
  connected. All four cohort processes later closed normally with exit code zero.
- C5 `out/editor-native-flow/toolbar-c5-r1/result.json`: unsaved authored draft,
  native Move, toolbar Undo, keyboard Redo, host Save and exact native applied
  acknowledgements passed. Guest Save created a distinct native local copy
  without changing canonical state or host save coverage. All four clients
  joined automatically; guest Play produced host approval, then four explicit
  Ready prompts. No game entered play before the host confirmed. Native Play,
  Build return and group Exit passed. Host and guest local saved copies reopened
  through the ordinary native browser with matching authored semantics.
- The readiness prompt was visually inspected at 1024x768 with all four names
  and waiting states readable. It uses native styling and leaves the full party
  dialog closed.

Further final-candidate and draft-protection receipts are recorded with delivery
below. An interrupted fixture or a navigation/input calibration failure is not
silently counted as a native workflow pass.

C5 draft protection is recorded in `out/editor-native-flow/handoff-c5-r6`
(initial Cancel) and `handoff-c5-r8` (resumed native confirmation through save and
reopen). The modified unrelated draft survives native Discard cancellation;
confirming Discard follows the host. Guest native Exit cancellation preserves the
shared draft. Guest Save + Exit saves a copy and leaves the party. Save-before-join
completes its native save first, then follows the host; the protected copy reopens
with its authored state. Host native Exit cancellation and Save + Exit pass, with
the party retained after the latter. The resumed receipt does not claim its
earlier fixture navigation attempts passed.

## Findings corrected during implementation

- The native document validator originally required at least one cast class.
  Empty new adventures have acts and zero classes; that valid case is now allowed.
- Serializing the live scenario alone omits terrain. The verified native
  CaptureTerrain exporter operates on an owned copy without saving a creation.
- The GOG loader rejects private roots outside its configured creation group.
  C2's isolated guest crashed after an arbitrary group was used. Private roots
  now use collision-checked random instances in the verified native group;
  content/act validation runs before entering the editor.
- Native Exit queues an application change. Its last Build frame previously
  triggered automatic sharing again. A retiring-generation guard prevents it.
- C4 guest export failed native CanSave reason4: the unsaved name was stored in
  edit data outside scenario serialization. Live metadata now travels separately.
  Explicit copy saving also handles the native unchanged-metadata gate without
  permitting invalid captains or unnamed drafts to save.

ABI details and negative receipts are in the
[native save notes](../re/adventure-editor-save.md).

## Scope and remaining acceptance

Native placement editing remains restricted to Act 1 and templates already in
the draft. Class properties/import, synchronized act authoring, objectives,
conversations and global authoring controls remain held pending their native
transaction adapters. Terrain authoring, nested creation editors, independent
acts, history browsing/selective Undo and concurrent playtest/editing remain
later goals. Sharing an empty draft does not make it eligible for native Save
or Play; Spore still requires a name and valid authored captain as applicable.

Broad adventure/opaque-data round trips, sustained loss/adversity, exclusive
large-editor FPS, saturated memory and overnight cycle coverage were not repeated.
Use the [testing playbook](../EDITOR-TESTING-PLAYBOOK.md) before a public release.

## Delivery

Previous candidate C6 DLL SHA-256:
`5d0c5e0d1a358815b329e305848b9b08793e30847296da711bd651005163a702`.
Its final two-player empty-draft check is
`out/editor-native-flow/final-empty/result.json`. The versioned normal-input
driver is [coop_editor_native_flow.py](../../tools/tests/coop_editor_native_flow.py).
`out/editor-native-flow/final-toolbar/result.json` repeats the native toolbar,
copy isolation, four-player join/approval/readiness, Play/Build/Exit and both
native-copy reopen checks on this exact candidate. All pass.

C7 adds one-shot compact choices and a new handoff prompt identity after native
cancellation, so delayed input from the previous prompt cannot restart Discard.
An owned native Exit confirmation is also released when its sharing scope ends.
The current candidate DLL is
`7113d23c5717b94acdb5434d5c2315ca75da9e7f679cca815478ce66d7782805`.
These changes add targeted cancellation/identity assertions to the focused
fixtures. C6 returned all four processes normally to Galaxy and closed with
exit code zero before C7 deployment.

C7 `out/editor-native-flow/c7-toolbar/result.json` passes the four-player native
toolbar workflow, explicit approval/readiness and saved-file reopen checks.
`c7-handoff/result.json` records the unchanged existing creation's first Save
creating a distinct local identity and initial joining Cancel preservation.
Its immediate native Cancel input was ignored while Spore's dialog animated.
The focused continuation `handoff-c7-settled/result.json` waits for the actual
dialog, then passes Discard cancellation, Discard/follow, guest and host native
Exit cancellation, both Save + Exit behaviors, Save-before-join and protected
file reopen. This continuation passed; the earlier complete harness invocation
is still recorded as failed. The versioned driver now settles native dialogs
and requires their closure before accepting Cancel.

The local installation receipt records the final DLL, supporting files, backup
and read-back SHA-256 values. Profiles, creations, settings and saved captains
are preserved; the Two Players launcher keeps `coop_player1` and `coop_player2`.
The setup and matching pack are packaged separately from the published gameplay
release. Installation occurs only after personal sessions have ended.

Installed and read back successfully on 2026-10-27 at 19:18 UTC. Four changed
files were backed up under
`out/personal-editor-install/20261027T191825Z/installation.json`: coop.dll,
the native Save symbol map, installed version record and Two Players launcher
instructions. The launcher still starts the same two profiles. Preserved settings
and individual launcher hashes were verified. All four C7 lab processes returned
to Galaxy and closed normally with exit code zero; no personal session was active
when installation began.
# Preview 4 implementation validation, 2026-10-28

Worktree: `D:/Code/SporeCoop-wt/ga-adventure-editor`, branch
`fix/ga-editor-preview4`. Supported executable: GOG 2024 `dc04aee5`.
The personal installation and personal profiles have not been changed.

The editor portable target passed after replacing a default optional metadata
construction that Clang 14 with libstdc++ 13 rejected. Win32 editor fixtures also
passed, including 165 cast/endpoint checks, 8,891 wire checks, 3,603 native input
and cancellation checks, 6,824 runtime checks including the large closure path,
and existing two/four-peer UDP packet-adversity coverage. Runtime native-flow
selection passed 568 checks for solo host/guest testing and the separate group
request route. These are model/ABI fixtures, not native gameplay acceptance.

Evidence is under `out/editor-preview4/`: `portable-tests2.log`,
`win-fixtures-build.log`, `win-fixtures-results.log`, and native RE query receipts.
The focused native fixture is `tools/tests/coop_editor_preview4.py`.

Private native pair 91/92 was launched through `tools/windows_lab.py`, hidden
and muted. The empty never-saved draft synchronized successfully on protocol 19.
The first cohort stalled in D3D9 after first-use onboarding, with no caught game
faults. Reports `report-20261028-010344-i91.txt` and
`report-20261028-010343-i92.txt` were retained in the private installation logs.
The retry rendered and entered both native editors. Early import fixture runs
timed out searching a remembered empty `Recent Downloads` filter. No successful
stock/custom import, movement, or group gameplay acceptance is claimed by those
failed runs.

Later native evidence:

| Candidate and artifact | Observed result |
| --- | --- |
| C7, `cast-c7` and `native-ui-explore` | Guest stock import, checked class Undo/Redo, health 100 to 450 with no instances, inspector retained. The first Undo input still required a retry at this candidate. |
| C7, `playtests-c7` | Native solo Play/Build passed for both roles. Group launch rejected an unsaved native source; this exposed and led to removal of the old saved-key restriction. |
| C9, `playtests-c9` | Native solo for both roles, guest Test Together request, host approval, explicit readiness, reciprocal moving live captains, coordinated Build return, unchanged committed checks/history, zero caught faults. Shared interactions/objectives and four players remain unverified. |
| C9, `properties-c9` | Respawn changed as one transaction with zero instances. Multi-character speech reproduced a capture-queue overflow and frozen recovery state. |
| C12, `import-c12` | Guest native stock import, single-input checked Undo/Redo, and one physical drag places that asset after structure ownership arrives. |
| C12, `movement-c12` | One physical drag each for host and guest moves the existing placement, one canonical transaction and Undo item each. |

C12 contains native Speech/Captions and Advanced Behaviors mutation guards.
Speech's per-character native commits are retained as one owned before/latest
after until the exact dialog closes. Its 128-character i686 regression also
checks no mutation before class ownership and stale-epoch retirement. Fresh
native text and the rest of the property matrix are still running.

The complete repository `make test` passed in `full-repository-tests2.log`.
The initial run found a stale protocol-16 assertion in the general network
fixture; it now requires protocol 19 and rejects a protocol-18 HELLO/header.
Later native input changes are covered by the rebuilt i686 fixture. Native
symbol lint passes with zero problems. The follow-up results below supersede
the remaining native movement/import gates in this initial checkpoint.
No merge or release was performed.

## Preview 4 follow-up matrix

All native evidence below used the private GOG 2024 installation, invisible and
muted profiles `winlab_preview4_c1_91` through `94`, via `windows_lab.py`.
Candidate C26 SHA-256:
`c374fb4974142787da80257575d710cdc2541ec2773dc2a3d8a822d129038feb`.

| Evidence under `out/editor-preview4/` | Result |
| --- | --- |
| `properties-c12-resume`, `speech-c12` | Both roles: health, awareness, speed, damage, stance, respawn, visibility, speech and captions. Native text coalesces to one synchronized action. |
| `sound-c17` | Both roles: emitter movement and single Undo, exact pitch/gain. |
| `sound-c19-four` | Cold recovery followed by movement/Undo from all four editors, matching native checks and zero caught faults. |
| `endpoints-c18` | Both entrance and destination movement/Undo by host and guest; the other endpoint is unchanged. |
| `ball-c18d` | Both roles: actual white sphere rotation across quaternion axes, unchanged position/opposite endpoint and Undo. |
| `playtests-c19-four` | Solo Play/Build for every participant with observers remaining in editors; guest Test Together request, host approval, all explicit ready, one run, reciprocal moving remote captains, coordinated return, unchanged draft/history. |
| `remove-c21b`, `remove-c26` | Host/guest populated class removal, Undo, Redo and Undo; four matching checks, retained stable placement identities and authored fields. C26 gates before palette card/history mutation. |
| `behavior-c21` | Guest advanced rule changes, one history item, Undo/Redo, four matching native checks. Host advanced rule changes passed in `behaviors-c20-explore`. |
| `custom-c21`, `host-custom-c25b` | Existing private creations imported by either role, including guest upload/host admission, distribution, single history item and Undo/Redo. |
| `disguises-c25` | Both roles: native object picker, class subtree plus scale companion, stable placement identities, Undo/Redo/Undo and exact scale restoration on all four editors. |
| `asset-matrix-c25b` | Full host/guest stock/custom native picker matrix, one canonical class and history item per import, checked Undo/Redo, four matching native checks. |

Failure findings are retained. C12 sound reconstruction changed pitch by 10 ULP;
C16 repeated refresh reached 20 ULP, disproving tolerance alone. C17 preserves
exact authored sound scalars at refresh boundaries. C19/C20 class deletion was
rejected safely because application validated a new class map with old markers;
C21 fixes the owned intermediate ordering. C21/C22 disguise failed from actor
replacement and missing placement guards; C23 retains surviving marker identity,
C24 includes scale companions under the existing class lease, and C25 passes
both roles. The destination visibility menu has no native Disguised option;
an earlier fixture incorrectly requested it. Native picker/category clicks can
be ignored during animation; the revised asset matrix verifies the filter first.

`full-repository-tests3.log` passed the complete repository suite.
The latest full Win32 Runtime run (`c25-runtime-full.log`) passed **6,991** checks,
including the large closure and cached join. The rebuilt i686 native fixture
passed **4,309** checks at C26, including retained quick release, cast actor
replacement, audio refresh, speech coalescing and palette pre-mutation gating.
The cast/endpoint fixture has **174** checks; wire has **8,891**. Later changes
were confined to the native adapter, Runtime guards, fixtures and documentation.
Symbol lint and Python syntax checks pass. Existing two/four-peer real UDP
loss/reorder tests passed earlier in this branch.

## Preview 4 Save and gameplay completion

`playtests-c28-four` and `playtests-c31-four` passed solo Play/Build for all four
participants, guest Test Together approval, every readiness confirmation,
reciprocal moving captains and coordinated Build. The revision and history were
unchanged by each round. Launch status additionally waits for the live scoped
captains and the authoritative goals/roster baseline; it retains that accepted
launch state for the run when a captain is later downed.

`talk-c28-four-b` and `talk-c28-terminal` completed two actual native conversations
on four clients. Only their initial Talk request used the explicit private lab
entry point. Pages and Finished controls used ordinary native UI. Guest completion
alone held host clocks/pages/goals; every participant then confirmed before the
host advanced objectives. Both completed counts replicated and produced the
actual shared success result. `results-return-c28/result.json` verifies public
group Return, disappearance of the terminal cinematic, native editor HUDs,
matching authored values and unchanged history. Screen reprojection varied by
less than one thousandth of a pixel on guests after the camera roundtrip.

C28 found and fixed a retained cinematic camera/input hold on terminal return.
The i686 save fixture has **431** checks, including playing, success, failure,
replaced context and a cinematic that refuses to release. Native solo terminal
results were not separately completed in the game; solo Build and the shared
native cleanup/state-machine paths are covered.

`save-c31-four` passed native Save at revision7 with all four explicit native
acknowledgements. `history-after-save-c31` passed placement and class-property
Undo/Redo across that boundary. `save-reopen-c31-four-e` passed another Save,
normal close, selection of the exact new native resource and equality of the
complete supported projection before Recover. This includes full cast subtrees
and the extended placement fields. Automatic sharing immediately creates a new
baseline on native reopen, so this run uses exact-source and projection proof;
the idle-only opaque adventure-wide resource probe was not applied. Those global
authoring fields remain outside this change.

Failure receipts remain available. C28/C30 Save rejected the imported class's
machine lookup hint, populated by native Save. C31 normalizes only that hint on
owned copies and upgrades retained recovery preconditions without dropping
history. The Save harness also needed pending-ack polling, exact-source handling
of duplicate titles, and recognition of the fresh automatic reopen baseline.

`full-repository-tests5.log` and `full-repository-tests6.log` passed the repository
suite. `c31-runtime-full.log` passed **6,991** Win32 Runtime checks, including the
large dependency closure. The preview bridge has **105** checks, cast has **176**,
wire has **8,891**, and the real two/four-peer UDP fixture passed **15,501** checks
in `final-win-results.log`. The later import queue fixture has **4,442** i686
checks. Symbol lint reports zero problems.

Final native batch validation and private Windows status smoke are recorded in
the final candidate addendum below. The Linux/Bottles `lab.py run smoke` gate is
still required before merging; it was not substituted by a Windows status check.
Sustained performance and overnight release acceptance remain reserved for a
release window. This branch is not merged.

## Final preview 4 candidate

Candidate C37 DLL SHA-256:
`d362db973ea8291ef3f70f0f6d7e73357f553ed5b57bb96f32f424ca941a29b2`.
Version `0.7.5.0-ga-editor-preview.4`, protocol **19**, supported GOG 2024 only.

| Final evidence under `out/editor-preview4/` | Result |
| --- | --- |
| `batch-imports-c37-four` | Cold recovery; host and guest each selected two actual native stock thumbnails. Both classes and both history items appeared, followed by Undo/Redo/Undo for each asset, with four converged clients and zero caught faults. |
| `gameplay-c37-launch` | Guest normal-lobby request, host approval, four explicit readiness confirmations, one run, live reciprocal captains and owner-matched moving remote positions. |
| `talk-c37-four`, `talk-c37-terminal` | Native pages/Finished on all four clients, host-authoritative completion of both Talk goals, replicated counters and the real shared success result. Initial approach/Talk request uses the explicitly scoped private lab entry point. |
| `results-done-c37` | Four normal native Mission Complete panels. Host Done restored all four editor HUDs with identical authored values/history and no caught faults. |
| `c37-runtime-final.log`, `c37-native-flow-final.log` | 6,992 full Win32 Runtime checks and 616 focused native-flow checks, including the retained import-owner scope. |
| `c37-build.log` | Final i686 native input fixture: 4,450 checks. Save fixture remains 431; preview bridge 105. |
| `final-lab-cleanup` | Ordinary native exits to Galaxy, four disconnected private profiles and zero caught faults. |
| `out/windows-lab/preview4_c1/status-regression.json` | Windows lab offline/temporary-host/offline status smoke passed on all four owned, hidden, muted children. |

The final import batch distinguishes stable owner identity from permission to
start a new gesture. Both direct import and queue drain wait for prior capture,
upload and canonical application. Waiting through that operation does not erase
the original project/epoch/participant/native-generation checks. The earlier
single-key, capture, timeout and transient empty-scope failures are retained in
the RE notes and native run receipts.

The Windows status smoke is narrower than the Linux/Bottles smoke suite.
`lab.py run smoke` remains a merge gate. No merge, push, release or personal
installation update was performed. The archive is an experimental preview;
sustained performance and overnight acceptance remain scheduled release-window
work, and the unsupported nested Create Your Own route is recorded in known issues.

## Preview 5: asset isolation, selection visibility and live models

Candidate `0.7.5.0-ga-editor-preview.5`, protocol **20**, 2026-10-28, on
`fix/ga-editor-preview4`. DLL SHA-256:
`1ec689c42299df7c05dcc7b26bfc1c75acefea2ba0c7b724148694e8e1f1103b`.
The exact GOG executable and authored-captain/Act 1 restrictions are unchanged.
Protocol 19 participants must upgrade. Existing durable document/checkpoint
formats remain readable; the new ownership/presence/preview fields are transient.

The allocation capability no longer overlaps unrelated asset properties. Class
removal retains its class/placement/reference checks. Ownership snapshots refresh
after own decisions and idle gates, including property Undo followed by removal.
Native refreshes preserve the participant's open inspector when its asset exists.
Remote selections and class/placement locks have player-colored viewport labels
and rings, plus asset-ownership summaries in the normal lobby and viewport.

Native spatial setters refresh teleporter exits immediately, independently of
entrances. Live movement updates visible native models; uncommitted placement
uses a temporary scene object without an authored marker or cast allocation.
Only the final native gesture creates the transaction and Undo item. Latest-only
updates have scoped capabilities, monotonic serials, revision/generation checks
and a one-second expiry. Native cancellation/lifecycle cleanup is exercised by
the controlled i686 fixture and portable transport tests; the live native matrix
below asserts completion/history cleanup rather than assuming Escape cancels an
already granted native placement.

Final evidence is under `out/editor-preview5`:

| Evidence | Result |
| --- | --- |
| `full-repository-tests-final.log` | Complete repository `make test` passed, including protocol boundary tests and RE lint. |
| `win32-fixtures-final.log` | Native input/render/inspector 4,498; cast/companion transactions 193; document 2,898; wire 8,905; project 5,167; store 2,473; native bindings 8,246; scale 186; native Save 431; preview bridge 105; accounting 3,425. |
| `win32-session-final.log` | 18,146 checks, zero failures, including four-peer preview ownership/expiry/disconnect and real UDP loss, duplication and reordering. |
| `c5-runtime-final.log`, `c5-native-flow-final.log` | Full Win32 Runtime 16,006 checks; focused native-flow 1,332. The latter includes property edit, Undo, removal and rejection of unallocated placement lease requests. |
| `baseline-c5` | Four cold native processes recovered the prior durable project and converged on the supported projection. |
| `all-c5-final` | **20 native checkpoints passed**, four participants, revision **122**, zero frozen replicas and zero caught faults. Both roles independently viewed/edited assets, removed and imported unrelated cast while the other inspector stayed open, and used Undo. |
| `all-c5-final`, teleporter checks | Entrance and exit movement from each role updated all four actual native spatial components before release. Each committed once, refreshed without a spectator click, and restored its rendered pose on Undo. |
| `all-c5-final`, placement checks | Both roles produced moving native preview models on all other participants before release, with unchanged revision/history while held. Completion allocated one placement and one history item; Undo/Redo/Undo never resurrected a temporary preview actor. |
| `playtests-c5-final` | Solo native Play/Build passed for all four testers while observers stayed in their editors. Guest Test Together request, host approval, four explicit readiness confirmations, one gameplay run, reciprocal moving captains and coordinated native Build return passed with the same draft/hash/history. |
| `final-lab-cleanup`, `windows-status-smoke.log` | All four participants ended shared editing, returned through native Exit and left the party with zero caught faults. The official Windows status regression passed on all four private children before closing them. |

The earlier C1/C3/C5 failed fixture receipts are retained. They distinguish
single-jump drag anchoring, a populated card's Replace action mistaken for Add,
and the genuine stale ownership revision after property Undo. Replacement was
rejected safely and restored the committed draft. Raw-memory class comparisons
also exposed native padding/inactive-storage differences; they are not semantic
class edits. Final native checks use ordinary UI input and read-only render
diagnostics, with no process-memory writes or loaded-save rewrites.

The first group-movement probe had one captain blocked by the co-located native
spawn bodies. Retrying ordinary movement after the other owners moved away
produced matching movement on all replicas. The fixture now makes bounded normal
input retries and retains its original per-owner/remote displacement assertions;
it does not reposition captains or relax the successful-launch criteria.

The existing release-window performance/overnight gates remain open. This preview
is not merged; Linux/Bottles `lab.py run smoke` remains required before merging.

## Preview 6: independent acts, captain authoring and Play choices

Development branch `feat/ga-editor-multi-act`, protocol **21**, version
`0.7.5.0-ga-editor-preview.6`. Final native candidate C12:
`e71442347342f4aaf3cc876c4c608f91e2ec28ae0e8f26019efe4c450066f80f`.
All native runs below use the supported dc04 executable, private stopped-profile
copies, hidden windows and muted audio. No personal profile database was modified.
The original seven fixes and preview5 collaboration behavior remain in this build.

This adds independent browsing of all existing acts; per-act names, descriptions,
timers and goals; captain movement, locking and native-picker replacement; and
creation/deletion/reordering of one to eight acts. Surviving logical act/settings
identities remain stable across native index changes. The native Play button now
offers Test Solo and Test Together. Group preparation pins the host's selected
act for every participant. Return restores each player's own act and view.

New typed settings and roster operations retain existing ownership, dependency,
reference and history checks. A roster transaction moves native act, class-act
and marker-act columns together using owned native copies. Native mapping v3 and
state wrapper v4 preserve older recovery readers and validate their identity maps
against canonical state. New roster requests can include bounded class companions
beyond the ordinary 64 operation limit, with unchanged 8 MiB wire/history bounds
and a canonical wire preflight before live application or durable acceptance.

### Current deterministic and Win32 evidence

Receipts are under `out/editor-preview6`.

| Check | Result |
| --- | --- |
| Full repository `make test` | Passed, `repository-test-c9.log`; portable core is unchanged in C12. |
| Cast, authoring and roster transactions | 377 checks, including stable identities, conflicts, references, restore after deleted acts and checked history. |
| Document / protocol / project / store | 2,898 / 8,927 / 5,634 / 2,483 checks passed. |
| UDP/controller with fake native adapters | 23,065 checks passed; timing-dependent totals vary. |
| Passive accounting / serialized bindings | 3,425 / 8,246 checks passed. |
| Final Win32 runtime quick suite | 10,184 checks, `runtime-c12-quick.log`; includes roster transactions with 80 classes and captain pose Undo scope deduplication. |
| Final native input/cancellation fixture | 4,667 checks, `native-c12.log`; focus versus mouse hover, per-act ownership, copied captain keys, denial and stale contexts. |
| Native preview bridge / gameplay mirror | 112 preview checks and zero mirror failures after the launch/goal lifetime fixes. |
| Symbol lint and whitespace | Passed; 372 used symbols, 2,955 known, zero supported-build problems. |

### Native receipts

| Receipt | Verified behavior |
| --- | --- |
| `c12/probe` | Four fresh processes recovered v4 state and v3 native mappings with one draft hash, no frozen replica and no caught fault. Earlier C3/C4 runs also exercised legacy hydration and upgraded cold recovery. |
| `c12/authoring` | **18 checkpoints passed.** Guest act name and host description in another act; per-author Undo preserving the other act; native timer; goal All and deletion with Undo; captain lock from both roles; guest captain drag with Undo/Redo/Undo. Every forward edit created one history item and all four drafts converged. |
| `c12/roster` | **10 checkpoints passed.** Independent act views; guest Add/Undo/Redo; reorder/Undo; original roster restoration; host Delete/Undo. Surviving local views and every native companion column remained valid. |
| `c12/playtests` | **12 checkpoints passed.** All four initiating players used native Play -> Test Solo and native Build while observers stayed in their editors and no party run began. Guest Play -> Test Together requested host approval; four explicit readiness confirmations started one run with reciprocal moving captains. Native group Build returned the same draft/hash/history. |
| `c12/native-reopen` | Native Save at revision64 received host4/4 and guest1/1 SavedApplied coverage for one logical root. Act/captain values and history were unchanged. Sharing ended, all players left, and the exact owned saved file reopened through My Creations before any recovery. Both its complete serialized native SHA and supported-projection SHA matched the saved resource. |
| `c12/late-new-act` | Three editors committed a fifth act, then the fourth joined and received its stable native mapping and complete companion columns. All four converged. Host Undo removed the added act on the late participant too, restoring the original four acts without faults. |
| `c6/captain-picker-accept`, `captain-replacement-undo` | Guest native picker accepted stock Ace, distributed its dependency and converged all four editors. Undo restored Bardados while retaining its authored pose. |
| `c5/group-act3`, `spatial-goal-after-combat`, `group-results-return` | Reported adventure: all four players entered Act 3 with hostile ponies and finite 202/202 health. NPC attacks, a later shared spatial goal, Mission Complete and coordinated return completed without the earlier goal crash. |
| `c9/group-act3`, `group-return`, `solo-build-return` | Confirmed Act 3 on all four participants, NPC-to-captain damage/downing, exact revision38 group return, then ordinary Solo/Build preserving that revision and independent views. |

### Failure findings and remaining gates

The initial group reproduction entered native preview while the co-op coordinator
remained Loading. Host ScenarioData +14 retained its historical missing-assets flag
after native replacements. The repaired owned-editor bridge validates the current
closure and ignores that historical byte only for its exact preview. The following
native spatial-goal crash was fixed by checking captain life/retirement and pinning
the aliased actor across the complete native goal call. See the existing RE notes
for addresses, signatures and the unsuccessful candidates.

Roster Undo originally requested redundant scopes and exceeded the small LeaseReply
bound after host acceptance. It now holds only Document for roster history and
deduplicates ordinary settings scopes. Private roster gestures and recovered history
also required refreshing the exact native Undo/Redo buttons from shared history.
The key harness now holds Ctrl across frame fences so modifiers arrive reliably.

The first scratch Save waiter incorrectly required host-style4/4 coverage on each
guest. Guests acknowledge their own native application as1/1 and retain their local
resource aliases; the logical saved root is shared. The corrected native-reopen
receipt checks that role distinction directly and preserves the failed waiter log.
The generic `settled` diagnostic also includes pending transport queues, separate
from exact SavedApplied acknowledgements and the successful native file check.

C10 was a temporary diagnostic build, never an installation candidate. Its input
trace read a keyboard-only payload as a window pointer and crashed during Ctrl
injection. That trace is removed. C12 repeats the authoring/history inputs with no
caught faults. Numeric goal-entry injection also failed in a detached unshared
native editor; native numeric-count entry remains a manual acceptance item. Complete
goal subtrees are covered deterministically, and All/deletion are verified natively.

Player-to-pony damage is not established by these reported-adventure receipts.
The private Bardados captain exposes empty native attack slots in both Solo and
Together; the attempted keys did not select an available attack. Finite pony health,
hostile AI, NPC damage and real replicated party movement are verified separately.

Linux/Bottles `tools/lab.py run smoke --lanes 3` could not execute here: this WSL
worktree's Windows absolute git pointer fails the lab's initial git query, and the
environment has no Flatpak/Bottles installation. Receipt: `lab-smoke1.log`. Windows
native integration and repository tests pass, but **no main merge is performed**.
WAN/adversity at the new native boundaries, sustained performance and overnight
release-window acceptance remain open. Same-asset inspector edits stay serialized
across acts; adventure-wide settings, terrain and nested editors remain held.

## Preview 7: teleporter placement and NPC contacts

Development branch `feat/ga-editor-multi-act`, protocol **22**, version
`0.7.5.0-ga-editor-preview.7`, 2026-10-29. Final candidate C2 DLL SHA-256:
`f3baa0369f6fbf34b17f1ecbaf94e4d557b28b0e7cee85ab6ec1f7ff08addd13`.
Protocol21 clients must upgrade. Authored adventure and editor recovery formats
are unchanged. Temporary marker storage and gameplay movement generations are
not persisted into authored data.

Two installed observer crashes showed a null marker dereference in the native
powerup factory. Placement previews now own constructed detached marker/act data
and pin both linked teleporter objects until checked native retirement and
reference release. Neither the shared marker pool nor native placement maps
receive preview entries. An invalid factory result retires the detached state
and allows retry. Final placement retains one transaction and one Undo item.

The shared pickup adapter previously discarded every non-captain contact.
Eligible host NPC contacts now use the native single-creature helper with NPC
effects and locomotion cleanup. Reliable subkind156 and snapshot movement
generations reset observer interpolation without moving unrelated captains.
Run, act and actor-incarnation fences also cover deferred bindings and cooldowns.
The path is shared by Test Together and ordinary co-op. Native eligibility,
collector-only upgrades and independent captain teleports remain intact.

### Deterministic and Win32 evidence

Receipts are under `out/editor-preview7`.

| Check | Result |
| --- | --- |
| `repository-test-c2.log` | Full repository `make test` passed; symbol lint375 used/2958 known, zero supported-build problems. |
| `native-c2.log` | 4,703 Win32 input/cancellation checks passed, including required nonnull native marker/acts, both linked objects, cleanup, failed factory and retry. |
| `pickups-expanded-final.log` | Zero failures. Host-only NPC authority, eligibility/range/invalid exits, separate NPC cooldowns, simultaneous captain contact, holds, missing identities/bindings, incarnation replacement, expired effects and stale run callbacks. Existing collector/captain checks still pass. |
| `mirror-c2.log` | Zero failures. Actual world movement-generation admission, delayed same-generation pose and stale-generation rejection, with updated static/dynamic record parsing. |
| Actor motion tests in `make test` | A6.5m teleport resets interpolation immediately; older generations cannot enter the new follower scope. |
| Executable verifier |39 native editor signatures/caller sets verified against the supported executable; original symbol entries retained alongside the three new marker helpers. |
| `windows-status-smoke.log` | Official Windows status regression passed on all four private native children; all four ended with zero caught faults and were stopped by exact owned handles. |

### Private native adventure evidence

The reported `coop test` was discovered read-only at
`408A0000!2909E915.366A930D` in the host's EditorSaves.package. It contains one
act,28 classes and36 markers, producing61 runtime actors. All runs used private
profile/installation copies, invisible windows and muted audio. Eight original
player profile packages retained their recorded hashes after testing.

| Receipt | Verified behavior |
| --- | --- |
| `c1/placements-two-offline` | Host and guest teleporter drag/commit with observer previews and Undo/Redo/Undo; no caught fault. C1 preceded the final linked-exit diagnostics and expired-binding guard. |
| `c2/recovery` | Four cold processes recovered the durable draft and converged before native editing. |
| `c2/placements-four` | Eight checkpoints. Every participant placed a teleporter while all three observers displayed both live endpoints before release. Draft revision/history stayed unchanged while held. Each release committed once; Undo/Redo/Undo retired temporary objects. Final revision36, zero frozen replicas/faults. |
| `c2/group-launch` | Native guest Play -> Test Together, host approval, four explicit readiness confirmations and one run with reciprocal moving captains. |
| `c2/npc-together-filtered` | Actual host NPC5 locomotion/contact with entry11 produced a newer teleport generation and the authored exit on all four spatial components and visible model transforms. No direct activation or teleport probe was used. |
| `c2/solo-launch`, `c2/npc-solo` | Native Play -> Solo left the three observers editing. The same NPC/entry reached the authored exit through the native Solo contact path. |
| `c2/native-save-confirmed` | Native Save at revision36 completed host4/4 and guest1/1 SavedApplied acknowledgement coverage for logical root `408A0000!290D9A68.366A930D`. |
| Native saved-file reopen / `reopen-identity-comparison.log` | The exact saved file reopened through My Creations before recovery. All28 classes, act/captain settings and34/36 placements matched after stable-ID mapping. Two buildings differed only by one float ULP in position, maximum0.000030518m; all teleporter transforms/properties matched. The exact semantic-hash check failed and is retained, not reported as a hash-equality pass. |

### Negative findings and remaining gates

Initial NPC fixture failures came from paging only the first48 global actors
and confusing the source spawner with its spawned creature. The corrected fixture
selects exact host incarnation/object identity through `actorsat` and independently
checks native rendered-model positions. A native saved-success confirmation also
had to be dismissed before the final SavedApplied acknowledgements could complete.
The initial waiter timeout is retained alongside its corrected successful receipt.

Escape and dropping over the cast card finalized the tested placement rather
than cancelling it. Those native attempts remain failures; cancellation and
allocation failure/retry are established by the controlled Win32 fixture.
Independent-act switching, cloning, native respawn, distant/invalid exits and
adverse timing at these new native boundaries still need dedicated live checks.
The reported adventure itself has only one act.

Ordinary co-op entered a four-player run, but its NPC regression did not pass.
After the prior editor/save sequence the native Gameplay pause reason retained
a positive counter and the NPC did not walk. Fresh-process retries stalled in
adventure/captain preparation with missing metadata/disabled native Next, including
a normal native-picker attempt. These receipts are not evidence of successful
ordinary NPC contact; that live check remains open despite the shared tested
adapter path. No native pause counter or actor pose was written to force a pass.

Linux/Bottles `tools/lab.py run smoke --lanes 3` cannot run here: the Windows
absolute worktree git pointer fails WSL's initial git query, and Bottles/Flatpak
is unavailable. Receipt: `lab-smoke.log`. The Windows status smoke is narrower.
No main merge is performed. Sustained performance, overnight acceptance and the
remaining native matrix above are not complete release acceptance.

### Local preview delivery

The tested C2 payload is installed in `D:\GOG Galaxy\Games\SPORE` for the desktop
`SporeCoop Two Players` shortcut. Its target and working directory resolve to
that installation's existing two-player launcher. The launcher/shortcut identify
preview7/protocol22; the player1/player2 launch commands and current settings
retain their pre-update hashes. Every packaged executable, dependency package
and symbol file was checked against its installed counterpart. Receipt:
`out/editor-preview7/installation-verification.json`.

The complete previous installed payload, settings, version marker, launcher and
shortcut are retained with checked hashes and restoration destinations under
`out/editor-preview7/preview6-rollback-20261029-191110`. Its DLL is the recorded
preview6 C12 hash. No game was running during deployment and no personal game
was launched to verify the shortcut. An initial post-copy guard compared settings
against the old preview6 receipt, detected a since-changed coop.ini and rolled
back. The successful deployment compares against the current pre-update settings
snapshot and preserves it. Original profile database hashes remain unchanged.

## Preview 8: Motel of Melora music admission

Version `0.7.5.0-ga-editor-preview.8`, protocol **22**, 2026-10-30. Candidate DLL:
`d9ffea0ab1839eb881a3c0e1870df8ab823a932bdd3998aa647f6e27adadbd08`.
This changes native audio resolution only. It adds no wire, authored-save or
recovery format and does not change the supported executable or graphics settings.

The installed preview7 log and a private two-player reproduction both rejected
`Motel of Melora ('22 Edition)`, root `408A0000!290DE23F.366A930D`, act6/music
`8F7E1796`. The descriptor is absent, but the real stock sample exists in
Spore_Audio1.package. Native music supports this fallback. The resolver now checks
the same-ID sample only after a missing root descriptor, preserving the authored
ID. Present descriptors retain precedence. Custom samples, descriptor overrides,
aliases, malformed/unreadable descriptors and missing inherited parents still fail.
The existing RE notes record the installed sample hash and native caller evidence.

### Evidence and limits

Receipts are under `out/melora-music`.

| Check | Result |
| --- | --- |
| `repository-tests.log` | Complete repository `make test` passed; RE lint375 used/2958 known, zero supported-build problems. |
| `catalog-audit.log` | All23 installed stock act music values across69 scenarios, plus the reported8F7E1796 value, passed. The reported value resolves to one real installed sample. |
| `music-win32.log`, `music-sanitizers.log` | Win32 resolver and ASan/UBSan checks passed, including direct-sample fallback, descriptor precedence and failure/provenance boundaries. |
| `symbol-verifier.log` |39 existing editor signatures/caller sets verified. This fix introduces no native function call or symbol. |
| `before-reproduction/result.json` | Exact preview7 missing-audio error reproduced with the two owned private processes; native act music IDs captured independently. |
| Lower-detail native run | Host collected707 records/257 dependencies, registered7,496,045 bytes and started sharing. The guest imported the same bundle and joined after its cold native animation-variance load. Both reached phase4 with the same CONVERGED hash. Host viewed act6 while the guest viewed act1. |
| `native-admission-timer-evidence.json` | A normal guest timer checkbox changed act1 from unlimited to60 seconds. Host shared history records exactly one revision1 transaction by guest1:1. Native collectors on both roles continued to resolve act6 as `native=sample`; the four nonzero authored music IDs stayed unchanged in those collection logs. |
| `windows-status-smoke.log` | Official Windows status regression passed on two fresh hidden/muted candidate processes; those exact children were stopped afterward. This is narrower than Linux lab smoke. |

The complete editor-music edit/Undo fixture has **no passing native result**.
Its initial checks ran before native loading finished, and120/300-second join
waits were too short: the guest took about11 minutes in Calculating Animation
Variance. After convergence, the synthetic name-entry attempt left a native text
buffer without a committed document edit. The later explicit act-property focus
and native timer checkbox did commit, as the separate receipt records. The fixture
now uses that checkbox, waits for its settings lease and makes screenshot readback
optional. Its full Undo path remains unverified on this large adventure.

At1600x1200/high detail the private host exhausted the native non-LAA address
space:1955/2047 MiB, followed by a null animation allocation at preferred9AC8E0.
Lower private texture/bake/planet/game detail allowed convergence and the timer
edit, but that run later exited C0000409 during screenshot capture. These are
recorded in known issues; they are not presented as a stable large-adventure
acceptance pass or fixed by the music change. An earlier restart also produced
a D3D/USER32 watchdog report before opening the adventure; a clean restart
rendered normally. None of these runs changed personal graphics preferences.

All original personal package hashes remained unchanged. The supported exe and
its large-address flag are unchanged. Muted tests do not establish audible music
quality. Linux/Bottles smoke is unavailable because the Windows worktree pointer
fails WSL's initial git query and Bottles/Flatpak is absent (`lab-smoke.log`).
The branch is not merged. Large-scene memory, native Undo and sustained acceptance
remain open; the missing-audio rejection itself is reproduced and repaired.

Preview8 is installed for `SporeCoop Two Players` in the existing GOG installation.
DLL, dependency-package and symbol hashes match the packaged payload; the version
marker and shortcut/launcher description identify preview8/protocol22. Current
settings and individual player launch commands retain their pre-update hashes.
Receipt: `out/melora-music/installation-verification.json`. The complete previous
preview7 payload, settings, version marker, launcher and shortcut are retained in
`out/melora-music/preview7-rollback-20261030-005759`, with restoration destinations
and checked backup hashes. No game was running during installation.
