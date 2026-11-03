// Real input.cpp detours and scheduling, controlled Win32 endpoints. No hooks
// are installed in Windows, no game is launched, and no window receives input.
#include "../sporekit/core.h"
#include <array>
#include <bit>
#include <cstdarg>
#include <cstdio>
#include <cstdlib>
#include <utility>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
namespace {
std::wstring command, profile;
bool invisible = true, module_available = true, cursor_held = false;
int instance = 95, hook_calls = 0, async_calls = 0, key_calls = 0, messages = 0;
std::array<SHORT, 256> physical{};
std::array<int, 2> hook_failure{};
std::array<bool, 2> missing_export{}, null_original{};
std::array<void*, 2> detours{};
std::vector<std::pair<int, std::function<void()>>> tasks;
SHORT WINAPI original_async(int vk) { ++async_calls; return vk >= 0 && vk < 256 ? physical[vk] : SHORT(0x1234); }
SHORT WINAPI original_key(int vk) { ++key_calls; return vk >= 0 && vk < 256 ? physical[vk] : SHORT(0x2345); }
LPWSTR WINAPI command_line() { return command.data(); }
HMODULE WINAPI module_handle(LPCWSTR name) { CHECK(std::wstring(name) == L"user32.dll"); return module_available ? HMODULE(0x1234) : nullptr; }
FARPROC WINAPI proc_address(HMODULE module, LPCSTR name) {
    CHECK(module == HMODULE(0x1234));
    const bool async = std::string(name) == "GetAsyncKeyState";
    CHECK(async || std::string(name) == "GetKeyState");
    if (missing_export[async ? 0 : 1]) return nullptr;
    return std::bit_cast<FARPROC>(async ? &original_async : &original_key);
}
BOOL WINAPI post_message(HWND window, UINT, WPARAM, LPARAM) { CHECK(window == HWND(0x5678)); ++messages; return TRUE; }
UINT WINAPI map_key(UINT key, UINT type) { CHECK(type == MAPVK_VK_TO_VSC); return key; }
BOOL WINAPI client_to_screen(HWND window, LPPOINT) { CHECK(window == HWND(0x5678)); return TRUE; }
}
#define GetCommandLineW command_line
#define GetModuleHandleW module_handle
#define GetProcAddress proc_address
#define PostMessageW post_message
#define MapVirtualKeyW map_key
#define ClientToScreen client_to_screen
#include "../sporekit/input.cpp"
#undef GetCommandLineW
#undef GetModuleHandleW
#undef GetProcAddress
#undef PostMessageW
#undef MapVirtualKeyW
#undef ClientToScreen

namespace sk {
SkMod g_kit_mod;
void* game_window() { return reinterpret_cast<void*>(0x5678); }
bool invisible_mode() { return invisible; }
const std::wstring& profile_name() { return profile; }
int instance_id() { return instance; }
void logf(SkMod*, int, const char*, ...) {}
std::wstring widen(const std::string& text) { return {text.begin(), text.end()}; }
void vcursor_set(int, int) {}
void vcursor_hold(bool held) { cursor_held = held; }
void run_after_frames(int frames, std::function<void()> callback) { tasks.emplace_back(frames, std::move(callback)); }
int hook_create(SkMod* mod, void* target, void* detour, void** original) {
    CHECK(mod == &g_kit_mod); const unsigned index = target == reinterpret_cast<void*>(original_async) ? 0 : 1;
    CHECK(index == 0 || target == reinterpret_cast<void*>(original_key)); ++hook_calls;
    detours[index] = detour; *original = null_original[index] ? nullptr : target;
    return hook_failure[index];
}
}

static void reset(bool exclusive)
{
    command = L"SporeApp.exe -sporekit-invisible -sporekit-profile:winlab_input_95";
    if (exclusive) command += L" -sporekit-exclusive-input";
    profile = L"winlab_input_95"; invisible = module_available = true; instance = 95;
    physical.fill(0); hook_failure.fill(0); missing_export.fill(false); null_original.fill(false); detours.fill(nullptr);
    hook_calls = async_calls = key_calls = messages = 0; cursor_held = false; tasks.clear();
    sk::g_input_policy = {}; sk::g_input_policy_frozen = sk::g_input_hooks_attempted = false; sk::g_input_hooks_installed = 0;
    sk::o_GetAsyncKeyState = nullptr; sk::o_GetKeyState = nullptr;
    for (auto& key : sk::g_vkdown) key = 0;
}
static SHORT poll_async(int vk) { CHECK(detours[0]); return reinterpret_cast<SHORT(WINAPI*)(int)>(detours[0])(vk); }
static SHORT poll_key(int vk) { CHECK(detours[1]); return reinterpret_cast<SHORT(WINAPI*)(int)>(detours[1])(vk); }
static void release_tasks()
{
    auto queued = std::move(tasks); tasks.clear();
    for (auto& [frames, callback] : queued) { CHECK(frames > 0); callback(); }
}

int main()
{
    const std::array<int, 9> keys{VK_LBUTTON, VK_RBUTTON, VK_MBUTTON, VK_SHIFT, VK_LSHIFT, VK_RSHIFT, VK_CONTROL, VK_MENU, 'A'};
    reset(false);
    CHECK(sk::input_install_hooks() && hook_calls == 0); // Legacy installation stays lazy.
    CHECK(sk::input_info().find("policy=os-or-virtual") != std::string::npos);
    CHECK(sk::input_key('A', "up") && hook_calls == 2);
    for (int vk : keys) {
        physical[vk] = SHORT(0x8000);
        const auto before_async = async_calls, before_key = key_calls;
        CHECK(poll_async(vk) == SHORT(0x8000) && poll_key(vk) == SHORT(0x8000));
        CHECK(async_calls == before_async + 1 && key_calls == before_key + 1);
        CHECK(sk::input_key(vk, "down"));
        CHECK(poll_async(vk) == SHORT(0x8001) && poll_key(vk) == SHORT(0x8000));
        CHECK(sk::input_key(vk, "up"));
        CHECK(poll_async(vk) == SHORT(0x8000) && poll_key(vk) == SHORT(0x8000));
        physical[vk] = 1; CHECK(poll_async(vk) == 1 && poll_key(vk) == 1);
        physical[vk] = 0;
    }
    CHECK(sk::input_key('A', "tap"));
    CHECK(poll_async('A') == SHORT(0x8001) && poll_key('A') == SHORT(0x8000));
    release_tasks(); CHECK(poll_async('A') == 0 && poll_key('A') == 0);
    // Policy is immutable after startup even if a controlled command buffer changes.
    command += L" -sporekit-exclusive-input"; CHECK(sk::input_install_hooks());
    physical[VK_SHIFT] = SHORT(0x8000); CHECK(poll_key(VK_SHIFT) == SHORT(0x8000));

    reset(true);
    CHECK(sk::input_install_hooks() && hook_calls == 2 && sk::g_input_hooks_installed == 2);
    CHECK(sk::input_info().find("policy=exclusive-virtual requested=1 hooks=2/2") != std::string::npos);
    for (int vk : keys) {
        physical[vk] = SHORT(0x8001);
        CHECK(poll_async(vk) == 0 && poll_key(vk) == 0);
        CHECK(sk::input_key(vk, "down"));
        CHECK(poll_async(vk) == SHORT(0x8001) && poll_key(vk) == SHORT(0x8000));
        CHECK(sk::input_key(vk, "up")); CHECK(poll_async(vk) == 0 && poll_key(vk) == 0);
    }
    CHECK(async_calls == 0 && key_calls == 0); // Physical/toggle state never leaks for a valid VK.
    CHECK(poll_async(-1) == SHORT(0x1234) && poll_async(256) == SHORT(0x1234));
    CHECK(poll_key(-1) == SHORT(0x2345) && poll_key(256) == SHORT(0x2345));
    CHECK(async_calls == 2 && key_calls == 2); // Invalid VK preserves native API forwarding.
    CHECK(sk::input_button(20, 30, 0, true) && cursor_held);
    CHECK(poll_async(VK_LBUTTON) == SHORT(0x8001));
    CHECK(sk::input_button(20, 30, 0, false) && !cursor_held);
    CHECK(poll_async(VK_LBUTTON) == 0);
    CHECK(sk::input_key(VK_SHIFT, "tap")); CHECK(poll_key(VK_SHIFT) == SHORT(0x8000));
    release_tasks(); CHECK(poll_key(VK_SHIFT) == 0);
    command = L"game"; profile = L"personal"; invisible = false; instance = 1;
    CHECK(sk::input_install_hooks() && hook_calls == 2 && sk::g_input_policy.exclusive);
    CHECK(poll_async(VK_SHIFT) == 0); // Neither later settings nor OS state change startup policy.

    for (unsigned failed = 0; failed < 2; ++failed) for (bool null_trampoline : {false, true}) {
        reset(true);
        if (null_trampoline) null_original[failed] = true; else hook_failure[failed] = -1;
        CHECK(!sk::input_install_hooks()); CHECK(hook_calls == 2 && sk::g_input_policy.exclusive);
        CHECK(sk::g_input_hooks_installed == 1);
        CHECK(!sk::input_install_hooks() && hook_calls == 2); // Failed opt-in never silently falls back.
    }
    reset(true); module_available = false;
    CHECK(!sk::input_install_hooks() && hook_calls == 0);
    for (unsigned missing = 0; missing < 2; ++missing) {
        reset(true); missing_export[missing] = true;
        CHECK(!sk::input_install_hooks() && hook_calls == 1 && sk::g_input_hooks_installed == 1);
    }
    reset(true); profile = L"personal";
    CHECK(sk::input_install_hooks() && hook_calls == 0 && !sk::g_input_policy.exclusive);
    CHECK(sk::input_info().find("reason=explicit-winlab-profile-required") != std::string::npos);
    CHECK(sk::input_key(VK_SHIFT, "up")); physical[VK_SHIFT] = SHORT(0x8000);
    CHECK(poll_key(VK_SHIFT) == SHORT(0x8000));
    std::printf("sporekit controlled input hooks: %u checks\n", checks);
    return 0;
}
