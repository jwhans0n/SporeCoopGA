# Automatic editor sharing and native controls: preview 3

Development preview `0.7.5.0-ga-editor-preview.3`, protocol **18**, branch
`feat/ga-editor-native-flow`, 2026-10-27. This is a focused integration report,
not completion of the overnight release acceptance matrix. The published
0.7.4.0 gameplay release is unchanged.

## Implemented behavior

- Automatic sharing from the host's ready native Act 1 editor, including a
  never-saved empty draft. Hosting before or after opening the editor is supported.
- Live terrain capture into an owned scenario copy, separate private project
  identity, retained source provenance and checkpoints without a native save.
- Automatic guest following, baseline validation and application hold. An
  unrelated local draft receives Save/Discard/Cancel protection. Native save and
  exit cancellation preserve that draft; joining Cancel leaves the party.
- Reliable editing/loading/play activity, independent of movement replication,
  in the ordinary lobby and discovery. The separate editor page is removed.
- Native Undo/Redo and keyboard history use per-author transactional validation.
  Host Save uses native copy ownership; guest Save exports a pinned local copy
  under a group hold and restores its shared resource identity and dirty state.
- Guest Play requests host approval. Compact prompts show all participants and
  start everyone unready. Native Build returns the party to the held draft.
  Native host Exit preserves party membership; guest Exit leaves the party.
- Bounded, authenticated editor commands scoped to project, epoch, revision,
  operation serial and participant generation. Existing GA/captain bundles are
  unchanged. Mapping version2 carries bounded live name/description/tags and
  genre; mapping version1 and previous recovery manifests remain readable.

## Deterministic evidence

These Win32 fixtures use controlled native doubles where indicated. They do not
prove native game behavior by themselves. All binaries were rebuilt before use.

| Check | Result |
| --- | --- |
| Focused automatic/native-flow runtime | 346 assertions passed |
| Runtime quick regression | 6,205 assertions passed; intentionally excludes the large closure sweep |
| Native Save/command adapter | 346 assertions passed |
| Native cancellation adapter | 1,683 assertions passed |
| Project resources, metadata, cache and recovery encoding | 5,167 assertions passed |
| Document validation, including empty native draft | 2,898 assertions passed |
| Editor wire codec and command direction/bounds | 8,843 assertions passed |
| Editor controller with real UDP and fake native adapters | Final run: 15,446 assertions passed, including loss, duplication and reordering; traffic-dependent check counts vary |
| Transport generation | 63 assertions passed |
| Lobby page routing | Passed |
| Exact-image Save/native command ABI | 17 symbols, 19 call edges, 30 layout/branch receipts verified |
| Native symbol lint and whitespace | Passed |

Runtime cases cover first copy/subsequent host Save, guest export isolation and
cancellation, wrong export completion identity, automatic start/join, stale scope,
handoff cancellation, asynchronous native Exit, host party preservation and guest
party leave. Blocked Undo changes nothing; existing conflict and lifecycle
regressions remain in the quick suite. The approval reply is allowed after the
host transitions into preparation and is matched to the guest's pending request.

## Actual hidden, muted game checks

Only the exact GOG executable is supported:
`dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`.
Native runs use the private marked lab installation and isolated profiles,
instances 73-76. Ownership verifies PIDs, executable/DLL hashes, hidden windows
and muting before input. All mutations use ordinary native mouse/keyboard input
and public party controls. No active native creation database is rewritten.

- C4 `out/editor-native-flow/probe-c4/result.json`: two players automatically
  opened the same never-saved empty Alpine Valley draft, rendered matching
  terrain, retained zero native-save coverage and showed Adventure Editor in
  the normal lobby. Native host Exit returned both to the menu with their party
  connected. All four cohort processes later closed normally with exit code zero.
- C5 `out/editor-native-flow/toolbar-c5-r1/result.json`: unsaved authored draft,
  native Move, toolbar Undo, keyboard Redo, host Save and exact native applied
  acknowledgements passed. Guest Save created a distinct native local copy
  without changing canonical state or host save coverage. All four clients
  joined automatically; guest Play produced host approval, then four explicit
  Ready prompts. No game entered play before the host confirmed. Native Play,
  Build return and group Exit passed. Host and guest local saved copies reopened
  through the ordinary native browser with matching authored semantics.
- The readiness prompt was visually inspected at 1024x768 with all four names
  and waiting states readable. It uses native styling and leaves the full party
  dialog closed.

Further final-candidate and draft-protection receipts are recorded with delivery
below. An interrupted fixture or a navigation/input calibration failure is not
silently counted as a native workflow pass.

C5 draft protection is recorded in `out/editor-native-flow/handoff-c5-r6`
(initial Cancel) and `handoff-c5-r8` (resumed native confirmation through save and
reopen). The modified unrelated draft survives native Discard cancellation;
confirming Discard follows the host. Guest native Exit cancellation preserves the
shared draft. Guest Save + Exit saves a copy and leaves the party. Save-before-join
completes its native save first, then follows the host; the protected copy reopens
with its authored state. Host native Exit cancellation and Save + Exit pass, with
the party retained after the latter. The resumed receipt does not claim its
earlier fixture navigation attempts passed.

## Findings corrected during implementation

- The native document validator originally required at least one cast class.
  Empty new adventures have acts and zero classes; that valid case is now allowed.
- Serializing the live scenario alone omits terrain. The verified native
  CaptureTerrain exporter operates on an owned copy without saving a creation.
- The GOG loader rejects private roots outside its configured creation group.
  C2's isolated guest crashed after an arbitrary group was used. Private roots
  now use collision-checked random instances in the verified native group;
  content/act validation runs before entering the editor.
- Native Exit queues an application change. Its last Build frame previously
  triggered automatic sharing again. A retiring-generation guard prevents it.
- C4 guest export failed native CanSave reason4: the unsaved name was stored in
  edit data outside scenario serialization. Live metadata now travels separately.
  Explicit copy saving also handles the native unchanged-metadata gate without
  permitting invalid captains or unnamed drafts to save.

ABI details and negative receipts are in the
[native save notes](../re/adventure-editor-save.md).

## Scope and remaining acceptance

Native placement editing remains restricted to Act 1 and templates already in
the draft. Class properties/import, synchronized act authoring, objectives,
conversations and global authoring controls remain held pending their native
transaction adapters. Terrain authoring, nested creation editors, independent
acts, history browsing/selective Undo and concurrent playtest/editing remain
later goals. Sharing an empty draft does not make it eligible for native Save
or Play; Spore still requires a name and valid authored captain as applicable.

Broad adventure/opaque-data round trips, sustained loss/adversity, exclusive
large-editor FPS, saturated memory and overnight cycle coverage were not repeated.
Use the [testing playbook](../EDITOR-TESTING-PLAYBOOK.md) before a public release.

## Delivery

Previous candidate C6 DLL SHA-256:
`5d0c5e0d1a358815b329e305848b9b08793e30847296da711bd651005163a702`.
Its final two-player empty-draft check is
`out/editor-native-flow/final-empty/result.json`. The versioned normal-input
driver is [coop_editor_native_flow.py](../../tools/tests/coop_editor_native_flow.py).
`out/editor-native-flow/final-toolbar/result.json` repeats the native toolbar,
copy isolation, four-player join/approval/readiness, Play/Build/Exit and both
native-copy reopen checks on this exact candidate. All pass.

C7 adds one-shot compact choices and a new handoff prompt identity after native
cancellation, so delayed input from the previous prompt cannot restart Discard.
An owned native Exit confirmation is also released when its sharing scope ends.
The current candidate DLL is
`7113d23c5717b94acdb5434d5c2315ca75da9e7f679cca815478ce66d7782805`.
These changes add targeted cancellation/identity assertions to the focused
fixtures. C6 returned all four processes normally to Galaxy and closed with
exit code zero before C7 deployment.

C7 `out/editor-native-flow/c7-toolbar/result.json` passes the four-player native
toolbar workflow, explicit approval/readiness and saved-file reopen checks.
`c7-handoff/result.json` records the unchanged existing creation's first Save
creating a distinct local identity and initial joining Cancel preservation.
Its immediate native Cancel input was ignored while Spore's dialog animated.
The focused continuation `handoff-c7-settled/result.json` waits for the actual
dialog, then passes Discard cancellation, Discard/follow, guest and host native
Exit cancellation, both Save + Exit behaviors, Save-before-join and protected
file reopen. This continuation passed; the earlier complete harness invocation
is still recorded as failed. The versioned driver now settles native dialogs
and requires their closure before accepting Cancel.

The local installation receipt records the final DLL, supporting files, backup
and read-back SHA-256 values. Profiles, creations, settings and saved captains
are preserved; the Two Players launcher keeps `coop_player1` and `coop_player2`.
The setup and matching pack are packaged separately from the published gameplay
release. Installation occurs only after personal sessions have ended.

Installed and read back successfully on 2026-10-27 at 19:18 UTC. Four changed
files were backed up under
`out/personal-editor-install/20261027T191825Z/installation.json`: coop.dll,
the native Save symbol map, installed version record and Two Players launcher
instructions. The launcher still starts the same two profiles. Preserved settings
and individual launcher hashes were verified. All four C7 lab processes returned
to Galaxy and closed normally with exit code zero; no personal session was active
when installation began.
