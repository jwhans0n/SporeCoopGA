// Win32 native hook regression: stack words, thiscall arguments, guest authority,
// host passthrough, graphics/cleanup continuity and partial installation rollback.
#include <array>
#include <bit>
#include <cstdio>
#include <cstring>
#include <map>
#include <string>

#include "adventure_world_vehicles.h"
#include "sporekit_game.hpp"

namespace {
constexpr const char* kHash = "dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37";
const char* hash = kHash;
bool active = true, host = false, live = true;
uint64_t run = 9;
int failures, creates, removes, fail_hook = -1;
unsigned native_trees, native_weapons, native_timers, native_actors, owner_gets;
unsigned initialized, graphics, cleanup;
bool tree_result = true;
std::array<uint32_t, 9> words{0x3E800000, 0x89ABCDEF, 0x01234567, 0x11223344, 0, 0x44332211,
                              0xAA55AA55, 0x55AA55AA, 0xFEDCBA98};
std::array<uint8_t, 0x40> object{}, other{};
void* mode = reinterpret_cast<void*>(0x11223344);
struct Combatant { void** table; void* owner; };
void* table[4]{};
Combatant combatant{table, object.data()}, other_combatant{table, other.data()};
constexpr uint32_t kDelta = 0xFEDCBA98;
constexpr float kSeconds = 0.125f;

void check(bool value, const char* message)
{
    if (!value) { ++failures; std::printf("FAIL: %s\n", message); }
}
bool SK_CDECL native_tree(uint32_t a, uint32_t b, uint32_t c, uint32_t d, uint32_t e,
                          uint32_t f, uint32_t g, uint32_t h, uint32_t i)
{
    ++native_trees;
    check(std::array<uint32_t, 9>{a,b,c,d,e,f,g,h,i} == words, "all nine tree stack words survive");
    return tree_result;
}
void SK_THISCALL native_weapon(void* self, uint32_t delta)
{
    ++native_weapons;
    check((self == object.data() || self == other.data()) && delta == kDelta, "weapon thiscall preserves self and delta");
}
void SK_THISCALL native_timer(void* self)
{
    ++native_timers;
    check(self == object.data() || self == other.data(), "timer thiscall preserves self");
}
void SK_THISCALL native_actor(void* self, void* value, float delta)
{
    ++native_actors;
    check(self == mode && (!value || value == &combatant || value == &other_combatant) &&
          std::bit_cast<uint32_t>(delta) == std::bit_cast<uint32_t>(kSeconds), "actor thiscall preserves all arguments");
}
void* SK_THISCALL owning_object(Combatant* self) { ++owner_gets; return self->owner; }

std::map<std::string, uintptr_t> symbols;
std::map<uintptr_t, void*> detours;
uintptr_t symbol(const char* name) { auto i = symbols.find(name); return i == symbols.end() ? 0 : i->second; }
int create(SkMod*, void* target, void* detour, void** original)
{
    if (creates++ == fail_hook) return 7;
    auto address = reinterpret_cast<uintptr_t>(target);
    check(detours.emplace(address, detour).second, "no duplicate hooks");
    *original = target; return 0;
}
int remove(SkMod*, void* target) { ++removes; return detours.erase(reinterpret_cast<uintptr_t>(target)) == 1 ? 0 : 1; }
const char* exe_hash() { return hash; }
adventure_world_vehicles::Ctx context()
{
    return {[] { return active; }, [] { return host; }, [] { return run; },
            [](void* value) { return live && value == object.data(); }};
}
using Tree = decltype(&native_tree);
using Weapon = decltype(&native_weapon);
using Timer = decltype(&native_timer);
using Actor = decltype(&native_actor);
Tree tree;
Weapon weapon;
Timer timer;
Actor actor;
bool tick_tree(void* target)
{
    words[4] = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(target));
    return tree(words[0],words[1],words[2],words[3],words[4],words[5],words[6],words[7],words[8]);
}
void aggregate(void* target, Combatant* target_combatant)
{
    ++initialized;
    weapon(target, kDelta);
    timer(target);
    tick_tree(target);
    ++graphics;
    actor(mode, target_combatant, kSeconds);
    ++cleanup;
}
} // namespace

int main()
{
    table[0x0C / 4] = reinterpret_cast<void*>(&owning_object);
    symbols = {
        {"SporeCoop::Adventure::TickBehaviorTree", reinterpret_cast<uintptr_t>(&native_tree)},
        {"SporeCoop::Adventure::TickVehicleWeapons", reinterpret_cast<uintptr_t>(&native_weapon)},
        {"SporeCoop::Adventure::TickVehicleTimers", reinterpret_cast<uintptr_t>(&native_timer)},
        {"SporeCoop::Adventure::ProcessActor", reinterpret_cast<uintptr_t>(&native_actor)},
    };
    SkHost api{}; api.sym = symbol; api.exe_sha256 = exe_hash; api.hook_create = create; api.hook_remove = remove;
    hash = "wrong build";
    check(!adventure_world_vehicles::init(&api, nullptr, context()) && creates == 0, "unverified executable installs no hooks");
    hash = kHash;
    uintptr_t saved = symbols["SporeCoop::Adventure::TickVehicleTimers"];
    symbols.erase("SporeCoop::Adventure::TickVehicleTimers");
    check(!adventure_world_vehicles::init(&api, nullptr, context()) && creates == 0, "missing native capability installs no hooks");
    symbols["SporeCoop::Adventure::TickVehicleTimers"] = saved;
    fail_hook = 3;
    check(!adventure_world_vehicles::init(&api, nullptr, context()) && removes == 3 && detours.empty() &&
          !adventure_world_vehicles::available(), "partial hook failure rolls back all earlier hooks");
    creates = 0; fail_hook = -1;
    check(adventure_world_vehicles::init(&api, nullptr, context()) && detours.size() == 4, "all verified capabilities install");
    tree = reinterpret_cast<Tree>(detours.at(reinterpret_cast<uintptr_t>(&native_tree)));
    weapon = reinterpret_cast<Weapon>(detours.at(reinterpret_cast<uintptr_t>(&native_weapon)));
    timer = reinterpret_cast<Timer>(detours.at(reinterpret_cast<uintptr_t>(&native_timer)));
    actor = reinterpret_cast<Actor>(detours.at(reinterpret_cast<uintptr_t>(&native_actor)));

    check(!tick_tree(object.data()), "blocked tree remains pending");
    aggregate(object.data(), &combatant);
    auto counts = adventure_world_vehicles::counters();
    check(!native_trees && !native_weapons && !native_timers && !native_actors, "guest mirrors cannot independently advance native behavior");
    check(counts.trees == 2 && counts.weapons == 1 && counts.timers == 1 && counts.actors == 1, "all authority guards report suppression");
    check(initialized == 1 && graphics == 1 && cleanup == 1, "native initialization graphics and cleanup continue");

    aggregate(other.data(), &other_combatant);
    live = false; aggregate(object.data(), &combatant); live = true;
    host = true; unsigned owner_before = owner_gets; aggregate(object.data(), &combatant);
    check(owner_gets == owner_before, "host passthrough does not add virtual owner calls");
    host = false; active = false; aggregate(object.data(), &combatant); active = true;
    run = 0; aggregate(object.data(), &combatant); run = 9;
    check(native_trees == 5 && native_weapons == 5 && native_timers == 5 && native_actors == 5,
          "nonmirrors dead actors host inactive and uncommitted runs retain native updates");

    host = true; tree_result = false;
    check(!tick_tree(object.data()), "native false return survives passthrough");
    tree_result = true;
    check(tick_tree(object.data()), "native true return survives passthrough");
    for (unsigned i = 0; i < 10000; ++i) aggregate(object.data(), &combatant);
    check(native_trees == 10007 && native_weapons == 10005 && native_timers == 10005 && native_actors == 10005,
          "repeated mixed cdecl and thiscall calls preserve stack balance");
    host = false; owner_before = owner_gets; actor(mode, nullptr, kSeconds);
    check(native_actors == 10006 && owner_gets == owner_before, "native null-combatant path passes through safely");
    adventure_world_vehicles::reset(); counts = adventure_world_vehicles::counters();
    check(!counts.trees && !counts.weapons && !counts.timers && !counts.actors && adventure_world_vehicles::available(),
          "run reset clears counters without removing hooks");
    std::printf("adventure vehicle hooks: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
