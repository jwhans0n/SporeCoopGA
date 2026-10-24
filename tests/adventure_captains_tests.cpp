// Win32 native-ABI tests with fake game functions. This never launches Spore.
#include "adventure_captains.h"
#include "adventure.h"
#include "adventure_assets.h"
#include "adventure_collect.h"
#include "net/adventure_assets.h"
#include "sporekit_game.hpp"

#include <cstdarg>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <map>
#include <tuple>
#include <utility>

static int failures;
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)
using Key = dbpf::Key;
static constexpr uint32_t crt = 0x2B978C46, bem = 0x1A99B06B, gear = 0x0F43029A, group = 0x40626200;
static ga::Descriptor descriptor;
static bool g_active = true;
static std::string cancelled;
static unsigned imports, choices, effect_stops, effect_releases;
static void SK_THISCALL stop_effect(void*, bool immediate) { CHECK(immediate); effect_stops++; }
static int SK_THISCALL release_effect(void*) { effect_releases++; return 0; }
static void* effect_vtable[4];
static void** preview_effect = effect_vtable;
static void** launch_effect = effect_vtable;
static uint8_t frontend[0x140], wizard_data[0x178], node[0x18];
static void* frontend_pointer = frontend;
static uintptr_t wizard_vtable = 0x12345678;
static std::map<std::tuple<uint32_t, uint32_t, uint32_t>, std::vector<uint8_t>> records;
static auto id(const Key& key) { return std::tuple{key.instance, key.type, key.group}; }
static std::vector<std::pair<SkTaskFn, void*>> tasks;
static void queue(SkTaskFn fn, void* user) { tasks.push_back({fn, user}); }
static void drain()
{
    while (!tasks.empty()) {
        auto pending = std::move(tasks); tasks.clear();
        for (auto [fn, user] : pending) fn(user);
    }
}

namespace adventure {
const ga::Descriptor* current() { return g_active ? &descriptor : nullptr; }
bool participating(uint8_t player) { return g_active && std::find(descriptor.roster.begin(), descriptor.roster.end(), player) != descriptor.roster.end(); }
bool cancel(const std::string& why) { cancelled = why; g_active = false; return true; }
} // namespace adventure
namespace adventure_collect {
bool available() { return true; }
bool collect_creation(const Key& key, std::vector<dbpf::Record>& out, std::string&)
{
    if ((key.instance & 0xFF000000) == 0x7D000000) {
        out.clear();
        for (const auto& [identity, bytes] : records) {
            auto [instance, type, record_group] = identity;
            if (instance == key.instance) out.push_back({{instance, type, record_group}, bytes});
        }
        return !out.empty();
    }
    out = {{key, {0xA1}}, {{key.instance, bem, key.group}, {0xB2}}, {{key.instance, gear, key.group}, {0xC3}}};
    return true;
}
} // namespace adventure_collect
namespace adventure_assets {
bool register_records(const std::vector<dbpf::Record>& incoming, std::string&)
{
    imports++;
    for (const auto& record : incoming) records[id(record.key)] = record.data;
    return true;
}
} // namespace adventure_assets

static uint8_t browser[0x200];
static void* SK_THISCALL find_record(void*, const Key* key, Key* real, void*)
{
    if (!records.count(id(*key))) return nullptr;
    *real = *key;
    return browser; // existence check only, no database methods are called
}
static void* manager_vtable[32];
static void** manager = manager_vtable;
static void* manager_pointer = &manager;
static void SK_THISCALL start_wizard(void* wizard)
{
    CHECK(wizard == wizard_data); choices++;
    skg::field<int>(wizard, 0x20) = 1;
}
static void SK_THISCALL select_adventure(void* wizard, const Key* key)
{
    skg::field<Key>(wizard, 0x8C) = *key;
}
static void SK_THISCALL next_step(void* wizard) { skg::field<int>(wizard, 0x20) = 2; }
static void SK_THISCALL visible(void*, bool) {}
static void SK_THISCALL reset_wizard(void*) {}
static void galaxy_reset() {}
static bool SK_THISCALL layout_visible(void*, bool show) { CHECK(show); return true; }
static void SK_THISCALL galaxy_effect(void*, bool) {}
static void SK_THISCALL browser_show(void*, bool show, bool, bool notify) { CHECK(!show && !notify); }
static void SK_THISCALL frontend_mode(void*, int, bool) {}
static void choose_wizard()
{
    drain();
    adventure_captains::tick(0); drain();
    adventure_captains::tick(0); drain();
}
static uint32_t model(const Key* key)
{
    return key->type == crt && (key->instance == 100 || key->instance == 200 || records.count(id(*key)))
        ? 0x4178B8E8 : 0;
}
static void* get_browser() { return browser; }
static uintptr_t symbol(const char* name)
{
    if (!std::strcmp(name, "SporeCoop::Adventure::FrontEnd_ptr")) return (uintptr_t)&frontend_pointer;
    if (!std::strcmp(name, "SporeCoop::Adventure::CaptainWizard_vtable")) return wizard_vtable;
    if (!std::strcmp(name, "SporeCoop::Adventure::CaptainWizardStart")) return (uintptr_t)start_wizard;
    if (!std::strcmp(name, "SporeCoop::Adventure::CaptainWizardSelectAdventure")) return (uintptr_t)select_adventure;
    if (!std::strcmp(name, "SporeCoop::Adventure::CaptainWizardNext")) return (uintptr_t)next_step;
    if (!std::strcmp(name, "SporeCoop::Adventure::CaptainWizardShow")) return (uintptr_t)visible;
    if (!std::strcmp(name, "SporeCoop::Adventure::CaptainWizardReset")) return (uintptr_t)reset_wizard;
    if (!std::strcmp(name, "SporeCoop::Adventure::FrontEndRequestMode")) return (uintptr_t)frontend_mode;
    if (!std::strcmp(name, "Sporepedia::GetAssetModelType")) return (uintptr_t)model;
    if (!std::strcmp(name, "Sporepedia::AssetBrowser::Get")) return (uintptr_t)get_browser;
    if (!std::strcmp(name, "UTFWin::UILayout::SetVisible")) return (uintptr_t)layout_visible;
    if (!std::strcmp(name, "SporeCoop::Adventure::GalaxyViewReset")) return (uintptr_t)galaxy_reset;
    if (!std::strcmp(name, "SporeCoop::Adventure::GalaxyIdleEffect")) return (uintptr_t)galaxy_effect;
    if (!std::strcmp(name, "SporeCoop::Adventure::AssetBrowserShow")) return (uintptr_t)browser_show;
    if (!std::strcmp(name, "Resource::ResourceManager_ptr")) return (uintptr_t)&manager_pointer;
    return 0;
}
static const char* hash() { return "dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37"; }
static void log(SkMod*, int, const char* format, ...)
{
    va_list args; va_start(args, format); std::vprintf(format, args); va_end(args); std::printf("\n");
}
static void put_run(net::Writer& w, uint64_t run) { w.u32(uint32_t(run)); w.u32(uint32_t(run >> 32)); }
static void put_key(net::Writer& w, const Key& key) { w.u32(key.instance); w.u32(key.type); w.u32(key.group); }
static net::Writer packet(uint8_t kind, uint8_t owner, uint64_t run, uint64_t serial)
{
    net::Writer w; w.u8(kind); w.u8(owner); put_run(w, run); put_run(w, serial); return w;
}
static void incoming(uint8_t from, const net::Writer& w)
{
    net::Reader r(w.data.data(), w.size()); adventure_captains::on_message(from, r);
}
static void receive(uint8_t from, uint8_t owner, uint64_t serial, const Key& original,
                    const net::adventure_content::Bundle& bundle)
{
    auto offer = packet(192, owner, descriptor.run_id, serial);
    put_key(offer, original); put_key(offer, bundle.root); offer.u16(uint16_t(bundle.records.size()));
    offer.u32(bundle.bytes); offer.str8(bundle.hash); incoming(from, offer);
    for (size_t i = 0; i < bundle.manifest.size(); i++) {
        auto entry = packet(193, owner, descriptor.run_id, serial);
        entry.u16(uint16_t(i)); put_key(entry, bundle.manifest[i].key);
        entry.u32(bundle.manifest[i].size); entry.str8(bundle.manifest[i].hash); incoming(from, entry);
    }
    incoming(from, packet(194, owner, descriptor.run_id, serial));
    for (size_t i = 0; i < bundle.records.size(); i++) {
        auto chunk = packet(195, owner, descriptor.run_id, serial);
        chunk.u16(uint16_t(i)); chunk.u32(0); chunk.u16(uint16_t(bundle.records[i].data.size()));
        chunk.raw(bundle.records[i].data.data(), bundle.records[i].data.size()); incoming(from, chunk);
    }
    incoming(from, packet(196, owner, descriptor.run_id, serial));
    drain();
}

int main()
{
    SkHost api{};
    api.sym = symbol; api.run_outside_frame = queue; api.exe_sha256 = hash; api.log = log;
    skg::field<void*>(frontend, 0x130) = node;
    skg::field<int>(node, 0x10) = 13;
    skg::field<void*>(node, 0x14) = wizard_data;
    skg::field<uintptr_t>(wizard_data, 0) = wizard_vtable;
    descriptor.adventure = {123, 0x366A930D, 0x408A0000};
    manager_vtable[0x30 / 4] = (void*)find_record;
    net::Session session;
    std::string error;
    CHECK(session.host(0, "Captain test", error));
    descriptor.run_id = 1;
    descriptor.roster = {0};
    CHECK(adventure_captains::init(&api, nullptr, &session));
    CHECK(adventure_captains::available());
    Key original{100, crt, group}, chosen;
    unsigned callbacks = 0;
    CHECK(adventure_captains::publish(original, [&](const Key& key) { chosen = key; callbacks++; }));
    CHECK(callbacks == 0);
    drain();
    CHECK(callbacks == 1 && chosen == original && adventure_captains::key_for(0) == original);
    CHECK(imports == 0); // local native captain and progression key stay untouched

    adventure_captains::reset();
    descriptor.run_id = 2;
    CHECK(adventure_captains::choose({}, [&](const Key& key) { chosen = key; callbacks++; }));
    CHECK(choices == 0); choose_wizard();
    CHECK(choices == 1 && callbacks == 1); // selecting a body has not saved a captain
    CHECK(!adventure_captains::complete_native({999, crt, group}, original));
    CHECK(!adventure_captains::complete_native(descriptor.adventure, original));
    skg::field<int>(wizard_data, 0x20) = 0; // native conversion and save completed
    CHECK(adventure_captains::complete_native(descriptor.adventure, original)); drain();
    CHECK(callbacks == 2 && chosen == original);
    adventure_captains::close_picker(); adventure_captains::reset();

    // Remote captains retain all collected gear records without shadowing our local key.
    descriptor.run_id = 3; descriptor.roster = {0, 1};
    Key remote{200, crt, group};
    records[id(remote)] = {0xEE};
    std::vector<dbpf::Record> body = {{remote, {0x12}}, {{200, bem, group}, {0x34}}, {{200, gear, group}, {0x56}}};
    net::adventure_content::Bundle bundle;
    CHECK(net::adventure_content::make_bundle(remote, body, bundle, error));
    receive(1, 1, 10, remote, bundle);
    auto mapped = adventure_captains::key_for(1);
    CHECK(mapped.instance != remote.instance && (mapped.instance & 0xFF000000) == 0x7D000000);
    CHECK(mapped.type == remote.type && mapped.group == remote.group);
    CHECK(records[id(remote)] == std::vector<uint8_t>{0xEE});
    CHECK(records[id({mapped.instance, gear, group})] == std::vector<uint8_t>{0x56});
    CHECK(imports == 1);
    receive(1, 1, 10, remote, bundle);
    receive(1, 1, 9, remote, bundle);
    receive(0, 1, 11, remote, bundle); // guest owner must match actual sender
    CHECK(imports == 1 && g_active);

    // Native databases survive a module reload. An identical occupied namespace
    // is reusable only after a complete byte comparison, without re-registering.
    adventure_captains::reset();
    CHECK(adventure_captains::init(&api, nullptr, &session));
    descriptor.run_id = 30;
    receive(1, 1, 10, remote, bundle);
    CHECK(g_active && adventure_captains::key_for(1) == mapped && imports == 1);
    adventure_captains::reset();
    CHECK(adventure_captains::init(&api, nullptr, &session));
    descriptor.run_id = 31;
    records[id({mapped.instance, gear, group})] = {0xFF};
    receive(1, 1, 10, remote, bundle);
    CHECK(!g_active && cancelled.find("occupied") != std::string::npos && imports == 1);
    records[id({mapped.instance, gear, group})] = {0x56};
    g_active = true;
    adventure_captains::reset();

    // The last captain cannot confirm readiness until every other frozen member ACKs.
    descriptor.run_id = 4;
    CHECK(adventure_captains::publish(original, [&](const Key& key) { chosen = key; callbacks++; })); drain();
    CHECK(callbacks == 2);
    std::vector<dbpf::Record> own_records;
    CHECK(adventure_collect::collect_creation(original, own_records, error));
    net::adventure_content::Bundle own_bundle;
    CHECK(net::adventure_content::make_bundle(own_records.front().key, own_records, own_bundle, error));
    auto ack = packet(197, 0, 4, 1); // reset starts a new session's transfer sequence
    ack.str8(own_bundle.hash); incoming(1, ack);
    CHECK(callbacks == 3 && chosen == original);

    adventure_captains::reset();
    descriptor.run_id = 5; descriptor.roster = {0};
    CHECK(adventure_captains::choose({300, crt, group}, [&](const Key& key) { chosen = key; callbacks++; }));
    CHECK(callbacks == 4 && chosen.instance == 300); // authored locks may use a story creature
    adventure_captains::reset();
    descriptor.run_id = 6;
    CHECK(adventure_captains::choose({}, [&](const Key&) { callbacks++; })); drain();
    choose_wizard();
    skg::field<int>(wizard_data, 0x20) = 0;
    adventure_captains::tick(0); drain();
    CHECK(callbacks == 5 && !g_active && cancelled.find("cancelled") != std::string::npos);
    adventure_captains::close_picker(); adventure_captains::reset();
    descriptor.run_id = 7; g_active = true;
    CHECK(adventure_captains::choose({}, [&](const Key&) { callbacks++; })); choose_wizard();
    effect_vtable[1] = (void*)release_effect; effect_vtable[3] = (void*)stop_effect;
    skg::field<void*>(wizard_data, 0x84) = &preview_effect;
    skg::field<void*>(wizard_data, 0x88) = &launch_effect;
    skg::field<void*>(browser, 0x1A0) = wizard_data + 8;
    adventure_captains::reset(); adventure_captains::close_picker();
    CHECK(skg::field<void*>(browser, 0x1A0) == nullptr);
    CHECK(effect_stops == 2 && effect_releases == 2);
    adventure_captains::close_picker();
    CHECK(effect_stops == 2 && effect_releases == 2);
    CHECK(!adventure_captains::complete_native(descriptor.adventure, original));
    CHECK(callbacks == 5); // cancelled attempt cannot publish a delayed save callback
    session.leave();
    std::printf("adventure captains: %s (%d failures)\n", failures ? "FAILED" : "passed", failures);
    return failures ? 1 : 0;
}
