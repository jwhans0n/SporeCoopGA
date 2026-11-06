# Upstream 0.7.8 source synchronization

The user changed the target from0.7.7 to0.7.8 during integration. Pin the published
`v0.7.8` commit `18f8d657e4691c547e50af97567fe17fd19c8193`; do not follow later
upstream changes. Preserve ordinary merge ancestry, the GA fork and editor preview10.

## Source and compatibility

Main integration: `sync/upstream-0.7.8-main`, source0.7.8.0, protocol27.
Editor integration: `sync/upstream-0.7.8-editor`, source0.7.8.0-ga-editor-preview.11,
protocol28. These are source/build identities, not published releases.

Upstream WSTATE62, PLAYER_RULE63 and MIRROR_FX64 retain their identities. Fork
ADVENTURE65, EDITOR66 and editor ACTIVITY55 are disjoint. PlayerState contains the
upstream scale and AGL followed by all three required GA identity words (99bytes).
Older transport and HELLO versions reject. Helpers, proxies, packet tests and
fake peers follow this layout; native saves and editor recovery formats do not change.

SporeKit advances to the release's0.3.11 while retained callbacks, shutdown
fences and the editor's lifetime/input safeguards remain. Original/LAA GOG2024
identities stay exact; unsupported executable checks are not broadened. New
world services remain behind the existing shared-authoring pause and dispatcher
gates. The current GOG2016 support status follows upstream0.7.8: unsupported.

The editor's20 colliding independent tickets0522-0541 become0606-0625, with
legacy references retained. Upstream tickets stay unchanged. The mapping and
line-provenance audit live at `out/upstream078/ticket-mapping.json` in the editor
worktree. Historical OUT files and commit messages are not rewritten.

## Qualification

Both co-op/Kit/loader/resource/setup candidates build. The editor native Win32
adapter passes161123 checks. Focused Win32 runtime baseline, rejection restore,
terrain transition, alias, recovery and staged capture checks pass108,13231,3774,
11193,2028 and8804 checks respectively. Main captain/lifecycle and blocked Update
Win32 fixtures pass. The corrected transport fixture passes3616 Win32 checks.

The full repository suites, source bindings and final knowledge checks are
recorded separately when they complete. Synthetic preview packaging tests16
and versioned editor proxy tests pass. No real public release package is produced.

## Native gate and publication

BUG-0626 remains open. `tools/windows_smoke_preflight.py` finds none of the eight
required native fixture/creation archives in the shared fixture directory. It
records all14 smoke cases as prerequisites to qualification, starts no games and
changes no profiles. It is a preflight, not a completed Windows smoke adapter.
The real native smoke launcher and fixture-backed smoke remain unqualified.
Fresh two/four-player game checks, Solo/Together and Native Save on these new
payloads are not claimed from earlier preview results or Win32 mock fixtures.

Publish reviewable synchronization branches while the native gate is unavailable.
Do not advance destination main or feat/ga-editor-multi-act. Do not push upstream,
force-push, publish release tags or install either candidate. The installed
preview10, shortcut, exact4GB executable, settings and rollback remain untouched.
Broader creation matrices, performance and final release acceptance stay deferred.
