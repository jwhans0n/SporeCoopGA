---
id: BUG-0627
title: Merged transport fixture stalls while receiving queued reliable data
status: fixed
kind: desync
stage: Networking
reported: 2026-11-05
source: 0.7.8 editor integration Win32 session_transport_tests
role: 
versions: 
fixed_in: unreleased
branch: sync/upstream-0.7.8-editor
commits: 498164cc
tests: tests/session_transport_tests.cpp
signature:
docs:
related:
---

## Report
The merged Win32 transport fixture times out in receive_until at caller135. Both transport and protocol compile; native adapter and focused runtime fixtures pass.

## Root cause
Upstream bulk_acks intentionally retries a lost reliable message only after newer acknowledged traffic or at least1000ms of silence. The fixture sends no ACK and expects its former250ms retry. Its later LAN expectation also includes deliberate long-delay RTT samples. This is a fixture-policy mismatch, not evidence of a gameplay transport failure.

## Fix
Keep production retry policy intact. Assert no early retry without ACK evidence, then exercise a fresh-time send-only flush after the actual silent interval. Keep the shared packet-budget boundary after retries become eligible. Train the real loopback RTT with fresh traffic before asserting the LAN branch; use no private RTT setter.

## Verification
FAIL: out/upstream078/session-transport-red.log and transport-diagnosis.log. PASS: out/upstream078/transport-green.log, 3616 actual-Win32 checks pass. The fixture was also exercised by the portable repository suite.

## History
- 2026-11-05: opened.
