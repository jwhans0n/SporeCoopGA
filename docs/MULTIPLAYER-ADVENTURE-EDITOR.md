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

Every participant needs the same editor build and protocol 16, Galactic Adventures
and the exact GOG executable with SHA-256
`dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`.
Older protocol 15 gameplay clients cannot join an editor build's party.

The placement adapter implements creation from templates already in the draft,
duplication, deletion, movement, rotation and uniform scale. It preserves opaque
scenario fields through owned native snapshots. Existing objective references and
the authored captain are protected. Deleting a placement required by an objective
is rejected conservatively.

Class import and class properties, act changes, objectives, conversations, global
settings, terrain and nested asset editors are read-only while sharing. Prepare
templates, the authored test captain and the shared act before starting. There
are no additional registered placement properties beyond transforms in this implementation. Unsupported
controls must remain disabled; they are not safe ways around the shared panel.

## Start and join

1. Connect the party through **Co-op**. The host opens an adventure in the native
   Adventure Editor. Guests remain at Galaxy.
2. Give the draft a new name using the native editor before starting. An unchanged
   existing adventure can fail Spore's native save validation.
3. Open **F9**, then **Adventure Editor**. The host selects **Start sharing**
   and completes the normal native Save As flow, including its confirmation.
   Sharing starts only after the new native resource has been read back.
4. Guests open the same panel and select **Join editing**. Their native
   editors open after verified content arrives. Editing remains held until the
   checkpoint and its bindings have been applied.

The panel shows participants, selections, committed revision, your local
checkpoint and the latest confirmed native save revision. The host also sees
each peer's durable checkpoint revision. Guest panels label other players'
checkpoint coverage as reported to the host rather than claiming an unknown
revision. Closing the panel leaves the session active. Ending shared editing
preserves the party connection and the private draft.

## Edit and history

The host validates placement and field ownership before mutations. Disjoint
placements can change independently. A conflicting gesture changes nothing and
reports why it was rejected. If ownership is still arriving, wait for it and repeat
the gesture. Leases expire or are released after abandoned interaction.

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

**Save draft** is host-only and uses Spore's native save UI. It holds new authoring
transactions until asynchronous native saving completes. A durable checkpoint is
distinct from a native save; later accepted edits make the saved revision older
than the current draft again.

Each profile stores a private project under its `SporeCoop/editor-projects`
directory, alongside its native save directory. The project has a UUID, verified
checkpoint, committed journal and content-addressed native artifacts. The mod
does not externally rewrite active EditorSaves or Pollination databases.

Immutable dependencies and the initial native root are retained once. Placement
resource messages carry checked copy/literal patches against that root. Full
join envelopes include the base; live deltas require the already verified base.
Both base and reconstructed bytes are hashed before native parsing. This editor
envelope does not change the existing GA or captain resource bundle formats.

On host loss, edits freeze. Host a co-op party before selecting **Recover**;
recovery starts from Galaxy or the settled owned editor. It rehosts the last
locally durable checkpoint in a new session epoch. Its revision can be behind the old host's
revision. An imported guest checkpoint must complete a new native Save As before
rehosting, so a remote source cannot become an overwritten local draft. Recovery
does not merge unacknowledged local gestures or elect a host automatically.

Do not remove project artifacts while a project is active. Resource packages can
retain native references beyond a playtest; they cannot be deleted or unmounted
using an arbitrary cache eviction policy.

## Party playtest

The host selects **Playtest** after outstanding edits settle. Every participant,
including the host, explicitly confirms the fixed revision. Authoring stays held
while the party uses that snapshot. The native editor's authored test captain is
used; this is not a new Sporepedia quickplay attempt.

Only the host returns the party through **Return to draft**, native preview
return or result exit. Guests wait for that group return. Each native editor
must finish restoring its draft before the
host releases the authoring hold. Gameplay health, destruction, objectives and
act progression never become authored edits, and test play awards no quickplay
captain XP. Cancellation or disconnection must preserve a recoverable draft.

## Validation

The [validation report](reports/EDITOR-IMPLEMENTATION-VALIDATION.md) separates
portable and controlled-native fixtures from actual game-process observations.

Portable core, wire, durable-store, project-envelope and controller tests run
through `make test`. `make test-editor-windows` builds Win32 fixtures for the
native save contract, preview ownership and full runtime orchestration. Those
fixtures substitute native game APIs; they do not establish in-game acceptance.

Native testing uses hidden, muted, isolated Windows profiles. The required gates
remain two- then four-player operation coverage, save/reopen and recovery,
playtest cancellation and return, at least 20 cycles, network adversity, resource
turnover and measured editor performance. A usable preview requires all
advertised operations to pass these gates. Broader editor support remains roadmap
work until it has equivalent evidence.

End shared editing and return to Galaxy before using developer hot reload.
Unloading the mod during an active native editor gesture is unsupported.
