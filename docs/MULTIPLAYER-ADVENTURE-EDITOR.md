# Shared Adventure Editor development guide

The editor implementation is under validation. It is separate from the published
0.7.4.0 GA gameplay release. Do not treat passing portable tests as acceptance of
the native editor or install this development build over an active game.

The initial scope is shared placement editing, with each player keeping their
own camera and selection. The host orders changes to one draft. Stage 5 adds
adventure semantics; independent acts, advanced history and playing while others
continue editing remain later milestones in the
[implementation plan](MULTIPLAYER-ADVENTURE-EDITOR-PLAN.md).

## Compatibility and scope

Every participant needs `0.7.5.0-ga-editor-preview.3`, protocol 18, Galactic Adventures
and the exact GOG executable with SHA-256
`dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`.
Older gameplay and editor previews receive an explicit version mismatch.

This preview adds automatic party following and native editor controls to the
placement adapter. Its class-property transaction support does not yet enable
the corresponding native controls. See the [authoring progress](EDITOR-SEMANTICS-PROGRESS.md)
and the [native-flow validation report](reports/EDITOR-NATIVE-FLOW-VALIDATION.md).

The placement adapter implements creation from templates already in the draft,
duplication, deletion, movement, rotation and uniform scale. Scale tracks both
physical size and the native continuous-scale companion; two-player native
Scale/Undo/Redo, including the physical clamp, now pass. All four authors also
pass native rotation, Scale, scaled Clone and Delete with Undo/Redo. The full
preview remains under validation. Older recovery projects without the typed companion
retain their schema and hold Scale until a new native copy is shared.
The adapter preserves opaque
scenario fields through owned native snapshots. Existing objective references and
the authored captain are protected. Deleting a placement required by an objective
is rejected conservatively.

Class import and class properties, act changes, objectives, conversations, global
settings, terrain and nested asset editors are read-only while sharing. This
placement preview supports only the first act (Act 1). Select it before starting
sharing; other acts remain available in the ordinary native editor. Nonzero-act
shared baselines and recovery checkpoints are refused without opening or
restoring them. Independent acts and synchronized host act changes remain planned.
Prepare templates and the authored test captain before starting. There
are no additional registered placement properties beyond transforms in this implementation. Unsupported
controls remain disabled while sharing.

## Start and join

1. The host opens an adventure in the native Adventure Editor and hosts through
   **Co-op**. Hosting first and then opening the editor also works. Select Act 1
   before sharing.
2. Guests join the normal party. From Galaxy, they automatically receive the live
   draft and open their own editor after content validation. Editing stays held
   until the baseline has been applied.
3. Start editing with the native placement controls. A completely unsaved, empty
   adventure can be shared. Sharing captures terrain and dependencies into a
   private project checkpoint; it does not perform a native save.

There is no separate editing page. **F9** opens the normal party/chat lobby,
which shows **Adventure Editor**, transfer progress, failures and **Retry editor
connection** when relevant. Closing the lobby leaves editing active.

If a guest has an unrelated draft open, a compact **Save / Discard / Cancel**
prompt protects it. Save completes Spore's native Save As before switching;
Discard uses the native exit confirmation. Cancel leaves the party and preserves
the guest's editor, preventing another immediate automatic offer. Cancelling a
native save or exit keeps the draft open. Native save eligibility still applies
to saving a local creation, including required authored content.

## Edit and history

The host validates placement and field ownership before mutations. Disjoint
placements can change independently. A conflicting gesture changes nothing and
reports why it was rejected. If ownership is still arriving, wait for it and repeat
the gesture. Leases expire or are released after abandoned interaction.

While another player drags an existing placement, a labeled transform hint shows
their proposed position and orientation. These coalesced, short-lived hints do not
move the committed placement or change the saved draft. Only current ownership
can publish a hint; cancellation, replacement ownership and playtest preparation
retire it. Browsing and new-placement previews remain local until the host has
accepted a stable placement identity. Spore groups overlapping cast icons in its
normal editor UI; the author keeps the new placement selected after creation or
duplication, while other players retain their own selections.

One native gesture becomes one transaction automatically. **Undo** targets your
latest still-applied original action. If another edit makes its inverse unsafe,
Undo changes neither the draft nor the history cursor and explains the conflict.
It never silently skips to an older action. **Redo** follows the same validation.
Native history buttons and shortcuts route through this policy.

History is bounded to 128 gestures and 8 MiB of semantic history data. The
private artifact store has a 384 MiB budget, including retained native before/after
snapshots and the immutable dependency closure. A large scenario can exhaust
that budget earlier; the next gesture is rejected safely without deleting retained
history to make room. Retention limits can make older actions
unavailable. Other players' actions do not silently clear your redo history;
your next accepted forward edit clears your own redo branch. No manual commits
are required. Selective older-change Undo, an activity view and branching are not
part of this preview.

## Save and recover

Use Spore's native **Save** button. Host Save holds authoring until the coordinated
native save completes. The first save creates a new host-owned local creation,
preserving an imported or existing source; later saves update that owned copy.
The private shared project identity and original creation provenance are separate.
A durable checkpoint is distinct from a native save; later edits make the saved
revision older than the current draft again.

Guest Save creates a local copy of the committed shared revision through native
Save As. Everyone briefly holds authoring while the copy is written. The guest's
shared working resource identity and dirty state are restored before resuming.
The export does not change shared history, revisions or host save coverage.
Complete or cancel the native confirmation dialog normally.

Each profile stores a private project under its `SporeCoop/editor-projects`
directory, alongside its native save directory. The project has a UUID, verified
checkpoint, committed journal and content-addressed native artifacts. The mod
does not externally rewrite active EditorSaves or Pollination databases.

Immutable dependencies and the initial native root are retained once. Placement
resource messages carry checked copy/literal patches against that root. A joining
player advertises only independently verified records and base bytes retained in
the current process. The host pins a checkpoint and transfers missing content;
an empty cache receives the full envelope. Cached content is checked again during
assembly, and an invalid or incomplete assembly cannot reach native parsing.
Cold processes can fall back to full transfer. Live deltas require the already
verified base. Both base and reconstructed bytes are hashed before native parsing. This editor
envelope does not change the existing GA or captain resource bundle formats.

On host loss, edits freeze. Host a co-op party before selecting **Recover**;
recovery starts from Galaxy or the settled owned editor. It rehosts the last
locally durable checkpoint in a new session epoch. Its revision can be behind the old host's
revision. Recovering an unsaved or imported checkpoint does not require Save As.
The first explicit native Save creates a new local copy. Recovery does not merge
unacknowledged local gestures or elect a host automatically.

Do not remove project artifacts while a project is active. Resource packages can
retain native references beyond a playtest; they cannot be deleted or unmounted
using an arbitrary cache eviction policy.

Native imports have a process-wide admission limit of 32 charged package attempts
and 256 MiB of validated uncompressed package payload, while each bundle keeps
its existing 128 MiB limit. Reusing a successfully owned package does not charge
another import. These are admission limits, not a bound on expanded native heap
or GPU memory. Failed or interrupted native imports retain their charge; unknown
ownership prevents more imports. Save the current draft and fully restart Spore
when the mod requests it. Shared End does not unload packages or reset the limit.
See the [package ownership notes](re/imported-package-admission.md) for scope and
validation limits.

For native round-trip investigation, `coop.editor inspect bytes OFFSET COUNT`
exports at most 4,096 owned serialized bytes per call as hex, together with the
full snapshot's size and SHA-256. It uses the existing read-only native capture
outside the render frame, keeps no extra snapshot, and performs no file writes.
The SDK console truncates responses at 8,191 characters; use 2,048-byte pages
(`coop.editor inspect bytes 0 2048`) to leave room for hex and its header.
An audit must require the same full hash and size on every page and verify the
reassembled bytes before comparing opaque authored data. This is a diagnostic,
not an editor mutation or recovery action.

## Party playtest

The host presses native **Play** after edits settle. Guest Play requests host
approval through a compact prompt. After approval, a compact readiness prompt
shows every participant; everyone, including the host, explicitly confirms the
fixed revision. Host **Cancel playtest** returns the party to editing. Each new
attempt starts everyone unready. Authoring remains held throughout playtest.
The native editor's authored test captain is used; normal native test eligibility
still applies.

Only the host returns the party through native **Build**, preview return or
result exit. Guests wait for that group return. Each native editor
must finish restoring its draft before the
host releases the authoring hold. Gameplay health, destruction, objectives and
act progression never become authored edits, and test play awards no quickplay
captain XP. Cancellation or disconnection must preserve a recoverable draft.

## Exit

Host native **Exit** or **Save + Exit** settles accepted changes, honors Spore's
confirmation/cancellation and returns the party to Galaxy while keeping it
connected. Guest Exit leaves the party and returns that guest to Galaxy. Guest
Save + Exit first creates a local copy. Saved creations and recovery checkpoints
are preserved. Rejoining the party follows the host's current editor again.

## Developer diagnostics

Use the [testing playbook](EDITOR-TESTING-PLAYBOOK.md) to distinguish focused
development checks from integration and opt-in overnight release acceptance.

These commands support controlled development tests. Their output does not
establish native acceptance or release readiness. Use the public console only
with the intended isolated test profile.

| Command | Readout or action |
| --- | --- |
| `coop.editor leases` | Local lease scopes, stable object identities, tokens and remaining milliseconds; participant phases, selections, ownership, pending work and preview lease identity. |
| `coop.editor status` | Includes the latest native saved revision, exact SavedApplied coverage and settlement state. Transport acknowledgements alone do not count as native application. |
| `coop.editor verify-reopen` | After a fully acknowledged native Save and normal End, compare the normally reopened exact native file against the saved authored state. This detached-editor diagnostic performs no recovery or repair. |
| `coop.editor timing reset` | Clear the previous timing samples and start recording. |
| `coop.editor timing stop` | Stop recording and discard any unfinished measurement. |
| `coop.editor timing` | Report the sample count, recording state, capacity and `confirmation_ms` values. |
| `coop.editor stages reset`, `stop`, `read OFFSET COUNT` | Opt-in bounded numeric operation stages, native apply and durable-save spans, paged at most 32 events per read. |
| `coop.editor accounting` | Read-only logical container counts and capacities, including retained project, resource and transport state. These are not allocator or process-memory totals. |

The lease readout uses cached controller data, is bounded to 12 KiB and changes
no ownership, native resources or durable state. It can distinguish waiting for
a grant from an expired lease or a participant still applying a checkpoint.
Tokens identify current capabilities; the diagnostic does not grant them.

Timing retains at most 256 accepted captured native placement gestures. Each
sample starts at `Controller::submit`, after ownership waiting and native inverse
settlement, and ends at that author's accepted canonical decision after native
application and durable checkpointing. Other authors' commits and rejected
operations do not produce samples. This measures confirmation latency after
submission, excluding the time to acquire ownership, perform the gesture or wait
for its native inverse. Start and stop collection at settled boundaries; it is
not an end-to-end mouse-input timer or an FPS measurement.

The scoped Return fence and cached-content Join remain under integration
validation. Isolated Runtime tests retain old return callbacks across rounds and
verify their project, epoch, round and revision cannot affect the new preview.
Those tests substitute native APIs. The C11 four-game normal Return check
preserved each local camera and selected stable placement. This does not cover
every cancellation or disconnect path. Cache-aware transfer still requires actual
cold and warm joins with verified native reconstruction.

The opt-in [native network harness](../tools/tests/coop_editor_network.py) uses
already-owned hidden, muted two- or four-player processes, exact candidate hashes
and ordinary native gestures through a bounded loopback impairment proxy. It
records submission-to-confirmation samples and UDP traffic, including source
retransmissions. Its rate-limited profile shapes each peer and direction to
2 Mbit/s without a concurrent bulk transfer. Its LAN, WAN and packet-adversity stage does not establish bulk
asset-contention performance, the large-editor FPS gate or sustained memory
retention. Those remain separate acceptance tests in the
[validation report](reports/EDITOR-IMPLEMENTATION-VALIDATION.md).

## Validation

The [validation report](reports/EDITOR-IMPLEMENTATION-VALIDATION.md) separates
portable and controlled-native fixtures from actual game-process observations.

Portable core, wire, durable-store, project-envelope and controller tests run
through `make test`. `make test-editor-windows` builds Win32 fixtures for the
native save contract, preview ownership and full runtime orchestration. Those
fixtures substitute native game APIs; they do not establish in-game acceptance.

Native testing uses hidden, muted, isolated Windows profiles. Focused preview
checks cover the changed native workflow. Sustained cycle, network, resource,
FPS and memory acceptance remains in the opt-in overnight playbook. This
development preview is not a completed public release acceptance claim. Broader
editor support remains roadmap work until it has equivalent evidence.

End shared editing and return to Galaxy before using developer hot reload.
After any native package was admitted in that process, imports and package reuse
are refused following a DLL reload because native ownership cannot be proved.
Fully restart Spore before importing again; saved creations and durable project
files are retained. A late first load without the normal cold-start lifecycle
also requires a restart. Unloading during an active native editor gesture is
unsupported.
