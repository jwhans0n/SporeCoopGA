# Shared Adventure Editor development guide

The editor implementation is under validation. It is separate from the published
0.7.4.0 GA gameplay release. Do not treat passing portable tests as acceptance of
the native editor or install this development build over an active game.

The preview supports shared placement and native cast editing, with each player
keeping their own camera and selection. The host orders changes to one draft.
Players can browse and edit different acts independently. Advanced history and
playing while others continue editing remain later milestones in the
[implementation plan](MULTIPLAYER-ADVENTURE-EDITOR-PLAN.md).

## Compatibility and scope

The installed C49 testing build uses `0.7.5.0-ga-editor-preview.9`, protocol 23.
The subsequent upstream 0.7.3.6 integration source uses protocol 24 and has not
been installed. Its adventure/editor message IDs are 61/62, preserving upstream
nest/parts IDs 52/53. Protocol 23 and 24 builds explicitly reject each other.
Every participant needs the same build, Galactic Adventures
and the exact GOG executable with SHA-256
`dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`.
Older gameplay and editor previews receive an explicit version mismatch.

The preview also accepts the exact Large Address Aware variant,
SHA-256 `47f60c4db38fda1f15c5a3525617e171b0a98d728d99d645b035aa794a9199ab`.
This changes one PE header bit, with no game code, data, wire or save changes.
It permits up to 4 GB of address space per instance on 64-bit Windows. The
updated co-op DLL is required: the original preview 8 DLL rejects this variant.
`tools/large_address_aware.py <SporeApp.exe>` inspects an image; `--apply` creates
a verified original backup before patching, and `--restore` restores it. Close
all game sessions first. Unknown executable images and conflicting backups are
refused. This raises the address-space ceiling, without guaranteeing every
large adventure or native allocation will fit.

This preview retains first-press dragging, sound pose companions,
independent teleporter endpoints, the native multi-axis ball, cast properties
and cast imports. See the [authoring progress](EDITOR-SEMANTICS-PROGRESS.md)
and the [native-flow validation report](reports/EDITOR-NATIVE-FLOW-VALIDATION.md).
Preview 9 adds crew transactions, ordered terrain changes, optional extended
complexity and independent native asset-editor sessions. These additions are
development capabilities: their two/four-player native acceptance is pending in
the [preview 9 report](reports/EDITOR-PREVIEW9-VALIDATION.md).

Cast/placement allocation is separate from individual asset ownership.
Property changes hold their asset, while other players can edit,
import and remove unrelated assets. Undo refreshes the ownership snapshot before
another gesture. Native scene refreshes retain each participant's open inspector
when that asset still exists.

Collaborators have distinct selection colors and labels. A lock marks every
placed instance of the owned asset; the lobby and viewport also identify asset
property ownership. Placement and movement previews update the native visible
models while dragging, including teleporter exits. New placements use temporary
scene objects. These previews do not add cast entries, authored markers or Undo
items. The completed gesture commits once; cancellation, expiry, ownership loss
and disconnect retire its preview. Fullscreen native pickers hide world hints.

Teleporter previews own detached native marker/act data and both linked scene
objects, preventing observer crashes while another player places one. In Test
Together and ordinary co-op, the host applies native eligible NPC contacts and
replicates each teleport immediately. Captain contacts remain independent; an
NPC entering a teleporter does not move the other players or their followers.

The placement adapter implements creation from the native cast browser,
duplication, deletion, movement, rotation and uniform scale. Scale tracks both
physical size and the native continuous-scale companion; two-player native
Scale/Undo/Redo, including the physical clamp, now pass. All four authors also
pass native rotation, Scale, scaled Clone and Delete with Undo/Redo. The full
preview remains under validation. Older recovery projects without the typed companion
retain their schema and hold Scale until a new native copy is shared.
The adapter preserves opaque
scenario fields through owned native snapshots. Retained objective and behavior
references are checked before removal. The captain has its own shared ownership
scope and can be moved, replaced or locked by either role.

The native cast inspector supports behaviors, respawn, visibility/disguise,
speech and captions. Class settings update every affected instance and also
work before any instance is placed. Either role can add stock assets or existing
creations through the native picker; guest content is validated by the host
before its canonical class is allocated. Conflicting dependency bytes under
the same resource key are rejected. Import failure restores the committed cast.

Act names, descriptions, timers and goals use the native controls with independent
ownership for each act. Switching acts changes only your view. Placement poses
remain adventure-wide, matching Spore's native authoring model. An asset's shared
inspector lease covers its class, so simultaneous edits to that asset remain
serialized even when the players browse different acts.

Use the native timeline **+** and trash controls to add and delete acts. The
Co-op lobby also has **Move act left / Move act right** for the current act.
Roster changes briefly require the shared authoring hold and preserve stable act
identities, behavior columns and patrol paths. Other players keep viewing their
current act; deleting that act selects a surviving neighbor. Spore supports one
to eight acts. Unimplemented adventure-wide settings and auxiliary manipulators
remain blocked.
Existing checkpoints are upgraded from verified owned native snapshots without
replacing the prior durable checkpoint early.

## Crew, terrain and asset creation

Crew controls use a separate adventure-wide ownership scope. Either role can
change the roster and native capacity without reserving unrelated act or cast
properties. Crew, captain and act settings retain their distinct native meanings.

Terraforming is represented by an ordered recipe with stable record identities.
The host orders concurrent new strokes; appending a stroke does not take a
planet-wide lock. Changing an existing stamp checks its ownership and revision.
Each completed stroke has one transaction and one checked Undo item. Planet
controls use their affected field scopes. Native terrain rebuilds must settle
before a participant reports its visible scene ready; placement grounding is
preserved as part of that application. Native coverage of every terraform tool,
cross-act editing and Save/reopen is still pending.
Quick planet/flora values are retained while ownership is requested, with copied
gradients and current native controls resolved again on replay. Escape, focus
loss, stale fields and replaced contexts cancel pending input. Actual native
coverage of these quick-control paths remains pending.

The shared editor's **New asset** menu offers Creature, Building, Vehicle and
Spaceship. Building opens a choice of City Hall, House, Entertainment or Factory.
Vehicle opens a choice of military, economic or cultural land, water or air
vehicles. These choices select the native editor's matching parts palette.
**Edit asset** targets the displayed cast class or selected placement and uses
that saved asset's native subtype. A participant can enter its asset editor while
others remain in the adventure or enter their own editors. Local Done/Cancel
returns that participant independently, including while the host is away.
This is the candidate lifecycle; actual entry, save/bake and independent return
need the native checks listed in the preview 9 report.

Saving an asset produces immutable content for validation and transfer. Replacing
a class's source asset uses its own Content lease and preserves current cast
properties, behaviors and acts. Failed distribution leaves the committed class
intact and retains the local saved creation for retry. Once replacement commits,
another player can acquire that class's Content lease and edit the asset.
Creating or editing an asset does not publish it to EA.

The Co-op lobby's **Complexity: Native / Extended** control is a shared project
option. Extended mode supports the candidate 4,096-placement bound; the native
1,024-class bound remains. It scales the existing native complexity ratio by
one quarter, preserving its weights. It changes checked in-process authoring bounds and
does not patch saved adventures or install a third-party complexity mod.
Publication eligibility and compatibility with other code patches are not
established; conflicting native patches are rejected. Extra address space does
not remove native allocation or performance limits.

## Start and join

1. The host opens an adventure in the native Adventure Editor and hosts through
   **Co-op**. Hosting first and then opening the editor also works. Any supported
   act can be selected before sharing.
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
reports why it was rejected. An initial drag waits for ownership and continues
with the same press and release, including a quick drag released before the
grant arrives. Escape, focus loss, denial and replacement context cancel held
input. Leases expire or are released after abandoned interaction.

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

Canonical acceptance, local durable storage and visible scene readiness are
tracked separately. Owned serialization and journal writes can run in a worker;
native editor calls stay outside the render frame. A candidate transaction is
not accepted until its durability result is known. Creation-editor participants
can keep a newer durable draft while their adventure scene is detached, then
materialize it on return. Solo/group testing waits for every required scene.

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

## Solo Play and Test Together

Native **Play** opens a compact **Test Solo / Test Together** menu.
**Test Solo** starts testing for its initiating player after the host
settles edits and obtains the shared hold. The other participants stay in their
editors with authoring paused. Solo testing does not enable party gameplay
replication or require group readiness. The tester's native **Build** or results
return restores the draft and releases the hold.

Choose **Test Together** from that menu or the normal Co-op lobby to test as a party. Guest
activation requests host approval. After approval, everyone, including the
host, explicitly confirms the same committed revision. Each new attempt starts
everyone unready. Authoring remains paused throughout either testing mode.
The native editor's authored test captain is used; normal native test eligibility
still applies. Group testing starts in the host's selected act at the confirmed
revision. Native act transitions apply the authored behaviors and goals before
the host begins authoritative gameplay.

For Test Together, the host returns the party through native **Build**, lobby return or
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

The [commit-performance harness](../tools/tests/coop_editor_commit_performance.py)
adds release-to-capture, validation, inverse, durability, confirmation and scene
readiness stages plus frame p95 and maximum values for real native gestures.
It defaults to 30 gestures per author on two/four-player fixtures. Its self-tests
pass; no new native latency or frame-budget acceptance is claimed. The shaped
2 Mbit/s profile alone does not establish concurrent bulk-transfer performance.

## Validation

The [preview 9 report](reports/EDITOR-PREVIEW9-VALIDATION.md) records current
checks and pending native/lab gates. The historical
[validation report](reports/EDITOR-IMPLEMENTATION-VALIDATION.md) separates
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
