# Shared adventure authoring implementation

The automatic native-flow preview is described in the
[editor guide](MULTIPLAYER-ADVENTURE-EDITOR.md) and its
[validation report](reports/EDITOR-NATIVE-FLOW-VALIDATION.md). It extends the
placement preview with native cast properties and separate solo/group testing.

## Current preview 6 checkpoint

Preview 6 / protocol 21 on `feat/ga-editor-multi-act` adds independent act browsing,
per-act names/descriptions/timers/goals, captain ownership and replacement, stable
act creation/deletion/reordering, and a native Play popup for Test Solo or Test
Together. The group run starts in the host's confirmed act; every editor restores
its own view on return. State wrapper v4 and native mapping v3 retain old recovery
readers and keep surviving act identities stable across index changes.

Private four-player native runs verify roster editing with Undo/Redo, independent
metadata edits, captain replacement and movement, and cold recovery. Group tests
in Obnoxious SpacePonies now reach one gameplay run with hostile ponies and finite
health in Act 3. Detailed combat assertions and remaining validation gates belong
to the current matrix in the linked report. No main merge or release acceptance
is implied by these preview results.

## Historical preview 4 checkpoint

Preview 4 / protocol 19 on `fix/ga-editor-preview4` adds owned native class
subtrees, behavior/text reference validation, class creation/removal, dependency
uploads from guests, retained first-press input and endpoint/audio pose fields.
Native two-player checks now cover the cast-property families, direct drags,
sound movement, both teleporter endpoints and multi-axis ball rotation.
Four private native editors have passed cold recovery, sound movement and Undo
from every role, initiating-player solo Play/Build, and Test Together with
reciprocal moving captains. Detailed failures and remaining acceptance gates
are tracked in the validation report; these results are not a release claim.
The final candidate also passed host/guest multi-selection imports with checked
Undo/Redo, native shared conversations/objectives, ordinary Mission Complete/Done
return, and exact-source native Save/reopen of the supported projection. The
Linux/Bottles merge smoke and sustained release-window acceptance remain open.

## Implemented development checkpoint

Branch `feat/ga-editor-semantics` now adds:

- Separate class/act settings identities, typed property schemas, revisions and
  explicit parent class/act references. Settings are not copied into placements.
- Host-ordered property transactions using existing canonical decisions,
  durability callbacks, replication and per-author chronological Undo/Redo.
  Shared cast changes require class ownership, including when the class has
  several acts or no placed instance in an act.
- Reference validation and rollback. Conflicting Undo leaves the draft and
  history cursor unchanged; earlier independent edits remain logically eligible.
- A bounded, versioned checkpoint wrapper. Placement-only checkpoints retain
  their exact old encoding and remain readable. Partial or malformed new
  checkpoints are rejected without modifying the destination state.
- Development protocol 17 and version `0.7.5.0-ga-editor-preview.2`, giving older
  clients an explicit session-version mismatch. Existing GA/captain content
  bundle formats are unchanged. This source version is not a published release.
- The former F9 editor page has been superseded by automatic sharing and native
  controls on `feat/ga-editor-native-flow`, preview 3 / protocol 18. The normal
  party lobby remains available for chat, progress, retry and recovery.

Focused checks: 2,894 Win32 document assertions, 8,245 protocol assertions,
186 native-schema/scale assertions, 307 focused real-UDP/controller assertions
with two/four peers, and a full editor transport run with 15,427 assertions all
pass. Transport native adapters are controlled doubles, not actual game editors.
Portable document ASan/UBSan and the UI routing checks are recorded separately
under `out/editor-semantics`. No long native stress/FPS/memory run was started.

The native property controls remain held. Projection/application, input gating,
owned UI refresh and a short actual two-player property workflow still need to
be integrated before those controls can be advertised as implemented gameplay.
The current adapter rejects class-setting payloads until that integration exists.

## Automatic sharing checkpoint

Preview 3 captures the live draft, including an unsaved empty adventure's terrain,
under a private project identity without saving a creation. The party host starts
sharing when its native editor is ready; guests follow automatically after the
baseline transfer. Reliable activity distinguishes editing from adventure play.
An unrelated guest draft receives Save/Discard/Cancel protection.

Native history controls use the existing transactional policy. Native host Save
creates and then updates a host-owned copy; guest Save exports a pinned local
copy under a coordinated hold and restores its shared working context. Native
Play supports guest approval requests and explicit participant readiness. Native
Build and Exit use the coordinated return/exit lifecycle. Recovery of an unsaved
checkpoint no longer requires an immediate native Save As.

The native-flow report records focused test evidence and remaining limits.
Sustained stress, FPS and memory gates remain in the overnight playbook.

## Active scope

Continue Stage 5 in small native-verified slices:

1. Shared class/cast properties, with class-wide ownership and checked Undo/Redo.
2. Verified asset/template import and its dependency transfer.
3. Host-selected shared act changes with an acknowledgement barrier.
4. Act creation/settings and objective transactions, including stable references.
5. Conversation content and global adventure settings.

Every slice needs semantic state, conflict checks, durable recovery, native
projection/application and a focused two-player check before its UI is enabled.
Existing native controls remain held until the corresponding transaction is
implemented. Initial work extends the collaboration model beyond placement-only
fields; class/act settings must not be falsely represented as independent copies
of placement properties.

## Explicitly later

Terrain, nested creation editors, independent act editing, shared history browsing
and selective Undo, and playtesting while other participants keep editing remain
separate later goals. Shared-act coordination is in scope; independent acts are
not. Preserve these exclusions while enabling each additional supported control.

## Development cadence

Follow the [testing playbook](EDITOR-TESTING-PLAYBOOK.md): focused deterministic
tests and short native reproductions during implementation, integration checks at
coherent checkpoints, and the long matrix only in an opt-in release window.
