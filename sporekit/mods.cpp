// Mod manager: loads mods\<name>.dll through shadow copies so they can be rebuilt
// and hot-reloaded while the game is running.
#include "core.h"

#include <memory>
#include <deque>
#include <limits>
#include <mutex>

namespace sk {

static std::vector<std::unique_ptr<SkMod>> g_mods;
static int g_gen = 0;

enum class ModOperation { Load, Reload, Unload };
struct ModRequest {
    std::string name;
    ModOperation operation;
    uint64_t id;
};
static std::mutex g_lifecycle_mu;
static std::deque<ModRequest> g_lifecycle_queue;
static uint64_t g_request_id = 0;
static bool g_lifecycle_busy = false, g_lifecycle_scheduled = false, g_lifecycle_stopping = false;
static void drain_lifecycle();

static bool lifecycle_thread()
{
    // in_frame is main-thread state; never read it from a worker.
    return main_thread_id() != 0 && GetCurrentThreadId() == main_thread_id() && !in_frame();
}

static bool executor_current(std::string& err)
{
    if (!lifecycle_thread()) {
        err = "mod lifecycle requires the main thread outside Present";
        return false;
    }
    std::lock_guard<std::mutex> lock(g_lifecycle_mu);
    if (!g_lifecycle_busy) {
        err = "mod lifecycle executor is not active";
        return false;
    }
    return true;
}

// Caller holds g_lifecycle_mu. The existing outside queue only enqueues; it
// never invokes its callback inline. No mod pointer/output/callback is retained.
static void schedule_lifecycle()
{
    if (!g_lifecycle_busy && !g_lifecycle_stopping && !g_lifecycle_queue.empty() && !g_lifecycle_scheduled) {
        run_outside_frame(drain_lifecycle);
        g_lifecycle_scheduled = true;
    }
}

struct LifecycleExecution {
    ~LifecycleExecution()
    {
        size_t cancelled = 0;
        {
            std::lock_guard<std::mutex> lock(g_lifecycle_mu);
            g_lifecycle_busy = false;
            try { schedule_lifecycle(); }
            catch (...) { cancelled = g_lifecycle_queue.size(); g_lifecycle_queue.clear(); }
        }
        if (cancelled) {
            try { SKLOG(SK_ERROR, "mod lifecycle dispatch failed; cancelled %u requests", unsigned(cancelled)); }
            catch (...) {} // A failing diagnostic must not unwind an execution guard.
        }
    }
};

static std::wstring shadow_dir() { return mods_dirw() + L"\\.shadow"; }

std::vector<SkMod*> mods_list()
{
    std::vector<SkMod*> v;
    for (auto& m : g_mods)
        v.push_back(m.get());
    return v;
}

static SkMod* find_mod(const std::string& name)
{
    for (auto& m : g_mods)
        if (m->name == name)
            return m.get();
    return nullptr;
}

static bool load_into(SkMod* m, std::string& err)
{
    if (!executor_current(err)) return false;
    CreateDirectoryW(shadow_dir().c_str(), nullptr);
    m->generation = ++g_gen;
    // instance id in the name: several game instances share the mods folder
    m->live_path = shadow_dir() + L"\\" + widen(m->name) + L".i" + std::to_wstring(instance_id()) + L"." +
                   std::to_wstring(m->generation) + L".dll";
    if (!CopyFileW(m->src_path.c_str(), m->live_path.c_str(), FALSE)) {
        err = "cannot copy " + narrow(m->src_path) + " (err " + std::to_string(GetLastError()) + ")";
        return false;
    }
    m->module = LoadLibraryW(m->live_path.c_str());
    if (!m->module) {
        err = "LoadLibrary failed (err " + std::to_string(GetLastError()) + ")";
        DeleteFileW(m->live_path.c_str());
        return false;
    }
    auto main_fn = (SkModMainFn)(void*)GetProcAddress(m->module, "sporekit_mod_main");
    if (!main_fn) {
        err = "export sporekit_mod_main missing";
        FreeLibrary(m->module);
        m->module = nullptr;
        return false;
    }
    m->info = main_fn(&g_host, m);
    if (!m->info) {
        err = "mod refused to initialise (returned NULL)";
        hooks_remove_all(m);
        unsubscribe_all(m);
        diag_context_remove(m);
        commands_remove_all(m);
        ui_winprocs_disable_mod(m);
        FreeLibrary(m->module);
        m->module = nullptr;
        return false;
    }
    logf(m, SK_INFO, "loaded %s v%s (gen %d)", m->info->name, m->info->version, m->generation);
    return true;
}

static bool unload_from(SkMod* m, std::string& err)
{
    if (!executor_current(err)) return false;
    if (!m->module)
        return true;
    if (m->info && m->info->on_unload)
        m->info->on_unload();
    hooks_remove_all(m);
    unsubscribe_all(m);
    diag_context_remove(m);
    commands_remove_all(m);
    ui_winprocs_disable_mod(m);
    FreeLibrary(m->module);
    m->module = nullptr;
    m->info = nullptr;
    DeleteFileW(m->live_path.c_str());
    logf(m, SK_INFO, "unloaded");
    return true;
}

static bool load_mod(const std::string& name, std::string& err)
{
    if (!executor_current(err)) return false;
    if (find_mod(name) && find_mod(name)->module) {
        err = "already loaded";
        return false;
    }
    SkMod* m = find_mod(name);
    if (!m) {
        g_mods.push_back(std::make_unique<SkMod>());
        m = g_mods.back().get();
        m->name = name;
        m->src_path = mods_dirw() + L"\\" + widen(name) + L".dll";
    }
    if (GetFileAttributesW(m->src_path.c_str()) == INVALID_FILE_ATTRIBUTES) {
        err = "file not found: " + narrow(m->src_path);
        return false;
    }
    return load_into(m, err);
}

static bool unload_mod(const std::string& name, std::string& err)
{
    if (!executor_current(err)) return false;
    SkMod* m = find_mod(name);
    if (!m || !m->module) {
        err = "not loaded";
        return false;
    }
    return unload_from(m, err);
}

static bool reload_mod(const std::string& name, std::string& err)
{
    if (!executor_current(err)) return false;
    SkMod* m = find_mod(name);
    if (m && m->module && !unload_from(m, err)) return false;
    return load_mod(name, err);
}

static bool execute_request(const ModRequest& request, std::string& err)
{
    if (!executor_current(err)) return false;
    switch (request.operation) {
    case ModOperation::Load: return load_mod(request.name, err);
    case ModOperation::Reload: return reload_mod(request.name, err);
    case ModOperation::Unload: return unload_mod(request.name, err);
    }
    err = "invalid mod lifecycle operation";
    return false;
}

static void drain_lifecycle()
{
    {
        std::lock_guard<std::mutex> lock(g_lifecycle_mu);
        g_lifecycle_scheduled = false;
        if (g_lifecycle_stopping || g_lifecycle_busy) return;
        if (!lifecycle_thread()) {
            g_lifecycle_queue.clear();
            return; // Reject unexpected dispatch rather than unload inside Render/off-thread.
        }
        g_lifecycle_busy = true;
    }
    LifecycleExecution execution;
    // Reentrant callbacks can add work. Bound this turn as well as the queue.
    for (unsigned count = 0; count < 64; ++count) {
        ModRequest request;
        {
            std::lock_guard<std::mutex> lock(g_lifecycle_mu);
            if (g_lifecycle_queue.empty()) return;
            request = std::move(g_lifecycle_queue.front());
            g_lifecycle_queue.pop_front();
        }
        std::string err;
        bool ok = false;
        try { ok = execute_request(request, err); }
        catch (...) { err = "mod lifecycle callback threw"; }
        SKLOG(ok ? SK_INFO : SK_ERROR, "mod request %llu (%s): %s",
              static_cast<unsigned long long>(request.id), request.name.c_str(), ok ? "completed" : err.c_str());
    }
}

static bool request_mod(ModOperation operation, const std::string& name, std::string& status)
{
    // File stems only; bound every queued name independently of caller storage.
    if (name.empty() || name.size() > 255 || name == "." || name.find("..") != std::string::npos ||
        name.find_first_of("/\\:") != std::string::npos || name.find('\0') != std::string::npos) {
        status = "invalid mod name";
        return false;
    }
    try {
        std::lock_guard<std::mutex> lock(g_lifecycle_mu);
        if (g_lifecycle_stopping) { status = "mod manager is shutting down"; return false; }
        if (g_lifecycle_queue.size() >= 64) { status = "mod lifecycle queue is full"; return false; }
        if (g_request_id == std::numeric_limits<uint64_t>::max()) { status = "mod request IDs exhausted"; return false; }
        const auto id = g_request_id + 1;
        std::string accepted = "queued request " + std::to_string(id);
        g_lifecycle_queue.push_back({name, operation, id});
        try { schedule_lifecycle(); }
        catch (...) { g_lifecycle_queue.pop_back(); status = "mod lifecycle dispatch failed"; return false; }
        g_request_id = id;
        status.swap(accepted);
        return true;
    } catch (...) {
        status = "cannot retain mod lifecycle request";
        return false;
    }
}

bool mod_load(const std::string& name, std::string& status) { return request_mod(ModOperation::Load, name, status); }
bool mod_unload(const std::string& name, std::string& status) { return request_mod(ModOperation::Unload, name, status); }
bool mod_reload(const std::string& name, std::string& status) { return request_mod(ModOperation::Reload, name, status); }

void mods_load_all()
{
    {
        std::lock_guard<std::mutex> lock(g_lifecycle_mu);
        if (!lifecycle_thread() || g_lifecycle_busy || g_lifecycle_stopping) {
            SKLOG(SK_ERROR, "unsafe mod bootstrap rejected");
            return;
        }
        g_lifecycle_busy = true;
    }
    LifecycleExecution execution;
    // clean stale shadow copies from previous runs
    WIN32_FIND_DATAW fd;
    HANDLE h = FindFirstFileW((shadow_dir() + L"\\*.i" + std::to_wstring(instance_id()) + L".*.dll").c_str(), &fd);
    if (h != INVALID_HANDLE_VALUE) {
        do
            DeleteFileW((shadow_dir() + L"\\" + fd.cFileName).c_str());
        while (FindNextFileW(h, &fd));
        FindClose(h);
    }

    std::string list = ini_str(mods_dirw() + L"\\sporekit.ini", "mods", "load", "*");
    std::vector<std::string> names;
    if (list == "*") {
        h = FindFirstFileW((mods_dirw() + L"\\*.dll").c_str(), &fd);
        if (h != INVALID_HANDLE_VALUE) {
            do {
                std::wstring f = fd.cFileName;
                names.push_back(narrow(f.substr(0, f.size() - 4)));
            } while (FindNextFileW(h, &fd));
            FindClose(h);
        }
    } else {
        size_t s = 0;
        while (s <= list.size()) {
            size_t e = list.find(',', s);
            if (e == std::string::npos)
                e = list.size();
            std::string n = list.substr(s, e - s);
            while (!n.empty() && n.front() == ' ') n.erase(0, 1);
            while (!n.empty() && n.back() == ' ') n.pop_back();
            if (!n.empty())
                names.push_back(n);
            s = e + 1;
        }
    }
    for (auto& n : names) {
        std::string err;
        if (!load_mod(n, err))
            SKLOG(SK_ERROR, "mod %s: %s", n.c_str(), err.c_str());
    }
}

void mods_shutdown()
{
    {
        std::lock_guard<std::mutex> lock(g_lifecycle_mu);
        if (!lifecycle_thread() || g_lifecycle_busy) {
            SKLOG(SK_ERROR, "unsafe mod shutdown rejected");
            return;
        }
        g_lifecycle_stopping = true;
        g_lifecycle_queue.clear();
        g_lifecycle_scheduled = false;
        g_lifecycle_busy = true;
    }
    LifecycleExecution execution;
    for (auto it = g_mods.rbegin(); it != g_mods.rend(); ++it) {
        std::string err;
        try {
            if (!unload_from(it->get(), err)) SKLOG(SK_ERROR, "mod shutdown: %s", err.c_str());
        } catch (...) { SKLOG(SK_ERROR, "mod shutdown callback threw; module retained"); }
    }
}

} // namespace sk
