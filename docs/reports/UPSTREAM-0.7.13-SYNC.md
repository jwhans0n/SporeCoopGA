# Preview 14 and upstream 0.7.13 consolidation

Branch: `sync/upstream-0.7.13-editor-main`. Source identity:
`0.7.13.0-ga-editor-preview.15`, protocol 29.

## Baseline and history

The retained fork main is `2d28b60a41586c33de012057c965d2dd4259ce4a`;
preview 14 is `31fd9bb6`. Upstream v0.7.13 is pinned to
`efd2e0ba12f92219fb0bafff0c3616b19c5a3548`, 1229 commits after v0.7.8.
The complete Git diff covers 1062 files, beyond the GitHub compare API's
300-file limit. No later upstream main changes are imported.

The two Flora backport commits are already implemented in the newer preview.
Their normal merge preserves current ColorUI guards and produces no product
source delta. Rollback refs and the original ref bundle are retained before
integration. Existing preview histories remain intact; no rebase or force push.

All 36 independently created fork tickets 0606-0641 collided with upstream.
They become 0994-1029, respectively (new ID = old ID + 388). Each renamed ticket
retains its old fork identity and prior aliases. Upstream identities and raw OUT
receipts/commit messages are unchanged. The detailed mapping is retained in
`out/upstream0713/ticket-mapping.json` in the main checkout.

## Integration decisions

Preserve upstream message IDs through 69. Fork ADVENTURE is 70, EDITOR is 71,
and ACTIVITY remains 55. PlayerState serializes upstream body after scale/AGL
and before the three 64-bit GA identities, for 103 bytes. Protocol 29 requires
its complete payload and rejects incompatible clients. Proxies, fake peers,
discovery, package checks and independent sender/receiver tests follow it.
Native saves and editor recovery formats remain unchanged.

Retain shared Peer ownership and monotonic transport-generation fences while
integrating upstream discovery hardening, connection admission, sanitization
and restart behavior. No old packet continues after a callback restarts a
session. Shared authoring admits its run/project channels and session metadata
(settings, colors, reports), while campaign gameplay/world changes remain
blocked. Session reports can progress while the author is in a creation editor.
Editor lock/selection colors use the host-derived upstream palette.

The merged UI keeps upstream's native connection steps, session settings,
colors and report controls alongside adventure preparation, difficulty,
independent editor controls and the playtest popup. Editor act/complexity
controls occupy the unused creation row during authoring. Footer and action
coordinates follow the taller upstream dialog. Preserve native owned-window
argument validation and explicit bounded tree roots.

SporeKit 0.3.16 retains upstream deferred outside-frame resumption and unlocked
task destruction, plus the fork's bounded FIFO pump, metrics, retained callbacks
and input/lifetime guards. Preserve scoped read-only memory caching alongside
upstream readability instrumentation. Fork issue/update destinations and PE
product identity remain SporeCoopGA, retaining upstream copyright attribution.
The no-make Windows helper links current product resources and required COM
libraries; custom Make build directories stage their own setup resources.

## Validation evidence

Evidence root: `out/upstream0713/` in the main checkout. BUG-1275 records the
initial integration build failures and corrections. `build-first.log` fails
on a colliding UI variable, obsolete keepalive timestamp and the exhaustive
world-message classifier. `build-second.log` exposes hard-coded setup-resource
paths under a custom build directory. The corrected product build passes in
`build-verified.log` with its exit receipt. Win32 cancellation fixtures pass 161136 checks and editor-runtime fixtures pass
124246 checks. Their exit receipts are zero. Repository audit follow-ups are recorded below; native qualification remains
blocked.

The first merged portable networking suite passes (`net-first.log`), including
restart callbacks. Protocol proxy tests pass. New tests cover every authoring
traffic type, new upstream/fork channel separation and issue-publisher safety.
Symbol lint newly scans read-only Python symbol contracts; actual GOG 2024 LAA
bytes were rechecked before promoting those identities into verified overrides.
No new native call is introduced by those address-only reader declarations.
Negative input fixtures use descriptive forbidden-call data instead of fake
symbol names. Documentation and symbol checks pass after reconciliation.

## Known issues and publication gate

The reviewed public issue manifest covers the nine original open fork reports plus the new startup-harness blocker,
separating source bugs, historical observations and missing qualification.
GitHub's connector rejects issue writes with 403, even after the user updates
access. A temporary repository workflow requested only issue-write and contents-read,
published this fork's reviewed manifest, deduplicated stable markers and
preserved maintainer closures. Its successful runs and output remain in GitHub
Actions. After all ten issues were verified, the one-shot workflow, publisher
and its isolated tests were removed from the final source candidate. The
reviewed issue manifest remains as a source record.
No private saves, profiles or raw player logs are published.

Required native smoke is not yet qualified: the original fixture archives are
absent. Provisioning/launcher work and focused editor checks must pass before
main promotion. Skips, mock fixtures and successful compilation do not count as
native smoke. Broader creation matrices, performance acceptance and overnight
testing remain follow-ups.

Until required gates pass, publish the reviewable integration branch and leave
main unchanged. Delete absorbed preview/sync branches and retire worktrees only
after successful main publication and evidence/rollback archival. Preserve
uncommitted ticket locks and unrelated work. The installed preview 14 payload,
shortcut, profiles, settings and exact 4 GB executable are outside this source
update and must match their baseline at completion.

## Native qualification attempt

The 14-case smoke prerequisites are UNAVAILABLE: creature, cell, tribe, civ,
space, space_empire and both creation packages are absent. The actual
`lab.py run smoke --lane 3 --no-build --retry 0` invocation fails before
assertions on Windows because the Wine shell backend is not a Win32 executable
(WinError193). Receipts: `smoke-preflight.json`, `native-smoke-attempt.log` and
`native-smoke-exit.json`. No skip is counted as a pass.

A separate, copied private installation launches two hidden muted GOG LAA games
and verifies relocated symbols, exclusive virtual input and SporeKit 0.3.16.
Their payload is 53c49fd87a32bb8dd86f1f2135c574b660ac1ade67ca3a55ae8eee60c8ff46fc.
Fresh and copied prior private editor profiles both stop at the strict
registration fixture before editor assertions. BUG-1276 records a zero-command
close control versus the required command 3. Startup diagnostic probes do not
qualify movement, recovery, gameplay or smoke. The runtime guard is unchanged.
Only assigned private test games are stopped; no personal profile is used.

The GitHub issue publication workflow succeeds and creates issues 1-10,
including the new startup blocker. Publication runs 43545839354 and 43547137097
retain their output artifacts. The connector still rejects PR creation with
403; the normal Git-pushed integration branch remains available for review.
The initial local and CI repository runs fail on stale protocol 28 packaging
fixtures. Commit 99fa3d01 corrects those identities, rejects protocol 28 and reads
source files explicitly as UTF8 on Windows; all 16 packaging checks then pass.
The remaining local audit/tool findings are resolved below. A fresh end-to-end
CI run remains pending.

CI run 43546502877 additionally catches unsupported parenthesized aggregate
initialization in the Ubuntu22.04 Clang fixture build. Both affected ObjectId
comparisons now use brace initialization with the same assertions.

## Additional fixture reconciliation

The affected Win32 fixture build exposed missing controlled endpoints for new
upstream wheel/cursor, task-queue and native destruction-hook dependencies, plus
an old exact SporeKit version assertion. The fixtures now retain current fix
defaults and check delivered wheel modifiers, deferred cursor restoration,
a single post-frame wake and callback lifetime. All eleven affected executables
pass after these corrections. The native deferred-identity guard is braced to
satisfy the fixture's stricter dangling-else warning, preserving its behavior.
Initial link/compile failures and the stale-version assertion remain in the
`win32-affected-build-first/second/third.log` and lifecycle FAIL receipts.

No personal game, profile or installed payload was changed. All ten recorded
installed hashes match the baseline, including shortcut, configuration and
4GB executable. The shortcut still targets the existing Two Players script.
The cleanup inventory identifies 43 absorbed local/remote refs; deletion and
worktree retirement are conditional on main promotion and have not run.

The crash dialog, report archive text and updater all point to this fork.
The temporary issue-publication tooling is removed after successful publication;
no branch-specific issue-writing workflow is left in the product source.

The first complete ticket audit finds nine historical fork records missing the
new upstream twin-strategy field. Each now names the actual controlled RED
strategy or explains why unsafe native behavior is not runtime-selectable.
Tool-only corrections are explicitly marked as having no mod change. Original
failure/pass evidence and strict checker rules are retained.

## Final integration findings

BUG-1277 records an actual uninstall cleanup defect exposed by the inherited
inventory test: seven fork maps were omitted. An independent focused Win32
regression first fails all seven removals, then passes after exact-name C++/batch
inventories and header build dependencies are corrected. Unrelated symbols,
settings and logs remain protected. The test uses only a checked workspace
TEMP directory and starts no game/setup window.

The receiver-field audit now explicitly names the adventure run and captain
selection checks in coop.cpp and movement-generation fence in adventure_motion.h.
All 46 fields/flags pass. The new registration lint identifies 53 opt-in Windows
fixtures or helper modules that cannot be launched by the Linux lane runner.
Each now states its actual ownership/private-input prerequisite; existing pure
self-tests remain in make test. No live test is marked passed by these
annotations. Remove one stale counter allowance; all 729 counter keys pass lint.

The final product identity remains preview 15/protocol 29. The single local full
make invocation stops at the new ticket declarations after all compiled suites,
ASan/UBSan, TSan and decoder fuzzing pass. After the record corrections, the
complete native-Windows ticket audit initially passes 1028 records with zero problems.
The subsequent tool self-tests expose the inventory defect above; its focused
checker and actual uninstall regression pass after correction. Remaining tool
checks are recorded separately. These resumed stages are not reported as a
clean end-to-end make invocation. Final clean CI is still required.

Read-only Git history caching was compared against native Git before use; the
original path adapter is restored byte for byte. Running the Git-heavy audit
through native Windows Python avoids WSL interoperability overhead. All rollback
refs and the original complete-history bundle remain intact. No merge to main,
old-branch deletion, public release, tag or installation occurs while native
qualification is blocked.

## Delivery decision

The integration source is published for review only. Main remains at 2d28b60a;
43 absorbed refs and their worktrees remain intact pending promotion. The
installed preview 14, settings, shortcut and exact 4 GB executable remain unchanged.
There is no new release tag, package publication or installation.

The applied [merge-batch skill](../../.claude/skills/merge-batch/SKILL.md)
requires: "After the batch: `tools/lab.py run smoke,<the batch's new tests> --lanes 6`."
The approved plan uses the reviewable-branch fallback when this gate cannot pass.
Issues [#7](https://github.com/jwhans0n/SporeCoopGA/issues/7) and
[#10](https://github.com/jwhans0n/SporeCoopGA/issues/10) track the required native
qualification blockers. The GitHub connector also rejects draft PR creation
with 403; branch publication uses the owner's normal Git push access instead.

All 18 dev-tool self-test cases were exercised; the inventory case initially
fails and then passes in its focused rerun. The changed registration checker
also passes its positive and negative self-tests. Visual self-tests, all 46
receiver-field/flag declarations and the 729-key counter/test registry pass.
No actual native smoke, focused live editor/gameplay assertions, four-player
run, broader creation matrix, performance acceptance or overnight run is
claimed. The guarded startup failure prevents those live checks from starting.

## Built source and payload record

The final native product build passes at commit `66961c8bc25c4b805f8b1c849fd40ad960724d69`.
The following records-only update does not change these build inputs. These
are local validation payloads, with no native launch acceptance or installation.

| File | SHA-256 |
|---|---|
| coop.dll | `6b7d021a3dbdf897288fb5ad52107a625bc8f493b1b7a21bb3adbb48825454c0` |
| sporekit.dll | `d5c5a7c197193aef2eb3d5c4bc4f3fad9ccf4b11821859f53ef8f4553a36c5e6` |
| steam_api.dll | `ef23406ccef6dc8cefeee35db14577b8f6d17474f52122a17afd1c5b632ca483` |
| SporeCoopSetup.exe | `cddeac79c4e34e4009ff0e0c812aa64311f275142c57ee0109bb128fc72c3763` |
| SporeCoop.package | `d122637d84dd9c06f6b29bfcaa726b2550795357376303f9a4fc18b3219f0f42` |

The final complete ticket audit, including BUG-1277, passes 1029 records with zero
problems (ticket-qualified.log, exit 0). Documentation, state-field and registry
checks pass again after the record updates. The original adapter is restored;
no temporary shell line-ending changes remain. Final source code is 66961c8b;
only this validation record and fixed-ticket metadata follow it.
