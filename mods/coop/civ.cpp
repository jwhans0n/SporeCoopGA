#include "civ.h"

#include <algorithm>
#include <cmath>
#include <cstdio>
#include <functional>
#include <unordered_map>
#include <unordered_set>

#include "game.h"
#include "shared.h"
#include "modes.h"
#include "net/interp.h"
#include "sporekit_game.hpp"

using skg::thiscall;
using skg::vcall;

namespace civ {

static const SkHost* H;
static SkMod* M;
static Ctx C;
static bool g_ok;

// ---- layout (docs/re/civ.md) ----
constexpr uintptr_t kVehicleVt = 0x014756D8;
constexpr uintptr_t kVSpatial = 0x34, kVPos = 0x38, kVRot = 0x44, kVSelected = 0xA0, kVHp = 0x540, kVLoco = 0xB1C,
                    kVPurpose = 0xB20, kVDead = 0xBCD, kPol = 0x30,
                    kVOrders = 0xB68;
constexpr uintptr_t kCityPos = 0x124, kCitySpecialty = 0x540, kCitySlots = 0x43C,
                    kCityCapture = 0x748, // + 4: capturing political id
                    kCityBuildings = 0x340, kBuildingState = 0x120 + 0x34; // combatant state, 2 = destroyed
constexpr uint8_t kShared = 0xFD; // a city everybody may use (the capital while cities < players)
constexpr uintptr_t kCivCities = 0x9C;
constexpr uint32_t kMsgActivated = 0x287259F6, kBtnLand = 0x62D6902, kBtnWater = 0x62D6908, kBtnAir = 0x62D690F,
                   kBtnPlanner = 0x62D5618, kBuildingType = 0x0E9CB8BA;
constexpr uintptr_t kPlannerAddBuildingRet = 0xD1419C;

static uintptr_t a_GetAllCivs, a_GetAllCities, a_GetAllVehicles, a_CivByPol, a_PlayerCiv, a_GetMoney, a_SpendMoney,
    a_CreateVehicle, a_SlotsLeft, a_VehicleCost, a_DesignKey, a_DesignId, a_SpawnVehicle, a_AddBuilding,
    a_SetBuildingModel, a_SnapToSlot, a_ChangeOwner, a_Capture, a_SetOrder, a_SetMoveOrder, a_OrderStamp, a_Die,
    a_SetSelected, a_PushEvent, a_BTTick, a_CreateInstance, a_UIMessage, a_CreateBuilding,
    a_NodeClaim, a_NodeUnclaim, a_SetCapture, a_SpawnNextCity, a_ExpansionGet, a_CinematicMgr, a_DestroyBuilding,
    a_CombatantDamage, a_AddMoney;

template <typename T>
static T& F(void* p, uintptr_t off) { return *(T*)((uint8_t*)p + off); }

static bool is_host() { return C.sess && C.sess->role() == net::Role::Host && C.sess->connected(); }
static bool is_guest() { return C.sess && C.sess->role() == net::Role::Client && C.sess->connected(); }
bool active() { return g_ok && C.sess && C.sess->connected() && game::mode_id() == modes::kCiv; }

static void* nm() { return game::noun_manager(); }
static std::vector<void*> vec_of(void* v)
{
    std::vector<void*> r;
    if (!v)
        return r;
    void** b = F<void**>(v, 0);
    void** e = F<void**>(v, 4);
    for (void** it = b; it && it < e && r.size() < 512; ++it)
        if (*it)
            r.push_back(*it);
    return r;
}
static std::vector<void*> all_cities() { return vec_of(thiscall<void*>(a_GetAllCities, nm())); }
static std::vector<void*> all_vehicles()
{
    std::vector<void*> r;
    void* manager = nm();
    if (!g_ok || !a_GetAllVehicles || !manager) return r;
    for (void* v : vec_of(thiscall<void*>(a_GetAllVehicles, manager)))
        if (game::object_alive(v) && !F<uint8_t>(v, kVDead))
            r.push_back(v);
    return r;
}
// The vehicles the game has right now (its own list, nothing filtered), once per frame. A vehicle
// the game destroyed and freed was still driven by guest_tick through our map (AV in civ::tick on a
// freed object, then the guest froze: coop_chaos civ_npc, 2026-10-23); object_alive() reads the
// freed memory and cannot tell.
static std::unordered_set<void*> g_exist;
static uint64_t g_exist_frame = ~0ull;
static bool exists(void* v)
{
    uint64_t f = H->frame_index();
    if (f != g_exist_frame) {
        g_exist.clear();
        for (void* x : vec_of(thiscall<void*>(a_GetAllVehicles, nm())))
            g_exist.insert(x);
        g_exist_frame = f;
    }
    return v && g_exist.count(v);
}
static void* player_civ() { return thiscall<void*>(a_PlayerCiv, nm()); }
static uint32_t pol_of(void* o) { return F<uint32_t>(o, kPol); }
static net::Vec3 city_pos(void* c)
{
    auto& p = F<float[3]>(c, kCityPos);
    return {p[0], p[1], p[2]};
}
static net::Vec3 veh_pos(void* v)
{
    auto& p = F<float[3]>(v, kVPos);
    return {p[0], p[1], p[2]};
}
static net::Quat veh_rot(void* v)
{
    auto& q = F<float[4]>(v, kVRot);
    return {q[0], q[1], q[2], q[3]};
}

// ---------------- ownership ----------------
static std::unordered_map<void*, uint8_t> g_city_owner; // both: player city -> player id
static std::vector<void*> g_host_cities;                // host: snapshot order; guest: host index -> local
static std::unordered_set<void*> g_city_gifted;         // host: cities given away keep their owner
static void* g_ui_city;                                 // the city whose menu was opened last
static uint32_t g_gifts;

// ---------------- host ----------------
struct HV {
    uint16_t id;
    uint8_t owner;
};
static std::unordered_map<void*, HV> g_hv;
static std::unordered_map<uint16_t, void*> g_hv_by_id;
static uint16_t g_next_id = 1;
static double g_last_send;
static uint16_t g_cycle;
static uint32_t g_cmds_done, g_cmds_rejected, g_builds_sent, g_unselected, g_buys_refused;
static int g_build_from = -1; // guest whose build is being executed (not echoed back to it)
static int g_pending_owner = -1; // owner of the vehicle the host is about to spawn for a guest
static std::vector<uint8_t> g_split_for;

// ---------------- guest ----------------
struct GV {
    void* v = nullptr;
    uint8_t owner = 0xFE, purpose = 0, loco = 0;
    uint32_t pol = 0;
    net::Vec3 pos;
    net::Quat rot;
    float hp = 0;
    double seen = 0;
    net::VelocityEstimate est; // vehicles snapshots carry no velocity
    bool listed = false;       // seen in the game's vehicle list (a new one joins it a little later)
};
static std::unordered_map<uint16_t, GV> g_gv;
static std::unordered_map<void*, uint16_t> g_gv_of;
static std::unordered_set<uint16_t> g_cycle_ids;
static std::unordered_set<uint8_t> g_cycle_chunks; // chunks of the current cycle received
static uint32_t g_incomplete;                      // cycles whose cleanup was skipped (a chunk missing)
static uint32_t g_last_chunks, g_max_chunk;        // host: chunks per cycle, largest chunk (bytes)
static size_t g_chunk_budget = net::kMaxPacket - 48; // host: bytes per chunk (coop.civ chunk <bytes>, tests)
static uint16_t g_rx_cycle = 0xFFFF;
static bool g_applying; // executing a host message: the hooks let the game act
static uint32_t g_created, g_removed, g_cmds_sent, g_builds_in, g_captures_in, g_orders_dropped, g_growth_blocked, g_nodes_in;

static uint8_t owner_of_vehicle(void* v)
{
    if (C.sess && C.sess->role() == net::Role::Host) {
        auto it = g_hv.find(v);
        return it == g_hv.end() ? 0xFF : it->second.owner;
    }
    auto it = g_gv_of.find(v);
    return it == g_gv_of.end() ? 0xFF : g_gv[it->second].owner;
}

static uint8_t owner_of_city(void* c)
{
    auto it = g_city_owner.find(c);
    return it == g_city_owner.end() ? 0xFF : it->second;
}

static int host_index_of_city(void* c)
{
    for (size_t i = 0; i < g_host_cities.size(); i++)
        if (g_host_cities[i] == c)
            return (int)i;
    return -1;
}

static void* nearest_player_city(const net::Vec3& p, uint8_t* owner = nullptr)
{
    void* best = nullptr;
    float bd = 1e18f;
    for (auto& [c, o] : g_city_owner) {
        float d = net::dist(city_pos(c), p);
        if (d < bd) {
            bd = d;
            best = c;
            if (owner)
                *owner = o;
        }
    }
    return best;
}

// ---------------- hooks ----------------
// behaviour tree tick shared by every agent: skipped for vehicle mirrors on guests
using BT_t = int(SK_CDECL*)(uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t);
static BT_t o_BT;
static int SK_CDECL h_BT(uint32_t a0, uint32_t a1, uint32_t a2, uint32_t a3, uint32_t agent, uint32_t a5, uint32_t a6,
                         uint32_t a7, uint32_t a8)
{
    if (agent && is_guest() && game::mode_id() == modes::kCiv && !IsBadReadPtr((void*)agent, 4) &&
        *(uintptr_t*)agent == H->rebase(kVehicleVt))
        return 0;
    return o_BT(a0, a1, a2, a3, agent, a5, a6, a7, a8);
}

using SetSelected_t = int(SK_THISCALL*)(void*, int);
static SetSelected_t o_SetSelected;
static int SK_THISCALL h_SetSelected(void* spatial, int sel)
{
    if ((sel & 0xFF) && active()) {
        uint8_t o = owner_of_vehicle((uint8_t*)spatial - kVSpatial);
        if (o != 0xFF && o != C.sess->my_id())
            sel = 0;
    }
    return o_SetSelected(spatial, sel);
}

struct Target {
    uint8_t kind = 0; // 0 none, 1 vehicle id, 2 host city index, 3 object by noun + position
    uint16_t id = 0;
    uint32_t noun = 0;
    net::Vec3 pos;
};

static Target encode_target(void* t)
{
    Target r;
    if (!t)
        return r;
    auto it = g_gv_of.find(t);
    if (it != g_gv_of.end()) {
        r.kind = 1;
        r.id = it->second;
        return r;
    }
    int ci = host_index_of_city(t);
    if (ci >= 0) {
        r.kind = 2;
        r.id = (uint16_t)ci;
        return r;
    }
    r.kind = 3;
    r.noun = game::noun_id(t);
    game::object_position(t, r.pos);
    return r;
}

static void write_target(net::Writer& w, const Target& t)
{
    w.u8(t.kind); w.u16(t.id); w.u32(t.noun);
    w.f32(t.pos.x); w.f32(t.pos.y); w.f32(t.pos.z);
}

static Target read_target(net::Reader& r)
{
    Target t;
    t.kind = r.u8(); t.id = r.u16(); t.noun = r.u32();
    t.pos.x = r.f32(); t.pos.y = r.f32(); t.pos.z = r.f32();
    return t;
}

enum : uint8_t { CMD_ORDER = 0, CMD_MOVE = 1, CMD_BUY = 2, CMD_BUILD = 3, CMD_GIVE = 4 };

static void send_cmd(net::Writer& w)
{
    C.sess->send_reliable_to(0, net::MSG_CIV_CMD, w);
    g_cmds_sent++;
}

using SetOrder_t = int(SK_THISCALL*)(void*, void*, int, int);
static SetOrder_t o_SetOrder;
static int SK_THISCALL h_SetOrder(void* v, void* target, int type, int flag)
{
    if (!g_applying && is_guest() && game::mode_id() == modes::kCiv) {
        auto it = g_gv_of.find(v);
        if (it != g_gv_of.end()) {
            // a mirror of another player's or nation's vehicle: the guest's own civ AI orders the
            // other nations' vehicles every few frames (hundreds of rejected commands a minute)
            if (g_gv[it->second].owner != C.sess->my_id()) {
                g_orders_dropped++;
                return 0;
            }
            net::Writer w;
            w.u8(CMD_ORDER);
            w.u16(it->second);
            w.u32((uint32_t)type);
            w.u32((uint32_t)flag);
            write_target(w, encode_target(target));
            send_cmd(w);
            return 0;
        }
    }
    return o_SetOrder(v, target, type, flag);
}

using SetMoveOrder_t = int(SK_THISCALL*)(void*, float, float, float, int);
static SetMoveOrder_t o_SetMoveOrder;
static int SK_THISCALL h_SetMoveOrder(void* v, float x, float y, float z, int flag)
{
    if (!g_applying && is_guest() && game::mode_id() == modes::kCiv) {
        auto it = g_gv_of.find(v);
        if (it != g_gv_of.end()) {
            if (g_gv[it->second].owner != C.sess->my_id()) {
                g_orders_dropped++;
                return 0;
            }
            net::Writer w;
            w.u8(CMD_MOVE);
            w.u16(it->second);
            w.f32(x); w.f32(y); w.f32(z);
            w.u32((uint32_t)flag);
            send_cmd(w);
            return 0;
        }
    }
    return o_SetMoveOrder(v, x, y, z, flag);
}

static void request_buy(void* city, int loco)
{
    net::Writer w;
    w.u8(CMD_BUY);
    w.u8((uint8_t)host_index_of_city(city));
    w.u8((uint8_t)loco);
    send_cmd(w);
}

// RolloverCivCity window (city menu): vehicle purchase and city planner buttons
using UIMessage_t = int(SK_THISCALL*)(void*, void*, const uint8_t*);
static UIMessage_t o_UIMessage;
static int SK_THISCALL h_UIMessage(void* self, void* win, const uint8_t* msg)
{
    if (active() && msg && F<uint32_t>((void*)msg, 8) == kMsgActivated) {
        uint32_t id = F<uint32_t>((void*)msg, 0xC);
        int loco = id == kBtnLand ? 0 : id == kBtnWater ? 1 : id == kBtnAir ? 2 : -1;
        if (void* c0 = F<void*>(self, 0xA8))
            g_ui_city = c0;
        if (loco >= 0 || id == kBtnPlanner) {
            void* city = F<void*>(self, 0xA8);
            uint8_t owner = owner_of_city(city);
            if (owner != 0xFF && owner != kShared && owner != C.sess->my_id()) {
                if (C.feed)
                    C.feed("this city belongs to " + C.sess->name_of(owner));
                return 1;
            }
            if (loco >= 0 && is_guest() && host_index_of_city(city) >= 0) {
                request_buy(city, loco); // the host builds it and pays from the shared treasury
                return 1;
            }
        }
    }
    return o_UIMessage(self, win, msg);
}

static uint32_t type_for_noun(uint32_t noun)
{
    switch (noun) {
    case 0x18EB106: return 0xFF10521; // house
    case 0x18EA2CC: return 0xECADE42; // factory
    case 0x1A56ABA: return 0x1A55E4D; // entertainment
    }
    return 0;
}

static void broadcast_building(void* city, uint32_t noun, const net::Vec3& pos)
{
    int ci = host_index_of_city(city);
    if (ci < 0) { // a city founded this frame (its first buildings come at once): the list is 0.1 s old
        g_host_cities = all_cities();
        ci = host_index_of_city(city);
    }
    if (ci < 0 || !type_for_noun(noun))
        return;
    net::Writer w;
    w.u8((uint8_t)ci);
    w.u32(noun);
    w.f32(pos.x); w.f32(pos.y); w.f32(pos.z);
    C.sess->broadcast_reliable(net::MSG_CIV_BUILD, w, g_build_from);
    g_builds_sent++;
}

using AddBuilding_t = int(SK_THISCALL*)(void*, void*, int);
static AddBuilding_t o_AddBuilding;
static int SK_THISCALL h_AddBuilding(void* city, void* b, int x)
{
    void* ret = __builtin_return_address(0);
    int r = o_AddBuilding(city, b, x);
    if (!active() || g_applying || !b)
        return r;
    net::Vec3 pos;
    game::object_position(b, pos);
    uint32_t noun = game::noun_id(b);
    if (is_host()) {
        broadcast_building(city, noun, pos); // every building the host gets appears on the guests
    } else if (is_guest() && (uintptr_t)ret == H->rebase(kPlannerAddBuildingRet)) {
        // the guest placed a building in its city planner: the host builds the same one
        int ci = host_index_of_city(city);
        if (ci >= 0) {
            net::Writer w;
            w.u8(CMD_BUILD);
            w.u8((uint8_t)ci);
            w.u32(noun);
            w.f32(pos.x); w.f32(pos.y); w.f32(pos.z);
            send_cmd(w);
        }
    }
    return r;
}

// city growth by the civ AI (docs/re/civ.md 1): the guest runs the other nations' AI too, so
// without this its cities grew on their own and got the host's buildings on top (NPC cities had
// more buildings on the guest after a minute). Only the two AI growth callers are blocked;
// CreateBuildingByType (0xBE74A0, the fill-the-city loop 0xCFB827 waits for the count) is not.
constexpr uintptr_t kGrowthRetBudget = 0xBE5BB3, kGrowthRetNew = 0xBE616E;
using CreateBuilding_t = void*(SK_THISCALL*)(void*, uint32_t, uint32_t);
static CreateBuilding_t o_CreateBuilding;
static void* SK_THISCALL h_CreateBuilding(void* city, uint32_t noun, uint32_t type)
{
    uintptr_t ret = (uintptr_t)__builtin_return_address(0);
    if (!g_applying && is_guest() && game::mode_id() == modes::kCiv &&
        (ret == H->rebase(kGrowthRetBudget) || ret == H->rebase(kGrowthRetNew))) {
        g_growth_blocked++;
        return nullptr; // both callers ignore the result (the host's growth comes as MSG_CIV_BUILD)
    }
    return o_CreateBuilding(city, noun, type);
}

// New cities of other nations appear over time at the planet's city sites (0xAE3A80 picks the next
// free site, the city materializes a few seconds later through a timed callback, docs/re/civ.md 9).
// The guest's own planet tick founded cities the host never had (and the host's came without their
// first buildings): guests only found the cities the host founds, in the same order.
constexpr uint8_t kNextCitySite = 0xFF; // MSG_CIV_BUILD city index: "found the next city site", noun 0
using SpawnNextCity_t = bool(SK_THISCALL*)(void*, void*, int);
static SpawnNextCity_t o_SpawnNextCity;
static uint32_t g_cities_founded, g_cities_in, g_cities_blocked;
static bool SK_THISCALL h_SpawnNextCity(void* ex, void* who, int flag)
{
    if (!g_applying && is_guest() && game::mode_id() == modes::kCiv) {
        g_cities_blocked++;
        return false; // both callers ignore the result
    }
    bool r = o_SpawnNextCity(ex, who, flag);
    if (r && is_host() && game::mode_id() == modes::kCiv && !who && !flag) {
        net::Writer w;
        w.u8(kNextCitySite);
        w.u32(0);
        w.f32(0); w.f32(0); w.f32(0);
        C.sess->broadcast_reliable(net::MSG_CIV_BUILD, w);
        g_cities_founded++;
    }
    return r;
}

// Money is the host's: the guest's local economy (its own copy of the claimed spice derricks, city
// income) ran too and every local gain went to the host as a shared-money delta (+80..+100 every few
// seconds: the treasury grew about twice as fast with a guest). Guests ignore AddMoney for the player
// civilization; their spending still reaches the host (SpendMoney is another function).
using AddMoney_t = void(SK_THISCALL*)(void*, float, int);
static AddMoney_t o_AddMoney;
static uint32_t g_income_blocked;
static void SK_THISCALL h_AddMoney(void* civ, float amount, int difficulty)
{
    if (!g_applying && is_guest() && game::mode_id() == modes::kCiv && civ == player_civ() && amount > 0) {
        g_income_blocked++;
        return;
    }
    o_AddMoney(civ, amount, difficulty);
}

using Capture_t = int(SK_THISCALL*)(void*, uint32_t, int, int, int, int);
static Capture_t o_Capture;
static int SK_THISCALL h_Capture(void* city, uint32_t pol, int type, int a3, int a4, int a5)
{
    if (is_guest() && game::mode_id() == modes::kCiv && !g_applying)
        return 0; // captures happen on the host and come back with the snapshot
    int r = o_Capture(city, pol, type, a3, a4, a5);
    if (is_host() && game::mode_id() == modes::kCiv) {
        void* pc = player_civ();
        if (pc && pol == pol_of(pc) && !g_city_owner.count(city)) {
            // the city goes to the player whose vehicle is closest (who took it)
            uint8_t owner = 0;
            float bd = 1e18f;
            for (auto& [v, hv] : g_hv) {
                if (hv.owner == 0xFE)
                    continue;
                float d = net::dist(veh_pos(v), city_pos(city));
                if (d < bd) {
                    bd = d;
                    owner = hv.owner;
                }
            }
            g_city_owner[city] = owner;
            if (C.feed)
                C.feed(C.sess->name_of(owner) + " captured a city");
        }
    }
    return r;
}

// ---------------- co-op: siege, roles, alarm, superweapon (docs/re/civ.md, co-op features) -----
// The game sees all players as one civilization: who did it comes from the attacking vehicle and
// the mod's owner table.
constexpr double kJointWindow = 10.0;
static uint8_t g_hit_by = 0xFF;                         // the player whose vehicle is hitting right now
static std::unordered_map<void*, std::unordered_map<uint8_t, double>> g_siege; // city -> player -> last hit
static std::unordered_set<void*> g_joint_told;
static uint32_t g_joint_hits, g_military_hits, g_religious_boosts, g_alarms, g_votes;

static uint8_t player_of_combatant(void* att)
{
    if (!att || IsBadReadPtr(att, 4))
        return 0xFF;
    void* v = vcall<void*>(att, 0x5C / 4, 0x137E8E0u); // the vehicle behind a combatant
    if (!v)
        return 0xFF;
    auto it = g_hv.find(v);
    return it == g_hv.end() ? 0xFF : it->second.owner;
}

static uint8_t role(uint8_t p) { return C.role_of && p != 0xFF ? C.role_of(p) : 0; }

// building / city hall TakeDamage (this = building+0x120): a military governor's vehicles hit harder;
// the player is remembered for the capture damage the original adds
using BTakeDamage_t = int(SK_THISCALL*)(void*, float, uint32_t, int, const void*, void*);
static BTakeDamage_t o_BTakeDamage, o_HallTakeDamage;
static int b_damage(BTakeDamage_t orig, void* self, float dmg, uint32_t pol, int type, const void* dir, void* att)
{
    uint8_t p = is_host() && active() ? player_of_combatant(att) : 0xFF;
    if (p != 0xFF && role(p) == 1) {
        dmg *= 1.25f;
        g_military_hits++;
    }
    uint8_t prev = g_hit_by;
    g_hit_by = p;
    int r = orig(self, dmg, pol, type, dir, att);
    g_hit_by = prev;
    return r;
}
static int SK_THISCALL h_BTakeDamage(void* s, float d, uint32_t pol, int t, const void* dir, void* a)
{
    return b_damage(o_BTakeDamage, s, d, pol, t, dir, a);
}
static int SK_THISCALL h_HallTakeDamage(void* s, float d, uint32_t pol, int t, const void* dir, void* a)
{
    return b_damage(o_HallTakeDamage, s, d, pol, t, dir, a);
}

// capture damage: the city is taken faster when vehicles of two or more players attack it
using AddCapture_t = void(SK_CDECL*)(void*, void*, void*, uint32_t, float, uint32_t, bool);
static AddCapture_t o_AddCapture;
static void SK_CDECL h_AddCapture(void* ctx, void* city, void* meter, uint32_t owner, float dmg, uint32_t att, bool f)
{
    if (is_host() && active() && city && g_hit_by != 0xFF) {
        double now = H->time_seconds();
        auto& hits = g_siege[city];
        hits[g_hit_by] = now;
        int players = 0;
        for (auto& [p, t] : hits)
            if (now - t < kJointWindow)
                players++;
        if (players >= 2) {
            dmg *= 1.5f;
            g_joint_hits++;
            if (!g_joint_told.count(city) && C.notice) {
                g_joint_told.insert(city);
                C.notice("joint siege: the city falls 50% faster");
            }
        }
    }
    o_AddCapture(ctx, city, meter, owner, dmg, att, f);
}

// religious conversion: a religious governor's preachers near the city convert faster, and even
// more while another player's army besieges it
using Convert_t = void(SK_THISCALL*)(void*, uint32_t, int);
static Convert_t o_Convert;
static void SK_THISCALL h_Convert(void* city, uint32_t pol, int count)
{
    if (is_host() && active() && city && count > 0) {
        net::Vec3 cp = city_pos(city);
        uint8_t preacher = 0xFF;
        for (auto& [v, h] : g_hv)
            if (role(h.owner) == 3 && F<int>(v, kVPurpose) == 2 && net::dist(veh_pos(v), cp) < 200.f) {
                preacher = h.owner;
                break;
            }
        if (preacher != 0xFF) {
            float k = 1.5f;
            double now = H->time_seconds();
            auto it = g_siege.find(city);
            if (it != g_siege.end())
                for (auto& [p, t] : it->second)
                    if (p != preacher && now - t < kJointWindow)
                        k = 2.f; // combined arms
            count = (int)std::ceil(count * k);
            g_religious_boosts++;
        }
    }
    o_Convert(city, pol, count);
}

// a player's city is attacked: everybody sees it (beacon + notice), and the other players'
// vehicles near it rush there faster for a while
struct Boost {
    float orig;
    double until;
};
static std::unordered_map<void*, Boost> g_boost;
static std::unordered_map<void*, double> g_alarm_at;

static void show_alarm(void* city, uint8_t kind, uint8_t owner)
{
    if (!city || !C.beacon)
        return;
    std::string who = owner == kShared ? "the capital" : C.sess->name_of(owner) + "'s city";
    C.beacon(city_pos(city), (kind ? "✝ " : "⚔ ") + who + (kind ? " is being converted" : " is under attack"),
             20.0, 0xFFFF4040);
}

using OnAttacked_t = void(SK_THISCALL*)(void*, int, uint32_t);
static OnAttacked_t o_OnAttacked;
static void SK_THISCALL h_OnAttacked(void* city, int kind, uint32_t att)
{
    o_OnAttacked(city, kind, att);
    if (!is_host() || !active() || kind > 1)
        return;
    uint8_t owner = owner_of_city(city);
    void* pc = player_civ();
    if (owner == 0xFF || (pc && att == pol_of(pc)))
        return;
    double now = H->time_seconds();
    if (now - g_alarm_at[city] < 20.0)
        return;
    g_alarm_at[city] = now;
    g_alarms++;
    int ci = host_index_of_city(city);
    net::Writer w;
    w.u8((uint8_t)ci); w.u8((uint8_t)kind); w.u8(owner);
    C.sess->broadcast_reliable(net::MSG_CIV_ALARM, w);
    show_alarm(city, (uint8_t)kind, owner);
    if (C.notice)
        C.notice(std::string(owner == kShared ? "the capital" : C.sess->name_of(owner) + "'s city") +
                 (kind ? " is being converted: help!" : " is under attack: help!"));
    // the defenders: vehicles of the other players within reach go 50% faster for 20 s
    net::Vec3 cp = city_pos(city);
    for (auto& [v, h] : g_hv) {
        if (h.owner == owner || net::dist(veh_pos(v), cp) > 600.f)
            continue;
        auto& b = g_boost[v];
        if (b.until < now)
            b.orig = F<float>(v, 0x218);
        b.until = now + 20.0;
    }
}

static void boost_tick(double now)
{
    for (auto it = g_boost.begin(); it != g_boost.end();) {
        void* v = it->first;
        bool alive = g_hv.count(v) && game::object_alive(v);
        if (!alive) {
            it = g_boost.erase(it);
            continue;
        }
        if (now > it->second.until) {
            F<float>(v, 0x218) = it->second.orig; // back to its own speed
            it = g_boost.erase(it);
            continue;
        }
        float want = it->second.orig * 1.5f; // RecalcStats rewrites it: keep it up every tick
        if (F<float>(v, 0x218) != want)
            F<float>(v, 0x218) = want;
        ++it;
    }
}

// superweapons are fired by a vote of all players
using FireSW_t = bool(SK_THISCALL*)(void*, int, const float*);
static FireSW_t o_FireSW;
static bool g_sw_bypass;
static bool SK_THISCALL h_FireSW(void* civ, int type, const float* target)
{
    if (g_sw_bypass || !active() || C.sess->players().size() < 2)
        return o_FireSW(civ, type, target);
    float pos[3] = {target ? target[0] : 0, target ? target[1] : 0, target ? target[2] : 0};
    if (is_guest()) { // the host asks everybody
        net::Writer w;
        w.u8(5 /* CMD_SUPER */);
        w.u32((uint32_t)type);
        w.f32(pos[0]); w.f32(pos[1]); w.f32(pos[2]);
        send_cmd(w);
        return false;
    }
    static void* s_civ;
    static int s_type;
    static float s_pos[3];
    s_civ = civ; s_type = type;
    memcpy(s_pos, pos, sizeof s_pos);
    bool has_target = target != nullptr;
    if (C.vote && C.vote(C.sess->name_of(C.sess->my_id()) + " wants to fire a superweapon", "the superweapon fires",
                         [has_target] {
                             g_sw_bypass = true;
                             o_FireSW(s_civ, s_type, has_target ? s_pos : nullptr);
                             g_sw_bypass = false;
                         }))
        g_votes++;
    return false;
}

static void host_superweapon_request(uint8_t from, net::Reader& r)
{
    int type = (int)r.u32();
    float pos[3] = {r.f32(), r.f32(), r.f32()};
    void* pc = player_civ();
    if (!r.ok() || !pc || !C.vote)
        return;
    static int s_type;
    static float s_pos[3];
    static void* s_civ;
    s_type = type; s_civ = pc;
    memcpy(s_pos, pos, sizeof s_pos);
    if (C.vote(C.sess->name_of(from) + " wants to fire a superweapon", "the superweapon fires", [] {
            g_sw_bypass = true;
            o_FireSW(s_civ, s_type, s_pos);
            g_sw_bypass = false;
        }))
        g_votes++;
}

bool init(const SkHost* host, SkMod* mod, Ctx ctx)
{
    H = host;
    M = mod;
    C = std::move(ctx);
    struct {
        uintptr_t* a;
        const char* n;
    } syms[] = {
        {&a_GetAllCivs, "Simulator::cGameNounManager::GetAllCivilizations"},
        {&a_GetAllCities, "Simulator::cGameNounManager::GetAllCities"},
        {&a_GetAllVehicles, "Simulator::cGameNounManager::GetAllVehicles"},
        {&a_CivByPol, "Simulator::cGameNounManager::GetCivilizationByPoliticalID"},
        {&a_PlayerCiv, "Simulator::cGameNounManager::GetPlayerCivilization"},
        {&a_GetMoney, "Simulator::cCivilization::GetMoney"},
        {&a_SpendMoney, "Simulator::cCivilization::SpendMoney"},
        {&a_CreateVehicle, "Simulator::cCivilization::CreateVehicle"},
        {&a_SlotsLeft, "Simulator::cCivilization::VehicleSlotsLeft"},
        {&a_VehicleCost, "Simulator::cCivilization::GetVehicleCost"},
        {&a_DesignKey, "Simulator::cCivilization::GetDesignKey"},
        {&a_DesignId, "Simulator::VehicleDesignId"},
        {&a_SpawnVehicle, "Simulator::cCity::SpawnVehicle"},
        {&a_AddBuilding, "Simulator::cCity::AddBuilding"},
        {&a_SetBuildingModel, "Simulator::cCity::SetBuildingModel"},
        {&a_SnapToSlot, "Simulator::cCity::SnapBuildingToSlot"},
        {&a_ChangeOwner, "Simulator::cCity::ChangeOwner"},
        {&a_Capture, "Simulator::cCity::Capture"},
        {&a_SetOrder, "Simulator::cVehicle::SetOrder"},
        {&a_SetMoveOrder, "Simulator::cVehicle::SetMoveOrder"},
        {&a_OrderStamp, "Simulator::cVehicle::OrderStamp"},
        {&a_Die, "Simulator::cVehicle::Die"},
        {&a_SetSelected, "Simulator::cVehicle::SetIsSelected"},
        {&a_PushEvent, "Simulator::cVehicle::PushEvent"},
        {&a_BTTick, "Simulator::BehaviorTreeTickGeneric"},
        {&a_CreateInstance, "Simulator::cGameNounManager::CreateInstance"},
        {&a_UIMessage, "UI::RolloverCivCity::HandleUIMessage"},
        {&a_CreateBuilding, "Simulator::cCity::CreateBuilding"},
        {&a_NodeClaim, "Simulator::cCommodityNode::Claim"},
        {&a_NodeUnclaim, "Simulator::cCommodityNode::Unclaim"},
        {&a_SetCapture, "Simulator::cCity::SetCaptureProgress"},
        {&a_SpawnNextCity, "Simulator::cCivExpansion::SpawnNextCity"},
        {&a_ExpansionGet, "Simulator::cCivExpansion::Get"},
        {&a_CinematicMgr, "Simulator::cCinematicManager::Get"},
        {&a_DestroyBuilding, "Simulator::cCity::DestroyBuilding"},
        {&a_CombatantDamage, "Simulator::cCombatant::TakeDamage"},
        {&a_AddMoney, "Simulator::cCivilization::AddMoney"},
    };
    g_ok = true;
    for (auto& s : syms)
        if (!(*s.a = H->sym(s.n))) {
            H->log(M, SK_WARN, "civ: missing symbol %s", s.n);
            g_ok = false;
        }
    if (!g_ok)
        return false;
    H->hook_create(M, (void*)a_BTTick, (void*)h_BT, (void**)&o_BT);
    H->hook_create(M, (void*)a_SetSelected, (void*)h_SetSelected, (void**)&o_SetSelected);
    H->hook_create(M, (void*)a_SetOrder, (void*)h_SetOrder, (void**)&o_SetOrder);
    H->hook_create(M, (void*)a_SetMoveOrder, (void*)h_SetMoveOrder, (void**)&o_SetMoveOrder);
    H->hook_create(M, (void*)a_UIMessage, (void*)h_UIMessage, (void**)&o_UIMessage);
    H->hook_create(M, (void*)a_AddBuilding, (void*)h_AddBuilding, (void**)&o_AddBuilding);
    H->hook_create(M, (void*)a_Capture, (void*)h_Capture, (void**)&o_Capture);
    H->hook_create(M, (void*)a_CreateBuilding, (void*)h_CreateBuilding, (void**)&o_CreateBuilding);
    H->hook_create(M, (void*)a_SpawnNextCity, (void*)h_SpawnNextCity, (void**)&o_SpawnNextCity);
    H->hook_create(M, (void*)a_AddMoney, (void*)h_AddMoney, (void**)&o_AddMoney);
    // co-op mechanics (optional: a missing symbol only disables that mechanic)
    struct Opt {
        const char* name;
        void* hook;
        void** orig;
    } opts[] = {
        {"Simulator::cBuilding::TakeDamage", (void*)h_BTakeDamage, (void**)&o_BTakeDamage},
        {"Simulator::cCityHall::TakeDamage", (void*)h_HallTakeDamage, (void**)&o_HallTakeDamage},
        {"Simulator::cCity::AddCaptureDamage", (void*)h_AddCapture, (void**)&o_AddCapture},
        {"Simulator::cCity::ConvertCitizens", (void*)h_Convert, (void**)&o_Convert},
        {"Simulator::cCity::OnAttacked", (void*)h_OnAttacked, (void**)&o_OnAttacked},
        {"Simulator::cCivilization::FireSuperweapon", (void*)h_FireSW, (void**)&o_FireSW},
    };
    for (auto& o : opts)
        if (uintptr_t a = H->sym(o.name))
            H->hook_create(M, (void*)a, o.hook, o.orig);
    return true;
}

// ---------------- host ----------------
static void host_assign_cities(const std::vector<uint8_t>& players)
{
    void* pc = player_civ();
    if (!pc || players.empty())
        return;
    auto cities = vec_of((uint8_t*)pc + kCivCities);
    // drop cities the player lost
    for (auto it = g_city_owner.begin(); it != g_city_owner.end();) {
        if (std::find(cities.begin(), cities.end(), it->first) == cities.end())
            it = g_city_owner.erase(it);
        else
            ++it;
    }
    bool resplit = players != g_split_for;
    g_split_for = players;
    if (cities.empty())
        return;
    // the first city of the player civ is the capital: the host's; the others in turns
    net::Vec3 cap = city_pos(cities[0]);
    std::sort(cities.begin() + 1, cities.end(),
              [&](void* a, void* b) { return net::dist(city_pos(a), cap) < net::dist(city_pos(b), cap); });
    // fewer cities than players: the capital is shared so that everybody can play from the start
    bool shared = cities.size() < players.size();
    if (shared != (g_city_owner.count(cities[0]) && g_city_owner[cities[0]] == kShared))
        resplit = true;
    size_t k = 0;
    for (size_t i = 0; i < cities.size(); i++) {
        if (!resplit && g_city_owner.count(cities[i]))
            continue;
        if (g_city_gifted.count(cities[i]) && g_city_owner.count(cities[i]) &&
            std::find(players.begin(), players.end(), g_city_owner[cities[i]]) != players.end())
            continue; // a gift stays with its new owner while that player is here
        g_city_owner[cities[i]] = i == 0 ? (shared ? kShared : 0) : players[(1 + k++) % players.size()];
    }
}

// Trade between the players: every pair of cities of two different players trades, the shared
// treasury earns kTradePerPair every kTradePeriod seconds (a player alone earns nothing extra)
constexpr double kTradePeriod = 30.0;
constexpr int kTradePerPair = 20;
static double g_last_trade;
static int g_trade_total, g_trade_pairs, g_trade_payouts;

static void trade_tick(double now)
{
    if (now - g_last_trade < kTradePeriod)
        return;
    g_last_trade = now;
    std::vector<std::pair<void*, uint8_t>> owned;
    for (auto& [city, o] : g_city_owner)
        if (o != kShared && o != 0xFF)
            owned.push_back({city, o});
    int pairs = 0, gain = 0;
    auto economic = [](uint8_t p) { return C.role_of && C.role_of(p) == 2; };
    for (size_t i = 0; i < owned.size(); i++)
        for (size_t j = i + 1; j < owned.size() && pairs < 8; j++)
            if (owned[i].second != owned[j].second) {
                pairs++;
                // an economic governor at either end doubles the pair (both: triple)
                gain += kTradePerPair * (1 + economic(owned[i].second) + economic(owned[j].second));
            }
    g_trade_pairs = pairs;
    if (!pairs)
        return;
    std::string err;
    if (!shared::debug_add("money", gain, err))
        return;
    g_trade_total += gain;
    if (g_trade_payouts++ % 6 == 0 && C.notice) // now and then, not every payout
        C.notice("trade between the players' cities: +" + std::to_string(gain) + " every 30 s (" +
                 std::to_string(g_trade_total) + " so far)");
}

// spice nodes (cCommodityNode, noun 0x403DF5C): position and owner political id (-1 free)
constexpr uint32_t kNodeNoun = 0x403DF5C;
static std::vector<std::pair<net::Vec3, uint32_t>> spice_nodes()
{
    std::vector<std::pair<net::Vec3, uint32_t>> r;
    game::for_each_object(
        [](void* o, uint32_t n, void* u) {
            net::Vec3 p;
            if (n == kNodeNoun && game::object_alive(o) && game::object_position(o, p))
                ((std::vector<std::pair<net::Vec3, uint32_t>>*)u)->push_back({p, pol_of(o)});
            return true;
        },
        &r);
    return r;
}

static void boost_tick(double now);
// a vehicle given away (or taken by a capture) stays selected on the giver's screen and its orders
// went to the game directly on the host: only the owner keeps a vehicle selected
static void drop_foreign_selection()
{
    uint8_t me = C.sess->my_id();
    for (void* v : all_vehicles())
        if (F<uint8_t>(v, kVSelected)) {
            uint8_t o = owner_of_vehicle(v);
            if (o != 0xFF && o != me) {
                thiscall<int>(a_SetSelected, (uint8_t*)v + kVSpatial, 0);
                g_unselected++;
            }
        }
}

// The host's world stands still while one of its cinematics plays (a captured city's caption waits
// for OK): everybody is told why the vehicles stopped
static bool g_movie;
static bool movie_playing()
{
    void* cm = a_CinematicMgr ? skg::cdecl_call<void*>(a_CinematicMgr) : nullptr;
    return cm && F<uint32_t>(cm, 0x2C) != 0;
}
static void movie_tick()
{
    bool on = movie_playing();
    if (on == g_movie)
        return;
    g_movie = on;
    if (on && C.notice)
        C.notice(C.sess->name_of(C.sess->my_id()) + " watches a cinematic: the world waits for the OK");
}

static void host_tick(double now)
{
    trade_tick(now);
    boost_tick(now);
    movie_tick();
    if (now - g_last_send < 0.1)
        return;
    g_last_send = now;
    drop_foreign_selection();
    std::vector<uint8_t> players;
    for (auto& p : C.sess->players())
        players.push_back(p.id);
    std::sort(players.begin(), players.end());
    host_assign_cities(players);
    void* pc = player_civ();
    uint32_t ppol = pc ? pol_of(pc) : 0;
    g_host_cities = all_cities();
    auto vehicles = all_vehicles();
    std::unordered_set<void*> alive(vehicles.begin(), vehicles.end());
    for (auto it = g_hv.begin(); it != g_hv.end();) {
        if (!alive.count(it->first)) {
            g_hv_by_id.erase(it->second.id);
            it = g_hv.erase(it);
        } else
            ++it;
    }
    for (void* v : vehicles) {
        if (g_hv.count(v))
            continue;
        uint8_t owner = 0xFE;
        if (pol_of(v) == ppol) {
            if (g_pending_owner >= 0)
                owner = (uint8_t)g_pending_owner;
            else
                nearest_player_city(veh_pos(v), &owner); // built by that city
            if (owner == 0xFF || owner == kShared)
                owner = 0;
        }
        uint16_t id = g_next_id++;
        if (!g_next_id)
            g_next_id = 1;
        g_hv[v] = {id, owner};
        g_hv_by_id[id] = v;
    }
    g_cycle++;
    // Chunks that fit one packet (kMaxPacket): a larger message is dropped by the connection for
    // good. Until 2026-10-23 chunk 0 carried every city and 24 vehicles (and later the spice nodes and
    // a city's buildings): ~1200 bytes with 9 cities and 24 vehicles, so with a grown planet the
    // guest got no cities, captures or the first 24 vehicles at all. The items are packed in order
    // (cities, spice nodes, one city's buildings, vehicles) into as many chunks as needed.
    constexpr size_t kCityBytes = 25, kNodeBytes = 16, kVehBytes = 41, kHead = 10;
    const size_t kBudget = g_chunk_budget; // net::kMaxPacket - 48 (packet + message headers); tests: less
    std::vector<std::pair<net::Vec3, uint32_t>> nodes;
    if (g_cycle % 10 == 0)
        nodes = spice_nodes();
    // the buildings of one city per snapshot (all cities within a second): the guest reconciles
    // what the incremental messages missed (a capture clears the ruins, ...)
    size_t bci = g_host_cities.empty() ? 0xFF : g_cycle % g_host_cities.size();
    std::vector<void*> blds;
    if (bci != 0xFF)
        for (void* bl : vec_of((uint8_t*)g_host_cities[bci] + kCityBuildings))
            if (type_for_noun(game::noun_id(bl)) && blds.size() < 32)
                blds.push_back(bl);
    struct Chunk {
        size_t c0 = 0, c1 = 0, n0 = 0, n1 = 0, v0 = 0, v1 = 0, bytes = kHead;
        bool bld = false;
    };
    std::vector<Chunk> ch(1);
    auto place = [&](size_t sz) -> Chunk& {
        if (ch.back().bytes + sz > kBudget)
            ch.emplace_back();
        ch.back().bytes += sz;
        return ch.back();
    };
    size_t ncity = std::min<size_t>(g_host_cities.size(), 250);
    for (size_t i = 0; i < ncity; i++) {
        Chunk& c = place(kCityBytes);
        if (c.c1 == c.c0)
            c.c0 = i;
        c.c1 = i + 1;
    }
    for (size_t i = 0; i < nodes.size() && i < 250; i++) {
        Chunk& c = place(kNodeBytes);
        if (c.n1 == c.n0)
            c.n0 = i;
        c.n1 = i + 1;
    }
    if (bci != 0xFF)
        place(1 + 17 * blds.size()).bld = true;
    for (size_t i = 0; i < vehicles.size(); i++) {
        Chunk& c = place(kVehBytes);
        if (c.v1 == c.v0)
            c.v0 = i;
        c.v1 = i + 1;
    }
    g_last_chunks = (uint32_t)ch.size();
    for (size_t k = 0; k < ch.size() && k < 255; k++) {
        const Chunk& c = ch[k];
        net::Writer w;
        w.u16(g_cycle);
        w.u8((uint8_t)k);
        w.u8((uint8_t)ch.size());
        w.u8((uint8_t)ncity);
        w.u8((uint8_t)c.c0);
        w.u8((uint8_t)(c.c1 - c.c0));
        for (size_t i = c.c0; i < c.c1; i++) {
            void* city = g_host_cities[i];
            net::Vec3 p = city_pos(city);
            w.f32(p.x); w.f32(p.y); w.f32(p.z);
            w.u32(pol_of(city));
            w.u8(owner_of_city(city));
            w.f32(F<float>(city, kCityCapture)); // military capture progress (the meter over the city)
            w.u32(F<uint32_t>(city, kCityCapture + 4));
        }
        // spice nodes and who owns them (once a second): claims happen on the host only
        w.u8((uint8_t)(c.n1 - c.n0));
        for (size_t i = c.n0; i < c.n1; i++) {
            w.f32(nodes[i].first.x); w.f32(nodes[i].first.y); w.f32(nodes[i].first.z);
            w.u32(nodes[i].second);
        }
        w.u8(c.bld ? (uint8_t)bci : 0xFF);
        if (c.bld) {
            w.u8((uint8_t)blds.size());
            for (void* bl : blds) {
                net::Vec3 bp;
                game::object_position(bl, bp);
                w.u32(game::noun_id(bl));
                w.f32(bp.x); w.f32(bp.y); w.f32(bp.z);
                w.u8(F<int>(bl, kBuildingState) == 2 ? 1 : 0);
            }
        }
        w.u8((uint8_t)(c.v1 - c.v0));
        for (size_t i = c.v0; i < c.v1; i++) {
            void* v = vehicles[i];
            const HV& hv = g_hv[v];
            net::Vec3 p = veh_pos(v);
            net::Quat q = veh_rot(v);
            w.u16(hv.id); w.u8(hv.owner); w.u32(pol_of(v));
            w.u8((uint8_t)F<int>(v, kVPurpose)); w.u8((uint8_t)F<int>(v, kVLoco));
            w.f32(p.x); w.f32(p.y); w.f32(p.z);
            w.f32(q.x); w.f32(q.y); w.f32(q.z); w.f32(q.w);
            w.f32(F<float>(v, kVHp));
        }
        g_max_chunk = std::max<uint32_t>(g_max_chunk, (uint32_t)w.size());
        for (auto& pl : C.sess->players())
            if (pl.id != 0)
                C.sess->send_unreliable_to(pl.id, net::MSG_CIV_STATE, w);
    }
}

static const char* g_buy_fail = "";
static void* host_buy(void* city, int loco)
{
    void* civ = player_civ();
    g_buy_fail = "no such city";
    if (!civ || !city)
        return nullptr;
    int purpose = F<int>(city, kCitySpecialty);
    g_buy_fail = "no vehicle slot left";
    if (thiscall<int>(a_SlotsLeft, civ, purpose) <= 0)
        return nullptr;
    float cost = thiscall<float>(a_VehicleCost, civ, purpose, loco);
    g_buy_fail = "not enough money";
    if (thiscall<float>(a_GetMoney, civ) < cost)
        return nullptr;
    g_buy_fail = "the city cannot build it";
    uint32_t* key = thiscall<uint32_t*>(a_DesignKey, civ, skg::cdecl_call<int>(a_DesignId, purpose, loco));
    if (!key)
        return nullptr;
    void* v = thiscall<void*>(a_SpawnVehicle, city, purpose, loco, key[0], key[1], key[2], 0);
    if (!v)
        return nullptr;
    thiscall<int>(a_PushEvent, v, 10);
    thiscall<void>(a_SpendMoney, civ, cost);
    return v;
}

// build `noun` in `city` at the free slot nearest to `pos` (the game's planner sequence)
static void* build_in_city(void* city, uint32_t noun, const net::Vec3& pos, bool pay)
{
    uint32_t type = type_for_noun(noun);
    if (!type || !city)
        return nullptr;
    void* o = thiscall<void*>(a_CreateInstance, nm(), noun);
    void* b = o ? game::cast(o, kBuildingType) : nullptr;
    if (!b)
        return nullptr;
    thiscall<void>(a_SetBuildingModel, city, b, type);
    vcall<void>(b, 0x54 / 4, city); // SetOwnerCity
    float p[3] = {pos.x, pos.y, pos.z};
    vcall<void>((uint8_t*)b + 0x34, 0x38 / 4, (const float*)p);
    if (!thiscall<bool>(a_SnapToSlot, city, b, 1))
        return nullptr; // no free slot (left for the game to collect)
    thiscall<int>(a_AddBuilding, city, b, 0);
    if (pay)
        if (void* civ = player_civ())
            thiscall<void>(a_SpendMoney, civ, vcall<float>(b, 0x5C / 4));
    return b;
}

static void* resolve_target(const Target& t)
{
    if (t.kind == 1) {
        auto it = g_hv_by_id.find(t.id);
        return it == g_hv_by_id.end() ? nullptr : it->second;
    }
    if (t.kind == 2)
        return t.id < g_host_cities.size() ? g_host_cities[t.id] : nullptr;
    if (t.kind == 3 && t.noun)
        return game::find_object(t.noun, t.pos, 10.f);
    return nullptr;
}

static void host_command(uint8_t from, net::Reader& r)
{
    uint8_t kind = r.u8();
    auto reject = [&] {
        if (from != C.sess->my_id())
            g_cmds_rejected++;
    };
    auto own_vehicle = [&](uint16_t id) -> void* {
        auto it = g_hv_by_id.find(id);
        if (it == g_hv_by_id.end() || g_hv[it->second].owner != from)
            return nullptr;
        return it->second;
    };
    if (kind == 5) { // a guest wants to fire a superweapon: everybody votes
        host_superweapon_request(from, r);
        return;
    }
    if (kind == CMD_GIVE) {
        uint8_t what = r.u8(), to = r.u8(), n = r.u8();
        int given = 0;
        for (int i = 0; i < n && r.ok(); i++) {
            uint16_t id = r.u16();
            if (what == 0) {
                if (void* v = own_vehicle(id)) {
                    g_hv[v].owner = to;
                    given++;
                }
            } else if (id < g_host_cities.size()) {
                void* city = g_host_cities[id];
                if (owner_of_city(city) == from) {
                    g_city_owner[city] = to;
                    g_city_gifted.insert(city);
                    given++;
                }
            }
        }
        g_gifts += given;
        if (given && C.notice)
            C.notice(C.sess->name_of(from) + " gave " +
                     (what == 0 ? std::to_string(given) + (given == 1 ? " vehicle" : " vehicles") : std::string("a city")) +
                     " to " + C.sess->name_of(to));
        if (!given)
            return reject();
        if (from != C.sess->my_id()) // the host's own gifts are not guest commands
            g_cmds_done++;
        return;
    }
    if (kind == CMD_ORDER) {
        uint16_t id = r.u16();
        int type = (int)r.u32(), flag = (int)r.u32();
        Target t = read_target(r);
        void* v = own_vehicle(id);
        void* target = resolve_target(t);
        if (!r.ok() || !v || (t.kind && !target))
            return reject();
        o_SetOrder(v, target, type, flag);
        thiscall<void>(a_OrderStamp, v, 0);
    } else if (kind == CMD_MOVE) {
        uint16_t id = r.u16();
        float x = r.f32(), y = r.f32(), z = r.f32();
        int flag = (int)r.u32();
        void* v = own_vehicle(id);
        if (!r.ok() || !v)
            return reject();
        o_SetMoveOrder(v, x, y, z, flag);
        thiscall<void>(a_OrderStamp, v, 0);
    } else if (kind == CMD_BUY) {
        uint8_t ci = r.u8(), loco = r.u8();
        void* city = ci < g_host_cities.size() ? g_host_cities[ci] : nullptr;
        uint8_t co = city ? owner_of_city(city) : 0xFF;
        if (!r.ok() || !city || (co != from && co != kShared) || loco > 2)
            return reject();
        g_pending_owner = from;
        void* v = host_buy(city, loco);
        if (v && !g_hv.count(v)) {
            uint16_t id = g_next_id++;
            g_hv[v] = {id, from};
            g_hv_by_id[id] = v;
        }
        g_pending_owner = -1;
        if (!v) {
            // the guest's button was enabled (its own count of vehicles may differ for a moment):
            // tell that player why nothing comes
            net::Writer nw;
            nw.str8(std::string("no vehicle: ") + g_buy_fail);
            C.sess->send_reliable_to(from, net::MSG_NOTICE, nw);
            g_buys_refused++;
            return reject();
        }
    } else if (kind == CMD_BUILD) {
        uint8_t ci = r.u8();
        uint32_t noun = r.u32();
        net::Vec3 p;
        p.x = r.f32(); p.y = r.f32(); p.z = r.f32();
        void* city = ci < g_host_cities.size() ? g_host_cities[ci] : nullptr;
        uint8_t co = city ? owner_of_city(city) : 0xFF;
        if (!r.ok() || !city || (co != from && co != kShared))
            return reject();
        g_build_from = from; // the guest already has it
        void* b = build_in_city(city, noun, p, false); // the guest paid in its planner (shared money)
        g_build_from = -1;
        if (!b)
            return reject();
    } else
        return reject();
    g_cmds_done++;
}

// ---------------- guest ----------------
static void* local_city_near(const net::Vec3& p)
{
    void* best = nullptr;
    float bd = 60.f;
    for (void* c : all_cities()) {
        float d = net::dist(city_pos(c), p);
        if (d < bd) {
            bd = d;
            best = c;
        }
    }
    return best;
}

// the host's building list of a city against the local one: a building missing here is built, one
// the host does not have is removed (a mismatch must be seen on two passes in a row: an incremental
// MSG_CIV_BUILD may be on its way)
static std::unordered_map<void*, int> g_bfix_seen;
static uint32_t g_bfix_added, g_bfix_removed, g_ruins_differ, g_ruins_in;
static void reconcile_buildings(void* city, size_t n,
                                const std::function<void(size_t, uint32_t&, net::Vec3&, bool&)>& host_b)
{
    auto local = vec_of((uint8_t*)city + kCityBuildings);
    std::vector<bool> used(local.size(), false);
    std::vector<size_t> missing;
    std::vector<void*> ruin_now;
    int ruins = 0;
    for (size_t i = 0; i < n; i++) {
        uint32_t noun;
        net::Vec3 p;
        bool ruin;
        host_b(i, noun, p, ruin);
        bool found = false;
        for (size_t k = 0; k < local.size() && !found; k++) {
            net::Vec3 lp;
            if (used[k] || game::noun_id(local[k]) != noun || !game::object_position(local[k], lp) ||
                net::dist(lp, p) > 4.f)
                continue;
            used[k] = found = true;
            if (ruin && F<int>(local[k], kBuildingState) != 2)
                ruin_now.push_back(local[k]);
            ruins += ruin != (F<int>(local[k], kBuildingState) == 2);
        }
        if (!found)
            missing.push_back(i);
    }
    std::vector<void*> extra;
    for (size_t k = 0; k < local.size(); k++)
        if (!used[k] && type_for_noun(game::noun_id(local[k])))
            extra.push_back(local[k]);
    g_ruins_differ = ruins;
    // a building the host's siege destroyed: the same damage here (the generic combatant damage
    // with no attacker: hp 0, state 2; no capture damage or alarm, those come from the host)
    static const float zero[3] = {0, 0, 0};
    for (void* b : ruin_now) {
        g_applying = true;
        thiscall<int>(a_CombatantDamage, (uint8_t*)b + 0x120, F<float>(b, kBuildingState + 4) + 1.f, 0xFFFFFFFFu, 0,
                      (const float*)zero, (void*)nullptr);
        g_applying = false;
        g_ruins_in++;
    }
    int& seen = g_bfix_seen[city];
    if (missing.empty() && extra.empty()) {
        seen = 0;
        return;
    }
    if (++seen < 2)
        return;
    seen = 0;
    g_applying = true;
    for (void* b : extra) {
        thiscall<void>(a_DestroyBuilding, city, b);
        g_bfix_removed++;
    }
    for (size_t i : missing) {
        uint32_t noun;
        net::Vec3 p;
        bool ruin;
        host_b(i, noun, p, ruin);
        if (build_in_city(city, noun, p, false))
            g_bfix_added++;
    }
    g_applying = false;
}

static void kill_local(void* v)
{
    g_applying = true;
    thiscall<void>(a_Die, v, 0, 0xFFFFFFFFu);
    g_applying = false;
}

static void guest_cities(net::Reader& r)
{
    uint8_t total = r.u8(), c0 = r.u8(), nc = r.u8();
    if (!r.ok())
        return;
    if (g_host_cities.size() != total)
        g_host_cities.resize(total, nullptr);
    for (int i = 0; i < nc && r.ok(); i++) {
        net::Vec3 p;
        p.x = r.f32(); p.y = r.f32(); p.z = r.f32();
        uint32_t pol = r.u32();
        uint8_t owner = r.u8();
        float capture = r.f32();
        uint32_t capture_by = r.u32();
        if (!r.ok() || c0 + i >= total)
            return;
        void* c = local_city_near(p);
        g_host_cities[c0 + i] = c;
        if (!c)
            continue;
        if (owner != 0xFE && owner != 0xFF)
            g_city_owner[c] = owner; // kShared included
        else
            g_city_owner.erase(c);
        if (pol_of(c) != pol) { // captured on the host
            g_applying = true;
            thiscall<void>(a_ChangeOwner, c, pol, 0, 0);
            g_applying = false;
            g_captures_in++;
        }
        // the siege meter: the guest's city is never hit (its mirrors do not shoot), so its own
        // progress stays 0 (and decays): show the host's (SetCaptureProgress updates the meter)
        if (std::isfinite(capture) && (std::fabs(F<float>(c, kCityCapture) - capture) > 0.5f ||
                                       F<uint32_t>(c, kCityCapture + 4) != capture_by)) {
            g_applying = true;
            thiscall<void>(a_SetCapture, c, capture_by, capture);
            g_applying = false;
        }
    }
}

static void guest_nodes_and_buildings(net::Reader& r)
{
    uint8_t nn = r.u8(); // spice node owners (a guest's derricks appear where the host's vehicles claimed)
    for (int i = 0; i < nn && r.ok(); i++) {
        net::Vec3 p;
        p.x = r.f32(); p.y = r.f32(); p.z = r.f32();
        uint32_t pol = r.u32();
        if (!r.ok())
            return;
        void* node = game::find_object(kNodeNoun, p, 5.f);
        if (!node || pol_of(node) == pol)
            continue;
        g_applying = true;
        if (pol == 0xFFFFFFFFu)
            thiscall<void>(a_NodeUnclaim, node);
        else
            thiscall<void>(a_NodeClaim, node, pol);
        g_applying = false;
        g_nodes_in++;
    }
    uint8_t ci = r.u8();
    if (!r.ok() || ci == 0xFF)
        return;
    struct HB {
        uint32_t noun;
        net::Vec3 p;
        bool ruin;
    };
    std::vector<HB> hb(r.u8());
    for (auto& b : hb) {
        b.noun = r.u32();
        b.p.x = r.f32(); b.p.y = r.f32(); b.p.z = r.f32();
        b.ruin = r.u8() != 0;
    }
    void* city = ci < g_host_cities.size() ? g_host_cities[ci] : nullptr;
    if (r.ok() && city)
        reconcile_buildings(city, hb.size(), [&](size_t i, uint32_t& noun, net::Vec3& p, bool& ruin) {
            noun = hb[i].noun; p = hb[i].p; ruin = hb[i].ruin;
        });
}

static void guest_state(net::Reader& r)
{
    uint16_t cycle = r.u16();
    uint8_t chunk = r.u8(), chunks = r.u8();
    if (!r.ok())
        return;
    if (cycle != g_rx_cycle) {
        g_rx_cycle = cycle;
        g_cycle_ids.clear();
        g_cycle_chunks.clear();
    }
    g_cycle_chunks.insert(chunk);
    guest_cities(r);
    guest_nodes_and_buildings(r);
    double now = H->time_seconds();
    uint8_t n = r.u8();
    for (int i = 0; i < n && r.ok(); i++) {
        uint16_t id = r.u16();
        GV m;
        m.owner = r.u8(); m.pol = r.u32(); m.purpose = r.u8(); m.loco = r.u8();
        m.pos.x = r.f32(); m.pos.y = r.f32(); m.pos.z = r.f32();
        m.rot.x = r.f32(); m.rot.y = r.f32(); m.rot.z = r.f32(); m.rot.w = r.f32();
        m.hp = r.f32();
        if (!r.ok())
            return;
        g_cycle_ids.insert(id);
        auto& g = g_gv[id];
        void* v = g.v;
        g.owner = m.owner; g.pol = m.pol; g.purpose = m.purpose; g.loco = m.loco;
        g.pos = m.pos; g.rot = m.rot; g.hp = m.hp; g.seen = now;
        g.est.update(m.pos, now);
        if (v && (!game::object_alive(v) || F<uint8_t>(v, kVDead))) {
            g_gv_of.erase(v);
            g.v = v = nullptr;
            g.listed = false;
        }
        if (v)
            continue;
        // bind a local vehicle of the same kind nearby, else create one for that civilization
        float best = 25.f;
        for (void* lv : all_vehicles()) {
            if (g_gv_of.count(lv) || pol_of(lv) != m.pol || F<int>(lv, kVPurpose) != m.purpose ||
                F<int>(lv, kVLoco) != m.loco)
                continue;
            float d = net::dist(veh_pos(lv), m.pos);
            if (d < best) {
                best = d;
                v = lv;
            }
        }
        if (!v) {
            void* civ = thiscall<void*>(a_CivByPol, nm(), m.pol);
            uint32_t* key = civ ? thiscall<uint32_t*>(a_DesignKey, civ,
                                                       skg::cdecl_call<int>(a_DesignId, (int)m.purpose, (int)m.loco))
                                : nullptr;
            if (key) {
                float p[3] = {m.pos.x, m.pos.y, m.pos.z};
                v = thiscall<void*>(a_CreateVehicle, civ, (int)m.purpose, (int)m.loco, key[0], key[1], key[2],
                                    (const float*)p, 0);
                if (v)
                    g_created++;
            }
        }
        if (!v)
            continue;
        g.v = v;
        g.listed = false;
        g_gv_of[v] = id;
    }
    if (chunk + 1 < chunks)
        return;
    // only a complete cycle tells which vehicles are gone: with a chunk lost (or late) every
    // vehicle of it was killed and made again on the next cycle
    if (g_cycle_chunks.size() < chunks) {
        g_incomplete++;
        return;
    }
    for (auto it = g_gv.begin(); it != g_gv.end();) {
        if (!g_cycle_ids.count(it->first)) {
            if (it->second.v && game::object_alive(it->second.v) && !F<uint8_t>(it->second.v, kVDead)) {
                kill_local(it->second.v);
                g_removed++;
            }
            g_gv_of.erase(it->second.v);
            it = g_gv.erase(it);
        } else
            ++it;
    }
    // the guest's own vehicles that the host does not have (its local civ AI, the start vehicle)
    for (void* lv : all_vehicles())
        if (!g_gv_of.count(lv)) {
            kill_local(lv);
            g_removed++;
        }
}

static void guest_tick(double now)
{
    static double last_sel;
    if (now - last_sel > 0.2) {
        last_sel = now;
        drop_foreign_selection();
    }
    for (auto& [id, g] : g_gv) {
        if (!g.v || !game::object_alive(g.v) || now - g.seen > 3.0)
            continue;
        net::Vec3 cur = veh_pos(g.v);
        net::Vec3 want = g.est.at(now); // moved on with the estimated velocity (no lag behind the host)
        float d = net::dist(cur, want);
        net::Vec3 p = want;
        if (d < 30.f && d > 0.01f) { // glide out small corrections
            float k = 0.5f;
            p = {cur.x + (want.x - cur.x) * k, cur.y + (want.y - cur.y) * k, cur.z + (want.z - cur.z) * k};
        }
        void* sp = (uint8_t*)g.v + kVSpatial;
        float pp[3] = {p.x, p.y, p.z}, q[4] = {g.rot.x, g.rot.y, g.rot.z, g.rot.w};
        vcall<void>(sp, 0xEC / 4); // StopMovement: the locomotion does not pull it back
        vcall<void>(sp, 0x44 / 4, (const float*)pp, (const float*)q); // Teleport
        F<float>(g.v, kVHp) = g.hp;
    }
}

static void guest_build(net::Reader& r)
{
    uint8_t ci = r.u8();
    uint32_t noun = r.u32();
    net::Vec3 p;
    p.x = r.f32(); p.y = r.f32(); p.z = r.f32();
    if (r.ok() && ci == kNextCitySite && noun == 0) { // the host's planet grew a new city: the same site here
        if (void* ex = skg::cdecl_call<void*>(a_ExpansionGet)) {
            g_applying = true;
            o_SpawnNextCity(ex, nullptr, 0);
            g_applying = false;
            g_cities_in++;
        }
        return;
    }
    void* city = ci < g_host_cities.size() ? g_host_cities[ci] : nullptr;
    if (!r.ok() || !city)
        return;
    g_applying = true;
    if (build_in_city(city, noun, p, false))
        g_builds_in++;
    g_applying = false;
}

static void prune_gone()
{
    // guests only: the host's map is rebuilt from the vehicle list in host_tick, and a vehicle it has
    // just bought for a guest is not in the list yet (dropping it lost the guest's ownership)
    for (auto& [id, g] : g_gv) {
        if (!g.v)
            continue;
        if (exists(g.v))
            g.listed = true;
        else if (g.listed) {
            g_gv_of.erase(g.v);
            g.v = nullptr;
            g.listed = false;
        }
    }
}

void tick(double now)
{
    if (!active())
        return;
    prune_gone();
    if (is_host())
        host_tick(now);
    else if (is_guest())
        guest_tick(now);
}

void on_message(uint8_t type, uint8_t from, net::Reader& r)
{
    if (!active())
        return;
    prune_gone();
    if (type == net::MSG_CIV_STATE && is_guest())
        guest_state(r);
    else if (type == net::MSG_CIV_CMD && is_host())
        host_command(from, r);
    else if (type == net::MSG_CIV_BUILD && is_guest())
        guest_build(r);
    else if (type == net::MSG_CIV_ALARM && is_guest()) {
        uint8_t ci = r.u8(), kind = r.u8(), owner = r.u8();
        if (r.ok() && ci < g_host_cities.size())
            show_alarm(g_host_cities[ci], kind, owner);
    }
}

void on_session_end()
{
    g_hv.clear();
    g_hv_by_id.clear();
    g_gv.clear();
    g_gv_of.clear();
    g_city_owner.clear();
    g_host_cities.clear();
    g_split_for.clear();
}

void for_each_owned(void (*fn)(void* obj, const net::Vec3& pos, uint8_t owner, bool city, void* user), void* user)
{
    if (!active())
        return;
    for (auto& [c, o] : g_city_owner)
        if (c && game::object_alive(c) && o != kShared)
            fn(c, city_pos(c), o, true, user);
    if (is_host()) {
        for (auto& [v, hv] : g_hv)
            if (hv.owner != 0xFE)
                fn(v, veh_pos(v), hv.owner, false, user);
    } else {
        for (auto& [id, g] : g_gv)
            if (g.v && g.owner != 0xFE)
                fn(g.v, veh_pos(g.v), g.owner, false, user);
    }
}

std::string status()
{
    char b[512];
    if (C.sess && C.sess->role() == net::Role::Host)
        snprintf(b, sizeof b,
                 "civ(host): vehicles=%u cities=%u owned_cities=%u cmds_done=%u rejected=%u builds_sent=%u gifts=%u "
                 "trade_pairs=%d trade_total=%d joint_hits=%u military_hits=%u religious=%u alarms=%u votes=%u boosts=%u "
                 "cities_founded=%u unselected=%u movie=%d buys_refused=%u chunks=%u max_chunk=%u\n",
                 (unsigned)g_hv.size(), (unsigned)g_host_cities.size(), (unsigned)g_city_owner.size(), g_cmds_done,
                 g_cmds_rejected, g_builds_sent, g_gifts, g_trade_pairs, g_trade_total, g_joint_hits, g_military_hits,
                 g_religious_boosts, g_alarms, g_votes, (unsigned)g_boost.size(), g_cities_founded, g_unselected, (int)movie_playing(), g_buys_refused, g_last_chunks,
                 g_max_chunk);
    else
        snprintf(b, sizeof b,
                 "civ(guest): mirrors=%u created=%u removed=%u cities=%u cmds_sent=%u builds_in=%u captures_in=%u "
                 "orders_dropped=%u growth_blocked=%u nodes_in=%u cities_in=%u cities_blocked=%u unselected=%u movie=%d "
                 "buildings_fixed=+%u/-%u ruins_in=%u ruins_differ=%u incomplete=%u income_blocked=%u\n",
                 (unsigned)g_gv.size(), g_created, g_removed, (unsigned)g_host_cities.size(), g_cmds_sent, g_builds_in,
                 g_captures_in, g_orders_dropped, g_growth_blocked, g_nodes_in, g_cities_in, g_cities_blocked, g_unselected, (int)movie_playing(), g_bfix_added,
                 g_bfix_removed, g_ruins_in, g_ruins_differ, g_incomplete, g_income_blocked);
    return b;
}

void for_each_target(void (*fn)(const char* kind, int id, int owner, const net::Vec3& pos, void* user), void* user)
{
    if (!active())
        return;
    for (size_t i = 0; i < g_host_cities.size(); i++)
        if (void* c = g_host_cities[i]) {
            uint8_t o = owner_of_city(c);
            fn("city", (int)i, o == 0xFF ? -1 : (int)o, city_pos(c), user);
        }
    if (is_host()) {
        for (auto& [v, hv] : g_hv)
            if (hv.owner != 0xFE && game::object_alive(v))
                fn("vehicle", hv.id, hv.owner, veh_pos(v), user);
    } else {
        for (auto& [id, g] : g_gv)
            if (g.v && g.owner != 0xFE && game::object_alive(g.v))
                fn("vehicle", id, g.owner, veh_pos(g.v), user);
    }
}

std::string list()
{
    std::string out;
    char b[300];
    if (!g_ok || !nm())
        return out;
    void* pc = player_civ();
    auto cities = all_cities();
    for (size_t i = 0; i < cities.size(); i++) {
        void* c = cities[i];
        net::Vec3 p = city_pos(c);
        uint8_t o = owner_of_city(c);
        auto blds = vec_of((uint8_t*)c + 0x340);
        int ruins = 0; // buildings destroyed (combatant +0x120, state +0x34 == 2; 0x8E8220)
        float hp = 0;
        for (void* bl : blds) {
            ruins += F<int>(bl, 0x120 + 0x34) == 2;
            hp += F<float>(bl, 0x120 + 0x38);
        }
        snprintf(b, sizeof b,
                 "city %d %p pol=%08X%s owner=%d buildings=%u pos=(%.0f %.0f %.0f) capture=%.0f/%08X convert=%.0f buy=%.0f "
                 "ruins=%d hp=%.0f\n",
                 host_index_of_city(c), c, pol_of(c), pc && pol_of(c) == pol_of(pc) ? " (player)" : "",
                 o == 0xFF ? -1 : (int)o, (unsigned)blds.size(), p.x, p.y, p.z, F<float>(c, 0x748),
                 F<uint32_t>(c, 0x74C), F<float>(c, 0x750), F<float>(c, 0x758), ruins, hp);
        out += b;
    }
    for (void* v : all_vehicles()) {
        net::Vec3 p = veh_pos(v);
        uint16_t id = 0;
        if (C.sess && C.sess->role() == net::Role::Host) {
            auto it = g_hv.find(v);
            id = it == g_hv.end() ? 0 : it->second.id;
        } else {
            auto it = g_gv_of.find(v);
            id = it == g_gv_of.end() ? 0 : it->second;
        }
        uint8_t o = owner_of_vehicle(v);
        // the current order (mOrders +0xB68, element {target, pos, type, issuer}): type, -1 none
        uint8_t* ob = F<uint8_t*>(v, kVOrders);
        uint8_t* oe = F<uint8_t*>(v, kVOrders + 4);
        int otype = ob && oe > ob ? *(int*)(ob + 0x10) : -1;
        snprintf(b, sizeof b,
                 "  veh #%u %p pol=%08X owner=%d purpose=%d loco=%d sel=%u hp=%.0f pos=(%.1f %.1f %.1f) order=%d\n", id, v,
                 pol_of(v), o == 0xFF ? -1 : (int)o, F<int>(v, kVPurpose), F<int>(v, kVLoco), F<uint8_t>(v, kVSelected),
                 F<float>(v, kVHp), p.x, p.y, p.z, otype);
        out += b;
    }
    return out;
}

// ---- debug (tests): the same hooked paths the UI uses ----
static void* local_vehicle(uint16_t id)
{
    if (C.sess && C.sess->role() == net::Role::Host) {
        auto it = g_hv_by_id.find(id);
        return it == g_hv_by_id.end() ? nullptr : it->second;
    }
    auto it = g_gv.find(id);
    return it == g_gv.end() ? nullptr : it->second.v;
}

std::string debug(const std::vector<std::string>& a)
{
    // tests of the co-op mechanics through the game's own functions (host)
    if (a.size() >= 3 && a[0] == "siege" && is_host()) { // siege <city> <player>: capture damage by that player
        size_t ci = (size_t)atoi(a[1].c_str());
        void* city = ci < g_host_cities.size() ? g_host_cities[ci] : nullptr;
        uintptr_t ctxf = H->rebase(0x1021150);
        if (!city)
            return "no such city";
        void* pc = player_civ();
        g_hit_by = (uint8_t)atoi(a[2].c_str());
        float before = F<float>(city, 0x748);
        (void)pc; // an attacker that is not the city's owner (the game ignores its own civ)
        h_AddCapture(skg::cdecl_call<void*>(ctxf), city, (uint8_t*)city + 0x2F0, pol_of(city), 10.f, 0x0BADF00Du,
                     false);
        g_hit_by = 0xFF;
        char b[96];
        snprintf(b, sizeof b, "capture %.2f -> %.2f", before, F<float>(city, 0x748));
        return b;
    }
    if (a.size() >= 2 && a[0] == "alarm" && is_host()) { // alarm <city>: as if an enemy attacked it
        size_t ci = (size_t)atoi(a[1].c_str());
        void* city = ci < g_host_cities.size() ? g_host_cities[ci] : nullptr;
        if (!city)
            return "no such city";
        g_alarm_at.erase(city);
        h_OnAttacked(city, 0, 0x0BADF00D);
        return "alarm raised";
    }
    if (a.size() >= 2 && a[0] == "super") { // super <type>: fire a superweapon (through the vote)
        void* pc = player_civ();
        return pc ? std::string("fired -> ") + (h_FireSW(pc, atoi(a[1].c_str()), nullptr) ? "1" : "0") : "no civ";
    }
    if (a.size() >= 3 && a[0] == "select") {
        void* v = local_vehicle((uint16_t)atoi(a[1].c_str()));
        if (!v)
            return "no such vehicle";
        thiscall<int>(a_SetSelected, (uint8_t*)v + kVSpatial, atoi(a[2].c_str()));
        return "selected=" + std::to_string(F<uint8_t>(v, kVSelected));
    }
    if (a.size() >= 5 && a[0] == "move") { // move <id> <x> <y> <z>
        void* v = local_vehicle((uint16_t)atoi(a[1].c_str()));
        if (!v)
            return "no such vehicle";
        thiscall<int>(a_SetMoveOrder, v, (float)atof(a[2].c_str()), (float)atof(a[3].c_str()),
                      (float)atof(a[4].c_str()), 1);
        return "move ordered";
    }
    if (a.size() >= 3 && a[0] == "buy") { // buy <host city index> <loco>: like the city menu button
        int ci = atoi(a[1].c_str());
        void* city = ci >= 0 && ci < (int)g_host_cities.size() ? g_host_cities[ci] : nullptr;
        if (!city)
            return "no such city";
        uint8_t o = owner_of_city(city);
        if (o != C.sess->my_id() && o != kShared)
            return "this city belongs to player " + std::to_string(o);
        if (is_guest()) {
            request_buy(city, atoi(a[2].c_str()));
            return "purchase requested";
        }
        return host_buy(city, atoi(a[2].c_str())) ? "bought" : "purchase failed";
    }
    if (a.size() >= 3 && a[0] == "build") { // build <host city index> <house|factory|fun>
        int ci = atoi(a[1].c_str());
        void* city = ci >= 0 && ci < (int)g_host_cities.size() ? g_host_cities[ci] : nullptr;
        if (!city)
            return "no such city";
        uint32_t noun = a[2] == "factory" ? 0x18EA2CC : a[2] == "fun" ? 0x1A56ABA : 0x18EB106;
        net::Vec3 p = city_pos(city);
        if (is_guest()) {
            net::Writer w;
            w.u8(CMD_BUILD);
            w.u8((uint8_t)ci);
            w.u32(noun);
            w.f32(p.x); w.f32(p.y); w.f32(p.z);
            send_cmd(w);
            g_applying = true; // the guest's own copy, as its planner would place it
            void* b = build_in_city(city, noun, p, true);
            g_applying = false;
            return b ? "built locally, sent to the host" : "no free slot";
        }
        return build_in_city(city, noun, p, true) ? "built" : "no free slot";
    }
    if (a.size() >= 1 && a[0] == "nouns") { // nouns [hex noun [TYPE]]: objects per noun, or that noun's objects on screen
        struct Q {
            uint32_t noun = 0, type = 0;
            std::unordered_map<uint32_t, int> count;
            std::string out;
            const SkHost* h;
        } q;
        q.h = H;
        if (a.size() >= 2)
            q.noun = (uint32_t)strtoul(a[1].c_str(), nullptr, 16);
        if (a.size() >= 3)
            q.type = (uint32_t)strtoul(a[2].c_str(), nullptr, 16);
        game::for_each_object(
            [](void* o, uint32_t n, void* u) {
                auto* q = (Q*)u;
                if (!q->noun) {
                    q->count[n]++;
                    return true;
                }
                net::Vec3 p;
                if (n != q->noun || !game::object_alive(o) || !game::object_position(o, p) || q->out.size() > 6000)
                    return true;
                float w[3] = {p.x, p.y, p.z}, sc[2] = {-1, -1};
                if (!q->h->world_to_screen(w, sc))
                    sc[0] = sc[1] = -1;
                char line[128];
                snprintf(line, sizeof line, "obj %p %.0f %.0f %.0f %d %d pol=%08X%s\n", o, p.x, p.y, p.z, (int)sc[0],
                         (int)sc[1], pol_of(o), q->type && !game::cast(o, q->type) ? " (not that type)" : "");
                q->out += line;
                return true;
            },
            &q);
        if (q.noun)
            return q.out;
        std::string out;
        for (auto& [n, c] : q.count) {
            char line[48];
            snprintf(line, sizeof line, "noun %08X %d\n", n, c);
            out += line;
        }
        return out;
    }
    if (a.size() >= 2 && a[0] == "buildings") { // buildings <host city index>: object, noun, state, hp, position
        int ci = atoi(a[1].c_str());
        void* city = ci >= 0 && ci < (int)g_host_cities.size() ? g_host_cities[ci] : nullptr;
        if (!city)
            return "no such city";
        std::string out;
        for (void* bl : vec_of((uint8_t*)city + kCityBuildings)) {
            net::Vec3 p;
            game::object_position(bl, p);
            char line[128];
            snprintf(line, sizeof line, "building %p noun=%08X state=%d hp=%.0f pos=(%.0f %.0f %.0f)\n", bl,
                     game::noun_id(bl), F<int>(bl, kBuildingState), F<float>(bl, kBuildingState + 4), p.x, p.y, p.z);
            out += line;
        }
        return out;
    }
    if (a.size() >= 2 && a[0] == "chunk") { // chunk <bytes>: smaller snapshot chunks (tests: many chunks)
        g_chunk_budget = std::clamp<size_t>((size_t)atoi(a[1].c_str()), 200, net::kMaxPacket - 48);
        g_max_chunk = 0;
        return "chunk budget " + std::to_string(g_chunk_budget);
    }
    if (a.size() >= 2 && a[0] == "slots") { // slots <host city index>: building plots, where they are on screen
        int ci = atoi(a[1].c_str());
        void* city = ci >= 0 && ci < (int)g_host_cities.size() ? g_host_cities[ci] : nullptr;
        if (!city)
            return "no such city";
        std::string out;
        uint8_t* b = F<uint8_t*>(city, kCitySlots);
        uint8_t* e = F<uint8_t*>(city, kCitySlots + 4);
        for (int k = 0; b && b + (k + 1) * 0x18 <= e && k < 32; k++) {
            uint8_t* s = b + k * 0x18;
            float* p = (float*)(s + 0xC);
            float sc[2] = {-1, -1};
            if (!H->world_to_screen(p, sc))
                sc[0] = sc[1] = -1;
            char line[128];
            snprintf(line, sizeof line, "slot %d %s %.0f %.0f %.0f %d %d\n", k, *(void**)(s + 4) ? "busy" : "free", p[0],
                     p[1], p[2], (int)sc[0], (int)sc[1]);
            out += line;
        }
        return out;
    }
    return "usage: coop.civ select <id> <0|1> | move <id> <x> <y> <z> | buy <city> <0 land|1 water|2 air> | "
           "build <city> <house|factory|fun> | slots <city>";
}

std::vector<syncdbg::Entry> sync_entries()
{
    std::vector<syncdbg::Entry> v;
    auto alive = all_vehicles();
    auto live = [&](void* x) { return std::find(alive.begin(), alive.end(), x) != alive.end(); };
    if (C.sess && C.sess->role() == net::Role::Host) {
        for (auto& [x, h] : g_hv)
            if (live(x))
                v.push_back({"v" + std::to_string(h.id), veh_pos(x), veh_rot(x)});
    } else {
        for (auto& [id, g] : g_gv)
            if (g.v && live(g.v))
                v.push_back({"v" + std::to_string(id), veh_pos(g.v), veh_rot(g.v)});
    }
    return v;
}

// own selected vehicles (ids as the host knows them); none: the own city whose menu was opened
static void own_selection(std::vector<uint16_t>& vehicles, int& city)
{
    uint8_t me = C.sess->my_id();
    city = -1;
    if (C.sess->role() == net::Role::Host) {
        for (auto& [v, h] : g_hv)
            if (h.owner == me && F<uint8_t>(v, kVSelected))
                vehicles.push_back(h.id);
    } else
        for (auto& [id, g] : g_gv)
            if (g.v && g.owner == me && F<uint8_t>(g.v, kVSelected))
                vehicles.push_back(id);
    if (vehicles.empty() && g_ui_city && owner_of_city(g_ui_city) == me)
        city = host_index_of_city(g_ui_city);
}

int own_selected(bool* is_city)
{
    if (!active())
        return 0;
    std::vector<uint16_t> v;
    int city;
    own_selection(v, city);
    if (is_city)
        *is_city = v.empty() && city >= 0;
    return v.empty() ? (city >= 0 ? 1 : 0) : (int)v.size();
}

int give_selected(uint8_t to)
{
    if (!active() || to == C.sess->my_id())
        return 0;
    std::vector<uint16_t> v;
    int city;
    own_selection(v, city);
    if (v.empty() && city < 0)
        return 0;
    net::Writer w;
    w.u8(CMD_GIVE);
    w.u8(v.empty() ? 1 : 0);
    w.u8(to);
    if (v.empty()) {
        w.u8(1);
        w.u16((uint16_t)city);
    } else {
        w.u8((uint8_t)std::min<size_t>(v.size(), 64));
        for (size_t i = 0; i < v.size() && i < 64; i++)
            w.u16(v[i]);
    }
    if (C.sess->role() == net::Role::Host) {
        net::Reader r(w.data.data(), w.data.size());
        host_command(C.sess->my_id(), r);
    } else
        send_cmd(w);
    if (v.empty())
        g_ui_city = nullptr;
    return v.empty() ? 1 : (int)v.size();
}

} // namespace civ
