// Win32 lifecycle regression: retired mappings, shared markers, and native-call ABI.
#include <array>
#include <cstdio>
#include <cstring>
#include <map>
#include <memory>
#include <string>
#include <vector>

#include "adventure_world_lifecycle.h"
#include "game.h"
#include "sporekit_game.hpp"

namespace {
using skg::field;
struct Node { uint32_t key; void* value; Node* next; };
struct Iterator { Node* node; Node** bucket; };
struct Map { uint32_t unused; Node** buckets; uint32_t count, size; };
static_assert(sizeof(Node) == 12 && sizeof(Iterator) == 8 && sizeof(Map) == 16);
std::array<uint8_t, 0xE0> mode{}, data{};
std::array<uint8_t, 0x110> play{};
std::array<uint8_t, 0x2C28> resource{};
std::array<uint8_t, 0x238> placement{};
std::array<uint8_t, 0x80> cls{};
std::array<uint8_t, 0x4E0> class_act{};
std::array<uint8_t, 0x34> marker_act{};
uint8_t* mode_pointer = mode.data();
uint8_t* marker = placement.data() + 4;
Node* reverse_buckets[2] = {nullptr, reinterpret_cast<Node*>(-1)};
Node* forward_buckets[2] = {nullptr, reinterpret_cast<Node*>(-1)};
std::vector<std::unique_ptr<std::array<uint8_t, 0x100>>> objects;
std::map<void*, int> references;
unsigned destroyed, double_destroyed, created, powerups, states, visibility;
void* state_target;
bool consumible;
int failures;
void* spatial_vtable[16]{};

Map* reverse() { return reinterpret_cast<Map*>(data.data() + 0x90); }
Map* forward() { return reinterpret_cast<Map*>(data.data() + 0xB0); }
Node* lookup(Map* map, uint32_t key)
{
    for (Node* node = map->buckets[0]; node; node = node->next) if (node->key == key) return node;
    return nullptr;
}
Node* insert(Map* map, uint32_t key)
{
    if (auto* node = lookup(map, key)) return node;
    auto* node = new Node{key, nullptr, map->buckets[0]}; map->buckets[0] = node; ++map->size; return node;
}
void erase_node(Map* map, Node* node, bool release)
{
    Node** previous = &map->buckets[0];
    while (*previous && *previous != node) previous = &(*previous)->next;
    if (*previous != node) { ++failures; return; }
    *previous = node->next;
    if (release && node->value) --references[node->value];
    delete node; --map->size;
}
void* object()
{
    objects.push_back(std::make_unique<std::array<uint8_t, 0x100>>());
    auto* result = objects.back()->data();
    field<void**>(result, 0x40) = spatial_vtable;
    field<uint8_t*>(result, 0x18) = marker;
    field<int>(result, 0x1C) = 0;
    field<uint32_t>(result, 0x28) = 0x018EB45E;
    return result;
}
Iterator* SK_THISCALL find32(Map* map, Iterator* out, const uint32_t* key)
{
    auto* node = lookup(map, *key);
    *out = {node ? node : map->buckets[1], map->buckets + (node ? 0 : 1)}; return out;
}
Iterator* SK_THISCALL erase_reverse(Map* map, Iterator* out, Node* node, Node**)
{
    erase_node(map, node, false); *out = {map->buckets[1], map->buckets + 1}; return out;
}
Iterator* SK_THISCALL erase_forward(Map* map, Iterator* out, Node* node, Node**)
{
    erase_node(map, node, true); *out = {map->buckets[1], map->buckets + 1}; return out;
}
uint32_t* SK_THISCALL reverse_slot(Map* map, const uint32_t* key)
{ return reinterpret_cast<uint32_t*>(&insert(map, *key)->value); }
void** SK_THISCALL forward_slot(Map* map, const uint32_t* key) { return &insert(map, *key)->value; }
void SK_THISCALL assign(void** slot, void* value)
{
    if (*slot == value) return;
    if (value) ++references[value];
    if (*slot) --references[*slot];
    *slot = value;
}
uint8_t* SK_THISCALL get_class(void*, int index) { return index == 0 ? cls.data() : nullptr; }
void* SK_CDECL create(const void*, const void*, const void*, float, bool, int, void*, void*, int)
{ ++created; return object(); }
void* SK_CDECL powerup(void*) { ++powerups; void* value = object(); field<uint32_t>(value, 0x28) = 0x018C88E4; return value; }
bool SK_THISCALL is_consumible(void*) { return consumible; }
void SK_THISCALL marker_state(void*, void*, bool)
{
    ++states; auto* entry = lookup(forward(), 0); state_target = entry ? entry->value : nullptr;
}
void SK_THISCALL marker_visibility(void*, void*, void*) { ++visibility; }
void SK_THISCALL set_position(void* spatial, const net::Vec3* value) { field<net::Vec3>(spatial, 4) = *value; }
void SK_THISCALL set_orientation(void* spatial, const net::Quat* value) { field<net::Quat>(spatial, 0x10) = *value; }
void* SK_THISCALL get_object(void*, void*) { auto* n = lookup(forward(), 0); return n ? n->value : nullptr; }
void* SK_THISCALL get_marker(void*, void* value)
{
    auto* entry = lookup(reverse(), static_cast<uint32_t>(reinterpret_cast<uintptr_t>(value)));
    return entry && entry->value == nullptr ? marker : nullptr;
}
void SK_THISCALL remove_marker(void*, void*)
{
    auto* n = lookup(forward(), 0);
    if (!n) { ++failures; return; }
    void* value = n->value;
    game::destroy_creature(value);
    if (auto* r = lookup(reverse(), static_cast<uint32_t>(reinterpret_cast<uintptr_t>(value)))) erase_node(reverse(), r, false);
    erase_node(forward(), n, true);
}
std::map<std::string, uintptr_t> symbols;
uintptr_t symbol(const char* name) { auto i = symbols.find(name); return i == symbols.end() ? 0 : i->second; }
int require(SkMod*, const char* const* names, int count)
{ int missing = 0; for (int i = 0; i < count; ++i) missing += !symbol(names[i]); return missing; }
void check(bool value, const char* message) { if (!value) { ++failures; std::printf("FAIL: %s\n", message); } }
} // namespace

namespace game {
void* cast(void* value, uint32_t type) { return value && type == 0x01186577 ? static_cast<uint8_t*>(value) + 0x40 : nullptr; }
uint32_t noun_id(void* value) { return value ? field<uint32_t>(value, 0x28) : 0; }
void destroy_creature(void* value)
{
    if (field<bool>(value, 0x20)) ++double_destroyed;
    field<bool>(value, 0x20) = true; ++destroyed;
}
} // namespace game

int main()
{
    spatial_vtable[0x38 / 4] = reinterpret_cast<void*>(&set_position);
    spatial_vtable[0x3C / 4] = reinterpret_cast<void*>(&set_orientation);
    field<uint8_t*>(mode.data(), 0x74) = data.data(); field<uint8_t*>(mode.data(), 0x78) = play.data();
    field<int>(mode.data(), 0xCC) = 2; field<int>(play.data(), 0xB8) = 0;
    field<uint8_t*>(data.data(), 0x10) = resource.data();
    *reverse() = {0, reverse_buckets, 1, 0}; *forward() = {0, forward_buckets, 1, 0};
    field<uint8_t*>(resource.data(), 0x2C10) = placement.data();
    field<uint8_t*>(resource.data(), 0x2C14) = placement.data() + placement.size();
    field<float>(marker, 0x20) = 1; field<uint8_t*>(marker, 0x28) = marker_act.data();
    field<uint8_t*>(marker, 0x2C) = marker_act.data() + marker_act.size();
    field<skg::ResourceKey>(cls.data(), 0) = {1, 2, 3};
    field<uint8_t*>(cls.data(), 0x70) = class_act.data();
    field<uint8_t*>(cls.data(), 0x74) = class_act.data() + class_act.size();
    symbols = {
        {"App::cScenarioMode::ptr", reinterpret_cast<uintptr_t>(&mode_pointer)},
        {"Simulator::cScenarioData::GetClass", reinterpret_cast<uintptr_t>(&get_class)},
        {"Simulator::cScenarioData::CreateObjectInstance", reinterpret_cast<uintptr_t>(&create)},
        {"SporeCoop::Adventure::ActorMapFind32", reinterpret_cast<uintptr_t>(&find32)},
        {"SporeCoop::Adventure::EraseObjectMarker", reinterpret_cast<uintptr_t>(&erase_reverse)},
        {"SporeCoop::Adventure::EraseMarkerObject", reinterpret_cast<uintptr_t>(&erase_forward)},
        {"SporeCoop::Adventure::ObjectMarkerSlot", reinterpret_cast<uintptr_t>(&reverse_slot)},
        {"SporeCoop::Adventure::MarkerObjectSlot", reinterpret_cast<uintptr_t>(&forward_slot)},
        {"SporeCoop::Adventure::AssignObjectReference", reinterpret_cast<uintptr_t>(&assign)},
        {"SporeCoop::Adventure::ApplyMarkerState", reinterpret_cast<uintptr_t>(&marker_state)},
        {"SporeCoop::Adventure::ApplyMarkerVisibility", reinterpret_cast<uintptr_t>(&marker_visibility)},
        {"SporeCoop::Adventure::RemoveMarkerObject", reinterpret_cast<uintptr_t>(&remove_marker)},
        {"Simulator::cScenarioData::GetObjectForMarker", reinterpret_cast<uintptr_t>(&get_object)},
        {"Simulator::cScenarioData::GetMarkerForObject", reinterpret_cast<uintptr_t>(&get_marker)},
        {"Simulator::cScenarioClass::IsConsumible", reinterpret_cast<uintptr_t>(&is_consumible)},
        {"SporeCoop::Adventure::CreateMarkerPowerup", reinterpret_cast<uintptr_t>(&powerup)}};
    SkHost host{}; host.sym = symbol; host.sym_require = require;
    check(adventure_world_lifecycle::init(&host, nullptr), "native capabilities resolve");
    std::string error; void* first = nullptr;
    net::Vec3 position{12, 24, 36}; net::Quat orientation{0, 0, 0, 1};
    auto recreate = [&](void** output = nullptr) {
        return adventure_world_lifecycle::recreate(data.data(), marker, 0, position, orientation, error, output);
    };
    check(recreate(&first), "create and bind absent actor");
    check(first && references[first] == 1 && created == 1 && states == 1 && visibility == 1, "native setup and intrusive reference");
    check(field<net::Vec3>(static_cast<uint8_t*>(first) + 0x40, 4).x == 12, "host transform follows native setup");
    check(!recreate() && created == 1, "reject conflicting live marker binding");
    void* extra = object(); insert(reverse(), static_cast<uint32_t>(reinterpret_cast<uintptr_t>(extra)))->value = nullptr;
    check(adventure_world_lifecycle::remove(data.data(), marker, extra, error), "remove extra simultaneous incarnation");
    check(get_object(nullptr, nullptr) == first && !get_marker(nullptr, extra), "extra removal preserves current forward binding");
    game::destroy_creature(first); unsigned before = destroyed;
    void* second = nullptr;
    check(recreate(&second), "replace mapping left by legacy generic DestroyInstance");
    check(destroyed == before && double_destroyed == 0 && references[first] == 0 && !get_marker(nullptr, first), "retired mappings release without a second destroy");
    check(adventure_world_lifecycle::remove(data.data(), marker, second, error), "native bound actor removal");
    check(!get_object(nullptr, nullptr) && references[second] == 0, "native removal clears maps and reference");
    consumible = true; void* item = nullptr;
    check(recreate(&item) && powerups == 1 && created == 2, "consumible uses its specialized native factory");
    check(adventure_world_lifecycle::remove(data.data(), marker, item, error), "specialized actor removal");
    consumible = false; void* primary = nullptr;
    check(recreate(&primary), "create primary before a distinct authoritative incarnation");
    void* additional = nullptr;
    net::Vec3 extra_position{100, 200, 300};
    auto extra_actor = [&](uint32_t noun) {
        return adventure_world_lifecycle::recreate(data.data(), marker, 0, extra_position, orientation, error, &additional, noun);
    };
    check(extra_actor(0x018EB45E), "create extra creature while another live actor owns the forward slot");
    check(additional != primary && state_target == additional && get_object(nullptr, nullptr) == primary && references[primary] == 1 &&
          get_marker(nullptr, additional) == marker && references[additional] == 0,
          "additional actor has reverse binding and preserves the primary intrusive reference");
    check(field<net::Vec3>(static_cast<uint8_t*>(additional) + 0x40, 4).x == 100 &&
          field<net::Vec3>(static_cast<uint8_t*>(primary) + 0x40, 4).x == 12,
          "native setup and host transform affect the additional actor only");
    check(adventure_world_lifecycle::remove(data.data(), marker, additional, error) && get_object(nullptr, nullptr) == primary,
          "removing an additional incarnation preserves the primary mapping");
    field<uint8_t*>(primary, 0x18) = nullptr;
    check(!extra_actor(0x018EB45E), "reject an aliased forward slot with mismatched marker metadata");
    field<uint8_t*>(primary, 0x18) = marker;
    auto* primary_reverse = lookup(reverse(), static_cast<uint32_t>(reinterpret_cast<uintptr_t>(primary)));
    primary_reverse->value = reinterpret_cast<void*>(1);
    check(!extra_actor(0x018EB45E), "reject an aliased forward slot with a different reverse placement");
    primary_reverse->value = nullptr;
    consumible = true;
    unsigned powerups_before = powerups;
    check(extra_actor(0x018EB45E) && powerups == powerups_before,
          "authoritative creature noun bypasses the class powerup factory");
    check(adventure_world_lifecycle::remove(data.data(), marker, additional, error), "remove additional creature with powerup class settings");
    check(extra_actor(0x018C88E4) && powerups == powerups_before + 1 && get_object(nullptr, nullptr) == primary,
          "same placement can also create its authoritative powerup noun");
    check(adventure_world_lifecycle::remove(data.data(), marker, additional, error), "remove additional powerup");
    check(!extra_actor(0x018C6DE8) && !additional && get_object(nullptr, nullptr) == primary,
          "factory noun mismatch is rejected without replacing the primary");
    check(adventure_world_lifecycle::remove(data.data(), marker, primary, error), "remove primary after additional incarnations");
    consumible = false; void* victim = nullptr; void* survivor = nullptr;
    check(recreate(&victim) && adventure_world_lifecycle::recreate(data.data(), marker, 0, extra_position, orientation,
                                                                 error, &survivor, 0x018EB45E),
          "create live primary and reverse-only sibling for roster cleanup");
    unsigned states_before = states, visibility_before = visibility;
    field<int>(survivor, 0x1C) = 1;
    check(!adventure_world_lifecycle::remove(data.data(), marker, victim, error, survivor) && get_object(nullptr, nullptr) == victim,
          "reject mismatched retained class before changing the forward slot");
    field<int>(survivor, 0x1C) = 0;
    auto* survivor_reverse = lookup(reverse(), static_cast<uint32_t>(reinterpret_cast<uintptr_t>(survivor)));
    survivor_reverse->value = reinterpret_cast<void*>(1);
    check(!adventure_world_lifecycle::remove(data.data(), marker, victim, error, survivor) &&
          references[victim] == 1 && references[survivor] == 0,
          "conflicting survivor reverse placement preserves both reference counts");
    survivor_reverse->value = nullptr;
    check(!adventure_world_lifecycle::remove(data.data(), marker, victim, error, victim), "reject retaining the victim itself");
    before = destroyed;
    check(adventure_world_lifecycle::remove(data.data(), marker, victim, error, survivor), "promote retained sibling when removing live primary");
    check(get_object(nullptr, nullptr) == survivor && get_marker(nullptr, survivor) == marker && !get_marker(nullptr, victim) &&
          references[victim] == 0 && references[survivor] == 1 && destroyed == before + 1,
          "promotion balances victim temporary reference and survivor map ownership");
    check(states == states_before && visibility == visibility_before &&
          field<net::Vec3>(static_cast<uint8_t*>(survivor) + 0x40, 4).x == 100,
          "retained sibling keeps its native state visibility and host transform");
    check(adventure_world_lifecycle::remove(data.data(), marker, survivor, error), "remove promoted final survivor");
    check(recreate(&victim) && adventure_world_lifecycle::recreate(data.data(), marker, 0, extra_position, orientation,
                                                                 error, &survivor, 0x018EB45E),
          "create retired primary promotion fixture");
    game::destroy_creature(victim); before = destroyed;
    check(adventure_world_lifecycle::remove(data.data(), marker, victim, error, survivor) && destroyed == before &&
          get_object(nullptr, nullptr) == survivor && references[victim] == 0 && references[survivor] == 1,
          "retired primary promotes the survivor without a second native destroy");
    check(adventure_world_lifecycle::remove(data.data(), marker, survivor, error), "clean final promoted fixture");
    field<int>(play.data(), 0xB8) = 1;
    check(!recreate(), "reject a queued update from an old act");
    field<int>(play.data(), 0xB8) = 0;
    forward()->count = 0;
    check(!recreate(), "reject invalid native map before native hash division");
    forward()->count = 1;
    check(double_destroyed == 0 && reverse()->size == 0 && forward()->size == 0, "no duplicate destruction or leftover bindings");
    std::printf("adventure lifecycle: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
