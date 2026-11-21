---
id: BUG-0002
title: Cell: the other player does not see the meat of a kill
status: fixed
kind: desync
stage: Cell
reported: 2026-10-24
source: player report (Reddit, two brothers)
role: both
versions: 0.7.x
fixed_in: 0.7.2.2
branch: fix/cell-meat
commits: 58ed855
tests: coop_cell_meat
signature: 
docs: docs/re/cell.md 11
related:
---

## Report
When one player killed an enemy, the other did not see the meat.

## Root cause
A guest's lethal bite broke the mirror locally: the loot was spawned only on the guest and purged at the next state; the host just removed its creature (no death, no meat). Food from a chunk both ate counted twice.

## Fix
The guest spawns no gameplay loot; MSG_CELL_EAT carries kill/break; the host breaks or kills its own cell with the guest's puppet as attacker; deaths are replayed on mirrors; food gain is sent with the eaten cell.

## Verification
coop_cell_meat.py FAIL before (4/4 guest kills not killed on the host), OK 5x after.

## History
- 2026-10-24: reported (player report (Reddit, two brothers)).
- 2026-10-24: fixed on fix/cell-meat, released in 0.7.2.2.
- 2026-10-25: ticket written from the CHANGELOG, commits and reports (backfill).
