#include "adventure_assets.h"

#include <windows.h>

#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>

#include "sporekit_game.hpp"
#include "../../updater/sha256.h"

namespace adventure_assets {

namespace content = net::adventure_content;
static const SkHost* H;
static SkMod* M;
static Ctx C;
static uintptr_t a_GetDb, a_New, a_DbCtor, a_Rm;
static std::wstring g_dir;
static uint64_t g_run, g_generation;
static Key g_key;
static content::Bundle g_bundle;
static content::Receiver g_rx;
static bool g_preparing, g_prepared, g_applying, g_cache_only;
static std::string g_error;
static std::map<std::string, void*> g_databases;
static int g_priority = 4000;

enum Kind : uint8_t { Offer = 64, Record = 65, Need = 66, Chunk = 67, Done = 68, Ack = 69, Error = 70, ManifestDone = 71 };
constexpr uint16_t kNeedDone = 0xFFFF;

struct Out {
    uint64_t run = 0;
    uint16_t manifest = 0;
    bool offered = false, manifest_done = false, need_done = false, done = false;
    std::vector<uint16_t> needed;
    std::vector<bool> requested;
    size_t resource = 0;
    uint32_t offset = 0;
};
static std::map<uint8_t, Out> g_out;

static bool host() { return C.sess && C.sess->role() == net::Role::Host; }
static void feed(const std::string& s) { if (C.feed) C.feed(s); }
static void put_run(net::Writer& w, uint64_t run) { w.u32((uint32_t)run); w.u32((uint32_t)(run >> 32)); }
static uint64_t get_run(net::Reader& r) { uint64_t lo = r.u32(); return lo | ((uint64_t)r.u32() << 32); }
static void put_key(net::Writer& w, const Key& k) { w.u32(k.instance); w.u32(k.type); w.u32(k.group); }
static Key get_key(net::Reader& r) { Key k; k.instance = r.u32(); k.type = r.u32(); k.group = r.u32(); return k; }
static net::Writer packet(Kind kind, uint64_t run) { net::Writer w; w.u8(kind); put_run(w, run); return w; }
static bool send(uint8_t peer, const net::Writer& w)
{
    return C.sess && C.sess->send_reliable_to(peer, net::MSG_ADVENTURE, w);
}

static void fail(uint64_t run, const std::string& error, bool tell_host = false)
{
    ++g_generation; // an already queued outside-frame apply must not survive a failure
    g_error = error;
    g_preparing = false;
    g_prepared = false;
    g_applying = false;
    g_run = 0;
    g_bundle = {};
    g_out.clear();
    g_rx.reset();
    if (tell_host && C.sess && C.sess->connected()) {
        auto w = packet(Error, run);
        w.str8(error);
        send(0, w);
    }
    feed("adventure content: " + error);
    if (C.failed) C.failed(run, error);
}

static bool paths()
{
    if (!g_dir.empty()) return true;
    void* db = a_GetDb ? skg::cdecl_call<void*>(a_GetDb, 0x11AC199u) : nullptr;
    const wchar_t* p = db ? skg::vcall<const wchar_t*>(db, 0x28 / 4) : nullptr;
    if (!p || !skg::readable(p, 4)) return false;
    // GetLocation follows the verified EditorSaves path used by creations.cpp.
    std::wstring path;
    for (size_t i = 0; i < 1000; ++i) {
        if (!skg::readable(p + i, sizeof(wchar_t))) return false;
        if (!p[i]) break;
        path += p[i];
    }
    auto sep = path.find_last_of(L"\\/");
    if (sep == std::wstring::npos || path.size() >= 1000) return false;
    path.resize(sep);
    path += L"\\SporeCoop";
    CreateDirectoryW(path.c_str(), nullptr);
    path += L"\\adventure-assets";
    if (!CreateDirectoryW(path.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
    g_dir = std::move(path);
    return true;
}

static std::wstring cached_path(const std::string& hash, const wchar_t* extension)
{
    return g_dir + L"\\" + std::wstring(hash.begin(), hash.end()) + extension;
}

static bool read_file(const std::wstring& path, std::vector<uint8_t>& data, uint32_t limit)
{
    data.clear();
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(f, &size) && size.QuadPart >= 0 && (uint64_t)size.QuadPart <= limit;
    if (ok) {
        data.resize((size_t)size.QuadPart);
        DWORD got = 0;
        ok = ReadFile(f, data.data(), (DWORD)data.size(), &got, nullptr) && got == data.size();
    }
    CloseHandle(f);
    if (!ok) data.clear();
    return ok;
}

static bool matches_file(const std::wstring& path, const std::vector<uint8_t>& bytes)
{
    HANDLE f = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    LARGE_INTEGER size{};
    bool ok = GetFileSizeEx(f, &size) && size.QuadPart >= 0 && (uint64_t)size.QuadPart == bytes.size();
    uint8_t buffer[64 * 1024];
    for (size_t offset = 0; ok && offset < bytes.size();) {
        DWORD count = (DWORD)std::min<size_t>(sizeof buffer, bytes.size() - offset), got = 0;
        ok = ReadFile(f, buffer, count, &got, nullptr) && got == count &&
             memcmp(buffer, bytes.data() + offset, count) == 0;
        offset += count;
    }
    CloseHandle(f);
    return ok;
}

static bool write_file(const std::wstring& path, const std::vector<uint8_t>& bytes, bool immutable = false)
{
    // Engine databases survive a mod hot reload and may still hold this package
    // open. Content-addressed packages are immutable: validate/reuse identical
    // bytes instead of replacing a file already used by native resources.
    if (matches_file(path, bytes)) return true;
    if (immutable && GetFileAttributesW(path.c_str()) != INVALID_FILE_ATTRIBUTES) return false;
    std::wstring temporary = path + L".tmp";
    HANDLE f = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, 0, nullptr);
    if (f == INVALID_HANDLE_VALUE) return false;
    DWORD wrote = 0;
    bool ok = WriteFile(f, bytes.data(), (DWORD)bytes.size(), &wrote, nullptr) && wrote == bytes.size();
    CloseHandle(f);
    if (ok) {
        DWORD flags = MOVEFILE_WRITE_THROUGH | (immutable ? 0 : MOVEFILE_REPLACE_EXISTING);
        ok = MoveFileExW(temporary.c_str(), path.c_str(), flags) != 0;
        if (!ok && immutable && matches_file(path, bytes)) {
            DeleteFileW(temporary.c_str());
            ok = true; // another importer won the create race
        }
    }
    if (!ok) DeleteFileW(temporary.c_str());
    return ok;
}

static bool cache_read(const std::string& hash, std::vector<uint8_t>& bytes)
{
    return content::valid_hash(hash) && paths() && read_file(cached_path(hash, L".bin"), bytes, content::kMaxRecordBytes);
}

static void forget_database(void* db)
{
    void* rm = a_Rm ? *(void**)a_Rm : nullptr;
    if (rm && db) skg::vcall<bool>(rm, 0x50 / 4, false, db, 0);
    if (db) skg::vcall<int>((uint8_t*)db + 4, 1); // release our caller reference
}

static bool cache_bundle(const content::Bundle& b, std::string& error)
{
    if (!paths()) { error = "cannot find the isolated adventure content cache"; return false; }
    for (size_t i = 0; i < b.records.size(); ++i)
        if (!write_file(cached_path(b.manifest[i].hash, L".bin"), b.records[i].data)) {
            error = "cannot cache an adventure resource";
            return false;
        }
    return true;
}

static bool apply_bundle(const content::Bundle& b, std::string& error)
{
    if (!cache_bundle(b, error)) return false;
    if (auto it = g_databases.find(b.hash); it != g_databases.end()) {
        // A previous run may have registered another version under the same resource keys.
        // Promote this cached database again instead of letting that other version shadow it.
        void* rm = a_Rm ? *(void**)a_Rm : nullptr;
        if (rm && skg::vcall<bool>(rm, 0x50 / 4, false, it->second, 0) &&
            skg::vcall<bool>(rm, 0x50 / 4, true, it->second, ++g_priority)) return true;
        error = "cannot activate the cached adventure package database";
        return false;
    }
    std::wstring path = cached_path(b.hash, L".package");
    if (!write_file(path, dbpf::write(b.records), true)) { error = "cannot create or validate the immutable adventure package"; return false; }
    void* rm = a_Rm ? *(void**)a_Rm : nullptr;
    if (!rm || !a_New || !a_DbCtor) { error = "adventure package registration symbols are unavailable"; return false; }
    Key real{};
    void* resident = skg::vcall<void*>(rm, 0x30 / 4, &b.root, &real, (void*)nullptr);
    const wchar_t* location = resident ? skg::vcall<const wchar_t*>(resident, 0x28 / 4) : nullptr;
    std::wstring resident_path;
    for (size_t i = 0; location && i < 1000 && skg::readable(location + i, sizeof(wchar_t)); ++i) {
        if (!location[i]) break;
        resident_path += location[i];
    }
    if (!resident_path.empty() && resident_path.size() < 1000 && _wcsicmp(resident_path.c_str(), path.c_str()) == 0) {
        // Native resource-manager ownership can outlive this DLL's state on reload.
        // Reuse that database instead of constructing a second handle to its file.
        skg::vcall<int>((uint8_t*)resident + 4, 0);
        g_databases[b.hash] = resident;
        H->log(M, SK_INFO, "adventure content: reused resident package manifest %s", b.hash.c_str());
        return true;
    }
    void* db = skg::cdecl_call<void*>(a_New, 0x388u, "SporeCoop adventure", 0, 0, 0, 0);
    if (!db) { error = "cannot allocate the adventure package database"; return false; }
    skg::thiscall<void*>(a_DbCtor, db, path.c_str(), (void*)nullptr);
    skg::vcall<int>((uint8_t*)db + 4, 0); // caller reference, as in creations.cpp
    if (!skg::vcall<bool>(db, 0x04 / 4) || !skg::vcall<bool>(db, 0x18 / 4, 1, 6, false)) {
        skg::vcall<int>((uint8_t*)db + 4, 1);
        error = "cannot open the adventure package database";
        return false;
    }
    if (!skg::vcall<bool>(rm, 0x50 / 4, true, db, ++g_priority)) {
        skg::vcall<int>((uint8_t*)db + 4, 1);
        error = "cannot register the adventure package database";
        return false;
    }
    g_databases[b.hash] = db;
    H->log(M, SK_INFO, "adventure content: registered %u resources, %u bytes, manifest %s",
           (unsigned)b.records.size(), b.bytes, b.hash.c_str());
    return true;
}

struct ApplyTask { uint64_t generation, run; content::Bundle bundle; bool guest; };

static void apply_task(void* arg)
{
    std::unique_ptr<ApplyTask> task((ApplyTask*)arg);
    if (task->generation != g_generation || task->run != g_run) return;
    std::string error;
    bool cache_only = false;
    if ((C.registration_policy && !C.registration_policy(task->run, g_key, cache_only, error)) || cache_only != g_cache_only) {
        fail(task->run, error.empty() ? "adventure content registration ownership changed during preparation" : error, task->guest);
        return;
    }
    if (!(cache_only ? cache_bundle(task->bundle, error) : apply_bundle(task->bundle, error))) {
        fail(task->run, error, task->guest); return;
    }
    g_bundle = std::move(task->bundle);
    g_preparing = false;
    g_prepared = true;
    g_applying = false;
    if (task->guest) {
        auto w = packet(Ack, g_run); put_key(w, g_key); w.str8(g_bundle.hash); send(0, w);
    }
    if (C.prepared) C.prepared(g_run, g_key, g_bundle.hash);
}

struct CollectTask { uint64_t generation, run; Key key; };
static void collect_task(void* arg)
{
    std::unique_ptr<CollectTask> task((CollectTask*)arg);
    if (task->generation != g_generation || task->run != g_run) return;
    std::vector<dbpf::Record> records;
    std::string error;
    if (!C.collect) {
        fail(task->run, "dependency collection is unavailable for this game build");
        return;
    }
    content::Bundle bundle;
    if (!C.collect(task->key, records, error) || !content::make_bundle(task->key, std::move(records), bundle, error)) {
        fail(task->run, error.empty() ? "cannot collect the complete adventure dependencies" : error);
        return;
    }
    apply_task(new ApplyTask{task->generation, task->run, std::move(bundle), false});
}

bool init(const SkHost* h, SkMod* m, Ctx ctx)
{
    H = h; M = m; C = std::move(ctx);
    a_GetDb = H->sym("Resource::GetDatabaseByID");
    a_New = H->sym("Memory::operator_new_named");
    a_DbCtor = H->sym("Resource::DatabasePackedFile::ctor");
    a_Rm = H->sym("Resource::ResourceManager_ptr");
    return C.sess && a_GetDb && a_New && a_DbCtor && a_Rm;
}

void reset()
{
    ++g_generation;
    g_run = 0; g_key = {}; g_bundle = {}; g_rx.reset(); g_out.clear();
    g_preparing = g_prepared = g_applying = false;
    g_cache_only = false;
    g_error.clear();
}

bool prepare(uint64_t run, const Key& key)
{
    if (!H || !C.sess || !C.sess->connected() || !run || !key.instance || !key.type) return false;
    reset();
    g_run = run; g_key = key; g_preparing = true;
    std::string error;
    if (C.registration_policy && !C.registration_policy(run, key, g_cache_only, error)) {
        fail(run, error.empty() ? "adventure content registration ownership is unavailable" : error, !host());
        return false;
    }
    if (host()) H->run_outside_frame(collect_task, new CollectTask{g_generation, run, key});
    return true;
}

bool share_with(uint8_t peer)
{
    if (!host() || !g_prepared || !peer) return false;
    auto players = C.sess->players();
    if (std::none_of(players.begin(), players.end(), [peer](const auto& p) { return p.id == peer; })) return false;
    Out out; out.run = g_run; out.requested.assign(g_bundle.records.size(), false);
    g_out[peer] = std::move(out);
    return true;
}

void on_message(uint8_t from, net::Reader& r)
{
    uint8_t kind = r.u8();
    uint64_t run = get_run(r);
    if (!r.ok() || kind < Offer || kind > ManifestDone || !g_run || run != g_run) return;
    std::string error;
    if (host()) {
        auto it = g_out.find(from);
        if (it == g_out.end() || it->second.run != run) return;
        auto& out = it->second;
        if (kind == Need) {
            uint16_t index = r.u16();
            if (!r.ok() || r.left() || !out.manifest_done || out.need_done) return;
            if (index == kNeedDone) out.need_done = true;
            else if (index >= g_bundle.records.size()) { fail(run, "guest requested an invalid adventure resource"); }
            else if (!out.requested[index]) { out.requested[index] = true; out.needed.push_back(index); }
        } else if (kind == Ack) {
            Key key = get_key(r); std::string hash = r.str8();
            if (r.ok() && !r.left() && out.done && key == g_key && hash == g_bundle.hash) {
                g_out.erase(it);
                if (C.peer_prepared) C.peer_prepared(from, run, key, hash);
            }
        } else if (kind == Error) {
            std::string why = r.str8();
            if (r.ok() && !r.left()) fail(run, C.sess->name_of(from) + ": " + why);
        }
        return;
    }
    if (!C.sess || C.sess->role() != net::Role::Client || from != 0) return;
    if (kind == Offer) {
        Key key = get_key(r); uint16_t count = r.u16(); uint32_t bytes = r.u32(); std::string hash = r.str8();
        if (!r.ok() || r.left() || !(key == g_key)) return;
        if (g_applying || g_prepared) return;
        if (!g_rx.begin(run, key, count, bytes, hash, error)) fail(run, error, true);
    } else if (kind == Record) {
        uint16_t index = r.u16(); content::Entry e; e.key = get_key(r); e.size = r.u32(); e.hash = r.str8();
        if (!r.ok() || r.left()) { fail(run, "truncated adventure content manifest", true); return; }
        if (!g_rx.entry(index, e, error)) fail(run, error, true);
    } else if (kind == ManifestDone) {
        std::vector<uint16_t> missing;
        if (r.left() || !g_rx.manifest_done(cache_read, missing, error)) { fail(run, error, true); return; }
        for (auto index : missing) { auto w = packet(Need, run); w.u16(index); send(0, w); }
        auto w = packet(Need, run); w.u16(kNeedDone); send(0, w);
    } else if (kind == Chunk) {
        uint16_t index = r.u16(); uint32_t offset = r.u32(); uint16_t size = r.u16();
        if (!r.ok() || r.left() != size || !g_rx.chunk(index, offset, r.cur(), size, error))
            fail(run, error.empty() ? "truncated adventure content chunk" : error, true);
    } else if (kind == Done) {
        if (g_applying || g_prepared) return;
        content::Bundle bundle;
        if (r.left() || !g_rx.finish(bundle, error)) { fail(run, error, true); return; }
        g_rx.reset();
        g_applying = true;
        H->run_outside_frame(apply_task, new ApplyTask{g_generation, run, std::move(bundle), true});
    } else if (kind == Error) {
        std::string why = r.str8();
        if (r.ok() && !r.left()) fail(run, why);
    }
}

void tick(double)
{
    if (!host() || !g_prepared) return;
    auto players = C.sess->players();
    for (auto it = g_out.begin(); it != g_out.end();) {
        if (std::none_of(players.begin(), players.end(), [&](const auto& p) { return p.id == it->first; })) {
            it = g_out.erase(it); continue;
        }
        uint8_t peer = it->first; auto& out = it->second; ++it;
        int rtt = C.sess->rtt_of(peer);
        size_t window = rtt >= 0 && rtt < 40 ? 600 : 160;
        int budget = 100;
        while (budget-- && C.sess->pending_to(peer) < window && !out.done) {
            if (!out.offered) {
                auto w = packet(Offer, out.run); put_key(w, g_key); w.u16((uint16_t)g_bundle.records.size());
                w.u32(g_bundle.bytes); w.str8(g_bundle.hash); send(peer, w); out.offered = true;
            } else if (out.manifest < g_bundle.manifest.size()) {
                uint16_t index = out.manifest++; const auto& e = g_bundle.manifest[index];
                auto w = packet(Record, out.run); w.u16(index); put_key(w, e.key); w.u32(e.size); w.str8(e.hash); send(peer, w);
            } else if (!out.manifest_done) {
                send(peer, packet(ManifestDone, out.run)); out.manifest_done = true;
            } else if (!out.need_done) break;
            else if (out.resource >= out.needed.size()) {
                send(peer, packet(Done, out.run)); out.done = true;
            } else {
                uint16_t index = out.needed[out.resource]; const auto& bytes = g_bundle.records[index].data;
                uint16_t size = (uint16_t)std::min<size_t>(content::kChunkBytes, bytes.size() - out.offset);
                if (size) {
                    auto w = packet(Chunk, out.run); w.u16(index); w.u32(out.offset); w.u16(size);
                    w.raw(bytes.data() + out.offset, size); send(peer, w); out.offset += size;
                }
                if (out.offset >= bytes.size()) { ++out.resource; out.offset = 0; }
            }
        }
    }
}

void release()
{
    reset();
    for (const auto& [hash, db] : g_databases) forget_database(db);
    g_databases.clear();
}

bool register_records(const std::vector<dbpf::Record>& records, std::string& error)
{
    if (records.empty()) return true; // a creation from a shared base-game package
    content::Bundle bundle;
    if (!content::make_bundle(records.front().key, records, bundle, error)) return false;
    return apply_bundle(bundle, error);
}

uint8_t progress()
{
    return g_prepared ? 100 : g_rx.bytes() ? uint8_t(std::min<uint64_t>(99, uint64_t(g_rx.received()) * 100 / g_rx.bytes())) : 0;
}

std::string status()
{
    char b[256];
    snprintf(b, sizeof b, "adventure content: run=%llu %s resources=%u bytes=%u receive=%u/%u peers=%u registered=%u cache_only=%u",
             (unsigned long long)g_run, g_prepared ? "prepared" : g_preparing ? "preparing" : "idle",
             (unsigned)g_bundle.records.size(), g_bundle.bytes, g_rx.received(), g_rx.bytes(),
             (unsigned)g_out.size(), (unsigned)g_databases.size(), unsigned(g_cache_only));
    return std::string(b) + (g_error.empty() ? "" : " error=" + g_error) + "\n";
}

} // namespace adventure_assets
