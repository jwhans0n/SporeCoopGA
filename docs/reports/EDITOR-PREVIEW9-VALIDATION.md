# Shared Adventure Editor preview 9 validation

Development candidate: **`0.7.5.0-ga-editor-preview.9`**, protocol **23**,
branch `feat/ga-editor-multi-act`, 2026-11-02. The original supported GOG 2024
executable and its exact 4 GB LAA variant remain supported. Older protocol
clients are explicitly rejected. This report records development evidence,
not completed native acceptance, a main merge or a public release.

## Candidate changes

- Crew roster/capacity has a separate settings identity and ownership scope.
- Host-ordered terrain records have stable identities, checked replacement and
  deletion, and one Undo item per stroke. Concurrent appends do not take a global
  planet lease. Native application preserves terrain-derived placement grounding.
- Asset-editor sessions are local to each participant. The six implemented entry
  families are Creature, Building, Vehicle, Spaceship, Plant and Cell. Saved
  creations use dependency validation and immutable content replacement under a
  separate Content lease, preserving current cast properties.
- Canonical, durable and rendered scene revisions are distinct. A detached
  participant can receive a newer durable draft without claiming its adventure
  scene has rendered it. Shared testing waits for required scene readiness.
- Staged commits keep acceptance behind persistence completion. Owned encoding,
  hashing and durable writes can run outside the editor thread; sparse journals
  avoid storing a full unchanged native base per gesture. Pure placement
  preparation and exact operation-scoped validation reuse retain opaque-change
  checks. No new measured frame or confirmation budget is claimed yet.
- Optional Extended complexity permits the candidate 4,096-placement bound while
  retaining the native 1,024-class limit. Conflicting patch signatures are
  rejected. Publication eligibility and other complexity mods are unverified.

These are implemented candidate paths. They require the native checks below
before being treated as accepted gameplay. Authoring remains paused during
Solo or Test Together. Asset editors do not coauthor the same native creation
simultaneously, and this change does not add automatic publication.

## Completed checks

| Check | Actual result and receipt |
| --- | --- |
| Full portable repository suite | `make test B=out/editor-preview9-portable` exited 0. `out/preview9-portable-tests.log` includes document 2,898, protocol 8,946, authoring extensions 166, terrain codec 84, cast 377, sparse store 2,015, project 5,634, UDP/controller 43,384 and accounting 3,425 checks. |
| Portable tool and symbol checks | Commit-performance parser 10, existing performance parser 20, network proxy 6 and memory-accounting collector 19 tests passed. Creation, lifecycle, Save/reopen and native-audit self-tests passed. RE lint reports 391 used symbols, 3,007 known and zero supported-build problems; the frozen unsupported 922af7c7 warning is informational. |
| Portable boundary failures | The first two attempts exposed old protocol-22 and 1,024-placement test expectations. Commit `c73ec00` preserves explicit protocol-22 rejection and checks the named 4,096-placement bound plus one. Failed logs remain as `out/preview9-portable-tests-attempt1.log` and `attempt2.log`; the final complete run passes. |
| Win32 controlled fixtures | Initial batch `out/preview9-win32-fixtures.json`: 25 of 26 executables passed. This includes native cancellation/terrain/cache 8,016, creation lifecycle 211, complexity bounds 32 and native Save 444 checks. The reopen executable still used the old 1,025-placement failure expectation; its rebuilt replacement passes 150 checks in `out/preview9-reopen-win32.log`. These fixtures substitute native game APIs. A final batch for subsequent source changes remains required. |
| Cross build | `out/preview9-build.log` records the co-op DLL and editor fixture cross build. The final candidate is not frozen by this report; incremental build logs and exact payload hashes must accompany native acceptance. |
| Hidden Windows render probe | `out/preview9-hidden-probe.log` reports a working hidden-window D3D adapter, three successful presentations and unchanged input desktop. It establishes fixture capability only, not game launch or authoring acceptance. |
| Initial private native startup | `out/preview9-two-runner.log` records two private processes loading the exact LAA executable and SporeKit; its loaded-map list omits the separate creation/complexity maps. Inspection of `sporekit/symbols.cpp` found discovery requires the executable header within ten 512-byte `fgets` reads. The separate maps lacked a usable header. Merging their symbols into a correctly headed map still needs a fresh candidate startup check; startup is not feature acceptance. |
| Native signature verification | `out/editor-creation-re/verified-creation.json` verifies six symbols/six callers; `verified-complexity.json` verifies six symbols/twelve callers on the exact LAA executable. Static signatures do not establish live editor or publication behavior. |
| Package self-test | `py -3 tests/editor_preview_package_tests.py` and WSL `python3 tests/editor_preview_package_tests.py` pass 16 tests using temporary synthetic binaries and documents. Manifest, pack version, hashes, archive contents, standalone links, allowlisted executable variants, actual source identity, symbol-header discovery (including CRT CRLF handling) and rejection guards are covered. Receipts: `out/preview9-package-selftest.log` and `out/preview9-package-selftest-wsl.log`. `tools/package_editor_preview.py --help` passes. No real candidate archive was created or installed during this check. |

The package recipe requires source preview 9/protocol 23 and a payload DLL that
identifies preview 9. The editor map must expose its verified executable header
within SporeKit's discovery window, and required creation/complexity symbols must
exist in executable-matched maps. Its manifest keeps
`complete_release_acceptance: false`.
Original personal saves, installations and launchers are not altered by these
packaging self-tests.
Receipts apply to their respective compiled snapshots. Later source changes
require the final affected checks again before a candidate is frozen.

## Required native acceptance

Actual two- and four-player candidate runs remain pending for crew edits,
terrain tools and grounding, concurrent/disjoint strokes, every asset-editor
family, native Done/Cancel/save/bake, replacement and remote reopening, and
independent returns while the host or all other participants remain in editors.
Cover ownership conflicts, stale callbacks, disconnects, retry, Undo/Redo, late
join, recovery and native Save/reopen. Verify Solo and Test Together after edits
and retain existing captain, teleporter, gameplay and result regressions.

Known input limit: a quick planet/flora value change completed before its lease
grant can require retry. Continuous flora drags begin history at the first
accepted value; complete deferred replay of these controls is not implemented.
This is not covered by the retained first-press placement-drag result.

The release-stall report still needs measured real native gestures on the
configured large adventure, 30 gestures per author with two and four players.
`tools/tests/coop_editor_commit_performance.py` records QPC release, capture,
validation, inverse, durability, confirmation and scene readiness stages plus
frame distributions. Proposed limits remain frame-gap p95 at most 100 ms and
maximum at most 250 ms, with confirmation p95 at most 200 ms LAN (RTT at most
20 ms), 500 ms WAN (100 ms RTT/1% loss), and 750 ms during a 2 Mbit/s
asset-transfer contention on that WAN profile. A shaped
2 Mbit/s run without concurrent bulk traffic does not satisfy that last gate.
Large-fixture native FPS and retained-memory comparisons also remain pending.

Extended complexity requires native threshold, Undo/Redo and Save/reopen checks.
No claim is made that an over-limit adventure remains publishable, or that
unreviewed third-party executable patches can coexist with this preview.

## Lab, delivery and remaining limits

Linux lab smoke is unavailable. The read-only receipt
`out/preview9-lab-smoke.log` shows no Wine, wine64, bottles-cli, Flatpak or
base Games bottle in WSL. The runner resolves its installation to `/mnt/d/Code`,
which has no expected SporeApp.exe; lane creation would copy unrelated code
directories before checking runtime prerequisites. That path was not executed.
No lanes, installations or games were changed by this lab preflight. A reviewed
equivalent Windows lab smoke or the supported Linux lab remains required before
merging. The render probe is not a substitute for it.

Native acceptance, final candidate hashes, verified packaging, shortcut/install
hash checks and rollback delivery remain separate pending steps. Sustained and
overnight acceptance is reserved for a release window. Historical preview
results in the [native-flow report](EDITOR-NATIVE-FLOW-VALIDATION.md) retain their
original binaries and limits; they are not relabelled as preview 9 passes.
