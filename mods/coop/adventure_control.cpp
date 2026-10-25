#include "adventure_control.h"

#include <array>
#include <cmath>
#include <cstring>
#include <map>
#include <utility>

#include "game.h"
#include "sporekit_game.hpp"

namespace adventure_control {
namespace {
using namespace skg;
constexpr const char* kHash = "dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37";
using Brain = void(SK_THISCALL*)(void*, float);
using Walk = void(SK_THISCALL*)(void*);
using Cursor = bool(SK_THISCALL*)(void*, bool, bool);
// Seven opaque stack words preserve the native float's bits and pointer arguments.
using Order = uintptr_t(SK_THISCALL*)(void*, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
Brain original_brain;
Walk original_walk;
Cursor original_cursor;
Order original_order;
uintptr_t remove_orders;
Ctx C;
bool ready;
struct Held { void* object; uint32_t incarnation; };
std::map<uint8_t, Held> held;
uint64_t brains, walks, cursors, orders, entered, restored, corrections;
struct MenuHold { void* object = nullptr; uint32_t incarnation = 0; net::Vec3 position; } menu_hold;
uint64_t menu_stops;

bool downed(void* object, ga::Captain* result = nullptr)
{
    ga::Captain state;
    if (!ready || !object || !C.active() || !C.captain_state(object, state) || state.life != ga::Life::Downed)
        return false;
    if (result) *result = state;
    return true;
}
bool local_downed() { return downed(C.local_avatar()); }
bool menu_paused(void* object)
{
    return ready && C.active() && C.local_paused && C.local_paused() && object && object == C.local_avatar();
}
void stop_orders(void* object)
{
    game::stop(object);
    auto* behavior = field<uint8_t*>(object, 0xB4C);
    if (behavior && readable(behavior, 0x1D0))
        thiscall<void>(remove_orders, behavior + 8, UINT32_MAX, UINT32_MAX);
}
void SK_THISCALL brain(void* object, float elapsed)
{
    if (downed(object) || menu_paused(object)) { ++brains; return; }
    original_brain(object, elapsed);
}
void SK_THISCALL walk(void* strategy)
{
    if (local_downed() || menu_paused(C.local_avatar())) { ++walks; return; }
    original_walk(strategy);
}
bool SK_THISCALL cursor(void* strategy, bool first, bool second)
{
    if (local_downed() || menu_paused(C.local_avatar())) { ++cursors; return false; }
    return original_cursor(strategy, first, second);
}
uintptr_t SK_THISCALL order(void* object, uint32_t a, uint32_t b, uint32_t c, uint32_t d,
                           uint32_t e, uint32_t f, uint32_t g)
{
    if (downed(object) || menu_paused(object)) { ++orders; return 0; }
    return original_order(object, a, b, c, d, e, f, g);
}
} // namespace

bool init(const SkHost* host, SkMod* mod, Ctx context)
{
    if (ready || !host || !host->sym || !host->exe_sha256 || !host->hook_create ||
        !context.active || !context.local_avatar || !context.captain_state) return false;
    const char* hash = host->exe_sha256();
    if (!hash || std::strcmp(hash, kHash)) return false;
    const char* names[] = {"SporeCoop::Adventure::AvatarTickAI", "SporeCoop::Adventure::MoveAvatarWASD",
        "SporeCoop::Adventure::MoveAvatarToCursor", "SporeCoop::Adventure::IssueCreatureOrder"};
    std::array<uintptr_t, 4> addresses{};
    for (size_t i = 0; i < addresses.size(); ++i) {
        addresses[i] = host->sym(names[i]);
        if (!addresses[i]) return false;
    }
    remove_orders = host->sym("SporeCoop::Adventure::RemoveBehaviorOrders");
    if (!remove_orders) return false;
    if (host->hook_create(mod, reinterpret_cast<void*>(addresses[0]), reinterpret_cast<void*>(&brain),
                          reinterpret_cast<void**>(&original_brain)) ||
        host->hook_create(mod, reinterpret_cast<void*>(addresses[1]), reinterpret_cast<void*>(&walk),
                          reinterpret_cast<void**>(&original_walk)) ||
        host->hook_create(mod, reinterpret_cast<void*>(addresses[2]), reinterpret_cast<void*>(&cursor),
                          reinterpret_cast<void**>(&original_cursor)) ||
        host->hook_create(mod, reinterpret_cast<void*>(addresses[3]), reinterpret_cast<void*>(&order),
                          reinterpret_cast<void**>(&original_order))) return false;
    C = std::move(context);
    ready = true;
    return true;
}

void creature_update(void* object, bool before)
{
    ga::Captain state;
    if (!downed(object) && menu_paused(object) && readable(object, game::kFlagsOff + 8)) {
        uint32_t incarnation = field<uint32_t>(object, 0x24);
        if (menu_hold.object != object || menu_hold.incarnation != incarnation) {
            menu_hold = {object, incarnation, game::position(object)};
            stop_orders(object); ++menu_stops;
        }
        field<net::Vec3>(object, game::kVelOff) = {};
        const auto position = game::position(object);
        const float dx = position.x - menu_hold.position.x, dy = position.y - menu_hold.position.y,
                    dz = position.z - menu_hold.position.z;
        if (!before && dx * dx + dy * dy + dz * dz > .0001f) game::set_position(object, menu_hold.position);
        return;
    }
    if (menu_hold.object == object) menu_hold = {};
    if (!downed(object, &state)) {
        // A replaced/destroyed object is never dereferenced during cleanup.
        for (auto it = held.begin(); it != held.end();) {
            if (it->second.object == object) { it = held.erase(it); ++restored; }
            else ++it;
        }
        return;
    }
    if (!readable(object, game::kFlagsOff + 8)) return;
    uint32_t incarnation = field<uint32_t>(object, 0x24);
    auto it = held.find(state.player);
    bool transition = it == held.end() || it->second.object != object || it->second.incarnation != incarnation;
    if (transition) {
        held[state.player] = {object, incarnation};
        stop_orders(object);
        ++entered;
    }
    // Native physics and effects still run. Remove old jump/throw/drive momentum,
    // and correct actual displacement at the simulation boundary, not at 10 Hz.
    field<net::Vec3>(object, game::kVelOff) = {};
    const net::Vec3 anchor{state.position.x, state.position.y, state.position.z};
    const auto position = game::position(object);
    const float dx = position.x - anchor.x, dy = position.y - anchor.y, dz = position.z - anchor.z;
    if (transition || (!before && dx * dx + dy * dy + dz * dz > .0001f)) {
        game::set_position(object, anchor);
        if (!transition) ++corrections;
    }
}

void reset() { held.clear(); menu_hold = {}; }
std::string status()
{
    return "downed controls: entries=" + std::to_string(entered) + " exits=" + std::to_string(restored) +
        " brain=" + std::to_string(brains) + " wasd=" + std::to_string(walks) +
        " cursor=" + std::to_string(cursors) + " orders=" + std::to_string(orders) +
        " corrections=" + std::to_string(corrections) + " menu_stops=" + std::to_string(menu_stops) + "\n";
}
} // namespace adventure_control
