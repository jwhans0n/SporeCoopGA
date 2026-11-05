---
id: BUG-0540
title: Captain drag is rejected by placement-only staged capture
status: fixed
kind: desync
stage: GA shared editor
reported: 2026-11-04
source: Player preview9 logs: staged non-placement rejection followed by captain restoration mismatch
role: both
versions: 0.7.5.0-ga-editor-preview.9
fixed_in: 0.7.5.0-ga-editor-preview.10 (private testing)
branch: feat/ga-editor-multi-act
commits: 4dc8c64a
tests: tests/editor_native_cancellation_windows_tests.cpp, tests/editor_native_captain_grounding_fixture.h, tests/editor_runtime_windows_tests.cpp; docs/reports/EDITOR-PREVIEW10-VALIDATION.md
signature:
docs:
related: BUG-0541
---

## Report
Both roles report rejected captain movement and recovery notices in preview 9. The preserved November 4 session logs contain `A staged pose changed a non-placement subtree`. Other gestures fail with missing authored-settings ownership and, in two sessions, `Native act/captain settings differ from the committed draft` during restoration. The latter issue is investigated separately in BUG-0541.

## Root cause
Observed in production source and a controlled actual-i686 input regression: the Captain selection acquires a Settings lease, but `ScopedPoseInput` marks its StartHistory boundary as an ordinary pose candidate. The staged runtime intentionally accepts placement-only mutations and rejects the changed Captain subtree. The input domain must be pinned before the native history start. A large-scene C70 guest drag instead succeeds, so that native run is retained as a non-reproduction rather than counted as a failing test.

## Fix
Pin the local gesture domain before native StartHistory. Captain poses use the full Settings capture and existing Captain ownership guards; only Placement poses enroll in placement-only staging. Carry the local domain to bounded rejection diagnostics. No wire, checkpoint or save fields change. The original press, movement and release remain queued through ownership latency; no native mutation precedes permission.

## Verification
RED: `out/preview10-captain-red-r2/run.log`, assertion `!pending.front().pose_candidate` fails on the original source. GREEN: `out/preview10-captain-green/run.log`, 278 focused checks cover immediate ownership and release before ownership. The final actual-i686 O0 and O1 native adapter fixtures each pass 161,123 checks. `out/preview10-c70-captain-native-red/` records a successful old-build drag on the large private draft and is not a RED result. The runtime rejection-restoration fixture passes 13,231 checks with bounded diagnostics; Native host and guest drag/history cycles verify the actual Captain actor and observer positions, complete authored semantic hashes and preserved appearance/health/lock. See `docs/reports/EDITOR-PREVIEW10-VALIDATION.md` for final payload qualification.

## History
- 2026-11-04: opened.
- 2026-11-05: fixed on the feature branch; private testing delivery, no public release claim.
