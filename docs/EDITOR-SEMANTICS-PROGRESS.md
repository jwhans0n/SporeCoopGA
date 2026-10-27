# Shared adventure authoring implementation

The current locally installed build is the C19 placement preview described in the
[editor guide](MULTIPLAYER-ADVENTURE-EDITOR.md). Subsequent authoring work happens
in a development branch and private lab; it does not silently replace the build
being used for hands-on testing.

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
