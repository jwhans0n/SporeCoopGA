# SporeCoop 0.7.15 integration

## Candidate and scope

The source candidate is `sync/upstream-0.7.15-editor-main`, based on the consolidated
0.7.13 editor candidate `b8f9a3ffee2d0457ea928850e776c09d3709a713` and upstream tag
`v0.7.15`, commit `66dc8640ed001fff4d5bb38f894a8c2d55530660`. The annotated tag object
is `40db448a5839850b2968c62dd6798e2829075276`. Later upstream main is outside this merge.
The source identifies as `0.7.15.0-ga-editor-preview.16`, protocol **30**.

This is source synchronization and qualification. The installed preview14, launcher,
settings, profiles, rollback payload and exact 4 GB executable remain unchanged.
No public release, release tag or installation is authorized by this delivery.

## Integration decisions

- Preserve upstream HOST_BUSY=70 and TRIBE_ANIM=71. Adventure=72, Editor=73 and
  Activity=55 have distinct manifest routes and sender/receiver directions.
- Session family 2.0 requires the strict 103-byte player state. Adventure and Editor
  families 11/12 are mandatory even when optional family checking is disabled.
  Validate them before token rejoin can evict an existing peer. Protocol 7, 28 and
  29 clients cannot join this candidate. Native saves and recovery layouts stay compatible.
- Port the fork runtime into upstream's split core files. Preserve run, captain,
  movement, act, scene, participant and retained-callback fences. Register each
  service and command once. One outside-frame adventure command handles fork
  status/readiness/settings and upstream native act/place diagnostics.
- Preserve the fork's unconditional Scenario WORLD_TIME exclusion independently
  of upstream's optional sky fix. Campaign host pause applies only to campaign
  stages. Galactic Adventures keeps unanimous party pause.
- New autosave and persistent-store services recheck native mode, session, world,
  scene and authoring holds. Queued saves/collections cannot apply to a replacement
  world. Retain campaign records during independent creation and GA sessions.
  Native busy-pause work and lifecycle destruction drain outside Present.
- Merge SporeKit 0.3.18 while retaining the fork's bounded value FIFO, retained
  lifecycle/input owners, exclusive input policy and read-only memory guards.
  Supported original/LAA GOG 2024 images stay supported; retired GOG 2016 code
  stays removed and its obsolete files remain in uninstall cleanup inventory.
- Replace weak adventure object liveness checks with current noun membership and
  recorded native serials. A pickup retire callback cannot destroy a new object
  that reused its address.
- Keep the fork updater destination and prerelease identity. Pin shell entry
  points to LF so the same Windows checkout runs correctly under WSL and Linux CI.

## Ticket identity preservation

Upstream occupied IDs through 1238 at fetch time. The 39 independently created
fork tickets BUG-0994 through BUG-1032 now occupy BUG-1239 through BUG-1277 in order
(new ID = old ID + 245). Each ticket retains its legacy alias. Upstream identities
and commit history are unchanged. The detailed mapping is retained in
`out/upstream0715/ticket-mapping.json` in the primary workspace and in ticket aliases.
BUG-1275 tracks integration failures; BUG-1259 and BUG-1276 retain native smoke and
startup blocker history. GitHub issue numbers are independent of these ticket IDs.

## Validation evidence

Intermediate evidence is local under the integration worktree's OUT directories;
final native results must bind the tested source commit and payload hashes.

- Full Windows loader/SporeKit/coop/resources/setup build passes with the pinned
  llvm-mingw 20261006 toolchain and warnings as errors. Initial build failure for
  a write-only native selection cache was retained before removing that dead cache.
- Eight portable network shards, actual Win32 UDP routing/rejoin, all 256 routing
  golden comparisons, fake peers, seven proxy tests and sixteen packaging tests pass.
  Missing/duplicate/mismatched mandatory-family and stale reconnect failures were
  reproduced before correction. See `out/upstream0715/network`.
- Eight SporeKit Win32 fixtures pass, including actual command draining, retained
  images, input hook races and outside-frame FIFO behavior. Six production kit
  objects compile cleanly. See `out/kit-merge/fixture-results.json`.
- Twenty isolated actual autosave/store/host-busy Win32 cases pass after retained
  failing cases. See `out/kit-merge/campaign-qualified-results.json`.
- Clean-checkout creation contract workflows create their owned OUT parent. Three
  regressions and all 728 original contracts pass; old failing evidence is retained.
- Source, manifest and ticket checks and repository tests are required on the final
  candidate after the merge commit makes upstream ticket history reachable.
- Original Windows smoke assertions, private native fixtures, focused two-player
  editor/adventure checks and a short four-player check are still qualification
  gates. Missing fixtures, crashes, hangs and skips are not passes.

Main promotion and absorbed branch/worktree cleanup wait for repository tests and
real Windows smoke. Broader creation matrices, sustained performance and overnight
acceptance remain deferred. Known editor issues remain in GitHub tracking and the
active known-issues document; this synchronization does not silently close them.

## Qualification progress, 2026-11-20

The selected Windows fixture matrix now passes all 35 executables plus eight
source-identical SporeKit fixtures. The full editor session fixture passed 24,360
checks in 113.933 seconds; the full runtime fixture passed 124,395 checks in
217.561 seconds. Their initial 90-second harness bounds were retained as failures,
then replaced by one measured 300-second retry each; no assertions were relaxed.
Ten focused runtime groups also pass. `out/win0715fixtures` records binary hashes,
651 tracked source-input hashes and unchanged expanded compile/link recipes.
The pickup address-reuse regression fails against the retained old adapter and
passes against the current adapter; stale consumed contacts remain intercepted.

The startup command-0 variant is now verified in separate cold profiles. Its
native command getter is correct; clicking its exact validated close control
returns directly to native Galaxy controls without the warning the old harness
waited for. Keep the command-3/warning path and stale-window guards. A normal native
`-r:1440x1080` startup option gives both that rendered viewport and UI canvas at
cold launch, avoiding a late renderer reset. The Windows backend owns a whole-run
kernel lease, retains failed cleanup handles, kills only its owned processes and
verifies the exact deployed symbol set. Its eighteen contracts pass.

Ticket checks preserve every ancestry, release, ambiguous-object and message
rule while batching their Git history reads. Real temporary repositories compare
identical verdicts and notes with the old implementation. The complete 1,270-ticket
check passes in 11.35 seconds on Windows and 26.22 seconds through the unchanged
WSL Git adapter, replacing repeated history walks lasting over ten minutes.

The published review branch is on the user's fork. GitHub's fuzzing job passes;
its build/test workflow and real campaign/editor native qualification are pending.
The connected GitHub app still rejects writes, but the user-authorized local
GitHub CLI now works. Issues1-10 have corrected ticket/source links with concurrent
bodies checked before update. The new observed Goal Delete activation gap is
tracked publicly as [issue11](https://github.com/jwhans0n/SporeCoopGA/issues/11).


Native group preparation caught a duplicate pickup detour before any launch:
upstream player_rules and the Adventure adapter owned two names for the same
verified native function. The adapter now uses the sole existing hold/release
owner through typed forwarding/interception; actual-handler RED/GREEN and mirror
fixtures pass. Editor observation also revealed repeated campaign-avatar reset
work during authoring; that observer is now gated until gameplay resumes. Cold
native availability, pickup behavior and idle logging must be verified on the
rebuilt payload before smoke/promotion.


A full post-init cold check then caught the later Civilization/GA vehicle generic
behavior-tree collision. A dedicated single-detour broker preserves both policies
without reordering services. Strict shared-dispatcher/actual GA vehicle regressions
pass20,057checks for each subscriber order; a controlled Civ slot is explicitly
not production Civ gameplay evidence. Native Civ smoke and full cold hook/error
checks remain required. The global read-only315-site hook audit found no other
concrete shared-owner collision, and cannot replace that native reprobe.


The real unit run subsequently found nine debug-symbol tooling failures for the
full preview version. PE validation treated the suffix as a numeric component,
and report parsing discarded it. The fixes preserve exact preview identities
while validating the numeric PE quad independently. Same-core wrong-preview
symbols and incorrect fixed PE versions are still refused. A real-PE regression
passes 41 checks on Windows and WSL, with an old-tool FAIL retained. All three
existing preview DLLs pass exact full-identity validation; the unchanged original
symbol packaging/triage regression now passes with zero failures against the
actual DLLs and archive, including wrong-version and bad-SHA refusals. Existing
triage selftests pass. Evidence is retained under
`out/upstream0715/network/preview-symbols`; no C++ or installed payload changed.
The complete real unit and native smoke acceptance gates remain separate.

The valid Hello/Welcome fuzz corpus also inherited the new default family list
and then appended randomized families, creating invalid duplicate IDs. The
corpus-only correction replaces that list with unique nonzero IDs/revisions and
retains canonical manifests, lengths 0..64, duplicate/zero rejection and all
original mutation/round-trip assertions. The exact original unit-fuzz target
passes ASan/UBSan in 95.674 seconds after the retained 44-failure run; no decoder
or production wire changed. Evidence: `out/integration/fuzz-fixed/test.log`.


The real two-player Test Together gate exposed a pre-existing place-identity
consumer bug, tracked as BUG-1279 and [issue12](https://github.com/jwhans0n/SporeCoopGA/issues/12).
Both native previews entered Scenario and exchanged world traffic, but differing,
changing player place IDs prevented reciprocal captain puppets and readiness.
`modes::adventure_key` returned a uint32 preview place as bool without assigning
its native-key output references, which `adventure_place` then hashed. The preview
override now belongs in the place consumer; native-key diagnostics always read
genuine fields and deterministically clear outputs on failure. Preview holds and
stale runs still return 0 ahead of debug forced-place overrides. Ordinary native
adventure identity behavior, including separate BUG-0963, is unchanged.

The actual production modes.cpp Win32 regression retained 77 failures in 87 checks
before the fix and passes 87 checks after it. The existing actual preview-provider
fixture passes 112 checks. Evidence is in `out/upstream0715/network/adventure-place`
in the primary workspace. This focused fix was prepared in an isolated worktree
from 47ed940a while the original native unit run stayed frozen. A new native
Together reprobe and final-source acceptance remain required; no native group
success is inferred from these fixture passes.


## Final source qualification, 2026-11-20

The required source/main qualification is complete. The candidate tested by the
final campaign runs is `7a129c0d546470c87316b907c57b7679885fbfcc`, version
`0.7.15.0-ga-editor-preview.16`, protocol 30. It contains pinned upstream
`66dc8640ed001fff4d5bb38f894a8c2d55530660`. The final documentation changes do
not change production, SporeKit, native symbols, Python test/setup code or build
recipes. This qualification does not publish a release or install a payload.

The original smoke suite has 14/14 actual standard PASS receipts. Existing
labcov reports complete coverage; existing loggates reports 13/13 gameplay rows
and zero findings. No skipped/inconclusive case or failed setup substitutes for
a pass. The complete real repository unit run is the 569.18-second row below;
push and PR build/fuzz workflows also pass on the final tested source.

| Original case | Tested source | Seconds | Standard run |
|---|---|---:|---|
| unit | `670f8f91` | 569.18 | `20261120-191203-windows-54961c` |
| creature | `670f8f91` | 180.12 | `20261120-192354-windows-2cc419` |
| ui | `670f8f91` | 276.06 | `20261120-193146-windows-3a1fc7` |
| team | `670f8f91` | 106.02 | `20261120-193622-windows-9da122` |
| cell_coop | `670f8f91` | 149.90 | `20261120-193622-windows-cae195` |
| space_coop | `670f8f91` | 94.06 | `20261120-193853-windows-05d959` |
| frames_loop | `670f8f91` | 111.10 | `20261120-194027-windows-59c239` |
| tribe_coop | `7a129c0d` | 164.68 | `20261120-200818-windows-0e1ab7` |
| civ_coop | `7a129c0d` | 120.37 | `20261120-201103-windows-9a9401` |
| space_play | `7a129c0d` | 208.88 | `20261120-201103-windows-d7be0b` |
| select | `7a129c0d` | 135.90 | `20261120-201304-windows-73a24b` |
| space_page | `7a129c0d` | 97.35 | `20261120-201432-windows-fcd387` |
| tribe_play | `7a129c0d` | 124.45 | `20261120-201520-windows-3a2b26` |
| chaos_tribe | `7a129c0d` | 235.16 | `20261120-201725-windows-bbdc28` |

Seven receipts retain their original tested source `670f8f91`; seven retain
`7a129c0d`. No source head or verdict was rewritten. All production source,
native maps and five payload bytes are identical across these tooling revisions.
The later adapter adds UTF-8 output and a read-only cold-scene readiness fence;
the earlier successful scenarios retain their original assertions and verified
inputs. Civ exercised native vehicle movement, building, ownership and gifts.
Tribe's original movement check now measures 6.0 m after its native entry bakes
clear naturally; no pause bit or gameplay assertion was changed. Chaos performs
267 actions over 120 seconds with five rejoins and zero native faults.

The canonical official aggregate is
`out/windows-lab/final-smoke-7a129c0d-native-final/official-aggregation/results.json`,
SHA256 `89effc8d3dd874d62259bd7c889459f0b17b98d5b40be9b0dd2df6bf9e6ce6c0`.
Its coverage hash is
`a453042c3cdfd7486cea8af30716baa0817aeb9eaac8b19c5828988cf96d893a`;
its game-log gate hash is
`2deb10692929aa59a0f1314a5a8dd2f1dc04dda2885dbe51f6148735fae72846`.
Original standard receipts and their selected copies remain byte-identical;
all failed and interrupted runs remain separately retained. Independent audit
also checks actual PIDs, profile generations, supported LAA image, native log
rotation, payload/maps and replayed original log gates.

The runtime source is `4d4788a21c6b00417f2d193cf7ad60d6368af83a`; coop.dll is
`7bd89bdabc2cdef9e58abcf9038ae23453f538c42f6c33d33c38a88cd3f1cb1c`.
The compiler dependency proof covers 330 unique source/header inputs and 47
qualified executable hashes. Unchanged fixtures keep their existing results;
the production adventure-place regression fails 77 checks on the old source
and passes 87/87 after the fix, while the existing native preview-provider
fixture passes 112/112. Final Windows adapter contracts pass 30 checks.

Focused GA acceptance has 17 hashed passing receipt records in
`out/win0715fixtures/native-ga-focused-qualification.json`. It verifies:

- Two- and four-participant terrain, with authors 53/54 and observers 55/56 in
  the four-player run: exact native convergence, one transaction/history item
  per completed stroke and per-author Undo/Redo preserving the other stroke.
- Captain world-body dragging by host 53 and guest 54, with immediate native
  observer updates on 55/56 and exact Undo/Redo preserving appearance, health,
  authored lock and unrelated acts. A first unselected portrait drop is also a
  no-op without sharing; those retained failures do not establish a co-op defect.
- Host and guest Solo with three peers remaining in their editors, no gameplay
  run and exact Build restoration. Test Together has reciprocal moving captain
  models, native shared objectives/conversations and host-authoritative progress.
- Four-player unanimity: host-only Esc keeps native clocks running, all four
  pause native clocks/goals, and one Resume resumes. Coordinated Build preserves
  durable revision 16 and every participant's nonempty history/fingerprint.
- Late join, explicit cold recovery to revision 16/all four history records,
  four-peer native Save, ordinary co-op native entry/talk/objectives/movement and
  normal return. Final four-peer idle checks have no repeated NPC world-change
  churn, hook errors or native faults. All owned game processes are stopped.

Retained setup failures include native Galaxy coordinates varying by profile,
an offline notice on reused profiles, cp1252 output, and assertions begun during
native stage-entry bakes. Genuine saves, fresh marked private profiles, verified
native load and bounded natural readiness resolve these without forging data or
relaxing native assertions. One four-player owner lifetime expired during an
attempted shutdown; that exit stays FAIL. Subsequent explicit recovery and final
normal cleanup pass. Unqualified first-use/keyboard-only fixture routes and the
historical standalone Save New/cold comparison remain separate.

Broader creation/replacement/failure matrices, performance and large-adventure
memory acceptance, overnight testing and release acceptance remain deferred.
BUG-1259 remains open for that broader acceptance; its required Windows/main
gate is qualified. Known Add Act, cold reopen, first Goal Delete and stale
ownership-caption reports remain open. The new caption report is
[issue 14](https://github.com/jwhans0n/SporeCoopGA/issues/14), BUG-1280.

Main promotion and metadata-only absorbed-branch cleanup use this qualified
source and retain rollback refs, all worktree/OUT evidence, private profiles and
exact dirty-file witnesses. Installed preview 14, its settings, shortcut,
rollback payload and exact 4 GB executable remain unchanged.
