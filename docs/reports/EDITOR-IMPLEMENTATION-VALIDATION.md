# Shared Adventure Editor: implementation validation

Report revision 27, C17 native cancellation and collector defect, 2026-10-26. Branch:
`feat/ga-adventure-editor`. The portable core and transport checkpoint is local
commit `216f8b0`; native integration is recorded at `0a35c7b`, with lab tools at
`a9b844f`. The C10 integration checkpoint is `071f6a4`, depending on network
checkpoint `7f7c3da`; measurement tooling is recorded at `b65ba3b`. Later
calibration changes require their own working-tree/run evidence.

**Implementation is under validation and is not release-ready.** Portable tests
and several real two/four-player workflows pass. Remaining lifecycle and
cancellation breadth, bulk contention and memory gates below prevent a
complete stages 0-4 claim.

This development branch uses protocol 16. The published
[0.7.4.0 gameplay release](../validation-ga-0.7.4.0.md), protocol 15, remains
unchanged. See the [development guide](../MULTIPLAYER-ADVENTURE-EDITOR.md) for the
current restricted controls and the [plan](../MULTIPLAYER-ADVENTURE-EDITOR-PLAN.md)
for the intended acceptance criteria.

## Current acceptance status

The current private candidate is C18, built from code at `d343307`. Its complete
Windows and portable checks pass. Native C17 evidence remains useful but does
not silently become a C18 gate pass after the collector correction.

| Area | Current evidence and remaining work |
| --- | --- |
| Core, transport and ABI checks | C18: all 22 Windows suites, 21 additional admission cases and portable `make test` pass with frozen source inputs. |
| Native placement and history | C10-C17: real two/four-player creation, transforms, clone/delete, conflicts, chronological own Undo/Redo and bounded history pass. C18 actual placement prewarm is underway. |
| Native cancellation | C17: four players preserve a disjoint host commit while an invalid guest Scale is held, then cancelled. Pure Scale also preserves poses and history. Additional two-player/cancellation breadth and a C18 native recheck remain. |
| Native save and recovery | Actual Save As/reopen, cold project recovery and one complete cold native-file opaque comparison pass. The one fixture is not broad adventure round-trip coverage. |
| Playtest and return | C17: four-player natural failure/success, Done, Build hammer and preparation/playing disconnect return preserve draft and local view. C18 cycle rerun, additional End/Replay/loading paths and actual no-XP evidence remain. |
| Large-editor FPS | C17: unobstructed two/four-player comparisons pass on 419 placements. The earlier covered four-player sample is excluded. C18 rerun remains. |
| Collector memory defect | Reproduced in C17; C18 real four-player idle test proves stable 64-entry capacity with continuing native updates. |
| Sustained retained memory | Open. C17 raw private-byte slopes did not establish a plateau. C18's exclusive 128-edit warm-up and 30 edit/Test/Return window is running. |
| Genuine bulk contention | Open. Aggregate 2 Mbit/s WAN instrumentation and real-content provenance are prepared; actual fresh-peer contention and the unchanged 750 ms p95 gate still need execution. |
| Gameplay regression breadth | C18 automated suites pass. Actual large-adventure FPS and the remaining captain/lobby/music/interactions/targeting/results/Replay native coverage are separate gates. |

Stages 5 and later remain roadmap work. None of these observations enable
unsupported act, template, goal, conversation, terrain or nested-editor authoring.

## Scope and evidence boundaries

Native checks use the GOG 2024 executable, 24,895,536 bytes, SHA-256:

```text
dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37
```

The C7 private test DLL has SHA-256:

```text
ccb2a4f3d9150597f5a8cf1c8948618b65cc81a2a494ce25d7f9469e803694c0
```

The C8 private candidate has SHA-256:

```text
3c61d13724cbe0952aaa328f5c786f979b6001cab3f1eb130249336095e3f1c4
```

Its exact-executable verifiers pass with 31 editor, 13 save and two history-site
entries. The native editor map includes scoped cached Undo/Redo availability
queries; the actual two-player shortcut receipt is recorded in the C8 section.

Tests run in a real private installation with isolated `winlab_` profiles, hidden
windows and verified muted audio. Process identity, executable/DLL hashes and
ownership are checked before input. The fixtures use normal native input and
public co-op actions; they do not fabricate accepted edits, finish adventures
through debug commands, or rewrite loaded native save databases. Personal games
and profiles are outside this lab's ownership.

Three evidence levels must remain separate:

1. Portable document/store/codec tests exercise deterministic data contracts.
2. Real UDP controller tests and Windows ABI/runtime fixtures use deliberately
   controlled native doubles. They validate ordering, callbacks and failures, not
   Spore's rendering, saves or actual multiplayer editor behavior.
3. Hidden game-process tests exercise the installed executable, actual UI,
   native resources and editor/playtest lifecycle.

Workspace evidence under `out/` includes candidate hashes, command logs,
screenshots and machine-readable results. These generated files are not assumed
to be published with this versioned report.

## Automated acceptance matrix

All listed runs completed with zero failures. Check totals are suite assertions,
not numbers of adventures or native playthroughs.

| Area | Recorded result | What it establishes |
| --- | --- | --- |
| Document core | 2,753 checks on Linux ASan/UBSan/leak and Win32 | Atomic typed mutations, per-field revisions, stable IDs/generations, reference-aware deletion, leases, deduplication, bounded history and checked own chronological Undo/Redo. |
| Durable store | 2,473 Win32 and 2,483 Linux checks in the final C10 sweep | Verified checkpoints/journal replay, interrupted writes, corruption/reordering rejection, atomic checkpoint replacement and recovery boundaries. |
| Editor protocol | 5,845 checks on both platforms | Strict sizes/types/context, capability fences, transfer verification, clone provenance/final pose, host-derived ownership, transient previews, bounded JOIN inventories, readiness and Saved acknowledgements. |
| Controller, after presence/JOIN work | Prior Linux ASan/UBSan/leak: 14,099; final C10 Linux/Win32: 14,065/8,837 checks | Real two/four-peer UDP, deferred native work, ordered commits, bounded cached JOIN negotiation, concurrent catch-up, leases, transient-preview token fences, duplicate/stale messages and explicit group return barriers. Poll-dependent assertion counts vary. |
| Binary root delta | 6,410 checks on both platforms | Bounded copy/literal reconstruction against an immutable base, base/target hashes, malformed input and transactional output. |
| Project/resource envelope and JOIN cache codec | 4,684 checks on Linux ASan/UBSan/leak and Win32 | Full/delta/legacy envelopes, verified missing-content JOIN assembly, partial caches, root-key rotation, bounded inventory, DBPF pre-read bounds, zero/mismatched sizes, aggregate limits and transactional failure. Runtime cache handshake acceptance remains separate. |
| Serialized native bindings | 8,224 Win32 fixture checks | Stable placement bindings across compacted native serialization, holes, deletion and restoration. This is not proof of scene model correctness. |
| Native Save adapter | 296 Win32 fixture checks | Verified Save As state/ownership and completion/failure handling using controlled native interfaces. Actual native Save As is separately exercised below. |
| Editor/playtest bridge | 74 Win32 fixture checks | Run/submode fencing, owned preview lifecycle and asynchronous native return, including negative active-cinematic/pending-intro cases. C7 actual game testing found an additional startup hold. |
| Integrated editor runtime | C11 full: 3,235 Win32 checks; C11 focused: 2,594; previous C10 full: 3,017 | Native/document ownership, opaque history artifacts, Store durability and real UDP with controlled native APIs, cached JOIN, save/recovery and close settlement. C11 additionally models NativeStop clearing local selection, delayed return readiness, generation changes, stale callbacks and post-launch cancellation. Actual native acceptance is separate. |
| Native asset registration | 253 Win32 fixture checks | Immutable dependency/resource lifetime and registration behavior in the native interface fixture. |
| Frame metrics | 15 checks | Bounded numeric metric capture. No actual editor FPS budget is established by this test. |
| Performance harness | 18 offline checks on Windows and WSL | Measurement/configuration helpers, including exact measured Spore process roster; actual large-native performance remains unmeasured. |
| Confirmation metrics and WAN proxy | Eight metric checks and four proxy checks on Windows and Linux | Bounded request-confirmation timing and UDP shaping/accounting helpers; these are not native WAN latency measurements. |
| Existing GA native regressions | All 17 Win32 suites passed | Captain, lifecycle, controls, movement/vehicles, targeting, social, goal/results, pause, items, explosives and pickup module fixtures remain passing. |

The JOIN codec receipt is `out/editor-core/editor-project-join-validation.json`.
It proves byte-level cache verification and assembly, not actual native JOIN
cache reuse; the transport/runtime handshake is still being integrated.
The pre-C9 presence/JOIN protocol and controller receipt is
`out/editor/transient-presence/validation.json`. Its native callbacks are doubles.
It covers 2,048-claim inventory transfer while another peer commits, a pinned
current baseline, duplicate transfer seals, and rejection of a delayed approved
preview after Return and lease regrant. No actual C9 editor or rendering receipt
is inferred from those checks.
The C7 aggregate is `out/editor-windows-c7-final-tests.json`; the host-coverage
run remains `out/editor-core/host-coverage-validation.json`. The earlier portable
matrix is `out/editor-core/portable-editor-validation.json`. The existing GA
fixture aggregate is `out/ga-regression-windows-final-tests.json`. The final
repository-wide `make test` rerun passed in `out/editor-portable-final2.log`,
including the portable GA/editor suites and knowledge lint: 344 used symbols,
2,915 known symbols and zero problems.
The subsequent C8 symbol lint passed with 344 used, 2,917 known and zero problems.
The rebuilt preview fixture has 74 checks; the older C7 aggregate predates its
last two negative cases.

The final current-source sweep is `out/editor-final-c10/validation.json` at
`0afb659`. All 244 recorded source/test/symbol inputs remained unchanged during
the sweep. Repository-wide portable tests, thirteen freshly built Win32 suites
and all seventeen archived GA suites passed. The GA executables matched their
recorded hashes and declared Make prerequisites; their original aggregate has
no compiled-source manifest. Knowledge lint reported 344 used symbols, 2,918
known and zero problems. Optional real `EditorSaves.package` input was absent
from that portable run, so its native-save read case was not exercised there.
This sweep does not establish actual native game, FPS or WAN acceptance.

The new coverage checks establish that the host's own participant entry has a
known canonical hash at revision zero, advances after durable host- or
guest-authored commits, remains unchanged after rejected persistence, and is
initialized when recovering a nonzero revision. An empty remote coverage hash
means unknown coverage, not a verified revision zero.

The controller's real UDP adversity tests dropped, duplicated and reordered
datagrams before Session reliability. The host-coverage Linux run observed 7/6/18 such
events with two peers and 24/18/64 with four; Win32 observed 10/8/22 and 57/39/72.
These totals depend on polling/retransmission timing. They are not actual
game-process network-adversity acceptance.

## Actual C7 native editor observations

| Workflow | Two-player evidence | Four-player evidence and limit |
| --- | --- | --- |
| Normal entry and Save As | Native Alpine creation, naming, successful native save confirmation, fresh resource-key readback and sharing passed. | Fresh host captain naming/save and normal Welcome exit completed; Alpine entry and native Save As then passed. An initial fixture attempt left the Welcome radial open; its ordinary close retry was corrected. |
| Join/baseline | Both native editors matched the canonical projection/opaque content, with settled rich-terrain visual agreement. | All four explicitly joined, reached native editor readiness and reported matching opaque/semantic hashes and durable revision. |
| Create and Move | Create at 650,470 committed at revision 1 but was unselectable; a visible Ace was a cursor/preview ghost. A fresh unshared editor reproduced that location's failure before any Restore, so it is not an established co-op render defect. | The first host Create/Move/Undo at 430,450 changed the intended stable placement while the original placement stayed unchanged. A later Create at that same point failed normal selection/movement. Serialized hashes alone do not prove actor selectability. |
| Undo/Redo | Public Undo could remove the hidden creation. | Public co-op Undo/Redo committed once and converged. Ctrl+Y and Ctrl+Shift+Z did not emit Redo; their binding/guard path remains unresolved. Public Redo success is not reported as proof of native shortcut support. |
| Explicit playtest readiness | Not accepted as a complete native preview cycle. | Zero, one, two and three confirmations kept all four in preparation. Leaving the host last also held launch. Only the fourth explicit editor Ready entered the shared preview. |
| Playtest startup | No completed gameplay-start acceptance. | All four entered native editor Test and the same GA run. C7 then waited for beam confirmations 0/4 although native Test has no beam cinematic/Continue button; native clocks remained zero. No hidden confirmation bypass was used. |
| Return to draft | Shared End/rebaseline passed. A roundtrip did not repair new-actor selectability. | Normal editor Return completed for all four and restored readiness, canonical draft and durable revision. |
| Disconnect/rejoin | Not substituted for the four-player receipt. | Fourth player left, exited through normal native UI, rejoined with a fresh capability and reconstructed the same draft. The old connection's history authority was not inherited. |
| Graceful End and Recover | Additional ordinary-editor diagnostics exist. | Host End followed by same-editor Recover preserved project identity, stable placements, revision and semantic history. Guests joined again; recovered own public Undo/Redo converged. Cold-process recovery is still untested. |

Two-player receipts are under `out/editor-native-candidate7-{bootstrap,join,edit}`
and the additional `candidate7` diagnostic directories. Four-player receipts and
their hashes are consolidated in
`out/editor-native-party-c7/validation-summary.json`. Individual failures are
retained alongside successful diagnostics; later success does not rewrite them.

The clean native negative control at 650,470 produced radius 500 and an
unselectable placement before any sharing or Restore. Native creation at
601,470 produced radius 559.6 and a genuine Move. The first and second
four-player 430,450 creations had the same radius 530.87026 but different
selection outcomes. Native investigation later identified deliberate grouping
of overlapping cast icons: the selected marker is excluded from grouping, so
clearing temporary selection can merge a new icon beneath an existing icon.
The 33-pixel gap in the four-player fixture was smaller than the icon width.
This is not evidence of a missing model or a broken canonical projection.
Acceptance still needs ordinary group selection or adequately spaced icons.
Sparse Clone/Delete, rotation and scale do not yet have complete actual-game
acceptance receipts.

The four-player C7 workflow completed **zero of the required 20 changed
edit/playtest/return cycles**. It stopped at basic operation failures. Both its
`four_player_workflow_pass` and `complete_acceptance` remain false. Six game
processes were rendering concurrently during parts of this lab, so their timing
cannot serve as a matched two/four-process FPS baseline.

## Measured transport latency and resource cost

The opt-in [UDP benchmark](../../tests/editor_session_benchmark.cpp) measured
real sockets with fake native callbacks and memory-only persistence. It used
512 placements, 30 leased gestures per guest, a deterministic proxy seed and
nearest-rank p95, once per profile. Timing includes reliable retransmission but
excludes native rendering/serialization and disk durability.

| Profile | Two-peer request-to-confirm p95 | Four-peer request-to-confirm p95 | Gate |
| --- | ---: | ---: | --- |
| LAN model | 14.504 ms | 15.590 ms | Below 200 ms. |
| 100 ms RTT, 1% loss | 244.248 ms | 211.110 ms | Below 500 ms. |
| 100 ms RTT, 10% loss stress | 739.931 ms | 449.084 ms | All operations converged; no normal-network latency pass is inferred. |
| 2 Mbit/s, 100 ms RTT, 1% loss, sparse 256 KiB root delta | 171.125 ms | 190.154 ms | Below 750 ms. |
| Same shaping, sparse 1 MiB root delta | 187.660 ms | 269.528 ms | Below 750 ms. |

All 600 measured gestures converged exactly once. Previously sending the raw
256 KiB/1 MiB root updates exceeded 750 ms; those failed measurements remain in
the report. The immutable-base patches reduced these sparse fixture envelopes
to 10,973 and 18,461 bytes, including 512 placement bindings. Dense or
incompressible rewrites may fall back to literals and still exceed the target.
That case is not passed or removed from the acceptance gate.

`out/editor-core/editor-latency.json` retains exact measurement-source hashes and
the earlier failed cases. Measurements preceded the final Saved/close/host
coverage changes and must not be relabeled as a new candidate's native latency
result. Shaping is symmetric per peer, not aggregate host uplink; byte accounting
includes UDP payload, ACKs and retransmissions but excludes IP/UDP headers.

The C9 controlled Windows runtime fixture also used a 24 MiB dependency closure
with 20 structural edits and Save: host/guest traffic was 64,841/3,879 bytes,
including retransmissions, and retained CAS grew from 25,166,419 to 25,203,841
bytes. A cached JOIN of that closure sent 29,397 host bytes and 751 guest bytes
while reproducing native bytes/bindings in the double. The receipt is
`out/editor-runtime-c9/validation.json`. This demonstrates dependency reuse in
that fixture, not native long-run memory safety or performance.

## Remaining acceptance gates

- Complete actual uniform scale, including invalid native cancellation, on all
  advertised peers. C14 small-object Scale and native Undo/Redo passed for both
  authors, but the relative-only boundary and four-player cases remain open.
  C12 rotation passed
  for both two and four players. The C10b
  Create/Move/Clone/Delete and native history receipts below use valid native
  placement and selection. Keep native cast grouping and fixed input conflicts
  separate from co-op defects.
- Extend the C11 camera/selection Return receipt to remaining native lifecycle
  adversity and object types. It closes the previously observed normal Return
  selection-loss case on the tested four-player Alpine draft.
- Extend actual four-player lifecycle adversity while preserving explicit
  readiness and the authored draft/history on return.
- Exercise real game-process loss/reordering/duplicates, concurrent/conflicting
  gestures, delayed callbacks, Save, disconnect and recovery boundaries.
- Extend the C9 cold host recovery and C13 same-process imported-guest recovery
  receipts to cold imported recovery, native file reopen and adverse
  save/recovery boundaries. Imported recovery's mandatory new native Save As
  passed in C13; it does not prove reopening a native saved file before Recover.
- Extend the C12 two-game native latency receipt below to four players and bulk
  asset contention. C14 four-player LAN passed, but its WAN p95 exceeded the
  target; later rate/loss-stress profiles did not execute. Measure large-editor
  frame time and retained memory against
  matched process-count/hardware/window/content baselines. Large-editor FPS
  remains unmeasured. The 30-cycle post-capacity diagnostic below does not
  establish a steady-state bound.
- Recheck affected suites after further fixes and publish candidate-specific evidence
  before any release-ready or complete stages 0-4 claim.

Class imports/properties, non-transform placement properties, act changes,
objectives, conversations, global settings, terrain and nested editors remain
outside the current supported shared mutation set. Independent acts, selective
history and continued editing while others playtest remain later milestones.
Unsupported controls must fail safely or remain read-only.

## C8 follow-up

C8 includes absent-intro handling, scoped history availability, host coverage,
close lifecycle and UI changes. No speculative scene/picking fix was applied.
The C7 four-player lab ended through normal shared End and native Exit; all four
reached Galaxy before the owned runner stopped with exit zero. C8 uses a fresh
bounded runner and the same closed private test profiles. At this report
revision C8 has the following partial actual-game results:

- Two-player normal native Ctrl+Z and Ctrl+Y passed at
  `out/editor-native-candidate8-native-history/result.json`. Create and Move
  passed; Clone committed but normal clone selection/Delete failed in the
  overlapping-icon fixture. Complete clone acceptance remains open.
- Four-player Save As and native baseline convergence passed. An initial
  calibration drag was obstructed by the co-op panel; the fixture was corrected
  to close it before world input. With the panel closed, original Move and host
  Create at calibrated land, Move, public Undo and public Redo passed.
- Guest creation at the same calibrated point committed with radius 559.6, but
  immediate selection/Move failed in the overlapping-icon fixture. After
  normal playtest reconstruction and allowing the closed panel to settle,
  ordinary guest Move passed with all four clients converged at revision 28.
  This later success does not turn the earlier input assertion into a pass. See
  `out/editor-native-party-c8/edit-calibrated/result.json`.
- The four-player explicit-ready, natural-simulation and normal-return
  diagnostic passed at
  `out/editor-native-party-c8/preview-diagnostic/result.json`. Zero through three
  confirmations held every editor; the host's final explicit Ready started all
  four native scenes. No native Continue control existed and none was clicked.
  Native elapsed clocks advanced from `[321,275,317,317]` to
  `[1891,1899,1891,1895]` ms over approximately 1.5 seconds. All twelve reciprocal
  remote captains were live in the same world, with maximum pose error 0.79 m
  and maximum received-state age 0.13 seconds. Normal Return restored all four
  editors at durable revision 6 with the authored hash preserved and no native
  faults.

Twenty changed edit/playtest/return cycles passed on a separately verified
existing placement in the exact C8 candidate. Each round made a real native
Move, required all four explicit Ready actions, observed advancing native clocks
and all reciprocal captains, and returned normally to the matching authored
draft. All 20 returns converged with no native faults. The receipt is
`out/editor-native-party-c8/twenty-cycles/result.json`: 80 clock checks and 240
remote-captain checks, maximum pose error 1.00 m and maximum state age 0.12 s.
The [four-player fixture](../../tools/tests/coop_editor_party.py) never clicks a
nonexistent beam Continue or manufactures a confirmation. This receipt leaves
`full_workflow_pass` false because every author's Create/Clone and other edit
cases remain separate gates.

The same run recorded 21 settled private-memory samples with six game processes
rendering. Host private bytes grew by 16.246 MiB; guests grew by 13.016, 12.906
and 10.887 MiB. These are observed totals, not a leak diagnosis or a memory-budget
pass. Native host history grew from 7 entries to its 25-entry cap at cycle 17;
only three further returns were sampled, growing another 724,992 bytes. Guest
native histories remained at 1, 0 and 0 entries. Retained snapshots and native
caches need separate accounting, followed by 30 or more cycles and a sustained
post-capacity window. See `retention-summary.json` and
`history-retention-analysis.json` beside the result. No actual FPS gate was
measured during this six-process functional test.

The native 25-entry cap alone does not bound the mod's 128 retained semantic
gestures and associated opaque artifacts. The prepared C11 fixture can first
perform 128 genuine native Move gestures, verifying all-peer convergence, then
require the observed semantic count 128 and native count 25 before taking its
baseline and thirty further edit/playtest/return samples. It also records core
history counts, native serialized bytes, registered packages and private memory.
This is prepared instrumentation, not a completed C11 memory result; no history
limit or acceptance budget is changed.

## C9 cold recovery and fresh entry

C9 private DLL SHA-256 is
`aa99e9c163bf7ad6c59110b79252097f49c9dcc3b80a3bf03b402ee63bd5d85e`.
All four C8 processes first ended normally at Galaxy and their owned runner
closed. Before C9 started, a read-only capture of the closed host's mod project
verified checkpoint revision 16 and twelve checksummed journal commits through
revision 28. Its durable native bytes exactly matched the last C8 live inspection.
The original native save databases were not externally modified.

Four fresh C9 processes then started at Galaxy. The host formed the party and
used Recover; all three guests explicitly joined. Project identity, revision 28,
all stable placements, exact opaque native SHA, normalized native semantic SHA,
and all 26 original gesture records were preserved. Normal native Ctrl+Z and
Ctrl+Y committed exactly once each, at revisions 29 and 30, and restored the exact
opaque/semantic content and poses on all four peers. Shared End and normal native
Exit returned every process to Galaxy with zero native faults. A read-only mod
checkpoint after its Store closed confirmed a changed session epoch. Receipts:
`out/editor-native-party-c9/cold-baseline/baseline.json` and
`out/editor-native-party-c9/cold-recovery/result.json`.

This closes that host's normal cold recovery case. It does not establish guest
imported recovery, crash interruption, every Save As/reopen path, or complete
stages 0-4 acceptance. A subsequent fresh native Alpine Save As and four-player
baseline join passed in the same candidate; the four-player panel is legible at
1024 by 768. Per-author operations and remaining gates are recorded separately.

## Additional C9 four-player operation receipts

| Actual workflow | Result and boundary |
| --- | --- |
| Four authors Create/Move/native history | Each author created a new placement, received that exact local native selection, immediately moved it, then used actual Ctrl+Z/Ctrl+Y. All peers converged at each revision with no faults. Receipt: `four-author-edit/result.json`. This resolves the immediate C8 picking assertions for these valid ordinary inputs. |
| Clone/Delete and structural history | Authors 89, 90 and 91 each completed Clone, native Del on the verified selected clone, Undo Delete, Undo Clone, Redo Clone and Redo Delete. Author 92 initially exhausted three attempts while waiting for ownership; an idle retry completed the same full sequence. Combined receipts establish all four authors' operations, while the original matrix failure remains recorded. Receipts: `four-author-structural/result.json` and `last-author-structural-retry/result.json`. |
| Concurrent edits | Four barrier-started disjoint native drags produced exactly four accepted gestures. Two attempts on the same placement produced one accepted gesture. All four peers converged; the fixture does not claim simultaneous lease ownership. Targets were created through native UI and separated by at least 60 pixels to avoid deliberate native cast-icon grouping. Receipt: `concurrent-four/result.json`. |
| Blocked chronological Undo | After A created an earlier independent object and made a latest move, B changed the latest target. A's actual native Undo rejected with the checked-field conflict, without changing the draft, original gesture flags or native history cursor. It did not skip to the earlier action. Full Redo-conflict and recovery coverage remain open. |
| Preparation cancellation | All four remained unlaunched at zero confirmations. Normal host End during preparation detached all four to their retained native drafts, with zero faults. Actual private native frame cap zero was read back on every process. Receipt: `prepare-host-end/result.json`. |

These receipts are under `out/editor-native-party-c9/`. The four-player panel was
also captured at `lease-panels/`: rows remained legible, but the current ownership
summary does not expose every field-lock token.

Two limitations remain explicit. First, a history fixture incorrectly expected
another author's Undo to make A's old inverse automatically valid. V1 deliberately
refreshes only the same author's causal preconditions; a foreign Undo is another
revision and does not promise that rebase. The failed continuation is preserved
rather than changing the core's policy. Second, later attempts to create a new
object remained waiting for ownership even after other editing stopped. No
accepted operation was lost, no peer diverged or froze, and an ordinary preparation
cancellation/End recovered the draft. The cause needs token-level diagnosis; it
must not be relabeled as successful interactive readiness.

C9 did not run the long memory window or establish actual rotation/scale, WAN,
large-editor FPS or complete stages 0-4 acceptance. Before the next DLL change,
all four processes exited the native editor normally to Galaxy and their owned
runner closed with exit zero. Profiles and durable draft files were preserved.

## C10 source and deployment boundary

C10 integration source is recorded at `071f6a4`; the private DLL is
`c93b4b7c99c7be2cff481a2eb7d3b4488ae0ef5c2e658443acfdd2979e94f7a0`.
The exact-source controlled-native Runtime quick suite passed 2,377 checks;
`out/editor-history-lease-build/validation.json` separates that run from the
older C9 full suite. It reproduces the conservative cross-author Undo policy and
reacquires document ownership after an abandoned field lease in virtual idle
time. This does not explain or pass the actual C9 ownership stall.

The first C10 cold startup exposed a deployment error before sharing: updated
maps had been copied to an ignored directory while the actual loader directory
retained an older map missing ClassInput. The editor adapter correctly remained
unavailable. All four processes exited through normal native Cancel to Galaxy
before their owned runner stopped. The failure is retained at
`out/editor-native-party-c10/close-map-mismatch/result.json`.

The actual loader path is `SporebinEP1/mods/sporekit/symbols`. Root corrected and
byte-verified all three maps there, verified the installed 32-entry native map,
and recorded `out/editor-native-candidate10/deploy-corrected.json`. The C10b
four-player runner now compares those installed map bytes with frozen source
before launching and requires `editor_native available=1` on every process in
Galaxy before editor input. All four passed that startup gate with the same
C10 DLL. Later C10b operation, lifecycle and performance receipts must still be
recorded separately; startup checks are not complete native acceptance.

## C10b four-player validation in progress

Fresh normal Alpine entry, native Save As and four explicit baseline joins
passed in the corrected C10b installation. The host and first two guests then
completed Create, immediate Move, native Ctrl+Z and native Ctrl+Y. The fourth
guest exhausted the fixture's three attempts while waiting for ownership; after
idle, the same ordinary inputs acquired Document token 23 and completed the
sequence. All four converged durably at revision 17 with zero native faults.
Receipts: `out/editor-native-party-c10b/four-author-edit/result.json` and
`fourth-author-retry/result.json`. The original failed attempt remains recorded.

All four authors also completed Clone, native Delete, Undo Delete, Undo Clone,
Redo Clone and Redo Delete, restoring exact stable identities and preserving
unrelated placements. The first run completed two authors, then exhausted
ownership retries for the third. A separate retry completed the remaining two,
converging durably at revision 41. Receipts: `four-author-structural/result.json`
and `last-two-structural-retry/result.json` in the same directory.

Public lease snapshots immediately after the failed attempts showed no live
local tokens; a successful retry captured the actual grant followed by normal
release on acceptance. These observations do not establish a persistent
lease-table leak. A minimum wall-time drag helper was subsequently used for the
concurrency fixture; its fourth author still exhausted Create retries.
No failure is converted into a complete workflow pass by these diagnostics.

That resumed fixture subsequently completed four disjoint barrier-started drags
with exactly four accepted gestures, followed by two authors contending for one
placement with one acceptance. All four converged durably at revision 54 with
zero native faults. Receipt: `concurrent-resume/result.json`.

A separate camera/selection gate changed every participant's local camera through
ordinary wheel input without authoring a revision, then required all four Ready
confirmations, natural simulation, reciprocal captains and normal Return. Each
peer's nine visible placement projections returned with exactly zero screen
drift. All four selected placement IDs were lost on Return, so the combined gate
failed despite camera preservation. Receipts: `camera-return/result.json` and
`camera-return/camera-summary.json`. Selection restoration remains a native
lifecycle defect; these receipts do not imply the fix has shipped.

The conservative history fixture also proved checked native Undo rejection:
A's latest move was changed by B, and A's Undo preserved the draft, original
gesture flags and native cursor without skipping an earlier independent action.
B's Undo committed, but A's old inverse correctly remained blocked across that
foreign revision. Receipt: `history-conservative/result.json`.

Follow-up instrumented Create attempts exposed an input-fixture conflict.
Guest 90 retained valid Document token 89 across three drags, while native
capture/rejection counters never changed. Normal `coop.ui at 601 470` showed
existing grouped cast tiles covering that fixed drop point. Changing only the
drop to clear terrain at 625,490 accepted Create, Move and native Undo without
a reset or lease intervention. The unchanged ownership message had been stale.
Receipts: `redo-conflict-instrumented/attempt-leases.json`, its
`hit-regions-at.json`, and `redo-conflict-clear-drop/result.json`. Earlier fixed
drop failures are retained without assuming they all share that cause.

The subsequent foreign Move initially targeted a numeric projection outside
that peer's viewport because cameras were intentionally independent. Normal
local zoom and actual cast hit-region verification are required before retrying
that input. This is a fixture limitation, not a failed canonical convergence
check. After ordinary local zoom and enabled native cast-hit verification, B's
Move committed at revision 63. A's actual Ctrl+Y then rejected with the checked
field conflict, preserving all four drafts, original gesture flags and native
cursor. Receipt: `redo-conflict-native-hit/result.json`.

The four C10b processes subsequently completed normal shared End and native
Cancel/Discard to Galaxy with zero native faults. Their bounded runner then
closed with exit zero, and all four owned PIDs were verified gone. The lab's
recorded child termination code follows its owned shutdown after reaching
Galaxy; it is not evidence of a native crash. Profiles and durable revision 63
were preserved. Receipt: `close-owned/result.json`.

## C11 native lifecycle follow-up

C11 private DLL SHA-256 is
`054ad3a6a64d8021d833c51ca210a92811a85da792b0db938988027e3e73276e`,
built from frozen checkpoint `2e09791`. The private runner verified all three
actual loader maps against source and every process reported native adapter
availability before input. Fresh normal Alpine Save As and four baseline joins
passed. All four authors then completed actual immediate Move and native
Ctrl+Z/Ctrl+Y for their newly created stable placement. A retained fixture failure
had incorrectly applied the fixed 601,470 elevation threshold to a different
clear drop point; that actor's successful immediate Move/Undo/Redo provides the
follow-up interaction evidence. Receipts are under
`out/editor-native-party-c11/{fresh-bootstrap,fresh-join,four-author-edit,remaining-authors}`.

The separate four-player camera/selection gate passed: each peer changed its
ordinary local camera without authoring a revision, entered Test only after all
four explicit confirmations, observed natural simulation and reciprocal
captains, and returned to its exact selected stable ID and local projected
positions. No native faults occurred. Receipt:
`out/editor-native-party-c11/camera-return/result.json`. This resolves the normal
Return selection-loss case observed in C10b; it does not establish every
failure/disconnect transition.

The genuine 128-Move retention prewarm stopped at its retry limit after fifteen
accepted moves. A subsequent instrumented retry committed the next move, and
further genuine moves passed the native 25-entry history cap. This rules out a
hard native-history stop in that case, but does not complete the 128-gesture
warmup or a settled retention window. No memory baseline was taken. The helper
requires a parent-owned marker and an exact four-process roster before that
baseline. A preliminary fixture also stopped before any input because it queried
the general adventure status for a registry counter; the corrected fixture reads
the existing registry line in editor status. Failed and diagnostic receipts are
retained under `memory`, `memory-run`, `prewarm16-instrumented` and
`memory-prompt-retry` in the C11 party directory. There is no C11 memory-budget
or large-editor FPS pass.

Inside-attempt lease samples found a valid token disappearing between mouse-down
and first motion, before another native capture or rejection. The native log also
recorded one checked rejection for missing field ownership. Controlled-native
follow-up made 160 host and 160 guest moves through the Runtime, including more
than 64 lease decisions per peer, with 8,964 checks passing. Therefore the bounded
decision cache is not established as the cause. A separate delayed-grant
reproducer demonstrated the Runtime releasing a newly received token using the
earlier request's idle timestamp, while that token remained locally usable before
its release acknowledgement. Release/reacquisition fencing and grant grace are
being corrected and require new candidate evidence.

The actual two-player network fixture stopped during LAN move 23 after 22 measured
acceptances and one warmup. It did not complete its required 30 samples, WAN or
loss-stress phases, so it establishes no latency-budget pass. Both drafts remained
converged and unfrozen; normal End preserved them. Receipt:
`out/editor-native-network-c11/run/result.json`.

Fresh ordinary native rotation before sharing changed only the single Ace's
orientation, preserving its position and scale. The same calibrated ring input
under sharing rejected before native Start and did not commit after three
attempts. This is an open native guard issue, separate from the demonstrated lease
timing race. Receipts: `out/editor-native-candidate11-fresh-yaw/result.json` and
`out/editor-native-candidate11-shared-yaw/actual/result.json`. Creature scale input
remains unverified. No guard was relaxed to convert those failures into passes.

C11's full controlled-native Runtime suite passed 3,235 checks with frozen source
and executable hashes. Its 24 MiB closure case retained 25,203,841 bytes in the
artifact store after twenty edits and Save, compared with 25,166,419 initially.
The cached JOIN transmitted 29,397 host and 751 guest bytes while retaining the
24 MiB closure. These figures include fixture transport accounting and are not
actual native-game bandwidth or memory measurements. Receipts:
`out/editor-selection-c11-build/{validation,full-validation}.json`.

A personal Spore process subsequently appeared outside the private test
installation. It is excluded from input and shutdown ownership. Strict matched
FPS acceptance requires an exclusive measured cohort; samples with that extra
process can only be diagnostic until it closes naturally. The personal
installation and published gameplay release remain unchanged.

## C12 integrated ownership and transform follow-up

The full C12 private DLL is SHA-256
`0a4a8a96a70a7da46d32d4253a4045b7b09e518966fb47be158b168335aff065`.
Its deployment receipt is `out/editor-native-candidate12/deploy.json`, including
the prior DLL backup and hashes of all three maps in the actual loader directory.
The private games were closed before replacement. Personal files were untouched.

The controlled delayed-grant reproducer established both halves of the C11
ownership race: the Runtime started idle expiry at request time rather than when
the usable grant arrived, and a released token remained locally usable until the
host replied. Runtime grant grace now starts when a new usable token is observed;
renewing the same token does not extend idle grace. Session Release immediately
quarantines the token. Overlapping acquisition waits for the authenticated release
decision, with its TTL starting at outbound admission. Scoped serial fences reject
previously unseen older Acquire/Renew requests after Release while allowing older
disjoint requests to proceed. All queues and fence retention remain bounded.

The final Session tests passed 17,724 Linux ASan/UBSan/leak checks and 13,018
Win32 checks, including 128 four-author edits crossing every peer's 64-entry
lease-decision cache, delayed replies, rejected atomic Release and replayed old
requests. Receipt: `out/editor-lease-c12/validation.json`. The integrated Runtime
passed 2,672 focused and 3,313 full Win32 checks with unchanged input hashes.
Its 24 MiB closure test sent 64,702 host and 3,879 guest bytes for twenty edits and
Save; cached JOIN sent 29,536/782 bytes. These are controlled-native measurements,
not native game bandwidth or memory acceptance. Receipt:
`out/editor-integrated-c12/validation.json`.

The rotation rejection was independently traced to the native active auxiliary
pointer equaling the verified rotation handle at context `+20`. Treating every
auxiliary as patrol-path editing blocked that legitimate pose gesture. The C12
policy permits only the three verified rotation-handle identities for actions 1
and 6, still requiring a selected stable placement and its lease. Unknown/path
auxiliaries and auxiliary cloning remain blocked. Pure policy/binding fixtures
passed 8,241 checks on both Linux and Win32, and the native adapter passed strict
compilation. The diagnostic C12n failure receipt is retained separately at
`out/editor-native-candidate12n/rejection-logs.json`; it is not the full C12 build
and does not establish a successful shared rotation. Actual C12 acceptance follows
separately.

The full C12 two-game shared rotation check subsequently passed for both host
and guest. Each actual native ring drag committed once (revision 0 to 1, then
1 to 2), changed the selected placement's quaternion and preserved position,
uniform scale, class and stable identities. Both native drafts converged, and
normal End and native exit returned both to Galaxy with zero recorded faults.
Receipt: `out/editor-native-candidate12/shared-yaw-retry/result.json`. The first
cold bootstrap had missed an ordinary Create radial click before entering the
editor; that failed fixture receipt remains separate. This resolves the two-peer
rotation guard failure. Uniform scale and four-author rotation remain separate
native gates.

The subsequent Scale audit established that Shift-wheel reaches native tool
action 6. After ownership arrived, the gesture entered native history, but the
opaque-data guard detected a serialized change outside the modeled scale fields
and restored the draft without a canonical commit. Sizes were 15,295 and 15,301
bytes, with the first normalized mismatch at byte 14,266. No native fault was
recorded, and both processes exited normally to Galaxy. This is a failed native
Scale acceptance case requiring field classification, rather than an unresolved
input-delivery assumption. Receipt:
`out/editor-native-candidate12/scale-audit/result.json`. No broader opaque-data
exception was added to obtain a pass.

The four-game C12 operation matrix subsequently passed native Create, Move and
Ctrl+Z/Ctrl+Y for all authors (17 exact revisions), disjoint and contested native
drags, and a quaternion-only native rotation from every author. Offline angular
checks confirmed approximately 19.18 degrees per rotation, not quaternion sign
changes. Receipts: `out/editor-native-party-c12/{four-author-edit,concurrent,yaw}`.
The separate 128-Move prewarm filled Core history to exactly 128 on all peers,
with host native history capped at 25 and one registered package per peer.
The post-cap Test/Return retention window is recorded separately; these operation
checks do not imply a memory or FPS pass.

### Actual C12 two-game network measurements

The actual native network fixture completed 30 accepted gestures per profile,
plus one warmup, at canonical revision 121. Every intended operation committed
exactly once and both native drafts converged. The fixture then completed normal
End, native exit and party leave; the separate handoff verified both at Galaxy,
editor phase zero and zero native faults.

| Profile | Native submission-to-confirmation p95 | Result |
| --- | --- | --- |
| Loopback LAN | 190.776 ms | Below 200 ms. |
| 100 ms RTT, 1% loss, 2% reordering, 1% duplication | 287.038 ms | Below 500 ms. |
| Same WAN with symmetric 2 Mbit/s per peer/direction | 287.081 ms | Below 750 ms under rate limiting only. |
| 100 ms RTT, 10% loss, 5% reordering, 2% duplication | 699.761 ms | All operations converged; no normal-network latency target applies. |

Timing starts at the Runtime's Controller submission after lease gating and
native inverse settlement, and ends after the author's accepted canonical native
application and durable commit. Proxy traffic records source UDP payload bytes,
headers, acknowledgements and actual retransmissions before injected duplicates;
IP/UDP headers are excluded. This is actual engine/IO evidence, unlike the earlier
controlled-native benchmark. There was no simultaneous bulk asset transfer, so
the 2 Mbit/s row does not pass the asset-contention gate. It is a two-player
receipt, not four-player network adversity acceptance.

The machine also had the four-game functional cohort and one unowned personal
game running. Their PIDs and hardware are recorded with the measurement; no
matched FPS or memory claim is inferred. Receipts:
`out/editor-native-network-c12/{run/result,handoff,environment-diagnostic}.json`.

### C12 post-capacity retention diagnostic and native return adversity

The four-game functional retention run completed 128 genuine Move transactions
before its baseline, then 30 changed edit/Test/Return cycles. Core history stayed
at 128 on all peers, host native history at 25, and registered package count at
one per peer. All 150 explicit-ready hold checks passed; 120 native clocks
advanced and 360 reciprocal captain checks passed, with maximum received pose
error 0.99 m and state age 0.13 seconds. This closes that functional cycle window.

It does not pass memory or FPS acceptance. Seven Spore PIDs were live at baseline,
including three outside the measured cohort; diagnostic mode explicitly made the
run ineligible. Final private-memory growth was 22.71 MiB on the host and
27.84/28.19/26.98 MiB on guests. The guests' last ten samples each spanned about
0.15 MiB with slightly negative slopes, while the host's last ten spanned 1.76 MiB
and still rose by about 0.205 MiB per cycle. These are raw retention observations,
not an attribution to either leaks or caches. Normal shared End released
13.98/3.35/3.91/1.68 MiB; normal native Galaxy exit released
30.41/24.31/25.34/24.26 MiB relative to cycle 30. The owned editors returned to
Galaxy before WindowsLab process cleanup. Receipt:
`out/editor-native-party-c12/memory-diagnostic/retention-analysis.json`.

Separate C12 two-player native checks passed Build-hammer return, guest disconnect
during preparation, and guest disconnect during Test followed by explicit rejoin.
Each preserved a genuinely edited draft, history and local context and completed
normal Galaxy exit without native faults. Source identities and verified authored
content matched; raw native hashes remain diagnostic because only the documented
local camera and derived selection bits may normalize. Earlier raw-hash and
obstructed-input fixture failures remain recorded. Receipt:
`out/editor-native-lifecycle-c12/validation.json`; detailed reproduction notes are
in [the native save notes](../re/adventure-editor-save.md).

## C13 preparation Cancel and imported recovery

The C13 private DLL is SHA-256
`db87a588ab30c0be25912c9ad61f125b403df77d1c1873915edea0353c632678`.
It adds a scoped **Cancel playtest** action during preparation. A queued action
remains valid if the final Ready crossed it into that same round's startup;
different project, epoch, revision or round callbacks cannot cancel a later Test.
No new cancellation is admitted after that preparation phase.

Controlled-native Runtime checks passed 2,938 focused and 3,579 full assertions,
including zero/one-Ready cancellation, stale callbacks, immediate retry and the
final-Ready race. The repository-wide portable rerun passed with unchanged C13
production/test inputs and zero knowledge-lint problems. Receipts:
`out/editor-preparation-cancel-c13/validation.json` and
`out/editor-portable-c13/validation.json`.

Actual two-player native checks then passed the visible zero-Ready Cancel button
at 1024x768, public Cancel after one Ready, and immediate retry through natural
Test clocks and normal Build-hammer return. A second stage recovered the former
guest's imported project: authority remained withheld until ordinary native Save
As completed and its successful-save dialog was confirmed. The resource changed
from `408A0000!2909430D.366A930D` to `408A0000!29094310.366A930D`, preserving
project, revision 1, history and authored placements. The former host explicitly
rejoined the recovered draft and both reported matching verified authored content.

Both stages returned normally to Galaxy, detached and offline, with zero native
faults. The personal game and installation were untouched. Receipt:
`out/editor-native-lifecycle-c13/validation.json`, including result and screenshot
hashes. These are same-process two-player observations. Cold native file reopen,
cold imported recovery, natural successful/failed Test results, four-player
lifecycle adversity, bulk asset contention, large-editor FPS and sustained memory
acceptance remain open. C13's subsequent Scale diagnostic has not passed: it
isolated native marker companion `+24` / property `071147E9` changing while
physical scale `+20` stayed at its tier boundary. A typed-field correction is
being prepared; opaque comparison has not been broadly relaxed.

## C14 continuous scale, legacy projects and native file reopen

The private C14 DLL is SHA-256
`148f63c91e11109b85ad1b10e01e707adb86d3f5744be2a4cd54f3712cf1754c`,
69,672,448 bytes. The existing private installation was backed up and all private
games closed before deployment. The personal installation and game were untouched.
All three loaded maps still match their reviewed source; exact-image verification
passes 32 editor entries, 13 save entries and two history entries. No additional
callable native address was introduced for the scale correction.

New projects register native property `071147E9`, marker `+24`, as a required finite
float alongside physical scale `+20`. The native calculation does not impose a
final zero-to-one clamp on that companion. Capture emits its own checked Property
Set, even when physical scale is unchanged; application writes the exact value.
Ordinary Clone must preserve it. Combined Clone/property changes remain unsupported.
Legacy schema-empty recovery retains its old schema and history and blocks new
native Scale input with a nonfatal explanation; starting a new shared project from
a native copy enables the new projection without rewriting the old project.

The full portable run passed, including 184 scale/schema checks, 150 read-only
reopen identity checks and 18,211 real-UDP Session assertions with native doubles.
All fifteen freshly built Win32 editor fixtures passed; Runtime's focused run
passed 3,732 checks and Session passed 12,874. The exact staged Runtime full run
passed 4,384 checks; its ten applied production/native-contract files matched the
reviewed proposal after line-ending normalization. Receipts are
`out/editor-runtime-c14/validation.json` and
`out/editor-integrated-c14/{build-result,windows-validation}.json`, with the full
portable log at `out/editor-integrated-c14/portable.log`. These are fixture/build
results, separate from actual native observations below.

The actual legacy recovery test passed on both native processes. It recovered
C13 project `5cd1e3d3fa534ed26493fa5ae477ba6b`, source
`408A0000!29094310.366A930D`, without migration. Each author's attempted Scale
stopped before native history or canonical mutation, preserving raw bytes,
revision and history. Genuine Move and native Undo still passed. Receipt:
`out/editor-native-candidate14/legacy-recovery/result.json`.

The small typed native workflow then passed both authors' Scale and native
Ctrl+Z/Ctrl+Y with exact companion convergence. A genuine native Save at revision
6 received both exact SavedApplied acknowledgements. Normal End and native exit
preceded ordinary exact-title reopening of the saved resource. The read-only
command returned `REOPEN_VERIFIED revision=6 source=408A0000!29094C76.366A930D`
before any Recover or native repair. It validates the observed saved bytes and
opaque authored content using only copied identity rebinding. Subsequent
checkpoint recovery preserved the scale companion and history. Both games
returned normally to Galaxy, offline and without native faults. Receipt:
`out/editor-native-candidate14/scale-save-native-reopen-retry/result.json`.
This is a same-process native-file reopen pass, not a cold-process reopen pass.

Separate failures remain recorded. Guest Clone preserved the companion and Undo
removed it, but Redo could not retain an explicitly granted tombstone capability
after document ownership expired. Session tick pruning discarded non-live IDs
even though Core permits exact retained tombstones. A separate C15 fix and real
UDP regression are under validation. The original actual failure is
`out/editor-native-candidate14/typed-scale/result.json`.

Repeated enlarged Scale left the host's live pose different from its canonical
draft and the guest, with a red placement appearance. C14 had no counter proving
that a native cancellation actually executed. Normal End restored the canonical
draft, and both exited without faults. Static follow-up separately identified
native cancellation calling the same entry Undo function intercepted for user
Undo. A scoped cancellation fix requires its own runtime evidence; the visual
appearance alone does not establish that this branch caused the observed
divergence. The failure receipt is
`out/editor-native-candidate14/relative-only-large-native-save/result.json`.
Earlier bounded boundary-search and UI calibration failures remain separate and
do not prove a relative-only actual native acceptance case.

### C14 actual four-player network and remaining test limits

Thirty LAN native gestures converged with p95 confirmation 194.693 ms, below the
200 ms budget. Thirty gestures under 100 ms RTT, 1% loss, 2% reordering and 1%
duplication also converged, but p95 was 519.477 ms, above the 500 ms target. The
fixture correctly stopped with failure at revision 61; its rate-limited and
10%-loss profiles did not execute. Earlier two-player passes do not replace this
failed four-player gate. Receipt: `out/editor-native-network-c14/four/result.json`.

All four subsequently exited their native editors normally to Galaxy, offline
and with zero native faults, before the owned runner stopped. Two guests retained
an old Offered view after transport teardown; this is a distinct lifecycle defect,
not an active adventure or a native crash. Exact final phases are recorded at
`out/editor-native-party-c14/normal-close/result.json`; the report does not claim
all four were Idle. A teardown fix is being prepared separately.

Actual Adventure Town content has 419 placements but only four transferred
records: its cold project envelope is 148,024 bytes, approximately 0.59 seconds
at an ideal 2 Mbit/s. Stock creations are deliberately omitted by the collector.
This cannot establish thirty samples of sustained bulk contention. The shared
host-link test preparation has five passing Windows/Linux UDP/parser tests in
`out/editor-native-bulk`, but genuine larger native content and overlapping
game-process measurements remain required. No synthetic padding is a substitute.

Natural success/failure Test results, failed loading, cold native reopen,
four-player scale/cancellation, sustained bulk contention, four-player WAN
latency, large-editor FPS and settled memory acceptance remain open. The
restricted preview is still under validation and is not ready for installation,
main merge or release.

### C15 restoration ownership follow-up, before native deployment

The native Clone Redo failure has a separate real-UDP reproducer. Frozen C14
Session code failed twelve focused assertions: the host granted exact tombstone
Placement/Field capabilities, but guests removed them on ordinary ticks. An
intermediate correction exposed six further failures involving cached replies
restamped with a newer revision and an expired Renew displacing a newer token.
Both failing source/executable/output sets are retained.

The applied Session correction preserves Core's policy that every accepted
commit retires non-live Placement/Field capabilities. It records the retirement
revision per exact retained identity and the host issue revision/serial per
usable local scope/token. A new tombstone grant survives idle ticks; an older
grant cannot return after deletion, unrelated commits or restoration. Equal
revision grants that overtake a deferred replica commit remain valid. The
64-entry reply cache retains its original issue revision instead of restamping
revoked payloads. Older Renew replies cannot replace a newer usable token.
Metadata remains bounded by retained document identities and local lease scopes;
release quarantine, expiry and project/epoch/peer fences still apply.

The final focused suite passed 2,178 checks on both Linux sanitizers and Win32.
Full Linux ASan/UBSan/leak testing passed 19,866 checks; full Win32 testing passed
14,874. A fresh integrated Runtime quick build passed 3,732 checks after applying
the fix. Receipt: `out/editor-tombstone-c15/validation.json`; the Runtime output
is `out/editor-integrated-c15/runtime-quick.txt`. No protocol, wire layout or
Core ownership policy changed. These results do not establish actual native
Clone Redo acceptance for the corrected candidate, which has not been deployed.

### C15 cancellation and transport teardown, before native deployment

The invalid Scale failure is now reproduced by a fixture that compiles the
production native adapter for the actual i686 ABI. Native in-progress entry
cancellation must restore that entry, rather than submit the player's prior
action as Undo. The adapter verifies the current history entry, its primary
interface, owner, native generation and the logical project/epoch/author captured
at gesture Start. It restores a private before snapshot, preserves the local
camera and selection, and waits for native finalization plus an actual frame
advance before reconciling the latest committed draft. Cancelled or replaced
contexts cannot restore or freeze a newer editor. Settlement submits no Core
history operation. Unsupported cancellation entries freeze with recovery guidance.

The exact GOG verifier passes 33 editor entries, including the read-only history
interface vtable and its cancellation callers. The production adapter fixture
passes 422 checks. A read-only ordinary-cancellation counter will distinguish an
actual native red-placement rollback from ignored input in the game comparison.
The Runtime fixture also covers a pre-existing own action, an intervening peer
edit, duplicate/stale cancellation tickets, restoration failure and disabled
Save/Undo/playtest controls while native settlement is held. These controlled
tests do not establish actual native cancellation acceptance. Receipts:
`out/editor-native-re/c15-cancellation/manifest.json` and
`out/editor-cancellation-c15/validation.json`. The final Runtime fixture passes
281 focused, 4,416 quick and 5,068 full checks, including the 24 MiB resource,
cached join and recovery cases.

The stale Offered state has a separate Runtime reproducer. Transport teardown
now retires its old editor view even when native cleanup is already waiting.
Cleanup remains scoped to the owned editor generation and logical capability;
a newer native context is untouched. At most one authenticated offer from a
later connected transport can wait until old cleanup finishes. Ordinary connected
End/reoffer behavior remains available. Explicit Leave, remote disconnect,
repeated cleanup, delayed callbacks and immediate rejoin are covered by fixtures.

Those tests also exposed an existing transport use-after-free: receiving BYE or
Reject could destroy the Connection while its packet callback was still running.
Packet dispatch now retains its private peer owner until callback return, and a
monotonic process-local transport generation retires remaining callbacks and the
old update timestamp after Leave/rejoin. No wire layout changes. The transport
fixture passes 63 checks on Win32 and Linux address/undefined/leak sanitizers.
The teardown Runtime fixture passes 401 focused checks and 4,788 full checks;
the editor UDP fixture passes 20,143 sanitizer checks. Timing-dependent polling
counts vary. Receipt: `out/editor-transport-teardown-c15/validation.json`.

All 17 freshly rebuilt GA Win32 regression fixtures pass with the transport
change. The portable suite passes, and the published feature branch's
[GitHub build and tests](https://github.com/jwhans0n/SporeCoopGA/actions/runs/40384731915)
pass at `a7e37bf`. These precede the combined C15 native candidate acceptance.
The combined C15 candidate passes all 17 freshly built editor Win32 fixtures,
including full Runtime (5,067 checks) and UDP (15,195 checks), with every recorded
source input unchanged after the run. The applied Native, Runtime and transport
files match the independently tested proposals after line-ending normalization.
Candidate SHA-256:

```text
5afca1756c395a3ee00b039365002ecf5279c34ecefe5480d3f99c35544e815f
```

The 69,890,048-byte DLL and exact maps were backed up and deployed only to the
private test installation while all private games were closed. Receipt:
`out/editor-integrated-c15/validation.json`; deployment:
`out/editor-native-candidate15/deploy.json`. Native cancellation, tombstone Redo
and reconnect still require actual game checks. The four-player WAN budget,
bulk contention, large-editor FPS and settled memory gates remain open.

### C15 actual history and Scale boundary checks

Both native authors passed Create/Undo/Redo and Delete/Undo/Redo with exact
stable identities and authored values. The initial complete driver then stopped
at an explicitly inconclusive invalid-placement search. A separate scaled Clone
regression passed for both authors: ordinary Clone preserved the source's exact
scale companion, native Ctrl+Z/Ctrl+Y restored the same clone identity, and
Delete/Undo/Redo preserved all remaining fields. Cleanup reached detached native
editors and offline party state. Receipts:
`out/editor-native-c15-acceptance/run-native-key-retry/result.json` (partial
history passes, overall inconclusive) and
`out/editor-native-c15-acceptance/run-clone-two-events/result.json` (passed).

Both authors also passed a genuine physical-unchanged/companion-changed Scale
operation at the native upper bound. Physical float bits stayed `00006041`
(14); the host companion changed `100c833f` to `fc37863f`, then the guest changed
it to `1f82813f`. Each used two ordinary wheel events with the verified signed
magnitude, followed by native Undo and Redo. Every other authored field remained
identical and cancellation counters stayed unchanged. Single-event attempts and
fixed input points that missed the enlarged body remain failed calibration
receipts. No particular internal cause for the single-event behavior is claimed.

The subsequent native Save completed at revision 7 with exact SavedApplied 2/2.
The generic Save/reopen harness nevertheless timed out before reopening. Its
additional condition required each live native source key to equal the saved
root. The host rotated to `408A0000!29095FA2.366A930D`; the guest retained its
private `408A0000!29095FA1.366A930D`. The adapter deliberately preserves each
live resource key during restoration. Host settled status was observed true in
238 samples, so the receipt does not establish a coordinator settlement defect.
A logical saved-root diagnostic and corrected alias-aware harness are being
validated before retry. The complete run remains failed; its separate Scale
checks do not establish native file reopen. Receipt:
`out/editor-native-c15-acceptance/run-relative-two-events/result.json` and its
per-process command logs.

The actual C15 transport lifecycle run subsequently passed connected End,
explicit Leave, former-guest Host role change and authenticated rejoin. It also
passed disconnect/rejoin during both preparation and active playtest, preserving
the draft and history. Both processes returned to Galaxy, offline, with zero
native faults, and the owning runner closed them normally. Receipt:
`out/editor-native-c15-acceptance/run-teardown/result.json`.

Two bounded pure-wheel searches did not invoke native cancellation, including
valid Scale at physical 14 / companion 1.02380562. The verified native branch
and 422-check routing fixture remain distinct from an actual invalid-input
cancellation pass, which is still pending.

### C16 first-act guard and read-only diagnostics, before native deployment

The act audit found that resource restoration rebuilds using the local native
act without setting it. A fresh guest normally starts on Act 1. This preview
now restricts sharing to Act 1 before Save As, baseline registration/entry and
recovery, and rechecks the actual loaded context. Core and wire types still
support general acts. A baseline Runtime test reproduces the missing guard;
210 focused checks pass with the restriction. Synchronized host act changes and
independent acts remain unverified follow-up work. Receipt:
`out/editor-act0-c16/validation.json`.

Opt-in timing uses a fixed 4,096-event numeric buffer and 32-event console pages.
It records operation identity, Windows QPC, native apply and durable-save spans
outside the render frame. External packet tracing records reliable IDs and
actual loss/retransmission chronology without changing policy or payloads.
The actual four-player WAN failure remains 519.477 ms against 500 ms. The
instrumented Runtime passes 5,276 full checks; recorder, proxy, collector and
correlation fixtures pass separately. Receipt:
`out/editor-wan-diagnostics-c15/validation.json`.

Passive accounting exposes bounded logical containers and buffer capacities;
these are explicitly not allocator or process-memory totals. The closed C12
checkpoint contains 128 history entries but only 192 of 1,024 retained decisions,
so history saturation did not establish saturation of every container. The
3,425-check accounting suite passes on Win32 and address/undefined/leak
sanitizers; combined Runtime accounting and trace checks pass. This identifies
unsaturated containers without attributing the OS memory slope. Receipts:
`out/editor-accounting-c16/validation-trace.json` and `retention-audit.md`.

The combined pre-saved-root C16 build passes all 19 freshly built editor Win32
fixtures, including full Runtime 5,347 checks. Its SHA-256 is
`abb0f30306bb80fc0a277980308fdf00573eb9589ea265aec4d94569ae308672`.
It has not been deployed. The alias-aware saved-root follow-up must receive
its own final build and verification before the next private deployment.

The final C16 build includes that read-only logical saved-root diagnostic and
the stricter alias-aware harness. It requires identical logical saved roots,
exact SavedApplied coverage and revision on every peer, the host's new native
root, and each guest's unchanged private live alias. It does not relax native
document integrity or settlement checks. All 19 freshly built Win32 fixtures
pass, and all recorded production/test/symbol inputs remained unchanged during
the build and test run. Candidate SHA-256:

```text
0f2a5b1d4b4e0edd533d12dae900d7b137bff37b6e9a300147a3a27f8abb4acf
```

The DLL is 71,383,552 bytes. Repository-wide portable `make test` also passes,
including the new passive accounting and timing recorder fixtures and symbol
lint (345 used, 2,919 known, zero problems). The portable run began before the
saved-root follow-up, whose production changes are Windows-only; its native
Save/reopen Python self-test ran with the corrected helper. Receipts:
`out/editor-integrated-c16/validation.json` and `portable.log`. Actual native
acceptance of this candidate remains separate.

### C16 actual Save/reopen and first-act refusal

The final C16 candidate and exact symbol maps were deployed only to the private
installation with every private game closed and a C15 backup retained. Both
hidden processes then passed a fresh normal Alpine creation, native Save As,
explicit shared Join, a genuine authored Move, and another native Save. At
revision 1 the host reported exact SavedApplied 2/2 and both peers reported
logical root `408A0000!29096B31.366A930D`; the guest kept its original private
live alias `408A0000!29096B30.366A930D` as designed. Normal End and native exit
preceded reopening the exact-title file through the ordinary browser.
`REOPEN_VERIFIED revision=1` then confirmed the saved root, stable placement
poses and opaque authored fields before any Recover action. Native faults
remained zero and normal cleanup completed. Receipt:
`out/editor-native-c16-acceptance/run-save/result.json`.

An ordinary Add Act switched the detached editor to native index 1. Begin then
refused with the first-act explanation before invoking Save As, without changing
the native snapshot or editor generation. The normal unshared act control
remained usable. Receipt: `out/editor-native-c16-acceptance/act-guard/result.json`.

The ordinary invalid-drag probe instead produced a valid native Move, so it is
inconclusive for cancellation and is not counted as a rollback pass. Attempts
to author a minimal natural-results fixture have not yet produced a verified
goal. Those remaining gates stay open. The two private processes subsequently
returned to Galaxy/offline with zero faults and closed normally. The user's
separate personal game was then closed normally with their explicit permission
for the exclusive four-player cohort. No personal installation update occurred.

The feature branch's [C16 GitHub build and tests](https://github.com/jwhans0n/SporeCoopGA/actions/runs/40394478123)
pass at `e5aaf45`. This is independent of the still-running native network and
performance acceptance work.

### C16 four-player native acceptance

The exclusive cohort contained exactly four hidden, muted private Spore
processes, with the same final C16 DLL and exact loader maps. The user's personal
game had been closed normally with their explicit authorization. No personal
profile, installation or settings were changed.

All four authors passed native Yaw, typed Scale with native Undo/Redo, scaled
Clone with Undo/Redo, and Delete with Undo/Redo. Each operation preserved exact
identities and fields on all participants, with zero native faults. Receipt:
`out/editor-native-party-c16/transform-history/acceptance.json`.

Thirty genuine native Moves per network profile each produced exactly one
accepted commit and converged durably on all four peers. Confirmation p95:

| Profile | C16 p95 | Budget | Result |
| --- | ---: | ---: | --- |
| LAN | 194.458 ms | 200 ms | Pass |
| 100 ms RTT, 1% loss, 2% reorder, 1% duplication | 334.361 ms | 500 ms | Pass |
| Same WAN, symmetric 2 Mbit/s per peer | 331.197 ms | 750 ms | Pass |
| 100 ms RTT, 10% loss, 5% reorder, 2% duplication | 792.097 ms | No latency budget | Converged |

The per-peer limiter is not an aggregate host-link measurement. This run did
not transfer a genuine missing asset bundle concurrently, so bulk contention
remains open. All 30 WAN operations have complete identity-scoped native/QPC
and external UDP traces. The longest WAN sample was 557.736 ms, including
7.544 ms host request-to-decision and 310.571 ms from first proxy forwarding
to decoded decision; this alone does not identify a loss/retransmission cause.
The earlier C14 WAN p95 failure remains recorded. No reliability policy was
changed to obtain this new pass. Receipt:
`out/editor-native-party-c16/network-traced/result.json`.

The strict four-player large-editor comparison used a native Save As copy of
Adventure Town with 419 placements, identical processes/cameras/configuration,
frame cap disabled and Present sampling. Shared editing was sampled first,
then normal End/Leave detached the same native drafts without restoration or
authored input before the independent baseline. Twenty-second samples after
eight-second settling passed: median FPS ratios were 0.998325, 0.998369,
0.997252 and 0.998356; p95 frame-time ratios were 1.006249, 1.006571,
1.006214 and 1.006985. Budgets remain median FPS at least 0.90 and p95
frame-time ratio at most 1.15. The first harness attempt stopped before input
because its factory omitted `stage`; the corrected retry is the measurement.
Receipt: `out/editor-native-party-c16/fps-four-retry/performance-result.json`.
The separate two-player FPS comparison and settled memory window remain open.

### C16 natural results and local camera defect

Normal native input produced a minimal one-act adventure with one Move To
objective targeting its sole actual placement class. The native class was
1024, not zero; the catalog reports the marker's runtime ID, which can be
`UINT_MAX` before play. Validation requires the actual class and single marker,
not a fabricated slot-zero identity. The first fixture-gate attempt rejected
these legitimate values before Test and was corrected without changing game
data. Fixture authoring remains ordinary detached editing, not Stage 5 support.

With the authored captain near the target and no timer, all four players
reached real native success state 5 after approximately 1.5 seconds, displayed
Mission Complete, and kept their result clocks frozen. Host Done returned all
four to the same Ready draft, preserving authored fields, local camera and
selection, and history. An immediate Test retry and cancellation also passed.
No outcome was injected. Receipt:
`out/editor-native-party-c16/natural-success/result.json`.

With the captain away and the native 60-second timer enabled, all four reached
real failure state 6 at 60,020 ms and displayed Mission Failed. Their clocks
remained frozen, and host Done returned everyone to Ready with the authored
draft and history preserved. The acceptance check correctly failed because
the host's locally located camera shifted its projected landmark from
`(511.999969,394.242371)` to `(512,384.000427)`; all three guest cameras stayed
unchanged. This is a production return-path defect, not a timeout or content
failure. It remains open until a verified local-camera restoration fixes it
and the same native failure test passes. Receipt:
`out/editor-native-party-c16/natural-failure-class-retry/result.json`.

Both natural-result runs ended with normal detached editor cleanup. The owner
then returned all four to Galaxy/offline with zero faults and closed them
normally. These small fixtures do not establish complete authored adventure
playthrough or reward/XP coverage.

The passive memory collector now records logical capacities separately from OS
private/working-set counters, verifies the complete roster and settled context
both before and after every admitted sample, and records normal End/Galaxy
release snapshots. Sixteen pure tests and the existing party harness self-test
pass. It cannot automatically declare a memory pass. Actual strict memory
acceptance remains pending.

A fresh pair of C16 processes subsequently reopened the exact native saved root
`408A0000!29096B31.366A930D` through the ordinary browser before any Recover.
The browser correctly contained two same-title Save As versions; each actual
card was inspected and only the expected source was admitted. The projected
semantic hash and complete known placement pose matched the prior reopen.
The raw native hash changed from `9207984f...` to `ccb4e789...`, with serialized
size 15,291 bytes in the fresh process. Full opaque-data acceptance remains
open until those differences are explained and compared, rather than omitted.
The first Create click at cold Galaxy was ignored and is now retried only until
the actual radial opens. The first browser selection also exposed a short
loading interval where native readiness preceded visible editor controls; early
exit input was ignored. Once settled, normal exit succeeded with zero faults.
Receipts: `out/editor-native-pair-c16-final/cold-reopen-exact-root/` and
`cold-reopen-settled/result.json`.

The strict two-player 419-placement Adventure Town comparison also passes with
the same process, camera and configuration requirements as the four-player
run. Median FPS ratios are 1.002018 and 1.001328; p95 frame-time ratios are
0.996271 and 0.995206. The setup needed a normal native browser search because
recent private creations put Adventure Town off-screen. Earlier setup attempts
collected no FPS samples and remain failed/inconclusive receipts. The corrected
run retained the original budgets and required the exact 419-placement source.
Receipt:
`out/editor-native-pair-c16-final/fps-two-search-settled/performance-result.json`.
Together with the strict four-player pass, this closes C16's large-editor FPS
gate. It does not establish resource turnover, gameplay FPS or future candidate
performance.

An additional read-only `inspect bytes OFFSET COUNT` diagnostic exports owned
native serialization in pages of at most 4,096 bytes, with the complete snapshot
hash and size on every page. It retains no extra snapshot and writes no files.
Its Win32 Runtime checks cover exact hex output, end-of-buffer, malformed and
overflowing offsets/counts, output bounds, and unchanged native resources,
restoration count, canonical state and durable checkpoint. Quick Runtime passes
5,250 checks and full Runtime passes 5,901. This is not yet in the deployed C16
DLL; actual cold opaque-data comparison awaits the integrated next candidate.
The full fixture's 24 MiB closure remains synthetic test data, not genuine bulk
network acceptance. Build: `out/editor-audit-c17/host/editor_runtime_windows_tests.exe`.

### C17 integration and actual failure return

The local camera fix captures the actual active editor orbit target, rendered
quaternion and current orbit scalars immediately before Test. It restores the
canonical draft and local selection first, then restores those values through
verified native instant setters. No camera pointer survives Test and no camera
values enter the shared document or network. Three new named functions bring
the exact-executable editor verifier to 36 entries. The native ABI fixture
passes 639 checks, including different current/desired views and no selection.

Cleanup retains this one value pin through End and transport loss. It checks
the immutable native preview's exact project/epoch/round/revision independently
of a disconnected Controller, plus the existing native mode/data/resource/key
owner before Stop. Only that synchronous owned Stop can advance the cleanup's
native generation. Replacement contexts receive no Stop or restoration. Return
acknowledgement follows canonical, selection and camera restoration. Delayed
camera activation retries for up to five seconds after native readiness; a
missing or stale owner fails closed. Init/shutdown now clear transient return
latches exposed by a controlled failure. Native Preview passes 84 checks;
focused return tests pass 818, quick Runtime 5,799 and full Runtime 6,451.
An independent ownership review found no remaining blocker. Receipts:
`out/editor-camera-c17/` and `out/editor-camera-c17-returns/`.

Imported native databases now have a process-lifetime admission ledger with at
most 32 charged package attempts and 256 MiB validated aggregate payload. This
does not bound expanded native/graphics memory. Same-module exact-owned cached
priority promotion retains its caller reference and consumes no new admission.
Uncertain failures remain charged. A trusted cold SDK lifecycle is mandatory;
nonempty DLL reload requires restart because database lifetime cannot be proved.
The fixed 1,960-byte ledger uses one process-owned mapping handle and never
stores a native pointer or unloads a live resource. Controlled tests pass 3,238
core checks on Win32 and Linux sanitizers, 14,230 Windows lifecycle/mapping
checks and 987 native ABI/content checks. The
[ownership notes](../re/imported-package-admission.md) describe limits and
failure behavior. Receipt: `out/editor-package-admission-c16/implementation-receipt.json`.

The integrated DLL is 71,650,816 bytes, SHA-256
`15993dd20451c0189c0b9b86f850746c16632038fa931fecf600353635ba27b4`.
All 21 Windows suites and 21 additional admission cases pass. The repository-wide
portable `make test` also returns zero, and all frozen source inputs remained
unchanged. All 36 exact-executable editor symbol checks pass. Receipts:
`out/editor-integrated-c17/validation.json`, `portable-validation.json` and
`native-symbol-verification.json`.

This candidate was installed only in the private test copy, with a C16 backup.
Four hidden, muted games reached normal Galaxy and `admission_boot=3` at cold
startup. Their first genuine native collection/import charged one package and
52,743 validated payload bytes each, with no uncertain admission.

The original C16 natural timeout camera regression now passes on all four C17
processes. The ordinary saved fixture has one actual MoveTo objective and a
60-second native timer. All four naturally reached failure at simulation time
60,033 ms, retained the frozen terminal clock, displayed Mission Failed, and
returned through the host's normal Done action. The unselected host's landmark
stayed exactly `(511.999969,394.242371)`; the three independent guest landmarks
stayed `(512,384.000427)`. Canonical authored content, native source, selection
and semantic history were preserved. Immediate Prepare/Cancel retry also passed,
and cleanup completed normally with zero native faults. Receipt:
`out/editor-native-party-c17/natural-failure/result.json`. This resolves C16's
specific camera defect, not the remaining complete-playthrough or XP gate.

Additional lifecycle routes, cold opaque-data comparison, replay/promotion,
memory turnover and genuine bulk-contention acceptance remain separate gates.

The C17 naturally completed one-MoveTo success fixture also passes all four
native result panels, frozen terminal clocks, Done return and immediate retry.
No outcome is injected. Receipt:
`out/editor-native-party-c17/natural-success/result.json`.

The C17 lifecycle run passes normal native Build-hammer return with distinct
local cameras/selections on all four participants. Guest disconnect during both
preparation and active playtest preserves its authored resource, camera and
selection before normal exit. Each fresh-capability rejoin converges with the
surviving three. These route gates passed in
`out/editor-native-party-c17/memory-strict/result.json`, but that combined run
is not a memory pass: its initial sample timed out before prewarm or measurement.

The timeout exposed a collector admission issue. Native runtime refresh sends
reliable presence every 300 ms, even without editing. The stronger
`Controller::settled()` transport-idle diagnostic can therefore be false during
an otherwise idle, fully converged draft. The revised passive collector requires
matching native canonical hashes and durable revisions, unchanged exact logical
and native ownership, and empty authoring/transfer queues both before and after
collection. It retains the transport-idle flags and actual reliable packet
counts rather than treating periodic acknowledgements as pending authoring.
Active gestures, queued operations, transfers, late work, divergent hashes,
changed hardware or an extra Spore process still reject the sample. All 19
fake-reader tests pass on Windows and WSL. Actual retention acceptance remains
pending the new strict run; no memory threshold has changed.

### C17 exclusive post-capacity window: functional pass, heap gate open

The corrected collector completed 128 genuine warm-up Moves followed by 30
changed Move/Test/Return cycles with exactly four live Spore processes. Every
baseline, cycle and release sample rechecked that roster, CPU/GPU/driver identity,
hidden/muted HWND ownership, native generation, canonical hashes, durable
revision, selection and empty authoring/transfer queues before and after the
passive counters. The raw result is eligible for memory review and is separate
from timed FPS measurement.

All 150 explicit-readiness holds passed. All 120 native clocks advanced, and all
360 reciprocal captain checks passed with maximum reported position error
1.00 m and age 0.16 seconds. Core history remained at 128 everywhere; host native
history remained at 25. Native package counts stayed at their per-process
baseline of 4/5/4/4. The first failed collector attempt did not produce these
samples; its rejection remains recorded separately.

This is still **not a sustained native-memory pass**. Final private-memory
changes were +3.89/+3.46/+3.17/-0.52 MiB. Last-ten slopes remained positive:
0.140/0.091/0.090/0.125 MiB per cycle. Host Core decision estimates grew by
bounded metadata, but their unsaturated 1,024-entry decision limit and the
logical capacity counters do not attribute all native/process allocation.
Shared End and normal native Galaxy exit completed. Final Galaxy footprints
were 28.45/19.94/18.57/21.55 MiB below their initial editing baselines. Normal
release is proven; an active native heap plateau remains unproven.

All four games then completed verified normal WM_CLOSE with exit code zero.
The separate WindowsLab wrapper raced those completed exits and reported an
Access Denied while trying to terminate an already exiting child; this is a
lab cleanup race, not a native game fault. The independent exact-handle closure
receipt records all four successful exits. Receipts:
`out/editor-native-party-c17/memory-strict-retry/result.json`,
`retention-analysis.json` in that directory, and
`out/editor-native-party-c17/normal-close-result.json`.

The pushed camera checkpoint `662a139` and follow-up `27012bd` both passed GitHub
Actions: runs 40415069319 and 40417161642. The published main/release and personal
installation remain unchanged.

### C17 actual cold native file comparison

Two fresh, hidden, muted C17 processes opened the recorded private native file
through normal UI. The selected card resolves exactly to
`408A0000!29096B31.366A930D`; no Recover or shared-project restoration ran.
Placement position, rotation and scale match the saved values, and the semantic
hash remains `2d6a4afb22721c6dff3377db488c8389511aef19d35aac634ebbf23847c390c2`.

The entire 15,291-byte native snapshot was exported read-only. Saved and cold
raw SHA-256 values differ, but only three bytes differ, all inside one four-byte
reserved scalar ResourceKey word at byte 32. Exact executable inspection
confirms the property-list format and native 12-byte key setter inside its
16-byte inline value. The actual instance/type/group key, every other metadata
byte and the complete embedded payload are identical. Negative checks reject
actual key and later authored/payload changes. This specific native file's cold
opaque round trip passes; no product normalization rule or native symbol changes
were required. Details and evidence are in
[the owned-snapshot notes](../re/adventure-world.md#owned-snapshots-and-native-reconstruction).

The initial 4,096-byte diagnostic page exceeded the real SDK console's
8,191-character response limit. The corrected helper uses 2,048-byte pages;
its boundary self-tests pass on Windows and WSL, and actual complete export
passes with zero native faults. Both the original truncated attempt and the
successful retry are retained. Receipts:
`out/editor-native-pair-c17-final/{cold-open,cold-bytes-retry}/result.json` and
`out/editor-audit-c17/cold-difference.json`.

### C17 two-player large-editor performance

The exclusive two-process comparison passes on the same 419-placement native
Adventure Town copy, with identical process identities, hardware, cameras and
rendering settings. Shared editing was measured first, then independent native
editing after normal End/Leave, without authored input or native restoration
between windows. Median FPS ratios are 1.00188 and 1.00416; p95 frame-time ratios
are 1.00158 and 1.00201. Both meet the unchanged 0.90 and 1.15 budgets. This is an
FPS pass, not a memory pass. Receipt:
`out/editor-native-pair-c17-final/fps-two/performance-result.json`.

### C17 positive ordinary native Scale cancellation

Ordinary native Ctrl-drag raised a real building before any shared session.
Reducing its scale allowed a further small native elevation. At position
`(-625.684753,264.873322,447.391357)`, enlarging Cheery Backdrop 1 from scale
4.434603 crosses the native validity boundary. The original cancellation counter
advances from two to three, and the complete exposed placement pose returns
exactly to its prior value. No native memory or scenario file was externally
written. Earlier clamp-only attempts remain inconclusive and are not counted.
This establishes an ordinary positive fixture; shared cancellation with a
disjoint peer commit is a separate gate. Receipt:
`out/editor-native-pair-c17-final/scale-bound-181513/result.json`.

Native heap attribution through PID-only Windows Performance Recorder heap
snapshot configuration was unavailable because the OS refused the required
privilege (0xd0000061). The PID-only configuration was disabled again, its final
query confirms disabled, and no recording or global image setting was started.
The active retained-memory gate remains open.

The subsequent shared attempt did not reproduce that pure native Scale path.
Delete correctly required a document-wide lease and could not commit during a
held placement gesture. A guest held Move combined with wheel scaling allowed
an unrelated host Scale to commit, but then tripped the strict opaque native
authoring guard (normalized byte 23022, sizes 60679/60677). The guest requested
an explicit baseline rather than silently accepting that unknown change. No
native cancellation serial advanced, so this is a failed, inconclusive shared
cancellation test and needs further investigation, not rollback acceptance.
The private draft remained recoverable. Both processes returned to Galaxy and
completed normal WM_CLOSE with exit code zero. Receipts:
`out/editor-native-pair-c17-final/shared-cancel-182438/result.json`,
`detach-result.json` and `normal-close-result.json`.

The fresh four-process C17 FPS calculation met the numeric budgets, but two new
profiles still displayed native registration dialogs over their editor view.
Those samples are excluded from final large-editor acceptance. The dialogs were
closed normally, and all four returned to Galaxy. The sampler now rejects a
visible registration prompt before and after recording and saves screenshots
outside the timed interval. Its 20 offline tests pass on Windows and WSL.
The original raw comparison remains at
`out/editor-native-party-c17-final/fps-four/comparison.json`; it is not promoted
to an unobstructed four-editor pass.

### C17 four-player native cancellation with a concurrent disjoint commit

The pure native Scale route now passes with all four participants. An unrelated
host Scale commits first; an invalid guest Scale then advances the owned native
cancellation serial, preserves every exposed authored pose and the full native
canonical check, and changes no collaboration history. All four remain ready
and locally durable. Receipt:
`out/editor-native-party-c17-final/pure-cancel-184814/result.json`.

A separate run holds a pure native Scale open using ordinary Shift/wheel input,
without an outstanding Move or mouse button. While the guest still owns that
native gesture, the host commits a Scale on a different placement. The guest's
authoring hold correctly defers the incoming commit. Normal Shift release and
an unmodified mouse move finalize the invalid placement and invoke the owned
cancellation path. All four then converge on the host's new revision and full
canonical native check. The host edit survives, every other exposed pose is
unchanged, and history contains exactly the peer edit, with no chronological
Undo or rejected-gesture entry. Receipt:
`out/editor-native-party-c17-final/held-wheel-185236/result.json`.

The first held-wheel harness incorrectly waited for all-peer convergence before
releasing the guest hold. Its manual normal release and failed test receipt are
retained separately; it is not the accepted run. The corrected harness waits
only for the host commit during the hold, then checks all peers after release.
The earlier combined Move/wheel case remains a separate unsupported-data guard
observation; the passing pure Scale tests do not waive that guard.

### C17 unobstructed four-player large-editor FPS

The corrected run rejects visible registration before and after sampling and
retains bracketing screenshots. All four native editors display the real
419-placement fixture. Shared and independent measurements use the same PIDs,
CPU/GPU/driver, native camera projections and render configuration; normal
End/Leave separates the windows without authored input or restoration. Median
FPS ratios are 0.99994/0.99978/0.99789/0.99715. The p95 frame-time ratios are
0.99721/1.00123/0.99937/0.99618. All meet the unchanged budgets, and native
cleanup completes normally. This passes the four-player editor FPS gate;
retained memory and large-adventure gameplay remain separate checks. Receipt:
`out/editor-native-party-c17-final/fps-four-uncovered/performance-result.json`.

### C18 correction: unconsumed native creature observations

An exact-C17, read-only observation found a real unbounded mod allocation while
shared editing bypasses the ordinary NPC consumer. Four real Town editors add
10,679/10,325/10,502/10,502 pointer entries in eight idle seconds. One collector
vector in each process had retained capacity for 1,048,576 pointers. This is a
demonstrated source of growth, not attribution of every prior private-byte change.
The native noun-list observation remains separate from the mod vector readings.
Receipt: `out/editor-native-party-c17-final/collector-growth-190626/result.json`.

The correction discards unconsumed and previous-scene observations each
authoring frame while preserving the ordinary gameplay consumer. No native call,
layout or symbol changes are introduced. The actual Win32 Update-wrapper test
passes 2,000 simulated authoring frames and checks that resumed gameplay receives
only current observations. C18 still needs its combined build and actual native
memory/performance rerun; the C17 gate results do not automatically transfer.

C18's combined validation now passes: 22 Win32 suites plus 21 additional
package-admission cases, and repository-wide portable `make test`. Frozen source
inputs remain unchanged. The new DLL is 71,650,816 bytes, SHA-256
`1970184ea9b51bc6a1991fc71a4c661fceb9b8673d407212dbb2885b4bbc75df`.
It is installed only in the private test copy, with a C17 DLL/map backup. All
three native loader maps remain byte-identical. The four prior games completed
normal WM_CLOSE with exit code zero; four fresh C18 processes reached trusted
cold startup (`admission_boot=3`). Receipts:
`out/editor-integrated-c18/validation.json`,
`out/editor-native-candidate18/deploy.json`, and
`out/editor-native-party-c18/editor-startup.json`.

The actual C18 idle-editor reproduction now passes. With 59 real editable
creatures per process, collector capacity stays at 64 entries on every client;
the previous-scene vector remains empty. An additional 10,502-10,710 original
native updates execute per process during the same eight-second interval, so
the fix does not disable creature updates. Input queue sizes stay within one
frame (observed 0-59), instead of C17's growing thousands. Normal cleanup
completes. This proves the specific collector correction in the real game;
the broader retained-memory gate still awaits its new window. Receipt:
`out/editor-native-party-c18/collector-growth-192753/result.json`.
