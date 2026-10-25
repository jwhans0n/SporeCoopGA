#include "adventure_world_lifecycle.h"

#include <cmath>
#include <limits>

#include "game.h"
#include "sporekit_game.hpp"

namespace adventure_world_lifecycle {
namespace {
using namespace skg;
const SkHost* H;
bool ready;
uintptr_t a_mode, a_class, a_create, a_find, a_erase_reverse, a_erase_forward;
uintptr_t a_reverse_slot, a_forward_slot, a_assign, a_marker_state, a_visibility;
uintptr_t a_remove, a_get_object, a_get_marker;
uintptr_t a_consumible, a_powerup;

struct Iterator { uint8_t* node = nullptr; uint8_t** bucket = nullptr; };
static_assert(sizeof(Iterator) == 8);
enum class Found { Invalid, Missing, Present };

bool context(uint8_t* data, uint8_t* marker, int* act, std::string& error)
{
    if (!ready) { error = "Native adventure actor lifecycle is unavailable for this executable."; return false; }
    auto* mode = *reinterpret_cast<uint8_t**>(a_mode);
    if (!mode || !readable(mode, 0xD0) || field<uint32_t>(mode, 0xCC) != 2 ||
        field<uint8_t*>(mode, 0x74) != data || !readable(data, 0xD0) || !readable(marker, 0x234)) {
        error = "The native adventure changed before its actor update."; return false;
    }
    if (marker == field<uint8_t*>(data, 0x8C) || field<int>(marker, 0) < 0) {
        error = "A captain cannot be replaced through the adventure actor lifecycle."; return false;
    }
    auto* resource = field<uint8_t*>(data, 0x10);
    if (!resource || !readable(resource, 0x2C28)) {
        error = "Native adventure placement data is unavailable."; return false;
    }
    uintptr_t begin = field<uintptr_t>(resource, 0x2C10), end = field<uintptr_t>(resource, 0x2C14);
    uint32_t id = field<uint32_t>(marker, 0x228);
    if (end < begin || (end - begin) % 0x238 || id >= (end - begin) / 0x238 ||
        reinterpret_cast<uintptr_t>(marker) != begin + uintptr_t(id) * 0x238 + 4) {
        error = "The adventure placement no longer matches its native marker."; return false;
    }
    if (act) {
        auto* play = field<uint8_t*>(mode, 0x78);
        if (!play || !readable(play, 0xBC) || *act < 0 || *act > 7 || field<int>(play, 0xB8) != *act) {
            error = "The adventure act changed before its actor update."; return false;
        }
    }
    return true;
}

Found find(uint8_t* map, uint32_t key, Iterator& result, std::string& error)
{
    if (!readable(map, 0x10)) { error = "Native adventure actor map is unavailable."; return Found::Invalid; }
    auto** buckets = field<uint8_t**>(map, 4);
    uint32_t count = field<uint32_t>(map, 8);
    if (!count || count > 0x100000 || !buckets || !readable(buckets, (size_t(count) + 1) * sizeof(void*)) ||
        !buckets[count]) {
        error = "Native adventure actor map has an invalid bucket table."; return Found::Invalid;
    }
    thiscall<Iterator*>(a_find, map, &result, &key);
    if (result.node == buckets[count]) return Found::Missing;
    uintptr_t bucket = reinterpret_cast<uintptr_t>(result.bucket), first = reinterpret_cast<uintptr_t>(buckets);
    if (!result.node || !readable(result.node, 12) || field<uint32_t>(result.node, 0) != key ||
        bucket < first || bucket >= first + size_t(count) * sizeof(void*) || (bucket - first) % sizeof(void*)) {
        error = "Native adventure actor map lookup returned an invalid entry."; return Found::Invalid;
    }
    return Found::Present;
}

void erase(uintptr_t method, uint8_t* map, const Iterator& entry)
{
    Iterator next;
    // Native iterator return uses an explicit hidden pointer, followed by the
    // iterator's two pointer words by value. Both erasers end with ret 0xC.
    thiscall<Iterator*>(method, map, &next, entry.node, entry.bucket);
}

bool unbind_retired(uint8_t* data, uint32_t id, void* object, std::string& error)
{
    Iterator reverse, forward;
    Found r = find(data + 0x90, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(object)), reverse, error);
    if (r == Found::Invalid) return false;
    if (r == Found::Present && field<uint32_t>(reverse.node, 4) != id) {
        error = "The retired actor belongs to a different native placement."; return false;
    }
    Found f = find(data + 0xB0, id, forward, error);
    if (f == Found::Invalid) return false;
    bool owns_forward = f == Found::Present && field<void*>(forward.node, 4) == object;
    if (r == Found::Present) erase(a_erase_reverse, data + 0x90, reverse);
    // This eraser releases the map's intrusive object reference. Do not read
    // object after this point: it may be its final reference.
    if (owns_forward) erase(a_erase_forward, data + 0xB0, forward);
    return true;
}
} // namespace

bool init(const SkHost* host, SkMod* mod)
{
    H = host;
    const char* names[] = {
        "App::cScenarioMode::ptr", "Simulator::cScenarioData::GetClass",
        "Simulator::cScenarioData::CreateObjectInstance", "SporeCoop::Adventure::ActorMapFind32",
        "SporeCoop::Adventure::EraseObjectMarker", "SporeCoop::Adventure::EraseMarkerObject",
        "SporeCoop::Adventure::ObjectMarkerSlot", "SporeCoop::Adventure::MarkerObjectSlot",
        "SporeCoop::Adventure::AssignObjectReference", "SporeCoop::Adventure::ApplyMarkerState",
        "SporeCoop::Adventure::ApplyMarkerVisibility", "SporeCoop::Adventure::RemoveMarkerObject",
        "Simulator::cScenarioData::GetObjectForMarker", "Simulator::cScenarioData::GetMarkerForObject",
        "Simulator::cScenarioClass::IsConsumible", "SporeCoop::Adventure::CreateMarkerPowerup"};
    ready = H && H->sym_require(mod, names, sizeof(names) / sizeof(names[0])) == 0;
    if (!ready) return false;
    uintptr_t* addresses[] = {&a_mode, &a_class, &a_create, &a_find, &a_erase_reverse, &a_erase_forward,
        &a_reverse_slot, &a_forward_slot, &a_assign, &a_marker_state, &a_visibility, &a_remove,
        &a_get_object, &a_get_marker, &a_consumible, &a_powerup};
    for (size_t i = 0; i < sizeof(names) / sizeof(names[0]); ++i) *addresses[i] = H->sym(names[i]);
    return true;
}

bool available() { return ready; }

bool remove(uint8_t* data, uint8_t* marker, void* object, std::string& error, void* retained)
{
    error.clear();
    if (!context(data, marker, nullptr, error) || !object || !readable(object, 0x28)) return false;
    if (field<uint8_t*>(object, 0x18) != marker || field<int>(object, 0x1C) != field<int>(marker, 0)) {
        error = "The native actor no longer belongs to this adventure placement."; return false;
    }
    uint32_t id = field<uint32_t>(marker, 0x228);
    Iterator forward;
    Found f = find(data + 0xB0, id, forward, error);
    if (f == Found::Invalid) return false;
    bool owns_forward = f == Found::Present && field<void*>(forward.node, 4) == object;
    if (retained == object) {
        error = "The retained native actor must differ from the actor being removed."; return false;
    }
    if (owns_forward && retained) {
        if (!readable(retained, 0x28) || field<bool>(retained, 0x20) || field<bool>(retained, 0x21) ||
            field<uint8_t*>(retained, 0x18) != marker || field<int>(retained, 0x1C) != field<int>(marker, 0)) {
            error = "The retained native actor does not match this live adventure placement."; return false;
        }
        Iterator old_reverse, kept_reverse;
        Found old = find(data + 0x90, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(object)), old_reverse, error);
        if (old == Found::Invalid) return false;
        Found kept = find(data + 0x90, static_cast<uint32_t>(reinterpret_cast<uintptr_t>(retained)), kept_reverse, error);
        if (kept == Found::Invalid) return false;
        if ((old == Found::Present && field<uint32_t>(old_reverse.node, 4) != id) ||
            kept != Found::Present || field<uint32_t>(kept_reverse.node, 4) != id ||
            thiscall<void*>(a_get_marker, data, retained) != marker) {
            error = "The retained native actor has a conflicting reverse placement binding."; return false;
        }
        // Removing the map's reference must not free the victim before its
        // reverse entry and noun-registry membership have been retired.
        void* held = nullptr;
        thiscall<void>(a_assign, &held, object);
        auto** slot = reinterpret_cast<void**>(forward.node + 4);
        thiscall<void>(a_assign, slot, retained);
        if (old == Found::Present) erase(a_erase_reverse, data + 0x90, old_reverse);
        if (!field<bool>(object, 0x20)) game::destroy_creature(object);
        thiscall<void>(a_assign, &held, (void*)nullptr);
        // Do not read the victim after releasing its final temporary reference.
        if (!readable(retained, 0x28) || field<bool>(retained, 0x20) || field<bool>(retained, 0x21) ||
            thiscall<void*>(a_get_object, data, marker) != retained ||
            thiscall<void*>(a_get_marker, data, retained) != marker) {
            error = "The retained native actor bindings did not agree after promotion."; return false;
        }
        return true;
    }
    if (field<bool>(object, 0x20)) return unbind_retired(data, id, object, error);
    if (owns_forward) {
        thiscall<void>(a_remove, data, marker);
        return true;
    }
    // Extra simultaneous instances can share a marker while the native forward
    // map names only one. Keep that other binding and retire only this instance.
    if (!unbind_retired(data, id, object, error)) return false;
    game::destroy_creature(object);
    return true;
}

bool recreate(uint8_t* data, uint8_t* marker, int act, const net::Vec3& position,
              const net::Quat& orientation, std::string& error, void** recreated, uint32_t expected_noun, uint8_t expected_powerup)
{
    error.clear();
    if (recreated) *recreated = nullptr;
    if (!context(data, marker, &act, error)) return false;
    float norm = orientation.x * orientation.x + orientation.y * orientation.y +
                 orientation.z * orientation.z + orientation.w * orientation.w;
    float scale = field<float>(marker, 0x20);
    if (!std::isfinite(position.x) || !std::isfinite(position.y) || !std::isfinite(position.z) ||
        !std::isfinite(norm) || norm < 1e-8f || !std::isfinite(scale) || scale < 0) {
        error = "The adventure actor has an invalid transform."; return false;
    }
    auto* cls = thiscall<uint8_t*>(a_class, data, field<int>(marker, 0));
    if (!cls || !readable(cls, 0x78)) { error = "The native adventure actor class is unavailable."; return false; }
    ResourceKey key = field<ResourceKey>(cls, 0);
    if (!key.instance || !key.type) { error = "The native adventure actor asset is unavailable."; return false; }
    auto* acts = field<uint8_t*>(marker, 0x28);
    uintptr_t first = reinterpret_cast<uintptr_t>(acts), end = field<uintptr_t>(marker, 0x2C);
    uintptr_t class_first = field<uintptr_t>(cls, 0x70), class_end = field<uintptr_t>(cls, 0x74);
    if (end < first || (end - first) % 0x34 || (end - first) / 0x34 > 8 ||
        (end > first && (!readable(acts, end - first) || size_t(act) >= (end - first) / 0x34)) ||
        class_end < class_first || (class_end - class_first) % 0x4E0 ||
        (class_end - class_first) / 0x4E0 > 8 || !readable(reinterpret_cast<void*>(class_first), class_end - class_first) ||
        size_t(act) >= (class_end - class_first) / 0x4E0) {
        error = "The native adventure actor has no data for the current act."; return false;
    }
    uint32_t id = field<uint32_t>(marker, 0x228);
    Iterator forward;
    Found f = find(data + 0xB0, id, forward, error);
    if (f == Found::Invalid) return false;
    void* previous = nullptr;
    if (f == Found::Present) {
        void* old = field<void*>(forward.node, 4);
        if (old && (!readable(old, 0x28) || !field<bool>(old, 0x20))) {
            if (!expected_noun) { error = "A live native actor already occupies this adventure placement."; return false; }
            if (!readable(old, 0x28) || field<bool>(old, 0x21) || field<uint8_t*>(old, 0x18) != marker ||
                field<int>(old, 0x1C) != field<int>(marker, 0) ||
                thiscall<void*>(a_get_object, data, marker) != old || thiscall<void*>(a_get_marker, data, old) != marker) {
                error = "The live native placement binding does not match its actor."; return false;
            }
            previous = old;
        }
        if (!previous) {
            if (old) { if (!unbind_retired(data, id, old, error)) return false; }
            else erase(a_erase_forward, data + 0xB0, forward);
        }
    }
    if (end == first) acts = nullptr;
    // Authored creatures can also have powerup settings. The host's concrete noun
    // distinguishes that creature from a separate powerup at the same placement.
    bool powerup_noun = expected_noun == 0 || expected_noun == 0x018C88E4 || expected_noun == 0x03A2511E;
    bool marker_powerup=powerup_noun && (!expected_powerup || expected_powerup==9) && thiscall<bool>(a_consumible, cls);
    void* object = marker_powerup ? cdecl_call<void*>(a_powerup, marker) :
        cdecl_call<void*>(a_create, &key, &position, &orientation, scale, false,
                         field<int>(marker, 0), marker, acts, act);
    if (!object || !readable(object, 0x28)) { error = "The native adventure actor could not be created."; return false; }
    if (expected_noun && game::noun_id(object) != expected_noun) {
        game::destroy_creature(object); error = "The native actor factory did not produce the requested noun."; return false;
    }
    field<int>(object, 0x1C) = field<int>(marker, 0);
    field<uint8_t*>(object, 0x18) = marker;
    uint32_t object_key = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(object));
    auto* reverse_value = thiscall<uint32_t*>(a_reverse_slot, data + 0x90, &object_key);
    if (!reverse_value || !readable(reverse_value, 4)) {
        game::destroy_creature(object); error = "The native actor reverse binding could not be created."; return false;
    }
    *reverse_value = id;
    auto** forward_value = thiscall<void**>(a_forward_slot, data + 0xB0, &id);
    if (!forward_value || !readable(forward_value, 4)) {
        Iterator entry;
        if (find(data + 0x90, object_key, entry, error) == Found::Present) erase(a_erase_reverse, data + 0x90, entry);
        game::destroy_creature(object); error = "The native actor placement binding could not be created."; return false;
    }
    // Marker-state setup looks up the forward slot rather than taking an actor.
    // Retain the old owner across this temporary assignment, then restore it.
    // The additional actor remains owned by the noun registry, with its own
    // reverse entry; a placement's forward map can name only one incarnation.
    void* retained = nullptr;
    if (previous) thiscall<void>(a_assign, &retained, previous);
    thiscall<void>(a_assign, forward_value, object);
    thiscall<void>(a_marker_state, data, marker, true);
    thiscall<void>(a_visibility, data, marker, object);
    void* spatial = game::cast(object, 0x01186577);
    if (spatial) {
        vcall<void>(spatial, 0x38 / 4, &position);
        vcall<void>(spatial, 0x3C / 4, &orientation);
    }
    if (previous) {
        thiscall<void>(a_assign, forward_value, retained);
        thiscall<void>(a_assign, &retained, (void*)nullptr);
    }
    if (thiscall<void*>(a_get_object, data, marker) != (previous ? previous : object) ||
        thiscall<void*>(a_get_marker, data, object) != marker) {
        std::string cleanup;
        remove(data, marker, object, cleanup);
        error = "The native adventure actor bindings did not agree after creation."; return false;
    }
    if (recreated) *recreated = object;
    return true;
}
} // namespace adventure_world_lifecycle
