#include "adventure_world.h"
#include "adventure_world_lifecycle.h"
#include "adventure_world_actions.h"
#include "adventure_world_vehicles.h"
#include "adventure_relationships.h"

#include <algorithm>
#include <array>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cwchar>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <tuple>
#include <vector>

#include "game.h"
#include "net/adventure_health.h"
#include "sporekit_game.hpp"

namespace adventure_world {

static const SkHost* H;
static SkMod* M;
static Ctx C;
static uintptr_t a_Mode, a_SetAct, a_GetMarker, a_Hostile, a_Befriend, a_Ally, a_Relationships, a_SocialEvent, a_AnimalDamage, a_SocialMode;
static uintptr_t a_GoalHUD;
static bool g_ok, g_applying;
static bool g_talk_cleanup, g_talk_presenting, g_talk_committing;
static uintptr_t a_GateKey, a_OpenGate;
using TalkStart = void(SK_THISCALL*)(void*,void*,void*);
using TalkEnd = void(SK_THISCALL*)(void*);
using Gate = void(SK_CDECL*)(void*,void*,void*);
static TalkStart o_TalkStart;
static TalkEnd o_TalkEnd;
static Gate o_Gate;
static void* powerup_for(void* object)
{
    void* powerup=game::cast(object,0x08367427);
    return powerup && skg::readable(powerup,0x3C) ? powerup : nullptr;
}
static uint64_t g_run;
struct NativeResult {
    void* play = nullptr;
    uint64_t run = 0;
    bool committed = false;
    int applied = 0; // terminal state claimed before entering native code
};
static NativeResult g_native_result;
static uint32_t g_sequence, g_goal_sequence;
static double g_last_send, g_last_goals;
static double g_last_item_apply;
static bool g_mirror_queued;
static bool g_catch_queued;
static std::string g_world_error;
static unsigned g_snapshots, g_hits, g_goal_updates;
static unsigned g_goal_hud_refreshes;
static bool g_goal_hud_dirty;
constexpr uint32_t kSpatial = 0x01186577, kCombatant = 0x013F94D4, kCreature = 0xCE9F6639;
constexpr size_t kMaxActors = 2048, kMaxGoals = 3;
enum Kind : uint8_t { Actors = 128, Goals = 129, Hit = 130, CaptainHit = 131, Interaction = 132, GoalEvent = 133, Roster = 134, SocialComplete = 135, StaticActors = 136, Attachments = 137, ItemThrow = 138 };
enum Action : uint8_t { PickUp, Drop, Give, Unlock };

struct Identity {
    uint32_t marker, incarnation;
    bool operator<(const Identity& b) const { return std::tie(marker, incarnation) < std::tie(b.marker, b.incarnation); }
    bool operator==(const Identity&) const = default;
};
struct Attachment {
    Identity item{}, holder{};
    uint8_t player = ga::kNoPlayer, slot = 0;
    bool operator==(const Attachment&) const = default;
};
struct AttachmentAssembly {
    uint32_t revision = 0;
    uint8_t act = 0;
    std::vector<Attachment> parts;
    std::vector<bool> received;
};
struct AttachmentSend { uint32_t revision = 0; size_t next = 0; };
static std::vector<Attachment> g_attachments;
static std::array<AttachmentAssembly,8> g_attachment_assemblies;
static std::array<std::vector<Attachment>,8> g_attachment_pending;
static std::array<uint32_t,8> g_attachment_received{};
static std::map<uint8_t, AttachmentSend> g_attachment_sent;
static uint32_t g_attachment_revision;
static int g_attachment_act = -1;
struct Flight { uint8_t owner; uint8_t act; net::Vec3 previous; double started, still_since = 0; };
static std::map<Identity, Flight> g_flying;
static uintptr_t a_Physics, a_Impulse, a_ThrowGrace;
static uint64_t g_last_throw[4]{};
static unsigned g_item_throws, g_item_catches, g_item_gives;
static std::set<void*> g_attached_spatials;
struct NativeActor {
    void* object = nullptr;
    void* spatial = nullptr;
    void* combatant = nullptr;
    uint32_t marker = 0, incarnation = 0, noun = 0;
    uint8_t powerup_kind = 0;
    bool static_candidate = false;
};
struct ActorState {
    Identity id{};
    uint32_t noun = 0;
    net::Vec3 position;
    net::Quat orientation;
    float health = 0, maximum = 0, max_tuning = 0;
    uint8_t flags = 0; // 1: combatant, 2: host actor dead, 4: native social completion
    int32_t party_relationship = -1; // native enum 0..7, -1 unavailable
    uint32_t sequence = 0;
    double seen = 0;
    bool static_actor = false;
};
struct Binding { void* object; uint32_t incarnation; int act = -1; };
static std::vector<NativeActor> g_native;
static bool g_native_overflow;
static std::map<uint32_t, uint8_t*> g_markers;
static std::map<Identity, double> g_spawn_attempt;
static std::map<Identity, ActorState> g_remote;
static std::map<Identity, Binding> g_bindings;
static std::map<Identity, ga::HealthScale> g_health_baselines;
static std::map<uint8_t, uint64_t> g_last_hit;
static std::map<uint8_t, uint64_t> g_last_captain_hit;
static std::map<uint8_t, uint64_t> g_last_interaction;
static std::map<Identity, std::pair<uint8_t, double>> g_dropped;
static std::set<std::tuple<uint8_t, uint8_t, uint32_t, uint32_t>> g_social_events;
static std::set<std::tuple<uint8_t, uint8_t, uint32_t, uint32_t>> g_social_completions;
static std::set<Identity> g_opened_gates;
static std::set<Identity> g_retired_items;
static uint64_t g_hit_sequence;
static uint64_t g_interaction_sequence;
struct DamageSource { uint32_t victim_incarnation; ga::Source source; double time; };
static std::map<void*, DamageSource> g_last_source;
static uint32_t g_roster_sequence;
static std::set<Identity> g_roster_live;
struct StaticHostActor { ActorState state, initial; bool promoted = false; };
struct StaticSent { ActorState state; bool promoted = false; };
static std::map<Identity, StaticHostActor> g_static_host;
static std::map<uint8_t, std::map<Identity, StaticSent>> g_static_sent;
static std::map<uint8_t, Identity> g_static_cursor;
// Reliable baselines can precede the queued native act change. Retain each
// bounded act independently until its native SetCurrentAct has completed.
static std::array<std::map<Identity, ActorState>, 8> g_static_pending;
static int g_static_act = -1;
struct ReplicationStats {
    uint64_t static_bytes = 0, dynamic_bytes = 0, baselines = 0, state_updates = 0, promotions = 0, deferred = 0;
    size_t static_actors = 0, dynamic_actors = 0;
};
static ReplicationStats g_replication_stats;

using Check_t = void(SK_THISCALL*)(void*);
using Goals_t = bool(SK_THISCALL*)(void*);
using State_t = void(SK_THISCALL*)(void*, int);
using Pick_t = bool(SK_THISCALL*)(void*, void*, int, bool);
using Drop_t = bool(SK_THISCALL*)(void*, void*, int, bool);
using Item_t = void(SK_THISCALL*)(void*, void*, void*);
using Count_t = void(SK_THISCALL*)(void*, void*, void*, void*);
using Social_t = void(SK_THISCALL*)(void*, void*);
static Check_t o_Check, o_Complete;
static Goals_t o_Goals;
static State_t o_State;
static Pick_t o_Pick;
static Drop_t o_Drop;
static Item_t o_Picked, o_Dropped;
static Count_t o_Count;
static Social_t o_Befriend, o_Ally;
using Damage_t = int(SK_THISCALL*)(void*, float, uint32_t, int, const net::Vec3*, void*);
static Damage_t o_GenericDamage;
using Avatar_t = void*(SK_THISCALL*)(void*);
static Avatar_t o_Avatar;
static thread_local void* g_override_avatar;

static bool goals_vector(uint8_t*, uint8_t*&, size_t&);
static void observe_native_attempt(uint64_t);
static void reset_replication();
static int item_slot(void*, void*, bool);
static bool same_pose(const ActorState&, const ActorState&);
static void send_goals();

static bool host() { return C.sess && C.sess->role() == net::Role::Host; }
static bool active()
{
    return g_ok && C.sess && C.sess->connected() && C.active && C.active() && C.run && C.run();
}
static bool guest() { return active() && !host(); }
static uint64_t run() { return C.run ? C.run() : 0; }
static void put_run(net::Writer& w, uint64_t n) { w.u32((uint32_t)n); w.u32((uint32_t)(n >> 32)); }
static uint64_t get_run(net::Reader& r) { uint64_t lo = r.u32(); return lo | ((uint64_t)r.u32() << 32); }
static bool finite(const net::Vec3& p) { return std::isfinite(p.x) && std::isfinite(p.y) && std::isfinite(p.z); }

static uint8_t* mode()
{
    uint8_t* m = a_Mode ? *(uint8_t**)a_Mode : nullptr;
    return m && skg::readable(m, 0xD0) && skg::field<uint32_t>(m, 0xCC) == 2 ? m : nullptr;
}
static uint8_t* play()
{
    uint8_t* m = mode();
    auto* p = m ? skg::field<uint8_t*>(m, 0x78) : nullptr;
    return p && skg::readable(p, 0x108) ? p : nullptr;
}
static uint8_t* scenario_data()
{
    auto* m = mode();
    auto* data = m ? skg::field<uint8_t*>(m, 0x74) : nullptr;
    return data && skg::readable(data, 0xD0) ? data : nullptr;
}
static void observe_native_attempt(uint64_t id)
{
    auto* p = play();
    if (!p || !id) return;
    if (g_native_result.play != p || g_native_result.run != id) g_native_result = {p, id, false, 0};
    g_native_result.committed = true;
}
static uint8_t owner(void* object)
{
    if (!object || !C.captain_of || !C.sess) return ga::kNoPlayer;
    for (const auto& player : C.sess->players()) if (C.captain_of(player.id) == object) return player.id;
    return ga::kNoPlayer;
}
static bool hostile(void* object)
{
    if (C.hostile_to_party) return C.hostile_to_party(object);
    if (!a_Hostile || !object) return false;
    void* creature = game::cast(object, kCreature);
    void* captain = game::cast(game::avatar(), kCreature);
    if (creature) return captain && skg::thiscall<bool>(a_Hostile, creature, captain);
    bool result = false; std::string error;
    auto* p = play();
    return p && adventure_world_actions::hostile(scenario_data(), object, skg::field<int>(p, 0xB8), result, error) && result;
}
static int SK_THISCALL generic_damage(void* self, float amount, uint32_t political, int type, const net::Vec3* direction, void* attacker)
{
    // The animal override is already filtered by game.cpp before reaching this
    // common base. Other combatants use their native ToGameData interface.
    if (!active() || (uintptr_t)skg::vtable(self)[0x18 / 4] == a_AnimalDamage ||
        skg::field<bool>(self, 0x80) || skg::field<int>(self, 0x34) == 2)
        return o_GenericDamage(self, amount, political, type, direction, attacker);
    void* victim = skg::vcall<void*>(self, 0x0C / 4);
    void* source_object = attacker ? skg::vcall<void*>(attacker, 0x0C / 4) : nullptr;
    if (!damage_filter(victim, amount, type, source_object)) return 0;
    return o_GenericDamage(self, amount, political, type, direction, attacker);
}
static bool marker_of(void* object, uint32_t& marker)
{
    uint8_t* m = mode();
    void* data = m ? skg::field<void*>(m, 0x74) : nullptr;
    if (!object || !data || !a_GetMarker || !skg::readable(object, 0x28)) return false;
    uint8_t* direct = skg::field<uint8_t*>(object, 0x18);
    if (direct && skg::readable(direct, 0x234) && skg::field<int>(direct, 0) == skg::field<int>(object, 0x1C)) {
        marker = skg::field<uint32_t>(direct, 0x228);
        if (marker != UINT32_MAX) return true;
    }
    uint8_t* p = (uint8_t*)skg::thiscall<void*>(a_GetMarker, data, object);
    if (!p || !skg::readable(p, 0x22C)) return false;
    marker = skg::field<uint32_t>(p, 0x228);
    return marker != UINT32_MAX;
}

static void refresh_markers()
{
    // A placement can be invisible in the guest's previous act and therefore
    // have no observed native object. Its resource marker still exists and is
    // required to recreate the host's newly visible actor after SetCurrentAct.
    g_markers.clear();
    auto* data = scenario_data();
    auto* resource = data ? skg::field<uint8_t*>(data, 0x10) : nullptr;
    if (!resource || !skg::readable(resource, 0x2C18)) return;
    uintptr_t begin = skg::field<uintptr_t>(resource, 0x2C10), end = skg::field<uintptr_t>(resource, 0x2C14);
    if (end < begin || (end - begin) % 0x238 || (end - begin) / 0x238 > 16384 ||
        (end != begin && !skg::readable(reinterpret_cast<void*>(begin), end - begin))) return;
    for (size_t i = 0; i < (end - begin) / 0x238; ++i) {
        auto* marker = reinterpret_cast<uint8_t*>(begin + i * 0x238 + 4);
        if (skg::field<uint32_t>(marker, 0x228) == i && skg::field<int>(marker, 0) >= 0)
            g_markers[static_cast<uint32_t>(i)] = marker;
    }
}

static void enumerate()
{
    // The registry, casts and marker getters below do not mutate native memory.
    skg::ReadOnlyMemoryScope memory;
    g_native.clear();
    g_native_overflow = false;
    refresh_markers();
    game::for_each_object([](void* object, uint32_t noun, void*) {
        if (g_native.size() >= kMaxActors) { g_native_overflow = true; return false; }
        uint32_t marker;
        if (!game::object_alive(object) || skg::field<bool>(object, 0x20) ||
            owner(object) != ga::kNoPlayer || !marker_of(object, marker)) return true;
        void* spatial = game::cast(object, kSpatial);
        if (!spatial || !skg::readable(spatial, 0x20)) return true;
        void* combatant = game::cast(object, kCombatant);
        if (combatant && !skg::readable(combatant, 0x3C)) combatant = nullptr;
        void* powerup=powerup_for(object);
        int kind=powerup ? skg::field<int>(powerup,4) : 0;
        // These common Cast interfaces are already used by the verified native
        // action adapters. Runtime creatures/vehicles win over authored disguises.
        bool stationary = !game::cast(object, kCreature) && !game::cast(object, 0x0137E8E0) &&
            (game::cast(object, 0x0E9CB8BA) || game::cast(object, 0x0175CDC9) || game::cast(object, 0x03A25119));
        // A grenade has independent flight/contact state even before its first
        // observed movement. Never move it onto the stationary path.
        if (kind == 10 || kind == 13 || (powerup && skg::field<bool>(powerup, 0x30))) stationary = false;
        if (stationary && C.captain_of && C.sess) for (const auto& player : C.sess->players())
            if (item_slot(C.captain_of(player.id), spatial, false) >= 0) { stationary = false; break; }
        g_native.push_back({object, spatial, combatant, marker, skg::field<uint32_t>(object, 0x24), noun,
            uint8_t(kind>=1 && kind<=15 ? kind : 0), stationary});
        return true;
    }, nullptr);
}

static NativeActor* actor_for(void* object)
{
    for (auto& a : g_native) if (a.object == object) return &a;
    return nullptr;
}

static float max_health(void* combatant)
{
    // +2C is a multiplier for creatures and an absolute tuning value for some other
    // combatants. The native virtual getter is the only shared maximum-HP interface.
    return combatant ? skg::vcall<float>(combatant, 0x58 / 4) : 0.f;
}

static bool identity(void* object, Identity& id)
{
    if (host()) {
        uint32_t marker;
        if (!object || !marker_of(object, marker)) return false;
        id = {marker, skg::field<uint32_t>(object, 0x24)};
        return true;
    }
    for (const auto& [remote, local] : g_bindings) if (local.object == object) {
        auto* native = actor_for(object);
        if (native && native->incarnation == local.incarnation &&
            skg::field<uint32_t>(object, 0x24) == local.incarnation) { id = remote; return true; }
    }
    return false;
}
static void* object_for(Identity id)
{
    if (!host()) {
        auto it = g_bindings.find(id);
        auto* native = it != g_bindings.end() ? actor_for(it->second.object) : nullptr;
        return native && native->incarnation == it->second.incarnation ? native->object : nullptr;
    }
    for (const auto& actor : g_native) if (actor.marker == id.marker && actor.incarnation == id.incarnation) return actor.object;
    return nullptr;
}
static void* object_from_spatial(void* spatial)
{
    // Native pickup/drop helpers use this Cast slot to recover cGameData.
    return spatial ? skg::vcall<void*>(spatial, 0xB8 / 4, 0x017F243Bu) : nullptr;
}
static void write_identity(net::Writer& w, Identity id) { w.u32(id.marker); w.u32(id.incarnation); }
static Identity read_identity(net::Reader& r) { return {r.u32(), r.u32()}; }
static int item_slot(void* creature, void* item, bool free_slot)
{
    if (!creature || !skg::readable(creature, 0xE48)) return -1;
    auto* begin = skg::field<uint8_t*>(creature, 0xE40);
    auto* end = skg::field<uint8_t*>(creature, 0xE44);
    uintptr_t b = (uintptr_t)begin, e = (uintptr_t)end;
    if (e < b || (e - b) % 0x24 || (e - b) / 0x24 > 128 || (e > b && !skg::readable(begin, e - b))) return -1;
    for (size_t i = 0; i < (e - b) / 0x24; ++i) {
        auto* slot = begin + i * 0x24;
        if ((!free_slot && skg::field<void*>(slot, 0x20) == item) ||
            (free_slot && skg::field<int>(slot, 0) != -1 && !skg::field<void*>(slot, 0x20) && !skg::field<uint32_t>(slot, 4))) return (int)i;
    }
    return -1;
}
template<class F> static void each_slot(void* creature, F callback)
{
    if (!creature || !skg::readable(creature,0xE48)) return;
    uintptr_t first=skg::field<uintptr_t>(creature,0xE40),last=skg::field<uintptr_t>(creature,0xE44);
    if(last<first || (last-first)%0x24 || (last-first)/0x24>128 ||
       (first!=last && !skg::readable(reinterpret_cast<void*>(first),last-first))) return;
    for(size_t i=0;i<(last-first)/0x24;++i) {
        void* spatial=skg::field<void*>(reinterpret_cast<void*>(first+i*0x24),0x20);
        if(spatial) callback(uint8_t(i),spatial);
    }
}
template<class F> static void each_holder(F callback)
{
    if(C.captain_of && C.sess) for(const auto& player:C.sess->players()) {
        void* captain=C.captain_of(player.id);
        if(captain) callback(captain,player.id,Identity{});
    }
    for(const auto& actor:g_native) if(game::cast(actor.object,kCreature))
        callback(actor.object,ga::kNoPlayer,Identity{actor.marker,actor.incarnation});
}
static bool attached(void* spatial)
{
    return g_attached_spatials.count(spatial)!=0;
}
static void refresh_attachment_spatials()
{
    skg::ReadOnlyMemoryScope memory;
    g_attached_spatials.clear();
    each_holder([](void* holder,uint8_t,Identity) {
        each_slot(holder,[](uint8_t,void* spatial) { g_attached_spatials.insert(spatial); });
    });
}
static void sample_attachments(int act)
{
    skg::ReadOnlyMemoryScope memory;
    std::map<Identity,Attachment> relations;
    each_holder([&](void* holder,uint8_t player,Identity holder_id) {
        each_slot(holder,[&](uint8_t slot,void* spatial) {
            Identity item;
            if(relations.size()<kMaxActors && identity(object_from_spatial(spatial),item))
                relations.emplace(item,Attachment{item,holder_id,player,slot});
        });
    });
    std::vector<Attachment> current;
    for(const auto& [id,relation]:relations) { (void)id; current.push_back(relation); }
    if(act!=g_attachment_act || current!=g_attachments) {
        g_attachment_act=act; g_attachments=std::move(current); ++g_attachment_revision;
        if(!g_attachment_revision) ++g_attachment_revision;
    }
}
static void send_attachments(int act)
{
    sample_attachments(act);
    for(const auto& player:C.sess->players()) if(player.id) {
        auto& sent=g_attachment_sent[player.id];
        if(sent.revision!=g_attachment_revision) sent={g_attachment_revision,0};
        if(sent.next>g_attachments.size()) continue;
        for(unsigned batch=0;batch<2 && sent.next<=g_attachments.size();++batch) {
            if(C.sess->pending_to(player.id)>=64) break;
            size_t count=std::min<size_t>(40,g_attachments.size()-sent.next);
            net::Writer w; w.u8(Attachments); put_run(w,g_run); w.u32(g_attachment_revision); w.u8(uint8_t(act));
            w.u16(uint16_t(g_attachments.size())); w.u16(uint16_t(sent.next)); w.u8(uint8_t(count));
            for(size_t i=0;i<count;++i) {
                const auto& relation=g_attachments[sent.next+i]; write_identity(w,relation.item);
                write_identity(w,relation.holder); w.u8(relation.player); w.u8(relation.slot);
            }
            if(!C.sess->send_reliable_to(player.id,net::MSG_ADVENTURE,w)) break;
            sent.next+=count;
            if(sent.next==g_attachments.size()) { ++sent.next; break; }
        }
    }
}
static void apply_attachments(int act)
{
    // Both endpoints can arrive after their reliable relation. Keep the complete
    // host snapshot and retry binding without guessing at a same-marker sibling.
    if(act<0 || act>=8 || !g_attachment_received[act]) return;
    g_attachments=g_attachment_pending[act]; g_attachment_act=act;
    std::map<void*,std::pair<void*,uint8_t>> expected;
    for(const auto& relation:g_attachments) {
        void* item=object_for(relation.item),*spatial=game::cast(item,kSpatial);
        void* holder=relation.player==ga::kNoPlayer ? object_for(relation.holder) : C.captain_of(relation.player);
        if(!spatial || !holder || !game::cast(holder,kCreature)) continue;
        auto predicted=g_dropped.find(relation.item);
        if(relation.player==C.sess->my_id() && predicted!=g_dropped.end() &&
           predicted->second.first==relation.player && H->time_seconds()-predicted->second.second<=2 &&
           item_slot(holder,spatial,false)<0) continue;
        expected[spatial]={holder,relation.slot};
    }
    g_applying=true;
    each_holder([&](void* holder,uint8_t,Identity) {
        std::vector<void*> release;
        each_slot(holder,[&](uint8_t slot,void* spatial) {
            auto found=expected.find(spatial);
            // Unbound identities remain pending, including relations whose holder
            // is not ready yet. Their previous native attachment is retained.
            Identity item;
            if(!identity(object_from_spatial(spatial),item)) return;
            bool mentioned=false;
            for(const auto& relation:g_attachments) if(relation.item==item) { mentioned=true; break; }
            if((found!=expected.end() && (found->second.first!=holder || found->second.second!=slot)) ||
                (!mentioned && found==expected.end())) release.push_back(spatial);
        });
        for(void* spatial:release) o_Drop(holder,spatial,0,false);
    });
    for(const auto& [spatial,endpoint]:expected) {
        void* holder=endpoint.first;
        if(item_slot(holder,spatial,false)==endpoint.second) continue;
        o_Pick(holder,spatial,int(endpoint.second),true);
    }
    g_applying=false;
}
struct AvatarScope {
    void* previous = nullptr;
    explicit AvatarScope(void* captain) {
        previous = g_override_avatar; g_override_avatar = captain;
    }
    ~AvatarScope() { g_override_avatar = previous; }
};
static void* SK_THISCALL native_avatar(void* self)
{
    return g_override_avatar ? g_override_avatar : o_Avatar(self);
}
static bool nearby(void* a, void* b, float range)
{
    net::Vec3 p, q;
    if (!game::object_position(a, p) || !game::object_position(b, q)) return false;
    float x = p.x - q.x, y = p.y - q.y, z = p.z - q.z;
    return x*x + y*y + z*z <= range * range;
}
static double simulation_now() { return C.simulation_time ? C.simulation_time() : H->time_seconds(); }
static bool world_box(void*,net::Vec3&,net::Vec3&);
static void record_flight(Identity id,uint8_t player)
{
    auto* p=play(); void* item=object_for(id); net::Vec3 lo,hi;
    if(host() && p && item && world_box(item,lo,hi)) {
        net::Vec3 center{(lo.x+hi.x)*0.5f,(lo.y+hi.y)*0.5f,(lo.z+hi.z)*0.5f};
        g_flying[id]={player,uint8_t(skg::field<int>(p,0xB8)),center,simulation_now(),0};
    }
}
struct ThrowTask { uint64_t run,event; uint8_t act,player; Identity item; net::Vec3 impulse; bool add; };
static void apply_item_throw(void* argument)
{
    std::unique_ptr<ThrowTask> task(static_cast<ThrowTask*>(argument));
    auto* p=play();
    if(!host() || !active() || task->run!=run() || !p || task->act!=skg::field<int>(p,0xB8) ||
       !C.captain_can_act(task->player) || (C.held && C.held())) return;
    enumerate();
    void* captain=C.captain_of(task->player),*item=object_for(task->item),*spatial=game::cast(item,kSpatial);
    auto dropped=g_dropped.find(task->item);
    bool owns=spatial && (item_slot(captain,spatial,false)>=0 ||
        (dropped!=g_dropped.end() && dropped->second.first==task->player && H->time_seconds()-dropped->second.second<=2));
    float speed2=task->impulse.x*task->impulse.x+task->impulse.y*task->impulse.y+task->impulse.z*task->impulse.z;
    void* powerup=powerup_for(item); int kind=powerup ? skg::field<int>(powerup,4) : 0;
    if(!captain || !spatial || !owns || kind==10 || kind==13 || g_flying.count(task->item) ||
       !nearby(captain,item,25.f) || !finite(task->impulse) || !std::isfinite(speed2) || speed2>250.f*250.f) return;
    g_applying=true;
    AvatarScope avatar(captain);
    bool released=item_slot(captain,spatial,false)<0 || o_Drop(captain,spatial,0,false);
    if(released) {
        void* physics=skg::cdecl_call<void*>(a_Physics);
        if(physics) {
            adventure_world_actions::throw_source(captain,item);
            skg::thiscall<void>(a_Impulse,physics,spatial,&task->impulse,task->add);
            skg::thiscall<void>(a_ThrowGrace,physics,game::cast(captain,kSpatial),spatial,0.5f);
            record_flight(task->item,task->player); ++g_item_throws;
        }
    }
    g_applying=false;
}
static bool ordinary_throw(void*,void* spatial,const net::Vec3& impulse,bool add)
{
    if(!active() || g_applying) return false;
    void* item=object_from_spatial(spatial); Identity id;
    if(!identity(item,id)) return false;
    auto dropped=g_dropped.find(id);
    if(dropped==g_dropped.end() || H->time_seconds()-dropped->second.second>2) return false;
    uint8_t player=dropped->second.first;
    if(!C.captain_can_act(player) || (C.held && C.held())) return true;
    float speed2=impulse.x*impulse.x+impulse.y*impulse.y+impulse.z*impulse.z;
    if(!finite(impulse) || !std::isfinite(speed2) || speed2>250.f*250.f) return true;
    if(host()) {
        if(!g_flying.count(id)) {
            adventure_world_actions::throw_source(C.captain_of(player),item);
            record_flight(id,player); ++g_item_throws;
        }
        return false;
    }
    if(player!=C.sess->my_id()) return true;
    auto* p=play(); if(!p) return true;
    net::Writer w; w.u8(ItemThrow); put_run(w,run()); put_run(w,++g_interaction_sequence);
    w.u8(uint8_t(skg::field<int>(p,0xB8))); w.u8(player); write_identity(w,id);
    w.f32(impulse.x); w.f32(impulse.y); w.f32(impulse.z); w.u8(add?1:0);
    // The native local throw retains its animation and predictive physics. Only
    // the host may accept a catch or change the shared attachment afterwards.
    return !C.sess->send_reliable_to(0,net::MSG_ADVENTURE,w);
}

static bool sweep_box(net::Vec3 start,net::Vec3 end,net::Vec3 minimum,net::Vec3 maximum,float& contact)
{
    float enter=0,leave=1;
    const float from[]={start.x,start.y,start.z},to[]={end.x,end.y,end.z};
    const float lo[]={minimum.x,minimum.y,minimum.z},hi[]={maximum.x,maximum.y,maximum.z};
    for(int axis=0;axis<3;++axis) {
        float delta=to[axis]-from[axis];
        if(std::fabs(delta)<1e-6f) { if(from[axis]<lo[axis] || from[axis]>hi[axis]) return false; }
        else {
            float a=(lo[axis]-from[axis])/delta,b=(hi[axis]-from[axis])/delta;
            if(a>b) std::swap(a,b);
            enter=std::max(enter,a); leave=std::min(leave,b); if(enter>leave) return false;
        }
    }
    contact=enter; return true;
}
static bool world_box(void* object,net::Vec3& minimum,net::Vec3& maximum)
{
    void* spatial=game::cast(object,kSpatial); if(!spatial) return false;
    struct Box { net::Vec3 lo,hi; } box;
    skg::vcall<Box*>(spatial,0x6C/4,&box);
    if(!finite(box.lo) || !finite(box.hi) || box.lo.x>box.hi.x || box.lo.y>box.hi.y || box.lo.z>box.hi.z) return false;
    minimum=box.lo; maximum=box.hi; return true;
}
static void catch_items()
{
    if(!host() || !play() || (C.held && C.held())) return;
    double now=simulation_now();
    for(auto it=g_flying.begin();it!=g_flying.end();) {
        auto& flight=it->second; void* item=object_for(it->first); void* spatial=game::cast(item,kSpatial);
        net::Vec3 lo,hi;
        if(!item || !spatial || flight.act!=skg::field<int>(play(),0xB8) ||
           !world_box(item,lo,hi)) { it=g_flying.erase(it); continue; }
        net::Vec3 position{(lo.x+hi.x)*0.5f,(lo.y+hi.y)*0.5f,(lo.z+hi.z)*0.5f};
        void* powerup=powerup_for(item); int kind=powerup ? skg::field<int>(powerup,4) : 0;
        if(kind==13 || attached(spatial)) { it=g_flying.erase(it); continue; }
        net::Vec3 half{(hi.x-lo.x)*0.5f,(hi.y-lo.y)*0.5f,(hi.z-lo.z)*0.5f};
        uint8_t collector=ga::kNoPlayer; float first=2;
        for(const auto& player:C.sess->players()) {
            if(!C.captain_can_act(player.id) || (player.id==flight.owner && now-flight.started<0.5)) continue;
            void* captain=C.captain_of(player.id); net::Vec3 a,b;
            if(item_slot(captain,nullptr,true)<0 || !adventure_world_actions::can_receive(captain,spatial) || !world_box(captain,a,b)) continue;
            a={a.x-half.x,a.y-half.y,a.z-half.z}; b={b.x+half.x,b.y+half.y,b.z+half.z};
            float contact;
            if(sweep_box(flight.previous,position,a,b,contact) &&
               (contact<first || (contact==first && player.id<collector))) { first=contact; collector=player.id; }
        }
        if(collector!=ga::kNoPlayer) {
            void* captain=C.captain_of(collector); int slot=item_slot(captain,nullptr,true);
            g_applying=true; AvatarScope avatar(captain);
            bool caught=slot>=0 && o_Pick(captain,spatial,slot,false); g_applying=false;
            if(caught) { ++g_item_catches; g_dropped.erase(it->first); it=g_flying.erase(it); continue; }
        }
        float dx=position.x-flight.previous.x,dy=position.y-flight.previous.y,dz=position.z-flight.previous.z;
        if(dx*dx+dy*dy+dz*dz<1e-6f) { if(!flight.still_since) flight.still_since=now; }
        else flight.still_since=0;
        if(flight.still_since && now-flight.still_since>1) { it=g_flying.erase(it); continue; }
        flight.previous=position; ++it;
    }
}
static bool interaction(uint8_t action, uint8_t player, Identity item, Identity recipient = {UINT32_MAX, UINT32_MAX})
{
    uint8_t* p = play();
    if (!p) return false;
    net::Writer w; w.u8(Interaction); put_run(w, run()); put_run(w, ++g_interaction_sequence);
    w.u8((uint8_t)skg::field<int>(p, 0xB8)); w.u8(action); w.u8(player);
    write_identity(w, item); write_identity(w, recipient);
    if (host()) { C.sess->broadcast_reliable(net::MSG_ADVENTURE, w); return true; }
    return C.sess->send_reliable_to(0, net::MSG_ADVENTURE, w);
}

static bool SK_THISCALL pick_item(void* self, void* spatial, int slot, bool replace)
{
    if(active() && C.held && C.held()) return false;
    if (!active() || g_applying || owner(self) == ga::kNoPlayer) return o_Pick(self, spatial, slot, replace);
    Identity id;
    if (!identity(object_from_spatial(spatial), id)) return host() ? o_Pick(self, spatial, slot, replace) : false;
    uint8_t player = owner(self);
    if(!C.captain_can_act(player)) return false;
    if (!host()) {
        if (player != C.sess->my_id()) return false;
        interaction(PickUp, player, id);
        return true; // the host's accepted native pickup supplies the inventory/attachment
    }
    for(const auto& other:C.sess->players())
        if(other.id!=player && item_slot(C.captain_of(other.id),spatial,false)>=0) return false;
    bool ok = o_Pick(self, spatial, slot, replace);
    if (ok) interaction(PickUp, player, id);
    return ok;
}
static void SK_THISCALL picked_item(void* self, void* holder, void* item)
{
    if (active() && !g_applying && owner(holder) == ga::kNoPlayer) {
        Identity id, recipient;
        if (identity(item, id) && identity(holder, recipient)) {
            auto it = g_dropped.find(id);
            if (it != g_dropped.end() && H->time_seconds() - it->second.second <= 5.0 &&
                (host() || it->second.first == C.sess->my_id()))
                interaction(Give, it->second.first, id, recipient);
        }
    }
    o_Picked(self, holder, item);
    if(active() && !g_applying) {
        Identity id; if(identity(item,id)) g_flying.erase(id);
    }
    if(active()) {
        uint8_t player=owner(holder); Identity id;
        if(player!=ga::kNoPlayer && identity(item,id))
            adventure_explosives::picked({id.marker,id.incarnation},player);
    }
}
static void SK_THISCALL dropped_item(void* self, void* holder, void* item)
{
    if (C.explosive_visual && C.explosive_visual()) return;
    if (active() && !g_applying) {
        uint8_t player = owner(holder); Identity id;
        if (player != ga::kNoPlayer && identity(item, id)) {
            g_dropped[id] = {player, H->time_seconds()};
            if (host() || player == C.sess->my_id()) interaction(Drop, player, id);
        }
    }
    o_Dropped(self, holder, item);
}
static void completed_social(void* self, void* target, uint8_t type, Social_t original)
{
    if (active() && !g_applying && self == play() && C.captain_of) {
        Identity id;
        void* creature = game::cast(target, kCreature);
        if (creature && skg::readable(creature, 0xB5C) && identity(target, id)) {
            uint8_t act = static_cast<uint8_t>(skg::field<int>(self, 0xB8));
            auto completion = std::make_tuple(act, type, id.marker, id.incarnation);
            if (host()) {
                // Native local completion already changed the relationship or posse.
                g_social_completions.insert(completion);
            } else if (!g_social_completions.count(completion) && is_live_mirror(target) &&
                       (type != 4 || (skg::field<uint32_t>(creature, 0xB58) & 0x80)) &&
                       nearby(C.captain_of(C.sess->my_id()), target, 25.f)) {
                // D9BBD0 sets the native impressed flag before OnBefriend. OnAlly
                // is emitted by actual native recruitment, not a goal counter.
                net::Writer w; w.u8(SocialComplete); put_run(w, run()); put_run(w, ++g_interaction_sequence);
                w.u8(act); w.u8(type); write_identity(w, id);
                if (C.sess->send_reliable_to(0, net::MSG_ADVENTURE, w)) g_social_completions.insert(completion);
            }
        }
    }
    original(self, target);
}
static void SK_THISCALL befriended(void* self, void* target) { completed_social(self, target, 4, o_Befriend); }
static void SK_THISCALL allied(void* self, void* target) { completed_social(self, target, 5, o_Ally); }
static void SK_THISCALL count_goal(void* self, void* goal, void* first, void* second)
{
    if (g_talk_cleanup) return;
    if (!active()) { o_Count(self, goal, first, second); return; }
    uint8_t* begin; size_t count;
    if (!goals_vector((uint8_t*)self, begin, count)) return;
    uintptr_t delta = (uintptr_t)goal - (uintptr_t)begin;
    if (delta % 0x1AC || delta / 0x1AC >= count) return;
    int type = skg::field<int>(goal, 0x24);
    if(type==2 && !g_talk_committing) return;
    if (host()) {
        Identity target;
        if ((type == 2 || type == 4 || type == 5) && identity(object_from_spatial(first), target) &&
            !g_social_events.emplace((uint8_t)skg::field<int>(self, 0xB8), (uint8_t)(delta / 0x1AC), target.marker, target.incarnation).second) return;
        o_Count(self, goal, first, second);
        return;
    }
    // Physical goals are evaluated against host-native objects and inventories.
    // Befriend/Ally completion is relayed by its native callback independently
    // of whether this act contains a matching goal. Talk retains its dialog route.
    if (g_applying || type != 2) return;
    Identity target;
    if (!identity(object_from_spatial(first), target)) return;
    net::Writer w; w.u8(GoalEvent); put_run(w, run()); put_run(w, ++g_interaction_sequence);
    w.u8((uint8_t)skg::field<int>(self, 0xB8)); w.u8((uint8_t)(delta / 0x1AC)); w.u8((uint8_t)type);
    write_identity(w, target);
    C.sess->send_reliable_to(0, net::MSG_ADVENTURE, w);
}

static void SK_THISCALL talk_start(void* self, void* goal, void* target)
{
    if(!active() || g_talk_presenting) { o_TalkStart(self,goal,target); return; }
    uint8_t* begin; size_t count; Identity id;
    if(self!=play() || !goals_vector(static_cast<uint8_t*>(self),begin,count) || !identity(target,id)) return;
    uintptr_t offset=reinterpret_cast<uintptr_t>(goal)-reinterpret_cast<uintptr_t>(begin);
    if(offset%0x1AC || offset/0x1AC>=count || skg::field<int>(goal,0x24)!=2) return;
    adventure_flow::Scene s; s.kind=adventure_flow::Kind::Talk;
    s.act=uint8_t(skg::field<int>(self,0xB8)); s.goal=uint8_t(offset/0x1AC);
    s.marker=id.marker; s.incarnation=id.incarnation;
    adventure_flow::request_talk(s);
}
static void SK_THISCALL talk_end(void* self)
{
    if(!active() || adventure_flow::scene().kind!=adventure_flow::Kind::Talk) { o_TalkEnd(self); return; }
    if(adventure_flow::local_waiting()) return;
    // Late fallback only: the cinematic adapter normally holds Finished before
    // native caption cleanup, then invokes release_talk after its end message.
    adventure_flow::finish_talk();
}

bool validate_talk(uint8_t player, const adventure_flow::Scene& s)
{
    auto* p=play(); uint8_t* goals; size_t count;
    if(!active() || !host() || !p || s.run!=run() || s.act!=skg::field<int>(p,0xB8) ||
        skg::field<int>(p,0x90)!=3 || !C.captain_can_act(player) ||
        !goals_vector(p,goals,count) || s.goal>=count) return false;
    enumerate();
    void* target=object_for({s.marker,s.incarnation}); auto* goal=goals+s.goal*0x1AC;
    return target && !goal[0] && skg::field<int>(goal,0x24)==2 &&
        skg::field<int>(target,0x1C)==skg::field<int>(goal,0x28) && nearby(C.captain_of(player),target,25.f);
}
bool present_talk(const adventure_flow::Scene& s)
{
    auto* p=play(); uint8_t* goals; size_t count;
    if(!active() || !p || s.run!=run() || s.act!=skg::field<int>(p,0xB8) ||
        !game::avatar() || !goals_vector(p,goals,count) || s.goal>=count) return false;
    enumerate(); void* target=object_for({s.marker,s.incarnation});
    if(!target) return false;
    g_talk_presenting=true; o_TalkStart(p,goals+s.goal*0x1AC,target); g_talk_presenting=false;
    return true;
}
void release_talk(const adventure_flow::Scene& s)
{
    auto* p=play(); uint8_t* goals; size_t count;
    if(!active() || !p || s.run!=run() || s.act!=skg::field<int>(p,0xB8) ||
        !goals_vector(p,goals,count) || s.goal>=count) return;
    g_talk_cleanup=true; o_TalkEnd(p); g_talk_cleanup=false;
    if (!host()) return;
    enumerate(); void* target=object_for({s.marker,s.incarnation});
    if(target) {
        g_talk_committing=true;
        count_goal(p,goals+s.goal*0x1AC,game::cast(target,kSpatial),nullptr);
        g_talk_committing=false;
    }
}

static void SK_THISCALL check_goals(void* self)
{
    if(active() && C.held && C.held()) return;
    if (guest() && !g_applying) return;
    o_Check(self);
    if (!active() || !host() || !C.captain_of || !skg::readable(self, 0xBC)) return;
    int act = skg::field<int>(self, 0xB8);
    for (const auto& player : C.sess->players()) {
        if (!player.id || skg::field<int>(self, 0x90) != 3 || skg::field<int>(self, 0xB8) != act) continue;
        void* captain = C.captain_of(player.id);
        if (!captain) continue;
        AvatarScope scope(captain);
        o_Check(self);
    }
}
static void SK_THISCALL complete_act(void* self)
{
    if (!guest() || g_applying) o_Complete(self);
}
static bool SK_THISCALL update_goals(void* self)
{
    if(active() && C.held && C.held()) return true;
    return guest() && !g_applying ? true : o_Goals(self);
}
static void native_state(void* self, int state)
{
    if ((state == 5 || state == 6) && g_native_result.play == self && g_native_result.run) {
        if (g_native_result.applied) return;
        // Original SetState can invoke callbacks. Claim before entering it so a
        // reentrant or conflicting result cannot repeat progression/UI effects.
        g_native_result.applied = state;
    }
    o_State(self, state);
}
static void SK_THISCALL set_state(void* self, int state)
{
    bool running = active();
    if (running) observe_native_attempt(run());
    bool terminal = state == 5 || state == 6;
    bool scoped = g_native_result.play == self && g_native_result.run;
    if (terminal && scoped && g_native_result.applied) return;
    if (running && !g_applying && state >= 4) {
        if (host() && state == 4) { native_state(self, state); return; }
        if (host() && (state == 5 || state == 6) && C.result) {
            // Native CountGoal has already updated the completed objective.
            // Queue its reliable final batch before the coordinator's result.
            send_goals();
            C.result(state == 5);
        }
        return;
    }
    // The coordinator ends before its outside-frame native callback executes.
    // Keep authority during that interval and after replication has reset.
    if (terminal && scoped && g_native_result.committed && !g_applying) return;
    native_state(self, state);
}

static void apply_actor(NativeActor& actor, const ActorState& state)
{
    g_applying = true;
    ActorState local;
    local.position = skg::field<net::Vec3>(actor.spatial, 4);
    local.orientation = skg::field<net::Quat>(actor.spatial, 0x10);
    if (!attached(actor.spatial) && (!state.static_actor || !same_pose(local, state))) {
        skg::vcall<void>(actor.spatial, 0x38 / 4, &state.position);
        skg::vcall<void>(actor.spatial, 0x3C / 4, &state.orientation);
    }
    if (actor.combatant && (state.flags & 1)) {
        float current = skg::field<float>(actor.combatant, 0x38);
        skg::field<float>(actor.combatant, 0x2C) = state.max_tuning;
        if ((state.flags & 2) && current > 0) {
            net::Vec3 direction{};
            skg::vcall<int>(actor.combatant, 0x18 / 4, current + 1.f, UINT32_MAX, 0, &direction, (void*)nullptr);
        } else if (!(state.flags & 2)) skg::field<float>(actor.combatant, 0x38) = state.health;
    }
    void* creature = game::cast(actor.object, kCreature);
    if ((state.flags & 4) && creature && !(skg::field<uint32_t>(creature, 0xB58) & 0x80)) {
        void* relationships = skg::cdecl_call<void*>(a_Relationships);
        if (relationships) skg::thiscall<void>(a_SocialEvent, relationships, 3, (uint8_t*)creature + 0xB28, creature);
        skg::field<uint32_t>(creature, 0xB58) |= 0x80;
    }
    g_applying = false;
}

static void mirror(double now)
{
    refresh_attachment_spatials();
    auto compatible=[](const NativeActor& actor,const ActorState& state) {
        uint8_t expected=state.flags>>4;
        if(!expected) return true;
        int actual=actor.powerup_kind;
        return actual==expected || (expected==14 && actual==11);
    };
    unsigned recreated_count = 0;
    bool failed = false;
    std::set<void*> bound;
    for (auto it = g_bindings.begin(); it != g_bindings.end();) {
        auto* native = actor_for(it->second.object);
        if (!native || native->incarnation != it->second.incarnation || !g_remote.count(it->first) ||
            !compatible(*native,g_remote.at(it->first))) it = g_bindings.erase(it);
        else { bound.insert(native->object); ++it; }
    }
    for (const auto& [id, state] : g_remote) {
        NativeActor* actor = nullptr;
        auto existing = g_bindings.find(id);
        if (existing != g_bindings.end()) actor = actor_for(existing->second.object);
        if (!actor) {
            float best = 1e30f;
            for (auto& candidate : g_native) {
                if (candidate.marker != id.marker || candidate.noun != state.noun || bound.count(candidate.object) ||
                    !compatible(candidate,state)) continue;
                const auto& p = skg::field<net::Vec3>(candidate.spatial, 4);
                float dx = p.x - state.position.x, dy = p.y - state.position.y, dz = p.z - state.position.z;
                float distance = dx * dx + dy * dy + dz * dz;
                if (distance < best) { best = distance; actor = &candidate; }
            }
            if (actor) { g_bindings[id] = {actor->object, actor->incarnation, play() ? skg::field<int>(play(), 0xB8) : -1}; bound.insert(actor->object); }
            else if (recreated_count < 4 && g_markers.count(id.marker) && now - g_spawn_attempt[id] >= 1.0) {
                g_spawn_attempt[id] = now;
                void* created = nullptr; std::string error;
                auto* p = play();
                if (p && adventure_world_lifecycle::recreate(scenario_data(), g_markers[id.marker], skg::field<int>(p, 0xB8),
                                                             state.position, state.orientation, error, &created, state.noun,state.flags>>4)) {
                    ++recreated_count;
                    enumerate(); actor = actor_for(created);
                    if (actor && (actor->noun != state.noun || !compatible(*actor,state))) {
                        adventure_world_lifecycle::remove(scenario_data(), g_markers[id.marker], created, error);
                        actor = nullptr; failed = true;
                        g_world_error = "Native respawn factory produced a different actor noun or powerup kind for marker " + std::to_string(id.marker) + ".";
                    }
                    if (actor) { g_bindings[id] = {actor->object, actor->incarnation, skg::field<int>(p, 0xB8)}; bound.insert(actor->object); }
                } else if (!error.empty()) {
                    failed = true;
                    g_world_error = "marker=" + std::to_string(id.marker) + " incarnation=" + std::to_string(id.incarnation) + ": " + error;
                }
            }
        }
        // Native gates replace the closed object with a type-14 open ornament.
        // Repair late bindings reconstructed from the original closed class.
        if(actor && (state.flags>>4)==14) {
            void* powerup=powerup_for(actor->object);
            if(powerup && skg::readable(powerup,0x3C) && skg::field<int>(powerup,4)==11) {
                void* old=actor->object;
                g_applying=true; skg::cdecl_call<void>(a_OpenGate,powerup); g_applying=false;
                g_bindings.erase(id); bound.erase(old); enumerate(); actor=nullptr;
                for(auto& candidate:g_native) {
                    if(candidate.marker==id.marker && candidate.noun==state.noun && !bound.count(candidate.object) &&
                        candidate.powerup_kind==14) { actor=&candidate; break; }
                }
                if(actor) { g_bindings[id]={actor->object,actor->incarnation,skg::field<int>(play(),0xB8)}; bound.insert(actor->object); }
            }
        }
        if (actor) apply_actor(*actor, state);
    }
    if (!failed && g_bindings.size() == g_remote.size()) g_world_error.clear();
}

static void scale_health(NativeActor& actor, int act)
{
    if (!actor.combatant || !C.difficulty) return;
    Identity id{actor.marker, actor.incarnation};
    bool enemy = hostile(actor.object);
    if (!enemy && !g_health_baselines.count(id)) return;
    float factor = enemy ? C.difficulty().health_multiplier(C.party_size ? C.party_size() : C.sess->players().size()) : 1.f;
    g_health_baselines[id].apply(act, skg::field<float>(actor.combatant, 0x2C), skg::field<float>(actor.combatant, 0x38),
                                factor, [&] { return max_health(actor.combatant); });
}

static bool same_pose(const ActorState& a, const ActorState& b)
{
    float dx = a.position.x - b.position.x, dy = a.position.y - b.position.y, dz = a.position.z - b.position.z;
    if (dx * dx + dy * dy + dz * dz > 1e-8f) return false;
    const auto& p = a.orientation; const auto& q = b.orientation;
    float pn = p.x*p.x + p.y*p.y + p.z*p.z + p.w*p.w;
    float qn = q.x*q.x + q.y*q.y + q.z*q.z + q.w*q.w;
    float dot = p.x*q.x + p.y*q.y + p.z*q.z + p.w*q.w;
    // q and -q represent the same orientation. Compare to the original published
    // pose, rather than the previous sample, so slow movement accumulates.
    if (pn > 1e-8f && qn > 1e-8f) return dot * dot >= pn * qn * (1.f - 2e-7f);
    return p.x == q.x && p.y == q.y && p.z == q.z && p.w == q.w;
}

static bool same_gameplay(const ActorState& a, const ActorState& b)
{
    return a.noun == b.noun && a.health == b.health && a.maximum == b.maximum &&
        a.max_tuning == b.max_tuning && a.flags == b.flags && a.party_relationship == b.party_relationship;
}

static void write_pose(net::Writer& w, const ActorState& state)
{
    w.f32(state.position.x); w.f32(state.position.y); w.f32(state.position.z);
    w.f32(state.orientation.x); w.f32(state.orientation.y); w.f32(state.orientation.z); w.f32(state.orientation.w);
}

static void write_gameplay(net::Writer& w, const ActorState& state)
{
    w.f32(state.health); w.f32(state.maximum); w.f32(state.max_tuning); w.u8(state.flags);
    w.u8(state.party_relationship >= 0 && state.party_relationship <= 7 ? uint8_t(state.party_relationship) : 0xFF);
}

static ActorState sample_actor(NativeActor& actor, int act)
{
    scale_health(actor, act);
    ActorState state;
    state.id = {actor.marker, actor.incarnation}; state.noun = actor.noun; state.sequence = g_sequence;
    state.position = skg::field<net::Vec3>(actor.spatial, 4);
    state.orientation = skg::field<net::Quat>(actor.spatial, 0x10);
    state.health = actor.combatant ? skg::field<float>(actor.combatant, 0x38) : 0;
    state.maximum = max_health(actor.combatant);
    state.max_tuning = actor.combatant ? skg::field<float>(actor.combatant, 0x2C) : 0;
    if (!std::isfinite(state.health) || !std::isfinite(state.maximum) || !std::isfinite(state.max_tuning) ||
        state.maximum < 0 || state.max_tuning < 0) state.health = state.maximum = state.max_tuning = 0;
    state.health = std::clamp(state.health, 0.f, state.maximum);
    state.flags = actor.combatant ? uint8_t(1 | (state.health <= 0 ? 2 : 0)) : 0;
    state.flags |= uint8_t(actor.powerup_kind << 4);
    void* creature = game::cast(actor.object, kCreature);
    if (creature && (skg::field<uint32_t>(creature, 0xB58) & 0x80)) state.flags |= 4;
    void* relationships = creature && a_Relationships ? skg::cdecl_call<void*>(a_Relationships) : nullptr;
    if (relationships) adventure_relationships::query_local_party(relationships,
        static_cast<uint8_t*>(creature) + 0xB28, creature, state.party_relationship);
    if (state.party_relationship < 0 || state.party_relationship > 7) state.party_relationship = -1;
    return state;
}

static void send_static_actors(int act)
{
    constexpr size_t kPendingLimit = 64, kBatchesPerTick = 4, kChunkActors = 12;
    std::set<uint8_t> peers;
    for (const auto& player : C.sess->players()) if (player.id) {
        peers.insert(player.id);
        auto& sent = g_static_sent[player.id];
        for (auto it = sent.begin(); it != sent.end();)
            if (!g_static_host.count(it->first)) it = sent.erase(it); else ++it;
        size_t batches = 0;
        auto cursor = g_static_cursor.find(player.id);
        auto it = cursor == g_static_cursor.end() ? g_static_host.begin() : g_static_host.upper_bound(cursor->second);
        if (it == g_static_host.end()) it = g_static_host.begin();
        size_t remaining = g_static_host.size();
        while (remaining && batches < kBatchesPerTick) {
            if (C.sess->pending_to(player.id) >= kPendingLimit) { ++g_replication_stats.deferred; break; }
            net::Writer w; w.u8(StaticActors); put_run(w, g_run); w.u32(g_sequence); w.u8(uint8_t(act));
            size_t count_offset = w.size(); w.u8(0);
            std::vector<std::pair<Identity, StaticSent>> updates;
            uint64_t baselines = 0, changes = 0;
            while (remaining && updates.size() < kChunkActors) {
                auto candidate = it++;
                if (it == g_static_host.end()) it = g_static_host.begin();
                --remaining;
                // Resume after the last examined identity next tick. Frequently
                // changing early actors cannot monopolize the bounded budget.
                g_static_cursor[player.id] = candidate->first;
                const auto& entry = candidate->second;
                auto previous = sent.find(candidate->first);
                bool initial = previous == sent.end();
                bool promotion = entry.promoted && (initial || !previous->second.promoted);
                if (entry.promoted && !promotion) continue;
                if (!initial && !promotion && same_gameplay(previous->second.state, entry.state)) continue;
                // Full records seed reconstruction and reliably settle promotion.
                // State-only records never overwrite the retained transform.
                uint8_t fields = promotion ? 2 : initial ? 1 : 0;
                write_identity(w, candidate->first); w.u8(fields); w.u32(entry.state.noun);
                if (fields) write_pose(w, entry.state);
                write_gameplay(w, entry.state);
                updates.push_back({candidate->first, {entry.state, entry.promoted}});
                if (fields) ++baselines; else ++changes;
            }
            if (updates.empty()) break;
            w.data[count_offset] = uint8_t(updates.size());
            if (!C.sess->send_reliable_to(player.id, net::MSG_ADVENTURE, w)) break;
            for (const auto& update : updates) sent[update.first] = update.second;
            g_replication_stats.static_bytes += w.size();
            g_replication_stats.baselines += baselines; g_replication_stats.state_updates += changes;
            ++batches;
        }
    }
    for (auto it = g_static_sent.begin(); it != g_static_sent.end();)
        if (!peers.count(it->first)) it = g_static_sent.erase(it); else ++it;
    for (auto it = g_static_cursor.begin(); it != g_static_cursor.end();)
        if (!peers.count(it->first)) it = g_static_cursor.erase(it); else ++it;
}

static void send_actors()
{
    // Health field writes do not change mappings. All native calls are getters.
    skg::ReadOnlyMemoryScope memory;
    int act = play() ? skg::field<int>(play(), 0xB8) : 0;
    ++g_sequence;
    if (g_static_act != act) {
        g_static_act = act; g_static_host.clear(); g_static_sent.clear(); g_static_cursor.clear();
    }
    std::set<Identity> live;
    g_replication_stats.static_actors = g_replication_stats.dynamic_actors = 0;
    net::Writer w;
    size_t count_offset = 0;
    uint8_t count = 0;
    auto begin = [&] {
        w = {}; w.u8(Actors); put_run(w, g_run); w.u32(g_sequence);
        w.u8((uint8_t)act);
        count_offset = w.size(); w.u8(0); count = 0;
    };
    auto flush = [&] {
        if (!count) return;
        w.data[count_offset] = count;
        for (const auto& player : C.sess->players()) if (player.id && C.sess->send_unreliable_to(player.id, net::MSG_ADVENTURE, w))
            g_replication_stats.dynamic_bytes += w.size();
    };
    begin();
    for (auto& actor : g_native) {
        ActorState state = sample_actor(actor, act);
        if (!finite(state.position) || !std::isfinite(state.orientation.x) || !std::isfinite(state.orientation.y) ||
            !std::isfinite(state.orientation.z) || !std::isfinite(state.orientation.w)) continue;
        live.insert(state.id);
        auto tracked = g_static_host.find(state.id);
        if (actor.static_candidate || tracked != g_static_host.end()) {
            if (tracked == g_static_host.end()) tracked = g_static_host.emplace(state.id, StaticHostActor{state, state, false}).first;
            auto& entry = tracked->second;
            if (!entry.promoted && (!actor.static_candidate || !same_pose(entry.initial, state))) {
                entry.promoted = true; ++g_replication_stats.promotions;
            }
            entry.state = state;
            if (!entry.promoted) { ++g_replication_stats.static_actors; continue; }
        }
        ++g_replication_stats.dynamic_actors;
        if (count >= 12) { flush(); begin(); }
        write_identity(w, state.id); w.u32(state.noun); write_pose(w, state); write_gameplay(w, state);
        ++count;
    }
    flush();
    for (auto it = g_static_host.begin(); it != g_static_host.end();)
        if (!live.count(it->first)) it = g_static_host.erase(it); else ++it;
    send_static_actors(act);
}

static bool goals_vector(uint8_t* p, uint8_t*& begin, size_t& count)
{
    begin = skg::field<uint8_t*>(p, 0x64);
    uint8_t* end = skg::field<uint8_t*>(p, 0x68);
    uintptr_t b = (uintptr_t)begin, e = (uintptr_t)end;
    if (e < b || (e - b) % 0x1AC || (e - b) / 0x1AC > kMaxGoals || (e > b && !skg::readable(begin, e - b))) return false;
    count = (e - b) / 0x1AC;
    return true;
}

struct InteractionTask {
    uint64_t run;
    uint8_t from, act, action, player;
    Identity item, recipient;
};
static void apply_interaction(void* argument);
static void SK_CDECL activate_gate(void* captain, void* key, void* gate)
{
    if(!active() || g_applying) { o_Gate(captain,key,gate); return; }
    if(C.held && C.held()) return;
    uint8_t player=owner(captain);
    if(player==ga::kNoPlayer) { if(host()) o_Gate(captain,key,gate); return; }
    if(!host() && player!=C.sess->my_id()) return;
    Identity item_id,gate_id;
    if(!key || !gate || !identity(skg::vcall<void*>(key,1),item_id) ||
        !identity(skg::vcall<void*>(gate,1),gate_id)) return;
    if(!host()) interaction(Unlock,player,item_id,gate_id);
    if(host()) H->run_outside_frame(apply_interaction,new InteractionTask{run(),player,
        uint8_t(skg::field<int>(play(),0xB8)),Unlock,player,item_id,gate_id});
}
static void apply_interaction(void* argument)
{
    std::unique_ptr<InteractionTask> task((InteractionTask*)argument);
    uint8_t* p = play();
    if (!active() || task->run != run() || !p || task->act != skg::field<int>(p, 0xB8) || !C.captain_of ||
        (C.held && C.held())) return;
    enumerate();
    void* captain = C.captain_of(task->player);
    void* item = object_for(task->item);
    void* spatial = game::cast(item, kSpatial);
    if (!captain || !spatial || (host() && (task->from != task->player || !C.captain_can_act(task->player)))) return;
    bool accepted = false;
    g_applying = true;
    {
        AvatarScope avatar(captain);
        if (task->action == PickUp) {
            bool held_elsewhere = false;
            for (const auto& player : C.sess->players())
                if (player.id != task->player && item_slot(C.captain_of(player.id), spatial, false) >= 0) held_elsewhere = true;
            int held = item_slot(captain, spatial, false), slot = item_slot(captain, nullptr, true);
            if (!held_elsewhere && (held >= 0 || (slot >= 0 && (!host() || nearby(captain, item, 25.f))))) {
                accepted = held >= 0 || o_Pick(captain, spatial, slot, true);
                if (accepted) g_dropped.erase(task->item);
            }
        } else if (task->action == Drop) {
            if (item_slot(captain, spatial, false) >= 0) accepted = o_Drop(captain, spatial, 0, false);
            else accepted = !host(); // the originating native drop may already have completed
            if (accepted) g_dropped[task->item] = {task->player, H->time_seconds()};
        } else if(task->action==Unlock) {
            void* gate=object_for(task->recipient);
            void* key_powerup=powerup_for(item);
            void* gate_powerup=powerup_for(gate);
            if(key_powerup && gate_powerup && !g_opened_gates.count(task->recipient) &&
                skg::readable(key_powerup,0x3C) && skg::readable(gate_powerup,0x3C) &&
                skg::field<int>(key_powerup,4)==12 && skg::field<int>(gate_powerup,4)==11 &&
                item_slot(captain,spatial,false)>=0 &&
                skg::cdecl_call<uint32_t>(a_GateKey,key_powerup)==skg::cdecl_call<uint32_t>(a_GateKey,gate_powerup) &&
                (!host() || (C.captain_can_act(task->player) && nearby(captain,gate,25.f)))) {
                g_opened_gates.insert(task->recipient);
                if(host()) interaction(Unlock,task->player,task->item,task->recipient);
                else {
                    // Do not reconstruct the just-consumed key/closed gate from
                    // an older actor datagram while the new roster is in flight.
                    g_retired_items.insert(task->item); g_retired_items.insert(task->recipient);
                    g_remote.erase(task->item); g_remote.erase(task->recipient);
                    g_bindings.erase(task->item); g_bindings.erase(task->recipient);
                }
                o_Gate(captain,key_powerup,gate_powerup);
                accepted=true;
            }
        } else if (task->action == Give) {
            void* recipient = object_for(task->recipient);
            auto dropped = g_dropped.find(task->item);
            bool carrying = item_slot(captain, spatial, false) >= 0;
            bool recently_dropped = dropped != g_dropped.end() && dropped->second.first == task->player &&
                                    H->time_seconds() - dropped->second.second <= 5.0;
            bool held_elsewhere = false;
            if (host()) for (const auto& player : C.sess->players())
                if (player.id != task->player && item_slot(C.captain_of(player.id), spatial, false) >= 0) held_elsewhere = true;
            if (recipient && !held_elsewhere && (!host() || ((carrying || recently_dropped) && nearby(captain, recipient, 25.f)))) {
                std::string error;
                accepted = item_slot(game::cast(recipient,kCreature),spatial,false)>=0 ||
                    adventure_world_actions::give(captain, recipient, item, error);
                if (!accepted && !error.empty()) g_world_error = error;
                if(accepted) ++g_item_gives;
                if (accepted) g_dropped.erase(task->item);
            }
        }
    }
    g_applying = false;
    if(accepted && !host()) {
        auto& relations=g_attachment_pending[task->act];
        relations.erase(std::remove_if(relations.begin(),relations.end(),[&](const Attachment& relation) { return relation.item==task->item; }),relations.end());
        if(task->action==PickUp) {
            int slot=item_slot(captain,spatial,false);
            if(slot>=0) relations.push_back({task->item,{},task->player,uint8_t(slot)});
        } else if(task->action==Give) {
            void* recipient=object_for(task->recipient); int slot=item_slot(game::cast(recipient,kCreature),spatial,false);
            if(slot>=0) relations.push_back({task->item,task->recipient,ga::kNoPlayer,uint8_t(slot)});
        }
    }
    if (accepted && host() && task->action!=Unlock) interaction(task->action, task->player, task->item, task->recipient);
}

static bool request_container_give(void* captain, void* item, void* recipient)
{
    if (!active() || g_applying || !C.sess || owner(captain) != C.sess->my_id() || !play() ||
        !C.captain_can_act(C.sess->my_id()) || (C.held && C.held())) return false;
    Identity item_id, recipient_id;
    if (!identity(item, item_id) || !identity(recipient, recipient_id)) return false;
    uint8_t player = C.sess->my_id();
    if (!host()) return interaction(Give, player, item_id, recipient_id);
    H->run_outside_frame(apply_interaction, new InteractionTask{run(), player,
        static_cast<uint8_t>(skg::field<int>(play(), 0xB8)), Give, player, item_id, recipient_id});
    return true;
}

struct SocialCompletionTask { uint64_t run; uint8_t from, act, type; Identity target; };
static void apply_social_completion(void* argument)
{
    std::unique_ptr<SocialCompletionTask> task((SocialCompletionTask*)argument);
    uint8_t* p = play();
    if (!active() || !host() || task->run != run() || !p || skg::field<int>(p, 0x90) != 3 ||
        task->act != skg::field<int>(p, 0xB8) || (task->type != 4 && task->type != 5) ||
        !C.captain_of || !C.captain_can_act || !C.captain_can_act(task->from)) return;
    enumerate();
    void* target = object_for(task->target);
    void* captain = C.captain_of(task->from);
    void* creature = game::cast(target, kCreature);
    void* combatant = game::cast(target, kCombatant);
    if (!target || !captain || !creature || !combatant || !skg::readable(creature, 0xB60) ||
        skg::field<int>(target, 0x1C) < 0 || skg::field<bool>(creature, 0xB5E) ||
        !std::isfinite(skg::field<float>(combatant, 0x38)) || skg::field<float>(combatant, 0x38) <= 0 ||
        owner(target) != ga::kNoPlayer || !nearby(captain, target, 25.f)) return;
    auto completion = std::make_tuple(task->act, task->type, task->target.marker, task->target.incarnation);
    if (g_social_completions.count(completion)) return;
    AvatarScope avatar(captain);
    int mode = skg::thiscall<int>(a_SocialMode, creature);
    // Native social mode evaluates the authored relationship and posse capacity.
    // Never accept a client-supplied goal index, class, count or relationship.
    if (task->type == 4) {
        // A successful recruitment minigame also emits Befriend before Ally.
        // Preserve its native event3/impressed state before the mode4 recruit.
        if ((mode != 1 && mode != 2 && mode != 4) || (skg::field<uint32_t>(creature, 0xB58) & 0x80)) return;
        void* relationships = skg::cdecl_call<void*>(a_Relationships);
        if (!relationships) return;
        g_social_completions.insert(completion); // claim before reentrant native messages
        skg::thiscall<void>(a_SocialEvent, relationships, 3, (uint8_t*)creature + 0xB28, creature);
        skg::field<uint32_t>(creature, 0xB58) |= 0x80;
        o_Befriend(p, target); // the native callback evaluates any matching current goals
    } else {
        if (mode != 4) return;
        g_social_completions.insert(completion);
        std::string error;
        if (!adventure_world_actions::recruit(target, error)) {
            g_social_completions.erase(completion);
            if (!error.empty()) g_world_error = error;
        }
    }
}

struct ActorHitTask { uint64_t run, event; uint8_t from; Identity actor; float damage; int type; };
static void apply_hit(void* argument)
{
    std::unique_ptr<ActorHitTask> task((ActorHitTask*)argument);
    if (!active() || !host() || task->run != run() || task->event <= g_last_hit[task->from] || !C.captain_of) return;
    enumerate();
    void* target = object_for(task->actor);
    void* attacker = C.captain_of(task->from);
    void* combatant = game::cast(target, kCombatant);
    if (!target || !attacker || !combatant || !nearby(attacker, target, 250.f)) return;
    g_last_hit[task->from] = task->event;
    net::Vec3 direction{};
    g_applying = true;
    skg::vcall<int>(combatant, 0x18 / 4, task->damage, skg::field<uint32_t>(attacker, 0x30), task->type,
                   &direction, game::cast(attacker, kCombatant));
    g_applying = false;
    ++g_hits;
}

static void send_goals()
{
    uint8_t* p = play();
    uint8_t* goals; size_t count;
    if (!p || !goals_vector(p, goals, count)) return;
    net::Writer w; w.u8(Goals); put_run(w, g_run); w.u32(++g_goal_sequence);
    w.u8((uint8_t)skg::field<int>(p, 0xB8)); w.u8((uint8_t)skg::field<int>(p, 0x90));
    w.u32((uint32_t)std::max(0, skg::field<int>(p, 0xC0)));
    w.u32((uint32_t)skg::field<int>(p, 0xBC));
    w.u8((uint8_t)count);
    for (size_t i = 0; i < count; ++i) {
        auto* goal = goals + i * 0x1AC;
        w.u8(goal[0] ? 1 : 0); w.u32((uint32_t)std::max(0, skg::field<int>(goal, 4)));
    }
    C.sess->broadcast_reliable(net::MSG_ADVENTURE, w);
}

static void send_roster()
{
    uint8_t* p = play();
    if (!p || g_native_overflow) return;
    // Removal is based on a complete reliable roster, never a lost position packet.
    for (size_t first = 0; first < g_native.size() || first == 0; first += 80) {
        size_t count = std::min<size_t>(80, g_native.size() - first);
        net::Writer w; w.u8(Roster); put_run(w, g_run); w.u32(g_sequence);
        w.u8((uint8_t)skg::field<int>(p, 0xB8)); w.u16((uint16_t)g_native.size()); w.u16((uint16_t)first); w.u8((uint8_t)count);
        for (size_t i = first; i < first + count; ++i) write_identity(w, {g_native[i].marker, g_native[i].incarnation});
        C.sess->broadcast_reliable(net::MSG_ADVENTURE, w);
        if (!count) break;
    }
}

struct RosterTask { uint64_t run; uint32_t sequence; uint8_t act; std::vector<Identity> actors; };
static std::vector<Identity> g_roster_parts;
static std::vector<bool> g_roster_received;
static void retire_snapshots(const std::set<Identity>& live, uint32_t sequence, std::set<void*>& remove)
{
    for (auto it = g_bindings.begin(); it != g_bindings.end();) {
        auto state = g_remote.find(it->first);
        if (!live.count(it->first) && (state == g_remote.end() || state->second.sequence <= sequence)) {
            auto* native = actor_for(it->second.object);
            if (native && native->incarnation == it->second.incarnation) remove.insert(native->object);
            g_spawn_attempt.erase(it->first);
            g_remote.erase(it->first); it = g_bindings.erase(it);
        }
        else ++it;
    }
    // Complete host rosters retire snapshots even if no local actor could ever
    // bind them. Keeping an unbound old incarnation would recreate it forever.
    for (auto it = g_remote.begin(); it != g_remote.end();) {
        if (!live.count(it->first) && it->second.sequence <= sequence) {
            g_spawn_attempt.erase(it->first); it = g_remote.erase(it);
        } else ++it;
    }
}
static std::map<uint32_t, void*> reconcile_siblings(const std::set<Identity>& live, uint32_t sequence,
                                                   std::set<void*>& remove)
{
    // A complete roster authorizes removing local-only siblings only after every
    // host identity at that marker has a concrete, current native binding. Newer
    // UDP states are additional authoritative identities until the next roster.
    std::set<Identity> required = live;
    for (const auto& [id, state] : g_remote) if (state.sequence > sequence) required.insert(id);
    std::map<uint32_t, bool> complete;
    std::map<uint32_t, void*> retained;
    std::set<void*> bound;
    for (const auto& id : required) {
        auto inserted = complete.emplace(id.marker, true);
        auto state = g_remote.find(id);
        auto binding = g_bindings.find(id);
        auto* native = binding != g_bindings.end() ? actor_for(binding->second.object) : nullptr;
        bool valid = state != g_remote.end() && native && native->marker == id.marker &&
                     native->noun == state->second.noun && native->incarnation == binding->second.incarnation &&
                     skg::field<uint32_t>(native->object, 0x24) == binding->second.incarnation;
        if (!valid) inserted.first->second = false;
        else {
            bound.insert(native->object);
            retained.emplace(id.marker, native->object);
        }
    }
    for (const auto& actor : g_native) {
        auto marker = complete.find(actor.marker);
        if (marker == complete.end() || (marker->second && !bound.count(actor.object))) remove.insert(actor.object);
    }
    return retained;
}
static void apply_roster(void* argument)
{
    std::unique_ptr<RosterTask> task((RosterTask*)argument);
    uint8_t* p = play();
    if (!guest() || task->run != run() || !p || task->act != skg::field<int>(p, 0xB8) || task->sequence != g_roster_sequence) return;
    std::set<Identity> live(task->actors.begin(), task->actors.end());
    if (live.size() != task->actors.size()) return;
    enumerate();
    std::set<void*> remove;
    retire_snapshots(live, task->sequence, remove);
    auto retained = reconcile_siblings(live, task->sequence, remove);
    g_applying = true;
    for (void* object : remove) if (skg::readable(object, 0x28) && game::object_alive(object) && owner(object) == ga::kNoPlayer) {
        std::string error;
        auto* native = actor_for(object);
        if (!native || skg::field<uint32_t>(object, 0x24) != native->incarnation) continue;
        auto* marker = skg::field<uint8_t*>(object, 0x18);
        auto survivor = retained.find(native->marker);
        if (!adventure_world_lifecycle::remove(scenario_data(), marker, object, error,
                survivor != retained.end() ? survivor->second : nullptr) && !error.empty()) g_world_error = error;
    }
    g_applying = false;
    g_roster_live = std::move(live);
    enumerate();
}

static uint32_t g_received_goals;
static bool valid_actor_state(const ActorState& state, bool pose)
{
    return (!pose || (finite(state.position) && std::isfinite(state.orientation.x) && std::isfinite(state.orientation.y) &&
        std::isfinite(state.orientation.z) && std::isfinite(state.orientation.w))) &&
        std::isfinite(state.health) && std::isfinite(state.maximum) && std::isfinite(state.max_tuning) &&
        state.health >= 0 && state.maximum >= 0 && state.health <= state.maximum && state.max_tuning >= 0 &&
        state.max_tuning <= 1e9f && state.maximum <= 1e9f && !(state.flags & 8) &&
        state.party_relationship >= -1 && state.party_relationship <= 7;
}

static void merge_actor_state(const ActorState& state, std::map<Identity, ActorState>& destination, bool full, bool current)
{
    if (g_retired_items.count(state.id)) return;
    if (current && state.sequence < g_roster_sequence && !g_roster_live.count(state.id)) return;
    auto previous = destination.find(state.id);
    if (previous != destination.end() && (int32_t)(state.sequence - previous->second.sequence) <= 0) return;
    if (previous == destination.end() && (!full || destination.size() >= kMaxActors)) return;
    ActorState merged = state;
    if (!full) {
        if (previous->second.noun != state.noun) return;
        merged.position = previous->second.position; merged.orientation = previous->second.orientation;
    }
    merged.seen = H->time_seconds();
    destination[state.id] = merged;
}

static void apply_pending_static(uint8_t act)
{
    if (act >= g_static_pending.size()) return;
    for (const auto& [id, state] : g_static_pending[act]) {
        (void)id;
        merge_actor_state(state, g_remote, true, true);
    }
    for (size_t i = 0; i <= act; ++i) g_static_pending[i].clear();
}

struct GoalTask { uint64_t run; uint32_t sequence, time; int32_t remaining; uint8_t act, state; std::vector<std::pair<bool, uint32_t>> goals; };
static std::unique_ptr<GoalTask> g_deferred_goals;
static uint64_t g_goal_generation, g_goal_work_run;
static bool g_goal_apply_queued, g_goal_apply_active;
static bool goals_held() { return C.goals_held ? C.goals_held() : C.held && C.held(); }
bool goal_application_pending()
{
    uint64_t current = run();
    return current && ((g_deferred_goals && g_deferred_goals->run == current) ||
        (g_goal_work_run == current && (g_goal_apply_queued || g_goal_apply_active)));
}
static void refresh_goal_hud()
{
    // CountGoal calls this same HUD method after both partial and completed
    // progress. UpdateGoals evaluates/fails objectives and is not a UI refresh.
    auto* m = mode();
    auto* display = m ? skg::field<uint8_t*>(m, 0x6C) : nullptr;
    auto* hud = display && skg::readable(display, 0x14C) ? skg::field<uint8_t*>(display, 0x148) : nullptr;
    if (!g_goal_hud_dirty || !a_GoalHUD || !hud || !skg::readable(hud, 0xB4) ||
        !skg::field<void*>(hud, 0x24)) return;
    g_goal_hud_dirty = false;
    skg::thiscall<void>(a_GoalHUD, hud, false);
    ++g_goal_hud_refreshes;
}
static void apply_goals(void* arg)
{
    std::unique_ptr<GoalTask> task((GoalTask*)arg);
    if (!guest() || task->run != run() || task->sequence < g_received_goals ||
        task->act >= 8 || task->state > 6 || task->goals.size() > kMaxGoals) return;
    // Host completion may advance an act while this client is still processing
    // the native Finished tail. Do not replace dialog goals/targets or enter
    // results until local caption cleanup has finished. Keep one newest batch.
    if (goals_held()) {
        if (!g_deferred_goals || task->sequence >= g_deferred_goals->sequence) g_deferred_goals = std::move(task);
        return;
    }
    uint8_t* p = play();
    if (!p) return;
    struct Applying {
        explicit Applying(uint64_t run) { g_goal_work_run = run; g_goal_apply_active = true; }
        ~Applying() { g_goal_apply_active = false; }
    } applying(task->run);
    g_applying = true;
    if (skg::field<int>(p, 0xB8) != task->act) {
        skg::thiscall<void>(a_SetAct, p, (int)task->act, false);
        g_remote.clear(); g_bindings.clear();
        g_roster_live.clear(); g_roster_parts.clear(); g_roster_received.clear();
        g_spawn_attempt.clear(); g_markers.clear(); g_world_error.clear();
    }
    apply_pending_static(task->act);
    uint8_t* goals; size_t count;
    if (goals_vector(p, goals, count) && count == task->goals.size()) {
        for (size_t i = 0; i < count; ++i) {
            auto* goal = goals + i * 0x1AC;
            if ((goal[0] != 0) != task->goals[i].first ||
                skg::field<int>(goal, 4) != static_cast<int>(task->goals[i].second)) g_goal_hud_dirty = true;
            goal[0] = task->goals[i].first ? 1 : 0;
            skg::field<int>(goal, 4) = (int)task->goals[i].second;
        }
        skg::field<int>(p, 0xC0) = (int)task->time;
        skg::field<int>(p, 0xBC) = task->remaining;
        refresh_goal_hud();
        // Goal replication cannot award results/progression. The coordinator's
        // authoritative result commits only after these final fields are set.
        if (task->state >= 3 && task->state <= 4 && skg::field<int>(p, 0x90) != task->state &&
            !(task->state==3 && C.held && C.held())) native_state(p, task->state);
        ++g_goal_updates;
    }
    g_applying = false;
}

static void flush_deferred_goals()
{
    if (!g_deferred_goals || g_goal_apply_queued || goals_held()) return;
    g_goal_apply_queued = true; g_goal_work_run = g_deferred_goals->run;
    struct Task { uint64_t run, generation; };
    H->run_outside_frame([](void* argument) {
        std::unique_ptr<Task> task(static_cast<Task*>(argument));
        if (task->generation != g_goal_generation) return;
        g_goal_apply_queued = false;
        if (!g_deferred_goals || g_deferred_goals->run != task->run) return;
        apply_goals(g_deferred_goals.release());
    }, new Task{g_goal_work_run, g_goal_generation});
}

void on_message(uint8_t from, net::Reader& r)
{
    uint8_t kind = r.u8(); uint64_t id = get_run(r);
    if (!r.ok() || !active() || id != run()) return;
    // The reliable commit and first actor baseline can arrive in one receive
    // pump, before tick observes the new run. Initialize before caching either
    // message so that the next tick cannot erase an already acknowledged baseline.
    if (g_run != id) { reset_replication(); g_run = id; }
    if (kind == Roster && !host() && !from) {
        uint32_t sequence = r.u32(); uint8_t act = r.u8(); uint16_t total = r.u16(), first = r.u16(); uint8_t count = r.u8();
        if (!r.ok() || !play() || act != skg::field<int>(play(), 0xB8) || total > kMaxActors || count > 80 ||
            first > total || count > total - first || (!count && total) || sequence < g_roster_sequence) return;
        std::vector<Identity> chunk;
        for (uint8_t i = 0; i < count; ++i) chunk.push_back(read_identity(r));
        if (!r.ok() || r.left()) return;
        if (sequence > g_roster_sequence || g_roster_parts.size() != total) {
            if (sequence == g_roster_sequence && !g_roster_parts.empty()) return;
            g_roster_sequence = sequence; g_roster_parts.assign(total, {}); g_roster_received.assign(total, false);
        }
        for (size_t i = 0; i < chunk.size(); ++i) {
            if (g_roster_received[first + i] &&
                (g_roster_parts[first + i].marker != chunk[i].marker || g_roster_parts[first + i].incarnation != chunk[i].incarnation)) return;
            g_roster_parts[first + i] = chunk[i]; g_roster_received[first + i] = true;
        }
        if (std::all_of(g_roster_received.begin(), g_roster_received.end(), [](bool v) { return v; })) {
            auto* task = new RosterTask{id, sequence, act, g_roster_parts};
            H->run_outside_frame(apply_roster, task);
        }
    } else if(kind==Attachments && !host() && from==0) {
        uint32_t revision=r.u32(); uint8_t act=r.u8(); uint16_t total=r.u16(),first=r.u16(); uint8_t count=r.u8();
        if(!r.ok() || !revision || act>=8 || total>kMaxActors || count>40 || first>total || count>total-first ||
           (total && !count) || revision<=g_attachment_received[act]) return;
        std::vector<Attachment> chunk;
        for(uint8_t i=0;i<count;++i) {
            Attachment relation; relation.item=read_identity(r); relation.holder=read_identity(r);
            relation.player=r.u8(); relation.slot=r.u8();
            if(relation.slot>=128 || (relation.player!=ga::kNoPlayer && relation.player>=4) ||
               relation.item.marker==UINT32_MAX || (relation.player==ga::kNoPlayer && relation.holder.marker==UINT32_MAX)) return;
            chunk.push_back(relation);
        }
        if(!r.ok() || r.left()) return;
        auto& assembly=g_attachment_assemblies[act];
        if(revision<assembly.revision) return;
        if(revision!=assembly.revision) { assembly={}; assembly.revision=revision; assembly.act=act; assembly.parts.resize(total); assembly.received.resize(total); }
        if(assembly.parts.size()!=total) return;
        for(size_t i=0;i<chunk.size();++i) {
            if(assembly.received[first+i] && assembly.parts[first+i]!=chunk[i]) return;
            assembly.parts[first+i]=chunk[i]; assembly.received[first+i]=true;
        }
        if(std::all_of(assembly.received.begin(),assembly.received.end(),[](bool ready){return ready;})) {
            std::set<Identity> unique;
            std::set<std::tuple<uint8_t,Identity,uint8_t>> slots;
            for(const auto& relation:assembly.parts)
                if(!unique.insert(relation.item).second || !slots.emplace(relation.player,relation.holder,relation.slot).second) return;
            g_attachment_pending[act]=assembly.parts; g_attachment_received[act]=revision;
        }
    } else if(kind==ItemThrow && host() && from>0 && from<4) {
        uint64_t event=get_run(r); auto task=std::make_unique<ThrowTask>(); task->run=id; task->event=event;
        task->act=r.u8(); task->player=r.u8(); task->item=read_identity(r);
        task->impulse={r.f32(),r.f32(),r.f32()}; uint8_t add=r.u8(); task->add=add!=0;
        if(!r.ok() || r.left() || !event || event<=g_last_throw[from] || task->act>=8 || task->player!=from ||
           add>1 || !finite(task->impulse) || !C.captain_can_act(from)) return;
        g_last_throw[from]=event; H->run_outside_frame(apply_item_throw,task.release());
    } else if (kind == Interaction && ((host() && from) || (!host() && !from))) {
        uint64_t event = get_run(r);
        auto task = std::make_unique<InteractionTask>(); task->run = id; task->from = from;
        task->act = r.u8(); task->action = r.u8(); task->player = r.u8();
        task->item = read_identity(r); task->recipient = read_identity(r);
        if (!r.ok() || r.left() || !event || event <= g_last_interaction[from] || task->act >= 8 || task->action > Unlock ||
            (host() && task->player != from)) return;
        g_last_interaction[from] = event;
        H->run_outside_frame(apply_interaction, task.release());
    } else if (kind == GoalEvent && host() && from) {
        return; // Talk completion requires the shared scene barrier.
    } else if (kind == SocialComplete && host() && from) {
        uint64_t event = get_run(r);
        auto task = std::make_unique<SocialCompletionTask>(); task->run = id; task->from = from;
        task->act = r.u8(); task->type = r.u8(); task->target = read_identity(r);
        if (!r.ok() || r.left() || !event || event <= g_last_interaction[from] || task->act >= 8 ||
            (task->type != 4 && task->type != 5) || !C.captain_can_act || !C.captain_can_act(from)) return;
        g_last_interaction[from] = event;
        H->run_outside_frame(apply_social_completion, task.release());
    } else if (kind == StaticActors && !host() && from == 0) {
        uint32_t sequence = r.u32(); uint8_t act = r.u8(), count = r.u8();
        if (!r.ok() || act >= 8 || count > 12 || !play()) return;
        int current_act = skg::field<int>(play(), 0xB8);
        if (act < current_act) return;
        struct Update { ActorState state; bool full; };
        std::vector<Update> updates;
        for (uint8_t i = 0; i < count; ++i) {
            ActorState state; state.id = read_identity(r); uint8_t fields = r.u8(); state.noun = r.u32();
            if (fields > 2) return;
            bool full = fields != 0;
            if (full) {
                state.position = {r.f32(), r.f32(), r.f32()};
                state.orientation = {r.f32(), r.f32(), r.f32(), r.f32()};
            }
            state.health = r.f32(); state.maximum = r.f32(); state.max_tuning = r.f32(); state.flags = r.u8();
            uint8_t relationship = r.u8();
            if (relationship != 0xFF && relationship > 7) return;
            state.party_relationship = relationship == 0xFF ? -1 : relationship;
            state.sequence = sequence; state.static_actor = fields != 2;
            if (!r.ok() || !valid_actor_state(state, full)) return;
            updates.push_back({state, full});
        }
        if (r.left()) return;
        auto& destination = act == current_act ? g_remote : g_static_pending[act];
        for (const auto& update : updates) merge_actor_state(update.state, destination, update.full, act == current_act);
        ++g_snapshots;
    } else if (kind == Actors && !host() && from == 0) {
        uint32_t sequence = r.u32(); uint8_t act = r.u8(), count = r.u8();
        if (count > 12 || !play() || act != skg::field<int>(play(), 0xB8)) return;
        std::vector<ActorState> states;
        for (uint8_t i = 0; i < count; ++i) {
            ActorState state; state.id = {r.u32(), r.u32()}; state.noun = r.u32();
            state.position = {r.f32(), r.f32(), r.f32()};
            state.orientation = {r.f32(), r.f32(), r.f32(), r.f32()};
            state.health = r.f32(); state.maximum = r.f32(); state.max_tuning = r.f32(); state.flags = r.u8(); state.sequence = sequence;
            uint8_t relationship = r.u8();
            if (relationship != 0xFF && relationship > 7) return;
            state.party_relationship = relationship == 0xFF ? -1 : relationship;
            if (!r.ok() || !valid_actor_state(state, true)) return;
            states.push_back(state);
        }
        if (r.left()) return;
        for (const auto& state : states) merge_actor_state(state, g_remote, true, true);
        ++g_snapshots;
    } else if (kind == Goals && !host() && from == 0) {
        auto task = std::make_unique<GoalTask>(); task->run = id; task->sequence = r.u32();
        task->act = r.u8(); task->state = r.u8(); task->time = r.u32(); task->remaining = (int32_t)r.u32(); uint8_t count = r.u8();
        if (task->sequence <= g_received_goals || task->act >= 8 || task->state > 6 || task->time > INT32_MAX || count > kMaxGoals) return;
        for (uint8_t i = 0; i < count; ++i) {
            uint8_t complete = r.u8(); uint32_t progress = r.u32();
            if (complete > 1 || progress > 1000000) return;
            task->goals.emplace_back(complete != 0, progress);
        }
        if (!r.ok() || r.left()) return;
        g_received_goals = task->sequence;
        g_deferred_goals = std::move(task);
        flush_deferred_goals();
    } else if (kind == CaptainHit && host()) {
        uint64_t event = get_run(r); uint8_t victim = r.u8(), source_owner = r.u8();
        float damage = r.f32(); int type = (int)r.u32();
        if (!r.ok() || r.left() || !from || !event || event <= g_last_captain_hit[from] ||
            !std::isfinite(damage) || damage <= 0 || damage > 1e6f || type < 0 || type > 12 ||
            (source_owner != from && source_owner != ga::kNoPlayer) ||
            (source_owner == ga::kNoPlayer && victim != from) || !C.captain_of) return;
        void* target = C.captain_of(victim);
        void* attacker = source_owner == from ? C.captain_of(from) : nullptr;
        if (!target || (source_owner == from && !attacker)) return;
        if (attacker) {
            net::Vec3 a, b;
            if (!game::object_position(attacker, a) || !game::object_position(target, b)) return;
            float dx = a.x - b.x, dy = a.y - b.y, dz = a.z - b.z;
            if (dx * dx + dy * dy + dz * dz > 250.f * 250.f) return;
        }
        g_last_captain_hit[from] = event;
        if (type == 10) {
            if (C.captain_heal) C.captain_heal(victim, damage);
        } else if (C.captain_damage) {
            ga::Source source;
            if (attacker) { source.kind = ga::SourceKind::Captain; source.owner = from; }
            C.captain_damage(victim, source, damage);
        }
        ++g_hits;
    } else if (kind == Hit && host()) {
        uint64_t event = get_run(r); Identity actor_id{r.u32(), r.u32()}; float damage = r.f32(); int type = (int)r.u32();
        if (!r.ok() || r.left() || !event || event <= g_last_hit[from] || !std::isfinite(damage) || damage <= 0 || damage > 1e6f ||
            type < 0 || type > 12 ||
            !C.captain_of || !C.captain_of(from)) return;
        H->run_outside_frame(apply_hit, new ActorHitTask{id, event, from, actor_id, damage, type});
    }
}

bool source(void* victim, void* attacker, int type, ga::Source& out)
{
    if (!active()) return false;
    if (C.explosive_environment && C.explosive_environment()) { out = {}; return true; }
    out = {};
    uint8_t player = owner(attacker);
    if (player != ga::kNoPlayer) { out.kind = ga::SourceKind::Captain; out.owner = player; }
    else if (attacker) out.kind = hostile(attacker) ? ga::SourceKind::HostileActor : ga::SourceKind::OtherActor;
    if (victim && skg::readable(victim, 0x28)) {
        uint32_t incarnation = skg::field<uint32_t>(victim, 0x24);
        if (attacker && type != 10) g_last_source[victim] = {incarnation, out, H->time_seconds()};
        else if (!attacker && type == 7) {
            auto it = g_last_source.find(victim);
            if (it != g_last_source.end() && it->second.victim_incarnation == incarnation &&
                H->time_seconds() - it->second.time <= 30.0) out = it->second.source;
        }
    }
    return true;
}

bool damage_filter(void* victim, float& damage, int type, void* attacker)
{
    if (C.explosive_visual && C.explosive_visual()) return false;
    if(active() && C.held && C.held()) return false;
    if (active() && C.explosive_environment && C.explosive_environment()) return true;
    if (!active() || g_applying) return true;
    uint8_t victim_owner = owner(victim);
    ga::Source attribution; source(victim, attacker, type, attribution);
    uint8_t attacker_owner = attribution.kind == ga::SourceKind::Captain ? attribution.owner : ga::kNoPlayer;
    bool environment = attribution.kind == ga::SourceKind::Environment;
    if (victim_owner != ga::kNoPlayer) {
        if (host()) {
            // Captain attacks originate on the attacker's client. Environmental
            // callbacks originate on the victim's client. NPCs remain host-owned.
            if ((attacker_owner != ga::kNoPlayer && attacker_owner != 0) ||
                (environment && victim_owner != 0)) return false;
            return true;
        }
        uint8_t me = C.sess->my_id();
        if ((attacker_owner == me || (environment && victim_owner == me)) &&
            std::isfinite(damage) && damage != 0 && std::fabs(damage) <= 1e6f && type >= 0 && type <= 12) {
            net::Writer w; w.u8(CaptainHit); put_run(w, run()); put_run(w, ++g_hit_sequence);
            w.u8(victim_owner); w.u8(attacker_owner == me ? me : ga::kNoPlayer);
            w.f32(std::fabs(damage)); w.u32((uint32_t)type);
            C.sess->send_reliable_to(0, net::MSG_ADVENTURE, w);
        }
        return false;
    }
    uint32_t marker;
    if (!marker_of(victim, marker)) return true;
    if (host()) return attacker_owner == ga::kNoPlayer || attacker_owner == 0;
    if (attacker_owner == C.sess->my_id() && std::isfinite(damage) && damage > 0 && type >= 0 && type <= 12) {
        for (const auto& [id, binding] : g_bindings) if (binding.object == victim) {
            net::Writer w; w.u8(Hit); put_run(w, run()); put_run(w, ++g_hit_sequence);
            w.u32(id.marker); w.u32(id.incarnation); w.f32(damage); w.u32((uint32_t)type);
            C.sess->send_reliable_to(0, net::MSG_ADVENTURE, w);
            break;
        }
    }
    return false; // local native scenario actors remain, but only host hits change their health
}

bool update_filter(void* creature)
{
    if (!guest() || g_applying || owner(creature) != ga::kNoPlayer) return true;
    skg::ReadOnlyMemoryScope memory;
    uint32_t marker;
    if (!marker_of(creature, marker)) return true;
    // Native death cleanup still advances after a host kill; live guest actors do not run AI.
    void* combatant = game::cast(creature, kCombatant);
    return combatant && skg::field<float>(combatant, 0x38) <= 0;
}

bool is_live_mirror(void* object)
{
    skg::ReadOnlyMemoryScope memory;
    // Eligibility must not commit the native result lifecycle. Bindings carry
    // the act in which they were made.
    if (!g_ok || g_applying || !C.sess || !C.sess->connected() || C.sess->role() != net::Role::Client ||
        !C.active || !C.active() || !C.run || !g_run || C.run() != g_run ||
        game::mode_id() != 0x01654C10 || !object || !skg::readable(object, 0x28) ||
        owner(object) != ga::kNoPlayer || !game::object_alive(object) || skg::field<bool>(object, 0x20)) return false;
    auto* p = play();
    auto* native = actor_for(object);
    if (!p || !native || native->incarnation != skg::field<uint32_t>(object, 0x24)) return false;
    for (const auto& [id, binding] : g_bindings) if (binding.object == object) {
        auto state = g_remote.find(id);
        if (binding.incarnation != native->incarnation || binding.act != skg::field<int>(p, 0xB8) ||
            id.marker != native->marker || state == g_remote.end() || state->second.noun != native->noun ||
            !(state->second.flags & 1) || (state->second.flags & 2) ||
            !std::isfinite(state->second.health) || state->second.health <= 0) return false;
        void* combatant = game::cast(object, kCombatant);
        return combatant && skg::readable(combatant, 0x3C) &&
               std::isfinite(skg::field<float>(combatant, 0x38)) && skg::field<float>(combatant, 0x38) > 0;
    }
    return false;
}

bool party_relationship(void* object, int& value)
{
    if (!is_live_mirror(object) || !game::cast(object, kCreature)) return false;
    for (const auto& [id, binding] : g_bindings) if (binding.object == object) {
        auto state = g_remote.find(id);
        if (state == g_remote.end() || state->second.party_relationship < 0 || state->second.party_relationship > 7) return false;
        value = state->second.party_relationship;
        return true;
    }
    return false;
}

void begin_native_attempt(void* p)
{
    // Initialize is a verified lifecycle boundary even when the native play
    // allocation is reused for another co-op attempt, retry or solo adventure.
    g_native_result = {p, C.run ? C.run() : 0, false, 0};
}

void apply_result(bool success)
{
    uint8_t* p = play();
    if (game::mode_id() != 0x01654C10 || !p || !o_State || g_native_result.play != p ||
        !g_native_result.run || g_native_result.applied) return;
    bool previous = g_applying;
    g_applying = true; native_state(p, success ? 5 : 6); g_applying = previous;
}

static void reset_replication()
{
    g_run = 0; g_sequence = g_goal_sequence = g_received_goals = 0;
    g_deferred_goals.reset();
    ++g_goal_generation; g_goal_work_run = 0; g_goal_apply_queued = g_goal_apply_active = false;
    g_native.clear(); g_remote.clear(); g_bindings.clear(); g_health_baselines.clear(); g_last_hit.clear(); g_last_captain_hit.clear();
    g_last_interaction.clear(); g_dropped.clear(); g_social_events.clear(); g_social_completions.clear(); g_interaction_sequence = 0;
    g_opened_gates.clear(); g_retired_items.clear(); g_talk_cleanup=g_talk_presenting=g_talk_committing=false;
    g_last_source.clear();
    g_markers.clear(); g_roster_sequence = 0; g_roster_parts.clear(); g_roster_received.clear(); g_roster_live.clear();
    g_static_host.clear(); g_static_sent.clear(); g_static_cursor.clear(); g_static_act = -1;
    for (auto& pending : g_static_pending) pending.clear();
    g_replication_stats = {};
    g_attachments.clear(); g_attachment_sent.clear(); g_attachment_revision=0; g_attachment_act=-1;
    g_attached_spatials.clear(); g_flying.clear(); g_item_throws=g_item_catches=g_item_gives=0;
    g_attachment_assemblies={}; g_attachment_pending={}; g_attachment_received={};
    std::fill(std::begin(g_last_throw),std::end(g_last_throw),0);
    g_spawn_attempt.clear(); g_world_error.clear();
    g_goal_hud_dirty = false; g_goal_hud_refreshes = 0;
    adventure_world_vehicles::reset();
    g_last_send = g_last_goals = g_last_item_apply = 0; g_hit_sequence = 0;
}

static void retire_identity(Identity id)
{
    uint64_t current = run();
    if (!current) return;
    // A routed pickup/explosion event can precede every world packet and tick.
    // Initialize before recording its tombstone, just as on_message does for
    // baselines, so the first world tick cannot erase an accepted retirement.
    if (g_run != current) { reset_replication(); g_run = current; }
    g_retired_items.insert(id); g_remote.erase(id); g_bindings.erase(id);
}

void reset()
{
    reset_replication();
    g_native_result = {};
}

void peer_changed(uint8_t player)
{
    g_static_sent.erase(player);
    g_static_cursor.erase(player);
    g_attachment_sent.erase(player);
}

void tick(double now)
{
    if (!active()) { if (g_run) reset_replication(); return; }
    observe_native_attempt(run());
    if (run() != g_run) { reset_replication(); g_run = run(); }
    if (!play()) return;
    flush_deferred_goals();
    if (host()) {
        enumerate();
        if (now - g_last_send >= 0.1) { g_last_send = now; send_actors(); send_attachments(skg::field<int>(play(),0xB8)); }
        if (now - g_last_goals >= 0.5) { g_last_goals = now; send_goals(); send_roster(); }
        if(!g_flying.empty() && !g_catch_queued) {
            g_catch_queued=true;
            H->run_outside_frame([](void* arg) {
                std::unique_ptr<uint64_t> run_id(static_cast<uint64_t*>(arg)); g_catch_queued=false;
                if(!active() || !host() || *run_id!=run()) return;
                enumerate(); refresh_attachment_spatials(); catch_items();
            },new uint64_t(run()));
        }
    } else if (!g_mirror_queued) {
        g_mirror_queued = true;
        auto* task = new std::pair<uint64_t, double>{g_run, now};
        H->run_outside_frame([](void* argument) {
            std::unique_ptr<std::pair<uint64_t, double>> task((std::pair<uint64_t, double>*)argument);
            g_mirror_queued = false;
            if (!guest() || task->first != run()) return;
            enumerate(); mirror(task->second);
            auto* p=play(); if(!p) return;
            if(task->second-g_last_item_apply>=0.1) {
                g_last_item_apply=task->second;
                apply_attachments(skg::field<int>(p,0xB8));
                bool carrying=false;
                each_slot(C.captain_of(C.sess->my_id()),[&](uint8_t,void*) { carrying=true; });
                if(carrying && !(C.held && C.held())) for(const auto& actor:g_native)
                    if(game::cast(actor.object,kCreature)) adventure_world_actions::poll_receive(actor.object);
            }
        }, task);
    }
}

bool init(const SkHost* h, SkMod* m, Ctx ctx)
{
    H = h; M = m; C = std::move(ctx);
    a_Mode = H->sym("App::cScenarioMode::ptr");
    a_SetAct = H->sym("Simulator::cScenarioPlayMode::SetCurrentAct");
    a_GetMarker = H->sym("Simulator::cScenarioData::GetMarkerForObject");
    a_Hostile = H->sym("SporeCoop::Adventure::IsHostileToCreature");
    a_Befriend = H->sym("SporeCoop::Adventure::OnBefriend");
    a_Ally = H->sym("SporeCoop::Adventure::OnAlly");
    a_Relationships = H->sym("SporeCoop::Adventure::SpeciesRelationshipsGet");
    a_SocialEvent = H->sym("SporeCoop::Adventure::SpeciesSocialEvent");
    a_SocialMode = H->sym("SporeCoop::Adventure::CreatureSocialMode");
    a_AnimalDamage = H->sym("Simulator::cCreatureAnimal::TakeDamage");
    a_GoalHUD = H->sym("SporeCoop::Adventure::RefreshGoalHUD");
    uintptr_t check = H->sym("Simulator::cScenarioPlayMode::CheckGoalProgress");
    uintptr_t complete = H->sym("Simulator::cScenarioPlayMode::CompleteAct");
    uintptr_t goals = H->sym("Simulator::cScenarioPlayMode::UpdateGoals");
    uintptr_t state = H->sym("Simulator::cScenarioPlayMode::SetState");
    uintptr_t pick = H->sym("SporeCoop::Adventure::PickUpItem");
    uintptr_t picked = H->sym("SporeCoop::Adventure::OnPickUp");
    uintptr_t dropped = H->sym("SporeCoop::Adventure::OnDrop");
    uintptr_t count = H->sym("SporeCoop::Adventure::CountGoal");
    uintptr_t avatar = H->sym("SporeCoop::Adventure::GetNativeAvatar");
    uintptr_t generic = H->sym("Simulator::cCombatant::TakeDamage");
    uintptr_t talk=H->sym("SporeCoop::Adventure::TalkBegin"), talk_end_address=H->sym("SporeCoop::Adventure::TalkFinish");
    uintptr_t gate=H->sym("SporeCoop::Adventure::ActivateGate");
    a_GateKey=H->sym("SporeCoop::Adventure::GateKey");
    a_OpenGate=H->sym("SporeCoop::Adventure::OpenGate");
    a_Physics=H->sym("Simulator::cPhysicsWorld::Get");
    a_Impulse=H->sym("Simulator::cPhysicsWorld::ApplyImpulse");
    a_ThrowGrace=H->sym("SporeCoop::Adventure::ThrowPairGrace");
    o_Drop = (Drop_t)H->sym("SporeCoop::Adventure::DropItem");
    if (std::string(H->exe_sha256()) != "dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37" ||
        !a_Mode || !a_SetAct || !a_GetMarker || !a_Hostile || !a_Befriend || !a_Ally || !a_Relationships || !a_SocialEvent || !a_SocialMode || !a_AnimalDamage || !a_GoalHUD ||
        !check || !complete || !goals || !state || !pick || !picked || !dropped || !count || !avatar || !generic || !o_Drop || !C.sess || !C.captain_can_act ||
        !talk || !talk_end_address || !gate || !a_GateKey || !a_OpenGate || !a_Physics || !a_Impulse || !a_ThrowGrace || !adventure_world_lifecycle::init(H, M)) return false;
    g_ok = H->hook_create(M, (void*)check, (void*)check_goals, (void**)&o_Check) == 0 &&
           H->hook_create(M, (void*)complete, (void*)complete_act, (void**)&o_Complete) == 0 &&
           H->hook_create(M, (void*)goals, (void*)update_goals, (void**)&o_Goals) == 0 &&
           H->hook_create(M, (void*)state, (void*)set_state, (void**)&o_State) == 0 &&
           H->hook_create(M, (void*)pick, (void*)pick_item, (void**)&o_Pick) == 0 &&
           H->hook_create(M, (void*)picked, (void*)picked_item, (void**)&o_Picked) == 0 &&
           H->hook_create(M, (void*)dropped, (void*)dropped_item, (void**)&o_Dropped) == 0 &&
           H->hook_create(M, (void*)count, (void*)count_goal, (void**)&o_Count) == 0 &&
           H->hook_create(M, (void*)a_Befriend, (void*)befriended, (void**)&o_Befriend) == 0 &&
           H->hook_create(M, (void*)a_Ally, (void*)allied, (void**)&o_Ally) == 0 &&
           H->hook_create(M, (void*)avatar, (void*)native_avatar, (void**)&o_Avatar) == 0 &&
           H->hook_create(M, (void*)generic, (void*)generic_damage, (void**)&o_GenericDamage) == 0 &&
           H->hook_create(M,(void*)talk,(void*)talk_start,(void**)&o_TalkStart)==0 &&
           H->hook_create(M,(void*)talk_end_address,(void*)talk_end,(void**)&o_TalkEnd)==0 &&
           H->hook_create(M,(void*)gate,(void*)activate_gate,(void**)&o_Gate)==0;
    if (g_ok) {
        adventure_world_actions::Ctx actions;
        actions.active = [] { return active() && game::mode_id() == 0x01654C10; };
        actions.host = host;
        actions.local_captain = [](void* object) { return C.sess && owner(object) == C.sess->my_id(); };
        actions.request_give = request_container_give;
        adventure_world_vehicles::Ctx vehicles;
        vehicles.active = actions.active; vehicles.host = host; vehicles.run = run;
        vehicles.paused=C.held;
        vehicles.is_mirrored = [](void* object) {
            if (update_filter(object)) return false;
            for (const auto& [id, binding] : g_bindings) if (binding.object == object) return true;
            return false;
        };
        g_ok = adventure_world_actions::init(H, M, std::move(actions)) &&
               adventure_world_vehicles::init(H, M, std::move(vehicles));
    }
    H->command_add(M, "coop.adventure.world", "Adventure native diagnostics and isolated-profile test actions",
                   [](void*, int argc, const char* const* argv, SkOut* out) { return debug(argc - 1, argv + 1, out); },
                   nullptr, SK_CMD_OUTSIDE_FRAME);
    return g_ok;
}

bool available() { return g_ok; }
adventure_pickups::Ctx pickups_context()
{
    adventure_pickups::Ctx context;
    context.sess=C.sess; context.run=run;
    context.act=[] { auto* p=play(); return p ? skg::field<int>(p,0xB8) : -1; };
    context.active=active; context.held=C.held; context.simulation_time=C.simulation_time;
    context.captain_of=C.captain_of; context.captain_can_act=C.captain_can_act;
    context.identity=[](void* object,adventure_pickups::Identity& output) {
        enumerate(); Identity id; if(!identity(object,id)) return false;
        output={id.marker,id.incarnation}; return true;
    };
    context.object_for=[](adventure_pickups::Identity id) { enumerate(); return object_for({id.marker,id.incarnation}); };
    context.retire=[](adventure_pickups::Identity id) {
        retire_identity({id.marker,id.incarnation});
    };
    context.captain_heal=C.captain_heal; context.teleport=C.teleport;
    context.failed=C.interaction_failed;
    return context;
}
adventure_explosives::Ctx explosives_context()
{
    adventure_explosives::Ctx context;
    context.sess=C.sess; context.run=run;
    context.act=[] { auto* p=play(); return uint8_t(p ? std::clamp(skg::field<int>(p,0xB8),0,7) : 0); };
    context.active=active; context.held=C.held;
    context.identify=[](void* object,adventure_explosives::Identity& output) {
        enumerate(); Identity id; if(!identity(object,id)) return false;
        output={id.marker,id.incarnation}; return true;
    };
    context.object=[](adventure_explosives::Identity id) { enumerate(); return object_for({id.marker,id.incarnation}); };
    context.powerup=powerup_for;
    context.combatant=[](void* object) { return game::cast(object,kCombatant); };
    context.from_spatial=object_from_spatial; context.captain=C.captain_of;
    context.owner=owner; context.can_act=C.captain_can_act; context.nearby=nearby;
    context.bounds=[](void* object,net::Vec3& minimum,net::Vec3& maximum) {
        void* spatial=game::cast(object,kSpatial);
        if(!spatial) return false;
        struct Box { net::Vec3 minimum,maximum; } box;
        skg::vcall<Box*>(spatial,0x6C/4,&box);
        if(!finite(box.minimum) || !finite(box.maximum)) return false;
        minimum=box.minimum; maximum=box.maximum; return true;
    };
    context.failed=C.interaction_failed;
    context.owns_throw=[](uint8_t player,void* item) {
        if(!C.captain_of) return false;
        void* spatial=game::cast(item,kSpatial);
        if(spatial && item_slot(C.captain_of(player),spatial,false)>=0) return true;
        Identity id; if(!identity(item,id)) return false;
        auto it=g_dropped.find(id);
        return it!=g_dropped.end() && it->second.first==player && H->time_seconds()-it->second.second<=2.0;
    };
    context.retire=[](adventure_explosives::Identity id) {
        retire_identity({id.marker,id.incarnation});
    };
    context.ordinary_throw=ordinary_throw;
    context.explosive_thrown=[](adventure_explosives::Identity id,uint8_t player) {
        void* item=object_for({id.marker,id.incarnation}),*captain=C.captain_of(player);
        void* spatial=game::cast(item,kSpatial),*physics=skg::cdecl_call<void*>(a_Physics);
        if(item && captain && spatial && physics) {
            adventure_world_actions::throw_source(captain,item);
            skg::thiscall<void>(a_ThrowGrace,physics,game::cast(captain,kSpatial),spatial,0.5f);
            void* powerup=powerup_for(item);
            if(powerup && skg::field<int>(powerup,4)==10) record_flight({id.marker,id.incarnation},player);
        }
    };
    return context;
}
std::string status(bool verbose)
{
    char b[360];
    snprintf(b, sizeof b, "adventure world: available=%d run=%llu actors=%u mirrored=%u snapshots=%u hits=%u goals=%u hostility=%s remote=%u placements=%u roster=%u\n",
             g_ok ? 1 : 0, (unsigned long long)g_run, (unsigned)g_native.size(), (unsigned)g_bindings.size(),
             g_snapshots, g_hits, g_goal_updates, C.hostile_to_party || a_Hostile ? "native" : "unavailable",
             (unsigned)g_remote.size(), (unsigned)g_markers.size(), (unsigned)g_roster_live.size());
    std::string summary = b;
    summary += "static replication: static=" + std::to_string(g_replication_stats.static_actors) +
        " dynamic=" + std::to_string(g_replication_stats.dynamic_actors) +
        " static_bytes=" + std::to_string(g_replication_stats.static_bytes) +
        " dynamic_bytes=" + std::to_string(g_replication_stats.dynamic_bytes) +
        " baselines=" + std::to_string(g_replication_stats.baselines) +
        " state_updates=" + std::to_string(g_replication_stats.state_updates) +
        " promoted=" + std::to_string(g_replication_stats.promotions) +
        " deferred=" + std::to_string(g_replication_stats.deferred) + "\n";
    summary += "native goal HUD: refreshes=" + std::to_string(g_goal_hud_refreshes) +
               " pending=" + std::to_string(g_goal_hud_dirty ? 1 : 0) + "\n";
    summary += "native result: run=" + std::to_string(g_native_result.run) +
               " committed=" + std::to_string(g_native_result.committed ? 1 : 0) +
               " applied=" + std::to_string(g_native_result.applied) + "\n";
    summary += "items: attachments="+std::to_string(g_attachments.size())+" revision="+std::to_string(g_attachment_revision)+
        " throws="+std::to_string(g_item_throws)+" catches="+std::to_string(g_item_catches)+
        " gives="+std::to_string(g_item_gives)+" flying="+std::to_string(g_flying.size())+"\n";
    auto vehicle = adventure_world_vehicles::counters();
    summary += "vehicle mirrors: trees=" + std::to_string(vehicle.trees) + " weapons=" + std::to_string(vehicle.weapons) +
               " timers=" + std::to_string(vehicle.timers) + " actors=" + std::to_string(vehicle.actors) + "\n";
    if (!g_world_error.empty()) summary += "native lifecycle: " + g_world_error + "\n";
    if (!verbose) return summary;
    std::ostringstream out; out << summary;
    auto* p = game::mode_id() == 0x01654C10 ? play() : nullptr;
    uint8_t* goals; size_t count;
    if (!p || !goals_vector(p, goals, count)) { out << "native scenario play mode unavailable\n"; return out.str(); }
    out << "act=" << skg::field<int>(p, 0xB8) << " state=" << skg::field<int>(p, 0x90)
        << " time_ms=" << skg::field<int>(p, 0xC0) << " remaining_ms=" << skg::field<int>(p, 0xBC) << "\n";
    for (size_t i = 0; i < count; ++i) {
        auto* goal = goals + i * 0x1AC;
        out << "goal=" << i << " type=" << skg::field<int>(goal, 0x24) << " class=" << skg::field<int>(goal, 0x28)
            << " class2=" << skg::field<int>(goal, 0x2C) << " count=" << skg::field<int>(goal, 4)
            << " required=" << skg::field<int>(goal, 0x30) << " complete=" << (goal[0] ? 1 : 0) << "\n";
    }
    enumerate();
    for(const auto& relation:g_attachments)
        out << "attachment item=" << relation.item.marker << ":" << relation.item.incarnation
            << " holder=" << relation.holder.marker << ":" << relation.holder.incarnation
            << " player=" << unsigned(relation.player) << " slot=" << unsigned(relation.slot) << "\n";
    for (const auto& actor : g_native) {
        const auto& pos = skg::field<net::Vec3>(actor.spatial, 4);
        out << "marker=" << actor.marker << " incarnation=" << actor.incarnation << " noun=" << actor.noun
            << " class=" << skg::field<int>(actor.object, 0x1C) << " pos=" << pos.x << "," << pos.y << "," << pos.z;
        out << " static_candidate=" << (actor.static_candidate ? 1 : 0);
        if(host()) {
            auto tracked=g_static_host.find({actor.marker,actor.incarnation});
            out << " replication=" << (tracked!=g_static_host.end() && !tracked->second.promoted ? "static" : "dynamic");
        }
        if (actor.combatant) out << " hp=" << skg::field<float>(actor.combatant, 0x38) << " max_hp=" << max_health(actor.combatant)
                                << " tuning=" << skg::field<float>(actor.combatant, 0x2C) << " hostile=" << (hostile(actor.object) ? 1 : 0);
        if (host()) {
            auto baseline = g_health_baselines.find({actor.marker, actor.incarnation});
            if (baseline != g_health_baselines.end()) out << " baseline=" << baseline->second.original;
        }
        out << "\n";
    }
    return out.str();
}

static bool item_fixture_profile(const wchar_t* command)
{
    if(!command) return false;
    const wchar_t* profile=std::wcsstr(command,L"-sporekit-profile:");
    if(!profile) return false;
    profile+=18;
    if(std::wcsncmp(profile,L"winlab_",7)) return false;
    size_t length=0;
    for(;profile[length] && profile[length]!=L' ' && profile[length]!=L'"';++length)
        if(!((profile[length]>=L'a' && profile[length]<=L'z') ||
             (profile[length]>=L'0' && profile[length]<=L'9') || profile[length]==L'_')) return false;
    return length>7;
}

int debug(int argc, const char* const* argv, SkOut* output)
{
    auto say = [&](const std::string& text) { H->out_printf(output, "%s", text.c_str()); };
    if (!argc || std::string(argv[0]) == "status") { say(status(true)); return 0; }
    if (!active() || !play()) { say("No committed adventure is running.\n"); return 1; }
    std::string action = argv[0];
    if((action=="give" || action=="throw" || action=="detachtool") && !item_fixture_profile(GetCommandLineW())) {
        say("Native item fixture commands require an explicit isolated winlab_ profile.\n"); return 1;
    }
    enumerate();
    auto number = [&](int n) { return n < argc ? std::strtoul(argv[n], nullptr, 0) : 0ul; };
    if(action=="talk" && argc==4 && C.captain_of) {
        uint8_t* goals; size_t count; unsigned index=number(1),marker=number(2),player=number(3);
        auto address=H->sym("SporeCoop::Adventure::TalkBegin");
        auto actor=std::find_if(g_native.begin(),g_native.end(),[&](const auto& a) { return a.marker==marker; });
        void* captain=player<net::kMaxPlayers ? C.captain_of(uint8_t(player)) : nullptr;
        if(!address || !captain || actor==g_native.end() || !goals_vector(play(),goals,count) || index>=count ||
            skg::field<int>(goals+index*0x1AC,0x24)!=2) {
            say("A native TalkTo goal, target and captain are required.\n"); return 1;
        }
        AvatarScope avatar(captain);
        skg::thiscall<void>(address,play(),goals+index*0x1AC,actor->object);
        say("Native conversation requested.\n"); return 0;
    }
    if(action=="fuse" && argc==2) {
        auto address=H->sym("SporeCoop::Adventure::StartBombFuse");
        auto actor=std::find_if(g_native.begin(),g_native.end(),[&](const auto& a) { return a.marker==number(1); });
        void* powerup=actor!=g_native.end() ? powerup_for(actor->object) : nullptr;
        if(!address || !powerup || skg::field<int>(powerup,4)!=10) {
            say("A native timed bomb is required.\n"); return 1;
        }
        skg::cdecl_call<void>(address,powerup);
        say("Native bomb fuse requested.\n"); return 0;
    }
    if(action=="abilityhit" && argc==4 && C.captain_of && C.captain_can_act) {
        auto address=H->sym("SporeCoop::Adventure::ApplyCreatureAbility");
        unsigned victim=number(1),attacker=number(2),index=number(3);
        if(!address || victim>=net::kMaxPlayers || attacker>=net::kMaxPlayers || index>=64 ||
            !C.captain_can_act(uint8_t(attacker)) || (C.held && C.held())) {
            say("A verified ability and two participating captains are required.\n"); return 1;
        }
        void* source=C.captain_of(uint8_t(attacker));
        void* target=C.captain_of(uint8_t(victim));
        void* combatant=game::cast(target,kCombatant); net::Vec3 position;
        if(!source || !combatant || !game::object_position(target,position)) {
            say("The native captain combatants are unavailable.\n"); return 1;
        }
        skg::thiscall<bool>(address,source,combatant,int(index),&position);
        say("Native captain ability damage requested.\n"); return 0;
    }
    if (action == "relations" && argc <= 2 && a_Hostile && C.captain_of) {
        std::ostringstream out; size_t shown = 0;
        for (const auto& actor : g_native) {
            if ((argc > 1 && actor.marker != static_cast<uint32_t>(number(1))) || shown >= 32) continue;
            void* creature = game::cast(actor.object, kCreature);
            if (!creature) continue;
            ++shown;
            out << "marker=" << actor.marker << " incarnation=" << actor.incarnation
                << " class=" << skg::field<int>(actor.object, 0x1C) << " bound=" << (is_live_mirror(actor.object) ? 1 : 0);
            for (const auto& player : C.sess->players()) {
                void* captain = game::cast(C.captain_of(player.id), kCreature);
                if (captain) out << " hostile_to_" << static_cast<unsigned>(player.id) << "="
                    << (skg::thiscall<bool>(a_Hostile, creature, captain) ? 1 : 0);
            }
            out << "\n";
        }
        out << "relations_rows=" << shown << "\n";
        say(out.str()); return 0;
    }
    if (action == "finish" && argc == 2 && host()) {
        std::string result = argv[1];
        if (result != "success" && result != "fail") { say("finish success|fail\n"); return 1; }
        set_state(play(), result == "success" ? 5 : 6);
        say("Controlled native adventure result requested.\n"); return 0;
    }
    if (action == "actors" || action == "actorsat" || action == "pending") {
        uint32_t first = (uint32_t)number(1);
        size_t limit = argc > 2 ? std::clamp<size_t>(number(2), 1, 48) : 32, shown = 0;
        std::ostringstream out;
        if (action == "pending") {
            for (const auto& [id, state] : g_remote) if (id.marker >= first && !g_bindings.count(id) && shown++ < limit)
                out << "marker=" << id.marker << " incarnation=" << id.incarnation << " noun=" << state.noun
                    << " placement=" << (g_markers.count(id.marker) ? 1 : 0) << " sequence=" << state.sequence << "\n";
        } else {
            for (const auto& actor : g_native) if ((action=="actorsat" ? actor.marker==first : actor.marker>=first) && shown++ < limit) {
                out << "marker=" << actor.marker << " incarnation=" << actor.incarnation << " noun=" << actor.noun
                    << " object=" << actor.object << " class=" << skg::field<int>(actor.object, 0x1C);
                void* powerup=powerup_for(actor.object);
                if(powerup && skg::readable(powerup,0x3C)) out << " powerup=" << skg::field<int>(powerup,4)
                    << " key_category=" << (skg::field<void*>(powerup,0x18) ? skg::cdecl_call<uint32_t>(a_GateKey,powerup) : 0);
                for (const auto& [id, binding] : g_bindings) if (binding.object == actor.object)
                    out << " host_incarnation=" << id.incarnation;
                out << "\n";
            }
        }
        say(out.str()); return 0;
    }
    auto marker_actor = [&](uint32_t marker) -> NativeActor* {
        NativeActor* spawner=nullptr;
        for(auto& actor:g_native) if(actor.marker==marker) {
            if(actor.powerup_kind!=9) return &actor;
            spawner=&actor;
        }
        return spawner;
    };
    if(action=="detachtool" && argc==3 && !host()) {
        auto* actor=marker_actor(uint32_t(number(1))); size_t index=number(2); void* spatial=nullptr;
        if(actor && game::cast(actor->object,kCreature))
            each_slot(actor->object,[&](uint8_t slot,void* value) {if(slot==index) spatial=value;});
        if(!actor || !spatial || index>=128) { say("A guest NPC held-tool slot is required.\n"); return 1; }
        g_applying=true; bool released=o_Drop(actor->object,spatial,0,false); g_applying=false;
        say(released?"Guest NPC tool released for reconstruction probe.\n":"Native NPC tool release failed.\n");
        return released?0:1;
    }
    if(action=="nudge" && argc==5) {
        auto* actor=marker_actor(uint32_t(number(1)));
        net::Vec3 delta{std::strtof(argv[2],nullptr),std::strtof(argv[3],nullptr),std::strtof(argv[4],nullptr)};
        if(!actor || !actor->static_candidate || !finite(delta) ||
            delta.x*delta.x+delta.y*delta.y+delta.z*delta.z>100.f*100.f) {
            say("A verified static candidate and finite offset within 100 metres are required.\n"); return 1;
        }
        auto pos=skg::field<net::Vec3>(actor->spatial,4);
        pos.x+=delta.x;pos.y+=delta.y;pos.z+=delta.z;
        // Use the same verified spatial setter as native mirror correction.
        skg::vcall<void>(actor->spatial,0x38/4,&pos);
        say("Native static actor moved.\n"); return 0;
    }
    if(action=="powerup" && argc==3 && C.captain_of) {
        auto* actor=marker_actor(uint32_t(number(1)));
        uint8_t player=uint8_t(number(2));
        void* captain=player<net::kMaxPlayers ? C.captain_of(player) : nullptr;
        void* powerup=actor ? powerup_for(actor->object) : nullptr;
        const char* symbol=nullptr;
        if(powerup) switch(skg::field<int>(powerup,4)) {
            case 1: symbol="SporeCoop::Adventure::ActivateMedKit"; break;
            case 3: symbol="SporeCoop::Adventure::ActivateSpeedBoost"; break;
            case 4: symbol="SporeCoop::Adventure::ActivateDamageBoost"; break;
            case 5: symbol="SporeCoop::Adventure::ActivateArmorBoost"; break;
            case 6: symbol="SporeCoop::Adventure::ActivateMine"; break;
            case 8: symbol="SporeCoop::Adventure::ActivateTeleporter"; break;
            case 13: symbol="SporeCoop::Adventure::ActivateGrenade"; break;
        }
        auto address=symbol ? H->sym(symbol) : 0;
        if(!captain || !powerup || !address) { say("A supported native powerup and captain are required.\n"); return 1; }
        void* recipient=skg::field<int>(powerup,4)==6 ? game::cast(captain,kCombatant) : captain;
        if(!recipient) { say("The native recipient is unavailable.\n"); return 1; }
        skg::cdecl_call<void>(address,powerup,recipient);
        say("Native powerup activation requested.\n"); return 0;
    }
    if(action=="throw" && argc>=3 && argc<=6 && C.captain_of) {
        auto* actor=marker_actor(uint32_t(number(1)));
        uint8_t player=uint8_t(number(2));
        void* captain=player<net::kMaxPlayers ? C.captain_of(player) : nullptr;
        void* powerup=actor ? powerup_for(actor->object) : nullptr;
        void* spatial=actor ? actor->spatial : nullptr;
        auto impulse=H->sym("Simulator::cPhysicsWorld::ApplyImpulse");
        auto physics_get=H->sym("Simulator::cPhysicsWorld::Get");
        net::Vec3 velocity{0,8,2};
        if(argc>=4) velocity.x=std::strtof(argv[3],nullptr);
        if(argc>=5) velocity.y=std::strtof(argv[4],nullptr);
        if(argc>=6) velocity.z=std::strtof(argv[5],nullptr);
        if(!captain || !actor || !finite(velocity) || !impulse || !physics_get ||
            !C.captain_can_act(player) || (C.held && C.held()) || item_slot(captain,spatial,false)<0) {
            say("A held native item and active captain are required.\n"); return 1;
        }
        if(powerup && skg::field<int>(powerup,4)==13) {
            auto arm=H->sym("SporeCoop::Adventure::ArmGrenade");
            if(!arm) { say("Native grenade arming is unavailable.\n"); return 1; }
            skg::cdecl_call<void>(arm,powerup,captain);
        }
        if(!o_Drop(captain,spatial,0,false)) { say("The native throw could not drop its item.\n"); return 1; }
        void* physics=skg::cdecl_call<void*>(physics_get);
        if(!physics) { say("The native physics world is unavailable.\n"); return 1; }
        skg::thiscall<void>(impulse,physics,spatial,&velocity,true);
        skg::thiscall<void>(a_ThrowGrace,physics,game::cast(captain,kSpatial),spatial,0.5f);
        say("Native item throw requested.\n"); return 0;
    }
    if(action=="give" && argc>=3 && argc<=4) {
        auto* item=marker_actor(uint32_t(number(1))); auto* recipient=marker_actor(uint32_t(number(2)));
        uint8_t player=argc==4 ? uint8_t(number(3)) : C.sess->my_id();
        void* captain=player<net::kMaxPlayers ? C.captain_of(player) : nullptr;
        bool queued=item && recipient && captain && request_container_give(captain,item->object,recipient->object);
        say(queued?"Native item Give requested.\n":"A local captain, held item and recipient are required.\n");
        return queued?0:1;
    }
    if(action=="forgetgate" && argc==2 && !host()) {
        auto* actor=marker_actor(uint32_t(number(1)));
        void* powerup=actor ? powerup_for(actor->object) : nullptr;
        if(!actor || !powerup || skg::field<int>(powerup,4)!=14) { say("An open guest gate is required.\n"); return 1; }
        void* object=actor->object; void* sibling=nullptr;
        for(const auto& candidate:g_native) if(candidate.marker==actor->marker && candidate.object!=object) { sibling=candidate.object; break; }
        auto* marker=skg::field<uint8_t*>(object,0x18); std::string error;
        if(!adventure_world_lifecycle::remove(scenario_data(),marker,object,error,sibling)) { say(error+"\n"); return 1; }
        for(auto it=g_bindings.begin();it!=g_bindings.end();) {
            if(it->second.object==object) it=g_bindings.erase(it); else ++it;
        }
        say("Guest open gate removed for snapshot reconstruction fixture.\n"); return 0;
    }
    if (action == "nearplayer" && argc >= 2 && C.captain_of) {
        unsigned long target_id = number(1), moving_id = argc > 2 ? number(2) : C.sess->my_id();
        if (target_id >= ga::kNoPlayer || moving_id >= ga::kNoPlayer) { say("Invalid party player.\n"); return 1; }
        void* target = C.captain_of(static_cast<uint8_t>(target_id));
        void* captain = C.captain_of(static_cast<uint8_t>(moving_id));
        net::Vec3 position;
        if (!target || !captain || !game::object_position(target, position) || !finite(position)) {
            say("Party captain position is unavailable.\n"); return 1;
        }
        position.x += 1.f;
        game::teleport(captain, position, game::orientation(captain));
        say("Captain moved beside party player.\n"); return 0;
    }
    if ((action == "hit" || action == "actorhit") && argc >= 4) {
        void* victim = action == "hit" ? (C.captain_of ? C.captain_of((uint8_t)number(1)) : nullptr) : nullptr;
        NativeActor* actor = action == "actorhit" ? marker_actor((uint32_t)number(1)) : nullptr;
        if (actor) victim = actor->object;
        void* attacker = std::string(argv[2]) == "env" ? nullptr : (C.captain_of ? C.captain_of((uint8_t)number(2)) : nullptr);
        float amount = std::strtof(argv[3], nullptr); int type = argc > 4 ? (int)number(4) : 2;
        void* target = game::cast(victim, kCombatant);
        if (!target || !std::isfinite(amount) || amount <= 0 || amount > 1e6f || type < 0 || type > 12 ||
            (!attacker && std::string(argv[2]) != "env")) { say("Invalid native hit.\n"); return 1; }
        net::Vec3 direction{};
        skg::vcall<int>(target, 0x18 / 4, amount, attacker ? skg::field<uint32_t>(attacker, 0x30) : UINT32_MAX,
                        type, &direction, game::cast(attacker, kCombatant));
        say("Native hit dispatched.\n"); return 0;
    }
    if ((action == "pickup" || action == "drop" || action == "near") && argc >= 2) {
        auto* actor = marker_actor((uint32_t)number(1));
        uint8_t player = argc > 2 ? (uint8_t)number(2) : C.sess->my_id();
        void* captain = C.captain_of ? C.captain_of(player) : nullptr;
        if (!actor || !captain) { say("Native actor or captain unavailable.\n"); return 1; }
        if (action == "near") {
            float offsets[2]{1.f, 0.f};
            for (int i = 0; i < 2 && i + 3 < argc; ++i) {
                char* end = nullptr;
                offsets[i] = std::strtof(argv[i + 3], &end);
                if (!end || end == argv[i + 3] || *end || !std::isfinite(offsets[i])) {
                    say("Invalid near offset.\n"); return 1;
                }
            }
            if (offsets[0] * offsets[0] + offsets[1] * offsets[1] > 25.f * 25.f) {
                say("Near offset exceeds 25 metres.\n"); return 1;
            }
            net::Vec3 pos = skg::field<net::Vec3>(actor->spatial, 4);
            pos.x += offsets[0]; pos.y += offsets[1];
            game::teleport(captain, pos, game::orientation(captain));
            say("Captain moved beside native actor.\n"); return 0;
        }
        if (action == "drop") {
            bool ok = o_Drop(captain, actor->spatial, 0, false);
            say(ok ? "Native item dropped.\n" : "Captain is not holding that item.\n"); return ok ? 0 : 1;
        }
        int slot = item_slot(captain, nullptr, true);
        bool ok = slot >= 0 && pick_item(captain, actor->spatial, slot, true);
        say(ok ? "Native pickup dispatched.\n" : "No native pickup slot available.\n"); return ok ? 0 : 1;
    }
    if (action == "social" && argc >= 3) {
        uint8_t* goals; size_t count; size_t index = number(1);
        auto* actor = marker_actor((uint32_t)number(2));
        if (!actor || !goals_vector(play(), goals, count) || index >= count) { say("Native goal or target unavailable.\n"); return 1; }
        auto* goal = goals + index * 0x1AC;
        int type = skg::field<int>(goal, 0x24);
        if (type == 4) skg::thiscall<void>(a_Befriend, play(), actor->object);
        else if (type == 5) {
            std::string error;
            if (!adventure_world_actions::recruit(actor->object, error)) { say(error + "\n"); return 1; }
        }
        else if (type == 2) count_goal(play(), goal, actor->spatial, nullptr);
        else { say("Goal is not TalkTo, Befriend or AllyWith.\n"); return 1; }
        say("Native social objective event dispatched.\n"); return 0;
    }
    if (action == "act" && argc >= 2 && host() && number(1) < 8) {
        skg::thiscall<void>(a_SetAct, play(), (int)number(1), false);
        say("Native act changed.\n"); return 0;
    }
    say("status | finish success|fail (host) | hit PLAYER ATTACKER|env AMOUNT [TYPE] | actorhit MARKER ATTACKER AMOUNT [TYPE] | nearplayer TARGET [PLAYER] | near MARKER [PLAYER] [X_OFFSET [Y_OFFSET]] | pickup|drop MARKER [PLAYER] | social GOAL MARKER | act INDEX\n");
    return 1;
}

} // namespace adventure_world
