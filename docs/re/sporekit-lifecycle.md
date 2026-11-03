# SporeKit 0.3.5 mod lifecycle boundary

Interactive `mod.load`, `mod.reload` and `mod.unload` now acknowledge
`ok: queued request N`. Completion or failure is recorded in the Kit log with
that request number. This applies to TCP, watcher tools, cheats, overlay code,
and SDK `command_exec`, including its immediate `from_main` route. The three
commands request `SK_CMD_OUTSIDE_FRAME`; the manager itself queues every
interactive request, so bypassing a command flag cannot unload inline.

The central FIFO retains only an operation, copied file stem and request ID.
It admits at most 64 pending requests, each with at most 255 name bytes, and
rejects separators, drive prefixes, embedded NUL and `..`. It captures neither
`SkMod*` nor `SkOut*` nor a mod callback. A successful reply means admission to
the queue, not successful loading. Existing reload tools print that reply;
the task-lifetime probe now waits for its loaded-cookie log before proceeding.

Every actual manager executor requires the established main thread, outside
`Present`, and an exclusive lifecycle turn. Workers never read the main-thread
`in_frame` state. Reentrant requests remain queued; nested outside-task pumping
cannot run them until the active executor returns. Each turn executes at most
64 requests and reschedules any remainder. Queue allocation/dispatch failure
does not execute the requested operation; a failure to dispatch previously
accepted reentrant work cancels that work with an explicit log. A throwing
`on_unload` retains the module and reports failure rather than freeing it.

Bootstrap and shutdown remain synchronous, with the same execution guards.
Shutdown closes admission, cancels queued requests, and unloads in reverse
order before the original native application shutdown. Already-posted queue
callbacks then perform no mod operations. Unexpected off-thread, in-Present
or reentrant bootstrap/shutdown calls are rejected rather than invoking mod
callbacks in that context. No queued payload holds native ownership.

This is a main-thread/outside-Present/nonreentrant-manager guarantee, not
arbitrary SDK callback quiescence. A requesting callback can itself enter a
nested native modal/message pump before returning; that pump may dispatch the
queued lifecycle operation outside Present while the caller remains active.
Adapters must retain callback code and recheck their own module/lifetime/request
fences after callbacks. An unload-time native gate must cancel its exact native
pending state before an outstanding completion can resume it.

The capability is the existing `SkHost::kit_version()` value `0.3.5`; the
`SkHost` layout and `SK_ABI_VERSION == 1` are unchanged. A native adapter that
requires unload-time outside-Present cleanup must require this lifecycle
contract before enrolling. This change does not implement or enable the
selected-adventure dependency gate itself.

## Supported native shutdown receipt

The supported GOG image is
`dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37`.
`EAMain F48810` calls application `Run F47420` at `F48988`, then application
`Shutdown F475A0` at `F4898F`, after Run has returned. The other direct
Shutdown call, `F4827F`, belongs to initialization-failure cleanup before Run.
Kit `h_Shutdown` fires the shutdown event and calls `mods_shutdown` before
invoking that original function. The native shutdown is not queued and its
borrowed `this` is not retained beyond the hook.

This boundary is needed for a future selected-adventure preflight pause:
native loader timeout modal 7 can finish on OK without repeating held phase-0
checks. Dropping a gate ticket on unload is therefore insufficient. A gate
must cancel its exact current native pause during safe `on_unload`, before
its hooks disappear; stale queued completions must remain zero-native-call.

## Controlled validation

`tests/sporekit_mod_lifecycle_windows_tests.cpp` compiles the actual manager
for i686 with controlled Win32 loader and task endpoints. It launches no game
and loads no DLL. The retained old manager fails when its actual `on_unload`
enters during controlled Present. The new corpus covers worker requests,
callback return ordering, FIFO reload/unload, nested pumping, bounded turns,
queue/name limits, dispatch and callback failures, executor guards, shutdown
cancellation and stale posted work. The focused target is
`test-mod-lifecycle-windows`; exact source/executable receipts are retained
under `out/preview9-c35-kit-lifecycle`.
