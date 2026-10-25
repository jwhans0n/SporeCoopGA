// Win32 fixture for the verified native action stack shapes and branch semantics.
#include <array>
#include <cstdio>
#include <map>
#include <string>

#include "adventure_world_actions.h"
#include "game.h"
#include "sporekit_game.hpp"

namespace {
using skg::field;
constexpr uint32_t kSpatial = 0x01186577, kCreature = 0xCE9F6639;
constexpr uint32_t kBuilding = 0x0E9CB8BA, kVehicle = 0x0137E8E0, kGameData = 0x017F243B;
using Tick = bool(SK_CDECL*)(void*, uint32_t, uint32_t, uint32_t, uint32_t, void*, uint32_t, float);
struct Object { std::array<uint8_t, 0x1000> bytes{}; std::array<uint8_t, 0xC4> spatial{}; uint32_t type; };
Object captain{{}, {}, kCreature}, building{{}, {}, kBuilding}, vehicle{{}, {}, kVehicle}, item{{}, {}, 0}, target{{}, {}, kCreature};
std::array<uint8_t, 0xD0> mode{}, data{};
std::array<uint8_t, 0x110> play{};
std::array<uint8_t, 0x2C0C> resource{};
std::array<uint8_t, 0x27E8> class_entry{};
std::array<uint8_t, 0x4E0> class_act{};
std::array<uint8_t, 0x20> advanced{};
std::array<uint8_t, 0x24> slot{};
std::array<uint8_t, 0x24> receiver_slot{};
std::array<uint8_t, 0x1D0> behavior{};
std::array<uint8_t, 0x38> order{}, state{};
std::array<uint8_t, 0x58> posse{};
uint8_t* mode_ptr = mode.data();
void* spatial_vtable[0xC4 / 4]{};
bool playing = true, hosting = true, request_ok = true, finished = true, storage_ok = true, consumes = false, recruitment_ok = true;
bool hook_fail;
int failures, storage_calls, drop_calls, pickup_calls, destroy_calls, recruit_calls, relation_calls, request_calls, native_ticks, hook_calls;
int relation = 5;
bool receives = true;
int removed_orders, receiver_picks;
int throw_references;
void* picked_holder;
void* stored_holder;
Tick hooked_tick;
const char* executable_hash = "dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37";
std::map<std::string, uintptr_t> symbols;
void check(bool value, const char* message) { if (!value) { ++failures; std::printf("FAIL: %s\n", message); } }
Object* object(void* p)
{
    for (auto* o : {&captain, &building, &vehicle, &item, &target}) if (o->bytes.data() == p) return o;
    return nullptr;
}
void* SK_THISCALL spatial_cast(void* p, uint32_t type)
{
    for (auto* o : {&captain, &building, &vehicle, &item, &target}) if (o->spatial.data() == p)
        return type == kGameData || type == o->type ? o->bytes.data() : nullptr;
    return nullptr;
}
bool SK_THISCALL store(void* recipient, void* value)
{
    check(value == item.bytes.data(), "storage item argument"); stored_holder = recipient; ++storage_calls;
    if (storage_ok) { picked_holder = recipient; ++pickup_calls; }
    return storage_ok;
}
bool SK_THISCALL vehicle_consumes(void* self) { check(self == vehicle.bytes.data(), "vehicle timer this"); return consumes; }
bool SK_THISCALL receive_item(void* self,void* spatial,bool allow_held,bool mouth)
{
    check(self==target.bytes.data() && spatial==item.spatial.data() && allow_held && !mouth,"native receive eligibility ABI");
    return receives;
}
bool SK_THISCALL pick(void* self,void* spatial,int index,bool replace)
{
    check(self==target.bytes.data() && spatial==item.spatial.data() && index==0 && replace,"native creature receive preserves slot0 replacement ABI");
    field<void*>(receiver_slot.data(),0x20)=spatial; ++receiver_picks; return true;
}
void* SK_CDECL order_target(void* value) { check(value==order.data(),"receive order target ABI"); return captain.bytes.data(); }
void SK_THISCALL remove_order(void* self,int low,int high)
{ check(self==behavior.data()+8 && low==0 && high==0x40000,"receive order removal ABI"); ++removed_orders; }
void* SK_THISCALL throw_reference(void* self,void* combatant)
{
    check(self==item.bytes.data()+0x10C && combatant==captain.bytes.data()+0x5A8,"native throw retains the combatant through its intrusive reference ABI");
    ++throw_references; return self;
}
bool SK_THISCALL drop(void* self, void* spatial, int arg, bool replace)
{
    check(self == captain.bytes.data() && spatial == item.spatial.data() && arg == 1 && !replace, "native Give drop ABI");
    ++drop_calls; field<void*>(slot.data(), 0x20) = nullptr; return true;
}
void SK_THISCALL picked(void* self, void* holder, void* value)
{
    check(self == play.data() && value == item.bytes.data(), "consume pickup ABI");
    ++pickup_calls; picked_holder = holder;
}
void* SK_CDECL get_posse() { return posse.data(); }
bool SK_THISCALL recruit(void* self, void* creature)
{
    check(self == posse.data() && creature == target.bytes.data(), "native recruitment this and creature");
    ++recruit_calls; return recruitment_ok;
}
void* SK_CDECL noun() { return data.data(); }
void SK_THISCALL destroy(void* self, void* value)
{
    check(self == data.data() && value == item.bytes.data(), "native consume destroy ABI");
    ++destroy_calls; field<bool>(value, 0x20) = true;
}
uint8_t* SK_THISCALL get_class(void* self, int id)
{ check(self == data.data() && id == 7, "native class lookup ABI"); return class_entry.data() + 8; }
int SK_CDECL relationship(void* cls, int act, void* optional)
{
    check(cls == class_entry.data() + 8 && act == 0 && !optional, "class relationship null optional creature and cdecl ABI");
    ++relation_calls; return relation;
}
bool SK_THISCALL animation_finished(void* self, int id)
{ check(self == captain.bytes.data() && id == 99, "Give animation query ABI"); return finished; }
uint8_t* SK_THISCALL get_order(void* self, int low, int high)
{ check(self == behavior.data() + 8 && low == 0 && high == 0x40000, "Give native order mask ABI"); return order.data(); }
bool SK_CDECL original(void* actor, uint32_t t0, uint32_t t1, uint32_t c0, uint32_t c1, void* s, uint32_t c2, float dt)
{
    check(actor == captain.bytes.data() && t0 == 0x12345678 && t1 == 0x9ABCDEF0 && c0 == 3 && c1 == 4 &&
          s == state.data() && c2 == 6 && dt == 0.25f, "all eight original tick stack words retained");
    ++native_ticks; return false;
}
uintptr_t symbol(const char* name) { auto i = symbols.find(name); return i == symbols.end() ? 0 : i->second; }
int hook(SkMod*, void* entry, void* detour, void** trampoline)
{
    check(entry == reinterpret_cast<void*>(&original), "intent hook target"); ++hook_calls;
    if (hook_fail) return -1;
    hooked_tick = reinterpret_cast<Tick>(detour); *trampoline = reinterpret_cast<void*>(&original); return 0;
}
void reset_item(bool carrying = true)
{
    field<bool>(item.bytes.data(), 0x20) = false;
    field<void*>(slot.data(), 0x20) = carrying ? item.spatial.data() : nullptr;
}
bool tick()
{ return hooked_tick(captain.bytes.data(), 0x12345678, 0x9ABCDEF0, 3, 4, state.data(), 6, 0.25f); }
} // namespace

namespace game {
void* cast(void* value, uint32_t type)
{
    auto* o = object(value);
    if (!o) return nullptr;
    if(type==0x0398420D && o==&item) return o->bytes.data();
    if(type==0x013F94D4 && o==&captain) return o->bytes.data()+0x5A8;
    return type == kSpatial ? o->spatial.data() : type == o->type ? o->bytes.data() : nullptr;
}
} // namespace game

int main()
{
    spatial_vtable[0xB8 / 4] = reinterpret_cast<void*>(&spatial_cast);
    for (auto* o : {&captain, &building, &vehicle, &item, &target}) field<void**>(o->spatial.data(), 0) = spatial_vtable;
    field<int>(target.bytes.data(), 0x1C) = 7; field<int>(building.bytes.data(), 0x1C) = 7;
    field<uint8_t*>(mode.data(), 0x74) = data.data(); field<uint8_t*>(mode.data(), 0x78) = play.data();
    field<int>(mode.data(), 0xCC) = 2; field<int>(play.data(), 0x90) = 3;
    field<uint8_t*>(data.data(), 0x10) = resource.data();
    field<uint8_t*>(resource.data(), 0x2BF4) = class_entry.data();
    field<uint8_t*>(resource.data(), 0x2BF8) = class_entry.data() + class_entry.size();
    field<int>(class_entry.data(), 0) = 7;
    field<uint8_t*>(class_entry.data() + 8, 0x70) = class_act.data();
    field<uint8_t*>(class_entry.data() + 8, 0x74) = class_act.data() + class_act.size();
    field<uint8_t*>(captain.bytes.data(), 0xE40) = slot.data();
    field<uint8_t*>(captain.bytes.data(), 0xE44) = slot.data() + slot.size();
    field<uint8_t*>(captain.bytes.data(), 0xB4C) = behavior.data();
    field<uint8_t*>(target.bytes.data(), 0xB4C) = behavior.data();
    field<uint8_t*>(target.bytes.data(), 0xE40) = receiver_slot.data();
    field<uint8_t*>(target.bytes.data(), 0xE44) = receiver_slot.data()+receiver_slot.size();
    field<void*>(building.bytes.data(), 0x1EC) = data.data(); field<void*>(vehicle.bytes.data(), 0xD94) = data.data();
    field<void*>(state.data(), 0x18) = building.spatial.data(); field<void*>(state.data(), 0x1C) = item.spatial.data();
    field<int>(state.data(), 0) = 1; field<int>(state.data(), 0x28) = 99; field<int>(order.data(), 0xC) = 6;
    symbols = {
        {"App::cScenarioMode::ptr", reinterpret_cast<uintptr_t>(&mode_ptr)},
        {"Simulator::cScenarioData::GetClass", reinterpret_cast<uintptr_t>(&get_class)},
        {"SporeCoop::Adventure::BuildingInventoryGive", reinterpret_cast<uintptr_t>(&store)},
        {"SporeCoop::Adventure::VehicleInventoryGive", reinterpret_cast<uintptr_t>(&store)},
        {"SporeCoop::Adventure::VehicleGiveConsumesItem", reinterpret_cast<uintptr_t>(&vehicle_consumes)},
        {"SporeCoop::Adventure::DropItem", reinterpret_cast<uintptr_t>(&drop)},
        {"SporeCoop::Adventure::OnPickUp", reinterpret_cast<uintptr_t>(&picked)},
        {"SporeCoop::Adventure::PosseGet", reinterpret_cast<uintptr_t>(&get_posse)},
        {"SporeCoop::Adventure::PosseRecruit", reinterpret_cast<uintptr_t>(&recruit)},
        {"SporeCoop::Adventure::ClassActRelationship", reinterpret_cast<uintptr_t>(&relationship)},
        {"Simulator::cGameNounManager::Get", reinterpret_cast<uintptr_t>(&noun)},
        {"Simulator::cGameNounManager::DestroyInstance", reinterpret_cast<uintptr_t>(&destroy)},
        {"SporeCoop::Adventure::GiveAnimationFinished", reinterpret_cast<uintptr_t>(&animation_finished)},
        {"SporeCoop::Adventure::GetBehaviorOrder", reinterpret_cast<uintptr_t>(&get_order)},
        {"SporeCoop::Adventure::TickContainerGive", reinterpret_cast<uintptr_t>(&original)}};
    symbols["SporeCoop::Adventure::CanReceiveItem"]=reinterpret_cast<uintptr_t>(&receive_item);
    symbols["SporeCoop::Adventure::SocialOrderTarget"]=reinterpret_cast<uintptr_t>(&order_target);
    symbols["SporeCoop::Adventure::RemoveBehaviorOrders"]=reinterpret_cast<uintptr_t>(&remove_order);
    symbols["SporeCoop::Adventure::PickUpItem"]=reinterpret_cast<uintptr_t>(&pick);
    symbols["SporeCoop::Adventure::SetThrowCombatantReference"]=reinterpret_cast<uintptr_t>(&throw_reference);
    SkHost host{}; host.sym = symbol; host.hook_create = hook; host.exe_sha256 = [] { return executable_hash; };
    adventure_world_actions::Ctx ctx{[] { return playing; }, [] { return hosting; },
        [](void* p) { return p == captain.bytes.data(); }, [](void* c, void* i, void* r) {
            check(c == captain.bytes.data() && i == item.bytes.data() && (r == building.bytes.data() || r==target.bytes.data()), "intent keeps native object identities");
            ++request_calls; return request_ok;
        }};
    auto expected_hash = executable_hash; executable_hash = "different-build";
    check(!adventure_world_actions::init(&host, nullptr, ctx) && hook_calls == 0, "fail closed on another executable");
    executable_hash = expected_hash; hook_fail = true;
    check(!adventure_world_actions::init(&host, nullptr, ctx) && !adventure_world_actions::available(), "failed hook leaves capability unavailable");
    hook_fail = false;
    check(adventure_world_actions::init(&host, nullptr, ctx), "native actions resolve and install");
    using namespace adventure_world_actions;
    std::string error; GiveResult result;
    throw_source(captain.bytes.data(),item.bytes.data());
    check(throw_references==1,"ordinary native item throw source is retained without raw native pointer writes");
    reset_item();
    check(give(captain.bytes.data(), building.bytes.data(), item.bytes.data(), error, &result) &&
          result == GiveResult::Stored && stored_holder == building.bytes.data() && drop_calls == 1, "building Give uses real inventory and drop");
    reset_item(false);
    check(give(captain.bytes.data(), vehicle.bytes.data(), item.bytes.data(), error, &result) &&
          stored_holder == vehicle.bytes.data() && drop_calls == 1, "already dropped item enters native vehicle inventory");
    reset_item(); field<bool>(building.bytes.data(), 0x1A0) = true;
    int storage_before = storage_calls, pickup_before = pickup_calls;
    check(give(captain.bytes.data(), building.bytes.data(), item.bytes.data(), error, &result) &&
          result == GiveResult::ConsumedWithGoal && picked_holder == building.bytes.data() && pickup_calls == pickup_before + 1 &&
          storage_calls == storage_before && destroy_calls == 1, "invulnerable building counts then consumes without storage");
    field<bool>(building.bytes.data(), 0x1A0) = false; reset_item(); consumes = true; pickup_before = pickup_calls;
    check(give(captain.bytes.data(), vehicle.bytes.data(), item.bytes.data(), error, &result) &&
          result == GiveResult::ConsumedWithoutGoal && pickup_calls == pickup_before && destroy_calls == 2, "timed vehicle consumes without inventing a goal callback");
    consumes = false; reset_item(); storage_ok = false;
    check(!give(captain.bytes.data(), building.bytes.data(), item.bytes.data(), error) && !field<bool>(item.bytes.data(), 0x20), "native storage rejection does not destroy item");
    storage_ok = true;
    reset_item();
    check(give(captain.bytes.data(),target.bytes.data(),item.bytes.data(),error) && receiver_picks==1 &&
          !field<void*>(slot.data(),0x20) && field<void*>(receiver_slot.data(),0x20)==item.spatial.data(),
          "NPC Give uses native drop and recipient attachment rather than a goal-only event");
    receives=false; reset_item(); int drops_before=drop_calls;
    check(!give(captain.bytes.data(),target.bytes.data(),item.bytes.data(),error) && drop_calls==drops_before && receiver_picks==1,
          "native recipient rejection retains the donor item");
    receives=true; hosting=false; field<int>(order.data(),0xC)=7; field<int>(order.data(),0x10)=0;
    int requests_before=request_calls;
    poll_receive(target.bytes.data());
    check(request_calls==requests_before+1 && removed_orders==1 && field<void*>(slot.data(),0x20)==item.spatial.data(),
          "guest receive order is forwarded without dropping its item or running NPC AI");
    request_ok=false; poll_receive(target.bytes.data());
    check(removed_orders==1,"failed guest Give send keeps its native order pending");
    request_ok=true; field<uint32_t>(order.data(),0x10)=128; poll_receive(target.bytes.data());
    check(removed_orders==1,"malformed donor slot is rejected before reading item pointers");
    field<int>(order.data(),0xC)=6; hosting=true;
    check(recruit(target.bytes.data(), error) && recruit_calls == 1, "actual native posse operation");
    recruitment_ok = false;
    check(!recruit(target.bytes.data(), error) && recruit_calls == 2, "native capacity and duplicate rejection retained");
    field<int>(target.bytes.data(), 0x1C) = -1;
    check(!recruit(target.bytes.data(), error) && recruit_calls == 2, "captain or unclassed creature is not recruited");
    bool is_hostile;
    check(hostile(data.data(), building.bytes.data(), 0, is_hostile, error) && is_hostile, "generic authored actor uses native hostile result");
    relation = 3;
    check(hostile(data.data(), building.bytes.data(), 0, is_hostile, error) && !is_hostile, "neutral relationship is preserved");
    int relations_before = relation_calls;
    field<int>(play.data(), 0xB8) = 1;
    check(!hostile(data.data(), building.bytes.data(), 0, is_hostile, error) && relation_calls == relations_before, "old act rejected before native relation lookup");
    field<int>(play.data(), 0xB8) = 0;
    field<bool>(class_act.data(), 0x4C8) = true;
    field<uint8_t*>(class_act.data(), 0x4CC) = advanced.data();
    field<uint8_t*>(class_act.data(), 0x4D0) = advanced.data() + 1;
    check(!hostile(data.data(), building.bytes.data(), 0, is_hostile, error) && relation_calls == relations_before, "malformed advanced behavior rejected before native scan");
    field<bool>(class_act.data(), 0x4C8) = false;
    reset_item();
    requests_before=request_calls;
    check(tick() && request_calls == requests_before+1 && field<int>(state.data(), 0) == 2 && !field<bool>(item.bytes.data(), 0x20), "host container intent intercepted before native consumption");
    hosting = false; field<int>(state.data(), 0) = 1; request_ok = false;
    check(tick() && field<int>(state.data(), 0) == 1, "guest failed send stays pending without deletion");
    request_ok = true; finished = false;
    check(!tick() && native_ticks == 1, "unfinished animation preserves all original callback words");
    finished = true; playing = false;
    check(!tick() && native_ticks == 2, "inactive run leaves native Give transparent");
    playing = true; field<int>(mode.data(), 0xCC) = 1;
    check(!tick() && native_ticks == 3, "stage transition leaves native Give transparent");
    std::printf("adventure actions: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
