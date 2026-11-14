---
id: BUG-1030
title: 0.7.13 integration conflicts with fork UI, keepalive diagnostics and message classification
status: fixed
kind: bug
stage: Integration
reported: 2026-11-13
source: Pinned upstream integration build
role: both
versions: 0.7.13.0-ga-editor-preview.15 integration
fixed_in: unreleased
branch: sync/upstream-0.7.13-editor-main
commits: 44dc57e3, 99fa3d01, 3d257400, f14f96a3
tests: make test; tests/net_tests.cpp; tests/validate_tests.cpp
signature: Windows integration build errors and custom-output setup resource paths
docs: docs/reports/UPSTREAM-0.7.13-SYNC.md
related:
switch: none (compile, link and fixture integration corrections, no runtime toggle)
---

## Report
The initial pinned-release merge fails the Windows build on an overlapping UI
local name, removed keepalive timestamp and exhaustive campaign-message switch.
After these fixes, the setup resource build fails when B points outside build/.
The first full repository run also finds protocol 28 literals in the synthetic
preview packaging fixtures while the real packaging recipe requires29.

## Root cause
Observed source integration gaps, not a reported installed-preview crash. The
new UI settings group and adventure group use the same local G identifier; the
fork replaced upstream keepalive timing with an owned PulseRun; upstream
message classification does not know the fork channels. Setup RC paths assume
the default output directory.

## Fix
Preserve independent UI groups, use main-thread diagnostic timing while owned
PulseRun scheduling remains intact, explicitly classify fenced fork channels,
and stage setup resources in B. Match synthetic packaging identities to protocol 29
and explicitly reject the immediately previous protocol 28. Decode source identity
files as UTF8 on Windows, including the new upstream localized text. Keep campaign traffic blocked during authoring
while permitting session metadata. Compile-time failures cannot have a runnable
old-behavior switch.

## Verification
FAIL: out/upstream0713/build-first.log and build-second.log.
PASS: build-verified.log and build-candidate.log, exit receipts 0.
FAIL: out/upstream0713/repository-first.log (stale packaging fixture identity).
PASS: tests/editor_preview_package_tests.py (16 tests, Windows) after 99fa3d01.
Full repository and native qualification are tracked separately in the sync report.

## History
- 2026-11-13: opened.

CI follow-up: run 43546502877 fails on parenthesized aggregate ObjectId
initialization in a terrain fixture under Ubuntu 22.04 Clang. Use brace
initialization with macro-safe expression parentheses in the terrain and native
grounding fixtures. Product ObjectId construction is unchanged.

The first affected Win32 fixture build also fails at link time: the included
new upstream input.cpp now needs vcursor_release and g_fix_wheelmk endpoints.
Update the controlled fixture endpoints and assert actual scheduled wheel
modifier/button flags and deferred cursor release. No OS hooks, physical input
or game processes are used by this test. FAIL: win32-affected-build-first.log.

Further Win32 fixture compilation identifies missing outpump/taskdrop controlled
endpoints and a strict dangling-else warning in native deferred destruction.
Retain upstream fix defaults, assert one deferred wake after the frame, and
brace the existing deferred-identity guard without changing its behavior.

PASS: win32-affected-result.json, all 11 executables after f14f96a3.
PASS: final product build at f14f96a3, build-final-exit.json exit 0.

CI run43547136833 also finds std::source_location unavailable in the
Ubuntu22.04 compiler/library combination. Preserve diagnostic call-site line
numbers using Clang __builtin_LINE(), and remove duplicate includes/log lines.
No transport timing, assertion or observed-state requirement is changed.
