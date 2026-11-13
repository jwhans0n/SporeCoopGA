# Shared editor preview 14 validation

Branch: `fix/ga-editor-creation-grounding`, from preview 13 tip `f899d860`.
Version: `0.7.8.0-ga-editor-preview.14`, protocol 28. Existing saves, recovery
formats, main and the editor destination are unchanged. No public release.

## Report and fix

BUG-0641: Creature/Outfitter return rejects expired original placement grounding
or exact captain settings before canonical restoration, leaving recovery frozen
and preventing normal subsequent creation entry. Restore the latest owned
canonical scene through the existing scene/session fence before strict final
validation. Preserve authored settings, local view/selection, history and
recovery safeguards. No tolerance, unsupported-field allowance, native symbol
or wire change is introduced. The diagnostic switch is
`coop.fix editor.creation_restore off`.

[Verified findings and distinction between observed errors and inferred native
teardown behavior](../re/creations.md#creations-canonical-return).

## Evidence

Private evidence is retained under `out/preview14/`. Personal profiles and
installed logs are read/copied only. Native instances 47/48 use cloned private
profiles, hidden/muted games, exclusive virtual input and the exact supported
4 GB executable. The reported draft has 300 placements, 70 classes and four acts.

| Check | Evidence | Result |
| --- | --- | --- |
| Installed report | `installed-evidence/` | Exact placement-witness error from both roles |
| Private cold draft, cancellation before fix | `cancel-return-red/` | FAIL, exact captain position mismatch on return |
| Private native body and Outfitter save before fix | `creation-save-measured-red/` | FAIL after durable admission, same captain mismatch |
| Retired grounding runtime regression | `runtime-return-red.log`, `runtime-return-green1.log` | FAIL then PASS |
| Private canonical recovery | `canonical-recovery-green1/` | PASS, both actual native scenes match |
| Repeated native cancellation/re-entry | `native-return-host-green1/`, `native-return-guest-green1/` | PASS, twice per role; exact history and observers |
| Native Creature/Outfitter save | `outfitter-host-green1/`, `outfitter-guest-green1/` | Candidate checks, final qualification recorded below |

The exact installed placement-witness signature was retained from the player
logs; the private cold recovery reproduced the related captain-ledger loss, not
the same raw placement mismatch. A controlled expired-witness regression fails
before the fix. No claim is made that native marker-key mutation was observed
in that cold recovery. Early fixture attempts stopped on wrong viewport or
random-body geometry preconditions, before product return; they are retained
as fixture failures, not passing native checks.

## Final qualification and installation

Runtime source commit: `7b3e894f05ffd7d75dd624090595468ee36440b4`.
Final DLL SHA-256:
`2ad4765f1daa14b7784258df8abed024e09ab5bbc3546e59d0b058e0236391d3`.
The earlier `49109df1...` native checks precede the final callback-verification
fence. All checks below use the final hash after a cold launch.

| Check | Evidence | Result |
| --- | --- | --- |
| Cold durable reported draft | `qualified-cold-recovery/` | PASS, exact actual observers and retained history |
| Two consecutive native entries/returns per role | `qualified-return-47/`, `qualified-return-48/` | PASS, no asset/history changes; rendered return screenshots retained |
| Host and guest Creature/Outfitter Save | `qualified-outfitter-47/`, `qualified-outfitter-48/` | PASS, one cast creation and one Undo item each |
| Native saved model dependencies | `observer-model-proof/` | PASS, both resources resolve in cached and private native loaders on both peers |
| Per-author Undo/Redo | `return-undo-redo-qualified/` | PASS, other author's asset and all unrelated content exact |
| Native Adventure Save | `qualified-native-save/` | PASS, revision 11 retained, two observers acknowledge, history exact |
| Browser frame-rate guard | `qualified-browser-fps/` | PASS, 20.6-21.5 FPS across Creature, Building and return tabs |
| Runtime Win32 fixture | `editor_runtime_windows_tests.log` | PASS, 124544 checks |
| Native Win32 fixture | `editor_native_cancellation_tests.log` | PASS, 161136 checks |
| Creation, UI and three PNG/zlib fixtures | `win32-fixtures.json` | PASS |
| Native crash/fault inspection | `final-native-logs/health.json` | PASS, no new crash/hang or return failure |

The first `return-undo-redo/` check correctly preserved the native resources but
incorrectly expected the restored asset's content revision to remain its pre-Undo
value. Redo is a new accepted transaction. The qualified check requires the new
revision and exact keys, behavior, stable identities and all unrelated rows.
This fixture assertion failure is retained. Native startup logs also retain two
inherited duplicate-hook messages (`MH_ERROR_ALREADY_CREATED`); they are not
silently removed or counted as new native faults. Hidden-game FPS is a browser
regression check, not visible-game performance or commit-latency acceptance.

`make test` passes on the final runtime source, including ASan/UBSan, TSan,
decoder fuzzing, package/protocol/document checks, fix registration, symbols,
documentation, glyphs, all 640 ticket checks and 13 tooling self-tests. Evidence:
`repository-final.log` and `repository-final-exit.json` (exit 0). Shell scripts
were temporarily normalized for WSL and restored byte-for-byte afterward.
The separately run native return/Outfitter contract checks also pass.

Installation is qualified in `qualification.json` and verified in
`installation-verification.json`. All 16 payload files match their qualified
hashes. Only `coop.dll` and `sporecoop.version` changed in the personal install.
The existing Two Players shortcut still resolves to the original launcher.
The exact 4 GB executable (SHA-256
`47f60c4db38fda1f15c5a3525617e171b0a98d728d99d645b035aa794a9199ab`),
launchers, settings, profiles and shortcut remain unchanged. The full preview 13
rollback payload and checked restore script are retained in
`out/preview14/rollback-preview13-20261113-211453/`; older rollback payloads are
also preserved. Personal games were closed before installation; the qualified
private pair was retired after checks. No branch was pushed or merged.

Broader creation matrices,
performance acceptance and upstream-wide smoke remain deferred. BUG-0637 Add Act
and BUG-0638 standalone Save New/reopen remain separate known issues.
