---
id: BUG-0005
title: Crash reports: the minidump can be empty and the archive is not made
status: fixed
kind: bug
stage: All
reported: 2026-10-24
source: lab test diag_check --crash
role: both
versions: 0.7.2.1
fixed_in: 0.7.2.2
branch: main
commits: 084cce7
tests: diag
signature: 
docs: docs/02-modkit.md 2.7
related:
---

## Report
The diag test found no minidump and no report zip after a crash on one lane.

## Root cause
The crash report thread passed its own thread id to MiniDumpWriteDump; the dump failed and the steps after it never ran.

## Fix
diag.cpp: the crashed thread's id; report, archive and the next-start mark first, the minidump last, then the archive again.

## Verification
diag 4/4 after the fix.

## History
- 2026-10-24: reported (lab test diag_check --crash).
- 2026-10-24: fixed on main, released in 0.7.2.2.
- 2026-10-25: ticket written from the CHANGELOG, commits and reports (backfill).
