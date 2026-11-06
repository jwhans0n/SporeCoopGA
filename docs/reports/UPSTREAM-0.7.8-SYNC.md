# Upstream 0.7.8 source synchronization

The target changed from 0.7.7 to 0.7.8 during integration. Both candidates pin
published tag `v0.7.8`, commit `18f8d657e4691c547e50af97567fe17fd19c8193`.
Normal merge commits preserve upstream and fork history. Later upstream changes
are outside this update.

## Source and compatibility

| Candidate | Source version | Protocol |
| --- | --- | --- |
| `sync/upstream-0.7.8-main` | `0.7.8.0` | 27 |
| `sync/upstream-0.7.8-editor` | `0.7.8.0-ga-editor-preview.11` | 28 |

These identify source builds, not published releases. Main and editor clients
remain explicitly incompatible.

Upstream WSTATE=62, PLAYER_RULE=63 and the new MIRROR_FX=64 retain their IDs.
Fork ADVENTURE=65 and EDITOR=66 are distinct; editor ACTIVITY remains 55.
EDITOR therefore moves to 66 rather than the 64 proposed for 0.7.7. PlayerState
contains upstream scale and AGL followed by all three required GA identity words
(99 bytes). Older transport and HELLO versions reject. Helpers, proxies, packet
tests and fake peers use this layout. Native saves and editor recovery formats
remain unchanged.

SporeKit advances to the release's 0.3.11 while retained callbacks, shutdown
fences and editor lifetime/input safeguards remain. Original and LAA GOG 2024
executable checks remain exact. New gameplay services stay behind the shared
authoring pause and dispatch gates. GOG 2016 is unsupported, following upstream
0.7.8. Fork updater destinations and release identity remain in place.

The editor's 20 independent tickets 0522-0541 become 0606-0625, respectively
(new ID = old ID + 84). Each renamed ticket retains its legacy identity;
upstream ticket identities are unchanged. The detailed line-provenance audit is
`out/upstream078/ticket-mapping.json` in the editor worktree. Historical OUT
references and commit messages are preserved.

## Qualification

Both co-op, Kit, loader, resource and setup candidates build. Both complete
`make test HOSTCXX=clang++` suites pass, including ASan, UBSan, TSan, decoder
fuzzing, symbol/document/glyph checks and the final tools self-test. Ticket checks
report 606 tickets on main and 627 on the editor, with zero problems. The tested
source tips are `c1711473c4e337535868ba935ecf11e0e67c5e6c` (main) and
`de063d22d0decfa1fc512fb6058cf027b377fb23` (editor); subsequent report changes
do not alter tested code. The earlier editor current-version rejection failure
is retained in `out/upstream078/repository-tests-net-version-red.log`. It is
corrected by testing incompatible main protocol 27 rather than current protocol 28.

The editor Win32 native adapter passes 161,123 checks. Focused Win32 runtime
baseline, rejection restoration, terrain transition, scene alias, recovery and
staged capture fixtures pass 108, 13,231, 3,774, 11,193, 2,028 and 8,804 checks,
respectively. Main captain, lifecycle and blocked Update Win32 fixtures pass.
The corrected real-UDP transport fixture passes 3,616 Win32 checks. Synthetic
preview packaging (16 tests) and versioned editor proxy checks (7 tests) pass.
An attempted adventure-proxy discovery found no tests and is not a pass.

Integration regressions retain failing and passing evidence. BUG-0627 covers
transport fixture assumptions about upstream ACK/retry timing: the fixture now
exercises the release's retry policy without changing product timing.
BUG-0628 covers inherited Git context escaping a temporary lab test repository.
Both fixes have focused FAIL/PASS evidence in the candidate OUT directories.

## Native gate and publication

BUG-0626 remains open. `tools/windows_smoke_preflight.py` finds none of the eight
required native fixture/creation archives in `D:/Code/SporeCoop/out/fixtures`.
It lists all 14 smoke scenarios, starts no games and changes no profiles. This
preflight does not implement or qualify the Windows smoke launcher adapter.
Fixture-backed native smoke remains unavailable. Fresh two/four-player game
checks, Solo, Test Together and Native Save on these payloads have not run;
earlier preview results and Win32 mock fixtures do not substitute for them.

Publish only the two reviewable synchronization branches. Keep destination
`main` and `feat/ga-editor-multi-act` unchanged until required native checks pass.
No upstream push, force push, release tag, public release package or installation
is part of this synchronization. Broader creation matrices, performance and
release acceptance remain deferred.

The installed preview 10, shortcut, exact 4 GB executable, settings and rollback
remain unchanged. The nine installation/shortcut files match their baseline
hashes. Original local destination tips remain `2d28b60a` (main) and `1e68cd33`
(editor). The remote main remains at the original tip; no editor destination
branch is created by this update.

## Harness correction

Inherited explicit Git context caused temporary self-test commits on the
unpublished main candidate. The accidental history is retained only under a
local rollback ref. It is absent from both review branches. Parent Git settings
and original destination refs were restored and verified. A two-repository
regression fails before Git context isolation and passes afterward. The corrected
complete lab self-test passes with its parent HEAD unchanged.

Final repository runs use a read-only, per-command WSL Git path adapter rather
than inherited global context. Temporary-repository Git writes remain confined
to disposable test directories. No games or personal profiles were involved.

## Frozen payload evidence

The final main build uses 295 production inputs at `b59423dfbcfdd417b03fd3efb15f423a23694966`; the editor build
uses 383 at `e408ea01c365113376a85996b2f06522b19cb323`. Both retain zero input drift. Each final committed source
tree is checked against these built bytes, allowing only Git's text line-ending
normalization. `final-source.json`, `final-build-receipt.json` and
`committed-source-binding.json` are in the respective worktrees'
`out/upstream077/` and `out/upstream078/`. These bound production inputs; they
do not claim to capture the entire compiler/toolchain closure.

| Payload | SHA256 |
| --- | --- |
| Main coop.dll | `c817f08e84f76c8bb2ad73a472838e251ba2366994d2d7099038c12442242620` |
| Editor coop.dll | `847456ec5305bc6c95bcc4e2e28e69ff9131770a2f8db6b13d8a5e10e5dac053` |
| Installed preview 10 coop.dll | `c6e80c335ab6a97ad60fbff311381f10aa3bf7aa4fb0b62b0f360ef3097c84ca` |
| Installed 4 GB SporeApp.exe | `47f60c4db38fda1f15c5a3525617e171b0a98d728d99d645b035aa794a9199ab` |

No candidate payload is copied to the personal installation.
