// Native lab fixture: callbacks queued from on_unload must keep their DLL mapped.
#include <windows.h>
#include "sporekit.h"

namespace {
const SkHost* host;
SkMod* mod;
DWORD cookie;
struct Payload { const SkHost* host; SkMod* mod; DWORD cookie; const char* queue; };
void callback(void* user)
{
    auto* payload = static_cast<Payload*>(user);
    payload->host->log(payload->mod, SK_INFO, "task-lifetime callback cookie=%lu queue=%s payload=deleted",
                       payload->cookie, payload->queue);
    delete payload;
}
void unload()
{
    host->run_on_main(callback, new Payload{host, mod, cookie, "main"});
    host->run_outside_frame(callback, new Payload{host, mod, cookie, "outside"});
}
SkModInfo info{SK_ABI_VERSION, "task_lifetime", "test", unload};
}

extern "C" SK_EXPORT const SkModInfo* sporekit_mod_main(const SkHost* value, SkMod* self)
{
    host = value;
    mod = self;
    cookie = GetTickCount();
    HMODULE module = nullptr;
    GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
                      reinterpret_cast<LPCWSTR>(&callback), &module);
    host->log(mod, SK_INFO, "task-lifetime loaded cookie=%lu module=%p", cookie, module);
    return &info;
}
