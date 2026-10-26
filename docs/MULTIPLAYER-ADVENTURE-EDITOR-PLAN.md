# Live multiplayer Adventure Editor: implementation plan

**Status: proposed, not implemented.** This plan follows the published
[v0.7.4.0 release](https://github.com/jwhans0n/SporeCoopGA/releases/tag/v0.7.4.0).
That release supports co-op adventure play, not shared adventure editing. This
document changes no game code, installed files, or released protocol 15.

The goal is for a party to create and edit one adventure together in native Spore
editor windows, playtest a committed draft together, and return to that draft.
Native feasibility work must precede implementation promises. All newly proposed
native editing, history, Save As, and editor-transition APIs remain unverified.

## Scope and defaults

| Decision | Proposed default |
| --- | --- |
| Party and compatibility | Host plus one to three guests; matching mod versions and supported GOG 2024 executable `dc04aee5` only. |
| Authority | Host orders and validates semantic editing transactions. No multi-master merge or host migration. |
| View | Each player owns their camera, selection, inspector, and browsing previews. Investigate independent act editing early; use a host-selected shared act if it needs a later milestone. |
| Concurrent editing | Renewable locks on affected placements, shared classes/templates, acts, or document settings. |
| History | Automatic gesture transactions and ordinary own-action Undo/Redo in v1; shared activity/history UI and selective operations are later work. No manual commits required. |
| Saving | Host creates a new native local draft with Save As. Original adventure remains intact. Guests persist verified recovery checkpoints/journals and report their durable revision. |
| Playtest | Party confirms an immutable committed snapshot; editing stops until everyone returns. |
| Initial exclusions | Terrain sculpting, nested creation editors, automatic EA publication, mixed builds, and editing while others playtest. |
| Later editors | Reuse the collaboration core through separate verified native editor adapters. |

The first usable placement preview completes stages 0-4, including four-player
hardening for every advertised feature. It covers opening/joining a project, presence, placement
creation/duplication/deletion and transforms, supported property editing, checked
Undo, saving, recovery, and party playtest. Stage 5 adds acts, objectives,
conversations, and global settings as their native transactions become verified.
Terrain remains a separate expansion. A restricted placement preview must say
which authoring controls remain unavailable; it must not be presented as complete
Adventure Editor support.
The host enters through normal native creation/opening UI, starts a new adventure
or a private draft copy of an existing one, then invites collaborators.

## Evidence and limits

| Existing foundation | Evidence and boundary |
| --- | --- |
| Four-player host networking | [Session API](../mods/coop/net/session.h), [protocol](../mods/coop/net/protocol.h), and [connection](../mods/coop/net/connection.h) provide reliable ordered delivery and unreliable messages. They do not provide editor transactions or durable history. |
| Resource transfer | [Bundle validation](../mods/coop/net/adventure_assets.h) supplies canonical manifests, SHA-256 verification, caching, and bounded transfers. Current limits are 128 MiB/bundle, 16 MiB/record, and 2,048 records. |
| Native scenario serialization | [Adventure world RE](re/adventure-world.md#content-extraction-and-registration) records owned private resources, native Read/Write, acts, shared classes, and placements. This is not proof of arbitrary live editor mutation or lossless editor round trips. |
| Runtime actor handling | [Lifecycle implementation](../mods/coop/adventure_world_lifecycle.cpp) verifies gameplay placement lookup. Native marker pointers depend on vector positions and cannot survive arbitrary editor reorder/delete/Undo. |
| Existing editor test | [Guest editor fixture](../tools/tests/coop_guest_editor.py) opens separate Creature Editors and saves each player's own creature. It does not coauthor one document. |
| Lifecycle distinctions | [Adventure entry RE](re/adventure.md#native-entry) and [build RE](re/build-dc04.md) distinguish quickplay from editor return. Native ScenarioMode return context `+D0=0` returns to editor; `1` returns to Galaxy; `2` returns to Space. |
| Native-call discipline | [Repository rules](../CLAUDE.md) require verified signatures, named symbols, owned resources, and UI/save work outside the render frame. |

The [generated symbol catalog](../data/symbols/march2017_dc04aee5.sym) contains
`GetEditModeActIndex`, `cScenarioEditHistory_GetLastEntry`, editor history/Undo, and
terraform-history candidates. These are investigation leads, not approved callable
Adventure Editor interfaces. [Creature save observations](re/creations.md) describe
a different editor and must not be treated as an Adventure Save ABI.

## Native feasibility gate

Before collaborative mutations, document the installed executable's actual editor
submodes and audit the following with isolated profiles:

1. Capture native gesture boundaries, affected objects, validation, dirty state,
   and history entries for create, clone, move, rotate, scale, properties, and delete.
2. Apply those operations through verified native transactions, with correct
   selection/inspector refresh, references, and one coherent history item.
3. Prove rollback or safe restoration after partial application. Exercise deletion
   of an object selected or inspected by another player.
4. Verify native Undo/Redo, local Save As, asynchronous save completion, close/reopen,
   and metadata ownership. Preserve original author/source metadata honestly.
5. Determine whether each process can browse and edit a different act without
   changing another player's context or misapplying properties to another act.
6. Verify editor-to-playtest-to-editor transitions preserve the unsaved document and
   history, including cancellation, results, disconnects, and native modal dialogs.

Adventure editing and play can share ScenarioMode while changing native submodes.
Fence callbacks using document/session generations and verified submode changes,
not only generic game-mode notifications. Isolate editor sessions from existing
captain puppets, player-state replication, gameplay actor hooks, and goal authority.

Every newly used native function needs signature/call-site verification, symbol
overrides, RE notes, and runtime evidence. If independent act browsing is unsafe,
an explicitly restricted preview may use a host-selected act with a synchronization
barrier; independent cameras remain required. Independent act editing then gets a
dedicated follow-up gate rather than blocking the initial shared-act workflow.
Unsupported native mutations must be disabled or read-only, including toolbar
commands, context menus, and hotkeys.

## Document identity and transactions

Add a game-independent collaboration coordinator, a native Adventure Editor adapter,
a durable project store, and a small native collaboration panel. Keep authoring
messages separate from gameplay's run-scoped adventure messages. Assign a future
protocol version when implementation introduces incompatible messages; protocol 15
remains unchanged by this plan.

Each project has a persistent UUID. Each hosted session has a new epoch; each
committed transaction advances a document revision. Assign stable IDs and
generations to placements, classes/templates, acts, and goals. Maintain explicit
native mappings through clone, reorder, deletion, Undo, and reopen. Never transmit
native pointers or reuse gameplay runtime/incarnation identity as project identity.

A request carries sender/operation ID, epoch, typed operation, affected IDs,
expected field revisions, lease token, and bounded payload. Validate permissions,
types, sizes, finite transforms, references, asset readiness, and authored limits.
Deduplicate operation IDs and return the original decision for retries.
Treat captain/avatar rules and objective references as document invariants:
clone/delete/Undo must repair verified dependencies or reject the operation, never
orphan a goal or duplicate a unique captain placement.

The host orders accepted operations and broadcasts canonical results, including
allocated IDs and normalized values. Disjoint fields can accept an older global
revision when their expected field revisions still match. Rebase only explicitly
safe operations; otherwise reject with a reason and current values. Never silently
overwrite a conflicting edit or reinterpret stale coordinates against changed data.

Lock before enabling a native mutation. Coalesce unreliable drag previews; one
native gesture, such as a drag, deletion, or completed property edit, automatically
becomes one transaction. Manual commits or history management are not required.
Previews never alter durable state.
Use a scoped remote-application guard to prevent callbacks from echoing committed
operations back onto the network. Native canonicalization must converge rather
than execute twice for the originating player.

Apply all affected native fields atomically. A host failure before commit restores
verified before state and rejects the transaction; if rollback cannot be proven,
freeze shared editing and restore a known checkpoint. A guest application failure
freezes and resynchronizes that guest, without undoing already durable host commits.
Unexpected uncaptured mutations trigger the corresponding recovery and diagnostics.

## Locks, presence, and history

Placement locks protect transforms and placement-local properties. Shared
class/template changes require a class lock and identify all affected placements.
Act, objective, conversation, and global settings use their own scopes; structural
operations acquire their required scopes together. Terrain starts excluded, then
uses an exclusive verified transaction before finer concurrency is considered.
Shared class/template and global locks still coordinate edits across different acts.

Leases renew while editing and expire on disconnect or abandoned gestures. Fence
every request with its lease generation so delayed commits cannot regain ownership.
Test expiry against native loading and modal stalls before choosing timeouts.
Presence shows player names, selections, editing ownership, pending commits, and
connection state; cameras remain local. Allow independent objects to be edited
without a document-wide lock.

V1 Undo targets the author's latest still-applied original gesture in chronological
order and submits its checked semantic inverse. Redo targets that author's most
recently undone gesture in normal redo order and checks its forward operation.
Unsafe Undo or Redo leaves both the draft and history cursor unchanged, explains
the conflict, and never automatically skips to an older action. Earlier independent
edits retain their eligibility, but v1 does not provide selection of older changes.
Native buttons and shortcuts must follow this policy.

Link Undo/Redo transactions to their original gesture; do not treat them as new
forward gestures. A new own forward gesture clears that author's redo branch while
retaining audit records. Other players' edits can invalidate preconditions but do
not silently clear another author's history. Multi-object gestures stay one item.

Retain bounded metadata: operation ID, author, committed revision, action summary,
affected stable IDs/generations and fields, forward/inverse data or resource
references, dependency/precondition information, and Undo/Redo links. Explain when
retention limits make an action unavailable. These records support recovery and
future history features; they do not promise unlimited Undo.

Later, add a read-only shared activity feed before selective history controls.
Explicitly choosing an older change to undo or reapply creates a new validated
operation against the current draft, preserving unrelated later edits. Changed or
missing dependencies may reject it. V1 excludes selective operations, multi-change
cherry-picking, branching, and merging; advanced history needs its own design gate.

## Saving and recovery

Keep durable checkpoints and an append-only committed command journal separate
from the disposable resource cache. Store project/schema version, IDs/mappings,
checkpoint revision, resource manifest, source metadata, and integrity checks.
Preserve opaque native scenario fields using owned snapshots plus typed patches;
do not reconstruct a document from only the fields currently understood.

The transaction persistence design must survive interruption between intent,
native application, and committed-record flush. Broadcast or acknowledge a commit
only after its host durable record is complete. Recover from the last valid checkpoint plus
committed operations; ignore incomplete tails and verify reconstructed state.
Define checkpoint compaction and disk-full handling before claiming crash recovery.

Receiving or applying a host commit does not prove guest durability. Guests persist
verified checkpoints and journal records separately, expose their last locally
durable revision, and acknowledge that coverage. Host UI distinguishes committed
state from each peer's recoverable state. Recovery on another computer can claim
only its locally durable revision and available verified assets.

Host Save As uses the normal verified native save pipeline, a new resource identity,
and actual completion evidence, including generated metadata/artwork. Report
checkpointed, saving, and natively saved states separately. The mod must not
externally rewrite active EditorSaves/Pollination or loaded save records. Verified
native Save As owns creation of the new local resource; the project journal stays
separate. No automatic online upload or invented
coauthor ownership metadata is included. Guest native-copy export is a later gate.

Pin Save As to an exact committed revision. Initially stop/settle gestures and hold
new commits through asynchronous thumbnail, metadata, and resource saving. A later
optimization may save an immutable owned snapshot only after native verification.
Record the saved revision and new native key only on actual completion; subsequent
edits make the draft dirty again. Failure must not advance the saved revision.

On host loss, freeze edits and retain each peer's last locally durable revision.
Rehosting recovered data creates a new epoch; old commands and leases stay invalid.
No automatic host election or unacknowledged-edit merge is promised.

## Assets, joining, and resource lifetime

Reuse validated resource manifests and supported dependency closure, including
current music resolution. Preserve existing rejection of unsupported custom
dependencies. A new placement references committed resource identities only after
required assets are verified; pending previews can display placeholders.

Late join sends a revision-pinned checkpoint and missing content hashes, then
replays buffered committed operations. Enable editing only after catch-up and
state verification. Overflow or divergence restarts from a newer checkpoint.
Guest disconnect releases leases; remaining editors continue. Rejoin receives a
new peer generation and cannot replay abandoned local gestures.

[Current registration](../mods/coop/adventure_assets.cpp) uses immutable packages
and native database priorities. Keep packages mounted while native references
exist; release outside the frame after editor/playtest resources detach. Do not
register a new package for every drag or overwrite an open package. Prove bounded
snapshot turnover and memory use in the 32-bit process.

Interactive commands and bulk transfers currently share ordered reliable transport.
Add bounded scheduling/backpressure or a separately verified control lane before
large assets can delay locks, commits, or heartbeats. Bound pending commands,
previews, journal catch-up, and asset queues; overload should pause intake visibly.

## Party playtest and return

Stop new gestures, settle transactions, create an immutable revision snapshot, and
ask every participant to confirm content/captain readiness. Launch all participants
against that exact revision with a separate gameplay run identity. Browsing or
editing cannot change the snapshot during playtest.

Audit reuse of the existing content, lobby, and gameplay systems rather than
routing editor preview blindly through Sporepedia quickplay. Existing cancellation
and menu restoration assume Galaxy. Implement a distinct verified editor return
path that restores the draft, shared revision, collaboration history, and each
player's camera/selection/act context. Do not merge gameplay health, destruction,
objectives, or act progression into authored data or award quickplay captain XP.

Results, cancellation, failed loading, and disconnects must recover coherently to
the editor or a recoverable project state. Keep nested Creature/building/vehicle
editors outside the initial scope: existing independent-editor tests record
pause/save-modal hazards and do not establish safe nested shared authoring.

### Later: playtest while editing

Keep the initial everyone-holds group playtest unchanged. A separate later workflow
would pin an immutable revision for testers while authors continue a newer draft;
simulation state never merges into authored data. On return, testers catch up to
the current draft without overwriting intervening edits.

This needs a native feasibility and authority design gate, not just another UI
option. Verify per-participant submodes and preserve every pinned resource version,
including same-key assets whose draft versions change. Prove lookup isolation for
snapshot roots and dependencies so newer draft registrations cannot shadow them;
keeping an old package open is insufficient. A playing host lacks an
active native editor to apply draft operations; an editing host lacks the active
play mode needed for current authoritative AI/objectives. Resolve both document and
gameplay authority explicitly, without assuming host migration, a second simulator,
or existing support. Verify resource lifetimes, cancellation, disconnects, and return.

## Staged delivery and acceptance

Each stage is a separate reviewable implementation with evidence. A failed native
gate narrows the supported feature set explicitly; it does not justify speculative
calls or hidden document divergence.

| Stage | Scope and exit gate |
| --- | --- |
| 0. Native authoring audit | Verified transaction, dirty/history, identity, save, submode, and return recipes; create/edit/delete/Undo/save/reopen round trip in one isolated process. Record unsupported controls and failure recovery. |
| 1. Collaboration core | Portable typed command, revision, lease, deduplication, chronological own-action Undo/Redo, bounded journal metadata, and recovery tests pass under reordered/duplicate input. No native mutation required. |
| 2. Shared placement integration | Two players edit separate objects concurrently; same-object conflicts reject safely; create/clone/delete/transforms converge; independent cameras, guarded callbacks, and native history routing verified. Not yet the usable preview release. |
| 3. Durable authoring | Native Save As/reopen, cold recovery, late join, disconnect/rejoin, asset import, and memory turnover pass. Original source remains unchanged. |
| 4. Shared playtest and placement preview | Readiness, immutable snapshot, failure/success/cancel/disconnect, and editor return preserve draft/history without gameplay contamination or XP awards. All stages 0-4 advertised features pass two- then four-player adversity, recovery, and performance gates before this usable preview release. |
| 5. Adventure semantics | Add verified class/act/goal/conversation/global transactions with dependency-aware locks. Keep the shared-act fallback if independent act editing needs more work. Repeat the complete two-/four-player gates for every added operation before release. |
| Follow-up A. Independent acts | If not resolved by the native audit, verify separate act contexts and simultaneous authoring, with cross-act shared-template/global locks and safe context restoration. |
| Follow-up B. History UI | First expose a read-only shared activity feed. Then design explicit older-change Undo/reapply with current-draft validation, dependency rejection, and preservation of unrelated edits. Advanced branching/merging/cherry-picking is not part of v1. |
| Follow-up C. Concurrent playtest | Design and verify separate participant submodes, document/gameplay authority, pinned resource versions, continued draft editing, and catch-up on return. Keep simulation changes separate. |
| Follow-up D. Expansion | Terrain and nested asset workflows receive separate feasibility gates; additional editors implement their own mutation/history/serialization/lifecycle adapters. |

Follow-ups are separate planning items, not prerequisites for the initial group
playtest preview or claims of implemented native support.

## Validation and performance gates

Use [lab tooling](../tools/windows_lab.py) and
[test guidance](06-testing.md) with hidden, muted, isolated profiles, first two
participants and then four. Do not alter personal profiles or running sessions.

| Area | Required pass criteria |
| --- | --- |
| Convergence | Every peer reaches the same committed semantic state and IDs after concurrent/disjoint/conflicting edits, duplicate delivery, delete/Undo, joins, and reconnects. No uncaptured native edits or feedback loops. |
| History | Blocked Undo/Redo changes neither draft nor history cursor and never skips actions. Earlier independent edits retain valid checks; conflicting dependencies reject safely. Verify original-gesture links, new-own-edit redo clearing, other-author edits, bounded retention, and preservation of unrelated changes. Retained inverse/dependency metadata survives checkpoint/recovery. |
| Round trips | Save, cold reopen, checkpoint recovery, and playtest return preserve supported fields, references, metadata, and opaque scenario data. Compare normalized semantics plus opaque payloads; byte differences require explained native normalization, not blanket omission. |
| Native lifecycle | Actual UI saves complete; no stale pointer/callback use, leaked locks, stuck dialogs, or unintended Galaxy/Space transitions. At least 20 edit/playtest/return cycles preserve draft/history and release superseded resources. |
| Network adversity | Exercise 100 ms RTT with 1% loss, duplicates, and reordering; separately stress 10% loss and disconnects. No accepted operation is lost or applied twice; bounded overflow produces explicit resync, not divergence. |
| Interaction latency | Proposed p95 request-to-confirmation: at most 200 ms on LAN (RTT at most 20 ms), 500 ms at 100 ms RTT/1% loss, and 750 ms during a 2 Mbit/s asset transfer on that WAN profile. Measure actual retransmissions and queue delay. |
| Performance | On identical large-editor fixtures, median FPS declines at most 10% and p95 frame time increases at most 15% against independent native editors with the same process count, hardware, cameras, window/rendering configuration, and fixture counts. After settled resource cleanup, repeated cycles show no monotonic retained-memory growth. Record configuration and samples. |
| Regression | Existing captain, lobby/readiness, resource/music, cancellation, terminal results/Replay, interactions, targeting, and large-adventure FPS tests remain passing. |

Future gates additionally cover independent act contexts with shared-template/global
conflicts, selective history against changed/deleted dependencies, and simultaneous
editing/testing that preserves both the old snapshot and newer draft through asset
replacement, save, disconnect, and return. These do not expand v1 acceptance scope.

Instrument epoch/revision, operation decisions, lease owner, apply suppression,
native transactions, durable flushes, resource references, and per-peer bytes.
Treat the numeric latency/performance budgets as proposed acceptance targets, not
measured capability. Revise a failed budget only with recorded evidence and review.

An editor preview is ready only when its advertised operations meet these gates,
unsupported controls fail safely, and recovery instructions are documented. Broader
editor support remains roadmap work until its own native adapter passes equivalent
verification.
