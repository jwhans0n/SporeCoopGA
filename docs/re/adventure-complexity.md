# Adventure complexity gates (GOG 2024, 2026-11-02)

These are static receipts for the exact dc04 executable and its reviewed LAA
variant. `tools/addrport/verify_editor_complexity_dc04.py` verifies the complexity
namespace in `data/symbols/overrides_editor_dc04aee5.sym`, instruction bytes,
branch destinations, native callers and constants. SporeKit discovers symbol
maps by filename, but requires their executable hash header within the first ten
lines. These aliases remain in the established reviewed editor map.
No legacy mod package was installed or copied into a game.

`F3E7B0` is thiscall(ScenarioData), returns a float in x87 ST0 and has a plain
ret. It rebuilds six resource-uniqueness maps and twelve actor/resource category
counts, then calls `F3DB80` at F3EB19. The latter is
thiscall(ScenarioData, const uint32 counts[12]*), x87 float, ret4. It computes:

```
max(sum(count[i] * native_weight[i]) / native_MaxComplexity,
    (live_marker_count + 1) / 1024)
```

Weights live at16C8884..16C8934 in sixteen-byte tuning wrappers. The denominator
is live float16C8944, registered as `MaxComplexity` by initializer
138E3D0..138E421 using ASCII name148D488 and default float10.0 at1473BF0.
The independent marker fraction is exactly1/1024 at14101F0. Raising only the
weighted denominator cannot exceed that second bound.

`F3EB30` is thiscall(ScenarioData, uint32 unusedClassID), boolEAX, ret4. It calls
the measure function and requires its ratio to be strictly below1. Its native
callers are the economy adapter ECFE6D and placement tools F003E8/F0067C. The
same measure feeds the native complexity gauge atED6476 and another native
validation/permission path atF40D50.

CreateMarker F44210 and CloneMarker F44460 also compare the live marker count
against1024 atF44267/F444DE. Each is `cmp eax,400h` immediately followed by a
six-byte JAE to its null-result cleanup. Both comparisons precede the existing
owning marker-pool insertion F2EBC0. The pool uses allocated storage and238-byte
rows; this observation alone is not running-game proof that every scene path
accepts4096 objects. ImportClass F462F0 has a separate signed comparison at
F46326 and its1024-class limit remains in force.

The inherited method named `IEditorLimits::SetLimit` is not a complexity tuning
setter for this implementation. ScenarioData.Initialize allocates30 bytes and
calls ECFE50, assigning economy vtable1489800 to ownerD0. Slot18 is ECFDA0,
which **adds** to selected current economy values and calls native refresh
5A7900. The layout names in the
[upstream economy header](https://github.com/Spore-Community/Spore-ModAPI/blob/master/Spore%20ModAPI/Spore/Simulator/cScenarioEconomy.h)
and [generic editor limit interface](https://github.com/Spore-Community/Spore-ModAPI/blob/master/Spore%20ModAPI/Spore/Editors/EditorLimits.h)
must not substitute for checking this executable's actual implementation.

The original scoped multiplier1/4 has an off-by-one at its advertised boundary:
the existing native strict comparison rejects ratio1, so `(N+1)/4096` already
rejects placement4096 when4095 markers exist. The native default has the same
intentional strict expression at its original1024 budget; changing Extended
must leave that default behavior unchanged.

The Extended-only correction is `max(weightedRatio/4, N/4096)`. Existing4095
markers then permit one final placement when the weighted ratio is strictly
below4; existing4096 markers refuse another. A weighted ratio exactly4 still
rejects placement. This does not change the native `CanPlace` branch, class
bound, rewards, object sizes or any live tuning variable.

`editor_native_complexity_ratio.h` reproduces the reviewed scalar SSE arithmetic
before applying that policy. F3DB80 accumulates categories11,10,9,8,7,6 in one
float32 chain, categories5,4,3,2,1,0 in another, adds the groups, then divides by
the current native denominator. All twelve values remain live tuning values;
the default denominator10 is not hard-coded into the calculation. The complete
F3DB80 instruction receipt now verifies every arithmetic operation and tuning
address. CountLiveMarkers EF18B0 is cdecl(firstRow,endRow), uint32 EAX and plain
ret atEF18E6; native callers pass an extra unused stack value. It walks the
verified238-byte pool iterator with no native callbacks or allocations.

Before using the correction the adapter must reconstruct the original
`max(weightedRatio,(N+1)/1024)` and require its float bits to equal the actual
original result. It must pin counts, live tuning and marker-pool identity around
the original call and reject changed or invalid observations. Unsupported
tuning, a foreign formula, nonfinite values or a proof mismatch must deny the
Extended placement and report the conflict. The pure helper leaves its output
untouched on failure. No epsilon or blanket rescaling can distinguish a
marker-limited ratio1 from a weighted-limited ratio1 safely.

The two creation/clone comparisons must still agree with4096, with explicit
class bounds and restoration of ordinary behavior on leaving that scope.
Deterministic arithmetic and code receipts do not substitute for native
Create/Clone boundary, cold Save/reopen or publishing acceptance. Those checks
remain separately required; no current publication qualification is claimed.

The [original mod author's discussion](https://davoonline.com/phpBB3/viewtopic.php?p=19435)
reports in March2015 that Infinite Complexity adventures could be shared and
played without that mod. The current topic also marks its old downloads as
outdated after the2017 patch. That is historical author evidence for the feature,
not a current Sporepedia publishing guarantee or compatibility proof for a
specific installed package. Native publishing, dependency validation and the
private co-op workflow still require their own checks. This implementation must
leave captain rewards and object-size tuning untouched.

### Shared tuning admission

The unreleased protocol23 appends authenticated complexity claims and cohort
acknowledgements without changing existing message tags. A capability carries
policy1, formula identityDC04DB80, twelve exact float32 weight words and the
MaxComplexity word. Unavailable evidence carries a bounded reason and zeroed
words. Each context has fresh claim/cohort sequences; the existing project,
epoch and complete participant generation envelope fences every message.

Ordinary native budgets do not require equal or available capabilities.
Enabling Extended, authoring while Extended, initial Extended baseline
materialization and affirmative playtest readiness require identical available
proofs for the current participant cohort. A guest additionally requires the
host's acknowledgement of its current local claim sequence. Retired peers and
old epochs cannot supply that acknowledgement. Changed local evidence withdraws
readiness immediately; a bounded outbound queue failure retains the current
claim for retry. This proof transport has no live Adventure scene dependency.

A tuning conflict preserves the draft and permits the exact single Project
COMP field change from true to false. Combining that change with a placement
edit still rejects. Cancellation, Unready and playtest return remain available.
The Runtime keeps read-only projection identity separate from authoring
permission while this hold is active, so canonical application and detached
host work can continue.

Strict Linux and actual i686 protocol/UDP fixtures cover two and four peers,
all thirteen mismatched words, missing proofs, late join, fresh recovery epochs,
stale participant generations/acknowledgements, bounded-queue retry, the exact
disable exception and queued playtest expiry. These are deterministic transport
and policy checks, not a running-game complexity boundary or publication result.
