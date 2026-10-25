# Galactic Adventures preview 11 validation

Build: **`0.7.3.4-ga-preview.11`**, protocol **14**.
**Status: targeted native two-player enemy acquisition and damage checks passed.**

## Fix and compatibility

**Defend the Crystal Mine** uses authored conditions and attacks targeting
political category 1. Local captains have political ID 1; remote captain puppets
have ID `FFFFFFFF`. Native Scenario sensors already see a nearby guest, but the
category predicate rejects that guest before selecting an attack. Relationship
query aliasing alone does not repair this separate predicate.

The final adapter expands only a false native player-category match, at four
verified condition/action callers, to current living hostile remote captains.
Native sensors retain visibility, range, ordering and reference ownership. Native
AI chooses targets and executes attacks once. Political IDs, avatar flags,
sensor vectors and combat targets are unchanged. Authored class targets, other
categories, neutral characters and existing nonparty combat targets retain native
behavior. Downed and stale captains cannot gain eligibility through the adapter.

Supported executable: exact GOG 2024 `SporeApp.exe`, 24,895,536 bytes, SHA-256
`dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`.
Protocol 14 requires matching previews. Packet and creation bundle layouts are
unchanged. Earlier features retain their scope in the
[preview 10 report](validation-ga-preview.10.md).

## Baseline and native boundaries

Tests use an independent real game copy, hidden muted Windows sessions and closed
lab profile copies. Personal saves are not used. The stock Maxis adventure is
`408A0000!6E008979.366A930D`, with authored Dango, explicit lobby Ready and native
beam-down confirmations. Crystal Hound marker 138 / class 1082 supplies positive
and negative controls in fresh attempts.

- Preview 10 host control: targeted in 234/234 samples, independently checked
  native target getter, natural health reduction from 232 to 46.60.
- Guest negative control: 96 samples over 5.93 seconds, actual host-side guest
  distance 0.26-6.55 m (median 2.27 m), host over 223.9 m away. Native target
  stayed null and guest health stayed 232.
- Later captures contained the guest in the native Scenario sensor in 35/35
  samples, while political IDs remained local 1 / remote `FFFFFFFF` on both
  peers. The actual authored condition and attack both target type 1, value 1;
  the separate generator target is type 2, class 1069.

Fixture placement changes only owner captains. No NPC orders, AI scores, authored
properties, health loss, damage or objective results are manufactured. Actual
host-side puppet proximity is checked independently of owner placement.

`MatchesBehaviorTarget` at C7D230 is cdecl bool(GameData*, BehaviorTarget*).
Its type 1 branch calls primary virtual +4C; the verified animal implementation
reads political ID at +30. The adapter is fenced to returns EC0CF5 (sensor
condition), EC0F70, EC0F8F and EC1032 (attack selection). A thread-local scope
identifies the original NPCTickAI subject and restores nested subjects without
extra AI calls. Current run, act, incarnation, ownership, coordinator Alive
state, native validity and hostility are checked before extending a false match.
See the [world RE notes](re/adventure-world.md) for exact ABI and call evidence.

## Final matcher-only candidate

Native-tested C3 DLL SHA-256:
`85bdebb2aed8dc870a6f0c37cdef29b25b02d2fb558a7ea3ac3a9fa49e71220d`.
After two comment lines were clarified, the final source build is
`870e85ccb2526e7648aca569e7fffbdb4db376e452c6ca9a3b2d50a4e408f6db`.
Its executable `.text` bytes, RVA and size match C3 exactly, SHA-256
`9c2481346ec1f96e705814836b753bd7f8335e03e586a101f23af3dce704eec5`.

- Guest proximity: target in **93/93 samples**, **12 independent getter checks**,
  natural owner health **232 to 139.299957**. The host puppet independently
  reported the same final health. Scenario sensor membership was 93/93.
- Host control: target in **95/95 samples**, 12 getter checks, and the same
  **92.700043** natural health loss.
- Guest to host to guest: all phases acquired and naturally damaged the nearby
  captain. Target samples **48 / 26 / 38**, getter confirmations **6 / 3 / 4**,
  health loss **55.62 / 18.54 / 18.54**. Actual proximity windows were
  **2.96 / 2.64 / 2.58 seconds**.
- Native perception pool count stayed within 2-4 and ended at 3; no captain had
  more than one sensed record. Political IDs remained unchanged.
- Every attempt returned to connected native galaxy menus with **zero faults**
  and no held inputs. Screenshots corroborate guest attack effects and 140 HP HUD.

Evidence: `out/lab/enemy-targeting-candidate3-guest`, `candidate3-host` and
`candidate3-switches`, with the common `enemy-targeting-` prefix.

## Rejected approaches and automated checks

Exploratory C1 expanded normal singleton creature perception and preserved native
sensed-record ownership. It produced over five seconds of continuous guest sight
without acquisition or damage. The actual adventure attack selector consumes a
separate Scenario sensor. The observed player-target evaluator and generic score
dispatcher paths supplied no evidence for this hound's attack decision. Those
exploratory hooks, avatar substitution, pool handling and diagnostics are absent
from final C3. C2 first proved the matcher fix; C3 repeated acceptance after their
removal. Negative findings remain in the RE record.

The final Win32 ABI fixture covers four actual assembly caller fences, exactly
one original call, native successes, foreign callers, class/avatar/other
political targets, neutral/dead/downed/disconnected/stale identities, duplicate
party entries and existing authored targets. **2,000 calls** leave object and
target fields unchanged. It passes with zero failures, as do the actual
Update/NPCTickAI nested-scope and mirror fixtures. Portable tests and the exact
executable verifier pass.

The verifier records **186 functions, four globals and eight noncallable labels**,
including historical investigation evidence, with **2,754 effective symbols**.
Final production adds one predicate hook with four caller fences; the larger
manifest does not imply that every reviewed helper is hooked.

## Cold start, bandwidth and performance

New hidden, muted native processes loaded C3 from cold startup. The nearby guest
was targeted in **92/94 samples**, with **11 independent getter checks**, and
again lost **92.700043 HP** naturally. Scenario sensor membership was 92/94,
pool count stayed 2-3, no captain record was duplicated, and both games returned
to native menus with zero faults.

Adventure Town passed with **4% packet loss and 20 ms jitter**. The 10.019-second
sample measured **57,070.80 B/s host to guest** and **1,742.61 B/s guest to host**,
including retransmitted UDP payloads. These are payload measurements, not link
overhead or a WAN guarantee. All 113 sampled dynamic actors received
**7.18604 updates/s**. The sample contained no asset traffic or static-state
messages/bytes. The 292 unchanged static actors emitted **zero additional bytes**
over 2.114 seconds; cached pose reconstruction took 0.231 seconds, and movement
promotion/restoration passed.

Both clients held **405 actors**, with **21.488 / 21.613 presented FPS**, above
the retained 20 FPS threshold. Both recovered native menus without faults.
Evidence: `out/lab/enemy-targeting-candidate3-cold-guest` and
`out/lab/enemy-targeting-candidate3-town-bandwidth`.

The native Town act 1 pause regression also passed. With only the host's menu
open the guest travelled 16.73 m, host drift was zero, NPCs travelled 23.18 m and
the clock advanced 4.383 seconds. With only the guest's menu open the host
travelled 18.76 m, guest drift was zero and the clock advanced 4.300 seconds.
Unanimous pause held native clocks, goals, captains and NPCs on both peers, with
zero NPC drift. One Resume restarted play within buffered-movement bounds.
Health stayed at 92/90, all inputs were released, both native menus recovered,
and zero faults were recorded. No native pause counters were manufactured.
Evidence: `out/lab/enemy-targeting-candidate3-native-pause/result.json`.

## Limits

Native Windows two-player checks do not establish Wine or
four-player native compatibility, every authored target type or AI command,
broader adventure compatibility or complete playthroughs.

Movement remains approximate, with the residual errors recorded in preview 10.
Ordinary guest click/key target selection and attack input remain unverified.
Natural enemy acquisition and damage do not extend that separate input claim.

Ignored evidence is under `out/lab/ga-enemy-targeting-re` and the baseline and
candidate directories above. The committed harness is
`tools/tests/coop_adventure_enemy_targeting.py`.
