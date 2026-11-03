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
replay, ordinary Done/Cancel, stale scopes and leases, the four supported
Adventure creation families, failed allocation, detached import scene suppression, content-only
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
(C00F0302), and four family choices C00F0310..C00F0313: creature, building,
vehicle and spaceship. The existence of native plant and cell editors does not
make those resource types Adventure cast-compatible; see the factory audit below.
`editor_creation_ui` creates and updates this retained native window outside
Present and calls only queued public creation actions. Edit resolves the shown
native class inspector, then the selected placement, while excluding the
captain. Selection does not acquire the asset's content lease. The coordinator
validates the chosen logical class and acquires that separate lease before
native entry. The toolbar hides during native pickers, creation sessions and
testing; its callbacks are fenced by the local prompt identity.

`tools/tests/coop_editor_creations.py` drives the ordinary toolbar and bounded
native mouse/key recipes against two or four already-owned hidden/muted private
lab processes. It checks all four compatible families from host and guest, one admission
and Undo/Redo, all participants designing simultaneously, guest return while
the host stays away, another participant's content replacement concurrent with
behavior editing, cancellation and final live scene convergence. Recipes must
contain measured normal construction/save/select input for the private seeds
and exact client viewport. The fixture rejects arbitrary console/native calls,
unowned windows, candidate hash mismatches and truncated asset diagnostics.
Its self-test and Python compilation pass; no running-game result or measured
recipe is claimed by those checks. The production i686 lifecycle fixture now
passes 479 checks, including same-key Save routing, both logical and companion resource
identity collisions, native existing-asset subtype routing, unsupported-family
rejection and the host ABI's zero-success hook registration. The earlier
308-check result predates the Adventure factory audit.

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

## Adventure-compatible creation families and native Edit routing

Native default-editor selection `5A909C..5A90F3` recognizes six resource
families, but that switch does not establish Adventure actor compatibility.
`EECAC0` reads the installed model subtype from resource metadata
`0670DA17` or `03C609F8`; it does not test whether ScenarioData can construct
that resource as an actor. Likewise `ImportClass F462F0` and its primary-asset
setter `F43990` allocate class data without providing that guarantee.

The decisive factory is `cScenarioData::CreateObjectInstance EEDD40`. It first
normalizes the key through `EEC980`, which transforms only a native prop of
type `00B1B104`; direct creation keys pass unchanged. The factory has explicit
branches for CRT `2B978C46` (`EEE0B9`), BLD `2399BE55` (`EEE074`), VCL
`24682294` (`EEDFAE`) and UFO `476A98C7` (`EEE230`). Direct FLR `438F6347`
and CLL `3D97A8E4` reach the default `EEE1AC`, which constructs object type
`018C88E4` and loads fixed model `0750F022`, group `FFFFFFFF`, instead of the
supplied creation. `GetObjectType EECD40` also classifies those direct types
as its default `867A9EE9`, but classification alone was not used as the
compatibility proof. In particular, pseudo-type `F0000001` is a powerup and
has its own valid factory branch; it must not be confused with creature type.

The shared creation toolbar and owned admission helpers therefore accept the
four compatible creation families. They reject direct plant/cell entry,
completion, detached class creation and immutable version collection before
changing the pending session or output. Reading existing documents and the
normal native prop-normalization path are unchanged. Supporting plants or
cells as actual Adventure assets would require a separately verified
conversion or factory implementation; native editor availability is not such
a conversion.

Existing Edit must retain the request constructor's `+0C = FFFFFFFF` sentinel
(`5A915D`). Native `EditorRequest::Submit 5A92C0` checks that sentinel at
`5A930F`, passes the exact source key in EAX to internal resolver `5A9040`,
and stores its result at `5A931D`. The resolver loads the source through the
ResourceManager, queries model interface `03C609F8`, reads subtype `+18` and
calls named `GetEditorForAssetType 4333E0`. Only when that metadata is
unavailable does it fall back to the source family's default editor.
No adapter call to the internal EAX-register function is needed.

The verified subtype map includes military Water `8F963DCB` and Air
`441CD3E6`, economic Water `2A5147A9` and Air `1A4E0708`, cultural Water
`1F2A25B6` and Air `449C040F`, and colony Water `C15695DA` and Air
`2090A11B`; each maps to its corresponding specialized editor. Forcing every
VCL into the default ground editor `99F87089` bypasses this native choice.
The explicit New routes still use the four native family defaults because
new content has no saved subtype. Intercepted picker requests keep the exact
editor ID selected by the native picker.

The i686 lifecycle fixture passes 479 checks. It delays ownership before
submitting each existing water/air variant, asserts the unresolved sentinel
and source key reach the original Submit trampoline, exercises the native
resolver's controlled result and completes a logical same-family Save. It
also checks specialized picker IDs remain unchanged, direct plant/cell entry
does not allocate or create a session, and rejected import/collection/remap
does not alter the existing output. These controlled objects exercise the
production adapter contract; they do not prove actual native air/water Save
or remote Edit. Receipt: `out/editor-preview9-creation-routing/lifecycle.log`.
The dedicated static verifier now checks the constructor sentinel, native
Submit/resolver callers, prop-only normalization and factory fallback bytes;
`out/editor-preview9-creation-routing/abi.json` records six named creation
symbols, eight native callers and the exact reviewed image hash. The UI passes
strict i686 syntax checking and the four-family integration tool self-test
passes. Actual Building, Vehicle and Spaceship workflows remain pending.

Candidate 9 exposed a separate toolbar rendering bug: the account-layout
template retained unnamed decorative children, including a Spore logo, and
the toolbar remained in front of the native Crew Sporepedia picker. The
retained owning tree in `out/preview9-c9-crew-add-open/tree.txt` shows the
creation toolbar directly under MainWindow and the visible picker
`53C6DC80` under native layer `05B598F9`. Relying on cached runtime editing
state or the first global ID match does not identify that visible instance.

The creation UI now hides every original direct child subtree of its retained
layout before creating its own controls, keeping the hidden style sources
alive. Its outside-frame update searches the current visible owning window
tree for the picker, skipping hidden ancestors and non-owning child links.
The traversal bounds total nodes, depth and stack size and hides the toolbar
when those bounds are exceeded. Queued click handlers repeat the live picker
check, so a picker that opens before the next update cannot accept a stale
toolbar action. No game calls, I/O or native state changes occur inside
Present.

`tests/editor_creation_ui_windows_tests.cpp` runs the production UI against
controlled native window objects and passes 64 strict i686 checks. It covers
unknown decorative template children, a hidden cached picker preceding a
visible duplicate, ancestor visibility, stale child ownership, queued clicks,
four family choices and bounded/context cleanup. Receipt:
`out/editor-preview9-creation-routing/ui-c11.log`. This proves the adapter's
window behavior; actual candidate 11 screenshot acceptance remains required.

Candidate 10 removed the decorative logo, but its creation toolbar was absent
before any creation input. A bounded ReadProcessMemory-only replay of the
verified native Flags, ControlID and Parent getters identified the first guard
branch: visible root `53C6DC80`, depth 2, after 381 visible nodes. Neither the
depth nor total-node safeguard caused the rejection. The closed picker retains
this visible root and empty scaffold, while its header `53C5B392`, card frame
`53C5B391` and sidebar `53C5B390` have flags `12` (hidden). In the candidate 9
open Crew picker, these same owning frames have flags `13` (visible).

The guard now requires one of these visible frames on a visible owning path
inside a `53C6DC80` subtree. A persistent empty root, hidden cached content or
an unrelated frame outside the picker does not hide the toolbar. The fixture
covers these states and all three observed active frames, including a visible
duplicate picker after a hidden cached instance. Receipts:
`out/preview9-creation-probe-c10/toolbar-guard-first-branch.json`,
`toolbar-picker-closed-tree.txt` in the same directory, and
`out/preview9-c9-crew-add-open/tree.txt`. No creation entry or Save was attempted
in candidate 10. All four processes returned normally through co-op Leave,
native Cancel and Quit/discard to Galaxy with zero faults; the private runner
remained under its owner's control. This is safe failure evidence, not native
acceptance of the corrected toolbar or four-family workflows.

Candidate 11 subsequently accepted the corrected toolbar on all four players.
Ordinary New entered four independent editors with actual native model types
BuildingCityHall, AnimalCreature, VehicleMilitaryLand and UFO. Each Cancel
returned only that participant. A guest Crew capacity edit committed while the
host stayed in Building, and the host's subsequent Building Save retained that
newer revision. Native Building Done explicitly rejected fewer than three
accepted parts with `You need at least 3 parts to Save`; a valid three-part
BuildingHouse then saved and returned, adding one cast class and one history
item. This supplies a concrete native validation explanation for the earlier
one-part candidate 7 probe, whose own masks were not captured. No native
validation was bypassed or changed. Full evidence and remaining limits are in
the candidate 11 section of `docs/reports/EDITOR-PREVIEW9-VALIDATION.md`.

The native resource's external source key is not part of its serialized body.
Constructor `F2E670` zeros `+8/+C/+10` at `F2E685..F2E68B`. Read `F2FC40`
and Write `F25E00` serialize metadata from `+18`, and their core paths
`F2F6D0`/`F25700` use reflection descriptor `15AF500`. Initializer
`138C850..138D54B` fills 34 entries through `15AFCBC`; the listed resource
field offsets start at `50` and do not include the external key. The receipt
is `out/editor-creation-re/scenario-descriptor-138C850.txt`. Candidate 11's
returned native captures independently confirm the distinction: all four have
identical 60,739-byte bodies with SHA-256
`f9aa43060c7d8034eb195a8e39e377c84067c3048bd4be7c20d6febc17d2d739`,
while the host source is `408A0000!BF0CE0D0.366A930D` and the guest sources
are `408A0000!40C7A6C1.366A930D`. An owned parse must retain verified source
provenance separately; a zero parsed header is not evidence of a source change.

Candidate 12's immutable Crew alias had no usable thumbnail. Native Crew
`ED568C..ED56B6` requests PNG type `2F7D0004` in the selected model's own
group. The retained alias records contain metadata and the editable BEM but
no PNG, while the original Spice Crawler has a 26,339-byte model-group PNG
and no `group + 1` image. Both host and guest therefore show the same blank
card. Restoring that original PNG unchanged would retain its old importable
creation payload. Receipts are
`out/preview9-c12-crew-thumbnail-offline/aliased-records.json` and
`source-header-analysis.json` in the same directory.

Immutable versioning now sanitizes every source-owned model-group or
`group + 1` PNG before allocating its identity, retaining the model-group
presentation key and synthesizing it from a sanitized `+1` image only when
necessary. Unrelated dependency images and authoritative BEM bytes remain
unchanged. The bounded decoder accepts native 8-bit RGB/RGBA PNGs, including
Adam7 and RGB transparency. It validates chunk bounds and CRCs, removes
ancillary and trailing data, then uses WIC solely to decode pixels. The
fixed encoder clears each channel's three native carrier bits and emits only
RGBA8 IHDR, filter-zero rows, zlib STORED blocks and IEND, with deterministic
Adler32 and PNG CRCs. Both audited native carrier-header probes must reject
the sanitized pixels before output is accepted; their precise native
algorithm and source-image positive oracle are recorded in
`docs/re/adventure-editor-history.md`.

Independent testing found that WIC reports success for truncated Deflate,
invalid zlib headers, bad Adler checksums and extra compressed-stream data;
direct frame decoding also accepts these inputs. Consequently the codec first
validates RFC 1950/1951 structure, Huffman trees, backreferences, advertised
window, exact output length, stream end and Adler checksum, plus PNG pass
lengths and filter bytes. Decode or validation failure leaves the existing
alias/output unchanged. The WIC counterexample receipt is
`out/preview9-c12-crew-thumbnail-offline/wic-deflate-diagnostic.txt`.

The creation/versioning i686 fixture passes 502 checks. Independent carrier
and Deflate fixtures pass 32 and 346 checks respectively on Linux and i686.
The actual i686 WIC fixture passes 2,234,201 checks, including stored, fixed
and dynamic streams, genuine Adam7, malformed inputs, deterministic output,
atomic failure and the real compressed Spice Crawler PNG. Its 65,737-byte
sanitized image matches every independently decoded source RGBA channel
masked with `F8`; both native embedded-header variants reject. Receipt:
`out/preview9-c12-crew-thumbnail-offline/independent-codec-receipt.json`.
These checks establish codec and immutable-record behavior. A native Crew
card screenshot using the next candidate remains required for presentation
acceptance.

### C14 Vehicle Save, return provenance and native model admission

The private two-player C14 Vehicle probe created a Military Land vehicle using
normal UI at 1024 by 768: body `(58,114)->(650,420)`, cockpit tab `(87,42)`
then part `(60,114)->(650,355)`, locomotion tab `(123,42)` then mirrored wheels
`(140,302)->(570,408)`, weapon tab `(236,42)` then part
`(60,114)->(665,300)`. These were slow drags. Clicking `(500,741)` opened the
name popup; the probe entered `Preview9VehicleProbeC14` and confirmed at
`(850,600)`. Native subtype, changed-model and finish-state checks passed
before Done. The save reached canonical and durable revision 70 from 69.
This establishes a native Save, not complete creation-return acceptance.

The guest then failed before its first canonical restore: captures advanced
383 to 384 while restores stayed 41, and retry captures 385/386 still left
restores at 41. `materialize_creation_return` first captures the returned
entry scene and checks it against its owned entry snapshot. The native scene
transition increments the generation and retires active terrain
materialization provenance. Offline comparison found the same 27 effect and
92 model derived transforms as before creation; only native position round
trips and Transform revision counters differed from the authored recipe.
The retained canonical stores on both peers recover exactly to revision 70.
Evidence is under `out/preview9-c14-vehicle-{save,store,failure-rpm}`.

The return fix carries an immutable, bounded terrain proof owned by the exact
creation session. Reattachment requires the original mode/data/resource,
source and Terraform tool, complete copied scope and serial, exact entry
snapshot identity, settled native resume and unchanged raw recipe. It is
one-use; runtime records successful transfer before any later capture or
camera retry. A newly entered adventure does not reuse it. Entry validation,
latest canonical restoration and final validation remain required. This is
controlled fixture coverage until another native Vehicle return is tested.

A separate host failure affected the new class 1342. Its canonical primary
was `40646200!7D3EB486.24682294`, while read-only native memory contained
`40646200!067256EA.24682294`, with local-server hint `FFFFFFFFFFFFFFFF`.
The 739-byte behavior block was exactly equal, and its references were empty.
The latter key is a native substitute, not the saved source. Closed-profile
inventory identifies the actual saved source as `40646200!2912711D`:
its 1,379-byte BEM is byte-identical to the alias BEM with SHA-256
`6fa638fda8e62c1f6b89aa7cf16922e096a27bb6fa1152df686bffd5b346d7ee`.
Neither the source nor alias has a compiled VCL record; both have BEM,
pollen metadata and two PNGs. Removing a compiled VCL is therefore not a
demonstrated closure loss in this case. The inventory and bounded class
memory receipt are `out/preview9-c14-vehicle-store/vehicle-closure-inventory.json`
and `out/preview9-c14-vehicle-class-rpm/result.json`.

Static restoration explains how the substitution can occur, without proving
which branch failed in the live case. `F46400` copies the resource then calls
`F43DC0` at `F46430`. That helper checks each primary through `F3C1D0`.
The check uses App ResourceManager cached GetResource slot `0C`, queries
editor-model interface `03C609F8`, and applies the native 128-bit Adventure
mask stored at `15DAB18..24`. A missing load or nonzero validation result can
trigger `F43A90 -> F430A0 -> F43990` and substitute another asset.
`EECAC0` subtype success alone is insufficient because it can read subtype
from metadata interface `0670DA17` before trying the editable model.

The bounded model-admission adapter reports cached and private loads,
interface availability, subtype, the exact mask and all four result words.
Its verified validation wrapper `4BB170` is
`thiscall(cEditorResource*, uint32 output[4], uint32 mask[4] by value, bool)`;
it returns the output pointer and uses `ret 18`. Native `F3C288` passes true.
The adapter retains and releases each acquired ResourceObject once, fences
scope/generation/manager/mask after callbacks, and never publishes pointers.
Private-load success does not replace a failed cached path. Admission before
class allocation rejects cached load failure, missing editable model or
nonzero native errors. The original and alias still require native diagnostic
comparison to distinguish loading from model validation; no byte remap,
post-restore primary rewrite or validation bypass is justified yet.
These loads can populate the native resource cache or warm dependencies.
The diagnostic runs the cached path first and the private path second; a
later repeated probe is not independent cold-load evidence. It does not
allocate a cast entry or mutate the authored adventure.

## dc04 immutable Vehicle identity audit and diagnostic limits

This audit uses the supported GOG 2024 executable, with only its LAA bit changed
in the installed image (SHA-256 `47f60c4d...9199ab`). The older 2016 section's
negative raw-byte self-key search is historical evidence, not a complete BEM
reference-format proof. Do not infer arbitrary creation remapping support from
that search or from the family-loop fixture's synthetic 12-byte model.

The retained C14 alias `40646200!7D3EB486.24682294` was extracted from the
guest's owned recovery dependency bundle without opening a game or changing a
profile. Its BEM is native version 5, with six `0x1D8`-byte part rows and the
header word at `0C` zero. Replaying the actual bounded decoder (`4BFF30 -> 4BF7E0`)
consumes all 1,379 bytes and produces a `0x80`-byte header plus 2,832 part bytes.
Each compressed block has a little-endian length bounded by `0x800`; `FE,n`
repeats the previous byte, and `FE,0` emits literal `FE`. The expanded 2,960
bytes have SHA-256 `484545498c27b673dd5653e2a08d3e90d15bf3aa793fe4d3de9f975b76ff5b02`
and native subtype `7D433FAD`. Neither raw nor expanded data contains the
original, alias or substitute instance, source group, or VCL type in either
byte order. This excludes those literal identities in this particular model.
The alias's version-13 pollen own key is correctly rewritten at `0C` as
big-endian `{type,group,instance}`. Evidence and the offline decoder are in
`out/preview9-c14-vehicle-store/immutable-alias-audit.json` and `alias-audit.py`.

Factory disassembly independently explains external self identity. The
supported-type table is `4BC750`, compatibility check `4BC7D0`, and BEM dispatch
`4BCA80 -> 4BCC10 -> 4BFF30`. After decoding, `4BC9E1..4BCA1F` obtains the
record's key, replaces its type with the requested model family and assigns
ResourceObject `+08..13`. BEM Read fills model content starting at `+18`, not
that external key. Supported native BEM versions are 3 through 5, with at most
1,024 parts and another header word bounded by 128. The ordinary writer sets
that word to zero; the reader consumes no secondary stream for it. Exact bytes/call targets are retained in
`out/preview9-c14-vehicle-store/factory-native-receipts.json`. This verifies the
external identity path, not every field in every part or private companion.

The current immutable versioner deliberately leaves BEM bytes unchanged,
rewrites audited pollen/typed-property owner references, sanitizes owner PNGs,
and rejects unknown owned companion record types and unsupported property
encodings. It removes known generated model/species cache records. Other
dependency keys stay unchanged. It does not yet decode all BEM part fields to
prove the closure or rewrite arbitrary private rigblock/texture companions.
A negative compressed-byte scan cannot establish that broader property, and
native Adventure model admission establishes load/interface/mask eligibility
rather than complete format-reference coverage. Actual Creature and Building
saves are case-specific evidence; the failed C14 Vehicle and unverified UFO
Save do not justify an all-family completion claim. Unknown companion formats
remain a strict rejection boundary; no broad byte-scanning rewrite is approved.

`validate_model` now logs each already-observed admission, including successful
ones, with PID, monotonic process-local observation number, requested key,
cached/private outcomes, subtype, exact mask/error words, scope and native
generation. A matching local creation also contributes its serial; a canonical
host operation is labeled `canonical` with serial zero. `observed=0` means
inspection did not complete, so its default outcome fields are not evidence of
a native load. This log adds no ResourceManager lookup. In C17, successful
calls could log an earlier caller error; C18 clears that log field on accepted
observations. `accepted`, `observed` and the native outcome words determine the
result. The passive creations status also reports
`last_saved=GROUP!INSTANCE.TYPE`, from retained module
state; it can refer to an earlier creation and does not probe the model.
Strict actual-i686 creation tests pass 935 checks, including exact load/release
counts and preservation of the first logged result after later observations.
The next live Vehicle attempt must retain that first automatic log before any
manual `coop.editor model` diagnostic, because both paths can warm the cache.

### C17 rejection-reason loss and C18 correction

The actual simultaneous Terraform attempt on both C17 peers rejected before
Session submission. `decoded=0 size=0/0` described the optional decoded
DBPF diagnostic, not the native snapshots: both snapshots had serialized and
the strict normalized comparison found unequal nonempty bytes. A retained
Town body is 1,237,014 bytes, exceeding that diagnostic's 1 MiB cap. This log
does not establish an empty transfer or an ownership failure.

Runtime then lost the actual unsupported-change reason: `reject_captured`
saved it, but its fresh identity proof reused the same error string, and real
`capture_scene_owner` clears that string on success. C18 uses a separate
identity error and restores the original reason before ordinary rollback.
Failed identity proof still leaves the replacement scene intact and includes
both errors when available. The Runtime double now clears the error on
successful owner capture, matching the real adapter. Tests retain all prior
identity-expiry cases and add explicit owner-query failures for both roles.
Creation admission logging separately passes 936 strict actual-i686 checks,
including a successful result following a rejected result with a stale caller
error. Subsequent strict native checks reported divergence on both peers even
though readiness was true and the Runtime was not frozen: marker 253 retained
a different quaternion after rollback. Native Restore validates its owned
input before scene reconstruction; its successful return alone does not prove
the later live scene. These fixes improve evidence and preserve the original
reason; the unsupported Terraform difference and settled rollback verification
still need actual native diagnosis. No quaternion tolerance was widened.

### C19 checked rejection restoration

A rejected edit now detaches only its local scene while canonical validation,
history and durability continue. Scene readiness and a successful native
Restore are prerequisites. The Runtime restores that player's act, selection
and view, waits a further game frame, then captures and strictly compares the
settled live scene with the latest committed document and native image. Local
input, Save, Test, history controls and scene-ready acknowledgements stay
blocked until that comparison succeeds. A failed comparison freezes the
recoverable draft; it never recursively starts another restoration.

The original scene owner, source, native generation, project, epoch, participant
generation and transport are copied before the failed operation's callbacks.
Rejection cannot authorize a replacement owner observed afterwards. The
initial and coalesced restores use the explicit fenced native API, as does the
local view restoration. Read-only projection identity stays valid during this
hold; authorizing gesture identity is empty. This is not the deferred persistent
SceneAnchor design and does not identify replacements that predate the entire
integrity operation.

The controlled actual-i686 Runtime fixture uses two real UDP controllers.
Both roles accept a foreign placement edit and then a structural addition
while their native scene is held unready; canonical state, history and both
durable checkpoints advance, followed by one materialization of the latest
revision. It also covers a later-frame proof, independent acts and views,
selection/view callbacks that alter authored pose or opaque bytes, failed
captures/restores, and identity expiry during readiness, begin, restore and
recovery callbacks. These tests exercise orchestration with native doubles;
the actual C19 two-player terrain/restoration acceptance remains pending.

The C20 recovery follow-up accepts the failed hold's exact original owner in
either Ready or published Frozen phase. It retains every project, epoch,
participant, transport, source and native-generation fence; mutation controls
remain blocked. A real UDP regression starts from the published failure,
keeps both previous checkpoints while the new canonical recovery is unready,
then requires strict publication and authenticated guest rejoin. The strict
actual-i686 runs pass 7,465 focused restoration checks, 1,508 captured-identity
checks and 40,689 full Runtime checks, with 71 pinned inputs unchanged in
`out/preview9-c20-runtime-receipt.json`.

The subsequent C20 owned-placement diagnostic logs at most eight changed
placements and eight fields per placement at the existing ownership rejection.
It prints exact float/identifier words and bounded windows beginning at the
first changed text/structured byte or reference. Marker numbers are explicitly
the snapshots' serialized bindings. `terrain_delta` means the owned documents
differ in recipe or Planet settings; it is not a native gesture-origin claim.
The logger reads no native state, preserves the original rejection, and cannot
turn a difference into ownership or normalization permission. Strict actual-i686
coverage pins known quaternion words, bounds, immutable inputs, zero native
reads/writes and logger-exception containment. The current full Runtime suite
passes 40,926 checks, with 7,540 focused and 1,508 captured-identity checks;
72 inputs remain unchanged in
`out/preview9-c20-placement-runtime-receipt.json`.

## Ordinary BEM writer and retained C14 dependency audit

The supported executable's BEM writer `4C0060` writes version 5, the part count,
and zero at serialized header `0C` (`4C00B4`). It writes exactly `0x80` property
bytes from resource `+18`, then `partCount * 0x1D8` part bytes from its `+98`
vector through the bounded RLE codec. Reader `4BFF30` consumes those two streams;
its bound on header `0C` does not imply a secondary-row payload. External model
identity remains the separately assigned ResourceObject key. Creature, Building,
Vehicle (including native land/air/water subtypes) and UFO share this BEM path;
there is no separate per-family internal-reference remapper.

`EditorModel::Load 4AE8D0` reads the part vector with stride `1D8`, constructs
rigblocks, and copies each part's first two words into rigblock group/instance
fields (`4AF2FB`, `4AF30D`). Native part-list validation `4EF8A0` likewise
compares these two words. This corroborates the SDK part layout; a complete
semantic audit of every property, paint and private-reference consumer is still
pending. The ordinary BEM is parametric model input, not an included baked mesh.

For the real C14 MilitaryLand Save, the six decoded group/instance pairs are
`40646000!AB10C9B6`, `40646000!B900B150`, `40646000!32ACB870`,
`40646000!DD99C1E6`, `40646000!4C84B9C6`, and `40646000!FE3E47E8`.
All six have type `5CAA5E28` records in stock `Spore_Game.package`. The only
active paint IDs, `F5967F30` and `010B5B15`, have stock `406A0200` property
records there. This was a bounded read-only header/index lookup with stable
double reads, not a native ResourceManager resolution probe. Higher-priority
overrides were not evaluated. Source and alias BEM bytes remain identical, and
neither original nor alias contains a compiled VCL. This artifact therefore
does not demonstrate a missing private dependency or justify blaming removal
of a compiled VCL for the earlier failure.

The fixed owner-companion Collector and typed-property remapper do not alone
prove arbitrary dependency closure. No ordinary native-produced saved artifact
with an unadmitted private dependency or unsafe same-key overwrite was found in
this audit. Existing installed-record conflict checks remain applicable. Real
missing-family Save/load/edit/cold-reopen runs, especially air/water Vehicles and
UFOs, plus their copied decoded reference inventories and first automatic native
model-admission observations, are still required. This adds no compatibility or
rejection policy. Receipts and exact supported-image disassembly are under
`out/preview9-creation-closure-audit/`.

## GA New creation variants and the C27 Vehicle rejection

The ordinary GA AssetBrowser New path does not use a generic family editor.
`64AC40` calls `645C30`, which loads the selected category in `.prop` group
`CC489C6F` and reads typed Key property `968AE1B0`. At `64AF5F`, the resulting
instance is stored in the empty-source EditorRequest `+0C`. Existing Edit uses
the separate category property `968AE1B1`; our existing-source constructor's
`FFFFFFFF` sentinel still lets native `5A930F..5A931D` resolve the asset subtype.

These are verified installed GA category New IDs, not inferred model masks:

| Family / variant | Editor ID | Example category instance |
| --- | --- | --- |
| Creature | `465C50BA` | `D49DE19D` |
| Building: city hall | `99E92F05` | `484FF0B8` |
| Building: house | `4E3F7777` | `DDC202B3` |
| Building: factory | `8707BE7D` | `51FBB249` |
| Building: entertainment | `72C49181` | `36F60725` |
| Vehicle: military land | `7D433FAD` | `DF036863` |
| Vehicle: military water | `8F963DCB` | `DA41D879` |
| Vehicle: military air | `441CD3E6` | `FA167DA8` |
| Vehicle: economic land | `F670AA43` | `13D90629` |
| Vehicle: economic water | `2A5147A9` | `6605EDB3` |
| Vehicle: economic air | `1A4E0708` | `85E38D82` |
| Vehicle: cultural land | `9AD7D4AA` | `0222DED4` |
| Vehicle: cultural water | `1F2A25B6` | `A5A3D5A4` |
| Vehicle: cultural air | `449C040F` | `AD76C2CD` |
| Spaceship | `96B24187` | `476A98C7` |

Factory's editor ID is `8707BE7D`; its model subtype `47C10953` maps to it through
`4333E0`. The nine Vehicle editor configs select part lists `39,3A,38,36,37,35,
33,34,32` in the table's order. Separate colony Vehicle category IDs also exist;
this table and the current toolbar expose the nine listed variants. Existing
asset Edit continues to use native metadata selection rather than this New menu.

C27's first unwarmed automatic Vehicle admission rejected subtype MilitaryLand
`7D433FAD` with error word `00000020` in both cached and private loads. The custom
New request had selected generic editor `99F87089`, whose config points to list
`3B`; no `406B7B3B` records exist in the audited stock packages. `4EF8A0` accepts
an empty list. Native chooser event `5E0040` writes the chosen model type at
`5E0085..5E00C0` without rebuilding the palette on that event path. Thus choosing
MilitaryLand inside this generic editor does not establish its GA part filter.

The retained original `40646200!2912CE77.24682294` and immutable alias
`40646200!7DD0DFB3.24682294` have identical 1,379-byte BEMs, SHA256
`6fa638fda8e62c1f6b89aa7cf16922e096a27bb6fa1152df686bffd5b346d7ee`.
Of their six decoded parts, the body, cockpit and wheel pair are in stock list
`39`. The paired guns `40646000!4C84B9C6` / `40646000!FE3E47E8` are absent there
and present in MilitaryAir list `38`. List `39` and its parent were fully decoded;
they contain no nested `0769EEEE` list that supplies these guns. This is concrete
incompatible-part evidence, not a missing compiled VCL or alias-BEM mutation.

Validation `4F4532..4F460B` maps the subtype through `4333E0`, loads its editor
config in group `40600100`, and passes `F5CBE065` to `4EF8A0`. Bit 5 also covers a
missing list property, so that error alone is not a universal offending-part
diagnostic. The list builder `4EB290` consumes `053878D0` keys and recursively
merges `0769EEEE` lists. The reviewed actual failure is corroborated by the exact
decoded gun pair and list membership; validation masks remain unchanged.

New Building and Vehicle toolbar choices now select explicit verified variants;
default public New calls select city hall and military land. Creature and
spaceship defaults remain their verified IDs. New-source interception accepts
these specialized IDs, rejects unrelated family/unknown IDs, and pins the full
original project/epoch/participant generation across request construction and
submission. Existing Edit preserves its native subtype sentinel. Controlled
actual-i686 lifecycle/UI tests and an unchanged old-source RED default-route
check are retained under `out/preview9-creation-routes/`. They do not qualify
running-game palette, Save, remote load, edit or cold reopen for every variant.

Exact supported-image bytes/call slices and package/list receipts are under
`out/preview9-c27-vehicle-validation-re/`, including
`native-route-{disassembly.txt,receipt.json}`, `browser-editor-routing.json`,
`vehicle-routes.json`, and `actual-bem-membership.json`. Property-header matches
in editor configs containing unsupported Text fields are labeled as such in the
receipts; the relevant typed keys were not inferred from unqualified byte hits.

## Immutable alias online identity and native Adventure Save

C32's new military-land Vehicle passed model admission, class creation and
Undo/Redo through revision108. Its later ordinary Adventure Save produced a
server-only primary reference that local validation could not resolve. The
retained canonical class1342 used `40646200!7DAA8C24.24682294`, with a local-server
sentinel of `FFFFFFFFFFFFFFFF`. The genuine saved class instead had instance0
and server ID `00000074AD6445F8`. Those twelve bytes were the complete decoded
body difference; every other byte of the 1,236,877-byte body matched.

The exact ID came from the alias's 193-byte version13 pollen metadata. Its
serialized own online ID at4..11 was still `00000074AD6445F8`, although its self
key at0C had been remapped correctly. This field is independent of later
ancestry keys and IDs. Native pollen writer54B7B0 writes version13 followed by
metadata object18/1C as a big-endian uint64 (`54B835..54B84D`). Reader54C780 reads
that next uint64 directly into object18 (`54C86A..54C872`); getter5507A0 returns
its address. Ordinary Adventure Save calls F2B1E0 atF451FA on its private copied
ScenarioResource. That helper enumerates references and calls F2AAD0, which
loads pollen metadata, reads this getter, clears the local instance atF2ABE0
and copies a nonsentinel online ID into the reference's server field at
F2ABEB..F2ABF3. This reproduces the exact observed identity change without a
missing-model or relaxed-resolution assumption.

New immutable aliases now set only their own pollen online ID toUINT64_MAX,
inside the existing exact version13/self-key gate, before remapping the self
instance. Parent identities, foreign metadata, dependency records and input
bytes remain unchanged. Already admitted aliases remain immutable; a reserved
old alias requires a new identity. Native witness, model admission and Save
validation remain strict. Controlled tests retain the actual 193-byte input,
the old-source failure, all four creation families and their fifteen reviewed
New routes, foreign/tail equality, collision reminting and atomic malformed
input rejection. Evidence is under `out/preview9-c32-save-witness-audit/` and
`out/preview9-c34-alias-online-*.json`; running-game Save/reopen of the corrected
new aliases remains a separate acceptance gate.


## Resumable native creation Save probes

`tools/tests/coop_editor_creation_save.py` promotes the retained staged probe
into an inert-by-default source fixture. It performs one explicit `enter`,
`steps`, `inspect`, `finish`, or `cancel` stage. Every invocation requires an
owned candidate/metadata pair, two or four exact participants, an author and
a new OUT directory. Only Root's UI owner executes it. Each stage preserves
its result and commands independently, avoiding the overwritten command-log
problem in the historical Building probe. The OUT checkpoint head rejects
older/modified parent results; candidate, process pins, project/epoch, peer
generations, creation serial and Native request must remain unchanged. An
attempted recipe file is not replayed, and a dispatched Save is one-shot.

The explicit profiles bind separate request and model identities:

| Profile | New request editor ID | Native model type | Toolbar subtype |
| --- | --- | --- | --- |
| `creature` | `465C50BA` | `9EA3031A` AnimalCreature | none |
| `building-house` | `4E3F7777` | `4E3F7777` BuildingHouse | `C00F0341` |
| `vehicle-military-land` | `7D433FAD` | `7D433FAD` VehicleMilitaryLand | `C00F0340` |
| `spaceship` | `96B24187` | `98E03C0D` UFO | none |

`coop_editor_creation_validation.py` retains the previously verified bounded
ReadProcessMemory sampler and ABI checks. Its source promotion changes the
explicit request-profile table and adds an `--execute`/new-OUT gate, rather
than introducing Native function calls or new field assumptions. Before Done,
the stage fixture requires the exact request/model and allowed subtype,
changed model/metadata, computed finish state zero with debug bypass ignored,
no save/exit command pending, visible owned editor Done, and no active early
Done gate. It then rechecks the logical/session guard and performs one ordinary
Done click. A nonzero finish state stops with the masks and scene preserved.
Successful return requires one durable revision and history item, unchanged
old cast rows, one new immutable family-correct class, unchanged placement/act
counts, matching strict Native checks and zero faults. This is a creation Save
probe; Adventure Save and cold reopen remain separate acceptance.

The Creature input file `tools/tests/fixtures/editor_creation/creature-mouth-c7.json`
retains C7's exact mouth drag and naming clicks. Its provenance is historical:
C7 returned successfully, but C39's replay completed those instantaneous input
commands without changing the model/name (`changed=[0,0]`, finish state 4,
Mouthless). Root retained that no-mutation result before calibrating slower
normal drag, initial focus and naming waits. Thus the file is not a qualified
current recipe; the fresh zero-blocker preflight remains mandatory.

C11 proves a valid BuildingHouse with three accepted body parts and the name
`Preview9BuildingProbeC11`. It does not prove CityHall, and the retained final
command log contains only the last inspections, not the successful part-drag
sequence. The source fixture therefore permits BuildingHouse calibration from
an explicit measured steps file, but supplies no invented three-part recipe.
No valid minimal Spaceship construction/zero-blocker Save recipe is retained.
The C38 four-player run proves independent entry and Cancel, not those Saves.

A bounded sequence uses the same owned metadata/candidate/instances/author and
profile on every invocation:

```powershell
$common = @('--execute', '--metadata', $metadata, '--candidate', $candidate, '--instances', '95', '96', '--author', '95', '--profile', 'creature')
py -3 tools/tests/coop_editor_creation_save.py @common --stage enter --output out/creation-enter
py -3 tools/tests/coop_editor_creation_save.py @common --stage steps --state out/creation-enter/result.json --steps out/current-measured-creature-steps.json --name PreviewCreation --output out/creation-design
py -3 tools/tests/coop_editor_creation_save.py @common --stage inspect --state out/creation-design/result.json --output out/creation-inspected
py -3 tools/tests/coop_editor_creation_save.py @common --stage finish --state out/creation-inspected/result.json --output out/creation-saved
```

Use `--profile building-house` for the explicit House entry. Use the latest
result after an invalid Finish to inspect or apply a different measured input
file, then repeat the fresh preflight. A previous Save dispatch is terminal and
must not be retried. Neither fixture launches, deploys, rewrites profiles or
forces Native acceptance. Offline tests use fake games, owner checks and
Native receipts; they establish the stage/checkpoint contract, not actual
Creature/Building/Spaceship Save acceptance.


C39 subsequently calibrated and completed Creature creation Save from both
roles with focus, held drag, naming waits and blur. The exact nine steps are
retained in `tools/tests/fixtures/editor_creation/creature-held-c39.json`, from
`out/preview9-c39-creature-slow-design.measured.json`. Host admission reached
R17 and guest admission R18; each fresh preflight had changed flags `[1,1]`
and computed finish state zero. Own Undo/Redo preserved the foreign class.
A comparison in the first history driver incorrectly required an unchanged
`content_revision`; normal Redo correctly advances that revision, while
restoring the immutable key/payload. This fixture correction is not a product
mutation. Evidence: the C39 host/guest creature Save and author-history OUT
receipts. It remains case-specific evidence, not a general palette guarantee.

The staged fixture also supports `--target-class <full-id:generation>` on
every stage for an existing immutable class. The UI owner first selects the
proper native cast card; `enter` clicks only Edit. Baseline class/family/key
must exist. The read-only sampler uses the existing Request source-key field
and the verified `FFFFFFFF` constructor sentinel, requires `new_asset=false`,
and compares the complete source key. The New profile table and default New
flow remain unchanged. An Edit Save requires one exact `Replace shared asset
content` history item (the current production label), the same class/placement/
act roster, unchanged unrelated asset rows, unchanged target behavior/settings,
a different immutable family-correct key, and target `content_revision` equal
to the new durable revision. No concurrent behavior case is covered: the
canonical baseline stays fixed while the local editor is open. For example,
append `--target-class 8589935936:1` to the same common arguments before the
`enter`, `steps`, `inspect`, and `finish` commands above. A zero-blocker
preflight is still required before the one ordinary Done click.


### Admission while other participants keep independent editors open

`--allow-other-editors` explicitly permits other participants to remain in
already-entered creation sessions while the author starts from a ready Native
Adventure Editor. Every non-ready other participant needs an owned
`--other-editor INSTANCE:PROFILE` declaration. The default still requires all
participants to start ready. Repeat the opt-in and declarations on every stage.
Profiles retain the exact IDs in the table above; unsupported variants stop.

The public creation diagnostic provides phase, serial, stale and entered
state, but no family/save-phase fields. The tool therefore checks public
`phase=1 stale=0 entered=1` and unchanged serial at every guard/canonical poll,
then closes the exact family/model/request and idle Native save/command state
with the existing read-only sampler before/after stages and before author
Done. Its separate observer mode can pin another New or Edit session, including
that Edit's complete original source key. Observer receipts cannot be used as
the author's Save proof. Other session/source/family/pending-Save expiry
invalidates the workflow; a later replacement is not adopted. The author still
requires its own exact request/source and zero-blocker preflight.

All peers must keep the same project/epoch and participant generations, and
all complete canonical asset rows are compared before and after admission.
The initial Native roster/check comes from the ready author, rather than an
away host. Post-admission roster and strict checks cover the live Adventure
participants; held creators are explicitly marked
`not_applicable_other_creation_editor`. They must still hold their original
sessions while their canonical/durable copies advance. Returning those editors
and proving all Native scenes is a separate Root-owned acceptance step.

For example, a ready guest 96 can enter with three other held editors:

```powershell
$common = @('--execute', '--metadata', $metadata, '--candidate', $candidate, '--instances', '95', '96', '97', '98', '--author', '96', '--profile', 'building-house', '--allow-other-editors', '--other-editor', '95:creature', '--other-editor', '97:vehicle-military-land', '--other-editor', '98:spaceship')
py -3 tools/tests/coop_editor_creation_save.py @common --stage enter --output out/guest-enter-host-away
```

The controlled four-party cases keep all three others in blank/invalid local
designs, accept one guest admission, verify four matching canonical copies,
and prohibit Native Adventure inspect/check calls on the away participants.
They also cover a held Edit session, default rejection, other serial expiry
during input, and pending Native Save rejection before author Done. These are
offline contract tests, not an actual host-away Save result. ResourceKey group
zero remains valid: Edit checks require nonzero instance/type and full source
equality, without an extra group veto. The staged self-test is included in
Make's existing Python editor-tool block. New evidence is kept separately in
`out/preview9-creation-hostaway-source/`, preserving the earlier 108-check
receipt unchanged.


### C44 observed resolved Edit request

After cold recovery at revision 22, the host opens the guest-created Creature
from cast page 3. Two ordinary held button taps at the card open its native
property panel; the New/Edit toolbar then enables Edit. A single card click
only selects it. Palette sheet opens the replacement picker, and dice is the
separately blocked randomization action; neither is the property route.

The active Creature Edit request is already resolved: +0C is `465C50BA`, +1C is
Scenario return mode `01654C10`, +20 is false, reference count is 2, and its exact
source is `40626200!7DEA8A37.2B978C46`. The generic `FFFFFFFF` request ID is an
entry representation, not a required value after native resolution. The reader
and stage guards therefore accept generic or this exact family ID for Edit,
while still requiring the same owner, model subtype, false New flag and complete
source key. Another family is rejected. Windows/Linux staged contracts pass 165.
Evidence: `out/preview9-c44-edit-request-diagnostic/output.txt` and
`out/preview9-c44-host-edit-guest-enter-r3/result.json`.

Editing the creature name leads to the ordinary Done confirmation offering
Replace or Save New. Root chooses Save New once, without another Done, preserving
the prior native resource. Canonical admission replaces only class8589935936:1
at revision 23. Its immutable key changes; settings and behavior stay equal,
all unrelated classes stay equal, class/placement/act counts stay121/419/5,
and exactly one `Replace shared asset content` item is added. Both returned
scenes strictly converge with zero faults. Evidence:
`out/preview9-c44-host-edit-guest-confirm/result.json` and
`out/preview9-c44-shared-edit-final-proof/result.json`. The initial confirmation
post-check compared JSON lists with parser tuples; its retained failure is a
fixture representation error, independently corrected by read-only inspection.
No second confirmation or Save was issued.

### C44 bounded Edit confirmation and completed native Save state

The staged source fixture now recognizes only the measured Edit-exit question:
overlay `05B598F7`, modal `06318E78`, body `01510D08`, frame `05822D40`, and the
four visible, enabled buttons `05107B17` Don't Save, `05107B18` Cancel,
`05107B19` Replace, and `05107B1A` Save New. The complete overlay must end before
any later truncated UI output. The body must have the exact quoted
`Save "..." as a new creation or replace "..." before exiting?` form. The tool
closes the same Edit request, exact source, family, false New flag, public
serial, project/epoch, participant generations and held other editors against
a fresh read. Pending command must be `0x102` (258), confirmation flag zero, and
the full modal identity must still match a second UI snapshot. It records the
one-shot confirmation dispatch before clicking the measured Save New center.
It never repeats Done or confirmation, including after an uncertain input
failure. An already returning editor simply waits for its ordinary return.
The New workflow retains its original return predicate and no confirmation
input; the reader's additional command/flag coherence reads apply only to Edit.

Native save state `editor+38C == 6` is completed idle, not an active bake.
The supported-image per-frame driver at `58CD3E..58CD4D` explicitly skips the
Save state machine for both 0 and 6. After `Editor_Save` at `58628F`, completion
calls `585C10` at `5862E6`; `585C9E` writes 6. Active states 1 through 5 continue
through `585D40`. The source finish/other-editor guards therefore permit exactly
0 or 6 with pending command zero, while all identity, validation and visibility
guards remain required. Unknown states are refused. This matches the reused
C44 host New Creature: serial 3 stays designing, first blank and then changed
with finish state zero, while save state remains 6 and command remains zero.
Evidence is retained in `out/preview9-c44-host-away-design/result.json` and
`out/preview9-c44-guest-hostaway-enter-r2/other-editors-initial-native.json`.

Controlled two/four-party contracts cover one confirmation, delayed return,
other editors held, wrong source/request/pending/phase/modal, changed modal,
stale author/other serial, and post-dispatch click failure. The source receipt
is `out/preview9-c44-edit-confirm-source/receipt.json`. These tests are fake
game/owner/read-only-reader contracts; the actual shared Edit result above
remains Root's independently measured Native evidence.

### C44 changed-design Cancel confirmation

Cancel on the host's changed unsaved New Creature shows a separate two-button
discard question. Its exact text is `Exiting will cause your changes to be
lost. Are you sure you want to exit?` in body `01510D08`, owned by modal
`06318E78` under overlay `05B598F7`. Frame `05822D40` contains only the visible,
enabled Cancel `05107B17` and Quit `05107B1A` captions. The measured Quit center
is `(737,450)` at 1024x768. The original request remains `714121376`, New true,
source words `[0,2B978C46,0]`, save state 6, pending command `0x103` (259), and
confirmation flag zero. Evidence is retained in
`out/preview9-c44-return-diagnostic/result.json` and
`out/preview9-c44-held-host-confirm-return/before-quit-native.json`.

The staged Cancel tool now recognizes exactly that complete modal subtree. It
closes the original request, full pre-Cancel source, family, serial,
project/epoch, participant generations and any held other editors. A dedicated
read-only `--exit-confirmation` sample closes the command and confirmation fields
across the read, also for New requests. Native save state must be 0 or 6 and
command must be exactly 259. The modal must remain identical in a second query,
followed by a final identity sample. Initial Cancel and confirmation Quit are
each recorded before input. An uncertain click failure cannot replay either
action. An absent prompt retains the ordinary return wait, and invalid designs
can still be discarded without satisfying Save validation.

Controlled contracts cover one Quit, absent/delayed prompts, malformed or wrong
dialogs, wrong command/state/request/source, late source/serial expiry,
project/participant/other-editor expiry, stale reader receipts, both input
failure boundaries and terminal replay refusal. Canonical baseline and history
checks are unchanged; this does not add permission to resume against an
intervening canonical edit. The source-only Windows/Linux receipt is
`out/preview9-c44-cancel-source/receipt.json`.

Root's separate measured continuation discards that held host draft after the
guest admission at revision 24. Both Adventure scenes return with 122 classes,
419 placements and 5 acts, strict convergence and zero faults; history stays
unchanged. Evidence: `out/preview9-c44-held-host-confirm-return/result.json`.

### C46 immutable creation persistence and minimal UFO Save

Native adventure Save with the new Creature versions originally rejects generated
record `40626200!7D8C23AC.0F43029A` because its immutable alias intentionally
retains the BEM body rather than generated owner caches. The missing-only Saved
receipt correction now passes the actual revision-24 Save, including exact
SavedApplied 2/2, unchanged history and a retained original native DBPF. Evidence:
`out/preview9-c45-creatures-native-save/result.json`,
`out/preview9-c46-creatures-native-save/result.json` and
`out/editor-preview9-saved-cache/receipt.json`.

A fresh native reopen selects exact source `408A0000!2913EE8A.366A930D` with all
122 classes, 419 placements and five acts. Its complete projection fingerprint
differs from the pre-close fingerprint. Independent comparison of both owned
native images identifies only four `ScenarioAsset.machine` fields
(`62B74271` inside primary asset `07114976`, classes 1342 through 1345), changing
`5523CA25` to zero. Their raw resource keys, model IDs and server `UINT64_MAX`
values stay identical. All other 1,259,068 decoded bytes match, including every
marker pose, act, Captain and Crew. Planet semantics match; the outer changes
are the unused fourth scalar-key word and embedded compressed-container length.
This exact observed transition is recorded separately from literal fingerprint
equality; no general reference validation is broadened. Both participants then
share the reopened scene and strictly converge with zero faults. Evidence:
`out/preview9-c46-cold-comparison/comparison.json` and
`out/preview9-c46-cold-shared-continuation/result.json`. The initial literal
fingerprint assertion remains retained as a failed tool expectation.

New UFO uses request `96B24187`, model `98E03C0D` and asset family `476A98C7`.
The measured 1024x768 setup starts with an empty Body palette. One held body drag
and naming leave native blocker 4. A cockpit alone does not clear it. Adding the
measured propulsion part clears blocker 4, leaves nonblocking mask `00000800`,
and gives native finish state zero. The successful source uses:

- Body `(58,114)` to `(700,400)`.
- Name at `(500,741)` through ordinary text input.
- Cockpit category `(86,40)`, first cockpit `(58,114)` to hull `(675,345)`.
- Flight category `(236,40)`, propulsion `(58,238)` to hull `(800,370)`.

The host's ordinary Save adds class `8589935938:1` at revision 1; the guest
independently replays the retained 15-step recipe and adds `8589935939:1` at
revision 2. Each admission adds exactly one cast entry and one history item,
preserves every previous row and returns both scenes with strict agreement and
zero faults. Counts progress from 122 to 124 classes, with 419 placements and
five acts retained. Native request, subtype, source, allowed types, idle state,
changed flags and zero blockers are checked before each Done. Evidence:
`out/preview9-c46-host-spaceship-save/result.json` and
`out/preview9-c46-guest-spaceship-save/result.json`. The repeatable recipe is
`tools/tests/fixtures/editor_creation/spaceship-minimal-c46.json`; another setup
still requires its current viewport and palette to match before using it.

New House uses request/model `4E3F7777` and asset family `2399BE55`. The body
must drop on the visible construction floor `(650,580)`: dropping it at
`(700,450)` above that floor retains no part or budget change. The successful
body clears `00020000` but leaves blocker 4. Door category `(198,40)` and its
first part `(58,114)` to wall `(680,555)` retain blocker 4; Roof category
`(102,40)` and first roof `(58,114)` to exposed top `(620,493)` clear all
blockers. This measures the complete three-part native design rather than
assuming a drag succeeded.

Host Save adds `8589935940:1` at revision 3. The guest repeats the retained
15-step recipe and saves `8589935941:1` at revision 4. Both saves have native
finish zero, one cast/history admission each, matching returned Native checks
and zero faults. The complete 126-class draft then completes native adventure
Save with SavedApplied 2/2 and exact original DBPF retention. Evidence:
`out/preview9-c46-host-house-save/result.json`,
`out/preview9-c46-guest-house-save/result.json`, and
`out/preview9-c46-all-new-models-native-save/result.json`.
The recipe is `tools/tests/fixtures/editor_creation/building-house-minimal-c46.json`.
