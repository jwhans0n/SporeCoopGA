#include "adventure_captains.h"

#include <algorithm>
#include <array>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <tuple>
#include <utility>
#include <vector>

#include "adventure.h"
#include "adventure_assets.h"
#include "adventure_collect.h"
#include "net/adventure_assets.h"
#include "sporekit_game.hpp"
#include "../../updater/sha256.h"

namespace adventure_captains {
namespace {
namespace content = net::adventure_content;
enum Kind : uint8_t { Offer = 192, Entry, ManifestDone, Chunk, Done, Ack, Ready, Error };
constexpr uint32_t kCaptainModel = 0x4178B8E8, kCaptainShopper = 0x5D4875D0;
constexpr uint32_t kCaptainBytes = 16u << 20;
constexpr uint16_t kCaptainRecords = 128;

const SkHost* H;
SkMod* M;
net::Session* S;
uintptr_t a_Ctor, a_Show, a_Dtor, a_ModelType, a_Browser, a_Rm;
bool g_available, g_picking;
uint64_t g_run, g_generation, g_next;
Selected g_selected;
std::string g_error;

struct Transfer {
    uint64_t run = 0, serial = 0;
    Key original, mapped;
    std::shared_ptr<content::Bundle> bundle;
    std::set<uint8_t> acknowledgements;
    bool applied = false, ready = false;
};
struct Receive {
    uint64_t run = 0, serial = 0;
    Key original;
    content::Receiver receiver;
    bool installing = false;
};
struct Outgoing {
    uint8_t peer = 0, owner = 0;
    uint64_t run = 0, serial = 0;
    Key original;
    std::shared_ptr<content::Bundle> bundle;
    uint8_t stage = 0;
    uint16_t record = 0;
    uint32_t offset = 0;
};
std::map<uint8_t, Transfer> g_transfers;
std::map<uint8_t, Receive> g_receivers;
std::vector<Outgoing> g_outgoing;
// Packages remain registered while native resources refer to them, including across runs.
std::map<std::tuple<uint32_t, uint32_t, uint32_t>, std::string> g_installed;

bool host() { return S && S->role() == net::Role::Host && S->connected(); }
uint8_t mine() { return S ? S->my_id() : 0; }
bool current(uint64_t run)
{
    const auto* d = adventure::current();
    return d && run && run == d->run_id && run == g_run;
}
void put_run(net::Writer& w, uint64_t value) { w.u32(uint32_t(value)); w.u32(uint32_t(value >> 32)); }
uint64_t get_run(net::Reader& r) { uint64_t lo = r.u32(); return lo | (uint64_t(r.u32()) << 32); }
void put_key(net::Writer& w, const Key& k) { w.u32(k.instance); w.u32(k.type); w.u32(k.group); }
Key get_key(net::Reader& r) { return {r.u32(), r.u32(), r.u32()}; }
bool finished(net::Reader& r) { return r.ok() && !r.left(); }

void outside(std::function<void()> action)
{
    struct Task { uint64_t generation; std::function<void()> action; };
    auto* task = new Task{g_generation, std::move(action)};
    H->run_outside_frame([](void* opaque) {
        std::unique_ptr<Task> task(static_cast<Task*>(opaque));
        if (task->generation == g_generation) task->action();
    }, task);
}

bool send(uint8_t peer, const net::Writer& packet)
{
    return S && S->connected() && S->send_reliable_to(peer, net::MSG_ADVENTURE, packet);
}
net::Writer packet(Kind kind, uint8_t owner, uint64_t run, uint64_t serial)
{
    net::Writer w;
    w.u8(kind); w.u8(owner); put_run(w, run); put_run(w, serial);
    return w;
}

void fail(uint8_t owner, uint64_t run, uint64_t serial, const std::string& error, bool announce = true)
{
    if (!current(run)) return;
    g_error = error;
    H->log(M, SK_WARN, "adventure captain: %s", error.c_str());
    if (announce) {
        auto w = packet(Error, owner, run, serial);
        w.str8(error);
        if (host()) S->broadcast_reliable(net::MSG_ADVENTURE, w);
        else send(0, w);
    }
    Selected callback = std::exchange(g_selected, {});
    g_picking = false;
    adventure::cancel("Captain: " + error);
    if (callback) callback({});
}

std::string builtin_hash(const Key& key)
{
    net::Writer w; put_key(w, key);
    return upd::sha256_hex(w.data);
}

void begin_run(uint64_t run)
{
    if (g_run == run) return;
    ++g_generation;
    g_run = run;
    g_selected = {};
    g_picking = false;
    g_transfers.clear(); g_receivers.clear(); g_outgoing.clear();
    g_error.clear();
}

bool captain(const Key& key)
{
    return key.instance && key.type == 0x2B978C46 &&
           skg::cdecl_call<uint32_t>(a_ModelType, &key) == kCaptainModel;
}

Key mapped_key(uint8_t owner, const Key& original, const std::string& hash)
{
    net::Writer seed; seed.u8(owner); seed.raw(hash.data(), hash.size());
    std::string digest = upd::sha256_hex(seed.data);
    uint32_t low = 0;
    for (size_t i = 0; i < 6; i++) low = low * 16 + (digest[i] <= '9' ? digest[i] - '0' : digest[i] - 'a' + 10);
    return {0x7D000000u | low, original.type, original.group};
}

bool record_allowed(const Key& original, const Key& key)
{
    if (key.instance != original.instance ||
        (key.group != original.group && key.group != original.group + 1 &&
         key.group != ((original.group & 0xFFFF0000u) | 0xE500u))) return false;
    switch (key.type) {
    case 0x2B978C46: case 0x1A99B06B: case 0x030BDEE3: case 0x02D5C9AF:
    case 0x2F7D0004: case 0x0F43029A: case 0x00B1B104: return true;
    default: return false;
    }
}

bool install(uint8_t owner, const Key& original, const content::Bundle& bundle, Key& use, std::string& error)
{
    if (bundle.records.empty()) {
        if (bundle.hash != builtin_hash(original) || !captain(original)) {
            error = "a built-in captain is missing or differs on this machine";
            return false;
        }
        use = original;
        return true;
    }
    use = mapped_key(owner, original, bundle.hash);
    auto id = std::tuple{use.instance, use.type, use.group};
    auto existing = g_installed.find(id);
    if (existing != g_installed.end()) {
        if (existing->second == bundle.hash) return true;
        error = "captain model namespace collision";
        return false;
    }
    std::vector<dbpf::Record> records = bundle.records;
    for (auto& record : records) {
        if (!record_allowed(original, record.key)) { error = "captain transfer contains an unrelated resource"; return false; }
        record.key.instance = use.instance;
    }
    void* rm = a_Rm ? *(void**)a_Rm : nullptr;
    Key real;
    if (rm && skg::vcall<void*>(rm, 0x30 / 4, &use, &real, (void*)nullptr)) {
        // Native package databases outlive a DLL reload. Reuse an occupied key
        // only after reading its complete creation back and matching every byte.
        std::vector<dbpf::Record> loaded;
        std::string read_error;
        if (adventure_collect::collect_creation(use, loaded, read_error) && loaded.size() == records.size() && captain(use)) {
            using Id = std::tuple<uint32_t, uint32_t, uint32_t>;
            std::map<Id, std::vector<uint8_t>> expected, actual;
            for (const auto& record : records)
                expected.emplace(Id{record.key.instance, record.key.type, record.key.group}, record.data);
            for (const auto& record : loaded)
                actual.emplace(Id{record.key.instance, record.key.type, record.key.group}, record.data);
            if (expected.size() == records.size() && actual == expected) {
                g_installed[id] = bundle.hash;
                return true;
            }
        }
        error = "captain model namespace is already occupied by another resource";
        return false;
    }
    if (!adventure_assets::register_records(records, error)) return false;
    if (!captain(use)) { error = "the imported model is not a Galactic Adventures captain"; return false; }
    g_installed[id] = bundle.hash;
    return true;
}

void queue_bundle(uint8_t peer, uint8_t owner, const Transfer& transfer)
{
    g_outgoing.push_back({peer, owner, transfer.run, transfer.serial, transfer.original, transfer.bundle});
}

void check_ready(uint8_t owner)
{
    auto it = g_transfers.find(owner);
    const auto* d = adventure::current();
    if (!host() || it == g_transfers.end() || !d) return;
    auto& t = it->second;
    if (t.ready || !t.applied || !current(t.run)) return;
    for (uint8_t player : d->roster)
        if (!t.acknowledgements.count(player)) return;
    t.ready = true;
    auto w = packet(Ready, owner, t.run, t.serial);
    w.str8(t.bundle->hash);
    S->broadcast_reliable(net::MSG_ADVENTURE, w);
    if (owner == mine()) {
        Selected callback = std::exchange(g_selected, {});
        if (callback) callback(t.original);
    }
}

void received(uint8_t owner, uint64_t run, uint64_t serial, const Key& original, content::Bundle bundle)
{
    outside([owner, run, serial, original, bundle = std::move(bundle)]() mutable {
        if (!current(run)) return;
        auto rx = g_receivers.find(owner);
        if (rx == g_receivers.end() || rx->second.serial != serial) return;
        Key use;
        std::string error;
        if (!install(owner, original, bundle, use, error)) { fail(owner, run, serial, error); return; }
        Transfer t;
        t.run = run; t.serial = serial; t.original = original; t.mapped = use;
        t.bundle = std::make_shared<content::Bundle>(std::move(bundle));
        t.applied = true;
        t.acknowledgements = {owner, mine()};
        g_transfers[owner] = std::move(t);
        auto& transfer = g_transfers[owner];
        if (host()) {
            for (uint8_t peer : adventure::current()->roster)
                if (peer && peer != owner) queue_bundle(peer, owner, transfer);
            check_ready(owner);
        } else {
            auto w = packet(Ack, owner, run, serial);
            w.str8(transfer.bundle->hash);
            send(0, w);
        }
    });
}

struct Listener {
    void** vtable;
    uint64_t generation, run;
};
std::vector<std::unique_ptr<Listener>> g_listeners;
void SK_THISCALL selected(Listener* listener, const Key* key)
{
    if (listener->generation != g_generation || !current(listener->run) || !g_picking) return;
    g_picking = false;
    if (!key || !skg::readable(key, sizeof(Key)) || !key->instance) {
        fail(mine(), g_run, 0, "captain selection was cancelled");
        return;
    }
    Key choice = *key;
    Selected callback = std::exchange(g_selected, {});
    publish(choice, std::move(callback));
}
void* listener_vtable[] = {(void*)selected};

void pump(Outgoing& out)
{
    if (!current(out.run) || S->pending_to(out.peer) > 24) return;
    auto w = packet(Offer, out.owner, out.run, out.serial);
    auto& bundle = *out.bundle;
    if (out.stage == 0) {
        put_key(w, out.original); put_key(w, bundle.root);
        w.u16(uint16_t(bundle.records.size())); w.u32(bundle.bytes); w.str8(bundle.hash);
        if (send(out.peer, w)) out.stage = bundle.records.empty() ? 5 : 1;
    } else if (out.stage == 1) {
        w = packet(Entry, out.owner, out.run, out.serial);
        const auto& entry = bundle.manifest[out.record];
        w.u16(out.record); put_key(w, entry.key); w.u32(entry.size); w.str8(entry.hash);
        if (send(out.peer, w) && ++out.record == bundle.records.size()) { out.stage = 2; out.record = 0; }
    } else if (out.stage == 2) {
        w = packet(ManifestDone, out.owner, out.run, out.serial);
        if (send(out.peer, w)) out.stage = 3;
    } else if (out.stage == 3) {
        const auto& record = bundle.records[out.record];
        if (out.offset == record.data.size()) {
            out.offset = 0;
            if (++out.record == bundle.records.size()) out.stage = 4;
            return;
        }
        uint16_t count = uint16_t(std::min<size_t>(content::kChunkBytes, record.data.size() - out.offset));
        w = packet(Chunk, out.owner, out.run, out.serial);
        w.u16(out.record); w.u32(out.offset); w.u16(count); w.raw(record.data.data() + out.offset, count);
        if (send(out.peer, w)) out.offset += count;
    } else if (out.stage == 4) {
        w = packet(Done, out.owner, out.run, out.serial);
        if (send(out.peer, w)) out.stage = 5;
    }
}
} // namespace

bool init(const SkHost* api, SkMod* mod, net::Session* session)
{
    H = api; M = mod; S = session;
    g_installed.clear(); // native records are revalidated when a fresh module initializes
    a_Ctor = H->sym("Sporepedia::ShopperRequest::ctor");
    a_Show = H->sym("Sporepedia::ShopperRequest::Show");
    a_Dtor = H->sym("Sporepedia::ShopperRequest::dtor");
    a_ModelType = H->sym("Sporepedia::GetAssetModelType");
    a_Browser = H->sym("Sporepedia::AssetBrowser::Get");
    a_Rm = H->sym("Resource::ResourceManager_ptr");
    g_available = S && H->run_outside_frame && a_Ctor && a_Show && a_Dtor && a_ModelType && a_Browser && a_Rm &&
        adventure_collect::available() && std::string(H->exe_sha256()) == "dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37";
    return g_available;
}

bool available() { return g_available; }

bool publish(const Key& key, Selected callback)
{
    const auto* d = adventure::current();
    if (!available() || !d || !adventure::participating(mine()) || !callback) return false;
    begin_run(d->run_id);
    if (g_selected) return false;
    g_selected = std::move(callback);
    const uint64_t run = g_run, serial = ++g_next;
    outside([key, run, serial] {
        if (!current(run)) return;
        if (!captain(key)) { fail(mine(), run, serial, "the selection is not an outfitted Galactic Adventures captain"); return; }
        std::vector<dbpf::Record> records;
        std::string error;
        if (!adventure_collect::collect_creation(key, records, error)) { fail(mine(), run, serial, error); return; }
        content::Bundle bundle;
        if (records.empty()) {
            bundle.root = key;
            bundle.hash = builtin_hash(key);
        } else {
            for (const auto& record : records)
                if (!record_allowed(key, record.key)) { fail(mine(), run, serial, "captain gear collection contains unsupported dependencies"); return; }
            Key root = records.front().key;
            if (!content::make_bundle(root, std::move(records), bundle, error) ||
                bundle.bytes > kCaptainBytes || bundle.records.size() > kCaptainRecords) {
                fail(mine(), run, serial, error.empty() ? "captain model exceeds transfer limits" : error); return;
            }
        }
        Transfer t;
        t.run = run; t.serial = serial; t.original = t.mapped = key;
        t.bundle = std::make_shared<content::Bundle>(std::move(bundle));
        t.applied = true; t.acknowledgements.insert(mine());
        g_transfers[mine()] = std::move(t);
        if (host()) {
            for (uint8_t peer : adventure::current()->roster)
                if (peer) queue_bundle(peer, mine(), g_transfers[mine()]);
            check_ready(mine());
        } else queue_bundle(0, mine(), g_transfers[mine()]);
    });
    return true;
}

bool choose(const Key& locked, Selected callback)
{
    const auto* d = adventure::current();
    if (!available() || !d || !callback || !adventure::participating(mine())) return false;
    begin_run(d->run_id);
    if (g_selected || g_picking) return false;
    if (locked.instance && locked.type) {
        Transfer t; t.run = g_run; t.original = t.mapped = locked; t.applied = t.ready = true;
        g_transfers[mine()] = std::move(t);
        callback(locked);
        return true;
    }
    g_selected = std::move(callback);
    g_picking = true;
    outside([] {
        if (!current(g_run) || !g_picking) return;
        auto listener = std::make_unique<Listener>();
        listener->vtable = listener_vtable; listener->generation = g_generation; listener->run = g_run;
        Listener* pointer = listener.get();
        g_listeners.push_back(std::move(listener));
        // Native ctor/dtor and a one-method MSVC listener avoid copying EASTL or C++ class ABIs.
        alignas(4) std::array<uint8_t, 0x48> request{};
        skg::thiscall<void>(a_Ctor, request.data());
        skg::field<uint32_t>(request.data(), 0) = kCaptainShopper;
        skg::field<void*>(request.data(), 0x38) = pointer;
        skg::cdecl_call<void>(a_Show, request.data());
        skg::thiscall<void>(a_Dtor, request.data());
    });
    return true;
}

void on_message(uint8_t from, net::Reader& r)
{
    if (!available() || !S->connected() || r.left() < 18) return;
    uint8_t kind = r.u8(), owner = r.u8();
    uint64_t run = get_run(r), serial = get_run(r);
    const auto* d = adventure::current();
    if (!d || run != d->run_id || !serial || kind < Offer || kind > Error ||
        !adventure::participating(owner) || (host() ? !adventure::participating(from) : from != 0)) return;
    begin_run(run);
    if (kind == Error) {
        std::string error = r.str8();
        if (finished(r)) fail(owner, run, serial, error, host());
        return;
    }
    if (kind == Ack || kind == Ready) {
        std::string hash = r.str8();
        auto it = g_transfers.find(owner);
        if (!finished(r) || it == g_transfers.end() || !it->second.bundle || it->second.serial != serial ||
            it->second.bundle->hash != hash) return;
        if (kind == Ack && host() && from != owner && it->second.applied) {
            it->second.acknowledgements.insert(from);
            check_ready(owner);
        } else if (kind == Ready && !host() && owner == mine() && !it->second.ready) {
            it->second.ready = true;
            Selected callback = std::exchange(g_selected, {});
            if (callback) callback(it->second.original);
        }
        return;
    }
    if ((host() && owner != from) || (!host() && owner == mine())) return;
    if (kind == Offer) {
        Key original = get_key(r), root = get_key(r);
        uint16_t count = r.u16(); uint32_t bytes = r.u32(); std::string hash = r.str8();
        if (!finished(r) || !original.instance || original.type != 0x2B978C46 || count > kCaptainRecords ||
            bytes > kCaptainBytes || !content::valid_hash(hash)) return;
        auto existing = g_receivers.find(owner);
        if (existing != g_receivers.end() && serial <= existing->second.serial) return;
        Receive rx;
        rx.run = run; rx.serial = serial; rx.original = original;
        std::string error;
        if (!count) {
            if (bytes || !(root == original) || hash != builtin_hash(original)) return;
            rx.installing = true;
            g_receivers[owner] = std::move(rx);
            content::Bundle bundle; bundle.root = root; bundle.hash = hash;
            received(owner, run, serial, original, std::move(bundle));
        } else if (record_allowed(original, root) && rx.receiver.begin(run, root, count, bytes, hash, error))
            g_receivers[owner] = std::move(rx);
        return;
    }
    auto it = g_receivers.find(owner);
    if (it == g_receivers.end() || it->second.serial != serial || it->second.installing) return;
    auto& rx = it->second;
    std::string error;
    if (kind == Entry) {
        uint16_t index = r.u16(); content::Entry entry;
        entry.key = get_key(r); entry.size = r.u32(); entry.hash = r.str8();
        if (finished(r) && record_allowed(rx.original, entry.key) && !rx.receiver.entry(index, entry, error))
            fail(owner, run, serial, error);
    } else if (kind == ManifestDone && finished(r)) {
        std::vector<uint16_t> missing;
        if (!rx.receiver.manifest_done({}, missing, error)) fail(owner, run, serial, error);
    } else if (kind == Chunk) {
        uint16_t index = r.u16(); uint32_t offset = r.u32(); uint16_t size = r.u16();
        if (!r.ok() || r.left() != size || size > content::kChunkBytes) return;
        if (!rx.receiver.chunk(index, offset, r.cur(), size, error)) fail(owner, run, serial, error);
    } else if (kind == Done && finished(r)) {
        content::Bundle bundle;
        if (!rx.receiver.finish(bundle, error)) { fail(owner, run, serial, error); return; }
        rx.installing = true;
        received(owner, run, serial, rx.original, std::move(bundle));
    }
}

void tick(double)
{
    const auto* d = adventure::current();
    if (!d || !S || !S->connected()) return;
    begin_run(d->run_id);
    for (auto& out : g_outgoing)
        for (unsigned i = 0; i < 4 && out.stage < 5; i++) pump(out);
    g_outgoing.erase(std::remove_if(g_outgoing.begin(), g_outgoing.end(), [](const auto& out) { return out.stage == 5; }), g_outgoing.end());
}

void reset()
{
    ++g_generation;
    // The browser keeps an unmanaged listener pointer after Show. Detach ours before unload.
    void* browser = a_Browser ? skg::cdecl_call<void*>(a_Browser) : nullptr;
    if (browser && skg::readable(browser, 0x1A8)) {
        void*& listener = skg::field<void*>(browser, 0x1A0);
        for (const auto& owned : g_listeners)
            if (listener == owned.get()) listener = nullptr;
    }
    g_selected = {};
    g_picking = false;
    g_run = g_next = 0;
    g_transfers.clear(); g_receivers.clear(); g_outgoing.clear();
    g_error.clear();
    g_listeners.clear();
}

Key key_for(uint8_t player)
{
    auto it = g_transfers.find(player);
    return it != g_transfers.end() && it->second.applied ? it->second.mapped : Key{};
}

std::string status()
{
    if (!g_available) return "Native captain selection is unavailable for this game build";
    if (!g_error.empty()) return g_error;
    if (g_picking) return "Choose your Galactic Adventures captain";
    if (g_selected) return "Sharing captain model and equipment with the party";
    return "Captain models ready";
}

} // namespace adventure_captains
