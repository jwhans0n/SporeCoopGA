# Galactic Adventures preview 9 validation

Build: **`0.7.3.2-ga-preview.9`**, network protocol **12**.
**Status: targeted native acceptance passed.**

## Scope and build identity

This preview repairs client NPC presentation, adds unanimous party pause, and
increases the revival radius to six metres. Earlier music, item, lighting,
confirmation and delivery evidence retains its build identity in the
[preview 8 report](validation-ga-preview.8.md).

The native adapter requires the exact installed GOG executable, 24,895,536 bytes,
SHA-256 `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`.
The C4 unstripped NPC smoke DLL was
`cb1c3f36138ef6a58f187cbbac07f15d213c874b9ab0796b55b1f42a7a884c48`.
A subsequent candidate retains this NPC and pause policy code, adding bounded
deferral for world interactions that were already queued when a hold begins.
The final C7 unstripped test DLL is
`b939580707f00867b823f01d0ab3f06dde8384c38213074d2bf8504a38d5d3e2`.
Protocol 12 introduces NPC clip requests and pause intent/snapshots; older peers
receive a version mismatch. Adventure/captain resource bundle formats are unchanged.

Tests use the verified game copy in a hidden, muted Windows lab with separate
profiles and saved captains PreparationNew41/PreparationNew42. They do not operate
on personal saves. This is a native Windows equivalent of the relevant lab checks;
Wine execution was unavailable and is not claimed. Two native participants were
used; four-player pause and membership cases were exercised in portable tests.

## Client NPC movement and combat presentation

The installed Maxis adventure **Defend the Crystal Mine** is
`408A0000!6E008979.366A930D`, with five acts and locked captain **Dango**
(`40626200!07CCC687.2B978C46`). Both native preparation cards enforced the authored
captain and used explicit Ready and native Continue.

Two problems were verified against the executable:

- The guest filter skipped the entire living NPC Creature::Update. It now blocks
  only NPCTickAI, allowing native animation/model and combatant maintenance while
  retaining host AI and damage authority.
- Separate spatial position/orientation setters did not mark the creature's
  animated root dirty. The verified creature SetTransform vtable slot +44 calls
  both setters and marks that flag, allowing the normal native consumer to update
  the graphics model. The fix applies to creatures; static props/buildings and
  vehicle handling retain their existing paths.

A one-actor native probe used the existing verified transform ABI at the current
pose, without writing model/core fields. Its graphics error fell from 71.39 m to
zero on the next rendered frame; an untreated NPC remained stale. The final
implementation uses that same native path. Exact addresses, call chain and
layouts are recorded in [the world RE notes](re/adventure-world.md).

| Trace | Measured result |
|---|---|
| Preview 8 baseline | 11 moving NPC incarnations; replicated spatial travel 53-243 m, rendered client travel 0-0.10 m; client model error p95 **215.63 m** |
| Candidate 2, Update fix alone | Clip requests crossed the network, but client model error p95 remained **212.28 m**; **failed** |
| Candidate 3, corrected transform bridge, 25 seconds | 13 moving NPC traces; rendered client travel 4.3-227 m; client model error p95 **1.67 m**, host control **0.61 m**; **passed** |

Candidate 3 used DLL
`e3c2da964abd2908d2836c3258593413597472812f1a964a192adc7aae366d83`.
The final candidate retains this NPC code and adds a pause startup-order fix.
A cold-start, ten-second C4 smoke also passed: 20 moving NPC histories on
each peer, guest model error p95 **1.67 m**, host control **0.47 m**, and
41-114 m rendered model travel, 46 clip requests sent/received/played, and
4,950 native client updates with 4,500 NPC AI ticks suppressed. Both still had
3,000 generator HP and zero kills during that shorter early-wave interval; its
evidence is `preview9-mine-final`.
In C3's corrected 25-second trace, client native updates increased by 12,105 while NPC AI
was suppressed 10,208 times. All **119** captured host NPC clip requests were
received and played on the guest, with none expired. Both participants observed
the generator going from 3,000 to **2,976.1499 HP** and the native kill objective
from zero to **nine**. Screenshots were inspected alongside independent read-only
spatial, animation-core and actual graphics-model positions. No native faults
were recorded.

Clip replication is bounded, coalesced, scoped by run/act/incarnation/serial and
applied outside the frame. Guest clip playback keeps the damage fence enabled.
The native listener observes a **request before PlayAnimation returns**; this
is not proof that every requested clip produced a successful native handle.
It provides visual clips, not targeted ability/projectile replay.

Evidence under ignored `out/lab/`:
`preview9-mine-baseline-trace`, `preview9-mine-candidate2-trace`,
`preview9-mine-candidate2-vt44-probe`, and `preview9-mine-candidate3-trace`.

## Unanimous pause

Opening Esc or a local modal menu stops that owner's input and residual movement,
while the host's adventure simulation continues for other participants. Once all
connected run participants request pause, shared clocks and gameplay hold. Any
participant resuming releases the hold. Native cinematic, adventure-dialog and
other gameplay pause reasons are preserved.

The adapter uses the verified RecalculatePauseFlags native transition to exclude
only local UserToggle/UIToggle/ModalDialog reasons while the party is not unanimous.
Signed nested counters are preserved. Host-authoritative, run-scoped snapshots
carry membership, revisions and per-player pause serials. Movement history is
fenced on transitions without invalidating teleport generations. Bounded queued
captain health, world hit/item/throw/social, pickup and teleport work waits through a
group hold and applies once after Resume. Per-source and item/hit receive ordering
is retained, with native eligibility and incarnation checked again on application.
Cancellation/reset invalidates queued callbacks; bounded overflow ends the attempt
with a recovery diagnostic.

The baseline reproduced a stationary host clock while a guest could physically
move with D. Candidate 2 established partial-pause continuation and unanimous
clock/owner freezing. Its final no-jump check was inconclusive because a synthetic
Resume mouse click continued into the game as native click-to-move. The corrected
fixture uses real Esc to close the menu and checks raw owner and remote-puppet
positions, native clocks and moving NPCs. Final C5 acceptance used safe
Adventure Town act 1, with both captains alive throughout. Mine attempts that downed a menu-bound captain, and distant Mine
terrain that prevented ordinary movement, failed fixture prerequisites and are
not counted as pause acceptance.

| Native C5 case | Result |
|---|---|
| Host menu only | Guest moved **17.12 m**; menu host drift **0**; host NPCs moved **25.66 m**; clock advanced **4.176 s** |
| Guest menu only | Host moved **20.42 m**; menu guest drift **0**; clock advanced **4.550 s** |
| Both menus, 2.5 seconds of held W | Both native clocks/goals unchanged; captain and NPC drift **0** |
| One host Resume, guest menu retained | Both world clocks resumed; remote movement stayed within owner travel and prior correction error, maximum excess **0.019 m** |

NPCs naturally idled during the inverse partial-menu window (0.003 m movement);
active NPC motion was required and observed in the host-menu case. Both health
values remained 92/90, both loaded shadows matched C5
`e5d55c37cfc92efb97208fe34e9c60b9fa49a040173205403a7258edc67565ac`,
and both games returned to connected native galaxy menus with input released
and zero faults. Evidence: `out/lab/preview9-pause-native-candidate5/result.json`.
Subsequent changes retain this pause/native/motion code and extend queued world
action retention only.

## Six-metre revival

The authority rule, rendered ground ring, countdown and instructions now use a
**6 m** radius. The required nearby duration remains **3 seconds**, revival restores
**50% health**, and leaving the radius or taking damage resets progress.
Portable tests cover 5.5 m, the exact 6 m boundary and 6.01 m rejection, as well as
HUD text and countdown.

Native C5 Adventure Town tests passed with **both host and guest as the downed
captain**, using the reviewed authority debug command to create each knockout.
The rescuer was staged 5.5 m away; actual native positions after settling measured
**4.136 m** and **4.135 m**, both beyond the former two-metre range. Leaving the
six-metre radius reset progress; five damage to the rescuer interrupted progress;
completion restored exactly **45/90** and **46/92 HP**. Both revived captains
could move and use their cameras. Downed input remained fenced while visual
updates continued. The larger ring, 6 m instruction and countdown were visually
inspected in the screenshots.

Evidence: `out/lab/preview9-revive-native/{context,result}.json` and the
`guest-origin` / `host-origin` screenshot and case files. Both games returned to
native galaxy application state and mode with zero faults. The source/native
revival rule and rendering are unchanged by the later queued-world-action work.

## Automated and native regression checks

- Full portable `make test`: **PASS**, including network/protocol, assets/music,
  authority, coordinator, flow, HUD, controls and pause policy.
- Four-player portable pause cases cover partial/unanimous state, Resume,
  disconnect, stale/reordered membership, duplicate/old-run messages and native
  frame versus keepalive startup ordering.
- **14 Win32 ABI fixtures: PASS**: captains, lifecycle, world actions, vehicles,
  mirror, progression, relationships, social, blocked Update dispatch, debug
  capabilities, controls, native pause, explosives and pickups.
- Mirror fixtures distinguish stale spatial-only setters from the native dirty
  transform path, and cover clip identity, bounded queues, stale events and
  incidental guest damage rejection.
- Coordinator tests cover deferred damage/healing ordering, shield reduction
  exactly once, disconnect, cancellation, retry and bounded overflow recovery.
  World mirror fixtures also cover retained social completions and mixed
  pickup/hit/drop/throw ordering, long-hold Give/Throw eligibility, exact-once
  Resume and stale callback rejection. Native throw vector, add flag, owner and
  grace behavior remain unchanged.
  Pickup/teleport and explosive fixtures cover callbacks queued before a hold,
  Resume once, and old-run/reset rejection.
- Installed-executable verification: **163 functions and four globals**, 2,724
  effective symbol names. RE lint passes with zero errors; its 58 missing symbols
  belong to the frozen unsupported GOG 2016 map.

Build/test evidence: `out/preview9-build-final-delivery.log`,
`out/preview9-portable-final-delivery.log`, `out/preview9-abi-build.log`,
`out/preview9-abi-results.json`, `out/preview9-mirror-final-results.json`, and
`out/preview9-native-symbols.json`. The mirror suite was rebuilt and rerun after
the final world-action changes; the other ABI fixture sources are unchanged.

## Final bandwidth and large-adventure FPS

Final C7 Adventure Town checks passed with one guest through the loopback proxy,
**4% packet loss** and **20 ms jitter**. The ten-second sample counted **570,742
host-to-guest** and **17,299 guest-to-host UDP payload bytes**, including repeated
reliable transmissions: **56,991 B/s down** and **1,727 B/s up**. These are UDP
payload measurements, not Ethernet/IP overhead or an internet latency guarantee.
There was no adventure/captain bundle traffic after preparation, and no recurring
static-state payload during the sample. Dynamic actor packets continued on the
existing frame-dependent update path (observed approximately 7.19 updates/s per
sampled incarnation).

The static regression independently passed unchanged-state suppression, client
cached-pose reconstruction, host movement promotion, repeated dynamic movement,
and original-pose restoration. Its 292 unchanged static actors emitted no
additional static bytes over the 2.06-second steady check; guest cached-pose
repair took 0.248 s. Its application counters are separate from the proxy's
actual datagram/retransmission accounting. Forced promotion persists for
that incarnation, as designed. Additional act transitions were not requested by
this particular static run.

The presented-frame regression passed in both active 405-actor sessions:
**21.49 FPS each**, measured over eight seconds, against the existing **20 FPS**
minimum. Only the two hidden lab games were running; personal games had already
exited. Both remained in the same playing act/run with advancing clocks and zero
faults. This preserves the recorded large-adventure regression threshold, not a
claim of a new frame-rate increase or performance on every adventure/hardware.

Evidence: `out/lab/preview9-town-bandwidth/result.json`, `static/result.json` and
`performance.json`. Its DLL hash matches final C7.

## Coverage limits

The baseline and C3 traces did not achieve the moving-host/stationary-guest
precondition because captains overlapped at the authored spawn. In the cold C4
trace the host moved 81.72 m, while idle-guest drift of 0.661 m exceeded the
fixture's strict 0.2 m stationarity threshold. Independent NPC spatial/model
observations and native combat deltas remain valid. An earlier ordinary guest
attack attempt was **inconclusive** because the generator failed naturally before a clean acceptance measurement. The final C7
fresh-attempt follow-up was also **inconclusive**: the real Crystal Hound moved
to 15.802 m before target/attack input, outside the fixture's 10 m staging guard.
Both peers agreed on its 33 HP and exact incarnation. All three verified native
getter preflights passed; no target click, key-1 attack or synthetic damage was
issued in that rejected setup. The fixture returned both games to native menus.
Evidence: `out/lab/preview9-guest-attack-final/`. This does not establish ordinary
guest attack input acceptance. Vehicle graphics-root parity was not measured.

These targeted checks do not establish a complete five-act playthrough, every
combat ability or projectile, four-player native combat, all custom adventures,
Steam GA support or Linux/Wine GA compatibility. Read-only native samples are
asynchronous; native root motion means model pivots do not exactly equal spatial
feet positions even on the host.

## Packaging and delivery

Targeted tests and all hidden native attempts ended before installation. Both
lab clients were in native galaxy menus with zero faults before teardown; one
exited through native Quit, then the bounded runner stopped its remaining owned
child. Personal games had already exited independently. No personal adventure
attempt was interrupted.

The package contains matching Setup/pack/zip, the GA guide, this validation
record and verified RE notes. Profile/settings backup, installed hashes and
visible startup evidence are appended to the repository report after delivery;
the packaged report remains the pre-install validation record. No release or
tag is created by this delivery.

## Installed delivery

Installed **0.7.3.2-ga-preview.9** after all game processes were closed.

- Validated implementation commit: `a2775f34214adbeff9413cfa2c2abc55869ee74d`.
- Independent cold backup: `D:\Code\SporeCoop\out\installed-fork-ga-preview9-20261025-033641`.
- Matching Setup/pack completed successfully; all **14 payload paths** matched
  expected hashes. Existing INI settings and the original native forwarding DLL
  were preserved; updates remain pinned to this fork's published releases.
- All **1392 checked personal profile files** were unchanged during installation,
  preserving creations, saved captains and settings. Normal visible startup
  happened afterward in the existing `coop_player1` / `coop_player2` profiles.
- Both visible instances loaded preview 9 with **zero native faults**. Their
  windows were arranged side by side without resizing. No party/adventure was
  automatically started.
- Package payload hashes, ZIP CRC and matching Setup/pack checks passed. The
  installed stripped DLL's `.text` bytes match the final tested C7 DLL exactly.
  The ZIP contains the pre-install report; this repository copy adds deployment.
- No GitHub release or tag was created. The desktop launcher remains unchanged.

| Delivered artifact | SHA-256 |
|---|---|
| ZIP | `678c7ead0021bff654aaaf4b39ee3d3e73e4c2d675526482a247aee21f746fa3` |
| Pack | `fabbd9271003e535429d93832262b16c5c6a7a95ef9f30724529a94bb6fc7cbb` |
| Setup | `87fb66f53a28e97b71c0f0f4369ebec0d41a06bfb925c0e9e6702fed81337c8f` |
| Installed stripped co-op DLL | `55ba1a5be4c9ba8df221edb2fd5ad2c13c899772ad4313fab71010dda4ee4df8` |

Local audit records: `out/lab/preview9-delivery/package-inspection.json`,
`installed-code.json`, `runtime-visible.json`, and the backup's `installed.json`.
Profiles and external creation resources are not included in the source commit.
