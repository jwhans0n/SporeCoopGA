# Galactic Adventures co-op: experimental guide

This experimental feature is developed on `feat/galactic-adventures-coop`.
The preview covers the verified cases below; broader compatibility is still being
tested. The native adventure adapter is currently
verified only for the March 2017 GOG executable, SHA-256
`dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`.
See [build identification](re/build-dc04.md). Every player needs Galactic Adventures
and the same experimental SporeCoop build.

Preview `0.7.0-ga-preview.4` adds the shared adventure/captain lobby and fixes the
native music setting used by Mr Puzzle's Magical Gates. It retains native captain
creation, cancelled adventure recovery and the large-adventure performance fix.
Install the same preview for every player; this version uses network protocol 8.

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
   Play begins after every participant has the content, captain and loaded adventure.

The host controls shared objective and act progress. A qualifying action by any
teammate can advance the party's goals. Late joins and reconnects wait for the next
attempt. Entry from the Space stage is outside this version's scope.
Cancelling preparation returns both players to the native galaxy menu. Successfully
saved captains remain available when retrying the same adventure.
**Change captain** opens your native picker. Changing a captain clears everyone's
readiness; cancelling a replacement keeps the previous saved captain. Locked
adventures show the authored captain and disable replacement. **Cancel adventure**
returns the party to the native galaxy menu while keeping the party connected.
Use **Done** to return to the menu between completed attempts; native Replay navigation
still needs validation.

## Difficulty

Settings are fixed during an attempt. Friendly fire between party captains offers
**0%, 25%, 50% or 100%**, with **0% as the default**. Enemy health defaults to
`1 + 0.5 * (players - 1)`; enemy damage defaults to 1x. Host overrides replace these
defaults.

| Death rule | What happens |
|---|---|
| **Teammate revival** (default) | An alive teammate stays within 2 metres for 3 seconds to revive you. |
| **Teammate revival + automatic recovery** | The same proximity revival is available; otherwise you recover after 30 seconds at the downed position. |
| **Any death fails** | One captain's death ends the party's attempt. |

Revival restores 50% health and grants 2 seconds of damage protection. Damage to the
rescuer, moving out of range or disconnecting resets revival progress. **Everyone
being downed always fails the attempt**, including with automatic recovery enabled.

## Verified scope and current limits

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
Simultaneous social interactions remain unvalidated.

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

The success test used controlled completion. A complete adventure playthrough,
all authored objective types, simultaneous conversations, extended four-player
play and broader adventure compatibility still need validation. Automatic
transfers cover supported adventure and creation assets installed on the host;
server-only references cannot be downloaded automatically. Unsupported custom
gameplay props or act audio can block launch. External custom parts and other mod
dependencies may require matching local installations. The [validation notes](re/adventure.md#validation)
track the tested cases and remaining work.
