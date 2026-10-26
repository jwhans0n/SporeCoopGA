# Imported package admission: ownership and validation

This admission guard is under validation. It preserves the verified same-module cached database promotion and does not unload live resources. The controlled fixtures below do not establish a running-game memory plateau; real cold-start and native compatibility checks remain required.

## Behavior

- Each process can charge at most 32 native package attempts and 256 MiB aggregate validated uncompressed payload. Existing 128 MiB per-bundle limits remain. Refused content can still be cached on disk; it is not registered with the native ResourceManager.
- Reservations are committed before native construction, AddRef or AddDB. Allocation/open/register failure, a C++ exception, interrupted completion, shutdown or abandoned module completion never refunds a charge. Uncertain state prevents more admissions until process restart.
- A previously successful current-module entry can use the existing remove/add reprioritization without another charge, native allocation or caller AddRef. Its existing caller reference remains held throughout. A failed promotion becomes uncertain; it does not release/free the database.
- A matching resident database without a current-module successful receipt is refused without taking a caller reference. Nonempty DLL reload refuses reuse and new admissions. A path/hash lookup alone is not treated as ResourceManager lifetime proof.
- Cache-only editor Test/Return still validates hashes, pinned resource ownership and transfer acknowledgements and creates no native database. The existing 20 changed-root cache-only fixture remains covered.
- Normal Shared End does not unload a database or refund its charge. The existing explicit `release()` recipe is unchanged, but it also does not refund process charges. An old hash whose owned map entry was explicitly released cannot be readmitted as a zero-cost reuse; current production has no caller of that explicit release function.
- No game version, protocol, bundle format, updater, native symbol or native calling convention changes. This is an admission bound, not a guarantee on expanded native heap/allocator/GPU use or a replacement for memory/FPS acceptance.

## Helper and wiring

`mods/coop/package_admission.h/.cpp` implements a portable fixed-POD core. Its 1,960-byte ledger has exactly 32 entries, checked aggregate charges, immutable hash/root/size receipts, module and completion serials, trusted lifecycle state, bound executable/profile identity, and exact reservation completion. Late/stale/forged completion cannot claim success, and a profile mismatch permanently fails the process ledger closed.

`package_admission_windows.h/.cpp` owns the 4 KiB pagefile mapping and existing SDK event subscriptions. Its mapping name uses PID and exact process creation FILETIME; the header additionally binds the executable SHA-256 and a normalized hash of the existing verified absolute adventure-cache path. It does not open a game profile or save database. A single original owner mapping handle survives DLL unload; later attachments close only their newly acquired handle and unmap their own view. No native pointer or code pointer is persisted.

The wrapper subscribes real `SK_EV_APP_PREINIT`, `SK_EV_APP_INIT`, `SK_EV_POST_INIT` and `SK_EV_SHUTDOWN`. First admission requires the complete ordered cold-start sequence. A late load without that sequence fails; a new empty mapping is not proof that an older DLL mounted no resources. Empty trusted reloads can continue. Any nonempty reload fails closed because current approved APIs cannot prove ResourceManager continuity. The SDK hook source boundaries are `sporekit/main.cpp:201-215,305-319` and `sporekit/gamehooks.cpp:18-44,59-64`; actual supported-game event ordering remains an integration gate.

`adventure_assets::init` initializes the wrapper before native content preparation. `apply_bundle` uses it for new registrations and exact-owned promotion. The unknown-resident post-reload AddRef path becomes a clear restart refusal. Existing open/register failure cleanup remains; no new native release/unregister boundary is introduced. `coop.cpp::on_unload` detaches the ledger view after transfer reset without releasing native resources. Admission counters added to `coop.adventure status` are cached values and do not map, serialize, enumerate or access native resources during rendering.

The mapping/core update guard is never held during native execution. Reentrant import is refused while an exact ticket is active. An unreturned native call retains its charged Reserved ticket; further imports are refused with interruption/restart guidance. A C++ exception is caught and marked Uncertain. This helper does not claim to recover from a native fault or repair an interrupted ResourceManager operation.

## Native and OS ownership evidence

The existing verified packed database recipe is unchanged: allocate `0x388`, call DatabasePackedFile ctor, hold caller AddRef at `database+4`, initialize/open through the existing vtable, then register through ResourceManager `+0x50`. The existing exact-owned cached branch removes/adds that same database while retaining its caller reference. It is reprioritization, not eviction of a no-longer-owned object. References: `docs/re/adventure-world.md:148-153` and the existing `apply_bundle` implementation. The old menu unregister/release fixture at `:1102-1117` is not used as proof that Shared End can safely free resources.

Native RE found no approved database enumeration API or persistent SkHost userdata getter. No new guessed catalog entry, layout or Native ABI call is introduced here. The fixed Windows mapping is cooperative accounting, not a security boundary against other code in the process. The ResourceManager is assumed to have its normal SDK lifetime during one attached module; this proposal deliberately makes no continuity claim after DLL reload.

## Validation and remaining gates

The isolated validation builds the actual staged helper and `adventure_assets.cpp`, rather than only the earlier Python policy model. `out/editor-package-admission-c16/implementation-validation.json` records exact inputs before/after, compiler commands, outputs and executable hashes. Portable core tests use ASan/UBSan with leak detection; actual i686 Windows tests use the exact CI toolchain. Windows tests access only their own process handle/mapping identity and synthetic SDK callbacks. Native integration tests use controlled fake i686 vtables plus isolated temporary content caches and real UDP fixture connections; they do not run the game or touch real profiles.

The fixtures cover aggregate/count limits before native allocation, same-module zero-charge promotion and unchanged caller ref, failed remove/add promotion, allocation/init/open/AddDB failure, C++ exception, unknown resident, nonempty reload refusal before native lookup, priority overflow, cold/late lifecycle, profile mismatch, stale completion, partial SDK subscription failure and malformed mapping cleanup. A 1,000-empty-reload test keeps exactly one process owner handle. The ordinary native content fixture preserves cache-only transfer/hash/cancellation/stale-policy behavior.

Still required before acceptance: review the 32/256 MiB early-refusal defaults; prove real cold-start hook order before content import; exercise normal GA old-version replay/promotion, many distinct projects and reload refusal in owned muted games; and repeat strict memory/FPS gates without relaxing them. No current game or release acceptance is claimed by these fixtures.

Final isolated results: 3,238 core checks passed on both Linux ASan/UBSan with leak detection and actual i686 Windows; nine Windows mapping/SDK cases passed 14,230 checks; fourteen fake-native ABI/content cases passed 987 checks. No game input, profile database access, native process launch, deployment or production-source application was performed for these results.
