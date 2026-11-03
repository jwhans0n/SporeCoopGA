---
id: BUG-0168
title: Creature: NPC aggression towards a partner is untested end to end
status: open
kind: test
stage: Creature
reported: 2026-10-25
source: docs/known-issues.md (ticket backfilled 2026-10-27)
role: 
versions: 0.7.2.4 (main) and earlier
fixed_in:
branch:
commits:
tests:
signature:
docs:
related:
---

## Report
Copied from docs/known-issues.md, where the entry stays up to date:

* Since 2026-10-25 an NPC on the host that is nearer to a partner's creature ticks its AI with that
  creature as the player (docs/re/creature.md 14.2): it notices the partner (checked: the "noticed"
  stimulus names the puppet, `coop_creature_kills.py`), and retaliation and epics hurt the partner
  (they did before too). That an NPC of an aggressive archetype then attacks the partner on sight
  is not checked: no such species in the `creature` fixture attacked even the host's avatar on
  sight, and spawned epics (`coop.npc epic`) do not attack the avatar.
* To check it: a save with an aggressive (red face) nest nearby, the guest next to it and the host
  60 m away: `damage_in` on the guest should grow. Side effects to watch: avatar-only UI of the
  host (hints, music) while an NPC hunts the partner.

## Root cause
See the report (observed vs inferred as marked there).

## Fix
Not fixed yet.

## Verification

## History
- 2026-10-25: first recorded in docs/known-issues.md.
- 2026-10-27: ticket backfilled from docs/known-issues.md (every entry carries its ticket id, CLAUDE.md 1).
- 2026-10-28: fix/w4-tests (wave 4): not reached. Needs a save with an aggressive (red face) nest near the avatar; the creature fixture has none and making one means playing to it. Left open (a fixture task, not a test-only change).
