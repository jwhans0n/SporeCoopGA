// Actual mods.cpp with controlled Win32 endpoints. No DLL/game/window is opened.
#include "../sporekit/core.h"
#include <bit>
#include <cstdio>
#include <cstdlib>
#include <deque>
#include <memory>
#include <mutex>
#include <stdexcept>
#include <thread>
#include <array>

static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { std::printf("FAIL %d: %s\n", __LINE__, #x); std::exit(1); } } while (0)
namespace {
const DWORD main_id = GetCurrentThreadId();
bool presenting = false, dispatch_failure = false, copy_failure = false, load_failure = false;
bool missing_export = false, refuse_main = false;
unsigned loads = 0, unloads = 0, frees = 0;
unsigned drop_scans=0,grace_calls=0;
int hook_count=0;
std::wstring bootstrap = L"alpha,beta";
std::vector<std::string> events, logs;
std::deque<std::function<void()>> tasks;
std::mutex tasks_mu;
std::function<void()> unload_callback, load_callback;
struct Module { alignas(IMAGE_NT_HEADERS) std::array<uint8_t,1024> image{};std::string name;bool loaded=true; };
std::vector<std::unique_ptr<Module>> modules;

void safe() { CHECK(GetCurrentThreadId() == main_id && !presenting); }
void unloaded(const char* name) { safe(); ++unloads; events.emplace_back(std::string("unload:") + name); if (unload_callback) unload_callback(); }
void unload_alpha() { unloaded("alpha"); }
void unload_beta() { unloaded("beta"); }
void unload_other() { unloaded("other"); }
const SkModInfo alpha_info{SK_ABI_VERSION, "alpha", "1", unload_alpha};
const SkModInfo beta_info{SK_ABI_VERSION, "beta", "1", unload_beta};
const SkModInfo other_info{SK_ABI_VERSION, "other", "1", unload_other};
const SkModInfo* mod_main(const SkHost*, SkMod* mod) {
    safe(); events.emplace_back("load:" + mod->name); if (load_callback) load_callback();
    if (refuse_main) return nullptr;
    return mod->name == "alpha" ? &alpha_info : mod->name == "beta" ? &beta_info : &other_info;
}
BOOL WINAPI create_directory(LPCWSTR, LPSECURITY_ATTRIBUTES) { safe(); return TRUE; }
BOOL WINAPI copy_file(LPCWSTR, LPCWSTR, BOOL) { safe(); return !copy_failure; }
BOOL WINAPI delete_file(LPCWSTR) { safe(); return TRUE; }
HMODULE WINAPI load_library(LPCWSTR path) {
    safe(); if (load_failure) return nullptr;
    const std::wstring text(path); const auto start = text.find_last_of(L'\\') + 1, end = text.find(L".i", start);
    const auto name = text.substr(start, end - start);
    auto module = std::make_unique<Module>(); module->name.assign(name.begin(), name.end());
    auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(module->image.data());dos->e_magic=IMAGE_DOS_SIGNATURE;dos->e_lfanew=0x80;
    auto* nt=reinterpret_cast<IMAGE_NT_HEADERS*>(module->image.data()+dos->e_lfanew);nt->Signature=IMAGE_NT_SIGNATURE;
    nt->OptionalHeader.SizeOfImage=DWORD(module->image.size());
    const auto result = reinterpret_cast<HMODULE>(module->image.data()); modules.push_back(std::move(module)); ++loads;
    return result;
}
FARPROC WINAPI get_proc(HMODULE, LPCSTR name) { safe(); CHECK(std::string(name) == "sporekit_mod_main"); return missing_export ? nullptr : std::bit_cast<FARPROC>(&mod_main); }
BOOL WINAPI free_library(HMODULE handle) {
    safe();for(auto& module:modules)if(module->image.data()==reinterpret_cast<uint8_t*>(handle)){
        CHECK(module->loaded);module->loaded=false;++frees;events.emplace_back("free:"+module->name);return TRUE;
    }
    CHECK(false);return FALSE;
}
void WINAPI grace_sleep(DWORD ms){safe();CHECK(ms==50);++grace_calls;}
DWORD WINAPI attributes(LPCWSTR path) { safe(); return std::wstring(path).find(L"missing") != std::wstring::npos ? INVALID_FILE_ATTRIBUTES : FILE_ATTRIBUTE_NORMAL; }
HANDLE WINAPI find_first(LPCWSTR, LPWIN32_FIND_DATAW) { safe(); return INVALID_HANDLE_VALUE; }
BOOL WINAPI find_next(HANDLE, LPWIN32_FIND_DATAW) { safe(); return FALSE; }
BOOL WINAPI find_close(HANDLE) { safe(); return TRUE; }
void run_tasks() {
    for (unsigned count = 0; count < 256; ++count) {
        std::function<void()> callback;
        { std::lock_guard<std::mutex> lock(tasks_mu); if (tasks.empty()) return; callback = std::move(tasks.front()); tasks.pop_front(); }
        callback();
    }
    CHECK(false);
}
}
#define CreateDirectoryW create_directory
#define CopyFileW copy_file
#define DeleteFileW delete_file
#define LoadLibraryW load_library
#define GetProcAddress get_proc
#define FreeLibrary free_library
#define GetFileAttributesW attributes
#define FindFirstFileW find_first
#define FindNextFileW find_next
#define FindClose find_close
#define Sleep grace_sleep
#ifdef SK_MODS_OLD_SOURCE
#include SK_MODS_OLD_SOURCE
#else
#include "../sporekit/mods.cpp"
#endif
#undef CreateDirectoryW
#undef CopyFileW
#undef DeleteFileW
#undef LoadLibraryW
#undef GetProcAddress
#undef FreeLibrary
#undef GetFileAttributesW
#undef FindFirstFileW
#undef FindNextFileW
#undef FindClose
#undef Sleep

namespace sk {
SkMod g_kit_mod;
SkHost g_host{};
KitFix g_fix_hookkeep{"hookkeep","controlled hook-removal grace",1};
DWORD main_thread_id() { return main_id; }
bool in_frame() { CHECK(GetCurrentThreadId() == main_id); return presenting; }
const std::wstring& mods_dirw() { static const std::wstring path = L"controlled\\mods"; return path; }
int instance_id() { return 95; }
std::string narrow(const std::wstring& text) { return {text.begin(), text.end()}; }
std::wstring widen(const std::string& text) { return {text.begin(), text.end()}; }
std::string ini_str(const std::wstring&, const char*, const char*, const char*) { return narrow(bootstrap); }
void run_outside_frame(std::function<void()> callback) {
    if (dispatch_failure) throw std::runtime_error("controlled queue failure");
    std::lock_guard<std::mutex> lock(tasks_mu); tasks.push_back(std::move(callback));
}
void logf(SkMod*, int, const char* format, ...) {
    char message[1024]; va_list args; va_start(args, format); vsnprintf(message, sizeof message, format, args); va_end(args);
    logs.emplace_back(message);
}
int hooks_remove_all(SkMod* mod) { safe(); events.emplace_back("hooks:" + mod->name);return hook_count; }
void unsubscribe_all(SkMod*) { safe(); }
void diag_context_remove(SkMod*) { safe(); }
void diag_headline(SkMod*,const char*){safe();}
size_t drop_tasks_in(uintptr_t lo,uintptr_t hi){
    safe();for(const auto& module:modules)if(uintptr_t(module->image.data())==lo){
        CHECK(module->loaded&&hi==lo+module->image.size());++drop_scans;return 0;
    }
    CHECK(false);return 0;
}
void commands_remove_all(SkMod*) { safe(); }
void ui_winprocs_disable_mod(SkMod*) { safe(); }
}

#ifndef SK_MODS_OLD_SOURCE
static void reset()
{
    presenting = dispatch_failure = copy_failure = load_failure = missing_export = refuse_main = false;
    loads = unloads = frees = drop_scans = grace_calls = 0;hook_count=0;sk::g_fix_hookkeep.on=1;
    bootstrap = L"alpha,beta"; unload_callback = load_callback = {};
    tasks.clear(); events.clear(); logs.clear(); modules.clear();
    sk::g_mods.clear(); sk::g_gen = 0; sk::g_lifecycle_queue.clear(); sk::g_request_id = 0;
    sk::g_lifecycle_busy = sk::g_lifecycle_scheduled = sk::g_lifecycle_stopping = false;
}
static void accepted(bool ok, const std::string& status) { CHECK(ok); CHECK(status.starts_with("queued request ")); }
static void load_alpha() { std::string status; accepted(sk::mod_load("alpha", status), status); run_tasks(); CHECK(loads == 1 && sk::mods_list()[0]->module); }
#endif

int main()
{
    std::string status;
    // True old-source RED: the actual old manager calls on_unload inside Present.
    bootstrap = L"alpha"; sk::mods_load_all(); CHECK(loads == 1);
    presenting = true;
    CHECK(sk::mod_unload("alpha", status));
    CHECK(unloads == 0 && frees == 0);
    presenting = false; run_tasks(); CHECK(unloads == 1 && frees == 1);
#ifdef SK_MODS_OLD_SOURCE
    std::printf("old manager: %u checks\n", checks);
    return 0;
#else
    CHECK(std::string(SK_VERSION) == "0.3.6"); CHECK(SK_ABI_VERSION == 1);
    // Upstream unload reads a real fixture PE extent and retains hook grace.
    reset();load_alpha();hook_count=1;
    accepted(sk::mod_unload("alpha",status),status);run_tasks();CHECK(drop_scans==1&&grace_calls==1&&frees==1);
    reset();load_alpha();hook_count=1;sk::g_fix_hookkeep.on=0;
    accepted(sk::mod_unload("alpha",status),status);run_tasks();CHECK(drop_scans==1&&!grace_calls&&frees==1);
    // Outside-frame callers also queue, so SDK self-unload returns before cleanup.
    reset(); load_alpha(); bool returned = false;
    unload_callback = [&] { CHECK(returned); };
    accepted(sk::mod_unload("alpha", status), status); CHECK(unloads == 0); returned = true; run_tasks(); CHECK(unloads == 1);
    // A caller can itself enter a nested modal/message pump before returning.
    // The capability guarantees main/outside Present, not callback quiescence.
    reset(); load_alpha(); bool caller_active = true, nested_outside = false;
    unload_callback = [&] { CHECK(caller_active); safe(); nested_outside = true; };
    accepted(sk::mod_unload("alpha", status), status); run_tasks();
    CHECK(nested_outside && unloads == 1); caller_active = false;
    // Worker requests cannot query in_frame or touch any actual manager endpoint.
    reset(); std::string owned = "alpha";
    std::thread worker([&] { accepted(sk::mod_load(owned, status), status); }); worker.join(); owned = "missing";
    CHECK(loads == 0); run_tasks(); CHECK(loads == 1 && sk::mods_list()[0]->name == "alpha");
    // FIFO reload/unload uses names at execution, never stale SkMod pointers.
    reset(); load_alpha(); events.clear();
    accepted(sk::mod_reload("alpha", status), status); accepted(sk::mod_unload("alpha", status), status);
    CHECK(loads == 1 && unloads == 0); run_tasks();
    CHECK((events == std::vector<std::string>{"unload:alpha", "hooks:alpha", "free:alpha", "load:alpha", "unload:alpha", "hooks:alpha", "free:alpha"}));
    // Nested outside pumps cannot execute while an on_unload callback is active.
    reset(); load_alpha(); bool nested = false;
    unload_callback = [&] {
        if (nested) return; nested = true;
        accepted(sk::mod_load("beta", status), status);
        sk::drain_lifecycle(); CHECK(loads == 1); CHECK(sk::g_lifecycle_busy);
        sk::mods_shutdown(); CHECK(!sk::g_lifecycle_stopping); CHECK(frees == 0);
    };
    accepted(sk::mod_unload("alpha", status), status); run_tasks(); CHECK(nested && loads == 2 && frees == 1);
    CHECK(events[events.size() - 2] == "free:alpha" && events.back() == "load:beta");
    // Turn cap reschedules work created by callbacks instead of spinning forever.
    reset(); unsigned callbacks = 0;
    load_callback = [&] { if (++callbacks < 70) accepted(sk::mod_reload("alpha", status), status); };
    accepted(sk::mod_load("alpha", status), status);
    { auto first = std::move(tasks.front()); tasks.pop_front(); first(); }
    CHECK(callbacks == 64 && tasks.size() == 1); run_tasks(); CHECK(callbacks == 70);
    // Queue count/name bounds and no path traversal. Rejection never adds work.
    reset(); presenting = true;
    for (unsigned i = 0; i < 64; ++i) accepted(sk::mod_unload("alpha", status), status);
    CHECK(!sk::mod_load("beta", status) && status == "mod lifecycle queue is full");
    CHECK(sk::g_lifecycle_queue.size() == 64 && tasks.size() == 1 && loads == 0);
    presenting = false; run_tasks(); CHECK(!sk::g_lifecycle_busy && sk::g_lifecycle_queue.empty());
    for (const std::string& name : std::vector<std::string>{"", ".", "..", "a..b", "a/b", "a\\b", "C:a", std::string(256, 'a'), std::string("a\0b", 3)}) {
        CHECK(!sk::mod_load(name, status)); CHECK(tasks.empty());
    }
    reset(); sk::g_request_id = std::numeric_limits<uint64_t>::max();
    CHECK(!sk::mod_load("alpha", status) && tasks.empty() && loads == 0);
    // A dispatcher exception cancels publication; a later request can retry.
    reset(); dispatch_failure = true; CHECK(!sk::mod_load("alpha", status));
    CHECK(tasks.empty() && sk::g_lifecycle_queue.empty() && !sk::g_lifecycle_scheduled && loads == 0);
    dispatch_failure = false; load_alpha();
    // If rescheduling reentrant work fails, cancel it with an explicit error log.
    reset(); load_callback = [&] { accepted(sk::mod_load("beta", status), status); dispatch_failure = true; };
    accepted(sk::mod_load("alpha", status), status);
    // Force this turn's cap with 64 harmless failures ahead of reentrant beta.
    for (unsigned i = 1; i < 64; ++i) accepted(sk::mod_unload("missing", status), status);
    run_tasks(); CHECK(loads == 1 && sk::g_lifecycle_queue.empty()); CHECK(!sk::g_lifecycle_busy);
    CHECK(logs.back().find("dispatch failed; cancelled 1") != std::string::npos);
    // Shutdown rejects unsafe execution, cancels all pending requests, then
    // unloads reverse order while the original native Shutdown has not run yet.
    reset(); sk::mods_load_all(); CHECK(loads == 2);
    presenting = true; sk::mods_shutdown(); CHECK(unloads == 0 && !sk::g_lifecycle_stopping);
    presenting = false; std::thread shutdown_worker([] { sk::mods_shutdown(); }); shutdown_worker.join(); CHECK(unloads == 0);
    accepted(sk::mod_reload("alpha", status), status); events.clear();
    unload_callback = [&] { CHECK(!sk::mod_load("other", status)); sk::drain_lifecycle(); };
    sk::mods_shutdown(); CHECK(unloads == 2 && frees == 2 && sk::g_lifecycle_stopping);
    CHECK(events.front() == "unload:beta" && events[3] == "unload:alpha");
    const auto count = events.size(); run_tasks(); CHECK(events.size() == count && loads == 2);
    CHECK(!sk::mod_reload("alpha", status)); sk::mods_load_all(); CHECK(loads == 2);
    // Direct private executor calls outside their guarded turn are denied.
    reset(); CHECK(!sk::load_mod("alpha", status)); CHECK(loads == 0);
    presenting = true; sk::mods_load_all(); CHECK(loads == 0); presenting = false;
    sk::g_lifecycle_busy = true; presenting = true;
    CHECK(!sk::load_mod("alpha", status) && loads == 0);
    presenting = false;
    std::thread executor_worker([&] { CHECK(!sk::load_mod("alpha", status)); }); executor_worker.join();
    CHECK(loads == 0); sk::g_lifecycle_busy = false;
    accepted(sk::mod_load("alpha", status), status); presenting = true; run_tasks();
    CHECK(loads == 0 && sk::g_lifecycle_queue.empty()); presenting = false;
    // Actual load errors are asynchronous and logged; later FIFO work survives.
    for (unsigned failure = 0; failure < 5; ++failure) {
        reset(); copy_failure = failure == 0; load_failure = failure == 1; missing_export = failure == 2; refuse_main = failure == 3;
        accepted(sk::mod_load(failure == 4 ? "missing" : "alpha", status), status); run_tasks();
        CHECK(!sk::mods_list().empty() && !sk::mods_list()[0]->module);
        CHECK(logs.back().find("completed") == std::string::npos);
        copy_failure = load_failure = missing_export = refuse_main = false;
        accepted(sk::mod_load("beta", status), status); run_tasks(); CHECK(sk::mods_list().back()->module);
    }
    reset(); load_alpha(); unload_callback = [] { throw std::runtime_error("unload failed"); };
    accepted(sk::mod_unload("alpha", status), status); run_tasks(); CHECK(frees == 0 && sk::mods_list()[0]->module && !sk::g_lifecycle_busy);
    CHECK(logs.back().find("callback threw") != std::string::npos);
    std::printf("sporekit controlled mod lifecycle: %u checks\n", checks);
    return 0;
#endif
}
