# Galactic Adventures co-op: experimental guide

Fork preview `0.7.3.4-ga-preview.11` fixes authored player-team attack targets
rejecting eligible guest captains. Native sensing, hostility and NPC target
selection remain in control, with existing authored nonparty targets preserved.
It retains preview
10's native creature/vehicle waypoint followers, preview 9's unanimous party pause
and six-metre revival, preview 8's music, held-item and confirmation fixes, and
the upstream SporeCoop 0.7.2.1 integration. Source:
[jwhans0n/SporeCoopGA](https://github.com/jwhans0n/SporeCoopGA).
Preview 11 passes targeted two-player **Defend the Crystal Mine** checks for
native guest/host targeting, natural damage and target switching. See its
[validation report](validation-ga-preview.11.md) for exact evidence and limits;
broader adventure compatibility remains unproven.
Preview 10's two-player locomotion checks pass with residual tracking lag; see its
[validation report](validation-ga-preview.10.md) for recorded evidence and limits.
The [preview 9 report](validation-ga-preview.9.md),
[preview 8 report](validation-ga-preview.8.md) and
[preview 7 integration report](integration-ga-upstream-0.7.2.1.md) retain their
original scope.

The native adventure adapter requires the exact GOG executable, SHA-256
`dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`.
Upstream calls this GOG 2024; the adventure RE notes call it March 2017 after the
ModAPI address map. See [build identification](re/build-dc04.md). Steam and other
same-code variants retain upstream base-stage support, but their native adventure
adapter is disabled and adventure compatibility remains unvalidated. Every player
needs Galactic Adventures and the same fork build (protocol 14); earlier GA
previews and upstream 0.7.0-0.7.2.1 clients cannot join this preview.

Preview `0.7.0-ga-preview.6` reduces traffic for stationary buildings and props and
shares explosives, crates, power upgrades and teleporters. It retains group scenes,
Replay, current captain identity/scale, keys/gates, the shared lobby, Mr Puzzle
music, native captain creation, cancellation recovery and the performance fix.
Its protocol 10 results remain historical evidence for that preview.

## Start an adventure

1. Open **Co-op** from the galaxy menu. One player hosts; the others join the lobby
   before the attempt begins.
2. The host opens **Adventure settings** and chooses the difficulty.
3. The host selects an adventure in **Sporepedia** and presses its normal **Play**
   button.
4. Each player uses the native captain setup screens. Choose an existing captain
   or create a new one, enter its name, review it and finish saving. Adventures
   that require an authored captain keep that restriction.
5. Review the adventure card and everyone's saved captain portraits in the shared
   lobby. Wait for content and captain outfits to transfer. Every player, including
   the host, presses **Ready** explicitly.
6. After loading and beam-down, everyone presses **Continue**. Gameplay and
   adventure clocks wait for every connected participant to confirm. Players who
   confirm early see each player's status in the native bottom black bar, to
   the left of the check mark. A sent confirmation becomes Ready after the host
   acknowledges it.

The host controls shared objective and act progress. A qualifying action by any
teammate can advance the party's goals. Late joins and reconnects wait for the next
attempt. Entry from the Space stage is outside this version's scope.
Cancelling preparation returns both players to the native galaxy menu. Successfully
saved captains remain available when retrying the same adventure.
**Change captain** opens your native picker. Changing a captain clears everyone's
readiness; cancelling a replacement keeps the previous saved captain. Locked
adventures show the authored captain and disable replacement. **Cancel adventure**
returns the party to the native galaxy menu while keeping the party connected.
After results are saved, **Done** returns to the menu. Any player can choose
**Replay** to request another shared attempt. After everyone accepts their
results, Replay opens the shared lobby with the saved captains and everyone
unready. Review the lobby and press **Ready** again to launch.

An objective conversation opens its native camera and dialog on every player.
Each player advances pages independently. **Finished** confirms that player's
exit; the objective and clocks wait for the whole group. If another objective
conversation is requested during one already in progress, it is queued and
revalidated before presentation. A disconnected participant leaves the check.
These confirmation checks no longer open the full co-op dialog automatically.
**F9** still opens party/chat controls and the adventure cancellation action;
cancelling restores the native menus.

Opening Esc pauses your own input through the native menu. The shared adventure
continues until every connected participant has a pause menu open. When everyone
is paused, shared clocks and gameplay stop together; one player resuming releases
the party pause. Beam-down and conversation confirmation holds remain separate.
NPC AI, damage and objectives remain host-authoritative. Client creatures and
vehicles use native waypoint movement between received host samples, with bounded
prediction and catch-up. Native hard corrections handle initial binding, severe
divergence, stuck followers and confirmed stationary disagreement. Clients keep
native locomotion and replay host visual clip requests; attack and unknown clip
requests retain their existing handling. Held items retain their attachment path.

Some authored enemy instructions select the local captain's player team. Guest
puppets have a different native ownership ID, so those instructions previously
ignored guests even when the native sensor already saw them. The host now
recognizes eligible living guest captains in that specific authored target match.
Native sensors still decide visibility and native AI selects orders and attacks
in one normal tick. Neutral relationships, existing nonparty combat targets and
explicit authored object classes retain their behavior. This does not change
captain ownership, avatar flags or native sensor lists.

Any captain can pick up a colored key and open its matching gate. The host checks
ownership, range and color, then shares native gate opening and key consumption.
A key held by another captain cannot be taken. Remote captains use the current
saved selection and their owner's actual spatial scale.

Mines, exploding barrels, hand bombs and grenades use the host's native activation,
timing, damage and chain reactions. Their explosions are environmental hazards:
friendly-fire settings do not reduce them. Crates retain their authored behavior;
a harmless crate does not acquire barrel damage. Carried and thrown objects keep
spatial updates. Guests display approved effects while the host applies damage.

Use the native Give interaction to hand an item to an NPC or eligible authored
container. The host validates ownership, range and the recipient's native receive
rules before applying the handoff. NPC tools and other held items use shared
attachment state, retaining their native hand/slot placement instead of receiving
competing loose-object transforms. This covers authored tools such as the Phyrus
Farmer's hammers in Adventure Town; it does not add new item or objective rules.

Captains can throw eligible handheld items toward each other using the native
throw action. An alive captain with an available compatible hand slot catches
the item when its flight crosses their native bounds. The host resolves the
catch and publishes the new attachment. Timed hand bombs can be caught and
thrown again while the original fuse continues. Contact grenades keep their
native impact/explosion behavior and are excluded from automatic catches.
An occupied hand or a native receive rejection prevents a catch.

Health, speed, damage and shield pickups benefit only the collecting captain.
Collection removes the shared pickup until its authored respawn. The host resolves
simultaneous claims. Boost magnitude, refresh and expiry follow native properties;
group confirmation holds pause their simulation time. Each captain uses teleporters
independently, with the authored exit and a separate cooldown, without a party check.

The host transfers supported missing adventure and captain resources during lobby
preparation. Guests reuse installed stock resources and cached transferred assets.
This transfer is separate from gameplay traffic: unchanged static transforms stop
recurring after a reliable baseline, while health/state changes remain reliable.
Movement promotes a static object to regular spatial updates for that incarnation.

Act music follows the game's native audio descriptor and inherited sample
references. A valid track can use a different sample ID from its authored music
ID; The Eighth Day's reported track is one such stock descriptor. Collection
preserves the authored track, checks installed stock resources and rejects
missing or unsupported custom audio. It does not replace music or build a list
of exceptions for individual adventures.

Each captain keeps native adventure lighting relative to their local position.
The host's planet orientation is no longer copied to guests in adventures.
Objective clocks, group holds, boost expiry and revival still use shared time.

## Difficulty

Settings are fixed during an attempt. Friendly fire between party captains offers
**0%, 25%, 50% or 100%**, with **0% as the default**. Enemy health defaults to
`1 + 0.5 * (players - 1)`; enemy damage defaults to 1x. Host overrides replace these
defaults.

| Death rule | What happens |
|---|---|
| **Teammate revival** (default) | An alive teammate stays within 6 metres for 3 seconds to revive you. |
| **Teammate revival + automatic recovery** | The same proximity revival is available; otherwise you recover after 30 seconds at the downed position. |
| **Any death fails** | One captain's death ends the party's attempt. |

Revival restores 50% health and grants 2 seconds of damage protection. Damage to the
rescuer, moving out of range or disconnecting resets revival progress. **Everyone
being downed always fails the attempt**, including with automatic recovery enabled.
Downed captains cannot walk, jump or issue gameplay orders; normal creature
visual/effect updates continue. The revive aid shows a six-metre ground ring,
progress bar and remaining revival time for nearby downed teammates. The optional
automatic-recovery rule also shows its countdown. The ring matches the six-metre
revival rule. Duration and interruption rules remain unchanged.

## Verified scope and current limits

The [preview 11 validation report](validation-ga-preview.11.md) records native
enemy-targeting evidence separately from callback ABI fixtures. The final build
passes targeted two-player Mine guest/host acquisition, natural damage and
switching checks. This does not establish every authored AI command or adventure.
Passing relationship queries or sensing callbacks alone was insufficient to
establish native attack behavior.

The [preview 10 validation report](validation-ga-preview.10.md) separates follower
reproductions, portable and native ABI fixtures, and current Windows observations.
The tested creature and land-vehicle models move with native locomotion; residual
tracking lag remains material near interactions. Broader custom routes, authored
speeds/scales, water/flying movement, four-player follower behavior and complete
playthroughs require further coverage. Ordinary guest click/key target selection
and attack input remain unverified; the controlled combat/damage checks below do
not establish that input path. Earlier results retain their original version
labels and do not expand preview 10's recorded scope.

Preview 8's focused portable music tests and installed-data audit passed all
23 distinct music values across 69 stock scenarios, including the reported
The Eighth Day descriptor. These checks establish resource resolution, not
audible playback or complete adventure playthroughs. Both clients also matched
all eight authored music values in the copied Eighth Day native fixture. Its
[validation report](validation-ga-preview.8.md) records those checks and the
fixture limits. The results below retain their earlier preview labels.

Two-player Adventure Town testing has covered personal captain transfer, shared
entry, friendly fire at 0%, 25%, 50% and 100%, combat originating from either player,
environmental damage, healing, interrupted proximity revival and revival protection.
Automatic recovery restored 45/90 HP at the same position after 30.135 seconds.
Final host/guest actor sets matched at 439 actors with every guest actor bound to
its host counterpart; the guest vehicle AI filters were active.
Guest Move advanced act 0 to 1; Talk callbacks advanced act 1 to 2 and passed in act
2 after position/snapshot synchronization. A later ordinary mouse click on Mr.
Mayor opened the native conversation, displayed its cinematic/text and Next
pages, and advanced both players from act 1 to 2 when the guest finished it.
Ordinary conversations also completed all three act 2 Talk objectives and the
Carnie objective in act 3, with shared partial progress. A separate controlled
partial-goal check confirmed that the guest HUD shows the green completion mark
and green Gallery objective text after receiving host progress.
Allow a brief synchronization delay after moving before interacting.

In the stock Music adventure, an ordinary NPC click and the native Sing response
won Lotta's recruitment minigame, recruited the NPC and advanced both players from
act 1 to 2 without native faults. Friendship and recruitment state matched on
both players after the win. Ordinary neutral NPC Befriend also passed for Kid,
with matching impressed state and no changes to unrelated Talk objectives.
Simultaneous recruitment/Befriend minigames remain unvalidated.

A party wipe reached Mission Failed on both players. With **Any death fails**,
one captain's death failed both players at 100% friendly fire; self damage remained
unchanged. A controlled success reached
Mission Complete, allowed reward selection and saved 100 XP and one level on each
original captain after **Done**. Restarting both game processes retained those
values. Done returned both players to the menu after failure and success.
Each results flow suppressed its single-player score submission once per player.

Preview 4 also passed four-client Mr Puzzle entry and controlled act changes,
four distinct named captain cards, simultaneous replacement cancellation and
disconnect recovery. A missing-thumbnail fixture entered a shared native run.
These checks do not establish a complete adventure playthrough.

Preview 5 passed four-player beam-down confirmation with frozen clocks, shared
native objective conversations with independent pages, and host/guest colored
key and gate interactions in Mr Puzzle. A lost open-gate replica reconstructed
its authored open appearance and native state. Two-player checks covered repeated
Replay, guest-requested Replay, new/previous guest captain selection and matching
owner/remote scales. The final 405-actor test measured about 21.5 FPS per hidden
session, with advancing native time and no faults.

Preview 6 passed the stationary-baseline/cache-repair/movement tests, native
collector-only upgrades and teleporter cases, and four-player explosive cases at
friendly fire 0 and 100. Crate destruction and shared removal/effects also passed
with 5% packet loss and up to 80 ms jitter. The final candidate passed two shared
Replay rounds and genuine timed-bomb repick while preserving its original fuse.
Native authored Kill validation excludes typed powerups, including the stock
crate. Co-op preserves that rule; the crate tests establish damage, destruction
and shared state rather than a new objective eligibility rule. These fixture
results do not validate every custom objective or palette.

The success test used controlled completion. A complete adventure playthrough,
all authored objective types, simultaneous recruitment minigames, extended four-player
play and broader adventure compatibility still need validation. Automatic
transfers cover supported adventure and creation assets installed on the host;
server-only references cannot be downloaded automatically. Unsupported custom
gameplay props or act audio can block launch. External custom parts and other mod
dependencies may require matching local installations. The [validation notes](re/adventure.md#validation)
track the tested cases and remaining work.
