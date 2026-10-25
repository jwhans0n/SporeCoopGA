#include "adventure_world_actions.h"

#include <array>
#include <cmath>
#include <cstring>
#include <utility>

#include "game.h"
#include "sporekit_game.hpp"

namespace adventure_world_actions {
namespace {
using namespace skg;
static_assert(sizeof(void*) == 4, "The native adventure adapter requires Win32.");
constexpr const char* kHash = "dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37";
constexpr uint32_t kSpatial = 0x01186577, kCreature = 0xCE9F6639;
constexpr uint32_t kBuilding = 0x0E9CB8BA, kVehicle = 0x0137E8E0, kGameData = 0x017F243B;
const SkHost* H;
Ctx C;
bool ready;
uintptr_t a_mode, a_class, a_building, a_vehicle, a_consume, a_drop, a_picked;
uintptr_t a_posse, a_recruit, a_relationship, a_noun, a_destroy, a_finished, a_order;
uintptr_t a_receive, a_order_target, a_remove_order, a_pick;
uintptr_t a_throw_source;
using Tick = bool(SK_CDECL*)(void*, uint32_t, uint32_t, uint32_t, uint32_t, void*, uint32_t, float);
Tick original_tick;

bool live(void* object) { return object && readable(object, 0x28) && !field<bool>(object, 0x20); }

bool context(uint8_t*& data, uint8_t*& play, std::string& error)
{
    if (!ready || !C.active || !C.active()) { error = "Native adventure actions are not active."; return false; }
    auto* mode = *reinterpret_cast<uint8_t**>(a_mode);
    if (!mode || !readable(mode, 0xD0) || field<uint32_t>(mode, 0xCC) != 2) {
        error = "The native adventure changed before its action."; return false;
    }
    data = field<uint8_t*>(mode, 0x74); play = field<uint8_t*>(mode, 0x78);
    if (!data || !readable(data, 0xD0) || !play || !readable(play, 0xBC) || field<int>(play, 0x90) != 3) {
        error = "The native adventure is not playing."; return false;
    }
    return true;
}

void* spatial_cast(void* spatial, uint32_t type)
{
    if (!spatial || !readable(spatial, sizeof(void*))) return nullptr;
    void** table = vtable(spatial);
    return table && readable(table, 0xBC) && table[0xB8 / 4] ? vcall<void*>(spatial, 0xB8 / 4, type) : nullptr;
}

// The native action callback has eight stack words, including the split double.
// Generic tree call BC9477 cleans 0x20 bytes. D83E90 uses actor word1, state word6.
bool SK_CDECL give_tick(void* captain, uint32_t time_low, uint32_t time_high, uint32_t context0,
                        uint32_t context1, void* state, uint32_t context2, float delta)
{
    uint8_t *data, *play;
    std::string error;
    if (ready && context(data, play, error) && C.local_captain(captain) && live(captain) &&
        readable(captain, 0xE8C) && state && readable(state, 0x2C) && field<int>(state, 0) == 1) {
        auto* behavior = field<uint8_t*>(captain, 0xB4C);
        auto* order = behavior && readable(behavior, 0x1D0) ?
            thiscall<uint8_t*>(a_order, behavior + 8, 0, 0x40000) : nullptr;
        void* recipient_spatial = field<void*>(state, 0x18);
        void* item_spatial = field<void*>(state, 0x1C);
        void* recipient = spatial_cast(recipient_spatial, kGameData);
        void* item = spatial_cast(item_spatial, kGameData);
        bool container = spatial_cast(recipient_spatial, kBuilding) || spatial_cast(recipient_spatial, kVehicle);
        if (order && readable(order, 0x10) && field<int>(order, 0x0C) == 6 && container && live(recipient) && live(item) &&
            thiscall<bool>(a_finished, captain, field<int>(state, 0x28))) {
            if (C.request_give(captain, item, recipient)) field<int>(state, 0) = 2;
            // A failed send remains pending. Never let a guest's consume branch
            // destroy this item before a host can validate its identity.
            if (std::isfinite(delta) && delta >= 0 && std::isfinite(field<float>(state, 8)))
                field<float>(state, 8) += delta;
            return true;
        }
    }
    return original_tick(captain, time_low, time_high, context0, context1, state, context2, delta);
}

bool held(void* captain, void* spatial, bool& result, std::string& error)
{
    result = false;
    if (!readable(captain, 0xE48)) { error = "The captain inventory is unavailable."; return false; }
    uintptr_t begin = field<uintptr_t>(captain, 0xE40), end = field<uintptr_t>(captain, 0xE44);
    if (end < begin || (end - begin) % 0x24 || (end - begin) / 0x24 > 128 ||
        (end != begin && !readable(reinterpret_cast<void*>(begin), end - begin))) {
        error = "The captain inventory has invalid slots."; return false;
    }
    for (uintptr_t slot = begin; slot < end; slot += 0x24)
        if (field<void*>(reinterpret_cast<void*>(slot), 0x20) == spatial) result = true;
    return true;
}
} // namespace

bool init(const SkHost* host, SkMod* mod, Ctx ctx)
{
    if (ready) return false;
    H = host; C = std::move(ctx);
    if (!H || !H->sym || !H->exe_sha256 || !H->hook_create || !C.active || !C.host ||
        !C.local_captain || !C.request_give) return false;
    const char* hash = H->exe_sha256();
    if (!hash || std::strcmp(hash, kHash)) return false;
    const char* names[] = {"App::cScenarioMode::ptr", "Simulator::cScenarioData::GetClass",
        "SporeCoop::Adventure::BuildingInventoryGive", "SporeCoop::Adventure::VehicleInventoryGive",
        "SporeCoop::Adventure::VehicleGiveConsumesItem", "SporeCoop::Adventure::DropItem",
        "SporeCoop::Adventure::OnPickUp", "SporeCoop::Adventure::PosseGet", "SporeCoop::Adventure::PosseRecruit",
        "SporeCoop::Adventure::ClassActRelationship", "Simulator::cGameNounManager::Get",
        "Simulator::cGameNounManager::DestroyInstance", "SporeCoop::Adventure::GiveAnimationFinished",
        "SporeCoop::Adventure::GetBehaviorOrder"};
    uintptr_t* addresses[] = {&a_mode, &a_class, &a_building, &a_vehicle, &a_consume, &a_drop,
        &a_picked, &a_posse, &a_recruit, &a_relationship, &a_noun, &a_destroy, &a_finished, &a_order};
    for (size_t i = 0; i < std::size(names); ++i) {
        *addresses[i] = H->sym(names[i]); if (!*addresses[i]) return false;
    }
    a_receive = H->sym("SporeCoop::Adventure::CanReceiveItem");
    a_order_target = H->sym("SporeCoop::Adventure::SocialOrderTarget");
    a_remove_order = H->sym("SporeCoop::Adventure::RemoveBehaviorOrders");
    a_pick = H->sym("SporeCoop::Adventure::PickUpItem");
    a_throw_source = H->sym("SporeCoop::Adventure::SetThrowCombatantReference");
    if (!a_receive || !a_order_target || !a_remove_order || !a_pick || !a_throw_source) return false;
    auto* tick = reinterpret_cast<void*>(H->sym("SporeCoop::Adventure::TickContainerGive"));
    if (!tick || H->hook_create(mod, tick, reinterpret_cast<void*>(&give_tick),
                               reinterpret_cast<void**>(&original_tick))) return false;
    ready = true;
    return true;
}

bool available() { return ready; }

bool can_receive(void* creature, void* spatial, bool allow_held)
{
    return ready && live(creature) && spatial && readable(creature,0xE48) &&
        thiscall<bool>(a_receive,creature,spatial,allow_held,false);
}

void throw_source(void* captain, void* item)
{
    if(!ready || !live(captain) || !live(item)) return;
    void* interactable=game::cast(item,0x0398420D);
    void* combatant=game::cast(captain,0x013F94D4);
    if(interactable && combatant && readable(interactable,0x110))
        thiscall<void*>(a_throw_source,static_cast<uint8_t*>(interactable)+0x10C,combatant);
}

void poll_receive(void* recipient)
{
    uint8_t *data, *play; std::string error;
    if (C.host() || !context(data, play, error) || !live(recipient) ||
        !readable(recipient, 0xE48) || !game::cast(recipient, kCreature)) return;
    auto* behavior = field<uint8_t*>(recipient, 0xB4C);
    if (!behavior || !readable(behavior, 0x1D0)) return;
    auto* order = thiscall<uint8_t*>(a_order, behavior + 8, 0, 0x40000);
    if (!order || !readable(order, 0x30) || field<int>(order, 0xC) != 7) return;
    void* donor = cdecl_call<void*>(a_order_target, order);
    if (!donor || !C.local_captain(donor) || !readable(donor, 0xE48)) return;
    uintptr_t first = field<uintptr_t>(donor, 0xE40), last = field<uintptr_t>(donor, 0xE44);
    uint32_t slot = field<uint32_t>(order, 0x10);
    if (last < first || (last-first)%0x24 || (last-first)/0x24 > 128 ||
        slot >= (last-first)/0x24 || !readable(reinterpret_cast<void*>(first),last-first)) return;
    void* item = spatial_cast(field<void*>(reinterpret_cast<void*>(first+slot*0x24),0x20), kGameData);
    if (live(item) && C.request_give(donor,item,recipient))
        thiscall<void>(a_remove_order, behavior+8, 0, 0x40000);
}

bool give(void* captain, void* recipient, void* item, std::string& error, GiveResult* result)
{
    error.clear(); uint8_t *data, *play;
    if (!context(data, play, error)) return false;
    (void)data;
    if (!live(captain) || !live(recipient) || !live(item) || captain == recipient || recipient == item) {
        error = "The adventure Give target or item is no longer live."; return false;
    }
    void* creature = game::cast(captain, kCreature);
    void* spatial = game::cast(item, kSpatial);
    void* building = game::cast(recipient, kBuilding);
    void* vehicle = building ? nullptr : game::cast(recipient, kVehicle);
    void* receiver = game::cast(recipient, kCreature);
    if (creature && spatial && receiver) {
        bool carrying;
        if (!held(creature, spatial, carrying, error)) return false;
        // Native receive initialization checks CanReceive(item,true,false),
        // releases its current slot0 tool, then picks into slot0. Let the native
        // replacement path perform that release; never fabricate its pointers.
        if (!thiscall<bool>(a_receive, receiver, spatial, true, false)) {
            error = "The creature cannot receive this native item."; return false;
        }
        if (carrying && !thiscall<bool>(a_drop, creature, spatial, 1, false)) {
            error = "The captain could not release the item for Give."; return false;
        }
        if (!thiscall<bool>(a_pick, receiver, spatial, 0, true)) {
            error = "The creature rejected the native item."; return false;
        }
        if (result) *result = GiveResult::Stored;
        return true;
    }
    if (!creature || !spatial || (!building && !vehicle)) {
        error = "The Give action requires a captain, a spatial item and a native container."; return false;
    }
    if ((building && !readable(building, 0x1F0)) || (vehicle && !readable(vehicle, 0xD98))) {
        error = "The native container is unavailable."; return false;
    }
    bool invulnerable = building && field<bool>(building, 0x120 + 0x80);
    if (!invulnerable && !(building ? field<void*>(building, 0x1EC) : field<void*>(vehicle, 0xD94)) &&
        !(vehicle && thiscall<bool>(a_consume, vehicle))) {
        error = "The target has no native container inventory."; return false;
    }
    bool carrying;
    if (!held(creature, spatial, carrying, error)) return false;
    if (carrying && !thiscall<bool>(a_drop, creature, spatial, 1, false)) {
        error = "The captain could not release the item for Give."; return false;
    }
    bool consumed = invulnerable || (vehicle && thiscall<bool>(a_consume, vehicle));
    if (consumed) {
        void* noun = cdecl_call<void*>(a_noun);
        if (!noun || !readable(noun, 4)) { error = "The native object manager is unavailable."; return false; }
        if (invulnerable) thiscall<void>(a_picked, play, building, item);
        thiscall<void>(a_destroy, noun, item);
        if (result) *result = invulnerable ? GiveResult::ConsumedWithGoal : GiveResult::ConsumedWithoutGoal;
        return true;
    }
    bool stored = thiscall<bool>(building ? a_building : a_vehicle, building ? building : vehicle, item);
    if (!stored) { error = "The native container rejected the item."; return false; }
    if (result) *result = GiveResult::Stored;
    return true;
}

bool recruit(void* target, std::string& error)
{
    error.clear(); uint8_t *data, *play;
    if (!context(data, play, error)) return false;
    (void)data; (void)play;
    void* creature = live(target) ? game::cast(target, kCreature) : nullptr;
    if (!creature || !readable(creature, 0xE6C) || field<int>(target, 0x1C) < 0) {
        error = "Only a live authored adventure creature can join the posse."; return false;
    }
    void* posse = cdecl_call<void*>(a_posse);
    if (!posse || !readable(posse, 0x58)) { error = "The native adventure posse is unavailable."; return false; }
    if (!thiscall<bool>(a_recruit, posse, creature)) {
        error = "The native posse rejected recruitment (capacity or existing membership)."; return false;
    }
    return true; // The native operation invokes OnAlly itself. Do not count twice.
}

bool hostile(void* expected_data, void* object, int act, bool& result, std::string& error)
{
    result = false; error.clear(); uint8_t *data, *play;
    if (!context(data, play, error)) return false;
    if (data != expected_data || act < 0 || act > 7 || field<int>(play, 0xB8) != act ||
        !live(object) || field<int>(object, 0x1C) < 0) {
        error = "The authored actor or current adventure act changed."; return false;
    }
    auto* resource = field<uint8_t*>(data, 0x10);
    if (!resource || !readable(resource, 0x2C0C)) { error = "The scenario class map is unavailable."; return false; }
    uintptr_t begin = field<uintptr_t>(resource, 0x2BF4), end = field<uintptr_t>(resource, 0x2BF8);
    if (end < begin || (end - begin) % 0x27E8 || (end - begin) / 0x27E8 > 1024 || begin == end ||
        !readable(reinterpret_cast<void*>(begin), end - begin)) {
        error = "The scenario class map has invalid bounds."; return false;
    }
    auto* cls = thiscall<uint8_t*>(a_class, data, field<int>(object, 0x1C));
    uintptr_t address = reinterpret_cast<uintptr_t>(cls);
    if (address < begin + 8 || address >= end || (address - begin - 8) % 0x27E8 ||
        field<int>(cls - 8, 0) != field<int>(object, 0x1C)) {
        error = "The native actor class does not match its authored map entry."; return false;
    }
    uintptr_t acts = field<uintptr_t>(cls, 0x70), acts_end = field<uintptr_t>(cls, 0x74);
    if (acts_end < acts || (acts_end - acts) % 0x4E0 || (acts_end - acts) / 0x4E0 > 8 ||
        size_t(act) >= (acts_end - acts) / 0x4E0 || !readable(reinterpret_cast<void*>(acts), acts_end - acts)) {
        error = "The native actor class has no bounded data for this act."; return false;
    }
    auto* current = reinterpret_cast<uint8_t*>(acts + size_t(act) * 0x4E0);
    if (field<bool>(current, 0x4C8)) {
        uintptr_t first = field<uintptr_t>(current, 0x4CC), last = field<uintptr_t>(current, 0x4D0);
        if (last < first || (last - first) % 0x20 || (last - first) / 0x20 > 1024 ||
            (last != first && !readable(reinterpret_cast<void*>(first), last - first))) {
            error = "The actor's advanced behavior entries have invalid bounds."; return false;
        }
    }
    // BA29F0 explicitly accepts a null optional creature. Use its authored
    // relationship result, not a guessed team != player test.
    int relationship = cdecl_call<int>(a_relationship, cls, act, static_cast<void*>(nullptr));
    result = relationship == 2 || relationship == 5;
    return true;
}

} // namespace adventure_world_actions
