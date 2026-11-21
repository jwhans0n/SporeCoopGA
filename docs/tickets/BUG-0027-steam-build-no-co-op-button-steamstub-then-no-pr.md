---
id: BUG-0027
title: Steam build: no Co-op button (SteamStub, then no Present on Windows)
status: fixed
kind: bug
stage: Menu
reported: 2026-10-24
source: GitHub issue + player report
role: both
versions: 0.7.0
fixed_in: 0.7.2
branch: main
commits: cf1f779
tests: frames_loop
signature: 
docs: docs/05-re-notes.md, docs/02-modkit.md
related:
---

## Report
On the Steam build the mod loaded but there was no Co-op item.

## Root cause
The Steam exe is the GOG 2024 code under SteamStub (encrypted .text): symbols had to wait for the unpacked code (0.7.1); on some Windows setups Present was never hooked, so no frames reached the mod (0.7.2).

## Fix
SporeKit late boot after the SteamStub unpack (0.3.2); frames from App::Canvas::Tick when Present is lost (0.3.3).

## Verification
Tested offline on Steam under Proton and by the owner on Windows; frames_loop test.

## History
- 2026-10-24: reported (GitHub issue + player report).
- 2026-10-24: fixed on main, released in 0.7.2.
- 2026-10-25: ticket written from the CHANGELOG, commits and reports (backfill).
