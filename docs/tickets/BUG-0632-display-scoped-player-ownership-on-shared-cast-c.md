---
id: BUG-0632
title: Display scoped player ownership on shared cast cards
status: open
kind: feature
stage: Adventure
reported: 2026-11-12
source: player
role: both
versions: 
fixed_in:
branch: fix/ga-editor-asset-browser
commits:
tests: tests/editor_creation_ui_windows_tests.cpp tests/editor_runtime_windows_tests.cpp
signature:
docs:
related:
---

## Report

Indicate which participant holds an asset content or properties lock.

## Root cause

Existing host-derived Class/Content scope summaries are available but the cast UI does not display them.

## Fix

Expose a typed cached per-asset view. Preserve separate Class/Content scope rules and independent selection; display ownership without granting capabilities.

## Verification
FAIL: out/preview12/evidence/native-ui-red.log (old custom toolbar). PASS: out/preview12/evidence/editor_creation_ui_tests.exe.log. Final native presentation and ownership acceptance remain pending.

## History
- 2026-11-12: opened.
