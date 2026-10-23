// SporeKit entry point (called by the loader at the exe entry point) and the SkHost table.
#include "core.h"

#include <cstdio>
#include <memory>

namespace sk {

SkMod g_kit_mod;
static std::wstring g_game_dir, g_mods_dir;
static std::string g_mods_dir_utf8;
static int g_instance = 1;

const std::wstring& game_dir() { return g_game_dir; }
const std::wstring& mods_dirw() { return g_mods_dir; }
int instance_id() { return g_instance; }

int ini_int(const std::wstring& file, const char* section, const char* key, int def)
{
    return (int)GetPrivateProfileIntW(widen(section).c_str(), widen(key).c_str(), def, file.c_str());
}

std::string ini_str(const std::wstring& file, const char* section, const char* key, const char* def)
{
    wchar_t buf[1024];
    GetPrivateProfileStringW(widen(section).c_str(), widen(key).c_str(), widen(def).c_str(), buf,
                             1024, file.c_str());
    return narrow(buf);
}

// ---------------- SkHost implementation ----------------
static void h_log(SkMod* m, int lvl, const char* fmt, ...)
{
    va_list ap;
    va_start(ap, fmt);
    logv(m ? m : &g_kit_mod, lvl, fmt, ap);
    va_end(ap);
}
static void h_logv(SkMod* m, int lvl, const char* fmt, va_list ap) { logv(m ? m : &g_kit_mod, lvl, fmt, ap); }
static uintptr_t h_sym(const char* n) { return sym(n); }
static int h_sym_require(SkMod* m, const char* const* names, int count)
{
    int miss = 0;
    for (int i = 0; i < count; i++)
        if (!sym(names[i])) {
            logf(m, SK_ERROR, "missing symbol: %s", names[i]);
            miss++;
        }
    return miss;
}
static uintptr_t h_image_base() { return image_base(); }
static uintptr_t h_rebase(uintptr_t va) { return rebase(va); }
static int h_hook_create(SkMod* m, void* t, void* d, void** o) { return hook_create(m, t, d, o); }
static int h_hook_enable(SkMod* m, void* t, int e) { return hook_enable(m, t, e); }
static int h_hook_remove(SkMod* m, void* t) { return hook_remove(m, t); }
static int h_subscribe(SkMod* m, int ev, SkEventFn fn, void* u) { return subscribe(m, ev, fn, u); }
static void h_unsubscribe(SkMod* m, int ev, SkEventFn fn, void* u) { unsubscribe(m, ev, fn, u); }
static int h_command_add(SkMod* m, const char* n, const char* h, SkCommandFn fn, void* u, int f)
{
    return command_add(m, n, h, fn, u, f);
}
static void h_out_printf(SkOut* out, const char* fmt, ...)
{
    if (!out)
        return;
    char buf[8192];
    va_list ap;
    va_start(ap, fmt);
    int n = vsnprintf(buf, sizeof buf, fmt, ap);
    va_end(ap);
    if (n > 0)
        reinterpret_cast<Out*>(out)->text.append(buf, n < (int)sizeof buf ? n : (int)sizeof buf - 1);
}
static int h_command_exec(const char* line, SkOut* out)
{
    std::string o;
    int r = command_exec_line(line, o, true);
    if (out)
        reinterpret_cast<Out*>(out)->text += o;
    return r;
}
static std::function<void()> retained_task(SkTaskFn fn, void* user)
{
    // A queued callback may outlive mod.reload. Keep its DLL mapped through the
    // callback and its payload cleanup; generation checks remain the mod's job.
    HMODULE module = nullptr;
    std::shared_ptr<void> keep_alive;
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
                          reinterpret_cast<LPCWSTR>(fn), &module))
        keep_alive = std::shared_ptr<void>(module, [](void* held) { FreeLibrary(static_cast<HMODULE>(held)); });
    return [fn, user, keep_alive = std::move(keep_alive)] {
        (void)keep_alive;
        fn(user);
    };
}
static void h_run_on_main(SkTaskFn fn, void* u)
{
    if (fn)
        run_on_main(retained_task(fn, u));
}
static std::wstring mod_ini(SkMod* m) { return g_mods_dir + L"\\" + widen(m ? m->name : "sporekit") + L".ini"; }
static int h_config_int(SkMod* m, const char* s, const char* k, int d) { return ini_int(mod_ini(m), s, k, d); }
static void h_config_str(SkMod* m, const char* s, const char* k, const char* d, char* buf, int sz)
{
    std::string v = ini_str(mod_ini(m), s, k, d ? d : "");
    if (sz > 0) {
        strncpy(buf, v.c_str(), sz - 1);
        buf[sz - 1] = 0;
    }
}
static int h_config_set_str(SkMod* m, const char* s, const char* k, const char* v)
{
    return WritePrivateProfileStringW(widen(s).c_str(), widen(k).c_str(), v ? widen(v).c_str() : nullptr,
                                      mod_ini(m).c_str()) ? 0 : 1;
}
static void h_run_outside_frame(SkTaskFn fn, void* u)
{
    if (fn)
        run_outside_frame(retained_task(fn, u));
}
static void* h_d3d_device() { return d3d_device(); }
static void* h_game_window() { return game_window(); }
static uint64_t h_frame_index() { return frame_index(); }
static double h_time() { return now_seconds(); }
static int h_instance() { return g_instance; }
static const char* h_mods_dir() { return g_mods_dir_utf8.c_str(); }
static void h_overlay_text(int x, int y, uint32_t c, const char* t) { overlay_text(x, y, c, t); }
static int h_w2s(const float* w, float* o) { return world_to_screen(w, o); }
static const char* h_exe_sha256()
{
    static std::string h = exe_sha256();
    return h.c_str();
}

SkHost g_host = {
    SK_ABI_VERSION, sizeof(SkHost),
    h_log, h_logv,
    h_sym, h_sym_require, h_image_base, h_rebase,
    h_hook_create, h_hook_enable, h_hook_remove,
    h_subscribe, h_unsubscribe,
    h_command_add, h_out_printf, h_command_exec,
    h_run_on_main,
    h_config_int, h_config_str,
    h_d3d_device, h_game_window, h_frame_index, h_time, h_instance, h_mods_dir,
    h_overlay_text, h_w2s,
    h_exe_sha256,
    cheat_register,
    screen_size,
    h_run_outside_frame,
    h_config_set_str,
    ui_winproc_new,
    ui_winproc_disable,
};

} // namespace sk

using namespace sk;

extern "C" __declspec(dllexport) int __cdecl sporekit_boot(HMODULE loader)
{
    (void)loader;
    static bool booted = false;
    if (booted)
        return 0;
    booted = true;
    set_main_thread();

    wchar_t exe[MAX_PATH];
    GetModuleFileNameW(nullptr, exe, MAX_PATH);
    g_game_dir = exe;
    g_game_dir.resize(g_game_dir.find_last_of(L"\\/"));
    g_mods_dir = g_game_dir + L"\\mods";
    g_mods_dir_utf8 = narrow(g_mods_dir);
    CreateDirectoryW(g_mods_dir.c_str(), nullptr);
    CreateDirectoryW((g_mods_dir + L"\\logs").c_str(), nullptr);

    char env[16] = {0};
    if (GetEnvironmentVariableA("SPOREKIT_INSTANCE", env, sizeof env))
        g_instance = atoi(env) > 0 ? atoi(env) : 1;
    // Bottles filters environment variables, so also accept a command line switch:
    //   SporeApp.exe -sporekit-instance:2
    if (const wchar_t* p = wcsstr(GetCommandLineW(), L"-sporekit-instance:"))
        g_instance = _wtoi(p + 19) > 0 ? _wtoi(p + 19) : 1;

    g_kit_mod.name = "sporekit";
    g_kit_mod.builtin = true;
    std::wstring ini = g_mods_dir + L"\\sporekit.ini";
    g_log_level = ini_int(ini, "sporekit", "log_level", SK_DEBUG);
    std::wstring logname = g_instance > 1 ? L"\\logs\\sporekit." + std::to_wstring(g_instance) + L".log"
                                          : L"\\logs\\sporekit.log";
    log_init(g_mods_dir + logname);
    SKLOG(SK_INFO, "SporeKit %s booting (instance %d, pid %lu)", SK_VERSION, g_instance,
          GetCurrentProcessId());

    watchdog_start();
    exceptions_install();
    watchdog_mark("hooks_init");
    hooks_init();
    watchdog_mark("symbols");
    symbols_load();
    watchdog_mark("instance");
    instance_install();
    focus_install_hooks();
    watchdog_mark("d3d9");
    d3d9_install();
    gamehooks_install();
    register_builtin_commands();
    cheats_register_builtin();
    int port = ini_int(ini, "sporekit", "console_port", 47800) + (g_instance - 1);
    watchdog_mark("console");
    console_start(port);
    watchdog_mark("mods");
    mods_load_all();
    watchdog_mark("game");
    SKLOG(SK_INFO, "boot complete");
    return 0;
}

BOOL WINAPI DllMain(HINSTANCE inst, DWORD reason, LPVOID)
{
    if (reason == DLL_PROCESS_ATTACH)
        DisableThreadLibraryCalls(inst);
    return TRUE;
}
