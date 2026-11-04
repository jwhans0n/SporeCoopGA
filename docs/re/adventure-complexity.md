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
The resulting budget is checked for Undo and Redo as well: a conflicted
true-to-false singleton can restore native behavior, but undoing a disable or
redoing an enable cannot reactivate Extended without current consensus. Those
rejections leave the canonical revision, native commit count and durable
persist count unchanged.

### Offline private boundary inputs

`tools/tests/coop_editor_complexity_fixture.py` accepts only explicitly hashed
copied inputs under OUT and writes a new OUT directory. It never opens a process
or profile. Its bounded parser recognizes the retained native PropertyList/DBPF
envelope and the embedded01A80D26 ClassSerializer. The placement vector is
07102E6C, class vector072A9B58 and act vector07102E6A. Marker attribute076CA8D3
must contain exactly the root's act count. Every selected marker must have a
present class and finite supported pose fields.

Native ReadMarkerRows F2F050 reads the count, reserves with F2E0D0, then invokes
the verified constructor F2A960, ReadMarker F2D680 and InsertMarker F2EBC0 for
each row. This reader does not impose the editor's1024 Create/Clone checks.
WriteMarkerRows F28B80 writes the iterator count and marker descriptor15AEFD0.
The retained serialized marker record contains no runtime pool index; native
insertion assigns those indices. The offline helper therefore appends exact
copies of one separately reviewed low-weight stock-prop marker, retains every
existing marker and all other root attributes, and updates only the checked
list count, list size and enclosing length/index metadata. It does not edit
marker references or invent a native layout.

An optional carrier must be a copied genuine native Save package whose exact
408A0000 adventure root already equals the supplied native bytes. Replacement
appends the new payload and a copied index, retaining every unrelated raw
record and the original package bytes except its active index offsets. Unknown
formats, malformed references/counts, overlapping records, hash mismatches,
existing outputs and count4097 reject before output is created. The tool does
not install its output. A caller must close all involved private profiles and
use a new private profile copy before installing a reviewed carrier.

Windows and Linux contract tests cover the boundary sizes through4096,
compressed/uncompressed DBPF input, all reviewed constant-index shapes,
unrelated compressed metadata retention, malformed inputs and filesystem
guards. An in-memory retained Town419-row root was expanded to4095 and4096
with exact other-attribute/marker retention. That proves the offline splice,
not native parsing, UI eligibility, stock-prop selection, cold reopen or
publication. Those remain required running-game checks. Runner profile setup
does not overwrite EditorSaves, but its viewport preference must be overridden
to the currently measured OptionScreenSize11 before a future private launch.

### Passive one-shot observation

`coop.editor complexity sample` arms a diagnostic for the next natural idle
WeightedRatio call. `coop.editor complexity observation` returns Pending until
that call occurs, then exposes its twelve input counts, thirteen exact tuning
words, live marker count, original/effective ratios and raw float words. The
sample includes a serial, game frame, QPC and native generation. It does not
invoke Measure, construct a projection, change tuning or grant admission.
An unarmed hook retains the existing single original call and result.

The observation owns values only. Scene/source, project/epoch, revision,
gesture, pool roster, class/native key, tuning and code identity must remain
exact through the original call and readback. A later interaction expires the
sample; before/after clone observations therefore require separate arms and
new serials. Missing natural queries remain Pending. Count indices are not
published as category names because their individual meanings are not yet
verified. A candidate is not a low-cost prop until ordinary native before/after
clone evidence establishes its count and ratio delta.

Controlled validation passed 1,199 native complexity assertions in the shared
strict i686 executable, 442 standalone ratio checks on both Linux and i686,
and the Runtime consumer/fake API syntax check. The superseding shared fixture
also passed Flora 990 and the full native corpus 140,147, with 59 compiled
inputs unchanged. Exact receipts are retained under
`out/editor-preview9-complexity-observation` and
`out/editor-preview9-flora-layout`. These prove diagnostic composition and
retirement, not running-game complexity boundaries or publication acceptance.

## Marker storage is reserved before native actor binding (2026-11-03, feat/ga-editor-multi-act)
<a id="complexity-marker-storage-lifetime"></a>

Actual C58 cold-loaded the genuine1,025-Sound carrier and reached shared R0
on both participants, strict and fault-free. Extended reached R1. The first
normal host Alt clone then crashed. The retained executable loaded at00130000:
runtimeC08A1F is preferredED8A1F, where native UI refresh reads a marker returned
by6C0230 (`mov eax,[ecx+18]; ret`). The old actor0 and source24 cache pointers
are1FCB000C and1FCB354C, exactly24*238h apart in the old marker arena.
The GetClass address in the stack scan is not the immediate crashing caller.
Crash, dump and stable private Store evidence remains in
`out/preview9-c58-count-1025-clone-crash-preserved`; BUG-0475 tracks the fix.

Exact supported-code receipts explain the419 versus1,025 boundary. Constructor
F2E670 reserves1,024 atF2E97B/F2E996. ReadMarkerRowsF2F050 reserves the serialized
count before insertion. ReserveF2E0D0 does not shrink: larger requests allocate
exactlyN*238h bytes, with no unused-slot initialization. Thus419 retains1,024
capacity, but cold1,025 has exactly1,025. Its next insertion grows through
F2E470, doubles to2,050, copies and destroys the old rows and frees their arena.
F2EBC0 protects the clone source with a stack copy before this growth. However,
F3F8C0 writes marker addresses only into the new actor's+18. The synchronous
07311FE9 broadcast reachesED8930 before the outer clone returns and enumerates
the old actor caches. HeldClone and MarkerAudio use value/slot witnesses; their
later cleanup cannot repair that native pre-return dereference.

C60 hooks the verified constructor while its pool is still empty and unpublished.
After the original valid1,024 constructor, a checked allocation from the native
F47240/F47220 pair supplies4,096 slots. Only begin/end/cap change, exactly as in
the native empty Reserve path. Free-list/iterator metadata, every resource field,
logical native1,024 limits and all weighted tuning remain unchanged. If the extra
allocation fails, the valid original resource remains: several native constructor
callers ignore EAX or unconditionally copy through the original self, so returning
null or freeing self would be unsafe. Populated or pre-existing live arenas are
never promoted. Native CopyMarkerRowsDFF300 retains destination capacity when
the source fits, and ResourceCopyDFFA00 copies metadata rather than capacity.
Read/Copy/recovery/late-join resources created after the hook therefore keep the
reserve before any actor/map binding. Native RestoreF46400 separately tears down
maps and retires/updates actors throughF411A0 before its copy/rematerialization.

The extra retained capacity is3,072*568 =1,744,896 bytes per constructed GA
resource; total marker capacity is2,326,528 bytes. Construction temporarily owns
both old and replacement empty arenas. This is a memory cost, not a measured
performance claim; actual heap use and performance remain separate acceptance.
The native allocation wrapper forwards only size/flags to927E70 through928C80,
ignoring its name argument, so its debug-name pointer does not escape the DLL.

A value-only immutable constructor policy is atomically published and retired.
The resource factory may construct off the editor thread: that path reads only
the retained policy, native code and its unpublished self. Main-thread calls
also close host/module/generation, allocator and policy identity after callbacks.
An atomic trampoline handoff covers entry during hook activation. No worker,
Native owner, frame-spanning parser or live actor-cache rewrite is introduced.
Controlled worker-retirement checks do not claim a new SDK module-unload guarantee.
The policy checks full constructor, Reserve, ReadRows, CopyRows and Free bodies,
including ret4 immediates and verified HIGHLOW relocations, and pins the installed
constructor bytes. The same code profile covers original and LAA executables;
GOG2016 remains frozen under BUG-0108 without invented symbols.

Fresh current-scene capacity is part of Extended capability and input/apply/restore
closure. An old or allocation-fallback scene holds Extended with its draft intact;
it cannot inherit another scene's widened hard bounds. No scene is required for
the global code/tuning proof while a host is away. Current policy2 and protocol25
exclude unsafe older private clients at the handshake; updated same-build native
default editing still accepts unavailable or mismatched tuning. Native Save,
recovery and authored formats are unchanged. Personal installed protocol23 remains
pinned independently; users were told to leave Extended off pending validation.

The controlled production-hook fixture passes508 checks: native storage models
for1,024 to1,025, cold1,025 to1,026,4,095 to4,096, refusal of4,097, unchanged
default1,024 behavior, Copy/recovery/late join, failed allocation, stale callbacks,
reentry, exact-code conflicts and worker retirement. Adjacent complexity1,575,
delayed Clone838, preset8,910, terrain materialization73,241 and full adapter153,522
checks pass under strict actual-i686 compilation,76 compiler/Make inputs with
zero drift. Receipts are under `out/editor-preview9-marker-capacity`; the first
fixture-only sentinel failure is retained separately. The initial unrecognized
`--materialization-only` ran a legacy default subset and is explicitly qualified;
the registered `--terrain-materialization-only` then passes on the unchanged EXE.
Static source receipts are in `out/preview9-c58-count-clone-source-audit` and
`out/preview9-c60-marker-capacity-review`. Actual C60 native boundary, Save/cold,
publication and performance qualification remain pending.

CORRECTION (2026-11-03): C60 now passes the original running-game cause repro.
It recovers the prior crash's durable R1 with1,025 markers,118 classes and5 acts;
both participants report current policy2 and Extended Ready. The same normal
Alt Sound clone reaches1,026 at R2, then checked Undo restores1,025 at R3.
Both peers' native SHA57e19767...36a1b9 and semantic4f492774...95dcb1 return
exactly to their pre-clone values. Only native count[4] changes630 to631;
all six uniqueness counts and thirteen tuning words remain exact. No fault or
crash occurs. Evidence is retained in `out/preview9-c60-recover-count1025`,
`out/preview9-c60-count-1025-navigation` and `out/preview9-c60-count-1025-clone`.
The recovery watchdog sampled `skg::readable` and existing terrain-validation
closures, and recovery subsequently completed within the120-second fixture.
That report establishes a long no-frame operation, not a demonstrated deadlock.
The separate4,096/overflow and native Save/cold checks remain pending; BUG-0475
stays open for them. Earlier C58 shorthand "count4" meant input index[4],
whose real value increased24 to25 on the419-marker baseline.
