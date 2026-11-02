# Adventure complexity gates (GOG 2024, 2026-11-02)

These are static receipts for the exact dc04 executable and its reviewed LAA
variant. `tools/addrport/verify_editor_complexity_dc04.py` verifies the complexity
namespace in `data/symbols/overrides_editor_dc04aee5.sym`, instruction bytes,
branch destinations, native callers and constants. The deployed SporeKit loader
uses its established override list and ignores newly named standalone maps.
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

An opt-in scoped ratio multiplier1/4 can extend both native budget terms to a
bounded4096-marker editor budget while preserving the original installed tuning
weights. It also requires the two creation/clone comparisons to agree, explicit
class bounds, restoration of ordinary behavior on leaving that editing scope,
and native threshold/Save/reopen checks. The static audit does not authorize a
global tuning rewrite or claim those native acceptance checks have passed.

The [original mod author's discussion](https://davoonline.com/phpBB3/viewtopic.php?p=19435)
reports in March2015 that Infinite Complexity adventures could be shared and
played without that mod. The current topic also marks its old downloads as
outdated after the2017 patch. That is historical author evidence for the feature,
not a current Sporepedia publishing guarantee or compatibility proof for a
specific installed package. Native publishing, dependency validation and the
private co-op workflow still require their own checks. This implementation must
leave captain rewards and object-size tuning untouched.
