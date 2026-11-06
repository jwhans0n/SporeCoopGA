---
id: BUG-0626
title: Upstream synchronization cannot qualify Windows smoke without native fixtures
status: open
kind: bug
stage: Validation
reported: 2026-11-05
source: Local 0.7.8 integration smoke preflight
role: both
versions: 
fixed_in:
branch: sync/upstream-0.7.8-main
commits:
tests:
signature:
docs:
related:
---

## Report
The Windows checkout has no native base-stage fixture archives required by the pinned upstream smoke suite. The integration can build and run portable/Win32 fixtures, but cannot claim the required gameplay smoke gate.

## Root cause
The shared fixture directory and both existing worktree artifact roots contain no creature, cell, tribe, civ or space .tgz fixtures. Linux Wine/Bottles is also absent. The Windows runner must fail before modifying profiles when prerequisites are missing.

## Fix
No gameplay or smoke assertion is weakened. Retain the source synchronization on review branches until the required fixture-backed native scenarios pass; destination main remains unchanged.

## Verification
Verification evidence belongs in the upstream synchronization report. Missing prerequisites are UNAVAILABLE, never PASS. No native acceptance is claimed from build or deterministic tests alone.

## History
- 2026-11-05: opened.
