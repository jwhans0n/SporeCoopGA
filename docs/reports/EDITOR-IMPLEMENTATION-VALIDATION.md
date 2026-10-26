# Shared Adventure Editor: implementation validation

Report revision 11, C11 native validation and ownership hardening, 2026-10-26. Branch:
`feat/ga-adventure-editor`. The portable core and transport checkpoint is local
commit `216f8b0`; native integration is recorded at `0a35c7b`, with lab tools at
`a9b844f`. The C10 integration checkpoint is `071f6a4`, depending on network
checkpoint `7f7c3da`; measurement tooling is recorded at `b65ba3b`. Later
calibration changes require their own working-tree/run evidence.

**Implementation is under validation and is not release-ready.** Portable tests
and several real two/four-player workflows pass. Remaining native transform,
history, lifecycle, network and performance acceptance gates below prevent a
complete stages 0-4 claim.

This development branch uses protocol 16. The published
[0.7.4.0 gameplay release](../validation-ga-0.7.4.0.md), protocol 15, remains
unchanged. See the [development guide](../MULTIPLAYER-ADVENTURE-EDITOR.md) for the
current restricted controls and the [plan](../MULTIPLAYER-ADVENTURE-EDITOR-PLAN.md)
for the intended acceptance criteria.

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

- Complete actual rotation and uniform scale on all advertised peers. The C10b
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
- Extend the C9 cold host recovery receipt below to guest imported recovery
  with mandatory new native Save As, native reopen and adverse save/recovery
  boundaries. The earlier graceful in-process receipt alone does not close it.
- Measure actual command latency, large-editor frame time and retained memory
  against matched process-count/hardware/window/content baselines. Actual native
  WAN latency and large-editor FPS remain unmeasured. The 20-cycle memory
  samples below do not establish a steady-state bound.
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
