// Exercises the actual world reconciliation pipeline with a fake native registry.
// Includes the implementation to test its queued roster and snapshot state directly.
#include <array>
#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>
#include <vector>

#include "../mods/coop/adventure_world.cpp"

// Explosive ownership is exercised by its own native ABI fixture. This mirror
// fixture leaves that adapter uninitialized while covering world callbacks.
namespace adventure_explosives { void picked(Identity, uint8_t) {} }

namespace {
using skg::field;
struct Object {
    std::array<uint8_t, 0xE80> bytes{};
    std::array<uint8_t, 0x500> spatial{};
    std::array<uint8_t, 0x84> combatant{};
    std::array<uint8_t, 0x3C> powerup{};
    uint32_t noun;
    bool has_combatant = false, is_creature = false;
    bool is_vehicle = false, is_building = false, is_ornament = false, is_interactive = false;
    bool model_dirty = false;
    net::Vec3 model_position{};
};
std::vector<std::unique_ptr<Object>> objects;
std::array<uint8_t, 0xD0> fake_mode{}, fake_data{};
std::array<uint8_t, 0x110> fake_play{};
std::array<uint8_t, 0x14C> fake_display{};
std::array<uint8_t, 0xB4> fake_hud{};
std::array<uint8_t, 3 * 0x1AC> hud_goals{};
std::array<uint8_t, 0x2C18> fake_resource{};
std::array<uint8_t, 3 * 0x238> placements{};
uint8_t* mode_pointer = fake_mode.data();
void* spatial_vtable[64]{};
void* combatant_vtable[24]{};
bool capture_motion = false, native_moving = false;
unsigned motion_moves = 0, motion_stops = 0;
net::Vec3 motion_target;
float motion_speed = 0;
std::vector<char> motion_order;
unsigned pose_sets;
unsigned transform_sets;
unsigned creates;
int failures;
unsigned native_terminals, native_other_states;
unsigned goal_act_sets;
bool reentrant_result;
void* teleported;
unsigned relation_calls;
unsigned hud_calls;
uint32_t expected_hud_count = 1;
unsigned native_befriends, native_allies, relationship_events, recruit_calls;
unsigned npc_clips;
uint32_t last_npc_clip;
int last_npc_block;
bool last_npc_blend;
unsigned captured_party_queries;
unsigned item_picks, item_drops;
unsigned native_actor_hits;
unsigned native_throw_impulses, native_throw_graces, native_throw_sources;
net::Vec3 native_throw_vector;
bool native_throw_add;
bool capture_world_actions;
std::vector<char> world_action_order;
bool item_receive_allowed = true;
bool capture_party_query;
void* expected_party_actor;
int native_social_mode = 2;
bool recruitment_accepted = true;
std::vector<std::pair<SkTaskFn, void*>> queued;
std::array<uint8_t, 0x1AC> social_goal{};
net::Vec3 teleported_position;
net::Quat teleported_orientation;
void check(bool ok, const char* message) { if (!ok) { ++failures; std::printf("FAIL: %s\n", message); } }
Object* lookup(void* p) { for (auto& o : objects) if (o->bytes.data() == p) return o.get(); return nullptr; }
void* create(uint32_t marker, uint32_t noun)
{
    auto value = std::make_unique<Object>(); value->noun = noun;
    auto* object = value->bytes.data();
    field<void**>(value->spatial.data(), 0) = spatial_vtable;
    field<void**>(value->combatant.data(), 0) = combatant_vtable;
    field<uint8_t*>(object, 0x18) = placements.data() + marker * 0x238 + 4;
    field<int>(object, 0x1C) = static_cast<int>(marker);
    field<uint32_t>(object, 0x24) = static_cast<uint32_t>(objects.size() + 10);
    objects.push_back(std::move(value)); return object;
}
void SK_THISCALL position(void* spatial, const net::Vec3* p) { ++pose_sets; field<net::Vec3>(spatial, 4) = *p; }
void SK_THISCALL orientation(void* spatial, const net::Quat* q) { field<net::Quat>(spatial, 0x10) = *q; }
void SK_THISCALL transform(void* spatial, const net::Vec3* p, const net::Quat* q)
{
    ++transform_sets; position(spatial,p); orientation(spatial,q);
    for (auto& object : objects) if (object->spatial.data() == spatial) object->model_dirty = true;
}
void render_models()
{
    for (auto& object : objects) if (object->model_dirty) {
        object->model_position = field<net::Vec3>(object->spatial.data(),4); object->model_dirty = false;
    }
}
void* SK_THISCALL spatial_game_data(void* spatial,uint32_t type)
{
    if(type!=0x017F243B) return nullptr;
    for(auto& object:objects) if(object->spatial.data()==spatial) return object->bytes.data();
    return nullptr;
}
void* SK_THISCALL bounds(void* spatial,void* output)
{
    auto p=field<net::Vec3>(spatial,4);
    auto* values=static_cast<net::Vec3*>(output);
    values[0]={p.x-0.25f,p.y-0.25f,p.z-0.25f};
    values[1]={p.x+0.25f,p.y+0.25f,p.z+0.25f}; return output;
}
bool SK_THISCALL item_pick(void* holder,void* spatial,int index,bool)
{
    auto* first=field<uint8_t*>(holder,0xE40); auto* last=field<uint8_t*>(holder,0xE44);
    if(index<0 || first+index*0x24>=last) return false;
    field<void*>(first+index*0x24,0x20)=spatial; ++item_picks;
    if(capture_world_actions) world_action_order.push_back('P');
    return true;
}
bool SK_THISCALL item_drop(void* holder,void* spatial,int,bool)
{
    int index=adventure_world::item_slot(holder,spatial,false); if(index<0) return false;
    field<void*>(field<uint8_t*>(holder,0xE40)+index*0x24,0x20)=nullptr; ++item_drops;
    if(capture_world_actions) world_action_order.push_back('D');
    return true;
}
int SK_THISCALL native_actor_damage(void* combatant,float amount,uint32_t,int type,const net::Vec3*,void*)
{
    void* target = nullptr;
    for(auto& object:objects) if(object->combatant.data()==combatant) target=object->bytes.data();
    // Match the real hook's hold check: invoking native damage while paused
    // would reject it, even if a caller had already consumed its event serial.
    if(!adventure_world::damage_filter(target,amount,type,nullptr)) return 0;
    field<float>(combatant,0x38)-=amount; ++native_actor_hits;
    if(capture_world_actions) world_action_order.push_back('H');
    return 1;
}
void* SK_CDECL native_physics() { return &native_throw_impulses; }
void SK_THISCALL native_throw_impulse(void* self,void*,const net::Vec3* impulse,bool add)
{
    check(self==&native_throw_impulses,"retained throw uses the native physics singleton as this");
    ++native_throw_impulses; native_throw_vector=*impulse; native_throw_add=add;
    if(capture_world_actions) world_action_order.push_back('T');
}
void SK_THISCALL native_throw_grace(void* self,void* captain,void* item,float seconds)
{
    check(self==&native_throw_impulses&&captain&&item&&seconds==.5f,"retained throw preserves native owner/item grace and its float ABI");
    ++native_throw_graces;
}
float SK_THISCALL maximum_hp(void* combatant) { return field<float>(combatant, 0x30); }
void* SK_THISCALL get_marker(void*, void*) { return nullptr; }
bool SK_THISCALL hostile_relation(void*, void*) { ++relation_calls; return true; }
void SK_THISCALL refresh_hud(void* self, bool initial)
{
    ++hud_calls;
    check(self == fake_hud.data() && !initial, "native HUD refresh receives the correct this pointer and one false stack argument");
    check(hud_goals[0] && field<uint32_t>(hud_goals.data(), 4) == expected_hud_count &&
          field<uint32_t>(hud_goals.data() + 0x1AC, 4) == 2,
          "native HUD observes the complete authoritative goal batch before repainting");
}
void SK_THISCALL result_state(void* self, int state)
{
    if (state == 5 || state == 6) ++native_terminals;
    else ++native_other_states;
    field<int>(self, 0x90) = state;
    if (reentrant_result && (state == 5 || state == 6)) {
        reentrant_result = false;
        adventure_world::set_state(self, state == 5 ? 6 : 5);
    }
}
void SK_THISCALL goal_act(void* self, int act, bool)
{
    ++goal_act_sets;
    field<int>(self, 0xB8) = act;
    // A native act change replaces the state a still-open dialog references.
    field<void*>(self, 0xC8) = nullptr; field<void*>(self, 0xCC) = nullptr;
}
void output(SkOut*, const char*, ...) {}
void queue(SkTaskFn function, void* argument) { queued.emplace_back(function, argument); }
void drain()
{
    auto tasks = std::move(queued); queued.clear();
    for (auto [function, argument] : tasks) function(argument);
}
int SK_THISCALL social_mode(void*) { return native_social_mode; }
void* SK_CDECL relationships() { return &relationship_events; }
void SK_THISCALL relationship_event(void* manager, int type, void* key, void* actor)
{
    check(manager == &relationship_events && type == 3 && key == static_cast<uint8_t*>(actor) + 0xB28,
          "native relationship event uses the authored target's real species key");
    ++relationship_events;
}
void SK_THISCALL native_befriend(void*, void* actor)
{
    ++native_befriends;
    if (adventure_world::host() && field<int>(social_goal.data(), 0x24) == 4 &&
        field<int>(social_goal.data(), 0x28) == field<int>(actor, 0x1C)) ++field<int>(social_goal.data(), 4);
}
void SK_THISCALL native_ally(void*, void* actor)
{
    ++native_allies;
    if (adventure_world::host() && field<int>(social_goal.data(), 0x24) == 5 &&
        field<int>(social_goal.data(), 0x28) == field<int>(actor, 0x1C)) ++field<int>(social_goal.data(), 4);
}
}

namespace game {
void for_each_object(bool (*fn)(void*, uint32_t, void*), void* user)
{ for (auto& object : objects) if (!fn(object->bytes.data(), object->noun, user)) break; }
bool object_alive(void* p) { return p && !field<bool>(p, 0x21); }
void* cast(void* p, uint32_t type)
{
    auto* o = lookup(p);
    if (!o) return nullptr;
    if (type == 0x01186577) return o->spatial.data();
    if (type == 0x013F94D4 && o->has_combatant) return o->combatant.data();
    if (type == 0xCE9F6639 && o->is_creature) return p;
    if (type == 0x0137E8E0 && o->is_vehicle) return p;
    if (type == 0x0E9CB8BA && o->is_building) return p;
    if (type == 0x0175CDC9 && o->is_ornament) return p;
    if (type == 0x03A25119 && o->is_interactive) return p;
    if (type == 0x08367427 && (o->is_ornament || o->is_interactive)) return o->powerup.data();
    return nullptr;
}
void* avatar() { return nullptr; }
void* npc_ai_subject() { return nullptr; }
bool is_dead(void* object) { return field<bool>(object, kDeadOff); }
float health(void* object) { return field<float>(object, kHpOff); }
bool is_puppet(void*) { return false; }
uint32_t mode_id() { return 0x01654C10; }
CreatureUpdateCounts creature_update_counts() { return {}; }
void play_animation(void* object, uint32_t animation, int block, bool disable_blend)
{
    ++npc_clips; last_npc_clip = animation; last_npc_block = block; last_npc_blend = disable_blend;
    check(!adventure_world::g_applying && adventure_world::g_visual_animation,
          "NPC visual replay does not grant the broad gameplay-application bypass");
    auto sequence = adventure_world::g_hit_sequence;
    float damage = 10;
    void* attacker = adventure_world::C.captain_of(adventure_world::C.sess->my_id());
    check(!adventure_world::damage_filter(object, damage, 0, attacker) &&
          adventure_world::g_hit_sequence == sequence,
          "incidental visual damage is rejected before guest hit forwarding or HP authority");
}
bool object_position(void* p, net::Vec3& result)
{ auto* o = lookup(p); if (!o) return false; result = field<net::Vec3>(o->spatial.data(), 4); return true; }
net::Quat orientation(void*) { return {0, 0, 0, 1}; }
float scale(void*) { return 1.f; }
float standard_speed(void*) { return 4.f; }
void move_to(void* object, const net::Vec3& target, float radius)
{
    if (!capture_motion) return;
    check(radius <= .1f, "native waypoint uses a small stop radius instead of falling idle at every packet");
    ++motion_moves; motion_order.push_back('M'); motion_target = target;
    motion_speed = 4.f; native_moving = true; // dc04 MoveTo restores standard speed.
    if (auto* value=lookup(object)) field<int>(value->spatial.data(),0x250)=1;
}
void set_speed(void*, float speed)
{
    if (!capture_motion) return;
    motion_order.push_back('S'); motion_speed = speed;
}
void stop(void*)
{
    if (!capture_motion) return;
    ++motion_stops; motion_order.push_back('T'); native_moving = false;
}

void teleport(void* object, const net::Vec3& position, const net::Quat& orientation)
{ teleported = object; teleported_position = position; teleported_orientation = orientation; }
}

namespace adventure_world_lifecycle {
bool init(const SkHost*, SkMod*) { return true; }
bool available() { return true; }
bool remove(uint8_t*, uint8_t*, void* object, std::string&, void*) { field<bool>(object, 0x20) = true; return true; }
bool recreate(uint8_t*, uint8_t* marker, int, const net::Vec3&, const net::Quat&, std::string&, void** output, uint32_t expected_noun, uint8_t)
{
    ++creates;
    uint32_t id = field<uint32_t>(marker, 0x228);
    check(expected_noun == (id == 1 ? 102u : 101u), "the host's noun selects the requested native incarnation factory");
    *output = create(id, expected_noun);
    return true;
}
}

namespace adventure_world_actions {
bool init(const SkHost*, SkMod*, Ctx) { return true; }
void poll_receive(void*) {}
bool can_receive(void*,void*,bool) { return item_receive_allowed; }
void throw_source(void*,void*) { ++native_throw_sources; }
bool give(void*, void*, void*, std::string&, GiveResult*) { return false; }
bool recruit(void* actor, std::string&) {
    ++recruit_calls;
    if (recruitment_accepted) {
        field<uint32_t>(actor, 0xB58) |= 0x100;
        adventure_world::allied(fake_play.data(), actor);
    }
    return recruitment_accepted;
}
bool hostile(void*, void*, int, bool& result, std::string&) { result = false; return true; }
}
namespace adventure_world_vehicles {
bool init(const SkHost*, SkMod*, Ctx) { return true; }
Counters counters() { return {}; }
void reset() {}
}
namespace adventure_relationships {
bool query_local_party(void* manager, const void* key, void* actor, int& value)
{
    if (!capture_party_query) return false;
    ++captured_party_queries;
    check(manager == &relationship_events && actor == expected_party_actor &&
          key == static_cast<uint8_t*>(actor) + 0xB28,
          "host snapshots query the actual NPC species key through the native relationship adapter");
    value = 6; return true;
}
}

static void static_replication_cases(net::Session& session, net::Session& client)
{
    using namespace adventure_world;
    auto saved_native = g_native;
    auto saved_remote = g_remote;
    auto saved_bindings = g_bindings;
    auto saved_retired = g_retired_items;
    auto saved_host = g_static_host;
    auto saved_sent = g_static_sent;
    auto saved_cursor = g_static_cursor;
    auto saved_pending = g_static_pending;
    auto saved_stats = g_replication_stats;
    auto saved_live = g_roster_live;
    uint32_t saved_sequence = g_sequence, saved_roster = g_roster_sequence;
    int saved_act = g_static_act;
    auto* saved_session = C.sess;
    g_static_host.clear(); g_static_sent.clear(); g_static_cursor.clear(); g_remote.clear(); g_bindings.clear(); g_retired_items.clear();
    for (auto& pending : g_static_pending) pending.clear();
    g_roster_sequence = 0; g_roster_live.clear(); g_sequence = 0; g_static_act = -1; g_replication_stats = {};
    void* building = create(0, 101); auto* b = lookup(building); b->is_building = b->has_combatant = true;
    field<float>(b->combatant.data(), 0x30) = 100; field<float>(b->combatant.data(), 0x38) = 25;
    field<float>(b->combatant.data(), 0x2C) = 1; field<net::Quat>(b->spatial.data(), 0x10) = {0, 0, 0, 1};
    void* prop = create(0, 101); lookup(prop)->is_ornament = true;
    void* creature = create(0, 101); lookup(creature)->is_creature = lookup(creature)->is_ornament = true;
    void* vehicle = create(0, 101); lookup(vehicle)->is_vehicle = lookup(vehicle)->is_ornament = true;
    void* grenade = create(0, 101); lookup(grenade)->is_interactive = true;
    field<int>(lookup(grenade)->powerup.data(), 4) = 13;
    void* bomb = create(0, 101); lookup(bomb)->is_interactive = true;
    field<int>(lookup(bomb)->powerup.data(), 4) = 10;
    void* unknown = create(0, 101);
    enumerate();
    auto native = [&](void* object) { auto* actor = actor_for(object); return actor ? *actor : NativeActor{}; };
    check(native(building).static_candidate && native(prop).static_candidate,
          "verified native building and ornament casts select the stationary path");
    check(!native(creature).static_candidate && !native(vehicle).static_candidate && !native(unknown).static_candidate &&
          !native(grenade).static_candidate && !native(bomb).static_candidate,
          "runtime creatures, vehicles, unknown classes, grenades and timed bombs keep legacy motion updates");
    std::array<uint8_t, 0x24> held_slot{};
    void* captain = C.captain_of ? C.captain_of(0) : nullptr;
    if (captain) {
        auto* first = field<uint8_t*>(captain, 0xE40); auto* last = field<uint8_t*>(captain, 0xE44);
        field<void*>(held_slot.data(), 0x20) = lookup(prop)->spatial.data();
        field<uint8_t*>(captain, 0xE40) = held_slot.data(); field<uint8_t*>(captain, 0xE44) = held_slot.data() + held_slot.size();
        enumerate(); check(!native(prop).static_candidate, "a held prop stays on the dynamic path before its first changed pose");
        field<uint8_t*>(captain, 0xE40) = first; field<uint8_t*>(captain, 0xE44) = last;
    }
    field<bool>(lookup(prop)->powerup.data(), 0x30) = true; enumerate();
    check(!native(prop).static_candidate, "native thrown objects cannot be classified as stationary");
    field<bool>(lookup(prop)->powerup.data(), 0x30) = false; enumerate();
    g_native = {native(building), native(creature), native(unknown)};
    std::vector<std::vector<uint8_t>> packets;
    client.cb.custom = [&](uint8_t type, uint8_t from, net::Reader& reader) {
        if (type == net::MSG_ADVENTURE && !from && reader.left() && (*reader.cur() == Actors || *reader.cur() == StaticActors))
            packets.emplace_back(reader.cur(), reader.cur() + reader.left());
    };
    auto pump = [&] {
        for (int i = 0; i < 1000; ++i) {
            session.update(); client.update();
            if (!session.pending_to(client.my_id()) && i >= 2) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(false, "static fixture reliable queue drains");
    };
    auto static_packets = [&] {
        std::vector<std::vector<uint8_t>> result;
        for (const auto& packet : packets) if (packet[0] == StaticActors) result.push_back(packet);
        return result;
    };
    C.sess = &session; send_actors(); pump();
    auto initial = static_packets();
    check(initial.size() == 1 && initial[0].size() == 70 && g_replication_stats.baselines == 1 &&
          g_replication_stats.static_actors == 1 && g_replication_stats.dynamic_actors == 2,
          "first stationary observation sends one complete reliable reconstruction baseline alongside legacy dynamic actors");
    uint64_t static_before = g_replication_stats.static_bytes, dynamic_before = g_replication_stats.dynamic_bytes;
    for (int i = 0; i < 10; ++i) send_actors(); pump();
    check(g_replication_stats.static_bytes == static_before && g_replication_stats.dynamic_bytes > dynamic_before,
          "unchanged stationary objects use no recurring actor payload while creatures and unknown actors keep every dynamic tick");
    field<net::Quat>(b->spatial.data(), 0x10) = {0, 0, 0, -1}; send_actors(); pump();
    check(g_replication_stats.promotions == 0 && g_replication_stats.static_bytes == static_before,
          "equivalent quaternion signs do not classify a stationary building as moving");
    field<float>(b->combatant.data(), 0x38) = 20; packets.clear(); send_actors(); pump();
    auto changed = static_packets();
    check(changed.size() == 1 && changed[0].size() == 42 && g_replication_stats.state_updates == 1,
          "building health changes send a compact reliable state record with no position or quaternion");
    auto decode = [&](const std::vector<uint8_t>& packet, uint8_t sender = 0) {
        auto* previous = C.sess; C.sess = &client;
        net::Reader reader(packet.data(), packet.size()); on_message(sender, reader); C.sess = previous;
    };
    decode(initial[0]); decode(changed[0]);
    Identity building_id{0, field<uint32_t>(building, 0x24)};
    check(g_remote.count(building_id) && g_remote.at(building_id).health == 20 && g_remote.at(building_id).static_actor &&
          g_remote.at(building_id).orientation.w == 1,
          "state-only changes merge into the reliable cached transform rather than overwriting it");
    ActorState target = g_remote.at(building_id); target.flags = 0;
    NativeActor local = native(building); local.combatant = nullptr;
    field<net::Quat>(b->spatial.data(), 0x10) = {0, 0, 0, 1}; unsigned sets = pose_sets;
    apply_actor(local, target); apply_actor(local, target);
    check(pose_sets == sets, "cached stationary snapshots avoid redundant native transform setters");
    field<net::Vec3>(b->spatial.data(), 4) = {2, 0, 0}; apply_actor(local, target);
    check(pose_sets == sets + 1 && field<net::Vec3>(b->spatial.data(), 4).x == 0,
          "a repaired or drifting guest building receives its cached authoritative transform");

    // Fill the existing ordered connection, then change HP twice. No new static
    // messages queue while blocked; the next opportunity carries only latest HP.
    net::Writer filler; filler.u8(255); put_run(filler, 0);
    while (session.pending_to(client.my_id()) < 64) session.send_reliable_to(client.my_id(), net::MSG_ADVENTURE, filler);
    static_before = g_replication_stats.static_bytes;
    field<float>(b->combatant.data(), 0x38) = 15; send_actors();
    field<float>(b->combatant.data(), 0x38) = 12; send_actors();
    check(session.pending_to(client.my_id()) == 64 && g_replication_stats.static_bytes == static_before &&
          g_replication_stats.deferred >= 2, "static transfer honors queue bounds and coalesces changes while the peer is congested");
    pump(); packets.clear(); send_actors(); pump();
    auto coalesced = static_packets();
    check(coalesced.size() == 1, "one state packet catches up after congestion without replaying intermediate health");
    if (!coalesced.empty()) decode(coalesced[0]);
    check(g_remote.at(building_id).health == 12, "coalesced state carries the most recent absolute HP");
    packets.clear(); peer_changed(client.my_id()); send_actors(); pump();
    check(static_packets().size() == 1 && static_packets()[0].size() == 70,
          "a reconnecting or reused peer receives a fresh complete baseline");

    field<net::Vec3>(b->spatial.data(), 4) = {0.00005f, 0, 0}; send_actors();
    field<net::Vec3>(b->spatial.data(), 4) = {0.00009f, 0, 0}; send_actors();
    check(g_replication_stats.promotions == 0, "sub-tolerance movement stays stationary until its accumulated displacement matters");
    field<net::Vec3>(b->spatial.data(), 4) = {0.0002f, 0, 0}; packets.clear(); send_actors(); pump();
    auto promoted = static_packets();
    check(g_replication_stats.promotions == 1 && g_replication_stats.dynamic_actors == 3 &&
          promoted.size() == 1 && promoted[0].size() == 70,
          "a moving building gets one reliable full correction and immediately joins the unchanged dynamic stream");
    if (!promoted.empty()) decode(promoted[0]);
    check(!g_remote.at(building_id).static_actor, "promotion clears the guest stationary classification");
    field<net::Vec3>(b->spatial.data(), 4) = {0, 0, 0}; send_actors(); pump();
    check(g_replication_stats.promotions == 1 && g_replication_stats.dynamic_actors == 3,
          "promotion lasts for the current act and incarnation even if the object returns to its original pose");

    auto packet = [&](uint32_t sequence, uint8_t act, uint8_t fields, ActorState state, bool malformed = false) {
        net::Writer w; w.u8(StaticActors); put_run(w, g_run); w.u32(sequence); w.u8(act); w.u8(malformed ? 2 : 1);
        auto record = [&](bool bad) {
            write_identity(w, state.id); w.u8(fields); w.u32(state.noun); if (fields) write_pose(w, state);
            if (bad) state.flags |= 8; write_gameplay(w, state);
        };
        record(false); if (malformed) record(true); return w.data;
    };
    ActorState future; future.id = {2, 7777}; future.noun = 101; future.position = {4, 5, 6};
    future.orientation = {0, 0, 0, 1}; future.health = 30; future.maximum = 100; future.max_tuning = 1; future.flags = 1;
    decode(packet(500, 1, 1, future), 1);
    check(g_static_pending[1].empty(), "only the host can seed future static baselines");
    decode(packet(500, 1, 1, future, true));
    check(g_static_pending[1].empty(), "a malformed later record rejects the entire static chunk before mutation");
    auto truncated = packet(500, 1, 1, future); truncated.pop_back(); decode(truncated);
    check(g_static_pending[1].empty(), "a truncated static baseline cannot partially seed a future act");
    decode(packet(500, 1, 0, future));
    check(g_static_pending[1].empty(), "state-only updates cannot create an actor without a reconstruction baseline");
    decode(packet(500, 1, 1, future)); future.health = 10; decode(packet(501, 1, 0, future));
    check(!g_remote.count(future.id) && g_static_pending[1].at(future.id).health == 10,
          "one-shot reliable baselines and later state merge while their native act change is still queued");
    field<int>(fake_play.data(), 0xB8) = 1; apply_pending_static(1);
    check(g_static_pending[1].empty() && g_remote.at(future.id).health == 10 && g_remote.at(future.id).position.x == 4,
          "native act completion installs the staged full cached state");
    future.position.x = 8; auto latest = future; latest.sequence = 600; latest.static_actor = false;
    merge_actor_state(latest, g_remote, true, true); future.position.x = 1; decode(packet(599, 1, 1, future));
    check(g_remote.at(future.id).position.x == 8 && !g_remote.at(future.id).static_actor,
          "delayed reliable static state cannot rewind a newer dynamic pose after promotion");
    g_retired_items.insert(future.id); future.position.x = 99; decode(packet(601, 1, 1, future));
    check(g_remote.at(future.id).position.x == 8, "consumed-item tombstones reject delayed static baselines");
    g_retired_items.erase(future.id);
    for (uint32_t i = 0; i < kMaxActors; ++i) {
        auto entry = future; entry.id.incarnation = 9000 + i; entry.sequence = 0;
        g_static_pending[2].emplace(entry.id, entry);
    }
    decode(packet(700, 2, 1, future));
    check(g_static_pending[2].size() == kMaxActors && !g_static_pending[2].count(future.id),
          "future-act baselines obey the same actor capacity bound as the live cache");
    auto existing = future; existing.id.incarnation = 9000; existing.health = 7;
    decode(packet(701, 2, 0, existing));
    check(g_static_pending[2].at(existing.id).health == 7,
          "existing static state continues updating even when a future-act cache is at capacity");
    field<int>(fake_play.data(), 0xB8) = 0;
    C.sess = &session; send_actors();
    field<int>(fake_play.data(), 0xB8) = 1; send_actors(); pump();
    check(g_replication_stats.static_actors == 1 && g_replication_stats.dynamic_actors == 2,
          "an act change begins fresh static classification and peer baselines");
    ++field<uint32_t>(building, 0x24); ++g_native[0].incarnation;
    send_actors(); pump();
    Identity respawned{0, field<uint32_t>(building, 0x24)};
    check(g_static_host.count(respawned) && !g_static_host.count(building_id) &&
          g_static_sent[client.my_id()].count(respawned) && !g_static_sent[client.my_id()].count(building_id),
          "respawned static incarnations receive fresh baselines and prune retired sender shadows");
    g_static_host.clear(); g_static_sent.clear();
    for (uint32_t i = 0; i < 80; ++i) {
        auto state = future; state.id.incarnation = 12000 + i;
        g_static_host.emplace(state.id, StaticHostActor{state, state, false});
    }
    send_static_actors(1);
    check(g_static_sent[client.my_id()].size() == 48 && session.pending_to(client.my_id()) == 4,
          "one tick queues at most four bounded static baseline chunks for each peer");
    pump();
    size_t dirtied = 0;
    for (auto& [id, entry] : g_static_host) {
        (void)id;
        if (dirtied++ < 48) entry.state.health = 17;
    }
    send_static_actors(1); pump();
    check(g_static_sent[client.my_id()].size() == 80,
          "later baseline identities progress even when the earliest 48 actors all change again");
    Identity tail{future.id.marker, 12079};
    g_static_host.at(tail).state.health = 3;
    for (int tick = 0; tick < 3; ++tick) {
        size_t early = 0;
        for (auto& [id, entry] : g_static_host) {
            (void)id;
            if (early++ < 48) entry.state.health = float(18 + tick);
        }
        send_static_actors(1); pump();
    }
    check(g_static_sent[client.my_id()].at(tail).state.health == 3,
          "a later actor's state change is delivered while early actors remain continuously dirty");
    // Reproduce the real receive-pump ordering: K_COMMIT makes C.run current,
    // four reliable initial chunks arrive, then the first world tick executes.
    // The sender already considers all 48 records sent and will not repeat them.
    packets.clear(); g_static_host.clear(); g_static_sent.clear();
    field<int>(fake_play.data(), 0xB8) = 0;
    for (uint32_t i = 0; i < 48; ++i) {
        auto state = future; state.id.incarnation = 16000 + i;
        g_static_host.emplace(state.id, StaticHostActor{state, state, false});
    }
    C.sess = &session; send_static_actors(0); pump();
    auto before_first_tick = static_packets();
    check(before_first_tick.size() == 4 && g_static_sent[client.my_id()].size() == 48,
          "the host queues all 48 one-shot initial static baselines and retains their sent shadows");
    bool saved_mirror_queued = g_mirror_queued;
    reset_replication(); C.sess = &client;
    check(g_run == 0, "the guest has not initialized its world run before the first reliable baseline");
    for (const auto& bytes : before_first_tick) decode(bytes);
    check(g_run == run() && g_remote.size() == 48,
          "early reliable static baselines initialize the current run before caching all four chunks");
    g_mirror_queued = true; // Keep this ordering test independent of native actor factories.
    tick(30); tick(30.1);
    check(g_remote.size() == 48 && g_static_pending[0].empty(),
          "the first and following world ticks preserve every already acknowledged static baseline");
    for (bool pickup : {false, true}) {
        reset_replication();
        auto retired = future; retired.id.incarnation = pickup ? 17001 : 17000;
        if (pickup) pickups_context().retire({retired.id.marker, retired.id.incarnation});
        else explosives_context().retire({retired.id.marker, retired.id.incarnation});
        check(g_run == run() && g_retired_items.count(retired.id),
              "an initial routed pickup or explosion retirement establishes the world run before its tombstone");
        tick(31); tick(31.1);
        check(g_retired_items.count(retired.id), "the first world ticks retain an event's early exact-identity tombstone");
        decode(packet(700, 0, 1, retired));
        decode(packet(701, 1, 1, retired));
        net::Writer dynamic; dynamic.u8(Actors); put_run(dynamic, g_run); dynamic.u32(702); dynamic.u8(0); dynamic.u8(1);
        write_identity(dynamic, retired.id); dynamic.u32(retired.noun); write_pose(dynamic, retired); write_gameplay(dynamic, retired);
        decode(dynamic.data);
        check(!g_remote.count(retired.id) && !g_static_pending[1].count(retired.id) && g_bindings.empty(),
              "delayed full dynamic, static and future-act states cannot recreate an early retired identity");
        auto respawn = retired; ++respawn.id.incarnation;
        decode(packet(703, 0, 1, respawn));
        check(g_remote.count(respawn.id) && !g_retired_items.count(respawn.id),
              "an early retirement leaves a newer same-marker incarnation eligible for its own baseline");
    }
    g_mirror_queued = saved_mirror_queued;
    for (void* object : {building, prop, creature, vehicle, grenade, bomb, unknown}) field<bool>(object, 0x21) = true;
    field<int>(fake_play.data(), 0xB8) = 0;
    client.cb.custom = {}; C.sess = saved_session;
    g_native = std::move(saved_native); g_remote = std::move(saved_remote); g_bindings = std::move(saved_bindings);
    g_retired_items = std::move(saved_retired); g_static_host = std::move(saved_host); g_static_sent = std::move(saved_sent);
    g_static_cursor = std::move(saved_cursor);
    g_static_pending = std::move(saved_pending); g_replication_stats = saved_stats; g_static_act = saved_act;
    g_sequence = saved_sequence; g_roster_sequence = saved_roster; g_roster_live = std::move(saved_live);
}

static void item_replication_cases(net::Session& session,net::Session& client)
{
    using namespace adventure_world;
    auto saved_captains=C.captain_of; auto saved_can_act=C.captain_can_act; auto saved_clock=C.simulation_time;
    auto* saved_session=C.sess; auto saved_bindings=g_bindings; auto saved_remote=g_remote;
    auto saved_pick=o_Pick; auto saved_drop=o_Drop;
    void* a=create(0,101),*b=create(0,101),*npc=create(1,102),*item=create(2,101);
    for(void* creature:{a,b,npc}) lookup(creature)->is_creature=true;
    lookup(item)->is_ornament=true;
    std::array<std::array<uint8_t,0x24>,3> slots{};
    unsigned index=0;
    for(void* creature:{a,b,npc}) {
        field<uint8_t*>(creature,0xE40)=slots[index].data();
        field<uint8_t*>(creature,0xE44)=slots[index].data()+0x24; ++index;
    }
    C.captain_of=[&](uint8_t player) {return player==0?a:player==1?b:nullptr;};
    C.captain_can_act=[](uint8_t player) {return player<2;};
    double clock=10; C.simulation_time=[&]{return clock;};
    o_Pick=item_pick; o_Drop=item_drop; C.sess=&session; field<int>(fake_play.data(),0xB8)=0;
    check(item_fixture_profile(L"SporeApp.exe -sporekit-profile:winlab_items_61 -w") &&
          !item_fixture_profile(L"SporeApp.exe -sporekit-profile:inst2") &&
          !item_fixture_profile(L"SporeApp.exe -sporekit-profile:winlab_") &&
          !item_fixture_profile(L"SporeApp.exe -sporekit-profile:winlab_bad!"),
          "item fixture capability requires an explicitly named isolated lab profile");
    field<int>(fake_play.data(),0x90)=3; g_run=C.run(); enumerate();
    Identity item_id{},npc_id{}; check(identity(item,item_id)&&identity(npc,npc_id),"item fixture has exact host identities");
    auto* spatial=lookup(item)->spatial.data(); field<void*>(slots[2].data(),0x20)=spatial;
    g_attachments.clear(); g_attachment_act=-1; sample_attachments(0);
    check(g_attachments.size()==1 && g_attachments[0].item==item_id && g_attachments[0].holder==npc_id &&
          g_attachments[0].player==ga::kNoPlayer && g_attachments[0].slot==0,
          "initial NPC tool attachment is sampled without a captain pickup event");
    auto initial=g_attachments;
    uint32_t revision=g_attachment_revision; sample_attachments(0);
    check(g_attachment_revision==revision,"unchanged held tools do not produce recurring reliable updates");
    refresh_attachment_spatials(); ActorState state=sample_actor(*actor_for(item),0); state.position={100,200,300};
    unsigned poses_before=pose_sets; apply_actor(*actor_for(item),state);
    check(pose_sets==poses_before,"world snapshots do not overwrite a native hand attachment pose");

    C.sess=&client; g_bindings[item_id]={item,field<uint32_t>(item,0x24),0};
    g_bindings[npc_id]={npc,field<uint32_t>(npc,0x24),0};
    field<void*>(slots[2].data(),0x20)=nullptr;
    g_attachment_pending[0]=initial; g_attachment_received[0]=1;
    unsigned picks_before=item_picks; apply_attachments(0); apply_attachments(0);
    check(field<void*>(slots[2].data(),0x20)==spatial && item_picks==picks_before+1,
          "initial and repeated attachment snapshots restore the native NPC hand exactly once");
    g_attachment_pending[0].clear(); g_attachment_received[0]=2; unsigned drops_before=item_drops; apply_attachments(0);
    check(!field<void*>(slots[2].data(),0x20) && item_drops==drops_before+1,"a complete empty relation snapshot releases a dropped tool");
    g_attachment_pending[0]=initial; g_bindings.erase(npc_id); picks_before=item_picks; apply_attachments(0);
    check(item_picks==picks_before,"late attachment holder remains pending without guessing another NPC");
    g_bindings[npc_id]={npc,field<uint32_t>(npc,0x24),0}; apply_attachments(0);
    check(item_picks==picks_before+1,"late holder binding reconstructs its pending tool");

    auto packet=[&](uint32_t serial,std::vector<Attachment> relations,uint16_t first=0,int total=-1,uint8_t sender=0) {
        net::Writer writer; writer.u8(Attachments); put_run(writer,C.run()); writer.u32(serial); writer.u8(0);
        writer.u16(total<0?uint16_t(relations.size()):uint16_t(total)); writer.u16(first); writer.u8(uint8_t(relations.size()));
        for(const auto& relation:relations) { write_identity(writer,relation.item); write_identity(writer,relation.holder); writer.u8(relation.player); writer.u8(relation.slot); }
        net::Reader reader(writer.data.data(),writer.size()); on_message(sender,reader);
    };
    g_attachment_assemblies={}; packet(10,initial);
    check(g_attachment_received[0]==10,"reliable held relation parses its run, act and revision");
    packet(9,{}); check(g_attachment_received[0]==10 && !g_attachment_pending[0].empty(),"older empty attachment snapshot cannot detach a newer tool");
    auto duplicate=initial; duplicate.push_back(initial[0]); packet(11,duplicate);
    check(g_attachment_received[0]==10,"duplicate item or hand entries cannot commit a malformed attachment snapshot");
    auto second=initial[0]; ++second.item.incarnation; second.slot=1;
    packet(12,{second},1,2);
    check(g_attachment_received[0]==10,"an incomplete out-of-order relation snapshot retains the previous complete state");
    packet(12,initial,0,2);
    check(g_attachment_received[0]==12 && g_attachment_pending[0].size()==2,"reordered relation chunks commit atomically after every entry arrives");
    packet(13,{},0,0,1);
    check(g_attachment_received[0]==12,"a non-host peer cannot forge an authoritative empty relation snapshot");

    C.sess=&session; field<void*>(slots[2].data(),0x20)=nullptr;
    field<net::Vec3>(lookup(a)->spatial.data(),4)={0,0,0};
    field<net::Vec3>(lookup(b)->spatial.data(),4)={20,0,0};
    field<net::Vec3>(spatial,4)={2,0,0};
    g_flying.clear(); g_flying[item_id]={1,0,{-2,0,0},9,0}; refresh_attachment_spatials();
    unsigned caught_before=g_item_catches; catch_items();
    check(g_item_catches==caught_before+1 && field<void*>(slots[0].data(),0x20)==spatial && g_flying.empty(),
          "a swept ordinary throw reaches a captain and becomes one authoritative hand attachment");
    field<void*>(slots[0].data(),0x20)=nullptr; refresh_attachment_spatials();
    field<net::Vec3>(lookup(b)->spatial.data(),4)={0,0,0};
    g_flying[item_id]={1,0,{-2,0,0},9,0}; catch_items();
    check(field<void*>(slots[0].data(),0x20)==spatial && !field<void*>(slots[1].data(),0x20),
          "simultaneous sweep contacts resolve deterministically by player ID");
    field<void*>(slots[0].data(),0x20)=nullptr; field<net::Vec3>(lookup(b)->spatial.data(),4)={20,0,0};
    refresh_attachment_spatials(); g_flying[item_id]={0,0,{-2,0,0},10,0}; caught_before=g_item_catches; catch_items();
    check(g_item_catches==caught_before && !g_flying.empty(),"original thrower retains native half-second pair grace");
    clock=10.6; field<net::Vec3>(spatial,4)={0,0,0}; catch_items();
    check(g_item_catches==caught_before+1,"original thrower can catch a returning item after native pair grace");
    field<void*>(slots[0].data(),0x20)=nullptr; refresh_attachment_spatials();
    item_receive_allowed=false; g_flying[item_id]={1,0,{-2,0,0},9,0}; caught_before=g_item_catches; catch_items();
    check(g_item_catches==caught_before,"native incompatible pickup eligibility prevents auto-catch"); item_receive_allowed=true;
    C.captain_can_act=[](uint8_t) {return false;}; g_flying[item_id]={1,0,{-2,0,0},9,0}; catch_items();
    check(g_item_catches==caught_before,"downed or disconnected captains cannot auto-catch");
    C.captain_can_act=[](uint8_t player) {return player<2;};
    field<void*>(slots[0].data(),0x20)=lookup(npc)->spatial.data();
    g_flying[item_id]={1,0,{-2,0,0},9,0}; catch_items();
    check(g_item_catches==caught_before,"a full hand is not replaced by automatic catching");
    field<void*>(slots[0].data(),0x20)=nullptr; refresh_attachment_spatials();
    field<int>(lookup(item)->powerup.data(),4)=13; g_flying[item_id]={1,0,{-2,0,0},9,0}; catch_items();
    check(g_item_catches==caught_before && g_flying.empty(),"contact grenades retain impact behavior and are never auto-caught");
    field<int>(lookup(item)->powerup.data(),4)=10; g_flying[item_id]={1,0,{-2,0,0},9,0}; catch_items();
    check(g_item_catches==caught_before+1,"native-repickable timed bombs use the same authoritative catch route");
    for(void* object:{a,b,npc,item}) field<bool>(object,0x21)=true;
    C.captain_of=saved_captains; C.captain_can_act=saved_can_act; C.simulation_time=saved_clock; C.sess=saved_session;
    o_Pick=saved_pick; o_Drop=saved_drop; g_bindings=std::move(saved_bindings); g_remote=std::move(saved_remote);
    g_attachments.clear(); g_attachment_pending={}; g_attachment_assemblies={}; g_attachment_received={};
    g_flying.clear(); g_attached_spatials.clear(); enumerate();
}

static void held_world_task_cases(net::Session& session, net::Session& client, uint64_t& reported_run)
{
    using namespace adventure_world;
    auto* saved_session=C.sess; auto saved_captains=C.captain_of; auto saved_can_act=C.captain_can_act;
    auto saved_held=C.held; auto saved_failure=C.interaction_failed; auto saved_pick=o_Pick; auto saved_drop=o_Drop;
    auto saved_damage=combatant_vtable[0x18/4];
    auto saved_physics=a_Physics,saved_impulse=a_Impulse,saved_grace=a_ThrowGrace;
    auto saved_bounds=spatial_vtable[0x6C/4];
    int saved_act=field<int>(fake_play.data(),0xB8);
    void* a=create(0,101); void* b=create(0,101); void* target=create(1,102); void* item=create(2,101);
    lookup(target)->has_combatant=true; field<float>(lookup(target)->combatant.data(),0x38)=100;
    std::array<uint8_t,0x24> slot{};
    field<void*>(b,0xE40)=slot.data(); field<void*>(b,0xE44)=slot.data()+slot.size();
    bool held=false, eligible=true;
    C.sess=&session; C.captain_of=[&](uint8_t player) {return player==0?a:player==1?b:nullptr;};
    C.captain_can_act=[&](uint8_t player) {return player<2 && eligible;}; C.held=[&] {return held;};
    o_Pick=item_pick; o_Drop=item_drop; combatant_vtable[0x18/4]=reinterpret_cast<void*>(&native_actor_damage);
    a_Physics=reinterpret_cast<uintptr_t>(&native_physics); a_Impulse=reinterpret_cast<uintptr_t>(&native_throw_impulse);
    a_ThrowGrace=reinterpret_cast<uintptr_t>(&native_throw_grace); spatial_vtable[0x6C/4]=reinterpret_cast<void*>(&bounds);
    reset_replication(); g_run=reported_run; field<int>(fake_play.data(),0xB8)=0; enumerate();
    Identity target_id{},item_id{}; check(identity(target,target_id)&&identity(item,item_id),"pending world fixture has exact native identities");
    auto receive=[&](const net::Writer& packet,uint8_t from=1) {
        net::Reader reader(packet.data.data(),packet.size()); on_message(from,reader);
    };
    auto hit_packet=[&](uint64_t event,uint64_t scope=0) {
        net::Writer w; w.u8(Hit); put_run(w,scope?scope:reported_run); put_run(w,event); write_identity(w,target_id); w.f32(5); w.u32(0); return w;
    };
    auto item_packet=[&](uint64_t event,uint8_t action,uint8_t act=0) {
        net::Writer w; w.u8(Interaction); put_run(w,reported_run); put_run(w,event);
        w.u8(act); w.u8(action); w.u8(1); write_identity(w,item_id); write_identity(w,{}); return w;
    };
    auto throw_packet=[&](uint64_t event,uint8_t act=0) {
        net::Writer w; w.u8(ItemThrow); put_run(w,reported_run); put_run(w,event); w.u8(act); w.u8(1); write_identity(w,item_id);
        w.f32(7); w.f32(8); w.f32(9); w.u8(1); return w;
    };
    capture_world_actions=true; world_action_order.clear();
    unsigned hits_before=native_actor_hits,picks_before=item_picks,drops_before=item_drops;
    receive(item_packet(1,PickUp)); receive(hit_packet(1)); receive(item_packet(2,Drop));
    check(queued.size()==1&&g_pending_world_tasks.size()==3,"one ordered drain owns queued hits and item actions");
    held=true; drain(); receive(hit_packet(1)); receive(item_packet(2,Drop)); receive(hit_packet(2));
    flush_world_tasks(); drain();
    check(g_pending_world_tasks.size()==4&&native_actor_hits==hits_before&&item_picks==picks_before&&
          item_drops==drops_before&&!field<void*>(slot.data(),0x20)&&field<float>(lookup(target)->combatant.data(),0x38)==100,
          "queued-before-pause and received-during-pause actions retain exact-once work without damage or inventory changes");
    held=false; flush_world_tasks(); drain(); flush_world_tasks(); drain();
    check(g_pending_world_tasks.empty()&&native_actor_hits==hits_before+2&&item_picks==picks_before+1&&item_drops==drops_before+1&&
          field<float>(lookup(target)->combatant.data(),0x38)==90&&world_action_order==std::vector<char>({'P','H','D','H'}),
          "Resume applies native hits and ownership actions once in their original source arrival order");

    // A native Give can already have dropped its item locally. A long shared
    // hold must not expire the recent-drop eligibility it had on admission.
    std::array<uint8_t,0x24> recipient_slot{}; lookup(target)->is_creature=true;
    field<void*>(target,0xE40)=recipient_slot.data(); field<void*>(target,0xE44)=recipient_slot.data()+recipient_slot.size();
    field<void*>(recipient_slot.data(),0x20)=lookup(item)->spatial.data();
    g_dropped[item_id]={1,H->time_seconds()};
    auto give=item_packet(3,Give); give.data.resize(give.size()-8); write_identity(give,target_id);
    auto gives_before=g_item_gives; receive(give); held=true; drain();
    const auto* immediate_host=H; SkHost delayed_host=*H; delayed_host.time_seconds=[] {return 100.;}; H=&delayed_host;
    held=false; flush_world_tasks(); drain(); H=immediate_host;
    check(g_item_gives==gives_before+1&&!g_dropped.count(item_id),
          "held time cannot expire a previously eligible native Give while exact ownership is revalidated");
    field<void*>(recipient_slot.data(),0x20)=nullptr; lookup(target)->is_creature=false;

    // Clients retain host-approved ownership changes too, rather than ACK/drop
    // them while their authoritative pause snapshot is still held.
    C.sess=&client; g_bindings[item_id]={item,field<uint32_t>(item,0x24),0};
    receive(item_packet(10,PickUp),0); receive(item_packet(11,Drop),0); held=true; drain();
    check(g_pending_world_tasks.size()==2&&item_picks==picks_before+1&&item_drops==drops_before+1,
          "guest holds approved ownership changes without consuming them");
    held=false; flush_world_tasks(); drain();
    check(g_pending_world_tasks.empty()&&item_picks==picks_before+2&&item_drops==drops_before+2,
          "guest Resume applies host-approved pickup/drop exactly once");
    C.sess=&session; world_action_order.clear();

    // Drop and its already emitted native impulse retain source order even if
    // the shared hold begins before either queued callback can execute.
    field<void*>(slot.data(),0x20)=lookup(item)->spatial.data();
    unsigned impulses_before=native_throw_impulses,graces_before=native_throw_graces,sources_before=native_throw_sources;
    receive(item_packet(4,Drop)); receive(throw_packet(1)); held=true; drain();
    check(g_pending_world_tasks.size()==2&&native_throw_impulses==impulses_before&&field<void*>(slot.data(),0x20),
          "queued Drop then Throw retains both ownership and impulse during the hold");
    H=&delayed_host; held=false; flush_world_tasks(); drain(); H=immediate_host;
    receive(throw_packet(1)); flush_world_tasks(); drain();
    check(world_action_order==std::vector<char>({'D','T'})&&!field<void*>(slot.data(),0x20)&&
          native_throw_impulses==impulses_before+1&&native_throw_graces==graces_before+1&&native_throw_sources==sources_before+1&&
          native_throw_vector.x==7&&native_throw_vector.y==8&&native_throw_vector.z==9&&native_throw_add,
          "Resume replays one native impulse after its Drop with unchanged vector, add flag, thrower and grace");
    g_flying.clear(); world_action_order.clear(); field<void*>(slot.data(),0x20)=lookup(item)->spatial.data();
    receive(item_packet(5,Drop)); drain(); receive(throw_packet(2)); held=true; drain();
    H=&delayed_host; held=false; flush_world_tasks(); drain(); H=immediate_host;
    check(world_action_order==std::vector<char>({'D','T'})&&native_throw_impulses==impulses_before+2,
          "a throw admitted inside the two-second drop window survives a long shared hold");
    g_flying.clear(); world_action_order.clear(); field<void*>(slot.data(),0x20)=lookup(item)->spatial.data();
    receive(item_packet(6,Drop)); receive(throw_packet(3)); held=true; drain();
    reset_replication(); g_run=reported_run; held=false; flush_world_tasks(); drain();
    check(world_action_order.empty()&&native_throw_impulses==impulses_before+2&&field<void*>(slot.data(),0x20),
          "cancel/reset discards a retained Drop and Throw without native impulse or ownership mutation");
    field<void*>(slot.data(),0x20)=nullptr;
    auto stale_pair=[&](uint64_t event,uint8_t act) {
        receive(hit_packet(event)); receive(item_packet(event,PickUp,act)); receive(throw_packet(event,act)); held=true; drain();
    };
    stale_pair(20,0); field<int>(fake_play.data(),0xB8)=1; held=false; flush_world_tasks(); drain();
    stale_pair(21,1); ++field<uint32_t>(target,0x24); ++field<uint32_t>(item,0x24);
    held=false; flush_world_tasks(); drain(); --field<uint32_t>(target,0x24); --field<uint32_t>(item,0x24);
    stale_pair(22,1); eligible=false; held=false; flush_world_tasks(); drain(); eligible=true;
    stale_pair(23,1); ++reported_run; held=false; flush_world_tasks(); drain(); --reported_run;
    check(world_action_order.empty()&&field<float>(lookup(target)->combatant.data(),0x38)==90&&!field<void*>(slot.data(),0x20),
          "Resume revalidates act, exact native incarnations, source life and run before hit, ownership or throw mutation");
    stale_pair(24,1); reset_replication(); g_run=reported_run; held=false; flush_world_tasks(); drain();
    check(world_action_order.empty(),"cancel/reset discards retained hits and item actions");
    receive(hit_packet(25)); receive(item_packet(25,PickUp,1));
    reset_replication(); g_run=reported_run;
    receive(item_packet(26,PickUp,1)); receive(hit_packet(26)); receive(item_packet(27,Drop,1)); drain();
    check(world_action_order==std::vector<char>({'P','H','D'})&&g_pending_world_tasks.empty(),
          "a stale queued drain cannot consume replacement-generation hits or ownership changes");

    unsigned failures_before=0; C.interaction_failed=[&](const std::string& reason) {
        check(reason.find("gameplay synchronization")!=std::string::npos,"pending gameplay overflow names its synchronization boundary"); ++failures_before;
    };
    held=true; auto final_health=field<float>(lookup(target)->combatant.data(),0x38);
    for(unsigned i=0;i<kMaxPendingWorldTasks;++i) receive(hit_packet(100+i));
    receive(hit_packet(100));
    check(g_pending_world_tasks.size()==kMaxPendingWorldTasks&&queued.empty(),"pending gameplay work is bounded and duplicate hit serials consume no slots");
    receive(item_packet(1000,PickUp,1)); drain();
    check(failures_before==1&&g_pending_world_tasks.empty()&&field<float>(lookup(target)->combatant.data(),0x38)==final_health,
          "bounded overflow reports cancellation instead of losing an acknowledged held hit or item action silently");
    capture_world_actions=false; world_action_order.clear(); reset_replication(); g_run=reported_run;
    for(void* object:{a,b,target,item}) field<bool>(object,0x21)=true;
    C.sess=saved_session; C.captain_of=saved_captains; C.captain_can_act=saved_can_act; C.held=saved_held;
    C.interaction_failed=saved_failure; o_Pick=saved_pick; o_Drop=saved_drop; combatant_vtable[0x18/4]=saved_damage;
    a_Physics=saved_physics; a_Impulse=saved_impulse; a_ThrowGrace=saved_grace; spatial_vtable[0x6C/4]=saved_bounds;
    field<int>(fake_play.data(),0xB8)=saved_act; enumerate();
}

static void held_health_admission_cases(net::Session& session, uint8_t from)
{
    using namespace adventure_world;
    auto* old_session = C.sess; auto old_damage = C.captain_damage; auto old_heal = C.captain_heal; auto old_held = C.held;
    C.sess = &session; C.held = [] { return true; };
    unsigned damage = 0, heals = 0;
    C.captain_damage = [&](uint8_t victim,const ga::Source& source,float amount) {
        check(victim == from && source.kind == ga::SourceKind::Environment && amount == 20,
              "delayed environment hit retains validated recipient, attribution and magnitude at the coordinator boundary");
        ++damage; return true;
    };
    C.captain_heal = [&](uint8_t victim,float amount) { check(victim == from && amount == 20,"delayed heal retains its validated recipient and magnitude"); ++heals; return true; };
    auto deliver = [&](uint64_t serial,int type,uint64_t scoped_run = 1) {
        net::Writer w; w.u8(CaptainHit); put_run(w,scoped_run); put_run(w,serial);
        w.u8(from); w.u8(ga::kNoPlayer); w.f32(20); w.u32(uint32_t(type));
        net::Reader r(w.data.data(),w.size()); on_message(from,r);
    };
    deliver(1,0); deliver(1,0); deliver(2,10); deliver(2,10); deliver(3,0,2);
    check(damage == 1 && heals == 1,
          "reliable duplicates cannot enqueue repeated environment damage/healing during a hold, and stale runs cannot enter the health boundary");
    C.sess = old_session; C.captain_damage = old_damage; C.captain_heal = old_heal; C.held = old_held;
}

static void animation_cases(net::Session& session, net::Session& client, void* live,
                            adventure_world::Identity id, uint64_t& reported_run)
{
    using namespace adventure_world;
    int old_state = field<int>(fake_play.data(), 0x90), old_time = field<int>(fake_play.data(), 0xC0);
    auto old_held = C.held;
    bool held = false;
    C.held = [&] { return held; };
    field<int>(fake_play.data(), 0x90) = 3; field<int>(fake_play.data(), 0xC0) = 0;
    auto message = [&](uint64_t serial, Identity target, uint8_t act = 0, uint64_t scoped_run = 1,
                       uint32_t animation = 0x12345678, uint32_t time = 0) {
        net::Writer w; w.u8(ActorAnimations); put_run(w, scoped_run); w.u8(act); w.u8(1);
        put_run(w, serial); w.u32(time); write_identity(w, target); w.u32(animation); w.u32(uint32_t(-1)); w.u8(1);
        return w;
    };
    auto receive = [&](const net::Writer& w, uint8_t from = 0) {
        net::Reader r(w.data.data(), w.data.size()); on_message(from, r);
    };
    uint64_t serial = 1;
    receive(message(serial, id));
    check(npc_clips == 0 && g_pending_animations[0].size() == 1,
          "a host visual event queues until outside-frame application");
    apply_animations(10);
    check(npc_clips == 1 && last_npc_clip == 0x12345678 && last_npc_block == -1 && last_npc_blend,
          "current host animation applies once to the exact live binding with native clip arguments");
    receive(message(serial, id)); apply_animations(10);
    check(npc_clips == 1, "duplicate animation delivery cannot repeat a clip or its incidental callbacks");
    unsigned received = g_animations_received;
    receive(message(++serial, id), 1);
    receive(message(++serial, id, 0, 2));
    receive(message(++serial, id, 8));
    auto malformed = message(++serial, id); malformed.data.pop_back(); receive(malformed);
    auto bad_flags = message(++serial, id); bad_flags.data.back() = 4; receive(bad_flags);
    check(g_animations_received == received, "nonhost, stale-run, invalid-act and truncated animation packets are rejected atomically");
    receive(message(++serial, id, 0, 1, 100)); receive(message(++serial, id, 0, 1, 200));
    check(g_pending_animations[0].size() == 1, "queued clips coalesce to the latest event for an actor");
    held = true; apply_animations(10); check(npc_clips == 1, "a group hold prevents native clip advancement");
    held = false; apply_animations(10);
    check(npc_clips == 2 && last_npc_clip == 200, "release applies only the newest coalesced clip");
    receive(message(++serial, id));
    auto binding = g_bindings.extract(id); apply_animations(10);
    check(npc_clips == 2, "an event cannot apply to an unbound native object");
    g_bindings.insert(std::move(binding)); apply_animations(10);
    check(npc_clips == 3, "a not-yet-bound event can apply after its exact incarnation binds");
    receive(message(++serial, id)); ++g_bindings[id].incarnation; apply_animations(10);
    check(npc_clips == 3, "a replaced local incarnation cannot receive an old clip");
    --g_bindings[id].incarnation; apply_animations(13);
    check(npc_clips == 3 && g_pending_animations[0].empty(), "an expired event is discarded instead of replaying when a replacement later becomes available");
    Identity old_host_id{id.marker, id.incarnation ^ 0x80000000u};
    receive(message(++serial, old_host_id)); apply_animations(10);
    check(npc_clips == 3, "an old host incarnation cannot target a same-marker successor");
    apply_animations(13);
    receive(message(++serial, id)); field<int>(fake_play.data(), 0xC0) = 3001; apply_animations(10);
    check(npc_clips == 3, "a delayed reliable event is rejected using shared simulation age");
    field<int>(fake_play.data(), 0xC0) = 0;
    receive(message(++serial, id, 0, 1, 300, 10000)); apply_animations(10);
    check(npc_clips == 3, "an implausibly future-dated clip cannot advance visual state");
    receive(message(++serial, id)); ++reported_run; apply_animations(10); --reported_run;
    check(npc_clips == 3, "queued clips do not survive a changed run before reset dispatch");
    g_pending_animations[0].clear();
    receive(message(++serial, id, 1)); apply_animations(10);
    check(npc_clips == 3 && g_pending_animations[1].size() == 1, "next-act clips wait for their native act and binding");
    field<int>(fake_play.data(), 0xB8) = 1; g_bindings[id].act = 1;
    receive(message(++serial, id, 0)); apply_animations(10);
    check(npc_clips == 4, "entering the matching act applies its clip and rejects previous-act events");
    field<int>(fake_play.data(), 0xB8) = 0; g_bindings[id].act = 0;
    receive(message(++serial, id)); g_remote[id].flags |= 2; apply_animations(10);
    check(npc_clips == 4, "host-dead actors retain native death cleanup rather than stale live attack clips");
    g_remote[id].flags &= uint8_t(~2); g_pending_animations[0].clear();
    auto sequence = g_animation_sequence;
    on_animation(live, 400, 0, false, false);
    check(g_animation_sequence == sequence && g_host_animations.empty(), "guest native animation callbacks cannot echo host events");

    C.sess = &session;
    unsigned before = g_animations_received;
    receive(message(++serial, id), client.my_id());
    check(g_animations_received == before, "a client cannot grant the host NPC visual authority");
    auto callback = client.cb.custom;
    std::vector<std::vector<uint8_t>> packets;
    client.cb.custom = [&](uint8_t type, uint8_t from, net::Reader& r) {
        if (type == net::MSG_ADVENTURE && !from && r.left() && *r.cur() == ActorAnimations)
            packets.emplace_back(r.cur(), r.cur() + r.left());
    };
    auto pump = [&] {
        for (int i = 0; i < 1000; ++i) {
            session.update(); client.update();
            if (!session.pending_to(client.my_id()) && i >= 2) return;
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
        check(false, "animation fixture reliable queue drains");
    };
    on_animation(live, 500, 2, false, false); on_animation(live, 600, 3, true, true);
    check(g_host_animations.size() == 1, "native host clip capture coalesces before transmission");
    send_animations(10); pump();
    check(packets.size() == 1 && packets[0].size() == 40 && g_animations_sent == 1,
          "one reliable host packet contains the latest native clip with no recurring spatial fields");
    net::Reader clip(packets[0].data(), packets[0].size()); clip.u8(); get_run(clip); clip.u8(); clip.u8();
    get_run(clip); clip.u32(); read_identity(clip);
    check(clip.u32() == 600 && clip.u32() == 3 && clip.u8() == 3 && !clip.left(),
          "native clip, block and blend/target provenance survive serialization exactly");
    net::Writer filler; filler.u8(255); put_run(filler, 0);
    while (session.pending_to(client.my_id()) < 64) session.send_reliable_to(client.my_id(), net::MSG_ADVENTURE, filler);
    on_animation(live, 700, 0, false, false); send_animations(10);
    on_animation(live, 800, 0, false, false); send_animations(10);
    check(session.pending_to(client.my_id()) == 64 && g_animations_sent == 1,
          "congested animation sending honors queue bounds and retains only the latest native clip");
    pump(); send_animations(10); pump();
    check(packets.size() == 2 && g_animations_sent == 2, "drained peers receive one coalesced replacement rather than a playback backlog");
    send_animations(13);
    check(g_host_animations.empty(), "old native clip captures expire instead of accumulating across respawn waves");
    client.cb.custom = callback; C.sess = &client; C.held = old_held;
    field<int>(fake_play.data(), 0x90) = old_state; field<int>(fake_play.data(), 0xC0) = old_time;
    g_pending_animations = {}; g_host_animations.clear(); g_animation_sent.clear();
}

void SK_THISCALL vehicle_waypoint(void* spatial, const net::Vec3* target, float stop_radius, float acceptable, bool straight)
{
    check(acceptable==stop_radius*2 && !straight,"native vehicle MoveTo preserves four-argument waypoint ABI");
    game::move_to(nullptr,*target,stop_radius);
    field<int>(spatial,0x250)=1;
}
void SK_THISCALL vehicle_speed(void*,float speed,int mode)
{
    check(mode==0,"native desired-speed override retains its int mode argument"); game::set_speed(nullptr,speed);
}
void SK_THISCALL vehicle_stop(void*) { game::stop(nullptr); }
float SK_THISCALL vehicle_standard(void*) { return game::standard_speed(nullptr); }
void* SK_THISCALL movement_request(void* spatial) { return static_cast<uint8_t*>(spatial)+0x1F4; }
void SK_THISCALL ground_strategy(void*,void*) {}

void native_waypoint_regression(adventure_world::NativeActor& actor)
{
    using namespace adventure_world;
    const auto saved_position=field<net::Vec3>(actor.spatial,4);
    const auto saved_orientation=field<net::Quat>(actor.spatial,0x10);
    const auto saved_motion=g_motion;
    auto* object=lookup(actor.object);
    const auto old_request=a_MovementRequest, old_strategy=a_VehicleGroundUpdate;
    a_MovementRequest=reinterpret_cast<uintptr_t>(&movement_request);
    a_VehicleGroundUpdate=reinterpret_cast<uintptr_t>(&ground_strategy);
    void* strategy_table[4]={nullptr,nullptr,nullptr,reinterpret_cast<void*>(&ground_strategy)};
    void** strategy=strategy_table;
    field<void*>(actor.spatial,0x4CC)=&strategy;
    const int old_type=field<int>(actor.object,0xB1C);
    field<int>(actor.object,0xB1C)=0;
    check(fly_through_waypoints(actor,object->is_creature),"verified native creature and land-vehicle strategies allow fly-through goals");
    if(object->is_vehicle) {
        field<int>(actor.object,0xB1C)=1;
        check(!fly_through_waypoints(actor,false),"unvalidated water vehicle strategy retains native arrival behavior");
        field<int>(actor.object,0xB1C)=0; strategy_table[3]=nullptr;
        check(!fly_through_waypoints(actor,false),"unknown vehicle strategies cannot enable the mode2 optimization");
        strategy_table[3]=reinterpret_cast<void*>(&ground_strategy);
    }
    Binding binding{actor.object,actor.incarnation,0};
    ActorState packet; packet.id={actor.marker,777}; packet.noun=actor.noun;
    packet.sequence=1; packet.seen=10; packet.position={0,0,0}; packet.orientation={0,0,0,1};
    capture_motion=true; motion_moves=motion_stops=0; motion_order.clear(); native_moving=false;
    const auto before=transform_sets;
    apply_actor(actor,packet,&binding,10); render_models();
    check(transform_sets==before+1,"fresh exact-incarnation binding uses one native transform initialization");
    unsigned between_packet_steps=0;
    for(int frame=1;frame<=120;++frame) {
        const double now=10.+frame/60.;
        if(frame%6==0) { ++packet.sequence; packet.seen=now; packet.position={float(frame/60.*4),0,0}; }
        const auto previous=field<net::Vec3>(actor.spatial,4);
        apply_actor(actor,packet,&binding,now);
        if(native_moving) check(field<int>(actor.spatial,0x250)==2,"native MoveTo is followed by verified fly-through mode admission");
        if(native_moving) {
            auto current=field<net::Vec3>(actor.spatial,4);
            const auto delta=adventure_actor_motion::difference(motion_target,current);
            const float distance=adventure_actor_motion::length(delta);
            if(distance>.001f) {
                float step=std::min(distance,motion_speed/60.f);
                current={current.x+delta.x*step/distance,current.y+delta.y*step/distance,current.z+delta.z*step/distance};
                field<net::Vec3>(actor.spatial,4)=current;
                object->model_position=current; // ordinary native locomotion/render maintenance
            }
        }
        if(frame%6 && field<net::Vec3>(actor.spatial,4).x>previous.x) ++between_packet_steps;
    }
    check(transform_sets==before+1 && motion_moves>4 && between_packet_steps>70,
          "real mirror command path keeps native locomotion advancing between packets without repeated transform snaps");
    bool ordered=true;
    for(size_t i=0;i<motion_order.size();++i) if(motion_order[i]=='M')
        ordered=ordered&&i+1<motion_order.size()&&motion_order[i+1]=='S';
    check(ordered&&motion_speed>4.f,"native MoveTo precedes desired catch-up speed so its standard-speed reset cannot discard it");
    bool held=true; auto old_held=C.held; C.held=[&] { return held; };
    const auto moves_before=motion_moves;
    apply_actor(actor,packet,&binding,12.01);
    check(motion_stops>0&&!native_moving,"group hold stops the native follower without relocating its body");
    held=false; apply_actor(actor,packet,&binding,12.02);
    check(motion_moves==moves_before,"Resume cannot reuse a pre-hold waypoint before a fresh host sample");
    ++packet.sequence;packet.seen=12.1;packet.position={8.4f,0,0};
    apply_actor(actor,packet,&binding,12.1);
    // Two fresh samples establish velocity after Resume; the first may
    // already be within the longer vehicle navigation goal's tolerance.
    ++packet.sequence;packet.seen=12.2;packet.position={8.8f,0,0};
    apply_actor(actor,packet,&binding,12.2);
    check(motion_moves>moves_before,"fresh post-hold sample re-enables native following");
    field<net::Vec3>(actor.spatial,4)={11.8f,0,0};
    for(int i=1;i<=4;++i) { ++packet.sequence;packet.seen=12.2+i*.1;apply_actor(actor,packet,&binding,packet.seen); }
    check(g_motion.stationary>saved_motion.stationary&&field<net::Vec3>(actor.spatial,4).x==8.8f,
          "confirmed stationary desync permits a corrective native transform");
    const unsigned idle_transforms=transform_sets;
    ++packet.sequence;packet.seen=12.7;packet.orientation={0,0,.7071068f,.7071068f};
    apply_actor(actor,packet,&binding,12.7);
    apply_actor(actor,packet,&binding,12.71);
    check(transform_sets==idle_transforms+(object->is_vehicle?1:0)&&field<net::Vec3>(actor.spatial,4).x==8.8f&&
          field<net::Quat>(actor.spatial,0x10).z==packet.orientation.z,
          "one fresh idle vehicle turn uses its physics bridge without translating or repeating the pose");
    C.held=old_held;capture_motion=false;
    field<net::Vec3>(actor.spatial,4)=saved_position;field<net::Quat>(actor.spatial,0x10)=saved_orientation;
    object->model_position=saved_position;object->model_dirty=false;g_motion=saved_motion;
    field<void*>(actor.spatial,0x4CC)=nullptr;field<int>(actor.object,0xB1C)=old_type;
    a_MovementRequest=old_request;a_VehicleGroundUpdate=old_strategy;
}

int main()
{
    using namespace adventure_world;
    field<int>(fake_mode.data(), 0xCC) = 2;
    field<uint8_t*>(fake_mode.data(), 0x74) = fake_data.data();
    field<uint8_t*>(fake_mode.data(), 0x78) = fake_play.data();
    field<uint8_t*>(fake_data.data(), 0x10) = fake_resource.data();
    field<uint8_t*>(fake_resource.data(), 0x2C10) = placements.data();
    field<uint8_t*>(fake_resource.data(), 0x2C14) = placements.data() + placements.size();
    for (size_t i = 0; i < 3; ++i) {
        auto* marker = placements.data() + i * 0x238 + 4;
        field<int>(marker, 0) = static_cast<int>(i);
        field<uint32_t>(marker, 0x228) = static_cast<uint32_t>(i);
    }
    spatial_vtable[0x38 / 4] = reinterpret_cast<void*>(&position);
    spatial_vtable[0x3C / 4] = reinterpret_cast<void*>(&::orientation);
    spatial_vtable[0x44 / 4] = reinterpret_cast<void*>(&transform);
    spatial_vtable[0xE0 / 4] = reinterpret_cast<void*>(&vehicle_waypoint);
    spatial_vtable[0xC4 / 4] = reinterpret_cast<void*>(&vehicle_speed);
    spatial_vtable[0xEC / 4] = reinterpret_cast<void*>(&vehicle_stop);
    spatial_vtable[0xCC / 4] = reinterpret_cast<void*>(&vehicle_standard);
    combatant_vtable[0x58 / 4] = reinterpret_cast<void*>(&maximum_hp);
    a_Mode = reinterpret_cast<uintptr_t>(&mode_pointer);
    a_GetMarker = reinterpret_cast<uintptr_t>(&get_marker);
    void* initial = create(0, 101);
    enumerate();
    check(g_markers.size() == 3 && g_native.size() == 1, "resource markers include placements never instantiated in the guest");
    ActorState parent; parent.id = {0, 100}; parent.noun = 101; parent.orientation.w = 1;
    ActorState clone = parent; clone.id.incarnation = 101; clone.position.x = 12;
    ActorState newly_visible = parent; newly_visible.id = {1, 102}; newly_visible.noun = 102; newly_visible.position.x = 20;
    g_remote[parent.id] = parent; g_remote[clone.id] = clone; g_remote[newly_visible.id] = newly_visible;
    mirror(10);
    check(g_bindings.size() == 3 && creates == 2 && g_native.size() == 3, "all host incarnations bind after an act reveals an absent placement");
    check(g_bindings[parent.id].object == initial && g_bindings[clone.id].object != initial,
          "a second host incarnation never aliases the already bound native object");
    void* local_extra = create(0, 101); enumerate();
    std::set<void*> extras;
    auto retained = reconcile_siblings({parent.id, clone.id, newly_visible.id}, 10, extras);
    check(extras.size() == 1 && extras.count(local_extra) && retained[0] && !extras.count(retained[0]),
          "complete bound marker retires only its unbound local sibling and retains a canonical peer");
    ActorState future = parent; future.id.incarnation = 103; future.sequence = 20;
    g_remote[future.id] = future; extras.clear();
    reconcile_siblings({parent.id, clone.id, newly_visible.id}, 10, extras);
    check(!extras.count(local_extra), "newer unbound host state protects candidate siblings from an older roster");
    g_bindings[future.id] = {local_extra, field<uint32_t>(local_extra, 0x24)};
    void* another_extra = create(0, 101); enumerate(); extras.clear();
    reconcile_siblings({parent.id, clone.id, newly_visible.id}, 10, extras);
    check(extras.size() == 1 && extras.count(another_extra) && !extras.count(local_extra),
          "a newer bound host identity survives while unrelated local duplicates are retired");
    g_remote.erase(future.id); g_bindings.erase(future.id);
    Identity waiting{1, 12345}; extras.clear();
    reconcile_siblings({parent.id, clone.id, newly_visible.id, waiting}, 30, extras);
    check(!extras.count(g_bindings[newly_visible.id].object), "a roster identity without its snapshot or binding cannot authorize sibling deletion");
    ActorState stale; stale.id = {2, 999}; stale.sequence = 5; g_remote[stale.id] = stale;
    g_spawn_attempt[stale.id] = 10;
    std::set<void*> removed;
    retire_snapshots({parent.id, clone.id, newly_visible.id}, 10, removed);
    check(!g_remote.count(stale.id) && !g_spawn_attempt.count(stale.id) && removed.empty(),
          "complete roster retires an unbound obsolete snapshot before it can respawn");
    g_remote[clone.id].sequence = 20;
    retire_snapshots({parent.id, newly_visible.id}, 10, removed);
    check(g_remote.count(clone.id) && g_bindings.count(clone.id) && removed.empty(),
          "a newer snapshot survives an older complete roster");
    retire_snapshots({parent.id, newly_visible.id}, 20, removed);
    check(!g_remote.count(clone.id) && !g_bindings.count(clone.id) && removed.size() == 1 && !removed.count(initial),
          "authoritative incarnation retirement preserves its sibling sharing the marker");
    Identity reused{2, 111};
    g_bindings[reused] = {initial, field<uint32_t>(initial, 0x24)};
    ++field<uint32_t>(initial, 0x24); enumerate(); removed.clear();
    retire_snapshots({parent.id, newly_visible.id}, 20, removed);
    check(!g_bindings.count(reused) && removed.empty(), "a reused local pointer is not destroyed through an old incarnation binding");
    field<bool>(initial, 0x20) = true;
    enumerate();
    check(!actor_for(initial), "native retired flag excludes objects still awaiting registry cleanup");

    // The coordinator marks Ended before its queued native callback. The native
    // play allocation is reused by retry/solo, so its lifecycle boundary matters.
    net::Session session; std::string error;
    check(session.host(0, "native-results-fixture", error), "isolated fixture session opens");
    bool running = false; uint64_t reported_run = 1; unsigned result_requests = 0;
    C.sess = &session; C.active = [&] { return running; }; C.run = [&] { return reported_run; };
    C.result = [&](bool) { ++result_requests; running = false; reported_run = 0; };
    g_ok = true; o_State = result_state;
    SkHost adapter{}; adapter.out_printf = output; adapter.run_outside_frame = queue;
    adapter.time_seconds = [] { return 10.0; }; H = &adapter;
    void* captain = create(0, 101); void* partner = create(1, 102);
    field<net::Vec3>(lookup(partner)->spatial.data(), 4) = {40, 50, 60};
    C.captain_of = [&](uint8_t id) { return id == 0 ? captain : id == 1 ? partner : nullptr; };

    // Social callbacks may run only on the exact living guest binding. These
    // eligibility reads must leave native state and result bookkeeping intact.
    net::Session client;
    check(client.join("127.0.0.1:" + std::to_string(session.port()), "mirror-client", error), "isolated mirror client joins");
    for (int i = 0; i < 1000 && !client.connected(); ++i) {
        client.update(); session.update(); client.update();
        if (!client.connected()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(client.connected(), "isolated mirror client connects");
    C.sess = &client; running = true; g_run = reported_run;
    void* live = g_bindings[newly_visible.id].object;
    auto* live_object = lookup(live); live_object->has_combatant = live_object->is_creature = true;
    field<float>(live_object->combatant.data(), 0x38) = 10;
    g_remote[newly_visible.id].flags = 1; g_remote[newly_visible.id].health = 10;
    g_bindings[newly_visible.id].act = 0; enumerate();
    auto result_before = g_native_result;
    check(is_live_mirror(live), "current living guest actor with exact host binding is eligible");
    // dc04's separate setters do not mark creature+B5D. Model maintenance must
    // observe SetTransform's dirty flag, as the one-NPC native probe established.
    auto saved_pose = g_remote[newly_visible.id];
    auto* live_native = actor_for(live);
    net::Vec3 moved{71,12,3}; net::Quat rotated{0,0,0,1};
    position(live_native->spatial,&moved); ::orientation(live_native->spatial,&rotated); render_models();
    check(live_object->model_position.x != moved.x && !live_object->model_dirty,
          "separate spatial setters reproduce a stale creature model despite matching numeric positions");
    auto fixed_pose = saved_pose; fixed_pose.position = moved; fixed_pose.orientation = rotated;
    auto transforms_before = transform_sets; apply_actor(*live_native,fixed_pose);
    check(transform_sets == transforms_before + 1 && live_object->model_dirty,
          "creature replication dispatches the two-argument combined transform and marks model maintenance dirty");
    render_models();
    check(live_object->model_position.x == moved.x && live_object->model_position.y == moved.y &&
          !live_object->model_dirty && !npc_ai_filter(live),
          "native-style render maintenance consumes the replicated creature pose while NPC decisions remain fenced");
    apply_actor(*live_native,saved_pose); render_models();
    native_waypoint_regression(*live_native);
    live_object->is_creature=false;live_object->is_vehicle=true;
    native_waypoint_regression(*live_native);
    live_object->is_creature=true;live_object->is_vehicle=false;
    check(!npc_ai_filter(live), "a living authored guest NPC suppresses only its autonomous brain");
    check(g_native_result.play == result_before.play && g_native_result.run == result_before.run &&
          g_native_result.committed == result_before.committed, "mirror eligibility has no native result lifecycle side effects");
    check(!is_live_mirror(local_extra) && !is_live_mirror(nullptr), "unbound sibling and null are ineligible");
    auto captains_before = C.captain_of;
    C.captain_of = [&](uint8_t id) { return id == client.my_id() ? live : nullptr; };
    check(npc_ai_filter(live), "local party captains do not enter the NPC brain filter");
    check(!is_live_mirror(live), "party captains cannot be treated as mirrored NPCs"); C.captain_of = captains_before;
    C.sess = &session; check(!is_live_mirror(live), "host actors are ineligible");
    check(npc_ai_filter(live), "the host continues to simulate authored NPC AI"); C.sess = &client;
    reported_run = 2; check(!is_live_mirror(live), "old run bindings are ineligible"); reported_run = 1;
    field<int>(fake_play.data(), 0xB8) = 1;
    check(!is_live_mirror(live), "previous-act binding is ineligible"); field<int>(fake_play.data(), 0xB8) = 0;
    ++g_bindings[newly_visible.id].incarnation;
    check(!is_live_mirror(live), "stale local binding incarnation is ineligible"); --g_bindings[newly_visible.id].incarnation;
    ++field<uint32_t>(live, 0x24);
    check(!is_live_mirror(live), "reused native pointer is ineligible"); --field<uint32_t>(live, 0x24);
    auto saved_snapshot = g_remote.extract(newly_visible.id);
    check(!is_live_mirror(live), "binding without its host snapshot is ineligible"); g_remote.insert(std::move(saved_snapshot));
    g_remote[newly_visible.id].flags = 3;
    check(!is_live_mirror(live), "host-dead snapshot is ineligible"); g_remote[newly_visible.id].flags = 1;
    g_remote[newly_visible.id].health = 0;
    check(!is_live_mirror(live), "zero host health is ineligible"); g_remote[newly_visible.id].health = 10;
    field<float>(live_object->combatant.data(), 0x38) = 0;
    check(npc_ai_filter(live), "host-killed guest creatures retain the native death-cleanup path");
    check(!is_live_mirror(live), "native death cleanup is ineligible"); field<float>(live_object->combatant.data(), 0x38) = 10;
    field<bool>(live, 0x20) = true; check(!is_live_mirror(live), "retired native actor is ineligible"); field<bool>(live, 0x20) = false;
    field<bool>(live, 0x21) = true; check(!is_live_mirror(live), "destroyed native actor is ineligible"); field<bool>(live, 0x21) = false;
    running = false; check(!is_live_mirror(live), "ended adventure actor is ineligible");
    check(npc_ai_filter(live), "NPC AI authority fencing does not survive an ended attempt"); running = true;
    lookup(captain)->is_creature = lookup(partner)->is_creature = true;
    animation_cases(session, client, live, newly_visible.id, reported_run);
    held_health_admission_cases(session,client.my_id());
    a_Hostile = reinterpret_cast<uintptr_t>(&hostile_relation);
    const char* relations[] = {"relations", "1"};
    check(debug(2, relations, nullptr) == 0 && relation_calls == 2,
          "bounded relations diagnostic queries the matching NPC against each party captain");

    // Exercise the actual host serializer across the loopback session. The
    // original relationship query is isolated by the adapter fixture above.
    std::vector<uint8_t> host_actor_packet;
    client.cb.custom = [&](uint8_t type, uint8_t from, net::Reader& reader) {
        if (type == net::MSG_ADVENTURE && !from && reader.left() && *reader.cur() == Actors)
            host_actor_packet.assign(reader.cur(), reader.cur() + reader.left());
    };
    auto saved_native = g_native;
    g_native = {{live, live_object->spatial.data(), nullptr, 1, field<uint32_t>(live, 0x24), newly_visible.noun}};
    a_Relationships = reinterpret_cast<uintptr_t>(&relationships);
    C.sess = &session; capture_party_query = true; expected_party_actor = live; send_actors();
    for (int i = 0; i < 1000 && host_actor_packet.empty(); ++i) {
        session.update(); client.update();
        if (host_actor_packet.empty()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    capture_party_query = false; C.sess = &client; g_native = std::move(saved_native); client.cb.custom = {};
    net::Reader captured(host_actor_packet.data(), host_actor_packet.size());
    check(captured.u8() == Actors && get_run(captured) == 1, "host relationship snapshot crosses the real UDP session");
    captured.u32(); check(captured.u8() == 0 && captured.u8() == 1, "host snapshot identifies its act and bounded actor count");
    read_identity(captured); captured.u32();
    for (int i = 0; i < 10; ++i) captured.f32();
    captured.u8();
    check(captured.u8() == 6 && captured.ok() && !captured.left() && captured_party_queries == 1,
          "host serializes its real relationship query result after the actor fields");

    // Only authoritative, bounded host relationship values for this exact
    // live incarnation may replace a local cached NPC/party query.
    auto actor_packet = [&](uint32_t sequence, uint8_t relationship, bool malformed_second = false) {
        net::Writer w; w.u8(Actors); put_run(w, 1); w.u32(sequence); w.u8(0); w.u8(malformed_second ? 2 : 1);
        auto record = [&](uint8_t value) {
            write_identity(w, newly_visible.id); w.u32(newly_visible.noun);
            w.f32(0); w.f32(0); w.f32(0);
            w.f32(0); w.f32(0); w.f32(0); w.f32(1);
            w.f32(10); w.f32(10); w.f32(1); w.u8(1); w.u8(value);
        };
        record(relationship); if (malformed_second) record(8);
        return w;
    };
    auto receive_actor = [&](const net::Writer& w, uint8_t from = 0) {
        net::Reader reader(w.data.data(), w.data.size()); on_message(from, reader);
    };
    int authoritative_relation = 123;
    check(!party_relationship(live, authoritative_relation) && authoritative_relation == 123,
          "absent native relationship snapshot preserves caller output");
    receive_actor(actor_packet(100, 6));
    check(party_relationship(live, authoritative_relation) && authoritative_relation == 6,
          "host relationship snapshot is returned for the exact live guest binding");
    receive_actor(actor_packet(101, 5), 1); receive_actor(actor_packet(99, 5));
    check(party_relationship(live, authoritative_relation) && authoritative_relation == 6,
          "guest sender and stale host sequence cannot replace authoritative relationship");
    receive_actor(actor_packet(102, 5, true));
    check(party_relationship(live, authoritative_relation) && authoritative_relation == 6,
          "out-of-range relationship rejects the whole snapshot chunk before partial mutation");
    auto truncated_actor = actor_packet(103, 5); truncated_actor.data.pop_back(); receive_actor(truncated_actor);
    check(party_relationship(live, authoritative_relation) && authoritative_relation == 6,
          "missing relationship byte rejects a truncated snapshot");
    receive_actor(actor_packet(104, 0xFF)); authoritative_relation = 123;
    check(!party_relationship(live, authoritative_relation) && authoritative_relation == 123,
          "unavailable native relationship retains native fallback");
    receive_actor(actor_packet(105, 5));
    ++g_bindings[newly_visible.id].incarnation;
    check(!party_relationship(live, authoritative_relation), "stale local incarnation cannot supply party relationships");
    --g_bindings[newly_visible.id].incarnation;
    field<int>(fake_play.data(), 0xB8) = 1;
    check(!party_relationship(live, authoritative_relation), "old-act binding cannot supply party relationships");
    field<int>(fake_play.data(), 0xB8) = 0;
    receive_actor(actor_packet(106, 7));
    check(party_relationship(live, authoritative_relation) && authoritative_relation == 7,
          "highest valid native relationship is bounded and accepted");

    // Direct snapshot fields need the same explicit HUD refresh used by native
    // CountGoal; unchanged snapshots must not replay presentation or completion.
    field<uint8_t*>(fake_mode.data(), 0x6C) = fake_display.data();
    field<uint8_t*>(fake_display.data(), 0x148) = fake_hud.data();
    field<void*>(fake_hud.data(), 0x24) = fake_hud.data();
    field<uint8_t*>(fake_play.data(), 0x64) = hud_goals.data();
    field<uint8_t*>(fake_play.data(), 0x68) = hud_goals.data() + hud_goals.size();
    field<int>(fake_play.data(), 0x90) = 3;
    a_GoalHUD = reinterpret_cast<uintptr_t>(&refresh_hud);
    auto snapshot = [&] (uint64_t attempt = 1) {
        return new GoalTask{attempt, 1, 1500, 60000, 0, 3, {{true, expected_hud_count}, {false, 2}, {false, 0}}};
    };
    apply_goals(snapshot());
    check(hud_calls == 1 && g_goal_hud_refreshes == 1 && !g_goal_hud_dirty,
          "changed completed and partial goals trigger exactly one native HUD repaint");
    apply_goals(snapshot());
    check(hud_calls == 1, "identical goal snapshots do not replay HUD effects");
    expected_hud_count = 2; field<void*>(fake_display.data(), 0x148) = nullptr;
    apply_goals(snapshot());
    check(hud_calls == 1 && g_goal_hud_dirty, "temporarily unavailable native HUD retains a pending refresh");
    field<uint8_t*>(fake_display.data(), 0x148) = fake_hud.data(); apply_goals(snapshot());
    check(hud_calls == 2 && !g_goal_hud_dirty, "next identical snapshot repaints once after native HUD returns");
    expected_hud_count = 3; apply_goals(snapshot(99));
    check(hud_calls == 2 && field<uint32_t>(hud_goals.data(), 4) == 2,
          "stale-run snapshot cannot mutate goals or refresh native UI");
    field<void*>(fake_play.data(), 0x64) = nullptr; field<void*>(fake_play.data(), 0x68) = nullptr;

    // Completed native social actions have their own relay, including NPCs that
    // are not a goal. Mere goal-counter calls must never manufacture this event.
    o_Befriend = native_befriend; o_Ally = native_ally;
    a_SocialMode = reinterpret_cast<uintptr_t>(&social_mode);
    a_Relationships = reinterpret_cast<uintptr_t>(&relationships);
    a_SocialEvent = reinterpret_cast<uintptr_t>(&relationship_event);
    bool captain_alive = true;
    C.captain_can_act = [&](uint8_t player) { return player == client.my_id() && captain_alive; };
    std::vector<std::vector<uint8_t>> completed_packets;
    session.cb.custom = [&](uint8_t type, uint8_t from, net::Reader& r) {
        if (type == net::MSG_ADVENTURE && from == client.my_id() && r.left() && *r.cur() == SocialComplete)
            completed_packets.emplace_back(r.cur(), r.cur() + r.left());
    };
    auto saved_partner_position = field<net::Vec3>(lookup(partner)->spatial.data(), 4);
    field<net::Vec3>(lookup(partner)->spatial.data(), 4) = field<net::Vec3>(live_object->spatial.data(), 4);
    field<int>(fake_play.data(), 0x90) = 3;
    g_social_completions.clear(); g_last_interaction.clear();
    befriended(fake_play.data(), live);
    check(g_social_completions.empty(), "a bare native goal callback without completed social state cannot relay Befriend");
    field<uint32_t>(live, 0xB58) = 0x80;
    befriended(fake_play.data(), live); befriended(fake_play.data(), live);
    allied(fake_play.data(), live); allied(fake_play.data(), live);
    for (int i = 0; i < 1000 && completed_packets.size() < 2; ++i) {
        client.update(); session.update(); client.update();
        if (completed_packets.size() < 2) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(completed_packets.size() == 2 && g_social_completions.size() == 2,
          "genuine native Befriend and Ally callbacks each relay once without a matching goal");
    field<int>(social_goal.data(), 0x24) = 4;
    count_goal(fake_play.data(), social_goal.data(), nullptr, nullptr);
    check(field<int>(social_goal.data(), 4) == 0, "guest social goal counters never advance independently");

    C.sess = &session; g_social_completions.clear(); g_last_interaction.clear();
    field<uint32_t>(live, 0x24) = newly_visible.id.incarnation;
    field<uint32_t>(live, 0xB58) = 0;
    native_befriends = native_allies = relationship_events = recruit_calls = 0;
    field<int>(social_goal.data(), 0x24) = 2; // an unrelated current objective
    auto completion_packet = [&](uint64_t event, uint8_t type = 4, uint8_t act = 0, uint64_t attempt = 1,
                                 Identity target = Identity{1, 102}) {
        net::Writer w; w.u8(SocialComplete); put_run(w, attempt); put_run(w, event);
        w.u8(act); w.u8(type); write_identity(w, target); return w;
    };
    auto deliver = [&](const net::Writer& w, uint8_t from = 1) {
        net::Reader reader(w.data.data(), w.data.size()); on_message(from, reader);
    };
    deliver(completion_packet(1, 2));
    deliver(completion_packet(1, 4, 0, 99));
    deliver(completion_packet(1), 2);
    auto malformed = completion_packet(1); malformed.u8(0); deliver(malformed);
    malformed.data.pop_back(); malformed.data.pop_back(); deliver(malformed);
    check(queued.empty(), "wrong category, attempt, sender, trailing and truncated social events never queue native work");
    net::Writer forged_goal; forged_goal.u8(GoalEvent); put_run(forged_goal, 1); put_run(forged_goal, 1);
    forged_goal.u8(0); forged_goal.u8(0); forged_goal.u8(4); write_identity(forged_goal, {1, 102});
    deliver(forged_goal);
    check(queued.empty(), "legacy goal events cannot forge Befriend progress");
    captain_alive = false; deliver(completion_packet(1));
    check(queued.empty(), "downed or disconnected captains cannot report social completion"); captain_alive = true;
    deliver(completion_packet(2, 4, 1)); drain();
    native_social_mode = 0; deliver(completion_packet(3)); drain(); native_social_mode = 2;
    field<float>(live_object->combatant.data(), 0x38) = 0; deliver(completion_packet(4)); drain();
    field<float>(live_object->combatant.data(), 0x38) = 10;
    field<net::Vec3>(lookup(partner)->spatial.data(), 4) = {1000, 1000, 1000}; deliver(completion_packet(5)); drain();
    field<net::Vec3>(lookup(partner)->spatial.data(), 4) = field<net::Vec3>(live_object->spatial.data(), 4);
    deliver(completion_packet(6, 4, 0, 1, {1, 999})); drain();
    check(!relationship_events && !native_befriends && g_social_completions.empty(),
          "old act, ineligible native category, dead, distant and stale-incarnation targets cause no mutation");
    deliver(completion_packet(7)); field<int>(fake_play.data(), 0xB8) = 1; drain();
    field<int>(fake_play.data(), 0xB8) = 0;
    check(!relationship_events, "queued completion revalidates the act before native mutation");
    deliver(completion_packet(8)); captain_alive = false; drain(); captain_alive = true;
    check(!relationship_events, "queued completion revalidates captain life before native mutation");
    deliver(completion_packet(9)); drain();
    check(relationship_events == 1 && native_befriends == 1 && (field<uint32_t>(live, 0xB58) & 0x80) &&
          field<int>(social_goal.data(), 4) == 0,
          "host applies a completed non-objective Befriend without changing an unrelated goal");
    deliver(completion_packet(9)); deliver(completion_packet(10)); drain();
    check(relationship_events == 1 && native_befriends == 1,
          "replayed packet and repeated new event cannot duplicate native social effects");
    native_social_mode = 2; deliver(completion_packet(11, 5)); drain();
    check(!recruit_calls, "Befriend eligibility cannot be used to forge recruitment");
    native_social_mode = 4; recruitment_accepted = false; deliver(completion_packet(12, 5)); drain();
    check(recruit_calls == 1 && native_allies == 0 && !g_social_completions.count({0, 5, 1, 102}),
          "native recruitment refusal preserves its result without fabricating Ally progress");
    recruitment_accepted = true; deliver(completion_packet(13, 5)); drain();
    deliver(completion_packet(14, 5)); drain();
    check(recruit_calls == 2 && native_allies == 1,
          "accepted non-objective recruitment uses the real native operation once");
    field<int>(fake_play.data(), 0xB8) = 1; field<uint32_t>(live, 0xB58) = 0; native_social_mode = 2;
    field<int>(social_goal.data(), 0x24) = 4; field<int>(social_goal.data(), 0x28) = field<int>(live, 0x1C);
    deliver(completion_packet(15, 4, 1)); drain(); deliver(completion_packet(16, 4, 1)); drain();
    check(relationship_events == 2 && native_befriends == 2 && field<int>(social_goal.data(), 4) == 1,
          "only the native host callback advances a matching authored goal, once per target and act");
    field<int>(fake_play.data(), 0xB8) = 2; field<uint32_t>(live, 0xB58) = 0; native_social_mode = 4;
    field<int>(social_goal.data(), 0x24) = 5; field<int>(social_goal.data(), 4) = 0;
    deliver(completion_packet(17, 4, 2)); drain();
    check(relationship_events == 3 && native_befriends == 3 && field<uint32_t>(live, 0xB58) == 0x80 &&
          recruit_calls == 2 && field<int>(social_goal.data(), 4) == 0,
          "mode4 native minigame win preserves its Befriend state before recruitment without counting Ally");
    deliver(completion_packet(18, 5, 2)); drain();
    deliver(completion_packet(19, 4, 2)); deliver(completion_packet(20, 5, 2)); drain();
    check(field<uint32_t>(live, 0xB58) == 0x180 && recruit_calls == 3 && native_allies == 2 &&
          relationship_events == 3 && native_befriends == 3 && field<int>(social_goal.data(), 4) == 1,
          "native Befriend then Ally preserves both completion flags and counts recruitment exactly once");

    // A genuine completion may already be reliable/queued when the last menu
    // opens. Keep it without mutating relationships or authored goal progress.
    auto previous_held = C.held;
    bool social_held = false; C.held = [&] { return social_held; };
    field<int>(fake_play.data(), 0xB8) = 3; field<uint32_t>(live, 0xB58) = 0; native_social_mode = 2;
    field<int>(social_goal.data(), 0x24) = 4; field<int>(social_goal.data(), 4) = 0;
    auto relationships_before_hold = relationship_events, befriends_before_hold = native_befriends;
    deliver(completion_packet(21, 4, 3));
    check(queued.size() == 1 && g_pending_social.size() == 1, "one outside-frame drain owns an admitted completion");
    social_held = true; drain();
    deliver(completion_packet(21, 4, 3)); deliver(completion_packet(22, 4, 3));
    flush_social_completions(); drain();
    check(g_pending_social.size() == 1 && relationship_events == relationships_before_hold &&
          native_befriends == befriends_before_hold && !field<uint32_t>(live, 0xB58) &&
          !field<int>(social_goal.data(), 4),
          "arrival-before-hold and duplicate completions retain one action without native or goal changes");
    social_held = false; flush_social_completions(); drain(); flush_social_completions(); drain();
    check(g_pending_social.empty() && relationship_events == relationships_before_hold + 1 &&
          native_befriends == befriends_before_hold + 1 && field<int>(social_goal.data(), 4) == 1,
          "Resume applies the retained native completion and authored objective exactly once");

    const auto completed_before_stale = relationship_events;
    field<int>(fake_play.data(), 0xB8) = 4; field<uint32_t>(live, 0xB58) = 0;
    deliver(completion_packet(23, 4, 4)); social_held = true; drain();
    field<int>(fake_play.data(), 0xB8) = 5; social_held = false; flush_social_completions(); drain();
    check(g_pending_social.empty() && relationship_events == completed_before_stale,
          "a retained completion cannot mutate a replaced act after Resume");
    deliver(completion_packet(24, 4, 5)); social_held = true; drain();
    ++field<uint32_t>(live, 0x24); social_held = false; flush_social_completions(); drain();
    --field<uint32_t>(live, 0x24);
    check(relationship_events == completed_before_stale, "Resume revalidates the exact target incarnation");
    deliver(completion_packet(25, 4, 5)); social_held = true; drain();
    captain_alive = false; social_held = false; flush_social_completions(); drain(); captain_alive = true;
    check(relationship_events == completed_before_stale, "a disconnected/downed source cannot apply retained completion");
    deliver(completion_packet(26, 4, 5)); social_held = true; drain();
    ++reported_run; social_held = false; flush_social_completions(); drain(); --reported_run;
    check(relationship_events == completed_before_stale, "a retained completion cannot cross an adventure run");

    deliver(completion_packet(27, 4, 5)); social_held = true; drain();
    reset_replication(); g_run = reported_run; social_held = false;
    flush_social_completions(); drain();
    check(g_pending_social.empty() && relationship_events == completed_before_stale,
          "cancellation/reset discards held completions rather than replaying them on retry");
    deliver(completion_packet(28, 4, 5)); // old drain is queued before native cleanup
    reset_replication(); g_run = reported_run;
    deliver(completion_packet(29, 4, 5)); drain();
    check(g_pending_social.empty() && relationship_events == completed_before_stale + 1,
          "a stale queued drain cannot consume or invalidate a replacement generation's completion");

    // Retention is bounded even if a peer invents endlessly distinct targets.
    auto previous_failure = C.interaction_failed;
    unsigned social_failures = 0;
    C.interaction_failed = [&](const std::string& reason) {
        check(reason.find("social synchronization") != std::string::npos, "social overflow reports its synchronization boundary");
        ++social_failures; running = false;
    };
    social_held = true;
    for (unsigned i = 0; i < kMaxPendingSocial; ++i) deliver(completion_packet(100+i, 4, 5, 1, {1, 10000+i}));
    check(g_pending_social.size() == kMaxPendingSocial && queued.empty(), "held social retention stops at its fixed queue limit");
    deliver(completion_packet(100+kMaxPendingSocial, 4, 5, 1, {1, 20000})); drain();
    check(social_failures == 1 && g_pending_social.empty() && relationship_events == completed_before_stale + 1,
          "overflow fails explicitly without applying or silently losing a paused completion");
    C.interaction_failed = previous_failure; C.held = previous_held; running = true;
    reset_replication(); g_run = reported_run;
    field<int>(fake_play.data(), 0xB8) = 0;
    field<net::Vec3>(lookup(partner)->spatial.data(), 4) = saved_partner_position;
    session.cb.custom = {}; g_last_interaction.clear(); g_social_completions.clear();
    held_world_task_cases(session,client,reported_run);
    static_replication_cases(session, client);
    spatial_vtable[0xB8/4]=reinterpret_cast<void*>(&spatial_game_data);
    spatial_vtable[0x6C/4]=reinterpret_cast<void*>(&bounds);
    item_replication_cases(session,client);
    client.leave(); C.sess = &session; running = false;

    begin_native_attempt(fake_play.data()); running = true;
    field<int>(fake_play.data(), 0x90) = 3; g_run = 1;
    const char* near_command[] = {"nearplayer", "1"};
    check(debug(2, near_command, nullptr) == 0 && teleported == captain && teleported_position.x == 41 &&
          teleported_position.y == 50 && teleported_position.z == 60 && teleported_orientation.w == 1,
          "nearplayer defaults to local captain and uses native teleport pose");
    const char* actor_near[] = {"near", "1", "0", "-3", "4"};
    check(debug(5, actor_near, nullptr) == 0 && teleported == captain && teleported_position.x == 17 &&
          teleported_position.y == 4, "native actor fixture supports a clear signed planar click offset");
    const char* invalid_near[] = {"near", "1", "0", "nan"};
    check(debug(4, invalid_near, nullptr) == 1 && teleported_position.x == 17,
          "nonfinite near offset cannot teleport the captain");
    const char* distant_near[] = {"near", "1", "0", "24", "24"};
    check(debug(5, distant_near, nullptr) == 1 && teleported_position.x == 17,
          "fixture offset is bounded to 25 metres");
    const char* finish[] = {"finish", "success"};
    check(debug(2, finish, nullptr) == 0, "controlled host finish dispatches the ordinary native state hook");
    check(result_requests == 1 && native_terminals == 0, "native result waits for coordinator outside-frame authority");
    set_state(fake_play.data(), 6); tick(20);
    check(native_terminals == 0 && g_run == 0 && g_native_result.run == 1 && !g_native_result.applied,
          "Ended native requests remain blocked after replication reset before authoritative apply");
    reentrant_result = true; apply_result(true);
    apply_result(false); set_state(fake_play.data(), 6); tick(21);
    check(native_terminals == 1 && g_native_result.applied == 5,
          "same-run duplicate conflicting and reentrant native results apply at most once");
    set_state(fake_play.data(), 4);
    check(native_other_states == 1 && g_native_result.applied == 5, "ordinary cinematic transition is not swallowed by terminal latch");
    reported_run = 2; begin_native_attempt(fake_play.data()); running = true;
    field<int>(fake_play.data(), 0x90) = 3;
    set_state(fake_play.data(), 6); apply_result(false);
    check(result_requests == 2 && native_terminals == 2 && g_native_result.run == 2 && g_native_result.applied == 6,
          "next co-op attempt can apply a new result on the same native play object");
    reported_run = 0; running = false; begin_native_attempt(fake_play.data());
    set_state(fake_play.data(), 5); set_state(fake_play.data(), 6);
    check(native_terminals == 4 && !g_native_result.run, "a subsequent solo attempt retains ordinary native result behavior");
    reported_run = 3; begin_native_attempt(fake_play.data()); running = true; active();
    running = false; reported_run = 0; reset(); apply_result(false);
    check(native_terminals == 4, "explicit adapter reset cancels its native result context");
    reported_run = 4; begin_native_attempt(fake_play.data());
    set_state(fake_play.data(), 6); set_state(fake_play.data(), 6); apply_result(false);
    check(native_terminals == 5, "a pre-commit native loading failure keeps its path without duplicate terminal effects");

    // A faster host may complete the dialog and send a new act/result before
    // this guest's native Finished tail publishes its own cinematic end.
    check(client.join("127.0.0.1:" + std::to_string(session.port()), "deferred-goal-client", error),
          "deferred-goal fixture reconnects its isolated guest");
    for (int i = 0; i < 1000 && !client.connected(); ++i) {
        client.update(); session.update(); client.update();
        if (!client.connected()) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(client.connected(), "deferred-goal fixture guest is connected");
    C.sess = &client; running = true; reported_run = 500; g_run = reported_run;
    begin_native_attempt(fake_play.data());
    field<int>(fake_play.data(), 0x90) = 3; field<int>(fake_play.data(), 0xB8) = 0;
    field<uint8_t*>(fake_play.data(), 0x64) = hud_goals.data();
    field<uint8_t*>(fake_play.data(), 0x68) = hud_goals.data() + hud_goals.size();
    field<void*>(fake_play.data(), 0xC8) = hud_goals.data(); field<void*>(fake_play.data(), 0xCC) = live;
    a_SetAct = reinterpret_cast<uintptr_t>(&goal_act);
    bool finishing = true; C.held = [] { return true; }; C.goals_held = [&] { return finishing; };
    auto goal_packet = [&](uint32_t revision, uint32_t progress, uint64_t attempt = 500) {
        net::Writer packet; packet.u8(Goals); put_run(packet, attempt); packet.u32(revision);
        packet.u8(1); packet.u8(5); packet.u32(1700); packet.u32(0); packet.u8(3);
        packet.u8(1); packet.u32(progress); packet.u8(0); packet.u32(2); packet.u8(0); packet.u32(0);
        return packet;
    };
    auto receive_goals = [&](const net::Writer& packet) {
        net::Reader reader(packet.data.data(), packet.size()); on_message(0, reader);
    };
    unsigned terminals_before = native_terminals, updates_before = g_goal_updates;
    g_received_goals = 0;
    receive_goals(goal_packet(10, 3)); receive_goals(goal_packet(11, 5));
    receive_goals(goal_packet(10, 4)); receive_goals(goal_packet(11, 99));
    check(g_deferred_goals && g_deferred_goals->sequence == 11 && field<int>(fake_play.data(), 0xB8) == 0 &&
          field<int>(fake_play.data(), 0x90) == 3 && field<void*>(fake_play.data(), 0xC8) == hud_goals.data() &&
          field<void*>(fake_play.data(), 0xCC) == live && !goal_act_sets && native_terminals == terminals_before,
          "validated final Goals coalesce before result and preserve native dialog fields despite stale/duplicate revisions");
    check(goal_application_pending(), "coordinator result fence observes the validated deferred final goal batch");
    flush_deferred_goals(); check(queued.empty(), "held dialog cannot queue its pending act mutation");
    expected_hud_count = 5; finishing = false;
    flush_deferred_goals(); flush_deferred_goals();
    check(queued.size() == 1 && g_deferred_goals && goal_application_pending(),
          "native cleanup queues one coalesced goal batch while the coordinator still retains its result");
    drain();
    check(goal_act_sets == 1 && g_goal_updates == updates_before + 1 && field<int>(fake_play.data(), 0xB8) == 1 &&
          field<int>(fake_play.data(), 0x90) == 3 && field<uint32_t>(hud_goals.data(), 4) == 5 &&
          native_terminals == terminals_before && !goal_application_pending(),
          "final goal fields apply once before the coordinator alone may enter native results");
    apply_result(true); apply_result(false);
    check(native_terminals == terminals_before + 1 && field<int>(fake_play.data(), 0x90) == 5,
          "authoritative coordinator result commits once after final goal application");
    finishing = true; receive_goals(goal_packet(12, 6));
    check(bool(g_deferred_goals), "replacement pending goal snapshot is retained within one run");
    finishing = false; flush_deferred_goals();
    reset_replication(); reported_run = 501; g_run = reported_run;
    begin_native_attempt(fake_play.data()); field<int>(fake_play.data(), 0x90) = 3;
    expected_hud_count = 7; receive_goals(goal_packet(1, 7, 501));
    check(queued.size() == 2 && goal_application_pending(), "new-run goal work remains owned beside a reset-invalidated callback");
    drain(); receive_goals(goal_packet(99, 8, 500));
    check(goal_act_sets == 1 && field<uint32_t>(hud_goals.data(), 4) == 7 && !goal_application_pending(),
          "reset-invalidated and delayed prior-run goal batches cannot overwrite the new final fields");

    // Exercise the actual host terminal hook and existing reliable serializer.
    // Its completed goal packet must enter the peer queue before K_RESULT.
    C.sess = &session; running = true; g_run = reported_run;
    hud_goals[0] = 1; field<uint32_t>(hud_goals.data(), 4) = 1;
    field<int>(fake_play.data(), 0x90) = 3;
    std::vector<uint8_t> terminal_order; bool final_goal_observed = false;
    client.cb.custom = [&](uint8_t type, uint8_t from, net::Reader& reader) {
        if (type != net::MSG_ADVENTURE || from || !reader.left()) return;
        uint8_t kind = reader.u8();
        if (kind != Goals && kind != 11) return;
        terminal_order.push_back(kind);
        if (kind == Goals) {
            get_run(reader); reader.u32(); reader.u8(); reader.u8(); reader.u32(); reader.u32();
            uint8_t count = reader.u8(); uint8_t complete = reader.u8(); uint32_t progress = reader.u32();
            final_goal_observed = count == 3 && complete == 1 && progress == 1 && reader.ok();
        }
    };
    C.result = [&](bool success) {
        net::Writer packet; packet.u8(11); put_run(packet, reported_run); packet.u8(success ? 1 : 0);
        session.broadcast_reliable(net::MSG_ADVENTURE, packet); running = false;
    };
    set_state(fake_play.data(), 5);
    for (int i = 0; i < 1000 && terminal_order.size() < 2; ++i) {
        session.update(); client.update();
        if (terminal_order.size() < 2) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    check(terminal_order == std::vector<uint8_t>{Goals, 11} && final_goal_observed,
          "native host terminal path publishes its completed final Goals reliably before the result");
    session.leave();
    std::printf("adventure mirror: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
