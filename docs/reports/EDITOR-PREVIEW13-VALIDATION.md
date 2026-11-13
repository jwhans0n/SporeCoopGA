# Shared editor preview 13 validation

Branch: `fix/ga-editor-browser-runtime`, based on preview 12 review tip
`44bf687deaa97d22cd47fa2c238c8b0613e7d205`. Candidate source version:
`0.7.8.0-ga-editor-preview.13`, protocol 28. Existing native saves and recovery
formats are unchanged. Main and the editor destination remain unchanged.

## Reports and observed causes

- BUG-0639: the installed preview 12 logs record sustained 2 FPS host and 6 FPS
  guest with 0.5-second hitches and zero native faults. Network service pulses
  repeatedly schedule native cast-card scans in a single rendered frame. A
  queued-task flag alone does not bound work between frames. Schedule browser
  work once per Present frame and skip unchanged decoration setters.
- BUG-0640: native Creature Continue switches `cEditor+1CC` to the root's owned
  `EditorRequest+98` successor without Submit or intermediate NotifyResult.
  Comparing only the root pointer loses the final Outfitter save. Follow the
  verified bounded owned chain, retaining scope/lease and result fences.

Verified field behavior and signatures are in
[the RE notes](../re/creations.md#creations-browser-pulses-outfitter).

## Failing and passing evidence

Private evidence is retained under `out/preview13/`, without publishing player
names, profiles, raw logs or paths. Tests use isolated hidden/muted instances
45/46 with exclusive virtual input and the supported exact 4 GB executable.
No personal save/profile participates.

| Check | Evidence | Result |
| --- | --- | --- |
| Installed evidence copied before work | `installed-evidence/` | Both reported signatures present |
| Idle decoration fixture before/after setter guard | `ui-idle-red.log`, `ui-idle-green.log` | FAIL then PASS |
| Setter guard alone in native games | `ui-native-ab/result.json` | Still about 2 FPS; insufficient |
| Same-frame service pulse fixture | `ui-pulse-red.log`, `ui-pulse-green.log` | FAIL then PASS |
| Native frame-guard A/B, settled samples | `frame-gate-ab/result.json` | About 2 FPS off, 21 FPS on; matching native states |
| Negative-control final FPS fixture | `browser-fps-final-red/result.json` | FAIL below 8 FPS; ready, converged scenes |
| Cross-tab native FPS | `browser-fps-green1/result.json` | PASS, about 21 FPS on both participants |
| Outfitter return before chain fix | `outfitter-return-red/`, `outfitter-red-native.log` | FAIL, exact installed missing-result/recovery signature |
| Owned successor lifecycle fixture | `outfitter-unit-red.log`, `outfitter-unit-final.log` | FAIL then PASS |
| Guest native Creature/Outfitter save | `outfitter-guest-green1/` | PASS, one class and one Undo item, native observers agree |
| Host native Creature/Outfitter save | `outfitter-host-green2/` | PASS, same native assertions |
| Guest cancel after native body save | `outfitter-guest-cancel/` | PASS, cast/history unchanged, editing resumes |

The lifecycle fixture also covers cancellation, expired context/content lease,
unrelated requests, unsupported families/variants/return modes, cyclic links,
posted result lifetime and ownership through the returning phase. The browser
fixture retains hover targeting, property/content ownership and UI cleanup
coverage. No native factory signature or network payload changed.

The first native entry attempt stopped before input because the private lab
launched at 1600x1200; its measured creation recipe requires 1024x768. The later
private launch uses that exact viewport. This was a fixture precondition failure.

## Qualification and delivery

Final candidate hashes, repeat checks, repository result and deployment receipt
are recorded below after completion. Hidden-game FPS establishes removal of this
sustained regression; it does not qualify visible-game FPS, large-adventure
commit latency or the previously deferred performance targets. Broad creation
matrices, upstream-wide smoke and release qualification remain deferred. No main
merge, release tag or public release package is part of this fix.

The existing Add Act native identity failure (BUG-0637) and standalone Save New
cold-reopen warning (BUG-0638) remain separate, unresolved issues.
