# Galactic Adventures preview 8 validation

Build: **`0.7.3.1-ga-preview.8`**, network protocol **11**.
**Status: targeted validation passed.** Completed checks below belong to their
recorded binaries; unexercised coverage remains explicit. Package and installation
checks passed; see the delivery record below.

The preview builds on the completed
[preview 7 upstream integration](integration-ga-upstream-0.7.2.1.md), retaining
SporeKit 0.3.4, the performance fix, shared preparation/Replay and the existing
adventure/captain resource bundle formats. Earlier results remain evidence for
their recorded binaries and are not relabelled as fresh preview 8 passes.

## Changes and compatibility

- **Music:** resolve the authored ID through the native audio descriptor,
  inherited settings and actual sample IDs. Validate effective stock provenance;
  reject unsupported custom overrides without replacing or silencing music.
- **Items:** host-validated NPC/container Give, reliable complete held-item
  snapshots, native slot placement, and authoritative ordinary throws/catches.
  Timed bombs keep their original fuse after a catch; contact grenades retain
  impact behavior and are excluded from automatic catches.
- **Confirmation and revival UI:** show party beam-down/conversation status
  beside the native check mark in the bottom black bar. Keep F9 party/chat
  controls available. Show the existing two-metre revival radius, progress and
  timer without changing revival gameplay rules.
- **Downed and lighting:** block downed controls/orders while retaining normal
  native visual updates. Stop sharing the location-dependent planet quaternion
  during adventures; objective and effect simulation clocks remain shared.

Every participant needs this fork build and protocol 11. Protocol 10 previews
and upstream clients cannot join. The PlayerState payload remains 95 bytes;
the new item attachment/throw messages account for the protocol boundary.
The native GA adapter remains restricted to the exact installed GOG executable:

```text
SporebinEP1\SporeApp.exe: 24,895,536 bytes
SHA-256: dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37
```

Steam/native GA, Linux GA, Space-stage adventure entry and unsupported custom
gameplay/audio dependencies are outside the claimed compatibility.

## Recorded checks

| Check | Status | Evidence and limits |
|---|---|---|
| Focused portable music resolver | PASS | Actual descriptor bytes, inherited music settings, distinct sample IDs, missing/custom root-parent-sample rejection, truncation, strings, cycles and 64/65-descriptor limits; ASan/UBSan pass. |
| Installed stock music catalog | PASS | All 23 act music values across 69 installed stock scenarios; `out/music-resolution/audit-input.json` and `audit-results.log`. Compact audit uses real descriptors and sample-key baselines; live precedence/playback remain separate checks. |
| Candidate 6 build and portable suites | PASS | `out/preview8-build-candidate6.log` and `out/preview8-portable-candidate6.log`: co-op DLL build, network/protocol, updater, assets, rules, coordinator, flow, HUD, controls, music, world, UI routing and symbol lint. Includes deferred native completion, result and future-act ordering cases. |
| Initial Win32 native-adapter ABI suite | PASS | All 13 fixtures returned zero failures in `out/preview8-abi-results.json`: captains, lifecycle, actions, vehicles, mirror, progression, relationships, social, blocked Update dispatch, debug capability, controls, explosives and pickups. This suite preceded the candidate 6 asynchronous cleanup changes. |
| Updated Win32 mirror fixture | PASS | Rebuilt after the candidate 6 goal/result fences; zero failures. Covers coalesced deferred goals, retained native dialog targets, one terminal application, reset and stale-run rejection. This supplements the initial ABI suite. |
| Terminal goal/result ordering regression | PASS | Candidate 7 portable coordinator and ASan/UBSan runs, plus the rebuilt Win32 mirror, returned zero failures. The host sends the completed reliable Goals snapshot before the existing result message; coalesced queued goal work holds native results until applied. Includes same-pump Release/Goals/result, duplicates, reset, stale generations and delayed native cleanup. Goals application cannot independently enter native terminal state. |
| Final build and portable suites | PASS | `out/preview8-build-final.log` and `out/preview8-portable-final.log` completed successfully for the final `b804f696...` binary, including HUD viewport bounds, coordinator, flow, network, music and symbol lint. |
| Final Win32 ABI fixtures | PASS | Rebuilt all 13 targets against the final source; every executable returned zero failures in `out/preview8-abi-final-results.json`. Includes the final queued Goals/result fence and item/control adapters. |
| Added native function/layout verification | PASS | Installed-executable verifier and generated map agree: 162 functions, 4 globals and 2,723 effective symbol names. Manifest: `out/preview8-native-symbols.json`. Music resolution adds no callable native ABI. |
| Package and installation | Delivery record | Package payload verification and installation are recorded after packaging in the repository copy of this report. |

The recurring music report identified adventure 29058787 (hex), act 8, music 3DAFBC30.
That authored music ID resolves to stock descriptor
`021407EE!3DAFBC30.02B9F662` in `Spore_EP1_Data.package`; its actual sample is
4BA53FA1, rather than 3DAFBC30. Stock adventure AF1BC5FF uses the same descriptor in
acts 2/4, providing a reproduction independent of the player's downloaded copy.
See the [music reverse-engineering evidence](re/adventure-world.md) and
[offline catalog instructions](06-testing.md#installed-adventure-music-catalog-regression).

## Completed native checks

Tests use hidden, muted, isolated `winlab_` profiles. Item acceptance used
candidate 2; later UI and lifecycle changes are not silently included in those
results. Candidate 6 Intro/Talk checks used profiles 61/62. Candidate 7 terminal
Talk and Replay checks used profiles 71/72 and verified generation 2 DLL shadows.
The final build was subsequently loaded at native menus in generation 3 shadows;
the handoff record is `out/lab/preview8-terminal-final-load/result.json`.

| Check | Result and evidence | Scope limit |
|---|---|---|
| Guest Give | PASS on candidate 2. An ordinary guest mouse click gave Town key 354 to Farmer 130. Both native inventories agreed; the host Give counter advanced once. `out/lab/items-town-give-native-ui/result.json`. | Additional native host/container Give and rejection combinations remain untested in this run. |
| Farmer hammers | PASS on candidate 2. Three Phyrus Farmers displayed their authored tools on the guest. Dropping each guest-only tool restored the exact host item/owner/slot relation. `out/lab/items-town-hammer-native/result.json` and screenshots. | Missing bindings, stale revisions and malformed snapshots have fixture coverage; broader native reconstruction combinations remain separate. |
| Ordinary physical passing | PASS on candidate 2. The same key passed guest-to-host and host-to-guest, with automatic catches and matching attachments. `out/lab/items-town-key-catch98/result.json`. | The fixture drives the native impulse adapter; it does not establish the full mouse-driven throw animation. Four-player competing catches remain untested natively. |
| Timed bomb passing | PASS on candidate 2. Two catches retained one physical bomb and its original 15-second fuse: 15.235 simulation seconds, 15.299 wall seconds. `out/lab/items-timed-bomb-catch2/result.json`. | Contact-grenade catch exclusion and duplicate/rejected catches have fixture coverage; the separate final native grenade check is below. |
| Contact grenade | PASS on the final build with friendly fire 0. Host pickup/throw retained native thrower grace: an immediate self-contact probe was ignored, then native contact produced exactly one detonation on each client. The nearby host took 10 environmental damage; the parked guest took none. `out/lab/preview8-grenade-final/result.json`. | This uses the existing immutable stock-derived fixture and outside-frame native adapters. It covers one host-origin contact/damage case, rather than guest throwing, four-player catches or the full mouse animation. |
| Beam-down confirmation | PASS on candidate 6. Guest-first Continue held both native clocks at zero; all-player confirmation released both into state 3. Reciprocal captain models/scales and 351 mirrored actors matched. `out/lab/preview8-intro-candidate6/result.json`. | Wider UI/recovery cases remain below. |
| Shared Talk | PASS on candidate 6. Mr Puzzle's pages advanced independently, the final confirmation waited for the other reader, and both clients entered act 1 after native cleanup. Both reported zero native faults. `out/lab/preview8-talk-candidate6/result.json` and final-page/waiting screenshots. | Final-act results and cancellation/retry have their own checks below; deliberately delayed next-act Talk remains fixture-covered. |
| Terminal Talk, final objectives and F9 | PASS on candidate 7. A copied one-act Puzzle fixture retained its original Talk goal, NPC and locked captain. Real mouse input opened the conversation; the guest advanced pages and pressed Finished first. Its native black bar and confirmation remained while both clocks/objective waited. F9 opened and closed the party dialog without releasing confirmation. After host Finished, both native goal arrays were complete/count 1, both entered state 5 and displayed Mission Complete, and each suppressed one score submission with zero native faults. `out/lab/preview8-terminal-talk-native7-body/result.json`. | This resolved the candidate 6 terminal regression where the guest reached results with goal count 0. The fixture changes only the root act-array count/payload and required container lengths. Native cleanup delay was not forcibly injected; asynchronous orderings have deterministic fixture coverage above. |
| Replay, readiness and clean retry | PASS on candidate 7 after terminal Talk. Replay returned both players to the same preparation with every card unready. Host Ready alone did not launch; guest Ready began the shared native Intro. Host-first Continue retained both clocks at zero until guest confirmation, and the new run's goal stayed incomplete/count 0. Cancellation restored connected native galaxy menus and normally unregistered the private fixture, with zero faults. `out/lab/preview8-terminal-talk-replay7/result.json`. | This is the locked-captain terminal fixture. Fresh open-captain creation and the broader cancellation matrix remain separate. |
| Confirmation cancellation and disconnect recovery | PASS on candidate 6. Cancelling a confirmed Intro or partly confirmed Talk restored both native menus. Disconnecting recovered the guest menu and allowed the remaining host to finish once. The same adventure retried after each case without a restart. `out/lab/preview8-flow-recovery6/result.json`. | Five scoped native recovery cases; deliberately delayed callbacks have separate deterministic fixture coverage. |
| Four-player confirmation and models | PASS on the final build in profiles 61/62/71/72. All four long-named players received the locked Puzzle adventure card and saved captain; none was implicitly ready. At 1024x768 the four native status labels stayed in the bottom bar left of the check. Guest-first Continue held all clocks, and only the final player released play. Every guest bound all 351 actors and three correctly scaled current captain models. Normal cancellation restored all four menus with zero faults. `out/lab/preview8-four-player/result.json` and `handoff/result.json`. | This is one native four-player Intro/layout/roster case, not extended four-player gameplay or competing catches. The screenshot was visually inspected. |
| The Eighth Day music | PASS on candidate 6. Host-only fixture registration, actual collection/transfer, locked captain and explicit Ready launched the reported adventure. Both clients' native `play+F4` matched every authored music ID across eight acts, including final-act `3DAFBC30`, with zero faults. `out/lab/preview8-eighth-day-native-music/result.json`. | Diagnostic act selection visited acts 1–7; a real native conversation and its independent Finished controls completed act 7 and populated act 8 normally. This checks music state and the transition path, not a complete objective playthrough or audible quality. |
| Cold adventure card and loading | PASS on the final `b804f696…` build. A host-only Eighth Day fixture alias had no matching guest resource database before transfer. Both clients acquired the correct title, author, adventure thumbnail and two locked captain portraits. Explicit Ready led to native play without manually closing the lobby; captain models/scales matched, and normal cancellation restored connected native menus with zero faults. `out/lab/preview8-final-cold-eighth-day/result.json`. | The alias changes only scenario/companion resource IDs to `7F008801`; payloads and authored music/assets remain identical. Both initial captain cards were unready. The test establishes a fresh resource key, rather than an entirely empty guest asset cache. |
| Actual UDP and stationary replication | PASS on the final build in Town with 4% configured loss and up to 20 ms jitter. Over 10.0208 seconds, host-to-guest traffic was 57,031 B/s and guest-to-host 1,716 B/s, including retransmissions. No stationary-state payload/messages recurred; the host transmitted 72 updates for each of 113 dynamic identities, about 7.19 Hz in this run. Guest cached-pose repair, movement promotion and pose restoration passed. `out/lab/preview8-final-town-bandwidth/result.json` and `static/result.json`. | Proxy counts are taken before its simulated drops. This is one two-player stationary interval, not a universal bandwidth ceiling. It includes existing promoted/dynamic props; deliberately forwarded act changes were not requested. |
| Large-adventure presented frames | PASS on the final build with only the two measured game processes running. Both Town sessions retained 405 actors, native state 3 and advancing shared clocks, at 21.49 presented frames/s each against the existing 20 FPS threshold. `out/lab/preview8-final-town-bandwidth/performance.json`. | This is a hidden, muted two-client lab measurement under the same packet-loss proxy; it does not promise a particular frame rate on other machines or compare against an old binary. |
| Downed control and revival | PASS on candidate 3 for both origins. `out/windows-lab/ga_preview8_native_downed_final/result.json` embeds the two cases and hashes: zero sampled raw main-thread frame-boundary position drift under movement, advancing brain/WASD/cursor fences, usable camera, one transition, no faults, progress/range/damage interruption, pause-consistent progress and restored movement after half-health revival. The direct order wrapper has separate ABI-fixture coverage. | Asynchronous memory reads can observe physics before its after-Update correction. Native host pause was tested; revival during a conversation hold was not combined. Sixteen overlay-off/on frames showed no alternating pose/camera jitter. A ground shading edge persisted with the overlay off and after revival, so that specific edge is independent of the downed HUD; exhaustive flicker coverage is not asserted. Native device Reset and CreateDevice replacement with a revive ring active were not exercised. |
| Local lighting and old packets | PASS on candidate 3. `out/windows-lab/ga_preview8_native_lighting_final_verified/result.json`: host moved 63.013 m while guest moved 0.00201 m; their local planet rotations changed 4.95169 and 0.00012583 degrees respectively. No WORLD_TIME packets appeared in the measured traffic. A valid injected 51-byte legacy host packet caused no native TimeOfDay application or guest quaternion/camera change. The shared host adventure clock advanced, with zero faults. | This establishes local lighting and the tested legacy-packet receive guard, rather than matching host/guest sun orientations or every adventure. |

Town's native MoveTo objective must populate its citizens before the Farmer test.
Selecting an act through the diagnostic setter alone does not reproduce that
population path. The item pair returned to connected native menus, normally
unregistered its fixture package and reported zero native faults; handoff record:
`out/lab/items-native-handoff/result.json`.
The final Town/grenade pair also returned to connected native menus and removed
both fixture registrations normally. Its actual loaded generation-2 shadow DLLs
matched the final build hash, with zero faults:
`out/lab/preview8-final-native-handoff/result.json`.

An external Eighth Day fixture was extracted read-only from cached resources:
44 creation SIDs, 192 resources, unchanged authored music and captain lock.
Only copied asset references were normalized to their verified local keys and
resolved server sentinels. Provenance and source hashes are in
`out/music-resolution/eighth-day/closure/manifest.json`. This offline extraction
was followed by the separately recorded native registration, transfer, launch
and music-state checks above. The original scenario SHA-256 is
`cadf5750ab79cb048f5060074b2505b4b9da5abd47912a235bec0b7577912380`;
the normalized copied scenario is
`d2e9a8da5eca820d263b73511949c8c2f2f82b5ddaa7fc48c117d5f7268859d3`.
The final cold-key package and its key-only provenance are recorded in
`out/music-resolution/eighth-day/cold-alias/manifest.json`. Both external
registrations were removed normally at native menus. These private cached
creations and derived fixtures remain ignored local artifacts.

## Unexercised coverage

| Area | Coverage limit |
|---|---|
| Music and recovery | Native launch of stock AF1BC5FF and recovery after a genuinely unsupported dependency. Eighth Day's eight-act music-state and final-transition checks passed above; audible quality and a complete playthrough remain outside muted automation. |
| Confirmation UI and native lifecycle | Queued next-act Talk with deliberately delayed guest cleanup and device Reset/CreateDevice replacement with a revive ring active remain unexercised natively. Scoped cancellation/disconnect, four-player long names, final Talk results, F9 and Replay passed above. |
| Visual downed regression | Broader visual/playthrough coverage before claiming that every reported flicker is eliminated. The recorded overlay isolation has the limited conclusion above. |
| Existing GA regressions | Fresh new-captain creation, additional cancellation combinations, keys/gates and pickups/teleporters remain outside this fresh run. Two-player static bandwidth/FPS and one native grenade case passed above, as did locked-captain Replay and the scoped recovery matrix; earlier preview results retain their original scope. |

A complete adventure playthrough campaign, every custom objective/palette,
audible music quality and extended four-player play are not established by the
offline catalog or prior preview evidence. The four-player case above is recorded separately; portable four-card layout
tests alone are not a native party acceptance result.

## Candidate and delivery record

SHA-256 identities recorded before packaging:

| Artifact | SHA-256 |
|---|---|
| Candidate 2 `coop.dll`, item acceptance | `8c3c1aa00be1c2da503b126d51386af74ca30c3c497b6bc24ffcee9e8d017bfa` |
| Candidate 3 loaded shadows, downed/lighting acceptance | `c0ae5bbc3ab30af32cb558d00eb8fb0410d0ebf4a2f43378173a20bbbd5558fc` |
| Candidate 6 `coop.dll`, build and profiles 61/62 loaded shadows | `deb1ffd4dc6839d0c9305de56a97e1f26d7ae8c40aeede49d29b65f984778fb6` |
| Candidate 7 `coop.dll`, terminal Talk/Replay and profiles 71/72 generation 2 shadows | `453f35b7e006c45b39f2d29ea160d655c171f92b492e380019c8fc78921dee29` |
| Final `coop.dll`, cold-card/loading smoke and profiles 61/62 generation 2 shadows | `b804f69663a8d8f655f584cc4c23c7a4a4a401f62e24fa0948cbe6391b9b90b3` |
| Final build menu-only load/handoff record, profiles 71/72 generation 3 shadows | `e02a67b37b8ef159cd27df3e7d3f3a784066372ff704f8c749694ee72cd740cc` |
| Final 13-fixture ABI results | `38d573965a9485f0455bff890bcacd1e343bac36e97c77f0eaf4c822671cc76c` |
| Final four-player Intro result | `3d116ae5d0eb712c3a298a708b24b7d416c4262827c3415f381bb061293a02a4` |
| Built SporeKit DLL | `cabac8b451773be29459f6b487ef2da7eba5f67be4809a30c2ccbf3190432a1f` |
| Initial 13-fixture ABI result JSON | `9afece9275b4c60ac79e13dda6ac11f93e4116691e86869242fb669ed2c63925` |
| Updated candidate 6 Win32 mirror test binary | `c36d4cae19a4a097b0fb0704e596b844729379e8dfaa1f0956913c0009cf6422` |
| Candidate 6 Intro result JSON | `4098167dd6596f4a9eb769612af3660b382b03b9fa8a5c7e489f5f4978880b8a` |
| Candidate 6 Talk result JSON | `f71a4a5456d5c414e2d9fa20276c688081c4bb60109e0342fb83be2974a04388` |
| Immutable one-act Puzzle terminal fixture package | `cda677497c36e7913d6b81c9ec6a28723db79c71c21d312dbd921a52a8786729` |
| Candidate 7 terminal Talk result JSON | `d29d161c0aaa595ebfd34268c4bc77d0957e607235c71fb67a5bc14f39f45db7` |
| Candidate 7 terminal Replay result JSON | `1ef0cf8739e39aca5fc2bf6e0535056ef80047cd4049fd6b24bf69d6156b0a8f` |
| External normalized Eighth Day fixture package | `58ac0dbf9b83195f9ea92650bb70a8172059049e3ac79424eaff5e2c5b6d85b7` |
| Candidate 6 Eighth Day eight-act music result JSON | `76e008fd1ebde4fcc8a3751f962862ac73f5dbe19ddad3debff45fd7f448e601` |
| Final cold-card/loading result JSON | `40cd9d3a43946984f7c3eff0ab503032363b88eae6eebd4cfc4751b5e278bfc8` |
| Final Town UDP result JSON | `bd460f502251fe1d29bcf4c353ca63aea83bd1f55d3924ccc1744aee4758e0f8` |
| Final stationary replication result JSON | `cbc8186204e091308267c6056d0ab4e119ad840bedac48da766eda54949d2a56` |
| Final Town frame-rate result JSON | `01908d1a194afc2992398d82f1d2646740b43190c6988c43fd3ef0ce1197ae8e` |
| Final native grenade result JSON | `b7ba6d781438ef65dce66621f96c7e3f40bad503ebbafc6fee2e5dc7302534c7` |
| Final native menu/fixture handoff result JSON | `c5851c14278151308c4418f4746cd8392de42e5dc65cb1a7c4272b09cc4a705a` |

- Validated implementation commit: `e43c1484a4a057e91406b5b4dd9b3a8c2caf22cc`.
- Targeted acceptance passed; all private native fixtures were unregistered and
  test games exited normally before delivery.
- Deployment details are recorded in the repository copy after packaging. The
  packaged copy contains the pre-install validation record. No release or tag
  is created by this delivery.

## Installed delivery

Installed preview 8 after all private native attempts ended. All test games
exited normally before installation. No release or tag was created.

- Implementation commit: `e43c1484a4a057e91406b5b4dd9b3a8c2caf22cc`.
- Independent cold backup: `D:\Code\SporeCoop\out\installed-fork-ga-preview8-20261025-004618`.
- Matching Setup and pack installed successfully; all 14 payload paths matched
  expected hashes. Existing INI settings and original native forwarding DLL were
  preserved. The updater remains pinned to this fork's published releases.
- All 997 checked profile files were unchanged during installation,
  including saved captains, creations and settings. Normal visible startup was
  performed afterward in the existing `coop_player1` / `coop_player2` profiles.
- Both visible instances loaded `0.7.3.1-ga-preview.8` with zero native faults,
  and their windows were arranged side by side without resizing.
- Package payload/zip CRC checks passed. Stripping changed file hashes as
  expected; the installed co-op DLL's `.text` bytes match the final tested binary.
- This Windows/WSL lane lacked the `zip` utility. The script produced its
  staged files, pack and Setup; Python's standard `zipfile` completed the archive.
  Inspection verified that archive and pack payloads agree byte for byte.

| Delivered artifact | SHA-256 |
|---|---|
| Zip | `017c31e61e9e5a57855dbb4a28e71580d5d2533b4f54141d1e40fb6931d03ce2` |
| Pack | `c69b0e90ac34caf2d191407276e81185dc64114b19c951ffb5484302d58f7985` |
| Setup | `b25c33e53eeac74055f4490a6ec4bcbbd245a007361a174d81d4e9d3afe835bc` |
| Installed stripped co-op DLL | `7b3428d20b81725d0e89a1b295860da57cd1a8d8dca20637e234f275c658f20b` |

Local audit records: `out/lab/preview8-delivery/package-inspection.json`,
`runtime-visible.json`, and the backup's `installed.json`. Personal data and
external adventure fixtures are not included in the published source.
