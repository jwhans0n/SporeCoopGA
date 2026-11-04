---
id: BUG-0487
title: Cold native reopen of terraformed Save changes decoded authored bytes
status: open
kind: bug
stage: GA shared editor
reported: 2026-11-04
source: C69 R261 saved Road native cold reopen
role: host
versions: private preview9 C69
fixed_in:
branch: feat/ga-editor-multi-act
commits:
tests:
signature: Unqualified decoded difference at 11104
docs:
related:
---

## Report
After two verified native Saves of the terraformed private Town draft, fresh-process native opening reaches the exact saved root and Ready, but the strict decoded comparison rejects differences beginning at11104. The test stops before hosting or sharing. Evidence: out/preview9-c69-road-cold-open, including the complete cold.native.bin and retained saved resource from out/preview9-c69-road-native-save-repeat.

## Root cause
Unassigned. The comparison reports saved bytes1442e88e00433b5fccbe0c94c6bf0b9e versus cold1642e88e02433b5fcdbe0c94c6bf0b9e at its first decoded mismatch. Do not infer a field, weaken equality or normalize float differences before tracing the native load/grounding boundary. Shared Save checkpoint publication itself passes under BUG-0486.

## Fix
No product fix yet. Preserve the exact saved DBPF, full saved/cold snapshots and failed comparator output for a focused native investigation.

## Verification
Native FAIL: out/preview9-c69-road-cold-open. No shared cold-reopen success is claimed.

## History
- 2026-11-04: opened.
