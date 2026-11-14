---
id: BUG-1030
title: 0.7.13 integration conflicts with fork UI, keepalive diagnostics and message classification
status: open
kind: bug
stage: Integration
reported: 2026-11-13
source: Pinned upstream integration build
role: both
versions: 0.7.13.0-ga-editor-preview.15 integration
fixed_in:
branch: sync/upstream-0.7.13-editor-main
commits:
tests: make test; tests/net_tests.cpp; tests/validate_tests.cpp
signature: Windows integration build errors and custom-output setup resource paths
docs: docs/reports/UPSTREAM-0.7.13-SYNC.md
related:
---

## Report
The initial pinned-release merge fails the Windows build on an overlapping UI
local name, removed keepalive timestamp and exhaustive campaign-message switch.
After these fixes, the setup resource build fails when B points outside build/.
The first full repository run also finds protocol28 literals in the synthetic
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
and stage setup resources in B. Match synthetic packaging identities to protocol29
and explicitly reject the immediately previous protocol28. Decode source identity
files as UTF8 on Windows, including the new upstream localized text. Keep campaign traffic blocked during authoring
while permitting session metadata. Compile-time failures cannot have a runnable
old-behavior switch.

## Verification
FAIL: out/upstream0713/build-first.log and build-second.log.
PASS: build-verified.log and build-candidate.log, exit receipts 0.
FAIL: out/upstream0713/repository-first.log (stale packaging fixture identity).
Full repository and native qualification are tracked in the sync report.

## History
- 2026-11-13: opened.
