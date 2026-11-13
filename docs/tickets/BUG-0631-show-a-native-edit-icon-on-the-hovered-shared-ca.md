---
id: BUG-0631
title: Show a native Edit icon on the hovered shared cast card
status: fixed
kind: feature
stage: Adventure
reported: 2026-11-12
source: player
role: both
versions: 
fixed_in: unreleased
branch: fix/ga-editor-asset-browser
commits: b736f8ec 9bd8ee07 ec6793d0 ffc73c44 0d43b5bc da29cef4
tests: tests/editor_creation_ui_windows_tests.cpp
signature:
docs:
related:
---

## Report

Show the native Edit icon at the bottom-right of the hovered cast card, using native tooltip and states.

## Root cause

The preview Edit asset toolbar resolves current inspector/selection rather than the hovered card.

## Fix

Bind native visible card identities, pin content revision on activation, reuse native edit presentation and clean up on UI/context replacement.

## Verification
FAIL: out/preview12/evidence/native-ui-red.log (old custom toolbar). PASS: out/preview12/evidence/editor_creation_ui_tests.exe.log. Native presentation and ownership evidence, including the corrected visible caption, is recorded in docs/reports/EDITOR-NATIVE-FLOW-VALIDATION.md#editor-preview12-browser. Broader release acceptance remains deferred.

## History
- 2026-11-12: opened.

- 2026-11-12: implemented on the preview 12 review branch; public release and destination promotion remain deferred.
