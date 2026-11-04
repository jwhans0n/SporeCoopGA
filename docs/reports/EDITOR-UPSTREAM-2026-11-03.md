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

The merge is committed as `5df22f6`; upstream `cf22e60` is an ancestor, with both
histories retained. A later native probe finds two integration gaps that the
initial compile checks cannot establish:

- C51 registers `coop.editor` twice. The incoming bare creation-request
  diagnostic replaces named shared-editor actions. C52 composes both into one
  outside-frame registration: no arguments retain upstream's diagnostic, named
  arguments route to shared authoring. No other literal command registration
  duplicates remain in the static audit.
- C52 cold-opens and exactly compares all 129 saved classes, and the host starts
  shared editing, but the guest never receives its offer. Controller Config
  retains literal message ID 53 while the dispatcher uses 62. C53 defaults to
  `net::MSG_EDITOR`; the real-UDP fixture now receives through the independent
  production constant instead of echoing Config's sender value. The focused
  suite passes 32,809 checks. Old native failure artifacts remain retained.

C53 builds with 316 retained inputs and zero drift. Its co-op DLL SHA-256 is
`e846164154f3da8c4eb5e70721552b15e169f4342379a7637380f8a9f1dc9cd0`;
Kit remains the C51 hash. Fresh private processes cold-open the exact saved
129-class/419-placement/five-act adventure, pass the complete qualified native
comparison, and automatically join with matching strict projections and zero
faults. This verifies the merged protocol-24 editor entry/join path, not every
upstream gameplay feature. Evidence:
`out/preview9-c51-cold-baseline/`, `out/preview9-c52-cold-baseline/`,
`out/preview9-c53-cold-baseline/result.json`, and
`out/preview9-upstream-integration/editor-session-canonical-dispatch-tests.log`.

Versioned GA traffic audit tools and pickup injection now select top-level IDs
from reviewed header versions. Their eight GA/pickup and seven editor-proxy
tests pass on Windows and Linux. The benchmark receiver independently uses
`net::MSG_EDITOR` and passes syntax checking with the required SDK include path;
no benchmark or performance acceptance is run. Evidence:
`out/preview9-upstream-message-tools/receipt-final.json` and
`out/preview9-upstream-integration/benchmark-wire-syntax-r2.log`.

Fresh native Terraform calibration preserves the shared revision. Host and
guest each complete Hazards toggle/Undo/Redo/Undo, reaching revision 8 with
exactly two history entries and only `8E390196` changed per action. Both then
complete day/night lock and time-of-day drag with the same history sequence,
reaching revision 24; only `0B208B41` changes. Every checkpoint strictly
converges, restores prior fields and remains fault-free. These are three
control families from two authors, not complete Terraform or four-author
coverage. Receipts: `out/preview9-c53-two-hazards/result.json` and
`out/preview9-c53-two-clock/result.json`; calibration/recipes are retained beside
those outputs.

The earlier offline large-count carriers are not suitable for a count-only
boundary claim: their selected marker/class resolves to teleporter resource
`D87454E6!90C48D09.00B1B104`. The preparation rejects these carriers for execution
and retains their original hashes. New boundary fixtures require an ordinary
static asset and measured effective weighted headroom. No native 1,025/4,096
or publication acceptance is inferred from generated counts.

Existing four-player Crew and Basic Tools concurrency evidence remains recorded
in the preview-9 report. The next terrain checks start with host/guest Hazards
history, then environment subtypes, selected-stamp transforms, Flora and the
remaining visible Terraform controls. Recipes require fresh native calibration.
Complexity checks must establish actual native 1,025/4,096/overflow behavior and
Save/reopen, separately from generated offline carriers and portable bounds.
Publication eligibility remains unverified.

## Flora replay correction and retained recovery

C53's first density edit and Undo pass, but the repeated deferred edit crashes
the host before a new transaction. The retained minidump resolves to
`sui::find` calling an IWindow slot on FloraUI's `+168` UILayout. Native code
uses UILayout::FindWindow for this member. Commit `2d47865` resolves the verified
container window and fences layout, window, control, act, scope and field
revisions across deferred replay. The exact C53 source fails the new poisoned
layout regression; the fix passes 990 focused checks and 140,147 full native
fixture checks with zero drift across 59 inputs. Evidence is retained under
`out/editor-preview9-flora-layout/`, including the original failed fixture
settlement probe and superseding corrected fixture.

C54 builds from 317 retained inputs with zero drift. Its co-op DLL is
`24b4ecab882f5d4daa6a79e99920920bcab14bf3bd78f8d4d9a9854a6e148c45`;
Kit remains `f03635d08960b6f3ffc0676bc195f0dfbae1f339f67b2fbb0f51b7a380c8be58`.
Only the private installation changes. Recovery restores the crashed project's
durable revision 26, all 129 cast rows, 419 placements, five acts, the complete
planet fields and all seven history rows (with only the transport epoch allowed
to change). Both participants then perform density edit/Undo/Redo/Undo twice,
reaching revision 42. Each edit changes only `073137BB`, creates one history
item, converges strictly and restores the prior fields without faults.
Evidence: `out/preview9-c53-flora-crash-preserved/`,
`out/preview9-c54-recover-flora-r26-r3/result.json`,
`out/preview9-c54-two-flora-density/result.json` and
`out/preview9-c54-two-flora-density-repeat/result.json`. Earlier recovery helper
attempts retain their failures: a fixture command allowlist omitted asset pages,
then an in-memory integer-key dictionary was compared to JSON string keys.
The successful follow-up only observes the already recovered draft; it does not
repeat Recovery or relax authored-field/history comparisons.

The user's installed C49 protocol-23 payload is still unchanged. A separate
Flora-only backport is being built and verified against that exact baseline;
the merged protocol-24 payload is not installed over running personal games.

## Ground Cover companion and Flora panel refresh

The first C54 Flowers action is rejected for missing settings ownership, with
no revision or canonical history change. The committed revision 42 and all
planet fields are restored on both participants. Native `F35160` also invokes
`F34500`, rebuilding `03ABB0AF` from the base effect and selected Ground Cover
key. Leasing only `56B14F05` is insufficient. Commit `ee02953` leases the exact
companion and adds a later-frame, fenced refresh of the current Flora panel.
The native `EE8C80` refresh updates all nine cards, density, Ground Cover and
Flowers without authoring notifications. Previously the rejected Flowers
checkbox stayed checked despite the restored draft.

The actual native fixture passes GroundEffect 108, Flora input 990, Flora UI
refresh 900, Environment refresh 1,137 and full adapter 141,185 checks, with 59
inputs unchanged. Exact C54 code fails the new real-Core test with the same
ownership rejection. ABI verification passes 102 entries; the frozen-build
symbol audit reports zero problems. Evidence: `out/editor-preview9-ground-effect/`,
`out/editor-preview9-flora-refresh/` and `out/preview9-c56-re-lint/`.

The isolated protocol-23 backport at `bd0c350` contains the three Flora fixes,
without the upstream merge or passive diagnostic. Its DLL is
`8d9f79abb0ff140a8392d7420e4eb792bbc0d4bc671fc8d016a238575d34d40b`;
Kit remains the installed C49 `0f25bb7a2943a4e1ce3ad7d44a077ea3e8bd41a230c1130b8f2957b10cf6ecfc`.
Private recovery restores revision 58 with all 129 classes, complete planet
fields and history. Both authors complete Flowers and Ground Cover
edit/Undo/Redo/Undo, reaching revision 74. Each action changes exactly
`56B14F05` and `03ABB0AF`, creates one history item and converges strictly.
Root reviews show the checked Flowers state after commit and unchecked state
after Undo on both peers. This verifies the UI correction as well as stored
values. Receipts: `out/preview9-backport-v2-recover-r58/`,
`out/preview9-backport-v2-two-flowers/` and
`out/preview9-backport-v2-two-ground-cover/`.

Both authors also complete Flora removal history on C54, reaching revision 50;
only `04E13CB7`, `073154E0` and `072BEC91` change. The replacement picker test
initially stops before card selection because a chooser ancestor changes flags
while opening. That fixture failure is retained separately from product
failures. Replacement and empty-card append remain unqualified at this point.

## Recovery entry during the Galaxy intro

C56 restores revision 58's data and strict projections, but the host has no
native toolbar. Existing `coop.status` identifies the exact stuck
`8915F9BB` Galaxy intro and input lock; the guest reaches its normal editor.
This matches upstream BUG-0404. The normal world-load preflight does not cover
the shared editor's direct entry, and the stage cleanup excludes Scenario.
This run is a recovery UI failure, not a successful live control qualification.
The guest returns normally; the host is detached and stopped through the owned
private runner because its native exit toolbar is absent. Revision 58,
checkpoints, EditorSaves and logs are retained in
`out/preview9-c56-menu-intro-preserved/`.

Commit `c89215b` checks the common native editor entry before ResetRequest or
loading mutations. It finishes only the exact Galaxy intro under copied
module, generation, thread and mode fences. Other cinematics remain intact and
produce an explicit retryable entry error. Native `ADFFBF` is verified to clear
the cinematic ID; this postcondition is not inferred from a mock. Focused
checks pass Native entry 134 and portable preflight 54 on Linux and i686;
unchanged C56 fails the new regression. C57 builds with 318 unchanged production
inputs; DLL `bd696414346662837ff8d82fb9c6bf896fc13a1d2412265bcdc48ec0f42552b1`.
Actual entry while the intro is playing is still pending. Evidence:
`out/editor-preview9-editor-entry/` and
`out/preview9-candidate57-build-receipt.json`.

## Boundary qualification preparation

Both participants' passive natural queries agree on 419 markers, the same
twelve counts and thirteen tuning words. The native weighted ratio is
`1.01392865`; maximum tuning is 560. These observations change no draft or
history. Bounded setter audits find no further proved missing companion beyond
GroundEffect; deep renderer and virtual callbacks remain qualified as unresolved.

The native measure routine maps its first six counts to Building, Vehicle/UFO,
Creature, default prop, Sound and Effect, with corresponding uniqueness maps in
the last six. A default prop cannot isolate the 4,096 count boundary under the
verified weights: even an otherwise minimal adventure exceeds weighted ratio 4
at that count. Sound marker 24/class 1306 matches the exact genuine C28 carrier
and has verified stock sound provenance without a powerup override. It is a
candidate for count-boundary testing, still requiring an actual native clone
with Sound count +1 and unchanged uniqueness counts. No tuning is overridden.
New count plans remain inert and refuse generation without that qualification.
Native 1,025/4,096/overflow, Save/cold reopen and publication claims remain open.
Evidence: `out/preview9-c54-passive-complexity/`,
`out/preview9-complexity-qualification/`,
`out/preview9-c56-terraform-companion-audit/` and
`out/preview9-qualified-count-plan/`.
