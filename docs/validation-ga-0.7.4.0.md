# SporeCoopGA 0.7.4.0 release validation

Version: **0.7.4.0**, protocol **15**.
This is the first published Galactic Adventures release of this fork.
**Galactic Adventures support remains experimental.**

## Included work and branch consolidation

The release contains all GA work through preview 12: shared Sporepedia entry,
native saved captain creation/selection, an explicit Ready lobby, shared
objectives and conversations, synchronized interactions, revival, unanimous
pause, native actor movement, guest enemy targeting, coordinated results and
Replay. Shared adventure editing remains planned.

The consolidation audit started at fork `main`
`dfa83ed3a7e89edeb65326542f856c54689b8bfe`. Every local branch and live fork
branch was already an ancestor of, or equal to, that commit. All audited
`main..branch` ranges were empty and `git cherry main branch` found no unique
patches. This includes the original adventure, frame-cost, captain recovery,
lobby, party flow, static interaction, upstream integration, project
presentation, interaction, pause, smooth-actor, targeting and outcome branches.
No old branch tree was restored and no GA fix was omitted.

The historical GA work is present by actual merge ancestry. Integration merge
`488b3af6b6cb36655d8a37a3d9eaf281d323eb06` joins upstream
`eb63c11133b9d2474cfc942ac18566edbaf7c4f1` and GA preview 6
`b69164aad62cb678500555dd33a8d8c67ea4199f`. Subsequent preview fixes are included
on top. The upstream base and compatibility remain those recorded in the
[integration report](integration-ga-upstream-0.7.2.1.md).

The sole production game-source change for this release is the `COOP_VERSION`
string, from `0.7.3.5-ga-preview.12` to `0.7.4.0`. Gameplay code, protocol 15,
resource bundle formats and native symbol maps are unchanged. Release-facing
documentation and the publishing workflow were updated separately.

## Compatibility and update behavior

Native GA requires the exact GOG 2024 executable, 24,895,536 bytes, SHA-256:

```text
dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37
```

All participants should install this matching release. Preview 12 also uses
protocol 15; earlier protocol versions cannot join. Matching wire versions do
not establish compatibility with unrelated builds or unsupported executables.
Steam and Wine GA compatibility remain unvalidated.

The actual updater version parser compares four numeric components. It confirms
that `0.7.4.0` sorts above `0.7.3.5-ga-preview.12`, and that the tag
`v0.7.4.0` compares equal to `0.7.4.0`. The release is published as a normal
GitHub release so the existing fork `/releases/latest` updater can discover it.
This publication status does not change the experimental GA support claim.

## Build and automated checks

Local release builds use the same llvm-mingw **20261006 UCRT Ubuntu 22.04
x86_64** toolchain pinned by GitHub Actions. The downloaded toolchain archive
SHA-256 is `5f9c6ed95b2d4bdb2869a488c5fd5857fbdabcf288a0aa3eb1da43f6a08d8ab4`.
It was obtained from the upstream compiler release over HTTPS.

- Complete loader, SporeKit, co-op, resources and Setup build passed.
- `make test` passed: networking, version handling, GA assets/music, coordinator,
  authority, movement, controls, scenes, pause and knowledge checks.
- Win32 native result-save, mirror and input-control fixtures passed.
- The exact executable verifier retains 187 reviewed functions, four globals,
  eight code sites and 2,755 effective symbols.
- Release documentation links, version/protocol references and package shell
  syntax were checked. Historical preview reports retain their original labels.
- The workflow's actual notes-extraction command selects only the 0.7.4.0
  changelog section. Tag/source version agreement is checked before publishing.
  The workflow verifies the existing tag, attaches the ZIP, matching pack,
  Setup and SHA256SUMS, and marks the normal release latest. Its release title
  identifies SporeCoopGA.
- Local package inspection passed all 14 update payload hashes, exact inventory,
  configuration-preservation flags, version markers, required documents, native
  symbol identity and ZIP CRC checks. The ZIP matches the complete staging tree;
  its pack and Setup match the adjacent download files. Stripping preserves the
  native-tested co-op DLL's exact x86 `.text` bytes and section layout.

Native-tested unstripped release DLL SHA-256:
`8c521edcaefd9832875f104636f659443ddc05fea1002931cfb8b0453e344bed`.

## Native release smoke

The release candidate passed in two hidden, muted native Windows instances using
the isolated lab installation and existing profiles
`winlab_ga_outcome_baseline_83` and `_84`. Both cold-started with the exact DLL
above as generation 1. The ordinary personal installation and profiles were not
modified by release preparation.

- Actual Defend the Crystal Mine act 2 generator failure stayed synchronized
  across 57 samples over 11.05 seconds, with both peers held at terminal state 6.
- The guest was downed at 1 HP with an unconfirmed ending cinematic. Host Done
  completed one native guest cinematic, Done and Saved sequence; both players
  returned to Galaxy application state and game mode with their party connected.
- Replay opened a fresh unready lobby with the same saved captains. Host Ready
  alone did not launch. Both players confirmed Ready and beam-down again before
  entering the new run. Cancellation restored both native Galaxy menus.
- Both native fault counters remained zero. Existing original captains remained
  at XP 100 / level 1. Only the owned lab children were closed after menu checks.

This targeted smoke used the existing private SporeKit 0.3.4 loader with the
release co-op DLL and matching symbol maps. It is not a new full-package native
installation test or a new full-playthrough, four-player, XP-award or FPS check.

Preview 12's broader acceptance remains independently documented in the
[preview 12 report](validation-ga-preview.12.md): real generator failure,
terminal state monotonicity, input/goal holds, host Done during an unfinished
guest cinematic, native reward selection, both original captain saves and
cold-process XP persistence, Replay, pause and 405-actor FPS checks. Those
results are not relabeled as newly completed full-adventure playthroughs.

## Release assets and limits

Release page: [SporeCoopGA 0.7.4.0](https://github.com/jwhans0n/SporeCoopGA/releases/tag/v0.7.4.0).
Extract `SporeCoop-0.7.4.0.zip` and keep Setup beside its matching `.pack`.
Close the game before installing. Profiles and creations remain in their normal
locations; existing configuration preservation is retained.

This release does not establish every custom adventure, complete objective-driven
playthroughs, four-player native outcome/reward coverage, precise movement or
ordinary guest click/key combat-input parity. The
[known issues](known-issues.md) and historical reports describe these limits.
Native Windows two-player checks do not prove Wine support. Adventure Editor
collaboration and other shared editors are not implemented in this release.

Ignored build and audit evidence is under `out/release` in the release worktree.
Public release publication, downloaded-asset checks and final checksums are
recorded after the tag workflow completes.
