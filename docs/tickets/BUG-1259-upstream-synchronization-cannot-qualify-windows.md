---
id: BUG-1259
title: Upstream synchronization cannot qualify Windows smoke without native fixtures
status: open
kind: bug
stage: Tools
reported: 2026-11-05
source: Local 0.7.8 integration smoke preflight
role: both
versions: 
fixed_in:
branch: sync/upstream-0.7.8-main
commits:
tests:
signature:
docs:
related:
---

## Report
The Windows checkout has no native base-stage fixture archives required by the pinned upstream smoke suite. The integration can build and run portable/Win32 fixtures, but cannot claim the required gameplay smoke gate.

## Root cause
The shared fixture directory and both existing worktree artifact roots contain no creature, cell, tribe, civ or space .tgz fixtures. Linux Wine/Bottles is also absent. The Windows runner must fail before modifying profiles when prerequisites are missing.

## Fix
No gameplay or smoke assertion is weakened. Retain the source synchronization on review branches until the required fixture-backed native scenarios pass; destination main remains unchanged.

## Verification
Verification evidence belongs in the upstream synchronization report. Missing prerequisites are UNAVAILABLE, never PASS. No native acceptance is claimed from build or deterministic tests alone.

## History
- 2026-11-05: opened.
- 2026-11-20: Added an explicit private Windows lane backend and original-suite
  adapter with hash-bound payload/symbols, handle-owned Job cleanup, checked
  profile/fixture operations, native viewport verification and unchanged test
  assertions/log gates. Fourteen adapter contracts and twenty strict startup
  contracts pass; real fixture-backed native smoke remains required. Corrected
  preflight to the five stock archives used by the fourteen smoke cases; genuine
  creation packages are additional dependencies of selected creation scenarios.
  See docs/reports/WINDOWS-NATIVE-QUALIFICATION-0.7.15.md.

- 2026-11-20: The first completed real repository unit row returned make exit 2
  for independent integration failures. The adapter misread planted portable
  self-test verdict examples as the outer verdict. Unit qualification now uses
  the actual make process result and supervisor failures, preserving strict
  gameplay verdict checks. The boundary regression fails against the old
  adapter and all 21 backend contracts pass. Native smoke remains required.

- 2026-11-20: GitHub Actions Python 3.10 exposed four hash-helper errors from
  using the Python 3.11-only file_digest API. Bounded streaming SHA-256 preserves
  exact hash admission on both versions. The absent-API regression fails before
  the correction and all 22 backend contracts pass afterward; real Python 3.10
  CI and native smoke remain separate gates.

- 2026-11-20: A second Python 3.10 CI run exposed only the new regression's
  teardown, which assumed file_digest existed before the test. The dictionary
  context now handles both initial states; its old failure is reproduced and
  all 22 contracts pass locally. Production hashing is unchanged.

- 2026-11-20: The original native Creature setup selected an empty galaxy slot;
  a genuine fresh-profile wizard at the same fixed coordinates also failed.
  Both RED attempts are retained. The Windows-only fixture loader now discovers
  one owned Game0 planet and uses the existing native load path, checking its
  actual named Creature avatar and scene readiness. Linux clicks and all smoke
  assertions remain unchanged. All 24 backend contracts pass; actual native
  smoke must rerun before qualification.

- 2026-11-20: The original native UI scenario reached hosting but its three
  U+25A0 swatch glyphs could not print under cp1252. The strict RED is retained;
  the unchanged scenario passed with UTF-8 launch configuration in 276.06s.
  Worker defaults now supply that exact successful encoding environment. The
  actual-subprocess regression and all 25 backend contracts pass. Remaining
  campaign and GA qualification remain separate gates.

Legacy fork reference: `BUG-0626` before upstream 0.7.13 integration. Historical commits and OUT receipts retain that identity; upstream owns its independently created ticket under that number.

Legacy fork reference: `BUG-1014` before upstream0.7.15 integration. Prior aliases and historical commit/OUT identities remain unchanged; upstream owns its independently created ticket under that number.
