---
id: BUG-0630
title: Use native Create Your Own instead of the shared editor creation toolbar
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
tests: tests/editor_creation_ui_windows_tests.cpp tests/editor_creation_windows_tests.cpp
signature:
docs:
related:
---

## Report

Use the native Add Asset picker Create Your Own button instead of the custom creation toolbar.

## Root cause

The preview adds a separate New asset/family/variant menu. Existing native EditorRequest interception and immutable admission are reusable.

## Fix

Keep creation routing scoped to the shared adventure picker and existing participant-local creation sessions. Remove the custom New asset menus.

## Verification
FAIL: out/preview12/evidence/native-ui-red.log (old custom toolbar). PASS: out/preview12/evidence/editor_creation_ui_tests.exe.log. Final native presentation and ownership acceptance remain pending.

## History
- 2026-11-12: opened.
