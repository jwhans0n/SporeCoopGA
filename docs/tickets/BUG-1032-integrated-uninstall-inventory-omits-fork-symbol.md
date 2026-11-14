---
id: BUG-1032
title: Integrated uninstall inventory omits fork symbol maps
status: fixed
kind: bug
stage: Installer
reported: 2026-11-13
source: integration-self-test
role: both
versions: 0.7.13.0-ga-editor-preview.15
fixed_in: unreleased
branch: sync/upstream-0.7.13-editor-main
commits: 66961c8b
tests: tests/win_tests.cpp; tools/tests/tools_selftest.py symlist
signature: uninstall leaves all seven fork-specific symbol maps
switch: none (inventory correction; actual pre-fix Win32 failure is retained)
docs: docs/reports/UPSTREAM-0.7.13-SYNC.md
related: BUG-0851
---

## Report
The upstream inventory checker finds seven fork-owned symbol maps missing from
both the C++ uninstall list and batch script. An independent Win32 fixture
installs those exact seven names into private temporary folders, then calls the
real uninstall function. All seven survive before the fix.

## Root cause
The new upstream exact cleanup inventory lists only its four symbol maps.
The setup and Win32 test targets also lacked the changed inventory header in
their build prerequisites, allowing a stale executable after a header-only fix.

## Fix
List all eleven shipped symbol maps explicitly in both uninstall paths. Retain
exact-name cleanup and preserve unrelated symbol files. Add the header build
prerequisites. The focused Win32 fixture uses independent literal fork names,
checks unrelated mine.sym survives, and exercises missing/in-use loader files
and the update lock. Its temporary deletion target is checked beneath the owned
workspace before launch; it starts no game or native setup window.

## Verification
FAIL: out/upstream0713/repository-tail-2.log (two inventory checks) and
out/upstream0713/uninstall-red.log, exit1 with seven retained-map failures.
The first green attempt still used the stale test executable; its failure is
retained as uninstall-green.log and prompted the dependency correction.
PASS: out/upstream0713/uninstall-green2.log, exit0, all seven maps removed and
unrelated symbols retained. Inventory checker: all three assertions pass.
Broader setup UI and actual batch execution are outside this focused fixture.

## History
- 2026-11-13: opened and reproduced during the pinned upstream integration.
