---
id: BUG-0634
title: Raw-pointer coop.ui btn diagnostic dereferences unverified input
status: fixed
kind: crash
stage: Tests
reported: 2026-11-12
source: internal
role: both
versions: 
fixed_in: unreleased
branch: fix/ga-editor-asset-browser
commits: 9bd8ee07
tests: tests/sui_tree_root_tests.cpp
signature:
docs:
related:
---

## Report
A private native diagnostic crashed after coop.ui btn received the browser control ID C00F0401 instead of an IWindow address. This was test-operator error, separate from card dragging.

## Root cause
Observed: the btn branch parsed a raw integer and immediately dispatched QueryInterface without verifying current native-tree membership. Crash-20261112-153833-i45 reads C00F0401 in coop.dll.

## Fix
Reuse the existing complete current-window-tree resolver before btn, font and show dereference a pointer. Control IDs and stale or unrelated pointers reject without native dispatch.

## Verification
FAIL: out/preview12/evidence/crash-20261112-153833-i45.txt. PASS: out/preview12/evidence/sui_tree_root_tests.exe.log, 143 checks, and guarded native diagnostic rejection. The poisoned private game run is excluded from gameplay acceptance.

## History
- 2026-11-12: opened.

- 2026-11-12: implemented on the preview 12 review branch; public release and destination promotion remain deferred.
