# Shared cast properties: development checkpoint

Branch: `feat/ga-editor-semantics`, based on placement-preview commit `b2dfa34`.
Date: 2026-10-26. This checkpoint is not a user-facing release.

The collaboration core now models each class/act settings object separately from
placed instances. Typed edits use class ownership, canonical transactions,
durability callbacks, checked chronological Undo/Redo and existing replication.
Class/act ownership also conflicts correctly when the class has no placement in
that act. Required references, values, revisions, identities and memory bounds
are validated before commit. Failed persistence rolls back the candidate.

Checkpoints containing these settings use an explicit versioned wrapper. An
impossible legacy epoch distinguishes the wrapper without reserving valid old
project UUIDs. Existing placement-only encodings stay byte-identical and readable.
Malformed or truncated new states leave the destination unchanged. Development
protocol 17 rejects older peers at session negotiation; resource bundles retain
their existing formats.

| Check | Observed result |
| --- | --- |
| Win32 document/history | 2,894 assertions, zero failures |
| Portable document ASan/UBSan | 2,894 assertions, zero failures |
| Win32 protocol/checkpoint codec | 8,245 assertions, zero failures |
| Win32 native schema/scale policy | 186 assertions, zero failures |
| Focused real UDP with two/four peers | 307 assertions, zero failures, controlled native adapters |
| Complete editor transport regression | 15,427 assertions, zero failures, controlled native adapters |
| UI routing policy | Pass; native Win32 UI translation unit also compiles |
| Exact installed executable property receipts | 23 static checks pass after the existing editor ABI checks |
| Knowledge lint and changed documentation links | Pass |

The transport run exercised source loss, duplicates and reordering. Those are
controller tests with fake native adapters, not actual game-process property
editing, a WAN latency budget or a performance result. The focused receipt,
targeted input hashes, executable hashes and command outputs are in
`out/editor-semantics/validation.json`.

The F9 refinement selects the editor panel when sharing is available, an offer
exists or collaboration is active. A normal Galaxy party still opens its lobby;
preparation retains page priority. Live native UI acceptance remains pending.

Native class controls remain disabled. The existing adapter rejects class-setting
documents until native projection, mutation, ownership and refresh are integrated.
The property-layout investigation establishes that native slider completion can
also modify authored objective flags. Such side effects must be validated as part
of the transaction or rejected with full restoration; they must not be omitted
from byte comparisons or treated as a read-only inspector refresh.

The installed C19 placement preview still has SHA-256
`f63796d23c67b1d44aa30cbd45ee0df1fd8a3b73ccd9804928dd85e969da192f`.
This checkpoint did not modify its files, profiles or running sessions. No new
game DLL was produced or installed, and no long release stress run was started.
See [authoring progress](../EDITOR-SEMANTICS-PROGRESS.md) for remaining work and
the explicitly deferred features.
