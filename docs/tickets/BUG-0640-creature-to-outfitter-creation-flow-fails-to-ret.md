---
id: BUG-0640
title: Creature to outfitter creation flow fails to return to shared adventure editing
status: open
kind: bug
stage: Adventure
reported: 2026-11-13
source: player
role: guest
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
A guest created a creature, switched into its outfitter, then returned to the adventure. The installed log reports that the native creation editor returned without its owned result; restoration failed and the host requested explicit recovery.

## Root cause
<!-- observed (runtime, logs, disassembly) vs inferred -->

## Fix
<!-- what changed and why; the switch to turn it off for tests (coop.fix <name>) -->

## Verification
<!-- the test: FAIL without the fix, PASS with it, each with its lab run id (out/lab/YYYYMMDD-HHMMSS);
     or a line `no-fail-run: <reason>` (docs only, regression only, not reproducible) -->

## History
- 2026-11-13: opened.
