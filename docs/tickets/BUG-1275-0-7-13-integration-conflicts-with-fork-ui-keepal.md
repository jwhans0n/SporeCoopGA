---
id: BUG-1275
title: 0.7.13 integration conflicts with fork UI, keepalive diagnostics and message classification
status: fixed
kind: bug
stage: Tools
reported: 2026-11-13
source: Pinned upstream integration build
role: both
versions: 0.7.13.0-ga-editor-preview.15 integration
fixed_in: unreleased
branch: sync/upstream-0.7.13-editor-main
commits: 44dc57e3, 99fa3d01, 3d257400, f14f96a3, 66961c8b
tests: make test; tests/net_tests.cpp; tests/validate_tests.cpp
signature: Windows integration build errors and custom-output setup resource paths
docs: docs/reports/UPSTREAM-0.7.13-SYNC.md
related:
switch: none (compile, link and fixture integration corrections, no runtime toggle)
---

## Report
The initial pinned-release merge fails the Windows build on an overlapping UI
local name, removed keepalive timestamp and exhaustive campaign-message switch.
After these fixes, the setup resource build fails when B points outside build/.
The first full repository run also finds protocol 28 literals in the synthetic
preview packaging fixtures while the real packaging recipe requires29.

## Root cause
Observed source integration gaps, not a reported installed-preview crash. The
new UI settings group and adventure group use the same local G identifier; the
fork replaced upstream keepalive timing with an owned PulseRun; upstream
message classification does not know the fork channels. Setup RC paths assume
the default output directory.

## Fix
Preserve independent UI groups, use main-thread diagnostic timing while owned
PulseRun scheduling remains intact, explicitly classify fenced fork channels,
and stage setup resources in B. Match synthetic packaging identities to protocol 29
and explicitly reject the immediately previous protocol 28. Decode source identity
files as UTF8 on Windows, including the new upstream localized text. Keep campaign traffic blocked during authoring
while permitting session metadata. Compile-time failures cannot have a runnable
old-behavior switch.

## Verification
FAIL: out/upstream0713/build-first.log and build-second.log.
PASS: build-verified.log and build-candidate.log, exit receipts 0.
FAIL: out/upstream0713/repository-first.log (stale packaging fixture identity).
PASS: tests/editor_preview_package_tests.py (16 tests, Windows) after 99fa3d01.
Full repository and native qualification are tracked separately in the sync report.

## History
- 2026-11-13: opened.

CI follow-up: run 43546502877 fails on parenthesized aggregate ObjectId
initialization in a terrain fixture under Ubuntu 22.04 Clang. Use brace
initialization with macro-safe expression parentheses in the terrain and native
grounding fixtures. Product ObjectId construction is unchanged.

The first affected Win32 fixture build also fails at link time: the included
new upstream input.cpp now needs vcursor_release and g_fix_wheelmk endpoints.
Update the controlled fixture endpoints and assert actual scheduled wheel
modifier/button flags and deferred cursor release. No OS hooks, physical input
or game processes are used by this test. FAIL: win32-affected-build-first.log.

Further Win32 fixture compilation identifies missing outpump/taskdrop controlled
endpoints and a strict dangling-else warning in native deferred destruction.
Retain upstream fix defaults, assert one deferred wake after the frame, and
brace the existing deferred-identity guard without changing its behavior.

PASS: win32-affected-result.json, all 11 executables after f14f96a3.
PASS: final product build at f14f96a3, build-final-exit.json exit 0.

CI run43547136833 also finds std::source_location unavailable in the
Ubuntu22.04 compiler/library combination. Preserve diagnostic call-site line
numbers using Clang __builtin_LINE(), and remove duplicate includes/log lines.
No transport timing, assertion or observed-state requirement is changed.

Legacy fork reference: `BUG-1030` before upstream0.7.15 integration. Prior aliases and historical commit/OUT identities remain unchanged; upstream owns its independently created ticket under that number.

## Upstream 0.7.15 network integration (2026-11-20)

Protocol 30 retains upstream HOST_BUSY 70 and TRIBE_ANIM 71, moves Adventure to
72 and Editor to 73, and keeps Activity 55. The generated manifest declares
Session 2.0 and mandatory Adventure 11 / Editor 12 families at 1.0. The full
103-byte PlayerState and the fork's shared peer lifetime, monotonic transport
generations and bounded send-only pumps remain intact. New upstream admission,
rate limits, rejoin identity and optional campaign-family checks are preserved.

FAIL before the admission guard: `out/upstream0715/network/required-families-fail.log`,
all 12 missing/duplicate/incompatible capability cases join incorrectly when the
optional upstream family check is off. A second failing reproduction,
`reconnect-required-families-fail.log`, shows an incompatible HELLO at the same
endpoint retiring an accepted peer. The hard checks now precede rejoin eviction
and WELCOME acceptance, independent of optional campaign checking.

PASS: `net-all-shards.log` (all eight portable network shards), `validate.log`
(all 256 message identities/classes and sender/receiver routes), and `fakepeer.log`
(real UDP handshake, full state, chat, leave and build mismatch).
PASS: `network-win32-focused.log`, native Win32 loopback checks for independent
70/71/72/73 routes, strict state payload, required families in both roles, rejoin
replacement, callbacks that restart sessions and packet delivery teardown.
Both portable and Win32 builds use `-Wall -Wextra -Werror`.
Windows Python proxy checks pass (7 tests); preview 16 packaging checks pass
(16 tests), without changing an installed game. These are network/packaging
fixtures, not native game smoke or main promotion evidence.

Windows lint follow-up: relative paths used native backslashes while the
ratchet baseline, planted-failure expectations and retired-switch history
exclusions use repository slashes. FAIL: `network/ratchet-selftest.log` (three
correctly detected violations reported as missed) and `network/fix-retire.log`
(510 historical ticket/archive false positives). Canonicalize the tools' relative
path keys and reports; keep every lifetime, guard, lifecycle and retired-switch
check enabled. The separate 11 additional fork object_alive sites remain a real
ratchet finding requiring native lifetime review.

## Core split audit and qualification follow-up (2026-11-20)

Compared the fork's 0.7.13 core additions against the 0.7.15 split across
coop.cpp, coop_cmds.cpp, coop_state.h, session_glue.cpp, puppets.cpp, vote.cpp and
dna.cpp. All fork-added metric commands, one-time service initialization,
manifest routes, captain/run/movement identity fields and unanimous pause-motion
callbacks are retained. Restored the unconditional GA world-time sender guard.
A duplicate coop.adventure registration previously replaced the fork's command
handler with upstream native diagnostics; one outside-frame dispatcher now
retains native act/place operations and delegates the other arguments to the
fork handler. Direct act changes no longer use a stale queued static callback.

The initial strict Windows build also failed on the unused write-only
selected_class cache in editor_native.cpp. Removing that cache preserves the
actual retained selection state and satisfies the existing warning gate. The
first portable shell entry failed on CRLF line endings; repository *.sh files
now use eol=lf in .gitattributes. Existing Windows batch formats are separate.
Root retains the initial failing product/shell receipts and subsequent build
results with the source integration report.

CORRECTION (2026-11-20): the 11 actual fork object_alive ratchet findings above
were subsequently audited and replaced with listed/LiveRef identity checks,
including post-callback serial revalidation and an address-reuse regression.
This does not raise or bypass the upstream baseline. The Adventure family lock
is refreshed because its fingerprint includes those lifetime implementation
files; authored formats and adventure payload bytes are unchanged.

PASS after canonical path normalization: ratchet-selftest-pass.log (all four
planted violations caught), fix-retire-pass.log (221 retired switches and 10
removed commands, no problems), fix-retire-selftest.log and
msg-manifest-selftest.log. RE, documentation, glyph, CI and game-test lints also
pass in the recorded network subdirectory. Full native game smoke and main
promotion remain separate gates.
