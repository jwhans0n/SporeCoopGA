# Shared editor preview 14 validation

Branch: `fix/ga-editor-creation-grounding`, from preview 13 tip `f899d860`.
Version: `0.7.8.0-ga-editor-preview.14`, protocol 28. Existing saves, recovery
formats, main and the editor destination are unchanged. No public release.

## Report and fix

BUG-0641: Creature/Outfitter return rejects expired original placement grounding
or exact captain settings before canonical restoration, leaving recovery frozen
and preventing normal subsequent creation entry. Restore the latest owned
canonical scene through the existing scene/session fence before strict final
validation. Preserve authored settings, local view/selection, history and
recovery safeguards. No tolerance, unsupported-field allowance, native symbol
or wire change is introduced. The diagnostic switch is
`coop.fix editor.creation_restore off`.

[Verified findings and distinction between observed errors and inferred native
teardown behavior](../re/creations.md#creations-canonical-return).

## Evidence

Private evidence is retained under `out/preview14/`. Personal profiles and
installed logs are read/copied only. Native instances 47/48 use cloned private
profiles, hidden/muted games, exclusive virtual input and the exact supported
4 GB executable. The reported draft has 300 placements, 70 classes and four acts.

| Check | Evidence | Result |
| --- | --- | --- |
| Installed report | `installed-evidence/` | Exact placement-witness error from both roles |
| Private cold draft, cancellation before fix | `cancel-return-red/` | FAIL, exact captain position mismatch on return |
| Private native body and Outfitter save before fix | `creation-save-measured-red/` | FAIL after durable admission, same captain mismatch |
| Retired grounding runtime regression | `runtime-return-red.log`, `runtime-return-green1.log` | FAIL then PASS |
| Private canonical recovery | `canonical-recovery-green1/` | PASS, both actual native scenes match |
| Repeated native cancellation/re-entry | `native-return-host-green1/`, `native-return-guest-green1/` | PASS, twice per role; exact history and observers |
| Native Creature/Outfitter save | `outfitter-host-green1/`, `outfitter-guest-green1/` | Candidate checks, final qualification recorded below |

The exact installed placement-witness signature was retained from the player
logs; the private cold recovery reproduced the related captain-ledger loss, not
the same raw placement mismatch. A controlled expired-witness regression fails
before the fix. No claim is made that native marker-key mutation was observed
in that cold recovery. Early fixture attempts stopped on wrong viewport or
random-body geometry preconditions, before product return; they are retained
as fixture failures, not passing native checks.

## Final qualification and installation

Pending final candidate checks and installation. Broader creation matrices,
performance acceptance and upstream-wide smoke remain deferred. BUG-0637 Add Act
and BUG-0638 standalone Save New/reopen remain separate known issues.
