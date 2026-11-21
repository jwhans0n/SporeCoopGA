---
id: BUG-1279
title: Test Together hashes uninitialized native keys instead of the committed preview place
status: fixed
kind: desync
stage: Adventure
reported: 2026-11-20
source: Private native Test Together gate on source450886e0/47ed940a; group-launch-red instances53/54
role: both
versions: 0.7.15.0-ga-editor-preview.16; also present in b8f9a3ff
fixed_in: unreleased
branch: fix/test-together-place-0715
commits: 482fc92d7252e69182b113672ee1cb1d3577728e
tests: tests/adventure_place_tests.cpp; tests/adventure_editor_preview_windows_tests.cpp
signature: Both preview captains enter Scenario but advertise different changing place IDs and never create reciprocal puppets
docs: docs/reports/UPSTREAM-0.7.15-SYNC.md
related: BUG-1275; BUG-0963
switch: none (the off side hashes uninitialized memory; retained old-source executable provides the failing regression)
---

## Report
The real private two-player Test Together gate entered native Scenario state 3,
act 0 on both participants with one active coordinator run and fresh remote
player states, but remained at "waiting for shared gameplay." Neither player
had a reciprocal captain puppet. The guest received more than 650 NPC snapshots,
so entering native preview and receiving world traffic did not prove party
gameplay. Solo and the checked editor restoration passed separately. Tracked
publicly as https://github.com/jwhans0n/SporeCoopGA/issues/12.

## Root cause
Observed on production source 450886e0 / 47ed940a: the host and guest place IDs
differed (15E98797 / 9513BED7), later changed to 124DD3B7 / 927FD3B7, and both
native key diagnostics reported 0/0. Native faults remained zero. The retained
logs are under `out/windows-lab/ga0715-final-qualified/group-launch-red`, with
the original helper failure in `pair-playtests/result.json`.

Source inspection and a Win32 test of the actual modes.cpp functions confirm
that adventure_key returned the uint32 preview party place as bool without
assigning its instance/group output references. adventure_place then hashed
uninitialized locals. The error is also present in b8f9a3ff; it is not introduced
by the 0.7.15 merge. The existing party-place provider correctly fences stale
runs and holds. The ordinary native private-key difference in BUG-0963 is a
separate issue and is not changed here.

## Fix
Move the editor-preview override to adventure_place before native-key reading
and the forced-place diagnostic. A held or stale preview therefore keeps place
0 even when a forced place is set. adventure_key always reads the verified
native fields and clears both outputs on failure. Ordinary adventure key hashing
and forced-place semantics remain unchanged. There are no new native calls,
payload fields, protocol changes or save/recovery changes.

Do not expose an unsafe runtime toggle that deliberately hashes uninitialized
memory. The unchanged old-source fixture executable retains the failing behavior.

## Verification
FAIL: the unchanged production modes.cpp included directly in the new Win32
fixture fails 77 of 87 checks. This includes deterministic unassigned output
references, canonical preview place mismatches and failed native-read output
initialization. The old source, executable and RED logs are retained under
`out/upstream0715/network/adventure-place` in the primary workspace.

PASS: the same fixture passes all 87 checks after the narrow production change.
It covers different native keys, repeated canonical party places, genuine zero
keys, stale/held place 0 with forced-place precedence, ordinary hashing and
forced-place behavior, and missing/unreadable native mode, data and resource.
The existing actual native preview-provider fixture also passes all 112 checks.
It independently covers stale run/current-context fencing and gameplay readiness.
Both are compiled with the pinned llvm-mingw toolchain and warnings as errors.

The real two-player native Together reprobe and full final-source acceptance are
pending. The issue must remain open publicly until those gates pass; this local
fixed status records the implemented branch fix and focused regression only.

## History
- 2026-11-20: opened.
- 2026-11-20: Retained native and actual-function RED evidence, implemented the
  narrow consumer fix in an isolated worktree while the previous source remained
  frozen for real unit checks, and passed the focused consumer/provider fixtures.
