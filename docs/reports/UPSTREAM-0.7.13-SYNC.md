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

Evidence root: `out/upstream0713/` in the main checkout. BUG-1030 records the
initial integration build failures and corrections. `build-first.log` fails
on a colliding UI variable, obsolete keepalive timestamp and the exhaustive
world-message classifier. `build-second.log` exposes hard-coded setup-resource
paths under a custom build directory. The corrected product build passes in
`build-verified.log` with its exit receipt. Win32 cancellation fixtures pass 161136 checks and editor-runtime fixtures pass
124246 checks. Their exit receipts are zero. Final repository/native qualification
remains in progress.

The first merged portable networking suite passes (`net-first.log`), including
restart callbacks. Protocol proxy tests pass. New tests cover every authoring
traffic type, new upstream/fork channel separation and issue-publisher safety.
Symbol lint newly scans read-only Python symbol contracts; actual GOG 2024 LAA
bytes were rechecked before promoting those identities into verified overrides.
No new native call is introduced by those address-only reader declarations.
Negative input fixtures use descriptive forbidden-call data instead of fake
symbol names. Documentation and symbol checks pass after reconciliation.

## Known issues and publication gate

The reviewed public issue manifest covers the nine still-open fork reports,
separating source bugs, historical observations and missing qualification.
GitHub's connector rejects issue writes with 403, even after the user updates
access. A repository workflow requests only issue-write and contents-read,
publishes only this fork's reviewed manifest, deduplicates stable markers,
preserves maintainer closures and records issue URLs as an artifact.
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
