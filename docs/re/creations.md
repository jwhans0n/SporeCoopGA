# Player creations: storage, runtime loading, avatar swap (GOG 2016 build, sha256 922af7c7…)

Goal: every player keeps their own cell / creature / spaceship, everybody sees it, and it plays
with its own parts. Today puppets are built from the *local* player's model (modes.cpp:
`creature_spawn` uses the local avatar species, `cell_spawn` the local `+0xFC` key, `space_spawn`
the local `cEmpire::mUFOKey`), and the guest's own avatar is whatever the host's save contains.

Static analysis only (llvm-objdump of `SporebinEP1/SporeApp.exe`, Ghidra via REA for a few
functions, a DBPF reader over the test profiles and the game packages). MSVC x86 conventions:
`thiscall` = `this` in ECX, callee pops `ret N`. Legend: **✔** verified in this build's
disassembly (prologue/`ret N`/call sites), **~** likely (behaviour inferred from code, not traced
end to end or not run), **?** unknown / hypothesis, **[ModAPI]** = from memory of Spore-ModAPI,
addresses differ.

ResourceKey in memory = `{u32 instance, u32 type, u32 group}` (✔, as in cell.md / FindRecord
0x8DFC70 reads `key+4` as the type). In save files (serialized nouns) a key is written
**big-endian as group, type, instance** (✔, seen in every .spo / stars.db record).

Scratch tools used (not part of the repo): `$CLAUDE_JOB_DIR/tmp/creations/dbpf.py` (DBPF 3.0 +
DBBF reader with RefPack), `keys.py` (finds serialized creation keys), `meta.py` (pollen metadata).

## 1. How creations are stored

### 1.1 Types and groups (✔ from the game packages; type ids are FNV-1 hashes of the extension)

| creation | resource type | group | example (Spore_Game / Spore_Content) |
|---|---|---|---|
| creature | `crt` 0x2B978C46 | 0x40626200 | 47 XML `.crt` in Spore_Game, ~760 `bem` in Spore_Content |
| cell | `cll` 0x3D97A8E4 | 0x40616200 (`*(u32*)0x15A8D0C`, used by GetModelKeyForCellResource) | premade player cell 0xE219E5D3 |
| building | `bld` 0x2399BE55 | 0x40636200 | |
| vehicle | `vcl` 0x24682294 | 0x40646200 | |
| UFO | `ufo` 0x476A98C7 | 0x40656200 | premade 0x3A948398, 0x0665F6CE |
| flora | `flr` 0x438F6347 | 0x40666200 | |
| (adventure assets?) | - | 0x406B6200 | bem only, not researched |

Record types that hold a creation:
- `bem` 0x1A99B06B - binary editor model (magic `D3 7E 65 48`), the normal storage form. 2.6–10 KB
  for the test creatures. **No self-reference to its own key inside** (✔ searched LE/BE) ⇒ a
  receiver may store it under a different instance id.
- `crt`/`cll`/`ufo`… as XML `<sporemodel>` (formatversion 18) - Maxis data in Spore_Game and
  creations *imported from PNG* (Pollination, tagged `tag:spore.com,2006:ImportedContent`).
- `creaturedata` 0x0F43029A - derived species data (abilities/stats cache). Read/written by
  `cCreatureDataResource` (Read 0x4BF920, Write 0x4BFC60, ported). `cEditor::LoadCreatureData`
  0x4BB4B0 (~) looks it up by `{instance, 0x0F43029A, group}` and **computes a new one when it is
  missing** (alloc 0x128 + 0x46AB00) ⇒ optional for a transfer.
- `png` 0x2F7D0004 in the model group (Sporepedia card 128×128, model data steganographic in the
  pixels, no extra chunks) and in group+1 (0x40626201, plain thumbnail ~). Not needed to play.
- `pollen_metadata` 0x030BDEE3 (big-endian: version 13, own key {type, group, instance} at +0x0C,
  parent key, timestamps, UTF-16BE author (the profile user name) and creation name). Useful for the UI.
- prop 0x00B1B104 in group 0x4062E500 (per-creation prop, not decoded).
- Baked meshes/textures of Maxis creations: 0x00E6BCE5 in groups +2/+3, 0x41626202/03,
  0x40627100/01 and 0x2F4E681C in 0x40622900/01. Baked data of player creations lives in
  `GraphicsCache.package` (607 MB in the test profile) ⇒ the game bakes on demand (~).

The resource factory for creations (✔ 0x4BC700 = GetSupportedTypes, 0x4BC780 = IsValid,
factory id 0x4FFF05D3 at 0x4BC6F0): supports crt, bld, vcl, ufo, flr, cll, creaturedata, bem, and
**a `bem` record may produce a crt/bld/vcl/ufo/flr/cll resource**. The resource manager maps a
requested type to record types through its type map (`cResourceManager+0x54`, used by FindRecord
0x8DFC70; SetTypeMapping 0x8E1BF0, GetTypeMapping 0x8DFC30, ported) ⇒ a key `{crt, 0x40626200, X}`
resolves to a `{bem, 0x40626200, X}` record in any registered database (~ - this is exactly how
EditorSaves.package, which contains only bem, serves crt keys).

### 1.2 Profile files (test profile `SporeKit-test/Spore`, ✔ listed with the reader)

| file | format | content |
|---|---|---|
| `EditorSaves.package` | DBPF 3.0 | **My Creations**: per creation `bem` + `png` (0x40626200 and 0x40626201) + `pollen_metadata` + prop (0x4062E500). Test profile: 7 creatures 0x29020CC4…0x290229B9 (Chitis, Ms Spoffit, Ciminerdo, Kirgle, Gorkbuddy, Judge, Tiger Horse); no cells/UFOs saved. |
| `Pollination.package` | **DBBF** (64-bit DBPF, index offset u64 @0x38, 32-byte entries with u64 offset) | Sporepedia cache: 2600 summaries `0x02D5C9AF` (groups 0x4061…0x4066 6200, 0x408A0000), `creaturedata` for the player's creatures, imported PNG creations as XML crt (+png, metadata). |
| `GraphicsCache.package` | DBBF | baked meshes/textures of creations |
| `Planets.package`, `RigblockInfo.package` | DBPF | planets / rigblock cache (not researched) |
| `Games/GameN/*.spo` | DBPF | one per planet; records keyed by **noun id** (type), group 0, instance 0x0179D304 (+ 0x01897C18 self-keyed record). Contains only *keys* of creations, never their bodies. |
| `Games/GameN/stars.db`, `planetRecords.pkp/.pkt`, `PlanetScripts.pld`, `GGEUserData.dat` | DBPF | galaxy (star records 0x03E4353C carry empire species keys; empire records 0x018EB9D2 carry bld/vcl designs), planet records, scripts, galaxy-game-entry user data (planet name) |

Records inside a .spo that reference the player's creations (✔ by scanning, names ~):
- **`cPlayer`** noun **0x02C21781** (GetNounID 0xC755D0 returns it ✔; instance kept at
  `cGameNounManager+0x74`, created lazily by 0xB20ED0 ✔). Holds the player's current designs:
  creature fixture → `crt 065CBE70` (the avatar species); space fixture → `bld 067C701A…`,
  `vcl 110771ED/A7278589/B40E95C5`, `ufo BDFB7DC5` + lists of encountered creations. Field
  offsets not decoded (?).
- `cCreatureAnimal` 0x018EB45E (species key per creature, mSpeciesKey +0xB28 in memory),
  herds 0x01BE418E (species per herd), citizens 0x018EB4B7, UFO 0x018EBADC (`mDesiredModelKey`
  +0x780 → `ufo BDFB7DC5` = the player ship).
- Cell stage record **0xABCDEF00** (+ 0x01897C18): `cll 40616200 E219E5D3` = the player cell
  (= cCellSerializableData+0x10, §2.2).

The fixtures were started from Maxis premades (0x065CBE70 creature, 0xE219E5D3 cell,
0x3A948398 UFO, all in Spore_Content) - those keys resolve on every machine. A real player's own
creations are 0x29xxxxxx keys from **their** EditorSaves.package and are unknown elsewhere.

**Instance collisions:** creation instance ids are allocated per profile and sequentially
(both test profiles contain the same 0x2902xxxx set with *different* history possible). Two
players can own different creations with the same key ⇒ a receiver must remap the instance
(e.g. `0x7C000000 | hash(peer, instance) & 0x00FFFFFF`; bem has no self reference ✔).

### 1.3 Where the game opens My Creations (✔ 0x6B3930…0x6B39DE)

`0x6B33C0(dirId 0xA02151, L"EditorSaves", Database** out, saveArea 0x11AC199, bool)` → builds
`<AppData>/EditorSaves.package` and calls the package opener **0x6B2FE0** (§3), registered with
**priority 1000**; then `RegisterSaveArea` 0x6B36F0 maps save-area ids 0x11AC199, 0x11AC19A,
0x11AC19B, 0x90368EA3, 0x90368EA2, 0x90368EA0, 0x86CA01C9 to that database. If global
`0x16056E0 == 0` a directory database "EditorSaves" (folder) is used instead (~).
⇒ `Resource::GetDatabaseByID(0x11AC199)` (0x6B1F20, cdecl, already in overrides) should return the
EditorSaves database (~). `Database::GetKeyList` (vt+0x30, DBPF 0x8D8A20, `ret 8`:
`(vector<ResourceKey>& dst, IResourceFilter* filter)` ✔ ret) lists its keys.
The editor saves a creation with `cResourceManager::WriteResource` (vt+0x20, 0x8DED90) for the
`bem` key, then the `creaturedata` key, each followed by vt+0x6C (CacheResource) (✔ 0x577722…
0x5777E9 inside Editor_Save 0x577650).

## 2. Runtime: key → species profile / cell model

### 2.1 Species profile (creature, tribe, civ citizens)

| addr | what | conv |
|---|---|---|
| 0x401090 | `cSpeciesManager::Get()` | ✔ cdecl, returns `*(void**)0x15D1C24` |
| **0x4DF580** | `cSpeciesProfile* GetSpeciesProfile(const ResourceKey*)` | ✔ thiscall(mgr) `ret 4` |
| 0x4DF700 | map lookup (mgr+4 = key→profile map) | ✔ thiscall |
| 0x4DA3D0 | `bool KeyExists(const ResourceKey*)` = `RM->GetResource(key,0,0,0,0,0)` (vt+0xC) | ✔ cdecl |
| 0x4D3E00 | `cSpeciesProfile::ctor(this, const ResourceKey*, 0)` (object 0xA18 bytes, alloc 0xF47240) | ✔ call site |
| 0x4DF450 | `GetDefaultSpeciesProfile()` = `GetSpeciesProfile(mgr+0xA4)` | ✔ thiscall(mgr), plain `ret` |
| 0x4DF410 | thiscall(mgr, profile) - called after SetSpeciesProfile for avatar-flagged creatures | ? |

GetSpeciesProfile ✔: key.instance==0 → default key `mgr+0xA4`; cached? → return; else
**`KeyExists(key)` through the resource manager; false → return null *without caching*** ;
true → `new cSpeciesProfile(key)`, insert, return. ⇒ A profile for a remote creation appears as
soon as its record is reachable through any registered database; earlier failed lookups do not
poison it. (Abilities come from the profile; `creaturedata` is computed if absent, §1.1.)

On a creature: `cCreatureBase+0xB20 mpSpeciesProfile`, `+0xB24` profile version (`profile+0x53C`),
`+0xB28..+0xB30 mSpeciesKey` (= `profile+0x504`) (✔ SetSpeciesProfile below).

### 2.2 Cell model key (✔ 0xE65650)

`ResourceKey* __cdecl GetModelKeyForCellResource(ResourceKey* out, cellRef)` (hidden out first,
plain `ret`): walks the .cell resource's model entries (`res+0x14`, count `+0x18`, 0x28 bytes
each, kind at +4):
- kind 3 → `{entry+0x10, cll 0x3D97A8E4, *(u32*)0x15A8D0C = 0x40616200}` (fixed cell model);
- kind 4 → random creature model (0xE655D0);
- **kind 5 → the player's cell: `S+0x10..+0x18`** where `S = *(u8**)(*(u8**)0x16B4BE4 + 0x5190)`
  (= cCellSerializableData) - `{instance, type, group}`;
- none → `{0,0,0}` (globals 0x16B4DB4..BC).
The cell's current model is then `cCellObjectData+0xFC` (cell.md). For the avatar and for
puppets built from the avatar's resource, kind 5 applies ⇒ `S+0x10` is "the player's cell".

## 3. Registering an extra package at runtime (✔)

The game's own opener for save-area packages, **0x6B2FE0** (✔ 0x6B307C…0x6B311F), does:

```
db = operator_new(0x388, "name", 0,0,0,0)          // 0x9262B0 cdecl (6 args, add esp,0x18)
DatabasePackedFile::ctor(db, const char16_t* path, ICoreAllocator* 0)   // 0x8DA290 thiscall ret 8
(db+4)->vt[0]()                                     // AddRef (0x8DD4B0, refcount at db+8)
0x8D8840(db, 0x12, float)                           // thiscall ret 8: hole/compaction params (+0x334/+0x338), optional
0x8D8870(db, 0)                                     // thiscall ret 4: byte +0x2E0, optional
db->vt[1]()                                         // Initialize 0x6BBDC0 (sets byte +0xC; Open fails without it)
db->vt[6](AccessFlags 3, CD 6, false)               // Open 0x8D8C90 thiscall ret 0xC
RM = *(void**)0x15FE88C  (getter 0x67DC90)
RM->vt[0x50/4](true, db, 1000)                      // RegisterDatabase 0x8E0FF0 thiscall ret 0xC
```

Details:
- **DatabasePackedFile** object 0x388 bytes; vtable 0x1437290 at +0, secondary (refcount) vtable
  0x143727C at +4 (AddRef 0x8DD4B0, Release 0x69D3C0 → on 0 calls vt[0](1) = scalar deleting dtor
  0x8D9CA0 which frees with 0x9262F0). Ctor: non-null path → SetLocation(path) ✔.
- Primary vtable (✔ addresses = ported names): +0x00 dtor 0x8D9CA0, +0x04 Initialize 0x6BBDC0,
  +0x08 Shutdown 0x8D8960, +0x0C GetDatabaseType, +0x10 GetRefCount, +0x14 Lock, **+0x18 Open**,
  +0x1C Close 0x8D9CC0, +0x20 GetAccessFlags, +0x24 Flush, +0x28 GetLocation 0xA426C0,
  **+0x2C SetLocation 0x8D9CD0** (`(const char16_t*) ret 4`, fails if already open),
  +0x30 GetKeyList 0x8D8A20, +0x34 OpenRecord 0x8DA6E0 (`ret 0x18`), +0x38 GetOpenCount,
  +0x3C CloseRecord, +0x40 DeleteRecord, +0x44 Attach 0x8D88B0, …
- **Open** ✔ `(AccessFlags acc, CD cd, bool lazy)`: acc 1 = read, 2 = write, 3 = RW; cd 6 =
  default → 3 OpenExisting for read-only, 4 OpenAlways when writing; `lazy=true` only stores the
  flags. Read-only `Open(1, 6, 0)` for a received package. The file stream (0x931900…) opens with
  share mode READ for read access and READ|WRITE when writing (✔) - so the mod can read the
  live EditorSaves.package from disk with `FILE_SHARE_READ|FILE_SHARE_WRITE`.
- **cResourceManager** (vtable 0x14375F8, ✔): +0x00 dtor 0x8E2560, +0x04 Initialize, +0x08 Dispose,
  **+0x0C GetResource 0x8DF6F0** (`(const ResourceKey*, ResourceObject** out, int, Database*,
  IResourceFactory*, const ResourceKey* cacheName)` ret 0x18), +0x10 GetResourceAsync,
  +0x14 GetLoadedResource, +0x18 ReloadResource, +0x1C GetPrivateResource, +0x20 WriteResource,
  +0x24 SetTypeMapping, +0x28 GetRecordTypesFromResourceType, +0x2C GetTypeMapping,
  **+0x30 FindRecord 0x8DFC70** (`Database* (const ResourceKey*, ResourceKey* outRealKey,
  Database* in)` ret 0xC), +0x34 GetResourceKeyList, +0x38/+0x3C GetRecordKeyList,
  +0x40 RegisterChangeNotification, +0x44 RegisterFactory, +0x48 FindFactory, +0x4C GetFactoryList,
  **+0x50 RegisterDatabase 0x8E0FF0** (`bool (bool add, Database*, int priority)` ret 0xC),
  +0x54 IsDatabaseRegistered 0x8DE5D0, **+0x58 FindDatabase 0x8DE610**, +0x5C GetDatabaseList,
  +0x60 DoDatabaseChanged, +0x64 RegisterCache, +0x68 FindCache, +0x6C CacheResource, +0x70
  FlushCache, …, +0x78 GetKeyFromName, +0x7C GetNameFromKey.
- RegisterDatabase(add) ✔: refuses an already registered db, inserts into the priority-sorted list
  `RM+0x2C` (0x8DEC00), AddRef, `db->vt[0x44/4](1, RM, 1)` (Attach). Remove = `(false, db, x)`:
  unlink, Attach(0, RM, 1), Release.
- **Lookup has no global key index** ✔: FindDatabase walks the list and probes
  `db->OpenRecord(key, 0, 1, 6, true, 0)` ⇒ a newly registered package is visible at once.
- The App global `0x15FE88C` (getter 0x67DC90, setter 0x67DE80) is what the game uses for
  RegisterDatabase; `Resource::IResourceManager::Get` 0x8DE4A0 returns `0x1668A80` (~ same object).
- Other DBPF creators (ctor callers, for reference): 0x41C1C0, 0x580DF0 (GGEUserData: wrapper
  0x69FA60 + SetLocation + `Open(1,0,0)`), 0x685730, 0x686D50, 0x6872B0, 0x688F90, 0xB276F3,
  0xB27990 (persistence manager), 0xBB8C60 (ReadStarsFromSaveDB), 0xF25E00, 0xF93BE0.

[ModAPI] does the same: `new Resource::DatabasePackedFile(u"path")`,
`dbpf->Open(IO::AccessFlags::Read, IO::CD::OpenExisting)`, `ResourceManager.RegisterDatabase(true,
dbpf, priority)`; newer ModAPI wraps it in a DatabaseManager - not present in this build.

Alternatives (not recommended): writing records into the guest's EditorSaves db (pollutes My
Creations, RW db is flushed by the game); building `cEditorResource` objects and injecting them
with CacheResource (needs the factory's parse path); importing a Sporepedia PNG (writes an XML
crt into Pollination as "ImportedContent" - import function not located, ?).

**Package to write on the receiver** (DBPF 3.0, uncompressed is fine): header `"DBPF"`, u32 3,
u32 0, …, `+0x24` entry count, `+0x2C` index size, `+0x3C` index minor 3, `+0x40` index offset
(u32). Index: `u32 flags = 4`, `u32 0` (constant instance-high), then per entry `type, group,
instance, offset, size | 0x80000000, usize, u16 compression (0 = none, 0xFFFF = RefPack),
u16 1`. Content per creation: `bem` (required), optional `creaturedata`, `pollen_metadata`, png.

## 4. Swapping a body at runtime

### 4.1 Creature stage (and tribe/civ citizens)

| addr | what | conv |
|---|---|---|
| **0xC22410** | `cCreatureBase::SetSpeciesProfile(cSpeciesProfile*)` | ✔ thiscall `ret 4` |
| **0xC038E0** | `cCreatureAnimal::SetSpecies(cSpeciesProfile*, int age, bool keepState)` → bool | ✔ thiscall `ret 0xC` |
| 0xC09B00 | `cCreatureAnimal::Create(pos*, profile, age, herd, isAvatar, bool)` | ✔ (overrides) |
| 0xC05E60 | cCreatureAnimal Read: `GetSpeciesProfile(+0xB28)` → `SetSpecies(p, 0, 1)` | ✔ |
| 0xC22590 | cCreatureBase Read part: re-resolves `+0xB28`, SetSpeciesProfile if changed | ✔ |
| 0xD5BFA0 | tribe: after the editor every member gets `SetSpeciesProfile(tribeSpecies)` when it differs | ✔ (decompiled) |

SetSpeciesProfile ✔: writes `+0xB20`, `+0xB24 = p+0x53C`, `+0xB28..+0xB30 = p+0x504` (key);
null → default profile 0x4DF450; then (unless global byte 0x168E8E4) 0xC21D10(changed, same)
(model rebuild ~), `vt+0xAC(changed)`, and re-picks ability indices via
`GetAbilityIndexByType` 0xC0C1A0 into `+0xE74`/`+0xE70` ⇒ **abilities follow the new species**.
SetSpecies 0xC038E0 adds the cCreatureAnimal part: health/limits from the profile
(`+0x590/+0x594`), 0xC13FD0/0xC211B0, locomotion setup 0xCEE410, scale (`+0xC0` vt+0x40), flags
`+0xB58`, `+0xB5E` (cleared when keepState=0).

Avatar creation on entering the stage (✔ 0xD44040…0xD44187): a herd is built from the start
species (0xB99020/0xB90A20), `herd+0x84 = 1` (owned by avatar), `Create(nestPos 0xC6AE30(herd),
herd+0xA4 species, age 1, herd, isAvatar 1, 0)`; fallback 0xD444DE uses the default profile.
After the creature editor the planet is re-saved/reloaded and the avatar re-reads its species key
(0xC05E60) (~). The new design keys are stored in cPlayer (§1.2).

To give the local avatar another body (~, mirrors what Read does):
`p = GetSpeciesProfile(&key); if (p) SetSpecies(avatar, p, *(int*)(avatar+0xB34) /*age*/, 1);`
optionally also `herd+0xA4 = p` for the avatar herd (nest respawn / babies use the herd
species ~). Heavier alternative: destroy the avatar and `Create(..., isAvatar=1)` (+SetAvatar
0xB1FC70).

### 4.2 Cell stage

Leaving the cell editor ✔ (handler **0xE809A0**, registered for message 0x30C11C7 at 0xE81B6A):
reads the editor result (0xE82C80), result code 0x1100000, then
`S+0x10..+0x18 = new cell model key`, **0xE624C0(key by value)** (cdecl, 3 dwords: adds the key to
the preload set at `G+0x70`, ~), unlocked parts `S+0x34` refresh 0xE4FBA0, then **RebuildLevel
0xE7FD00** (destroys and recreates every cell, avatar gets the kind-5 model `S+0x10`) and posts
message 0x6579712.

To give the local avatar another cell (~): `S+0x10 = key; 0xE624C0(key)`; then either
RebuildLevel (wipes all mirrors/puppets on this machine - acceptable right after joining, before
mirrors exist) or an in-place swap: write avatar `+0xFC`, free its GFX (0xE66020, `+0x248 = 0`)
so 0xE78C00 recreates it lazily (?, soft-body particles `+0x250` come from the GFX - needs a
runtime test). Remote puppets: already spawned with the forced-model hook on
GetModelKeyForCellResource; just use the remote key (and call 0xE624C0 first).
How cell abilities (mouth/weapons/movement) derive from the model parts was not traced (?).

## 5. Space stage

- Player ship model: `cEmpire::mUFOKey` +0xB8 (known), the live ship `cGameDataUFO+0x780
  mDesiredModelKey` (space.md), cPlayer record holds the same key (§1.2). Type ufo, group
  0x40656200.
- Spawn with a model: `SpawnUFO(type, &politicalId, &modelKey)` 0x102ACC0 cdecl (space.md ✔);
  `CreateUFO` 0x102AC60. Puppets: register the remote package first, then pass the remote key
  instead of the local `mUFOKey`. Changing the local player's ship = the editor flow (not traced, ?);
  setting `mUFOKey` alone does not rebuild the live ship (~).

## 6. Recommended implementation path

1. **Identify own creations** (each machine, before/independent of the host world):
   creature = local avatar `+0xB28` in the player's own game, cell = `S+0x10`, UFO =
   `cEmpire+0xB8`; or let the player pick from My Creations (`GetDatabaseByID(0x11AC199)` →
   GetKeyList, group 0x40626200 / 0x40616200 / 0x40656200, names from pollen_metadata). Keys
   whose record is in Spore_Content/Spore_Game (Maxis) need no transfer - test with
   `RM->FindRecord(&key, &real, 0)` and the database's location (EditorSaves/Pollination ⇒ send).
2. **Extract** the records: `bem` (+`creaturedata`, metadata, png optional) - either parse
   `EditorSaves.package` from disk (share R|W) or `FindRecord` → `db->OpenRecord` (vt+0x34) and
   read the stream. A creature is ~3–10 KB (+ up to 13 KB creaturedata): send in chunks over the
   existing reliable channel together with `{owner, full ResourceKey, content hash}`.
3. **Receive**: remap the instance to a collision-free id, write
   `<profile>/Temp/coop_<peer>.package` (DBPF 3.0, uncompressed), create a DatabasePackedFile with
   that path, Initialize, `Open(1, 6, 0)`, `RegisterDatabase(true, db, 1000)` (one package per peer;
   unregister/re-register when a peer re-sends after an editor).
4. **Use**: creature puppets `GetSpeciesProfile(&remoteKey)` → CreateHerd → `Create(...,
   isAvatar 0)` (npc.cpp path); local avatar `SetSpecies(avatar, ownProfile, age, 1)` after the
   host world is loaded. Cell puppets: forced model key = remote key; local avatar: `S+0x10` +
   RebuildLevel right after the world arrives. Space: `SpawnUFO(6, &pol, &remoteUfoKey)`.
   Tribe/civ citizens: SetSpeciesProfile per owned citizen (as 0xD5BFA0 does).
5. `net::PlayerState.species` (u32 instance) must become a full key per stage.

Runtime checks still needed: baking of a never-seen creation on first spawn (IBakeManager::Get
0x401010, `*(void**)0x15D1C04`, is used by spawn paths; expect a delay/placeholder ?), GetDatabaseByID(0x11AC199) =
EditorSaves, SetSpecies on the live avatar (animations, hit points, UI ability bar refresh),
cell in-place model swap.

## 7. Editor "done" and the part list (✔ disassembly + runtime 2026-10-22)

Why the guest's "done" did nothing for its own creature (cancel worked): the creature comes from
the Creature Creator (My Creations) and holds parts that are not in the creature-stage editor's
part list. The stage editor refuses to finish such a creation.

The chain, from the button to leaving the editor:

| addr | what | conv |
|---|---|---|
| 0x5DF4B0 | `EditorUI::OnExitButton(int command, int)`: command 0x102 = done (0x101/0x103/0x107 other exits) | ✔ thiscall `ret 8` |
| **0x57AAA0** | `cEditor::GetFinishState()`: 3 = unchanged, may leave; 4 = unchanged but an error of `editor+0x48` is still there; 0/1/2 the same after edits (2 = a load-time error is still there) | ✔ thiscall plain `ret` |
| 0x5DEC50 | `EditorUI::RunExitCommand(command)`: switch on command-0x100, 0x102 → `cEditor::Exit(1, 2, 1)` (or a confirmation dialog when `ui+0xEC`) | ✔ thiscall `ret 4` |
| 0x5DCA40 | state 4 (and 2): `0x4F3B80(+0x48, current)` = first error bit in both, `0x4F3C80(bit)` = its message id (table 0x150D920, 8 bytes per bit), shown through 0x67CA80; the editor stays | ✔ |
| 0x58A350 | `cEditor::Exit(int how, int, int)`: 0 = commit (0x5802F0), 1 = accept and leave (0x57E8A0), 2 = cancel (0x57C1E0) | ✔ thiscall `ret 0xC` |
| 0x585D40 | save state machine (`editor+0x38C`: 1 → 2 → 4 thumbnail 256 → 5 thumbnail 128 → `Editor_Save` 0x577650), runs per frame from 0x58BE50 after an edited creature is accepted | ✔ runtime (136 calls, then one Editor_Save) |

Errors are 128-bit sets (`u32[4]`); `0x4F3DA0(a, b)` (cdecl, two sets by value) returns 1 when
they share no bit. `editor+0x1CC` is the live validation object (flags at +0x24..+0x30, masked by
the globals 0x15DB7EC..F8, read by 0x57A960); **`editor+0x48`** = the blocking set computed when
the creation is loaded: `cEditor::GetBlockingErrors` **0x57AC00** (thiscall `(u32 out[4],
cEditorResource*, model, bool) ret 0x10`, returns `out`; called from the load 0x57F6C0 for +0x48
and +0x58 and from 5 other places). It returns an empty set when app property 0x55D7CA1 is true
(a debug switch, ~), else the live errors plus:
- bit 10 (0x400): creature/cell without a part with property 0xB00F0FEC or 0x11B79301 (?);
- bit 18 (0x40000): check 0x4EFB60 against editor config prop 0x7A926123 (?);
- **bit 5 (0x20): a rigblock of the creation is not in the editor's part list**: editor config
  (`editor+0x24`) prop **0xF5CBE065** = list id, list from the global map 0x15DB960 (filled by
  0x4EB2B0 from the editor's palette resources; empty list = everything allowed), check
  **0x4EF8C0** (cdecl `(cEditorResource*, u32 listId, u32* flags)`: walks the rigblocks at
  `res+0x98`, 0x1D8 bytes each); message id 0x74E889BF;
- bit 11 (0x800): `editor+0x4B1 && !+0x4B2`;
- then filtered by 0x4EDF80.

Runtime (lane 3, guest Kirgle 0x290225CD in CreatureEditorExtraLarge 0x465C50BA): `editor+0x48`
= `{0x20, 0, 0, 0}`, GetFinishState = 4, the message for bit 5 is posted and the editor stays.
The host's species (Maxis 0x065CBE70, made in the stage) has an empty blocking set: state 3,
`Exit(1, 2, 1)`. Clearing bit 5 at `editor+0x48` by hand made the guest's done work; an edited
creature is then saved to My Creations under a new key (0x2904B8xx) and the editor returns to
the creature stage.

The mod (coop.cpp `h_BlockingErrors`) drops bit 5 from GetBlockingErrors while the player's own
editor (`open_own_editor`) is open: in the own editor the player's own parts are allowed. Other
blocking errors still stop "done". Not done: native evolution data of the request (`+0x90 =
0x64AA168`, `+0x94` = object from `0xF67D40()+0x10E8`, `+0x70` vector of 4 values from
`+0x10FC..+0x1108`, flags +0x34..+0x3D, see 0xD42F2C) is not filled by the mod's request; done and
cancel work without it.

## 8. Independent Adventure Editor creation sessions (GOG 2024, 2026-11-02)

Static receipts in `tools/addrport/verify_editor_creation_dc04.py` verify the
original dc04 image and its reviewed LAA variant. Native request constructor
`5A9140` owns 9C bytes and primary interface references at vtable04/08. Fields
are editor ID0C, source ResourceKey10, return mode1C and new-asset flag20.
`5A909C..5A90F3` selects default editors for CRT465C50BA, BLDD817CD63,
VCL99F87089, UFO96B24187, FLR1C7ECA95 and CLL3615A30B.

`57A710` is thiscall(editor, result), ret4. Its result key is18 and cancellation
flag44. It posts30C11C7 and the normal E11332 return, then releases editor1CC
request and4BC result. Native cast palette `ED4289..ED42C1` checks result44
before importing. The creation adapter matches its exact owned request and
copies the real key and cancellation flag into an owned immutable completion.
It pins the native result across the original notifier, marks that result
cancelled and leaves it cancelled for its entire queued lifetime. The posted
message holds its own reference. Native return/cleanup remains native and the
shared coordinator receives the original completion for admission. Unrelated
requests and their native result flags are untouched.

Native class import `F462F0` owns constructor/copy/insertion, class allocation and
default act behaviors, but calls `F43990` to assign its primary asset. That
setter also touches species/actors and finishes with live refresh `F42470`.
Calling it on a fake ScenarioData containing only10 is unsafe. Detached import
therefore intercepts F43990 only for the exact scoped owned temporary parent,
assigning its primary key and verified model-type cache18. `F3E740` is
thiscall(parent, classIndex), ret4, and reads only parent10 and the owned map.
Model type `EECAC0` is cdecl(key), plain ret, using installed asset metadata.
Every other F43990 call stays native. The owning import still supplies default
act data and vectors; no scene pointer is fabricated.

Content has a separate ResourceKey property43524101. Behavior fragments clear
only their primary key, machine hint0C and local server hint10 on owned copies;
the native adapter restores current content after copying a behavior subtree.
Content replacement preserves class behavior, positions and every other key.

Project-owned versions retain BEM bytes and remap resource index keys. The
existing BEM self-key and factory findings in section1 apply; an unexpected
complete self key in BEM is rejected. Pollen v13 own key at0C is rewritten in
BE(type, group, instance) order while ancestry stays original. Standard property
streams remap only declared ResourceKey payloads, never numeric words or blind
byte matches. Unrelated dependency keys/bytes remain unchanged. Native derived
species/summary caches, source-format XML and the model-group PNG (which embeds
old metadata in pixels) are omitted; native loading/baking regenerates them.
The plain group+1 thumbnail is retained. Unsupported owner companions and
unknown metadata/property formats fail before canonical admission.

`tests/editor_creation_windows_tests.cpp` exercises production hooks under the
real i686 ABI: intrusive request/result lifetime, delayed ownership and single
replay, ordinary Done/Cancel, stale scopes and leases, all six default request
families, failed allocation, detached import scene suppression, content-only
replacement and checked immutable record transformations. These controlled
fixtures are not evidence of running-game entry, editing, remote reopening,
baking or return success. Each family and simultaneous participants still need
the private native integration checks recorded by the delivery report.

Creation symbols are included in the established
`data/symbols/overrides_editor_dc04aee5.sym`. The deployed SporeKit loader's
override list does not discover a newly named standalone symbol map. Both the
normal native deployment path and the dedicated static verifier must use the
established map; an ignored sidecar leaves creation entry unavailable.

The local native cast toolbar exposes New asset (control C00F0301), Edit asset
(C00F0302), and six family choices C00F0310..C00F0315 in the order above.
`editor_creation_ui` creates and updates this retained native window outside
Present and calls only queued public creation actions. Edit resolves the shown
native class inspector, then the selected placement, while excluding the
captain. Selection does not acquire the asset's content lease. The coordinator
validates the chosen logical class and acquires that separate lease before
native entry. The toolbar hides during native pickers, creation sessions and
testing; its callbacks are fenced by the local prompt identity.

`tools/tests/coop_editor_creations.py` drives the ordinary toolbar and bounded
native mouse/key recipes against two or four already-owned hidden/muted private
lab processes. It checks all six families from host and guest, one admission
and Undo/Redo, all participants designing simultaneously, guest return while
the host stays away, another participant's content replacement concurrent with
behavior editing, cancellation and final live scene convergence. Recipes must
contain measured normal construction/save/select input for the private seeds
and exact client viewport. The fixture rejects arbitrary console/native calls,
unowned windows, candidate hash mismatches and truncated asset diagnostics.
Its self-test and Python compilation pass; no running-game result or measured
recipe is claimed by those checks. The production i686 lifecycle fixture now
passes 308 checks, including same-key Save routing, both logical and companion resource
identity collisions, and the host ABI's zero-success hook registration.

Uploaded creation closures are checked against the host's effective native
ResourceManager before package registration. This uses the collector's existing
read-only dc04 ABI: ResourceManager FindRecord vtable30, database RecordOpen34
with read access1, creation6 and lazy=true, record stream18, RecordClose24 and
Release08, then stream size1C, seek28 and read30. Native streams supply
decompressed bytes. The comparison includes vanilla packages and permits only
an exact key with equal bytes; a different effective key, including another
resource format, is rejected. An existing resource that cannot be read also
rejects admission. Only the exact mutable adventure root is exempt. This check
does not allocate a host remapping or mount the proposed package.

`tests/adventure_collect_windows_tests.cpp` exercises that production collector
under the i686 ABI with controlled ResourceManager, database, record and stream
vtables. Its 101 checks pass for equal bytes, changed bytes, missing resources,
typed aliases, failed open/read/seek, oversized records, exact-root exemption,
duplicate keys, unavailable manager and balanced record close/release. Other
adventure resources do not inherit the mutable root exemption. The
controlled fixture does not establish running-game package collision behavior.

Act roster edits use a separate Authoring lease. It covers authored adventure
fields while excluding Content and the primary content ResourceKey field;
Document remains a complete hold. The portable authoring extension fixture
passes 328 checks, including roster Undo/Redo concurrent with another player's
asset Content lease and preservation of the later immutable content version.
Production runtime UDP and durable-store creation fixtures pass 1522 checks,
including independent guest return and act reordering while the host edits an
existing asset, and safe guest return after host resource collision rejection
without a partial cast entry or history item. The guest receives the host's
specific installed-resource conflict reason. These tests use controlled native scenes rather than game
processes. Native all-family, two-player and four-player acceptance is pending.

Terrain request bounds preserve a complete native stroke or preset as one
gesture. Requests containing only append/set/delete terrain operations and at
most 64 Set companions may contain up to twice the configured terrain-record
bound, enough to remove an old recipe and append its replacement. Ordinary and
mixed structural requests remain bounded to 64 mutations. Pure terrain allocation
is bounded by the record limit; a small mixed gesture gains only its actual
terrain append identities in addition to the existing 64 ordinary identities.
The 8 MiB operation and retained-history limits remain unchanged. The portable
fixture covers a 100-record stroke, checked 102-record to 300-record replacement
with a planet-field companion, per-author Undo/Redo preserving later foreign
appends, canonical replay and wire/checkpoint roundtrips, overflow and 8 MiB
rejection. The selective wire container bounds do not widen placement edits.

The combined i686 runtime quick fixture passes 11696 checks and the complete
fixture passes 20655 checks with current creation and scene-return callbacks.
With a retained 24 MiB dependency closure, 20 structural edits and Save send
123659 host bytes and 51348 guest bytes while retained object storage grows
42798 bytes. Cached rejoin sends 33733 host bytes and 754 guest bytes.
The closure fixture reacquires ownership for each separate placement gesture;
an expired idle lease cannot stand in for a new gesture's ownership.
`SPORECOOP_FIXTURE_TRACE=1` prints and flushes the current case, held-gesture
subcase, and fault PC/frame offsets to aid diagnosis of test process faults.
The focused tail fixture passes 703 checks in ten consecutive runs after the
complete run. An earlier intermittent access violation was observed during
the generation/disconnect fence case; it was not reproduced in these later
runs, and no running-game safety conclusion is inferred from that absence.
Receipts are under `out/editor-preview9-creation/` and the collector receipt is
`out/editor-creation-re/adventure-collector-validation.log`. These controlled
tests do not replace the pending game-process acceptance checks.

## 9. Native creation return failure, candidate 6 (2026-11-02)

The actual hidden/muted private pair 95/96 used DLL SHA-256
`82afd4a885c30b381d0198dc3e9bf4956c1a6cccde20e5e672d37da23403a94a`,
PIDs 12316/52476 and the supported LAA executable. Both participants began at
committed/durable revision 62 with zero native faults. Ordinary host input on
New asset `C00F0301`, then New creature `C00F0310`, entered native mode
`00DBDBA1`. A native mouth drag, the name `Preview9CreationProbeC6` and the
green Done button saved the local creature. The host advanced once to
committed/durable revision 63. Adventure return then failed with
`Waiting for the active native Adventure Editor camera`; the host froze and
displayed a black viewport. The guest froze at revision 62 with an explicit
host-baseline-recovery notice. Both processes still reported zero native
faults. This is a failed workflow, not native creation acceptance.

Receipts are `out/preview9-creation-probe/result.json`, `commands.json` and the
`95-creature-*.png` screenshots. No recovery, Undo, native memory write or
direct native-function invocation was used after that failure. A later
read-only receipt, `camera-native-readonly.json`, confirmed that both games
were in Scenario mode and native Adventure Editor readiness was true. Host
camera manager `+A8` was index 29, ID `A50C91D5`; its preferred vtable
`013F69E8` resolves to `Editors::EditorCamera` methods. The Adventure Editor
orbit camera `00EBB801` was present at index 40 with preferred vtable
`0145CF20`, but was not active. The guest's active camera was that index 40.
The host therefore retained the creation-editor camera despite mode return.

Static cancellation findings remain narrower than a proof about all listeners.
`57A710..57A960` does not inspect result `+44`; it emits result `030C11C7`,
then return message `00E11332` using request `+1C`. Request constructor
`5A9140` and Submit `5A92C0` obtain that mode through the native manager.
Additional Adventure UI result listeners at `EDCD70`, `ED6B97`, `ED4289`
and `EE3660` were inspected: their reviewed cancellation branches control
asset callbacks/imports or follow-up validation, not camera activation.
This does not establish that every result listener is cancellation-independent.

The reviewed new-class import route is `ED4289 -> ED4640/ED4650 -> ED3A30`;
its new branch calls `F462F0 -> F43990 -> F42470`. The last function only
recreates already-placed actors belonging to the target class; a freshly
allocated class has none. `ED37B0` builds/selects the cast card. The subsequent
`07A12A49` notification reaches tutorial-state helpers at `EFC361`.
No `EFDDC0/B5CEF0` activation call was found along that reviewed new-class
route. Existing-class replacement can refresh actors and retains a broader
side-effect path. Re-enabling the uncoordinated importer is therefore not a
verified remedy for the observed new-creation return.

Native browser `64B379..64B387` clears request `+39` for Scenario return.
Tracing its consumer `58FB8D -> 5DCDC0` shows an editor UI visibility control,
not a camera-resume operation. The Simulator request wrapper `B1DE20`
optionally pauses token `04BF38A6` and runs UI transition `0462C656` before
raw Submit; its reviewed `B1DAA0/B1DB10` helpers change UI visibility, while
the direct branch at `B1DED4` submits immediately. Neither is a verified fix
for the observed camera failure.

Native edit-world setup at `F0007A` calls `EFDDC0`, which selects `00EBB801`
through `B5CEF0`, then applies the resource camera bookmark and may focus the
authored captain. `B5CEF0` is `thiscall(SimulatorSystem*, uint32 cameraID)`,
`ret4`; it selects the App camera and refreshes the Simulator camera
controller's cached orbit pointer. A bare App `SetActiveCamera` omits that
second step. These are static activation receipts, not a completed creation
return fix or proof that camera selection alone restores the whole scene.
The proposed scoped boundary must check the exact creation scope/serial,
native generation and settled Scenario return, validate the existing orbit
camera before activation, verify the active object and controller afterward,
then restore the pinned local view/selection before acknowledging return.
Controlled fixtures should cover a missing orbit, changed context/generation,
duplicate service, delayed readiness and both Done and Cancel. Actual native
Done/Cancel and independent return must still establish scene acceptance.

The result-listener lifetime audit found a separate defect in candidate 6.
`57A737` and `57A85B` use MessageManager vtable `+18`, which is MessagePost
`885980`, rather than synchronous MessageSend `8854A0` at `+14`. The verified
vtable is `01427AC0`, installed by `884B17`. MessagePost retains the result at
`8859A5` when the manager's reference-ownership option `+C4` is enabled, then
enqueues it through `8859FA`. Restoring result `+44` immediately after the
original notifier returns lets queued cast listeners observe an uncancelled
result and bypass shared admission. The old synchronous fixture concealed
that defect. This does not establish that the defect caused the observed
black viewport; the toolbar's new request has no native cast callback context.

The corrected hook copies the true completion before mutation and never restores
the native result's cancellation flag. Complete `57A710..57A960` inspection
shows no read of `result+44`, including its later return-message and cleanup
branches. The reviewed Adventure import/callback listeners use that flag to
skip their asset mutation. Generic Simulator listener `B367D3` instead resolves
the result context and sends `3047CB34` without inspecting cancellation; no
camera activation was found in that reviewed branch. These findings preserve
the separate requirement to verify actual native UI and scene return.

The lifecycle fixture now models a retained queued result and a separately
queued mode return. Its 308 checks include listener dispatch after the hook,
waiting for the native mode return, queued delivery after module shutdown,
balanced result/request references, original saved-key/cancellation delivery,
same-key saves and an unrelated request while a shared creation session is
active. The strict i686 build and executable pass; the receipt is
`out/editor-preview9-creation/creation-posted-result.log`. This is controlled
message-lifetime evidence, not a running-game Done/Cancel acceptance result.

The ordinary Scenario resume also owns more than its direct orbit-camera
getter calls. `EF0990` unconditionally calls edit-world setup `EFF670` at
`EF09A3`. Complete `EFF670..F0008D` inspection shows no early return before
`F0007A -> EFDDC0`, so a completed ordinary resume performs paired Adventure
Editor camera activation. The later resume calls `EDA030` and `ED6910` restore
the native UI; `ED6910` sets display `+94` and re-enters the retained display
mode through `ED6620`. For the Build view used by candidate 6, the shared Save
adapter explicitly permits this idempotent Build transition. The reviewed
mod hooks do not intercept Scenario OnEnter `EF24C0`, OnExit `EF1580`, frame
update `EF2EF0` or editor resume `EF0990`. The co-op gameplay update hook is
the separate play-mode function `F1F960`.

OnEnter marks mode `+DC`, pauses reason `04BF38A7`, and resets the native
initialization state `15ACC34` to zero through `EF1BC0`. Scenario frame update
first requires Simulator mode-manager `+28` clear, advances that initialization
through states zero to four, then clears `+DC` at `EF2F3C`, releases that pause
reason at `EF2F4F` and calls `EF0990(false)` at `EF2FB2`. The retained shell
alone can satisfy the adapter's earlier readiness predicate before this native
sequence finishes. Canonical capture/restore must wait for ordinary resume
and visible native UI rather than treating Scenario mode and that shell as
complete return. The missing candidate 6 state values were not captured, so
premature restoration remains an inferred cause, not a proven crash signature.

The Simulator request wrapper does not supply a missing late release for that
OnEnter pause. Its separate transition reason `04BF38A6` is paused at
`B1DEBE`; completion handler `B1DF60` calls raw Submit at `B1DFC8`, then releases
that reason at `B1DFF1` before posted creation entry is dispatched. Direct Submit
never acquires that wrapper reason. Simulator mode-manager `+28` is set by
SetMode `B1E350` and cleared at `B5F199` through the native App post-mode-change
`0212D3E7` route `B637A4 -> B5F180`. The mod's Simulator-message hook delays
only `04715068` while a world save is busy and does not suppress that route.
No direct pause-counter, pending-flag or initialization-state writes are a
verified substitute for completing these native transitions.

## Preview 9 candidate 7 native creation receipts

Candidate 7 (`ab5fba723390806add3a1feb61c383225c6a91dd8fd116e05f4a070cf23369a4`)
completed the first host Creature Save and guest unchanged Creature Cancel in
the owned hidden two-player Adventure Town lab. A normal mouth placement and
unique local name produced exactly one `Create adventure asset` history item,
revision 1 and 118 to 119 native classes on both peers. The new immutable class
1342 used `40626200!7D98D399.2B978C46`; all original primary class keys remained
unchanged. Both strict native checks returned
`56faca4149d09dbbb23a7160c3f8a6ee75d3cc6d522c061d2598691d343f9954`,
and both processes reported zero faults. The guest's subsequent unchanged New
Creature Cancel preserved revision 1 and that single history item. Receipts
are under `out/preview9-creation-probe-c7/`.

The host Save return was also sampled through ordinary deferred Scenario
initialization. Scenario mode and the retained shell appeared first with
initialization zero, pending OnEnter and an invisible world. Canonical restore
did not advance until initialization four, pending clear and world visible.
The first immediate screenshot still showed one black frame; a later settled
screenshot showed the world and native toolbar, with registered Adventure
Editor camera `00EBB801` active. These observations establish the settled
first workflow, not screenshot readiness in the same native return frame.

A separate bounded independence probe began at revision 66 after the native
movement measurements. The host entered New Building and remained there while
the guest entered New Creature, explicitly Cancelled and independently
returned to the Adventure Editor. No draft or history change occurred during
those entries and Cancel. The guest then used ordinary cast-card Locate to
select placement `4294967303:1`, verified its Placement lease before moving,
and completed a 12-pixel native drag. Both canonical controllers durably
accepted revision 67 while the host remained in the Building editor. The host
explicitly Cancelled and returned to revision 67; both strict native checks
returned `8af8bf51e3710141f012a53d6960545651a1f84b993ff9a13b11918e0d812327`.
History gained only the guest's `Edit placement`, no cast entry. Settled
screenshots show the world and toolbar on both participants, independent
selection and zero faults. The receipt is
`out/preview9-creation-independence-c7/independent-edit-return-result.json`.
The public asset diagnostic truncated mid-row in this 119-class adventure;
the initial parser refused it before input. The completed Cancel proof uses
the full canonical document hash, exact revision/history and strict native
projection rather than treating a partial cast listing as complete.

An optional subsequent host New Building probe placed one native body shape
and named it. Its first normal Done click dismissed the open metadata popup;
a second normal Done click with that popup closed also did not produce a
result or return within its 60-second bound. The Building editor remained
active, revision 67 and fault count zero, without an error dialog. Its native
Type Chooser remained visible, and this probe did not select a building
subtype. Native validation, required creation setup and shared gating remain
to be distinguished. The failed scene and both bounded receipts were retained
before the owned runner reached its lifetime bound. This is not Building Save
acceptance or evidence that generic shared-save gating caused the failure.
Vehicle and the other creation-family workflows, changed Cancel, remote Edit,
four-player creation and complete native Save/reopen remain unverified.

The supported-image Done dispatch is command `102 -> 5DFDF6 -> 5DFB80`.
Before invoking `5DF4B0`, it reads EditorUI `+5C` (Editor), Editor `+7C`
(resource), resource `+C` (another owner), then byte `+44`. If that byte is
nonzero and window `03F67620` is visible, it returns without attempting Save.
The tested flag is `Window::GetFlags`, vtable `+28`, bit one: visibility, not
checkbox selection. The resource-owner byte's higher-level meaning is not
established. Otherwise native `GetFinishState=57AAA0` can reject through its
blocking-error UI without producing NotifyResult. In this supported image,
`57A960` masks request `+24..30` by the complement of globals
`15DA7EC..15DA7F8`; `57A9E0` similarly masks `15DAA40..15DAA4C` and excludes
the current error tuple. These globals differ from older image notes above.
No native error tuple or early-gate window was sampled before candidate 7
ended, so those branches remain alternatives rather than the observed cause.

Subtype choice is also explicit native state. The chooser is populated from
the Editor's allowed-type vector `+334/+338` and initialized to list index
zero. Selection event `9A1552D3` for `503517B0` retrieves the selected type at
`5E0085..5E00B5`, then stores it in Editor `+98` model `+58` at `5E00C0`.
`GetNameForAssetType=4BBA50` and its UTF-16 literals identify
`99E92F05` as BuildingCityHall, `4E3F7777` as BuildingHouse and `BDD15F3D` as
BuildingFarm. Land-vehicle choices include `7D433FAD` MilitaryLand,
`F670AA43` EconomicLand, `9AD7D4AA` CulturalLand and `BC1041E6` ColonyLand.
The generic ground-vehicle editor's actual allowed list and initial model
type must be observed; selecting list index zero during initialization alone
does not prove whether the model received that type.

## Native title refresh and captain transport

The guest's `Name Your Adventure` caption was already visible before its first
creation entry. Both owned initial checkpoints carry the title `Adventure
Town`, the same description, empty tags and genre `3027271567`. A later
bounded ReadProcessMemory-only sample of the exact two owned candidate 7 PIDs
confirmed all three live ScenarioData strings and genre also match. The
sample read 318 bytes per process in 2.2 ms, checked stable mode/data/resource
pointers, recorded source identities, and made no game calls or writes. Its receipt is
`out/preview9-creation-probe-c7/live-metadata-readonly.json`. The visible
caption is stale UI; metadata was not lost in transport or overwritten in
the sampled native ScenarioData. No live helper pointer was guessed.

The exact refresh route is distinct from raw metadata assignment. `F3CC00`
operates on the `INameableEntity` subobject at ScenarioData `+4`; its equality
branch returns before notification when the title already matches. After
assigning the title it sends synchronous `7AA519DC`. Its 20-byte stack packet
starts at the caller's `ESP+0C`: owner at `+0`, UTF-16 text pointer at `+8` and
an explicit zero DWORD at `+10`. The reviewed receivers do not use `+4` or
`+C`. Generic Editor handler `592F12` checks the owner against its bound
nameable before using the text. The native name helper listener `5BFC33`
likewise checks the exact bound owner, then calls `5BFA50`, which updates
display `D0E6D04B` and editable caption `C7CEB1BD`. Scenario helper setup
`EDB3C0 -> EDB3EE` binds the current ScenarioData `+4` through `5BFE40`.
`ED5550` rebuilds crew thumbnails and does not refresh this title.

The existing named `App::IMessageManager::Get` is `67DC80`,
`cdecl()->void*`, reading the supported image's global `15FD890`. Its vtable
`01427AC0`, installed at `884B17`, maps `+14` to `MessageSend=8854A0`:
`thiscall(manager,id,payload,singleRecipient)`, `ret0C`. Dispatch invokes
recipients inline at `8855A0` or `88568A`, drains its temporary recipient
references and returns at `8856F9`. It does not enqueue the stack packet.
This proves the lifetime of an owned zero-initialized 20-byte notification
and null-terminated owned title during that call. A scoped caption adapter
must preserve dirty flags and verify its exact SceneOwner before and after
dispatch; calling SetName with already assigned text would simply early-out.

Snapshot captain sidecars have a separate transport issue: resource envelopes
and checkpoints contain the typed Captain settings rather than duplicate
captain fields in their native identity mapping. `native_snapshot` and
`load_snapshot` now hydrate the asset and locked flag from that authoritative
row. The portable helper accepts an intentionally empty nullable captain,
rejects malformed advertised identities atomically and defers legacy rows to
verified native expansion. It does not derive metadata from Project settings,
which contain no title/description/tags/genre fields. The strict i686 project
fixture passes 5,683 checks, including full/delta/cached-join transfer,
checkpoint hydration, nullable captain, rejection and legacy preservation;
receipt `out/editor-preview9-captain/project-hydration.log`.
