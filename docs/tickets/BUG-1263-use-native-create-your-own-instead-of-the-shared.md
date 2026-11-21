---
id: BUG-1263
title: Use native Create Your Own instead of the shared editor creation toolbar
status: fixed
kind: feature
stage: Adventure
reported: 2026-11-12
source: player
role: both
versions: 
fixed_in: unreleased
branch: fix/ga-editor-asset-browser
commits: b736f8ec 5a35e3b4 b4b4bc13 86decc65
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
FAIL: out/preview12/evidence/native-ui-red.log (old custom toolbar). PASS: out/preview12/evidence/editor_creation_ui_tests.exe.log. Native presentation and ownership evidence, including the corrected visible caption, is recorded in docs/reports/EDITOR-NATIVE-FLOW-VALIDATION.md#editor-preview12-browser. Broader release acceptance remains deferred.

## History
- 2026-11-12: opened.

- 2026-11-12: implemented on the preview 12 review branch; public release and destination promotion remain deferred.

Legacy fork reference: `BUG-0630` before upstream 0.7.13 integration. Historical commits and OUT receipts retain that identity; upstream owns its independently created ticket under that number.

Legacy fork reference: `BUG-1018` before upstream0.7.15 integration. Prior aliases and historical commit/OUT identities remain unchanged; upstream owns its independently created ticket under that number.
