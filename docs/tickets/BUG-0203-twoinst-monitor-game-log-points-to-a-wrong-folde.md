---
id: BUG-0203
title: twoinst Monitor.game_log points to a wrong folder when a test runs from a git worktree
status: duplicate
kind: tooling
stage: All
reported: 2026-10-27
source: fix/w2-world (wave 2)
role: 
versions: 
fixed_in:
branch:
commits:
tests:
signature:
docs:
related: BUG-0068
---

## Report
Found while writing coop_world_queue.py on fix/w2-world: in a git worktree
(`../SporeCoop-wt/<branch>`, which is how the lab tests branches) `twoinst.Monitor.game_log()`
returns `.../SporeCoop-wt/SPORE-lab/laneN/...`, a folder that does not exist (checked: lane 3,
instance 5). `Monitor.loading()` then always says "not loading", so the freeze monitor uses the
short FREEZE limit instead of LOAD_FREEZE (60 s) during a world load: a slow load in a worktree run
can be reported as a freeze.

## Root cause
`root = os.path.dirname(lane.ROOT)` assumes the repository is the main checkout next to the game
(`SPORE/SporeCoop`). coop_rejoin_net.py derives the game folder from
`git rev-parse --git-common-dir` instead, which works in worktrees. Not fixed (tooling, not in
the wave 2 world area).

## Fix
Duplicate of BUG-0068 (same code, same cause), fixed there on fix/w4-lab: `lane.game_log()`.

## Verification
<!-- the test: FAIL without the fix (run dir), PASS with it (runs) -->

## History
- 2026-10-27: opened.
- 2026-10-28: closed as duplicate of BUG-0068 (fix/w4-lab).
