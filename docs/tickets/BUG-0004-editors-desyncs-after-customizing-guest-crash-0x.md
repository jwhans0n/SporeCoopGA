---
id: BUG-0004
title: Editors: desyncs after customizing, guest crash 0xE83EC8, guest cannot call a mate
status: fixed
kind: crash
stage: Cell
reported: 2026-10-24
source: player report (Reddit) + Spore Exception Report (guest, 13:43 on 2026-10-25, 0.7.2.1)
role: guest
versions: 0.7.2.1
fixed_in: 0.7.2.2
branch: fix/editor-desync
commits: 44653ae, d19f65f
tests: coop_editor_desync
signature: dc04aee5:0xE83EC8 read 0x9C (CreateCellObject with an unknown creation key)
docs: docs/re/cell.md 12, docs/re/creations.md 8
related:
---

## Report
After the host customized: the guest could not call a mate, had to rejoin to see changes, or crashed; in Creature the guest kept seeing the host's Cell-stage creature.

## Root cause
A new creation went out before it was on disk (0 records) and the guest forced the unknown key into CreateCellObject; the pool purge deleted the guest's own mate; a second vote raced; the guest's own editor opened too early; a rejoin could not rewrite open package files.

## Fix
creations.cpp sends a creation only with its records; the purge spares the mate; editor requests deduplicated; the own editor waits for the world; packages reused by path.

## Verification
coop_editor_desync.py FAIL before (guest fault at 0xE83EC8), 2 passes after.

## History
- 2026-10-24: reported (player report (Reddit) + Spore Exception Report (guest, 13:43 on 2026-10-25, 0.7.2.1)).
- 2026-10-24: fixed on fix/editor-desync, released in 0.7.2.2.
- 2026-10-25: ticket written from the CHANGELOG, commits and reports (backfill).
