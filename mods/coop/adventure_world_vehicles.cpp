#include "adventure_world_vehicles.h"

#include <array>
#include <cstring>
#include <utility>

#include "sporekit_game.hpp"

namespace adventure_world_vehicles {

static_assert(sizeof(void*) == 4, "The native adventure adapter requires the Win32 ABI.");

namespace {
constexpr const char* kHash = "dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37";
constexpr std::array<const char*, 4> kSymbols = {
    "SporeCoop::Adventure::TickBehaviorTree",
    "SporeCoop::Adventure::TickVehicleWeapons",
    "SporeCoop::Adventure::TickVehicleTimers",
    "SporeCoop::Adventure::ProcessActor",
};

const SkHost* H;
SkMod* M;
Ctx C;
bool g_ok;
Counters g_counters;

// Bit-preserving stack words: float delta, double time, tree, actor, context,
// and three native state words. The return value is in AL.
using Tree = bool(SK_CDECL*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                            uint32_t, uint32_t, uint32_t, uint32_t);
using Weapons = void(SK_THISCALL*)(void*, uint32_t);
using Timers = void(SK_THISCALL*)(void*);
using Actor = void(SK_THISCALL*)(void*, void*, float);
Tree o_tree;
Weapons o_weapons;
Timers o_timers;
Actor o_actor;

bool guest()
{
    return g_ok && C.active && C.active() && C.host && !C.host() && C.run && C.run() != 0;
}
bool paused() { return g_ok && C.active && C.active() && C.paused && C.paused(); }
bool mirrored(void* object) { return object && (paused() || (guest() && C.is_mirrored && C.is_mirrored(object))); }

bool SK_CDECL tree(uint32_t delta, uint32_t time_low, uint32_t time_high,
                   uint32_t definition, uint32_t actor, uint32_t context,
                   uint32_t state0, uint32_t state1, uint32_t state2)
{
    if (mirrored(reinterpret_cast<void*>(static_cast<uintptr_t>(actor)))) {
        ++g_counters.trees;
        return false; // Leave the native tree pending, without advancing its state.
    }
    return o_tree(delta, time_low, time_high, definition, actor, context, state0, state1, state2);
}

void SK_THISCALL weapons(void* vehicle, uint32_t delta)
{
    if (mirrored(vehicle)) { ++g_counters.weapons; return; }
    o_weapons(vehicle, delta);
}

void SK_THISCALL timers(void* vehicle)
{
    if (mirrored(vehicle)) { ++g_counters.timers; return; }
    o_timers(vehicle);
}

void SK_THISCALL actor(void* mode, void* combatant, float delta)
{
    if(paused()) { ++g_counters.actors; return; }
    if (!guest()) { o_actor(mode, combatant, delta); return; }
    // The common native combatant interface returns its owning cGameData at
    // vtable +0C. The predicate must never see an interior combatant pointer.
    void* object = nullptr;
    if (combatant && skg::readable(combatant, sizeof(void*))) {
        auto* vtable = skg::vtable(combatant);
        if (vtable && skg::readable(vtable, 0x10) && vtable[0x0C / 4])
            object = skg::vcall<void*>(combatant, 0x0C / 4);
    }
    if (mirrored(object)) { ++g_counters.actors; return; }
    o_actor(mode, combatant, delta);
}
} // namespace

bool init(const SkHost* host, SkMod* mod, Ctx ctx)
{
    if (g_ok) return false;
    H = host; M = mod; C = std::move(ctx); reset();
    if (!H || !H->sym || !H->exe_sha256 || !H->hook_create || !H->hook_remove ||
        !C.active || !C.host || !C.run || !C.is_mirrored) return false;
    const char* hash = H->exe_sha256();
    if (!hash || std::strcmp(hash, kHash) != 0) return false;
    std::array<void*, 4> targets{};
    for (size_t i = 0; i < targets.size(); ++i) {
        targets[i] = reinterpret_cast<void*>(H->sym(kSymbols[i]));
        if (!targets[i]) return false;
    }
    std::array<void*, 4> detours = {reinterpret_cast<void*>(&tree), reinterpret_cast<void*>(&weapons),
                                  reinterpret_cast<void*>(&timers), reinterpret_cast<void*>(&actor)};
    std::array<void**, 4> originals = {reinterpret_cast<void**>(&o_tree), reinterpret_cast<void**>(&o_weapons),
                                     reinterpret_cast<void**>(&o_timers), reinterpret_cast<void**>(&o_actor)};
    for (size_t i = 0; i < targets.size(); ++i) {
        if (H->hook_create(M, targets[i], detours[i], originals[i]) != 0) {
            while (i != 0) { --i; H->hook_remove(M, targets[i]); }
            return false;
        }
    }
    g_ok = true;
    return true;
}

bool available() { return g_ok; }
Counters counters() { return g_counters; }
void reset() { g_counters = {}; }

} // namespace adventure_world_vehicles
