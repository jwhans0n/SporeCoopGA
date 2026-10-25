# Galactic Adventures preview 10 validation

Build: **`0.7.3.3-ga-preview.10`**, network protocol **13**.
**Status: targeted two-player locomotion and regression checks passed.**

## Scope and native identity

This preview replaces repeated client creature and vehicle pose assignments with
native waypoint following. The host retains AI, combat, health and objective
authority. Initial binding, large divergence, a stuck follower, and confirmed
stationary disagreement can still use a corrective native transform. Static
object replication, adventure/captain bundle formats, party pause and the six-metre
revival rule retain their existing behavior.

The supported native executable remains the exact GOG 2024 `SporeApp.exe`,
24,895,536 bytes, SHA-256
`dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`.
Protocol 13 requires matching follower behavior on all participants; it does not
change the actor packet or creation bundle layouts. Earlier preview evidence
retains its build identity in the [preview 9 report](validation-ga-preview.9.md).

Tests use hidden, muted native Windows sessions in an independent game copy and
separate lab profiles. Personal profiles and active sessions are excluded. Wine
execution and a four-player native run are not claimed.

## Reproduction and verified native paths

The stock Maxis adventure **Defend the Crystal Mine** is
`408A0000!6E008979.366A930D`. Its authored Dango captain, normal lobby Ready and
native beam-down Continue are used on both peers. Read-only high-rate traces
capture host and guest spatial roots, actual rendered model roots, native clip
identities/clocks, movement requests, health and objective counters.

Preview 9 reproduced the vehicle defect: markers 9 and 37 travelled 80.63 m and
53.31 m on the host, while the guest spatial roots travelled 82.13 m and 53.41 m
but their rendered models travelled **zero metres**. Guest model-to-spatial error
p95 was **83.99 m** and **52.54 m**. Creature marker 138 had unchanged rendered
positions in 146 of 251 frame intervals and frequently showed the idle clip
instead of the host's locomotion clip. The trace does not support a claim that
identical clip requests were repeatedly restarting the animation.

One controlled vehicle probe called native spatial vtable +44 with the existing
received pose. Its 10.28 m graphics discrepancy became zero after one rendered
frame; the untreated vehicle stayed stale. Native vehicle +44 rebinds the
physics/body path, which differs from the creature's animated-root dirty path.
No model fields or dirty flags are written directly.

Native MoveTo resets desired speed, so the follower issues MoveTo before its
bounded catch-up speed. Vehicle locomotion caps speed according to remaining
goal distance; a short network-packet goal causes persistent braking. The
navigation goal therefore leads the observed host movement, separately from the
short positional prediction. Native fly-through request mode 2, selected by the
same guarded mode-1-to-2 write used by native creature AI, avoids pre-arrival
braking. Vehicles enable it only for verified ordinary GA land locomotion;
water, air and unknown strategies retain normal native request behavior.
Full three-dimensional goals retain native terrain,
water and flight strategies. Exact ABIs, offsets, call chains and limits are
recorded in [world RE notes](re/adventure-world.md) and
[creature RE notes](re/creature.md).

Candidate 1 fixed rendered vehicle movement (79.33 m and 53.20 m) and creature
marker 138 advanced in all 250 intervals. Its guest locomotion clip matched the
host in 226 of 251 samples, compared with 25 of 252 at baseline. However, vehicle
host-to-guest position error p95 remained 13.13 m and 15.98 m. This was diagnostic
evidence, **not acceptance**; the next candidate increases native navigation lead
and adapts the stopping radius for slow authored movement.

## Final native locomotion result

The accepted C5 unstripped DLL is
`eb78a0b5725d53cb682d3a9ef033f42e3c553f1157fa629d871e36e32cdcc382`.
Both loaded shadow DLLs were independently hashed. Normal native preparation,
authored Dango, explicit Ready and both beam-down confirmations were exercised.

| C5, twenty-second native Mine trace | Vehicle 9 | Vehicle 37 |
|---|---:|---:|
| Guest rendered model travel | 79.85 m | 54.56 m |
| Guest rendered model versus its own spatial root, p95 | 0.41 m | 0.39 m |
| Guest versus host authoritative position, p95 | 8.08 m | 8.37 m |
| Median trailing distance along host movement | 2.51 m | 4.05 m |
| Cross-track error, p95 | 2.49 m | 2.04 m |

Both previously frozen vehicle models advance through normal native locomotion.
Creature marker 138 has zero unchanged model intervals in the sampled movement.
Its guest retains one native walking clip/slot/generation for **5.277 seconds**:
the playback clock advances from **0.786 to 6.066 seconds** while the spatial
root travels **67.50 m**. The actual locomotion clip matches the host in 200 of
247 samples. These native clocks distinguish a continuously running gait from
merely counting received animation requests.

The final settled generator health is exactly **2,995.22998 HP** on both peers,
with **six kills** on both native objective counters. Screenshots were inspected,
no native faults were recorded, and cancellation restored both connected native
galaxy application states and game modes. The trace observes 2,176 native MoveTo
commands across all followers and 22 pose corrections: two initial, ten severe,
eight stationary and two stuck. Moving NPCs continue between packets; pose
correction is no longer the ordinary every-frame movement path.

**Tracking remains approximate.** The 8 m vehicle p95 values are material,
especially near interactions. Strictly living creature histories have p95
host-to-guest errors from **3.84 to 16.19 m**, depending on the actor; scale affects
the correction threshold. This result establishes repaired model motion and
continuous native walking, not precise collision/combat parity. Natural AI paths
differ between successive trials, so candidate metrics are not controlled
identical-trajectory comparisons.

The rejected long-lead C3 candidate reduced median trailing but increased vehicle
cross-track p95 to 12.08 m and 9.80 m. C4 restored shorter goals and reduced vehicle
retarget frequency; C5 adds the verified native fly-through request while keeping
near-future goals. Vehicle goals are updated no faster than 350 ms, creature goals
150 ms, with a 400 ms refresh bound. Freshness is evaluated when the outside-frame
callback executes. Stale, held, attached and dead followers stop, and Resume waits
for fresh host samples. Idle vehicle turns use the verified combined physics
transform with unchanged position. Water/air and broader authored movement need
separate native acceptance.

## Automated checks

The portable follower fixtures cover slow movement, irregular packet arrival,
duplicates and reordering, stale samples, initial/severe/stationary/stuck
corrections, attachment, death, group holds and fresh-sample Resume, and run/act/
incarnation replacement. Native mirror fixtures exercise the real production
command path for creatures and vehicles, including MoveTo-before-speed ordering
and movement between packets without repeated pose corrections.

- Full portable `make test`: **PASS** on the final C5 source, including protocol,
  coordinator, captain, asset/music, authority, control, hold and lifecycle cases.
- **14 Win32 ABI fixtures: PASS**, including the production mirror path for
  creature and vehicle followers, mode-2 strategy admission and fallback, native
  move/speed order, idle rotation, holds, resume and stationary correction.
- Portable follower AddressSanitizer and UndefinedBehaviorSanitizer: **PASS**.
- Five native-trace fixture tests: **PASS**, distinguishing clip loops from
  channel replacement, independent graphics/authority errors, and live/dead
  creature samples.

Installed-executable verification checks **174 functions and four globals**,
with **2,735 effective symbol names**. The eleven newly recorded entries include
creature and vehicle waypoint/speed/stop functions, the request getter and verified
GA ground strategy. Verification passes against
the exact executable above; unsupported frozen GOG 2016 symbols are unchanged.

## Bandwidth, static reconstruction and FPS regression

Final C5 **Adventure Town** checks passed through the loopback proxy with **4%
packet loss and 20 ms jitter**. The 10.02-second sample measured **56,601.62 B/s
host-to-guest** and **1,748.39 B/s guest-to-host**, including repeated reliable
UDP payload transmissions. These are payload measurements, not link-layer
overhead or a WAN latency guarantee. Moving-actor packets retain their existing
source cadence and layout.
The 113 sampled dynamic incarnations each received **7.186 updates/s** in this
frame-dependent run. The steady-state sample contained no adventure/captain
asset traffic and no static-state messages or bytes in either direction.

The 292 unchanged static actors emitted **zero additional static bytes** during
the 2.071-second independent suppression check. Cached pose reconstruction
completed in **0.239 seconds**; movement promotion, repeated dynamic movement
and original-pose restoration passed. Both native sessions held 405 actors and
achieved **21.493 / 21.492 presented FPS**, above the retained 20 FPS threshold.
Both returned to connected native menus without faults.

Evidence: `out/lab/smooth-actors-candidate5-town-bandwidth/`, including static
and performance results. The exact loaded DLL hash is the accepted C5 hash above.

## Native pause regression

Final C5 Adventure Town act 1 passed the native Esc/Resume regression with both
captains alive and unchanged at 92/90 HP. With only the host's menu open, the
guest moved **17.84 m**, the host stayed in place, host NPCs moved **26.27 m** and
the host clock advanced **4.535 seconds**. With only the guest's menu open, the
host moved **19.82 m**, guest drift was **0.01 m**, and the clock advanced
**4.467 seconds**.

Unanimous pause froze native clocks, goals, captains and NPCs on both peers;
NPC drift was **zero**. One Resume restarted both worlds while the other menu
stayed open, without puppet movement beyond owner travel and prior correction
error. No faults were recorded, all inputs were released, and both games returned
to connected native galaxy application states and game modes.
Evidence: `out/lab/smooth-actors-candidate5-native-pause/result.json`.

## Validation limits

These tests concern native locomotion, presentation and retained authority.
Ordinary guest combat inputs producing accepted native attacks remain outside
the established acceptance evidence. This preview does not claim full adventure
compatibility, complete playthroughs, every creature/vehicle strategy, or native
four-player combat coverage.

## Evidence

Ignored local evidence is under `out/lab/`: `smooth-actors-preview9-native-motion`,
`smooth-actors-preview9-vehicle-vt44-probe-retry1`,
`smooth-actors-candidate1-native-motion`, and
`smooth-actors-candidate1-native-braking`, and candidate 2 through 5 native-motion
directories. Build and verification logs are under
`out/smooth-*`; the committed harness is
`tools/tests/coop_adventure_actor_motion.py`.
