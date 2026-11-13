# Shared editor preview 13 validation

Branch: `fix/ga-editor-browser-runtime`, based on preview 12 review tip
`44bf687deaa97d22cd47fa2c238c8b0613e7d205`. Candidate source version:
`0.7.8.0-ga-editor-preview.13`, protocol 28. Existing native saves and recovery
formats are unchanged. Main and the editor destination remain unchanged.

## Reports and observed causes

- BUG-0639: the installed preview 12 logs record sustained 2 FPS host and 6 FPS
  guest with 0.5-second hitches and zero native faults. Network service pulses
  repeatedly schedule native cast-card scans in a single rendered frame. A
  queued-task flag alone does not bound work between frames. Schedule browser
  work once per Present frame and skip unchanged decoration setters.
- BUG-0640: native Creature Continue switches `cEditor+1CC` to the root's owned
  `EditorRequest+98` successor without Submit or intermediate NotifyResult.
  Comparing only the root pointer loses the final Outfitter save. Follow the
  verified bounded owned chain, retaining scope/lease and result fences.

Verified field behavior and signatures are in
[the RE notes](../re/creations.md#creations-browser-pulses-outfitter).

## Failing and passing evidence

Private evidence is retained under `out/preview13/`, without publishing player
names, profiles, raw logs or paths. Tests use isolated hidden/muted instances
45/46 with exclusive virtual input and the supported exact 4 GB executable.
No personal save/profile participates.

| Check | Evidence | Result |
| --- | --- | --- |
| Installed evidence copied before work | `installed-evidence/` | Both reported signatures present |
| Idle decoration fixture before/after setter guard | `ui-idle-red.log`, `ui-idle-green.log` | FAIL then PASS |
| Setter guard alone in native games | `ui-native-ab/result.json` | Still about 2 FPS; insufficient |
| Same-frame service pulse fixture | `ui-pulse-red.log`, `ui-pulse-green.log` | FAIL then PASS |
| Native frame-guard A/B, settled samples | `frame-gate-ab/result.json` | About 2 FPS off, 21 FPS on; matching native states |
| Native FPS fixture, switch off | `browser-fps-final-red/result.json` | FAIL below 8 FPS; ready, converged scenes |
| Cross-tab native FPS | `browser-fps-green1/result.json` | PASS, about 21 FPS on both participants |
| Outfitter return before chain fix | `outfitter-return-red/`, `outfitter-red-native.log` | FAIL, exact installed missing-result/recovery signature |
| Owned successor lifecycle fixture | `outfitter-unit-red.log`, `outfitter-unit-final.log` | FAIL then PASS |
| Guest native Creature/Outfitter save | `outfitter-guest-green1/` | PASS, one class and one Undo item, native observers agree |
| Host native Creature/Outfitter save | `outfitter-host-green2/` | PASS, same native assertions |
| Guest cancel after native body save | `outfitter-guest-cancel/` | PASS, cast/history unchanged, editing resumes |

The lifecycle fixture also covers cancellation, expired context/content lease,
unrelated requests, unsupported families/variants/return modes, cyclic links,
posted result lifetime and ownership through the returning phase. The browser
fixture retains hover targeting, property/content ownership and UI cleanup
coverage. No native factory signature or network payload changed.

The first native entry attempt stopped before input because the private lab
launched at 1600x1200; its measured creation recipe requires 1024x768. The later
private launch uses that exact viewport. This was a fixture precondition failure.

## Qualification and delivery

Candidate hashes, repeat checks, repository result and deployment receipt
are recorded below. Hidden-game FPS establishes removal of this
sustained regression; it does not qualify visible-game FPS, large-adventure
commit latency or the previously deferred performance targets. Broad creation
matrices, upstream-wide smoke and release qualification remain deferred. No main
merge, release tag or public release package is part of this fix.

The existing Add Act native identity failure (BUG-0637) and standalone Save New
cold-reopen warning (BUG-0638) remain separate, unresolved issues.

## Final runtime candidate and focused results

Runtime source: `90535e5282b7caea44cbe5bd3fc1d0ac1ff971ca`.
DLL SHA-256:
`694490b9311463319fdc9ed186c476a546976b4ae0e5718437f18d9cbe30642d`.
The later guide/package-identity commit changes no runtime code or payload.

- `qualified-cold-recovery/` restores durable revision 26 and both native scenes
  from the private checkpoint on a fresh process launch.
- `outfitter-qualified-guest/` and `outfitter-qualified-host/` each pass the full
  normal-input Creature, Continue, Outfitter save and independent return flow
  on this exact DLL. Final native observers agree and each save admits one
  immutable class with one history item.
- `outfitter-qualified-cancel/` passes cancellation after the intermediate native
  body save, with unchanged canonical inventory and history.
- `observer-model-proof/` loads both finished canonical creatures through each
  game's cached and private native model paths. All four observations report
  AdventurerCreature `4178B8E8`, with valid objects and no inspection error.
- `return-history-verified.json` confirms the guest's Undo preserves the host's
  newer asset, and Redo restores the guest's resource/behavior and both native
  observers. The first private comparison incorrectly required the original
  content revision after Redo; that failed receipt remains in `return-undo-redo/`.
  Read-only comparison verifies the sole difference is the expected new content
  revision 30. No native input was replayed to hide the failed assertion.
- `qualified-native-save/` passes native Save at revision 30, host 2/2 and guest
  1/1 acknowledgements, unchanged history and matching actual native scenes.
- `browser-fps-qualified1/` and `browser-fps-qualified2/` each pass all three tabs
  at 21.4-21.6 FPS on both games. The second follows creation returns, Undo/Redo
  and native Save. Both require readiness, matching native states, unchanged
  canonical document/history and zero native faults.
- `win32-fixtures.json` records 2,016 creation-lifecycle checks, 1,409 browser UI
  checks, 122,208 runtime checks, 2,102,959 PNG checks, 346 zlib checks and 32 PNG
  carrier checks. All pass on the affected source.
- `final-native-logs/health.json` checks both private processes after the native
  assertions: alive, zero native faults and no new crash/hang report. Only those
  owned private games are retired after the checks.

## Repository checks and installed testing payload

`out/preview13/repository-final.log` and `repository-final-exit.json` record
`make -j4 HOSTCXX=clang++ B=out/preview12/repo-check test` passing with no sanitizer
skip. This includes portable editor/network tests, ASan/UBSan, TSan, decoder
fuzzing, native-build/package boundaries, RE/docs/glyph/ticket audits and tool
self-tests. The job began at `4d0b4116`; its package tests read the preview 13
identity from `0083a944` before that stage ran. Runtime/SDK/data/Makefile content
is identical to the tested native source `90535e52`. Only guide/package identity
and validation records changed afterward. Original tracked shell-file bytes
were restored after the temporary WSL newline normalization.

Installed after verifying both personal games were closed. The deployed DLL
matches the exact qualified SHA-256 above; all 16 payload hashes match the
manifest. Only `coop.dll` and `sporecoop.version` needed replacement. The existing
SporeCoop Two Players shortcut still resolves to the same launcher. Executable,
launcher, shortcut and settings hashes are unchanged; personal profiles and
native saves were not modified. The exact 4 GB executable remains installed.

Receipts: `out/preview13/qualification.json` and
`out/preview13/installation-verification.json`. Complete preview 12 rollback:
`out/preview13/rollback-preview12-20261113-102224/`, including a checked restore
script and the original payload hashes. The earlier preview 10 rollback is also
retained. No public package/tag, push, merge or destination promotion was made.
