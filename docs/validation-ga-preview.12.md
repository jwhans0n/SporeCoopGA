# Galactic Adventures preview 12 validation

Build: **`0.7.3.5-ga-preview.12`**, protocol **15**.
**Status: targeted native two-player failure, party exit, captain saving and Replay checks passed.**

## Problem and resulting behavior

In **Defend the Crystal Mine**, destruction of the mine generator could leave
the host at Mission Failed while a guest continued through later acts. A
read-only capture of the reported personal sessions found the guest's native
result latch at run 1 / applied 6 while its actual play object was at act 4,
state 3. Clicking Done on the host then returned only that instance to Galaxy.

The first authoritative outcome now seals gameplay until menu recovery or a new
Replay attempt. Only the coordinator can commit native win/loss. Nonterminal
state changes, goal callbacks and stale scene messages cannot reopen the sealed
run. Local controls and damage stay fenced after the coordinator leaves Running.
The downed captain's native 1 HP presentation remains intentional; it does not
permit continued gameplay after the party loses.

Host **Done** returns the party to Galaxy. A guest's native cinematic and normal
results save finish before its scene is torn down. Mandatory captain reward
choices remain interactive and must be completed. Guest **Done** accepts and
exits locally. Shared **Replay** requires the host's Replay choice, every
remaining participant's acceptance and independently confirmed native result
state. It reuses saved captains in a new lobby with everyone unready.

## Compatibility and native boundaries

Supported executable: exact GOG 2024 `SporeApp.exe`, 24,895,536 bytes, SHA-256
`dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`.
Every participant needs this fork version and protocol 15. Older packets are
incompatible; an older client can time out during joining. Existing actor,
adventure and captain bundle formats are unchanged.

The broad native replication flag no longer authorizes terminal SetState 5/6.
Result acknowledgement reads the actual current play object and run, rather
than trusting a prior call or cached latch. The native result cinematic's
Finished event is scoped to its actual cinematic ID, play pointer and run.
Results updates continue so native cinematics and panels can finish normally.

One run-scoped cleanup lease covers Done, Replay, cancellation and disconnect.
Superseding intent invalidates retry callbacks while retaining mandatory cleanup.
Completed cleanup is reused; old callbacks cannot tear down a later attempt.
New preparation and its resource messages wait behind pending cleanup.

Native captain progression is saved by accepting the results panel, not by
entering it. A forced party exit therefore follows the normal enabled Done
button path and waits for its saved-choice publication. It does not write XP
or select captain rewards. See the [native world notes](re/adventure-world.md)
and [adventure notes](re/adventure.md) for verified layouts and call evidence.

## Baseline evidence

Tests use a separate real installation, hidden muted Windows processes and
isolated lab profile copies. Personal sessions are inspected read-only and are
not used as test fixtures. The stock adventure is `408A0000!6E008979.366A930D`;
generator marker 102, class 1069, is destroyed through the real authoritative
native damage callback. Failure checks do not inject a result or goal outcome.

Installed preview 11 deterministically reproduced the Done split: the host
returned to GalaxyGameEntry / application state `02CCDA9C`, while the guest
remained in Scenario / application state `21851EBE` at native failure state 6.
Evidence: `out/lab/outcome-baseline-host-done-first/result.json`.

The full natural timing that reopened the guest from failure to active play was
not reproduced in the isolated baseline attempts. Normal, downed and paused
generator-failure attempts stayed at state 6. The personal read-only capture
establishes the actual contradictory latch/play state; deterministic native
wrapper and coordinator regressions exercise the late callback paths directly.
No claim of a natural baseline 6-to-3 replay is made.

## Automated regression coverage

Portable `make test` passes, including networking, launch/captain coordination,
terminal snapshot fallback, first-result ownership and cleanup. The expanded
coordinator suite also covers actual portable UDP messages, early Applied and
Accepted delivery while the host settles, acknowledgement/acceptance ordering,
native readback and retry, timeout recovery, late host exit after guest Done,
crossed Done/Replay, disconnect/reset, saved captain reuse and deferred new-run
messages. Shared exits wait for native saving and keep reward UI fenced until
completion. Cancellation can supersede the pending result UI.

The Win32 mirror and control fixtures pass. They cover late goal/act/state
callbacks, nested native replication, actual terminal-state readback, stale
Finished messages and independent local input blocking after Running ends.
The control fixture includes 1,000 terminal iterations. Captain, lifecycle,
pause, targeting, blocked-update dispatch, progression, social, relationship,
action, vehicle, explosive and pickup ABI regressions also pass.

The added native result-save fixture passes with distinct UILayout, container
and window objects, complete thiscall arguments and message layout, mandatory
reward waiting, delayed Saved publication and 2,000 repeated polls without a
second native Done request. The exact installed executable verifier passes for
187 reviewed functions, four globals, eight code sites and 2,755 effective names.
The sole additional callable symbol is the reviewed native results UI handler;
the layout container getter was already verified.

The outcome harness has nine passing helper tests. Its committed integration
entry point is `tools/tests/coop_adventure_outcome.py`; Replay and controlled
progression fixtures remain separate from a complete authored playthrough.

## Native candidate evidence

Intermediate C2 DLL SHA-256:
`1b10a954df9ff0cf9d829c1db16ad022a2277c42f15a5c67c89661265224d310`.

- Real act 2 generator failure with a downed guest: both games remained at state
  6 / act 2 across 99 samples over 19.51 seconds, actual Continue and eight
  seconds of ordinary movement/ability/jump input. During the 49 input samples,
  both captains had zero displacement, unchanged objective data and frozen
  elapsed values. Host Done alone returned both to the native Galaxy game mode
  and application state `02CCDA9C`, with zero faults.
- A paused guest resumed into the same failure; guest Replay followed by host
  Done returned both to Galaxy and did not start Replay. Zero faults.
- Two native Replay rounds required fresh explicit Ready and beam confirmations,
  kept saved captain keys and used new run IDs. Zero faults.
- Hostile Crystal Hound targeting selected the guest in 126/126 samples with 16
  independent getter checks. Natural owner health fell from 232 to 102.22 over
  eight seconds; the perception pool stayed at three without duplicate captain
  records. Both games recovered menus without faults.
- Adventure Town held 405 actors / 419 placements at 21.616 host and 21.612 guest
  presented FPS over eight seconds, above the retained 20 FPS threshold. Native
  act 1 clocks advanced and both games recovered menus with zero faults.

These C2 observations precede the added forced-exit captain-save adapter and do
not establish the final package's save behavior. Evidence directories share the
prefix `out/lab/outcome-candidate2-`. Final candidate acceptance and delivery are
recorded below when complete.

The first save-aware C3 candidate was rejected by the hidden paired test. Host
Done while the guest was still in its failure cinematic reached the forced
results path and exposed an incorrect pointer category: results `+18` contains
a `UTFWin::UILayout*`, not an `IWindow*`. Passing it to window visibility caused
an access violation through the incompatible native UI vtable. The crash, dump and command logs
are preserved under `out/lab/outcome-candidate3-guest-cinematic-downed` and the
private installation logs. No C3 package was installed in the personal game.
The corrected adapter obtains the real container through the already verified
`UILayout::GetContainerWindow` before inspecting native window controls. Its
fixture models layout and window objects separately.

## Corrected C4 native acceptance

Native-tested DLL SHA-256:
`2631e54b075bf91c16ed942c5b0bd8d579298e7676d4f6f5c00a4ac06cb4879a`.
Both fresh private processes loaded this exact DLL and the matching 2,755-name
symbol map from cold startup. Private starts were serialized until the first
instance reached Galaxy. A prior cold startup failed before frame zero with
native Data-directory error 1004; that attempt supplied no gameplay acceptance.
Its precise failed path was not captured, so a startup-race cause is not claimed.

The corrected crash regression passed in the real UI: act 2 generator failure,
guest downed to 1 HP, and **only the host** confirming its ending dialog. Across
56 samples over 10.9 seconds both games reached state 6 and stayed in act 2.
The guest's Continue was still enabled and Replay absent when the host chose
Done. The adapter finished exactly one guest ending cinematic, invoked native
Done once, observed one Saved publication and returned both games to
GalaxyGameEntry / application state `02CCDA9C`. Neither game recorded a fault.
Evidence: `out/lab/outcome-candidate4-guest-cinematic-downed/result.json`.

Controlled success with two fresh named captains also passed. Both original
local captain keys changed from XP 0 / level 0 to XP 100 / level 1. Only the
host accepted its ending and chose Done; the guest was still in its cinematic.
The guest's mandatory reward wait was observed, its reward was selected through
the normal native UI, and its normal save completed before party menu return.
Score-suppression counters increased exactly once, from three to four per game.
Both games recovered menus with zero faults. This exercises native success and
progression plumbing; it does not complete Town's authored objectives.
Evidence: `out/lab/outcome-candidate4-controlled-success-fresh/result.json`.

An earlier success fixture correctly saved both original captains at XP 100,
but reported failure because it did not wait for the asynchronous menu
transition after selecting a reward. That fixture was corrected to use the
actual native reward grid and wait between ordinary selections. A repeat trial
then retained XP 100 when the fixture expected 200; its native repeat-award
policy is separate from first-award saving. Exact native inspection identifies
`scenarioCaptainAdventureHistory`: an existing remembered captain/adventure
match selects the noncaptain results path and skips XP writes. Successful saves
record that adventure in the native bounded history. This explains the repeat
trial; it was not a failed first-award save or a new mod XP rule. See the native
notes for addresses, history bounds and the separate rank-cap calculation.

A paused-guest failure also passed on C4. Both games stayed at state 6 / act 2
through Resume, actual ending confirmations and ordinary movement/ability/jump
input: 108 samples over 21.48 seconds. The 49 input-phase samples had zero
captain displacement, constant elapsed values and unchanged objective data on
both peers. Guest Replay followed by host Done returned the whole party to the
native Galaxy game mode and application state, with zero faults.
Evidence: `out/lab/outcome-candidate4-paused-replay-host-done/result.json` and
its `input-analysis.json`.

C4 shared Replay passed one native Mine round: guest Replay and host Replay,
new run ID 1 to 2, unchanged saved captain keys, a shared unready lobby and fresh
explicit Ready/beam-down confirmation. No native faults were recorded.
Evidence: `out/lab/outcome-candidate4-replay/result.json`.

The final settled Adventure Town FPS check held 405 actors in act 1 at
**21.493 host / 21.606 guest presented FPS** over eight seconds. Both game clocks
advanced and both exceeded the retained 20 FPS threshold with zero faults. An
initial sample began before act synchronization settled and correctly failed
its act-stability check; that attempt is not performance acceptance.
Evidence: `out/lab/outcome-candidate4-town-fps-settled.json`.

After the award processes exited, two new hidden muted processes cold-loaded
the same private profiles and exact C4 DLL. Both fresh original captain keys
still reported XP 100 / level 1 from their disk-loaded native properties, with
zero faults. Both private games then quit from native Galaxy; all owned test
processes were closed. Personal sessions were never driven by these fixtures.
Evidence: `out/lab/outcome-candidate4-xp-cold-persistence/result.json`.

## Limits

Targeted native Windows two-player checks do not establish Wine, four-player
native results/reward handling, every adventure, full objective-driven success
playthroughs or interrupted-save durability. Controlled success tests exercise
native progression plumbing separately from authored objective completion.

The [preview 11 targeting report](validation-ga-preview.11.md),
[preview 10 movement report](validation-ga-preview.10.md) and earlier reports
retain their exact scope. Approximate movement, ordinary guest click/key combat
input and broader compatibility remain subject to their recorded limits.
