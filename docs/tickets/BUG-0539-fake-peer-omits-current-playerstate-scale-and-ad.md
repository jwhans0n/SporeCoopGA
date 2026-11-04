---
id: BUG-0539
title: Fake peer omits current PlayerState scale and adventure identity tail
status: fixed
kind: tooling
stage: Network
reported: 2026-11-04
source: Protocol26 actual C++ Session fake-peer check
role: guest
versions: shared editor protocol26 integration
fixed_in: unreleased
branch: feat/ga-editor-multi-act
commits:
tests: tools/tests/fakepeer_check.py
signature:
docs:
related:
---

## Report
The protocol26 real C++ Session fixture accepts the fake peer, chat and leave, but records no player states: joined1/state0/chat1/left1. Evidence: out/preview9-upstream074-integration/fakepeer-red.log.

## Root cause
pack_state still sends the legacy67-byte shape. Current PlayerState::read requires95 bytes, including scale and three64-bit adventure identities. Updating only the protocol header exposes this previously stale helper.

## Fix
Default to protocol26, permit the explicitly reviewed legacy6/7 and preview23-26 choices, and append scale1 plus zero run, captain-selection and movement generations for current preview packets. Generic bots receive no authored captain identity. Preserve legacy6/7 payloads.

## Verification
FAIL: out/preview9-upstream074-integration/fakepeer-red.log. PASS: out/preview9-upstream074-integration/fakepeer-green.log. The actual C++ Session verifies handshake, decoded player state, chat, leave, current default protocol and build mismatch.

## History
- 2026-11-04: opened.
