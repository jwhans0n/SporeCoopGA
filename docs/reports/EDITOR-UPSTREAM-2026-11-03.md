# Shared Editor upstream synchronization, 2026-11-03

The user requested three priorities: install preview 9 for testing, synchronize
the feature branch with the original SporeCoop repository, then continue
Terraform and Extended Complexity coverage. Performance acceptance, broader
four-player creation/failure/recovery tests and final release smoke are deferred.

## Installed testing build

C49 was installed before source integration. It is preview 9/protocol 23 at
source `28536e76ab13f12040dbadbd6c89a0163ebf9cc6`. DLL and symbol hashes,
shortcut resolution and rollback copies are verified. The exact 4 GB executable,
player settings and player launchers are preserved. Both subsequently launched
personal instances answer read-only diagnostics with active frames and zero
faults. The installation is pinned independently of subsequent source changes.
Receipts: `out/preview9-delivery/preview9-testing-installation.json` and
`personal-startup-readonly.json`.

## Integration decisions

The fetched upstream is `WoodyWizard/SporeCoop` main at
`cf22e60` (release 0.7.3.6), adding 1,195 commits since shared base
`eb63c11`. A normal merge preserves both histories for a future pull request.
The pre-merge source is retained by local branch
`rollback/preview9-testing-installed-20261103`. No upstream/main push or public
release is part of this work.

Most of the 929 upstream-changed files merge automatically. The 27 conflict
paths require semantic resolution. Upstream content validation, asynchronous
name lookup, split-message capabilities, diagnostics, installer safeguards and
base-stage fixes are retained. Shared-editor holds, exact GA/LAA support,
module-retained callbacks, bounded task pumping and GA gameplay remain present.

Upstream owns top-level message IDs NEST=52 and PARTS=53. The integration moves
ADVENTURE to 61 and EDITOR to 62, preserving ACTIVITY=54 and DIAG=60. Protocol
24 explicitly rejects installed protocol 23 clients. This source is uninstalled;
the testing handoff is still protocol 23. Recovery/native-save formats are not
changed by assigning transport message IDs.

Session integration retains shared packet owners and transport-generation
checks through callbacks. This also protects immediate Leave/Host/Join within a
packet callback, while retaining upstream's async resolver and injected transport
and clock. Byte accounting now belongs to Connection only, avoiding duplicate
counts. Retained-resource accounting includes upstream split-message buffers.

SDK integration preserves upstream tagged-task cancellation before DLL unload.
SDK tasks that own a module reference still run their cleanup callback; they
must not be dropped before releasing their payload/module lease. Upstream's
heap-owned keepalive thread is composed with editor service-pulse fencing to
avoid reintroducing a joinable-global-thread exit failure.

The installer accepts the existing shared exact original/LAA executable pair.
Its manual fallback and updater retain the fork's release URL. The normal
package script adopts upstream's INI-preserving install scripts, while retaining
fork identity and the portable ZIP fallback. Historical upstream and fork
validation records are kept under their original scope.

## Focused checks

- Portable merged network suite: PASS, including upstream rejection/leave,
  capabilities, split messages, rejoin tokens and local protocol-23 rejection.
- The same network suite under ASan/UBSan: PASS.
- Preview packaging self-test: 16 PASS; proxy tests: seven PASS, including
  version-specific editor message accounting for protocol 23 and 24.
- SDK host/i686 queue: 199 checks each; metrics: 23 each; pulse: 138 each.
  Actual i686 pump/commands/tag cancellation: 21; module lifecycle: 2,910.
  SDK receipt records 19 pinned inputs with zero drift.
- Product co-op DLL and Kit: PASS, 316 retained production inputs, zero drift.
  C51 DLL hashes are `faba5f750f3eb4e53c0a155834df45c9d89d4bda55caf77a7c5b9b678373c72a`
  and `f03635d08960b6f3ffc0676bc195f0dfbae1f339f67b2fbb0f51b7a380c8be58`.
- Installer build and direct Windows installer/updater tests: PASS; exact native
  executable identity tests pass on Linux/i686; Adventure dispatch has zero
  failures. The installer receipt records 40 normalized inputs with zero drift
  and qualifies the pre-existing resource icon path.
- Editor UDP session suite: 31,870 checks, zero failures. Transport-generation
  suite: 3,898 checks passed, including restarting from packet callbacks.
- Updated RE lint: 657 used symbols, 3,183 known, zero problems. The 280 newly
  required frozen-build annotations explicitly disable GA/editor/memo paths on
  GOG 2016; no 2016 symbol address or linter rule was changed.

Evidence: `out/preview9-upstream-integration/`,
`out/editor-preview9-upstream-sdk/receipt.json`,
`out/preview9-upstream-sdk-merge/resolution.json`, and
`out/docs-upstream-merge-check.json`.
Installer evidence: `out/editor-preview9-upstream-install/receipt.json`.
Frozen-build annotations: `out/preview9-c51-re-lint-integration/`.

The first frozen integration build C50 stops at a missing Make conditional
newline before invoking any compiler. Its failed receipt is retained. C51 fixes
that boundary and retains 316 production inputs. These focused checks do not
replace deferred native integration, performance or release smoke acceptance.
One inherited transport test assumes a malformed dotted address fails before
return; upstream now resolves such names asynchronously. The test now uses an
invalid port, which must fail synchronously, retaining its failed-join generation
assertion. The original failed log is preserved alongside the passing rerun.

## Continuing coverage

Existing four-player Crew and Basic Tools concurrency evidence remains recorded
in the preview-9 report. The next terrain checks start with host/guest Hazards
history, then environment subtypes, selected-stamp transforms, Flora and the
remaining visible Terraform controls. Recipes require fresh native calibration.
Complexity checks must establish actual native 1,025/4,096/overflow behavior and
Save/reopen, separately from generated offline carriers and portable bounds.
Publication eligibility remains unverified.
