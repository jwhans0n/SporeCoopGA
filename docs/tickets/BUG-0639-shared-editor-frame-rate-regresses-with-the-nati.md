---
id: BUG-0639
title: Shared editor frame rate regresses with the native cast browser
status: open
kind: bug
stage: Adventure
reported: 2026-11-13
source: player
role: both
versions: 0.7.8.0-ga-editor-preview.12
fixed_in:
branch: fix/ga-editor-browser-runtime
commits:
tests:
signature:
docs:
related:
---

## Report
Both roles report sustained editor framerate loss in preview 12; the guest improves on non-creature viewer tabs. Installed logs show repeated 0.5-second hitches, host 2 FPS and guest 6 FPS, with zero native faults.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
