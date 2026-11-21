---
id: BUG-1269
title: Windows sharing violations interrupt native fixture checkpoints
status: fixed
kind: test
stage: Tools
reported: 2026-11-12
source: internal
role: both
versions: preview12 qualification harness
fixed_in: unreleased
branch: fix/ga-editor-asset-browser
commits: e067c51b
tests: tools/tests/coop_editor_creation_save.py --self-test
signature:
docs:
related:
---

## Report
The staged creation fixture intermittently stopped while replacing its checkpoint on Windows, before any native construction input was sent. The original failure is retained in out/preview12/c815-46-Entertainment-steps/result.json.

## Root cause
Observed: an atomic Path.replace can fail with Windows error 5 or 32 while another reader briefly holds the destination without delete sharing. A real CreateFileW sharing-denial probe reproduced error 5. This is a checkpoint filesystem failure, not a game Save rejection.

## Fix
Retry only replacement of the already-written immutable checkpoint bytes, with eight bounded attempts and exponential backoff. Other errors and persistent sharing failures still fail. Do not replay native input or recreate the workflow. The retry covers both the result and workflow-head replacement.

## Verification
FAIL/PASS: out/preview12/checkpoint-sharing-probe/result.json records the actual Windows sharing fault followed by successful bounded retry after the reader releases its handle. No native input was sent by the probe. Five direct retry contracts cover transient errors 5/32, permanent failures and unrelated errors; out/preview12/evidence/checkpoint-retry-selftest.log reports 728 offline checks passed.

## History
- 2026-11-12: opened.

- 2026-11-12: implemented on the preview 12 review branch; public release and destination promotion remain deferred.

Legacy fork reference: `BUG-0636` before upstream 0.7.13 integration. Historical commits and OUT receipts retain that identity; upstream owns its independently created ticket under that number.

Legacy fork reference: `BUG-1024` before upstream0.7.15 integration. Prior aliases and historical commit/OUT identities remain unchanged; upstream owns its independently created ticket under that number.
