---
id: BUG-0181
title: Space: no test save after the tutorial missions
status: open
kind: test
stage: Space
reported: 2026-10-27
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

* The `space` fixture starts at the first mission ("Fly to planet Vulpica", the wreck): the ship has
  no interstellar drive (tests add it with `coop.space tool add interstellardrive`), no
  terraforming tools, no colony besides home and no other empire has been met. `space_empire`
  (docs/06-testing.md 6.15) adds the drive, money, two colonies with spice and two contacts, made
  with test commands; it is still inside the tutorial (travel to stars that are not own stars is
  refused there), and has no raids, missions or trade. A save played through the tutorial would
  still be better.

## Root cause
See the report (observed vs inferred as marked there).

## Fix
Not fixed yet.

## Verification

## History
- 2026-10-27: first recorded in docs/known-issues.md.
- 2026-10-27: ticket backfilled from docs/known-issues.md (every entry carries its ticket id, CLAUDE.md 1).
- 2026-10-28: fix/w4-tests (wave 4): not reached: a save played through the Space tutorial is a fixture task (hours of play by a bot or by hand). Left open.
