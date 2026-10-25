# March 2017 GOG executable (dc04aee5)

The Windows installation inspected for Galactic Adventures uses:

* `SporebinEP1/SporeApp.exe`, 24,895,536 bytes, PE32/x86, preferred base `0x400000`.
* SHA-256 `dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`.
* Upstream Spore ModAPI explicitly recognizes this size as March 2017 in
  `SourceCode/DLL/DllModAPI.cpp`. The source revision inspected was
  `69b27b5b407a2c76e1e701ba27630a77a9ce5d01`.

This is a different executable from the original Linux lab's `922af7c7` build.
Changing an existing map's hash would enable wrong functions and globals.

## Reviewed symbol subset

`data/symbols/march2017_dc04aee5.sym` retains the full upstream map and its code hash.
`data/symbols/overrides_dc04aee5.sym` retains upstream's verified stage-specific
corrections and code/data/text hashes for compatible and Steam-packed executables.
`data/symbols/overrides_ga_dc04aee5.sym` adds the separately reviewed bootstrap,
native UI, creature/captain, resource and scenario functions. The loader sorts
overrides by filename, so the GA override loads after `overrides_dc04aee5.sym`.
Its stricter function identity corrections take precedence over imported aliases.
The existing adventure/world override files remain enabled for this build too.

`tools/addrport/verify_dc04.py` records the inspected prologues, return instructions
and stack cleanup, selected direct-call edges, and global reference instructions.
It refuses every other executable hash and can regenerate or compare the GA override:

```powershell
py -3 tools/addrport/verify_dc04.py 'D:\GOG Galaxy\Games\SPORE\SporebinEP1\SporeApp.exe' --check-map data/symbols/overrides_ga_dc04aee5.sym --check-symbols data/symbols
```

The evidence is static verification, not a claim that every listed function has
been called in a running game. Preserve this distinction in future updates.
The effective-directory check also applies the native loader's normal/override
sort order and rejects later files that replace either float accessor or a
verified GA entry with another address.

The symbol loader must read the preferred image base from the executable on
disk. On this Windows installation the loaded PE optional header's `ImageBase`
already contains the relocated base. Treating it as the preferred base left
symbol addresses unrelocated and crashed the first bootstrap at the cheat-manager
getter. `sporekit/symbols.cpp` now validates and reads the on-disk x86 PE header,
and fails closed if that header cannot be read. There is no guessed base.

The Windows runner verifies this regression against a live named profile: the
game-mode-manager getter resolves to `actual_base + 0x67DCD0 - 0x400000`, and its
loaded `A1 ... C3` instructions reference the correspondingly relocated global.
At actual base `0x00360000`, bootstrap reached the main menu with the mod loaded,
a valid Direct3D device, and advancing frames. This validates bootstrap and
relocation, not every scenario function listed below.

Corrections found while auditing the upstream/old generated names:

* `cCreatureAnimal::TakeDamage` is `0xC075E0`, with three inspected `ret 0x14`
  returns. Upstream's generated `0xBFCF10` entry is the base combatant damage
  handler, not the animal override. The old GOG handwritten map already records
  this naming problem. Do not reverse-port the generated alias.
* `cScenarioData::GetObjectForMarker` is `0xF3D620`: its avatar-marker branch
  compares data `+0x8C` and returns the noun manager's avatar.
  `GetMarkerForObject` is `0xF3DB10`: its avatar-object branch returns data
  `+0x8C`. The old generated map reverses the first name.
* The resource manager pointer used by App is `0x15FD894` (getter `0x67DC90`),
  not an assumed fixed displacement from `0x15FE88C`. The alternate resource
  singleton getter `0x8DE4B0` reads `0x1667AA0`.
* `DatabasePackedFile` constructor is `0x8DA2A0`, `thiscall(path16*, allocator)`,
  `ret 8`. Scenario Read calls it at `0xF2FD6D`; Write calls it at `0xF25E8A`.
* Herd enable is `0xC6AE80`, `thiscall(bool)`, `ret 4`; it writes `+0x218` and
  is called from native CreateHerd at `0xB23A09`.
* `App::Property::GetFloat=0x40CF10` is upstream's verified
  `cdecl(PropertyList*, propertyID, float* out) -> bool`, used by Space trade.
  The separate boost reader at `0x4E1CE0` takes a float default and returns a float;
  GA calls it through `SporeCoop::Adventure::GetFloatDefault`. Do not replace the
  upstream symbol with the GA address, because their calling signatures differ.
  The pickup ABI test exposes both symbols and rejects calls to the bool/out one.
* `cScenarioClass::IsConsumible=0xF25D50` reads first act `+0x4A4` and compares it
  with `-1`. The imported `0xF27730` alias is a different function. The verified
  GA override preserves this correction and `GetMarkerForObject=0xF3DB10`.

## Scenario launch research anchors

* `cScenarioMode` constructor `0xEF0210` installs primary vtable `0x148AFD8`
  and writes the object to global `0x16C7AA4` at `0xEF02E9`.
* That vtable's `IGameMode::OnEnter` slot `+0x18` is `0xEF24C0`, plain return
  at `0xEF2550`; it initializes `GameInput` at mode `+0x20`.
* `cScenarioData::LoadScenarioResource` starts at `0xF44A90`, with `ret 4`
  at `0xF44C5B`. `cScenarioData::Load` starts at `0xF464D0`, calls the resource
  loader at `0xF464E0`, and ends with `ret 4` at `0xF4672C`.
* `cScenarioPlayMode::Initialize` reads global `0x16C7AA4`, mode `+0x74`
  (data), and data `+0x10` (resource). `GetCurrentClassAct` checks mode `+0xCC`
  and reads play mode `+0x78`, consistent with the upstream structures.
* `Scenario::ResolveLocalServerAsset` at `0xF257B0` is a local server-ID-to-key
  resolver (`cdecl(uint64_t, ResourceKey*)`, plain returns at `F25821/F2582A`).
  Its calls to local asset lookup do not make it an asset downloader.

The loading screen's Show function alone does not switch game modes. `E3E350`
updates loading UI and posts message `0x490A868` at `E3E684` on its show path.
An intercepted Show can therefore leave a native launch state machine running;
replaying only Show can display loading without entering Scenario.

The shared mode-switch implementation is `App::cGameModeManager::SwitchToIndex`
at `0x7D8670`, `thiscall(manager, index) -> void`, `ret 4` at `7D87F2`.
Manager `+0x14/+0x18` delimit a vector of 0x18-byte entries: mode pointer at
entry `+0`, ID at `+4`. It calls old mode vtable `+0x1C` at `7D86D4`, writes
active index `manager+0x28` at `7D86E3`, and calls the new mode's `+0x18`
OnEnter at `7D8766`. Public `SetActiveMode` at `0x7D8CF0` accepts a mode ID,
returns bool in AL, and pops its one argument (`ret 4` at `7D8D30/7D8D3D`).
Its ID lookup calls the common switch at `7D8D34`; public SetActiveModeAt
`7D8D40` also reaches the same switch at `7D8D65`. Gate the actual transition
before the common implementation mutates active mode state.

### Native hostility predicate

`SporeCoop::Adventure::IsHostileToCreature` names the inspected function at
`0xC0B9C0`: `thiscall(creature, other) -> bool`, with `ret 4` at `C0B9ED`,
`C0B9F6`, and `C0B9FC`. It obtains the active species relationship manager through
`B3D5C0`, then calls `BA40E0(manager, &creature->speciesKey, creature, other)`.
It returns true for native relationship values 2 or 5, false otherwise or without
the manager. Native AI calls it at `D8CAB7` with the actor in ECX and its target
as the argument. Use an NPC as `creature` and the current native avatar as
`other` when classifying current hostility to the party.

This avoids treating every non-player team as hostile. In scenario mode,
`BA40E0` consults the live per-object relationship cache at manager `+0x40`, and
initializes absent records from the current scenario act. Its initializer
`BA29F0(class, act, creature)` considers team (`+0x4A8`), stance (`+0x4AC`), and
advanced behavior (`+0x4C8`, actions vector `+0x4CC`). Team 1 can be neutral:
ordinary stance 1/2 yields relation 3; advanced action type `0xB` also yields 3.
The simple team getter `C048F0` and friendly predicate `C0EA30` cannot replace
the hostility predicate. The SDK's relationship enum labels also disagree with
the already documented creature-stage interpretation of value 5; use the native
predicate rather than applying enum labels across stages.

The creature-base Cast type is also verified natively: `C0B7D0` compares its
argument with `0xCE9F6639` at `C0B7D8`, returns the original ECX on equality,
and pops the type argument with `ret 4` at `C0B7E4`. Scenario code at `F1D031`
calls primary vtable slot `+0xC` with that type, matching the SDK declaration.

### Generic combatant damage

`cCombatant::TakeDamage=BFCF10` is the common HP handler:
`thiscall(combatant, float amount, uint32 attackerPoliticalID, int damageType,
Vector3* direction, cCombatant* attacker)`, `ret 0x14` at `BFD16E/BFD1F2`.
`cCreatureAnimal::TakeDamage=C075E0` calls the creature-base override `C1A520`
at `C07825`; the creature-base override calls `BFCF10` at `C1A982`. A common
handler detour must avoid filtering an animal hit twice through this chain.
Combatant primary vtable `+0x0C` returns its owning cGameData, as exercised at
`BFCF4B..BFCF50`; generic owners must not be recovered by subtracting 0x5A8.

Direct calls into the common handler also occur in native building damage
(`BCF34A`, function `BCEE50`), turret damage (`BD0A47`), vehicle damage
(`CAB773/CABDFF`, function `CAB6F0`), UFO damage (`C3CF17/C3D451`), tribal hut
damage (`C9CAAE`), and tool damage (`C9D848`). Blocking only the base handler
prevents its HP mutation, but class-specific code before and after that call
can still execute. Do not claim it suppresses every outer damage side effect.

The common handler checks combatant `+0x80` invincibility at `BFD03C`, then
dead state `+0x34 == 2` at `BFD081`. This also precedes type-10 healing. In
Scenario it checks the native class damage gate at `BFD055..BFD07B`. Type 10
adds HP at `BFD135..BFD140` and clamps through `GetMaxHitPoints`, vtable `+0x58`.
Ordinary damage subtracts HP at `BFD176..BFD17B`; lethal damage calls native
scenario death/goal helper `F249B0` at `BFD1DA` after setting dead state. The
direction pointer is read as three floats, and must remain non-null.

### Actor removal and reappearance

`adventure_world_lifecycle.*` follows the native map registration sequence from
`F4439A..F44403` and `F4452B..F44569`. The two maps at data `+0x90` and `+0xB0`
have 32-bit keys, nodes `{key, value, next}`, and eight-byte iterators
`{node, bucketPointer}`. The following entries have inspected prologues, return
instructions and native callers in the strict map verifier:

| Function | Address | Native ABI |
| --- | --- | --- |
| Find32 | `645E40` | thiscall(map, iteratorOut, keyPointer), ret8 |
| Erase object-to-marker | `1068390` | thiscall(map, iteratorOut, node, bucketPointer), retC |
| Erase marker-to-object | `ADC8F0` | same retC; releases the stored object through vtable+4 |
| Object-to-marker slot | `B475C0` | thiscall(map, objectKeyPointer), ret4, returns uint32 value slot |
| Marker-to-object slot | `B70020` | thiscall(map, markerIDPointer), ret4, returns object-reference slot |
| Assign object reference | `B5FAB0` | thiscall(valueSlot, object), ret4; AddRef new then Release old |
| Apply marker state | `F3D930` | thiscall(data, marker, bool), ret8; transforms and class-specific state |
| Apply marker visibility | `F40A50` | thiscall(data, marker, object), ret8; current act visibility |
| Remove marker object | `F3FC30` | thiscall(data, marker), ret4; DestroyInstance and both map erasures |

The erase functions return an iterator through the explicit output argument.
Their native call sites reserve two iterator words by value, then push the output
pointer, so they pop 12 stack bytes. Calling them with only an iterator pointer
would corrupt the stack. Find returns the sentinel stored at `buckets[count]`
when a key is absent. The wrapper checks bucket bounds and nonzero count before
calling it, avoiding a division by zero in the native hash lookup.

Native refresh `F41730` is not a general respawn call: it requires an existing,
non-null forward binding and always destroys that old object again. Native
DestroyInstance `B22560` has no early already-marked check. The lifecycle wrapper
instead erases already-retired reverse and forward bindings without a second
DestroyInstance, then creates and registers a replacement. New removals through
`F3FC30` clean both maps. The avatar branch also frees its marker, so the wrapper
explicitly rejects avatar markers and negative class indices.

Creation matches the internal `F3F8C0` factory: ordinary classes use
`CreateObjectInstance`; classes selected by `IsConsumible=F25D50` use the special
marker powerup factory `F237B0` (`cdecl(marker)`, plain return `F238BC`). Both get
the native class/marker fields and both map entries. Native state setup restores
authored transforms, so the requested host position/orientation are applied
after state and visibility setup. Successful creation checks both native lookup
directions before handing the object back to world synchronization.

Capabilities fail closed on stale acts, detached resource markers and conflicting
live forward bindings. A marker can have simultaneous incarnations while the
native forward map names only one; removal of an extra incarnation preserves
that current binding. Such a conflict must be matched to an existing instance or
reported, not resolved by overwriting another live object.

`tests/adventure_lifecycle_tests.cpp` passes as a native 32-bit Windows executable
with a fake native adapter. It checks the explicit stack argument shapes, both
map directions and intrusive references, retirement after a legacy generic
destroy without double destruction, extra incarnation removal, conflicting live
bindings, specialized powerup creation, stale acts and malformed maps. This
validates adapter behavior; actual native reappearance still requires a running
two-instance adventure fixture.

### Guest vehicle AI authority

The generic behavior-tree tick starts at `BC9160` in this executable. The old
GOG address `BC9140` is inside the preceding function here. Its ABI is cdecl,
nine 32-bit stack words (36 bytes): float delta, double time (two words), tree
definition, actor, tree context, and three further tree-state words. The actor
is the fifth word, at entry `ESP+0x14`. Plain returns were inspected at
`BC9769/BC9774/BC97DF/BC97F6/BC9801`.

The Scenario vehicle strategy post-update is `F39DF0`,
`thiscall(strategy, int deltaTime, int deltaGameTime)`, `ret8` at `F39E65/F39FAE`.
Constructor `F39C90` installs primary vtable `148D0F8`; slot `+0x3C` at
`148D134` points to this update. The two integer arguments match upstream
`cStrategy::PostUpdate`; native `EBP+0C` is the game-time delta, not the low half
of a 64-bit argument. It enumerates vehicle noun `18C6DE8`. In native
play mode 2, each vehicle in ESI follows this path:

* `CAC8B0`: one-time initialization.
* `C9F460(vehicle, deltaGameTime)`: weapon-cycle index/time counters, `ret4 C9F4B2`.
* `CA9850(vehicle)`: status timers and event queue updates, plain `ret CA9905`;
  its five-second branch queues native event `0x1B` through `CA8310`.
* `BC9160`: behavior-tree evaluation at `F39F69`, after checking vehicle
  `+0x5D0` vtable `+0x20` and vehicle `+0xAF0` tree definition.
* `CA3380(vehicle)`: model/graphics update at `F39F77`.
* `EF1850(scenarioMode, vehicle+0x508, float deltaSeconds)` at `F39F91`:
  delegates to mode `+0x70` actor processing at `EBDBB0`, `ret8 EF1865`.

`adventure_world_vehicles.*` hooks the generic tree tick, vehicle weapon counters,
vehicle status timers, and the common `EF1850` actor processor independently.
The verified names are `TickBehaviorTree`, `TickVehicleWeapons`,
`TickVehicleTimers`, and `ProcessActor` in namespace `SporeCoop::Adventure`.
The native aggregate is left running, including `CAC8B0` initialization,
`CA3380` graphics, and final `DC69B0` cleanup. Actor processing resolves the
combatant's owning `cGameData` through native vtable `+0C` before consulting the
mirror predicate. Host/inactive/uncommitted runs pass through without that extra
owner call.

Suppression requires all four context callbacks and a nonzero active guest run.
`is_mirrored` must identify only live bound NPCs, excluding captains and dead
actors. Returning false for dead actors preserves native death cleanup. A blocked
tree returns false (pending) without advancing its state. Missing symbols,
another executable hash, or partial hook installation leave the adapter
unavailable; partially installed hooks are removed. The adapter's counters make
each suppressed native path observable to a running-game fixture.

The separate Win32 regression `tests/adventure_vehicle_tests.cpp` passed exact
nine-word forwarding, both native bool returns, 10,000 mixed cdecl/thiscall calls,
combatant-to-owner conversion, host/nonmirror/dead/inactive/run-zero passthrough,
all four suppression paths, graphics/cleanup continuity, and partial-hook
rollback. This validates the adapter and native ABI shape, not vehicle behavior
in a real scenario. The helper was developed without rebuilding or deploying
over the ongoing native pair fixture; integration and a real vehicle adventure
remain runtime validation gates.

### Native recruitment entry

`D542E0` is the native posse recruitment operation:
`thiscall(posseManager, cCreatureBase* target) -> bool in AL`, `ret4` at
`D5464B/D5465A`. Getter `D53980` is cdecl, lazily creates a 0x58-byte manager
at global `169E5B8`, and returns without arguments. Native Scenario goal code
pushes a creature, calls the getter at `F1D877`, then recruitment at `F1D87E`.

Recruitment checks capacity and duplicate membership, adds the creature through
noun-manager helper `B23430` at `D543DE`, and updates stamina, names, flags, herd
and presentation. In Scenario it calls play-mode AllyWith callback `F1D3E0` at
`D544C5`. Calling only that goal callback omits these native posse side effects.
The full operation invokes UI/effects and must run outside Present. Its rejection
conditions and original host avatar/posse ownership must be respected before
using it for remote players; this is a research anchor, not runtime validation.
The strict manifest also verifies the native building/vehicle inventory Give
entries `BCF600/CA44B0`, vehicle consume predicate `C9F0B0`, class-act relation
`BA29F0`, and the container Give behavior callback `D83E90`. Its indirect tick
callers `BC9477/BC969E` load `cBehavior::mpTickCallback` at `+8`, pass eight stack
words, clean `0x20` bytes and test AL. Actor is word one, the split double occupies
words two/three, state is word six and float delta is word eight. Six-word calls
through callback `+0C` in the same tree evaluator are deactivation callbacks.
The native Give callback uses `C12560` (a tail jump to animation-completion
predicate `C0E420`) and order lookup `BC9810` (`thiscall`, `ret8`). The separate
`adventure_world_actions.*` adapter owns these operations; its runtime integration
and actual two-game action checks remain separate from static map verification.

## Native Windows test isolation

`tools/windows_lab.py` prepares an actual copy under `out/windows-lab`, deploys
the mod only into that copy, creates fresh named profiles with windowed muted
preferences, and tracks child process handles and PIDs. It never uses a process
name to stop games. Processes and desktop handles are closed on normal exit or
test failure. No player profile or login configuration is copied.

The first private-desktop fixture failed safely before any Spore launch:

* Python `subprocess.STARTUPINFO` does not forward a dynamically assigned
  `lpDesktop`. A direct `CreateProcessW` wrapper is required.
* Direct `CreateProcessW` correctly placed the probe on `SporeCoopLab-...`, while
  the input desktop stayed `Default`.
* Both GPU adapters returned `D3DERR_NOTAVAILABLE` (`0x8876086A`) from Direct3D9
  `CreateDevice` on the inactive private desktop. Adapter enumeration and display
  mode retrieval succeeded. Do not launch the game through that unrenderable path.

An always-hidden window on the input desktop passed the separate renderer
fixture: Direct3D9 HAL creation, exact framebuffer readback, three successful
Present calls, externally invisible HWND, and unchanged input desktop. Use the
explicit `--hidden` option for that path:

```powershell
py -3 tools/windows_lab.py probe --hidden
py -3 tools/windows_lab.py prepare
py -3 tools/windows_lab.py run --hidden --instances 2 --seconds 120
```

The game side requires both a named profile and `-sporekit-invisible`. It installs
window creation/show/position/activation hooks before game initialization and
stops before creating game windows if any required hook fails. The external
runner checks actual window visibility, independently of the game's own
visibility facade used to keep its frame loop active. The runner never switches
desktops or injects global input.

Hidden native windows also need posted `WM_PAINT` messages to keep Spore's frame
loop active. The runner posts these at 30 Hz to all positive-size top-level
windows owned by its tracked child. Selecting only the largest window is wrong:
another owned window can be larger than the Direct3D game window. Without this
pump the console can initialize while the frame counter stays at zero.

Fresh preferences must include `OptionVersion 9`. Without it Spore replaces the
file with defaults during initialization, including unmuting audio. The runner
writes version 9 and muted zero-volume settings, and the running profile was
checked after initialization: `AudioMuteAll true`, all volume values zero,
`OptionFullScreen 0`. No player preferences are read or copied.

`tools/windows_audio.py` additionally sets and reads back only the tracked child
PIDs' CoreAudio session mute and volume. The live child had an active render
session with `muted=true`, `volume=0.0`. The runner repeats this check while it
holds the children and writes `audio.json`. An empty list means no session was
present; it does not claim a session was muted. System volume is never changed.

Runs are bounded to one hour from console startup. A live run can renew its
original shorter timeout using `extend --out <run-output> --seconds 600`; the
runner reads the lease while retaining its own exact process handles. A lease
does not detach children or disable visibility checks and cleanup.

Named profiles now redirect `SHGetFolderPathW(CSIDL_PERSONAL)` to
`AppData/Roaming/SporeKit-<profile>/Documents` as well as redirecting AppData. This
keeps creation PNGs away from the player's My Spore Creations folder. With no
named profile these folder lookups retain their normal behavior.
The hidden runtime created its `My Spore Creations` subdirectories under this
redirected Documents directory, confirming the path is used by native startup.

The first complete hidden run reached the native main menu at 1024x768 with the
Galactic Adventures and Co-op entries visible in framebuffer screenshots. The
registration prompt was dismissed using the devkit's per-window virtual input.
This uses no global cursor movement or foreground activation.

The first paired run exposed a disabled-module status bug: `coop.status` printed
social status, then `tribe::status` called the absent `GetPlayerTribe` symbol
through address zero even though tribe initialization had disabled the module.
The exception was `C0000005`, EIP zero, with the noun manager in ECX. Disabled
tribe status now returns an unavailable line, and the player-tribe helper also
checks the function address. Native regression checks must call `coop.status`
both before connection and while hosting, not just `info`.
`windows_lab.py check-status --out <run-output>` performs that regression on
recorded disconnected lab children, validates their PID and executable identity,
starts an ephemeral host, checks status, leaves it, and checks status again. It
refuses a child that is already connected. The guard fix passed this sequence on
the native host process and offline status also passed on the second process.

Hot reload also exposed a queued callback lifetime bug. `adventure::reset` queued
a flag update from on_unload, then the mod manager unloaded that DLL. The next
outside-frame task jumped into unmapped old DLL code (`C0000005` at `59517C20`;
SporeKit stack resolved to `h_run_outside_frame`, main.cpp). The reset flag now
updates synchronously. SporeKit's public task wrappers additionally retain the
callback's containing module until the kit-owned queue closure is destroyed,
so old callback code and its payload cleanup remain mapped after reload.
This retains code lifetime only: mods must still invalidate old generations and
avoid actions through state already torn down by on_unload. The task API has no
cancellation payload deleter, so blindly dropping queued raw callbacks would
leak the caller's payload.

Native regression `tests/windows_task_lifetime_mod.cpp` queues both public task
types with heap payloads during on_unload. The test observed both callbacks after
the manager logged unload, successful payload cleanup, and the old DLL base
becoming unreadable only after the callbacks completed. The child kept rendering
and answered adventure status. Compile the fixture as a 32-bit Windows DLL into
`build/windows_task_lifetime.dll`, then run
`py -3 tools/tests/windows_task_lifetime.py --out <run-output> --instance 21`.
It validates the recorded lab PID and executable before loading the fixture into
the test copy, and writes `task-lifetime-21.json` as evidence.

The first synchronized captain launch reached every captain-publication ACK and
guest-ready barrier, then both native games failed at preferred `00C0714E`
(`movzx ecx, byte ptr [ebp+1]`, EBP zero, read address one). Both complete native
reports, minidumps, mod logs and process manifest are preserved under
`out/windows-lab/crash-launch-20261023-1224`. Both actual images were based at
`00270000`, so the exception instruction was `00A7714E`.

The call chain is `cScenarioPlayMode::SetCurrentAct F1EE70` -> `F1E580` ->
`cCreatureAnimal` act setup `C070D0`. `F1E580` walks the entire creature noun
roster, skipping creatures in the player avatar's herd (noun-manager getter
`989290`, creature herd getter `C046F0`). A remote captain spawned into its own
herd therefore participates. `C070D0` first skips creatures with flag `B58 &
0x200`; otherwise it calls `B18470` for the object's scenario class and derives
the current class-act pointer. `B18470` returns null immediately for object
`+1C == -1`. `C070D0` still dereferences that null class-act pointer at `C0714E`.
This explains why ordinary unclassed creature puppets cannot enter this native
NPC act-initialization path, even when they were created successfully earlier.

The subsequent `F195D0(cCombatant*)` call is safe for an unclassed captain:
`F195EB..F195ED` tests the class pointer and returns before changing HP/death
state when it is null. Assigning an arbitrary authored NPC class would instead
make native act transitions apply that class's HP and death settings to the
captain. `F1B330` iterates authored classes and their native marker-object maps,
then adjusts only the native singleton avatar; an unregistered puppet is outside
that authored-object loop.

Native avatar creation is a singleton operation, not a general second-player
factory. `F432C0(data, position, rotation)` allocates `data+8C`, initializes that
marker with class sentinel `-2`, and invokes internal factory `F3F8C0`. That
factory derives its `isAvatar` argument by comparing the marker address with
`data+8C`, then calls `CreateObjectInstance EEDD40`. In the creature branch,
`isAvatar=true` reuses the noun manager's current avatar instead of creating
another creature, and calls `D2E580` to update avatar properties. `GetClass
F3E740` has no special `-2` handling: it performs a regular resource-class map
lookup. Copying the sentinel or reusing the singleton avatar marker does not
register an independent captain. A separate captain representation must preserve
the native singleton and exclude remote player puppets from authored NPC act
setup; a general native multiplayer-avatar registration API remains unverified.
The direct callers of `C070D0` are `F1E62F` (global act setup), `EF2D84`
(scenario captain/posse setup), `D53570` (posse operation, already gated on class
not being `-1`) and `F201F7` (authored-object setup). A guard at the shared act
setup entry covers these paths for registered remote captain puppets.

After the captain act guard, both native games survived act changes through
acts zero, one and two. The next live failure was a persistent guest lifecycle
conflict: the host had 433 live marker-backed objects across 417 placements,
the guest 390 across 385. Read-only native map/object evidence is saved in
`out/windows-lab/runtime-bootstrap-3/actor-maps-20261023-124752.json`; class keys
and native powerup links are in `actor-links-20261023-125033.json` beside it.
These are real multiple incarnations, not just a stale pointer: markers
407/408/409/412..416 had two host objects versus one guest object, while markers
177 and 206 had mixed creature and ornament nouns at the same placement.
The missing 32 placements also require caching the complete authored marker
vector rather than only markers observed on guest objects.

Classes 1312/1313 in that adventure have creature resource keys and nonnegative
class-act powerup settings. Therefore selecting a factory solely from
`IsConsumible` creates an ornament when an authoritative creature is needed.
`recreate()` now accepts an optional authoritative noun. Creature/vehicle/building
nouns select the ordinary object factory; ornament/pickable-ornament nouns may
select the specialized powerup factory. The created noun must match before map
registration, or the new actor is destroyed and the request fails explicitly.

A nonzero requested noun also permits an additional incarnation after the
existing live forward owner validates marker, class and native forward/reverse
lookup consistency. The helper retains that old owner through a native intrusive
reference, temporarily binds the new actor for marker-state setup, then restores
the previous forward owner. The new actor retains its own reverse entry and noun
registry ownership. Removing that additional actor must preserve the other live
forward owner. Calling the legacy API without a noun still rejects a live-slot
conflict. The Win32 fixture covers both mixed-noun creation and simultaneous
incarnations, reference balance, metadata/reverse aliases, factory mismatch and
independent transforms, with zero failures.

Complete-roster cleanup can also remove an unbound object which currently owns
the forward slot while a required sibling has only a reverse entry. `remove()`
therefore accepts an optional retained sibling. Before mutation it validates the
survivor's live marker/class and reverse placement; conflicting victim reverse
entries are rejected too. It holds a temporary native intrusive reference to the
victim, switches the existing forward value to the survivor with
`AssignObjectReference`, erases the victim reverse entry and retires the victim
from the noun registry, then releases the temporary reference. An already-retired
victim is not destroyed again. Survivor transforms, visibility and marker state
are unchanged. The expanded Win32 regression passed live/retired primary
promotion, both reference counts, map agreement, pre-mutation validation and
unchanged survivor state. The caller must first prove every required identity
for that placement is bound, including snapshots newer than the complete roster.

`ApplyMarkerState F3D930` does not directly call either object factory. Its final
operations include native collision setup `EED640`, spatial virtual `+84`, and
`GamePersistenceManager::B48870(object,bool)` at `F3DA8B`; the setup bool is passed
through. The true setup path therefore participates in native persistence as well
as applying the transform. An immediate duplicate spawn through that path has
not been established; changing this bool requires a separate native test.

Some specialized factories can produce multiple objects. Native `F212C0` type
eight creates two linked ornaments, setting parent `+54` to child and child `+50`
to parent via `AssignObjectReference`. Callers must enumerate again after one
factory call so both can bind to authoritative identities. The actual class1341
objects in this fixture were independent type-nine powerups with both links
zero. Reconstructing a missing linked component beside a surviving component
still needs a separate runtime case; the live evidence does not validate it.

Native results revealed a separate state-manager requirement. Results window
handler `F16FA0(this,window,message)` (`ret8`) handles button `07C79948` by hiding
the panel and setting its result field `+14` to one. Cleanup `F17750` publishes
native message `070B18C7`, payload `+8=1`. `cScenarioPlayMode::HandleMessage
F1C570` handles this at `F1C994`: ScenarioMode `+D0=0` returns to editor through
`EF1280`, one calls `EEFFD0`, and two returns to Space. Both isolated games had
the expected quick-play context `+D0=1`; changing that field is not the fix.

`EEFFD0` calls native `IStateManager::SwitchToState(02CCDA9C)`, the menu entry
application state. Native Sporepedia Play first enters application state
`21851EBE` (`DFBFAF..DFBFC0`). The initial co-op replay entered only the Scenario
game mode, leaving the guest's application state at menu entry. Consequently
Done became a same-state no-op: `SwitchToState 7E3BF0` compares manager `+88`
against the requested state at `7E3C18` and returns without running state actions
at `7E3C1C`. This explains a closed results panel with the guest still showing
the Scenario HUD. The separate host/guest state-manager snapshots are saved as
`out/windows-lab/runtime-bootstrap-3/results-state-21.json` and `-22.json`.

The launch recipe must enter application state `21851EBE` through the verified
native state manager as well as ensure the Scenario game mode is active. The
host may already be in that application state after its original Play request
was intercepted; in that case the state transition is a no-op and the explicit
game-mode entry remains necessary. Both operations belong under the co-op replay
guard. A results-button detour would hide the missing launch context instead of
matching native quick play.

### Native captain XP diagnostics and persistence

`adventure_progress.*` provides a read-only diagnostic for the original local
captain key. `LoadAssetProperties 5BF1E0` is cdecl with `(ResourceKey*,
PropertyList**)`, bool return and plain returns at `5BF21B/5BF21E`. It calls the
native property manager and transforms the resource group to
`(captain.group & FFFFE5FF) | E500`. The property resource type is `00B1B104`;
captain group `40626200` therefore maps to `4062E500`. The returned property list
owns an intrusive reference which must be released once through virtual `+4`,
including a failed load that nevertheless supplies a reference.

The native property list's virtual `+24` is
`thiscall(list, propertyID, Property** out)->bool`; native results call it at
`F15848/F164CD`. Captain XP is property `CF837237`, level is `5888EF41`, both
Int32 (native property type `9`, at property `+12`). Missing XP is the native
zero baseline. `App::Property::GetValueInt32 41E950` is thiscall with no stack
arguments, plain return `41E9B0`, and handles inline data versus pointer flags
`0x30`. The helper checks type before calling it, validates the value pointer,
and reports missing versus present values separately. It never calls a setter
or save operation. The exact executable and both symbols are required.

Results setup `F15770` reads the previous XP and computes a target. Results
object `+B8` is the local captain property list, `+C0` is previous XP and `+C8`
is target XP; the earned points come from `F19080`, which returns PlayMode
`+C4` only for successful state five and a non-editor entry. Setup clamps the
target at the maximum rank threshold when required. Showing a successful
ending or the XP bar alone does not prove that progression was saved.

The actual native write is `F16460(thisResults,bool)` (`ret4 F16EA1`), called
from the results handler at `F17728` when the panel is accepted. It writes
`int(results+C8)` to `CF837237` through property-list virtual `+14` at
`F16528..F1652D`, then updates level `5888EF41`. Later `F16D60` calls native
`5BF220` with the original captain key and its updated properties. That routine
stores the property list through `6B1CE0`, updates the native creation database
and returns success in AL. The mod preserves this ordinary local results path.

For a low-rank captain and a 100-point adventure, capture each original key and
XP before the successful attempt, before accepting results, and after native
Done. The expected saved increase is exactly 100 per captain, subject to the
native maximum-rank cap. Re-reading after duplicate/stale completion traffic
must stay at that value. Reads while the same process is alive can return cached
properties; persistence requires the same value after restarting the isolated
profile or independently inspecting the written resource. A mod-only reload is
not a disk persistence test. Reaccepting the same result object writes its
absolute target; reopening results and calculating another target is the route
which would need the coordinator's per-run reward guard to prevent an extra award.

The Win32 fixture `tests/adventure_progress_tests.cpp` passed 10,000 repeated
reads, native cdecl/thiscall argument forwarding, inline/pointer Int32 values,
missing/wrong properties, failure cleanup, exact intrusive reference balance
and no writes to progression data. It uses fake native adapters. A later controlled
native success fixture reached Mission Complete, selected an unlocked reward and
accepted Done on both players. The original captains each changed from XP 0/level 0
to XP 100/level 1, with one suppressed score submission and native result guard
state 5 each. Both returned to the menu. Restarting both complete game processes
with the same isolated profiles retained XP 100 and level 1 on each original key.
Artifact `out/windows-lab/adventure-results/result.json` includes the before/after
values and `restart_persistence`. This is a controlled result/progression/save
PASS; completing every objective through a full adventure remains unvalidated.

### Native party relationships and social interaction audit

`BA40E0` is `int thiscall(speciesRelationshipManager, ResourceKey*, creature1,
creature2)`, with three stack arguments and `ret0xC` at `BA41CE`, `BA4235`,
`BA424A`, `BA4259`, `BA437D` and `BA43BD`. Its Scenario branch ignores the
resource key and chooses the non-avatar participant by creature flag `+B58`
bit `0x200`. It then resolves that actor's class (`B18470`) and current authored
act relationship (`BA29F0`). A missing class yields relationship zero. Therefore
an NPC relationship query against an unclassed remote captain can return zero,
while the ordinary local avatar query uses the NPC's authored relationship.
`C0B9C0` accepts only relationships two and five as hostile. This is a verified
classification defect; it does not prove that every AI acquisition path uses it.

`adventure_relationships` hooks the verified native getter and aliases only
registered remote captain arguments to the real local avatar while a committed
co-op Scenario is active. Authored NPCs, the resource key, manager, null paths
and native integer return remain unchanged. It does not write avatar flags,
object fields or relationship records. The real avatar callback must bypass
temporary `GetAvatar` overrides. Its fake Win32 ABI fixture passed 20,008 calls,
both argument orders, captain-versus-captain, nulls, inactive/solo paths,
capability failures and no object mutation. Native NPC acquisition and damage
against a guest still require the live fixture after integration.

Ordinary native Talk initiation is independent of NPC autonomous updates:
the Scenario UI calls `F1B240` eligibility and `F1B1D0(play,target)` at
`F0A67B..F0A698`. `F1B1D0` checks gameplay state three, a valid authored class,
proximity through `F19F50`, and an incomplete TalkTo goal matching that class.
It calls `F1A000(play,goal,target)`, which stores goal `+C8`, target `+CC`,
dialog index `+D4`, and cinematic token `+D8`. Native page/greeting messages
read dialog from the authored class/act directly. Finished message `044F1189`
with the matching token calls `F1C1D0` and then `CountGoal`. No NPC Update wait
occurs in that path. This establishes the native call structure; an injected
goal callback does not validate ordinary mouse interaction or the dialog UI.

The social minigame has a separate recipient dependency. Init callback
`DA4360` distinguishes the initiator by order mask `0x10000`, type one.
The recipient path at `DA488C` joins the initiator's decider context. Tick
callback `DA4950` reads actor `+B4C -> +600`, with shared social data at decider
`+1A4`. Recipient work consumes response orders `0x10000000`, advances shared
response count `+4C`, and handles participant animations. Suppressing the
entire live NPC `C0AE30` Update also suppresses its action timers and behavior
tree, so social minigame completion needs independent validation or a narrowly
scoped recipient adapter. Do not infer it from the working OnBefriend/OnAlly
goal forwarding tests.

The scoped native adapter is now `adventure_social`. `game::h_Update` dispatches
an optional blocked-update handler exactly once when a filter rejects the real
simulation call. Calling a pure filter for ownership or diagnostics does not run
presentation. The adapter requires a running, unfrozen guest Scenario, a current
live mirror binding and an explicit native social order. The primary performer
must be the real local captain's target, with social order `0x10000`, kind one,
targeting that captain. Native supporters require kind two targeting that
performer and join its existing shared controller. At most 32 participants are
tracked, matching the native controller capacity requested at `DA4426`.

The native callback table at `159339C` contains behavior `341BD4BE`, start
`DA4360`, tick `DA4950`, end `D9F7A0`. These are cdecl callbacks: start takes
seven stack words, tick eight, end six. Generic BT call sites `BC943D`,
`BC9477/BC969E`, and `BC94B4` clean `1C`, `20`, and `18` bytes respectively.
All include actor, split double time, context words, and a callback-state pointer;
tick additionally receives float delta seconds. Start initializes only POD
callback fields `0/4/8/C`. It constructs the separate shared controller through
native `BCB220/D9F740`, which initializes the intrusive references and EASTL
containers. The adapter never substitutes zeroed memory for those native objects.
Controller ID is `+8`, completion flag `+1C`, performer `+1A4`, captain `+1A8`.

The helper owns its initialized POD state and runs native end exactly once on
false tick, cancellation, death, binding/act/run change, or explicit reset while
the actor still owns that controller. End closes the native rainbow, preserves
normal posse side effects, detaches the native shared reference through
`BCB5A0`, and removes social orders. If native teardown already replaced or
destroyed the controller, the helper does not call an old exit against the new
one. It rejects an already initialized native SocialPropose BT frame rather than
duplicating its lifecycle. Actual noun-registry membership is checked before
reading stale actor pointers. Unrelated NPCs perform no avatar/partner registry
scans; engaged sessions sweep at most once per rendered frame.

This leaves the complete NPC Update, autonomous target selection, combat and
movement suppressed. Native social callbacks drive the ordinary presentation and
emit the existing verified Befriend/Ally completion path. The player counterpart
is the separate native SocialRespond behavior `941BD4BF`, start `D9DB00`, tick
`D9C100`; it runs normally on the local captain. Its tick creates the performer's
kind-one social order at `D9C25F..D9C2A1`, then sends response orders back to that
NPC. These two sides must advance for the minigame to finish.

The fake Win32 fixture passed 10,000 repeated social ticks, all three callback
ABIs, native construction/refcount balance, explicit shared supporters,
cancellation, inactive/frozen/host/solo scope, actor death, run/act/binding
changes and partial-init failure cleanup. A separate fixture includes the actual
game Update wrapper and checks exactly-once rejection dispatch and pure filter
queries. These fixtures verify adapter boundaries, not a completed native social
UI session. Ordinary Talk/Befriend/Ally interactions still require the live pair
test after integration.

`C0E800` is the native `int thiscall(creature)` social-eligibility getter,
with no stack arguments and nine plain returns. Its Scenario relationship
mapping is three to social mode two, four to mode one, one/two/five to zero.
Relationship six permits mode four (recruit) only when the native posse has
capacity and the actor is not already a party member. Native success sets
`B58` bit `80` before calling `OnBefriend` (`D9BD68` then `D9BDA1`), so the
completed guest callback can be distinguished from arbitrary pre-success calls.
The host has not applied that success yet and must validate its pre-replay
eligibility rather than demand its own impressed flag beforehand.

Live read-only audit of Adventure Town (`BF0CE0D0/366A930D/408A0000`) found
five acts: Move markers 169/170; Talk to Mr Mayor 93; Talk to Gallery Owner 55,
Playwright 95 and Ms Spoffit 214; Talk to Carnie 185 and Captain Amazing 292;
then Talk to Mr Mayor 93. Every required count is one. There is no Befriend or
Ally objective, and allowed posse count is zero. In act two, its ordinary citizens
are authored team two (relationship six); the team-one guards have custom
behaviors without entry type eleven and classify as hostile five. Neither class
is an appropriate native social-minigame fixture merely by changing stance.
Artifacts are `out/town-authored-acts.json` and `out/town-social-candidates.json`.

`adventure_catalog::init` registers the read-only outside-frame diagnostic
`coop.adventure.catalog <stock instance hex> [more keys]`. It uses the same
verified six-argument private resource getter as the collector, casts
`E742574A`, bounds-checks authored act/goal/marker vectors, reports captain
restriction and posse capacity, then releases each native private clone exactly
once. It never replaces or launches the active adventure. An offline read of
the installed EP1 Data package index found 69 stock scenario records; their
compressed metadata uses localized identifiers, so literal title searches alone
cannot select a suitable objective test. The ignored catalog index is
`out/stock-adventure-index.json`.

Live pair read-only relationship inspection found a separate authority issue:
Town guards 140/173 had native relationship-cache values six on the host and
five on the guest, despite identical authored class 1198, act two, team one,
stance one and custom behavior entries. The native `BA40E0` Scenario branch
checks its per-creature cache at manager `+40` before the authored fallback.
The cache therefore carries runtime state that captain aliasing alone cannot
synchronize. The captured proof is `out/town-guard-relations.json`; the setter
that produced those particular host entries was not established. Native social
event three changes cached neutral three to six in Scenario, but does not
directly change hostile five to six (`BA4A00`).

`adventure_relationships::query_local_party` captures the host's current native
NPC-versus-real-avatar value through the original evaluator. Actor snapshots
carry that enum. The guest relationship detour may return the authoritative
value only for one recognized party captain and one exact current live mirrored
NPC accepted by the world binding accessor. It preserves native evaluation for
NPC-versus-NPC, captain pairs, nulls, inactive attempts, absent/stale bindings
and invalid enum values. No guest native relationship-cache entry is written.
The Win32 fixture checks all eight native enum values, both query directions,
local and remote captains, missing/stale accessor results, malformed enums,
unchanged native forwarding, and capture without recursive snapshot lookup.

The live read-only catalog scan passed for all 69 installed stock scenarios
without changing the active mode. Its saved output is
`out/stock-adventure-native-catalog.json`. Stock `EF9DD7F3` is an unlocked
seven-act music mission with posse capacity three: act zero talks to class 22,
act one talks to and allies with class 52, and act three repeats Talk/Ally for
class 1042. No scanned stock goal had Befriend type four; the Ally targets'
actual pre-recruit social relationship still needs inspection in the loaded
mission. `AF1BC5FF` is an unlocked five-act Grox infestation combat mission,
beginning with Talk class 15 and then Kill goals. Private scenario-resource
clones have unassigned marker IDs `FFFFFFFF` before normal world initialization,
so catalog class matches do not establish runtime marker IDs.

Concert in the Park (`EF9DD7F3`) provided the first live social-controller
diagnostic. Lotta Pipes is marker 164/class 52, authored/cached relationship six,
with Sing type 37 and recruit social mode four. Fancy Chancey marker 92/class 11
is neutral three and uses Charm/Sing/Dance, matching the guest captain's actual
abilities; Colonel Jim Farkus marker 80 also has Radiant Pose, which that captain
does not have. These are loaded-world observations, not successful minigames.

The ordinary UI opened its rainbow and the scoped NPC callback reached state
six, selected Sing and completed its animation. It then waited for the captain's
response (controller `+1FC` bit one, ten-second timer `+1E8`) and timed out.
Read-only inspection of the actual DLL's owned callback state and native player
BT proved that the player stayed in SocialRespond state two without a queued
ability. The cause was native UI eligibility `D1E720`, cdecl `(avatar,target)`
returning int with plain returns: at `D1E79A..D1E7E1`, a SocialRespond player is
blocked with result five unless its target has a native SocialPropose BT leaf.
The separate callback adapter intentionally has no such native frame. The
ability tray's `D49560` dispatch treats result five as a no-op, so ordinary
hotkeys could open the interaction but never submit their response.

The scoped helper now hooks that exact eligibility function. It preserves the
original result except five, and returns allowed zero only for the real local
SocialRespond captain and its exact currently targeted, live mirrored primary
performer. Run, act, incarnation, binding, controller ownership, POD callback
state six and the native controller's ready byte `+1E4` must all match. Its active
context also requires authoritative captain life Alive. It never writes native
BT state or broadens NPC Update. The Win32 regression checks that case, unchanged
other native results, inactive/downed/frozen scope, stale run/act/incarnation,
foreign actors/controllers, unready or completed controllers and 10,000 repeated
two-argument cdecl calls. Actual UI completion still requires the next live run.

The next live ordinary UI run passed recruitment in Concert in the Park:
`out/windows-lab/native-social-input/lotta-recruit-sing/result.json` records the
guest's body click and native Sing key input, shared Ally completion and act one
to two on both processes. The active controller trace recorded native player
SocialRespond state two to three, performer state six, matching response flags
`F`, then native win state eight with recruit-success bit `10`, followed by native
cleanup. The guest's live ability tray maps key one to Sing (index five, strength
three), key two to Dance (index twelve), and key three to Charm (index two).
An earlier key-two attempt therefore submitted Dance against Lotta's Sing and
lost normally. Native Kid marker 38/class 20 is neutral and has only Sing strength
two, making it a suitable separate nonobjective Befriend fixture.

A later legacy `coop.w2s` diagnostic crashed the isolated guest at EIP zero:
the restricted map does not export `App::cViewer::GetCameraToPoint`, and that old
command called the missing inverse unconditionally. This was a diagnostic call
after the ordinary Ally proof, not a native minigame failure. The fault log is
`out/windows-lab/native-social-input/read-only-debug-crash-22.log`. The command
now checks all required native capabilities before any game/camera call and
guards null manager/viewer and failed inverse results. The Win32 regression in
`tests/adventure_debug_capability_tests.cpp` invokes the actual command and proves
that missing capabilities execute zero game/native calls. Its complete fake ABI
path also passes. Legacy diagnostics must be checked against the active restricted
map before invoking them. The ignored camera projection helper remains unvalidated
for click targeting; a behind-camera result does not establish avatar location.

## Large adventure frame cost on native Windows (2026-10-23)

Two visible preview-1 sessions in a 438-actor adventure measured 5.7-5.9 FPS on
the host and 5.1-5.3 FPS on the guest. Both used the RTX 5070 Ti Laptop GPU;
GPU load was about 1%, while each main thread saturated a CPU core. Sampling
100 main-thread instruction pointers per process found 97 host and 99 guest
samples inside `NtQueryVirtualMemory`. COFF/DWARF resolution attributed the
callers to `skg::readable`, notably marker lookup, play/scenario accessors and
authored-hostility validation. Neither process recorded native faults.

`ReadOnlyMemoryScope` now shares bounded memory-region checks inside synchronous
read-only enumeration, snapshot collection and mirror-eligibility operations.
No scope spans mirror application, actor spawn/destruction, act changes, a queued
task or a frame boundary. Unscoped checks still query Windows on every call.
Native `active()` eligibility no longer commits the result lifecycle or validates
the play-mode pointer on each call; that bookkeeping runs in tick and SetState.
Guests enumerate in their outside-frame reconciliation task, rather than also
enumerating in Present immediately before that task.

The Win32 memory fixture covers 438 repeated/nested actor checks, protection
changes between traversals, cross-region reads, guard pages, freed allocations
and overflowing ranges. The integration regression measures actual frame-count
deltas in a recorded isolated pair, requires at least 400 actors and a native
playing state with advancing game time, and rejects run/act changes or faults.

The controlled comparison used the same two hidden native lab processes and
Adventure Town's 405-actor opening act. Preview 1 failed the 20-FPS regression at
3.87 FPS host / 4.75 FPS guest. Preview 2 passed at 21.61 / 21.49 FPS with advancing
native time and zero faults. These hidden-window figures are not a claim of the
visible player's final frame rate. Evidence is in
`out/windows-lab/frame-cost/performance-before.json` and `performance-after.json`.
Run the check after dismissing the opening dialog; the fixture deliberately
rejects a cinematic/non-playing state. The live large-world check also retained
429-436 matching actors with zero native faults.

The final stripped DLL also passed native combat/proximity revival, shared Move
progress through an act change, and controlled results/Done with zero faults.
The reused captains already had XP 100. The first results assertion expecting
another +100 failed; a separate repeat-result check expecting unchanged XP
passed at 100 -> 100 for each captain. This pass establishes result lifecycle
and preservation of saved progression, not another first-completion XP award.
The result fixture now waits for the ending dialog to appear before clicking
Finished, rather than checking for it once immediately after SetState.

From the repository root, after the lab pair is playing Adventure Town:

```powershell
py -3 tools/tests/coop_adventure_performance.py --metadata out/windows-lab/frame-cost/processes.json --output out/windows-lab/frame-cost/performance-after.json
```

`make test-memory-windows` builds the standalone Win32 memory regression; run
`build/host/readable_memory_tests.exe` on Windows. Its bounded-cache overflow
case also checks more than 64 distinct readable/inaccessible memory regions.

## Preview 5 party-flow symbols and native validation (2026-10-24)

The restricted map now includes verified HandlePlayMessage F1C570 (thiscall,
ret8), TalkBegin F1A000 (thiscall, ret8), TalkFinish F1C1D0 (thiscall, ret),
ActivateGate F23AB0 (cdecl, tailcalls F1C390), GateKey F21670 (cdecl, uint32)
and OpenGate F238C0 (cdecl, return F23AA2). The verifier checks121 functions
and4 globals against the installed dc04 executable before map generation.
See `out/party-flow-re/symbols.json`, [adventure.md](adventure.md#preview-5-shared-scenes-and-replay-dc04)
and [adventure-world.md](adventure-world.md#preview-5-colored-gates-and-physical-powerups-dc04).

The final preview5 DLL passed hidden, muted four-client intro, objective Talk
and native colored key/gate checks, including forced reconstruction of an open
gate. Two-client results Replay, captain replacement/previous-selection and
scene cancellation/disconnect/retry checks also passed. The405-actor regression
measured21.49 FPS host and21.62 FPS guest with native clocks advancing and zero
faults. These numbers describe isolated hidden windows, not visible play speed.

One exploratory test process was stopped by a diagnostic call whose target was
mistakenly supplied as decimal text. The console `call` target parser expects
hexadecimal, so that text resolved to the wrong native address. Both isolated
test processes were replaced and affected checks rerun. This was a probe error,
not a game-flow failure, and no player profiles were involved. Use named symbols
or explicit0x addresses with verified signatures in native diagnostics.
