---
id: BUG-0003
title: Join: the guest is put into an older game of the host
status: fixed
kind: bug
stage: Menu
reported: 2026-10-24
source: player report (Reddit, two brothers)
role: guest
versions: 0.7.x
fixed_in: 0.7.2.2
branch: fix/join-new-game
commits: 7f14562
tests: coop_join_new_game
signature: 
docs: docs/07-modes.md 7.2
related:
---

## Report
The host started a new game, but the guest joined an old save of the host; deleting the old save "fixed" it.

## Root cause
A GameN slot is a galaxy with several planets (.spo). The planet name was read as ASCII (Cyrillic became "_"), the file was not found and the host fell back to another planet of the slot; the guest built the load name from UTF-8 bytes; safe_rel rejected names with "!".

## Fix
world.cpp/modes.cpp: CurrentGameName read as UTF-8, the host sends only the played planet (saves again if it has no file), the guest loads exactly that planet.

## Verification
coop_join_new_game.py FAIL on the old build, 2/2 OK (Cell, "Земля") and OK with "Zoë Prime!". The exact player trigger was not reproduced.

## History
- 2026-10-24: reported (player report (Reddit, two brothers)).
- 2026-10-24: fixed on fix/join-new-game, released in 0.7.2.2.
- 2026-10-25: ticket written from the CHANGELOG, commits and reports (backfill).
