---
id: BUG-1280
title: Shared editor keeps waiting-for-ownership text after no request remains
status: open
kind: bug
stage: Editor
reported: 2026-11-20
source: Private four-player Town qualification; retained host status after a 90-second observation
role: host
versions: 0.7.15.0-ga-editor-preview.16, source 4d4788a2
fixed_in:
branch: fix/test-together-place-0715
commits:
tests: tools/tests/coop_editor_party.py; retained four-player native observation
signature: Waiting for editing ownership remains visible with every member ownership 0/pending 0 and native gesture 0
docs: docs/reports/UPSTREAM-0.7.15-SYNC.md
related: BUG-1275
---

## Report
During a private four-player shared-editor check, the host displayed
"Waiting for editing ownership" after a Captain portrait drag. The same text
remained after the 90-second wait even though all four members reported
ownership 0 and pending 0, the host had no native gesture or transaction pending,
and the draft stayed responsive and unfrozen. This ticket tracks stale status
text. It does not establish a Captain movement or ownership failure.

## Root cause
Observed on source 4d4788a2 with coop.dll SHA256
7bd89bdabc2cdef9e58abcf9038ae23453f538c42f6c33d33c38a88cd3f1cb1c:
all four participants remained at durable revision 8 with two unchanged history
entries and zero captured native faults. Host generation 8 and restores 13 were
unchanged; captures increased from 58 to 60 and rejected from 0 to 1. The baseline status was
"The editing lease was already released or expired." The final status was
"Waiting for editing ownership," while every member's ownership and pending
counts were zero. The host also reported native_pending 0 and gesture 0.

Source fact: editor_runtime.cpp's lease function assigns the waiting text after
an accepted asynchronous request. Its successful complete branch returns true
without clearing that text. Native retained-creation retirement also has paths
that return without publishing a replacement status. This permits an old waiting
caption to outlive its request or gesture. The exact intermediate grant outcome
is not established by the final empty lease snapshot.

The attempted portrait drop ended on a rock edge in this Town view. An invalid
native placement can legitimately produce no edit. The separate movement gate
requires a retry on verified clear land; this status issue must not be presented
as proof of a lost gesture, incorrect Captain domain or failed restoration.

## Fix
Open. Derive the waiting caption from the current pending ownership/retained
input state, or retire this specific caption when that state ends through grant,
denial, cancellation, expiry or a native no-op. Preserve newer unrelated status
messages. Do not change ownership authorization or force an edit for an invalid
native placement.

## Verification
Observed stale-status evidence:
`out/windows-lab/ga0715-fixed/town-four-captain/result.json` and
`failure-native-state.json`, with the retained host/client native logs and
`out/windows-lab/ga0715-fixed/four-caption-readonly/53-current.png` in the
integration worktree. The strict fixture timed out waiting for a transaction;
that timeout is not itself evidence of a product movement defect.

The read-only evidence summary is
`out/upstream0715/captain-town-readonly-audit/findings.json` in the primary
workspace. No fix or passing stale-status regression is claimed. No installed
preview reproduction is claimed, and installed payloads were not changed.

## History
- 2026-11-20: Opened from a bounded native four-player observation. Searched
  existing ticket/known-issue text before allocating this separate status issue.
  The active campaign/editor source remains unchanged; this record was prepared
  in an isolated worktree.
