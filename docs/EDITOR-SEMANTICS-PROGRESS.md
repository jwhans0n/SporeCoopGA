# Shared adventure authoring implementation

The current locally installed build is the C19 placement preview described in the
[editor guide](MULTIPLAYER-ADVENTURE-EDITOR.md). Subsequent authoring work happens
in a development branch and private lab; it does not silently replace the build
being used for hands-on testing.

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
- F9 page routing that opens the shared editing panel directly when the host can
  share, a guest has an offer, or a shared session is active. Ordinary Galaxy
  parties retain their normal lobby. Native live verification of this UI
  refinement is still pending; it is not installed into the user's running games.

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
