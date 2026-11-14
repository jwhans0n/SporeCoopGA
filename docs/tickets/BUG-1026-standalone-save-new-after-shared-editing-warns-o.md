---
id: BUG-1026
title: Standalone Save New after shared editing warns of unavailable custom creations on reopen
status: open
kind: bug
stage: Adventure
reported: 2026-11-12
source: internal
role: host
versions: 0.7.8.0-ga-editor-preview.12 candidate 93e52aaf
fixed_in:
branch: fix/ga-editor-asset-browser
commits:
tests: out/preview12/ordinary-two-act-save-confirm/ out/preview12/last-two-act-bootstrap/
signature:
docs:
related:
---

## Report
After ending shared editing, an ordinary unshared Save New of a private two-act copy reported success. Cold opening that new copy displayed the native warning that some creations were banned and would be replaced. Replacement was declined. The previous coordinated shared Save and its checkpoint remain separate verified evidence.

## Root cause
Unclassified. The warning is observed, not proof of an online ban or a particular missing dependency. One hypothesis is that the ordinary new root is not associated with the former shared asset versions/dependency receipt. That association has not been inspected or established as the cause.

## Fix
Unfixed and retained outside browser qualification. Do not accept native replacement as a successful preservation/reopen check. Restore the verified shared checkpoint before further browser tests.

## Verification
FAIL: out/preview12/last-two-act-bootstrap/result.json, with the native warning retained in out/preview12/last-verified-recovery/declined-warning.txt. The ordinary Save New success is separately recorded in ordinary-two-act-save-confirm/result.json. No passing preservation check is claimed for that copy. Shared checkpoint revision 13 and actual native observers restored in last-verified-recovery/result.json.

## History
- 2026-11-12: opened.

Legacy fork reference: `BUG-0638` before upstream 0.7.13 integration. Historical commits and OUT receipts retain that identity; upstream owns its independently created ticket under that number.
