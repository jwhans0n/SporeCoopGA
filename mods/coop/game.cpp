#include "game.h"

#include <algorithm>
#include <vector>

#include "sporekit_game.hpp"

using namespace skg;

namespace game {

static const SkHost* H;
static SkMod* M;
static bool g_ready;

static uintptr_t a_NounGet, a_DestroyInstance, a_CreatureCreate, a_NPCTickAI, a_ModeMgrGet;
static uintptr_t a_SpeciesMgrGet, a_GetSpecies, a_CreateHerd;

typedef void(SK_CDECL* AddEvo_t)(float);
static AddEvo_t o_AddEvo;
static DnaListener g_dna_listener;

static void SK_CDECL h_AddEvo(float delta)
{
    if (g_dna_listener && !g_dna_listener(delta))
        return;
    o_AddEvo(delta);
}

static std::vector<void*> g_puppets;

// ---- registry via cCreatureAnimal::Update (thiscall, int deltaTime) ----
typedef void(SK_THISCALL* Update_t)(void*, int);
static Update_t o_Update;
static std::vector<void*> g_seen, g_seen_out;
static UpdateFilter g_update_filter;
static UpdateFilter g_extra_update_filter;
static UpdateFilter g_npc_ai_filter;
static CreatureUpdateCounts g_creature_update_counts;
static BlockedUpdateHandler g_blocked_update_handler;
static UpdateObserver g_update_observer;

static void SK_THISCALL h_Update(void* self, int dt)
{
    g_seen.push_back(self);
    if (g_extra_update_filter && !g_extra_update_filter(self)) {
        if (g_blocked_update_handler) g_blocked_update_handler(self, dt);
        return;
    }
    if (g_update_filter && !g_update_filter(self)) {
        if (g_blocked_update_handler) g_blocked_update_handler(self, dt);
        return;
    }
    if (g_update_observer) g_update_observer(self, true);
    ++g_creature_update_counts.native;
    o_Update(self, dt);
    if (g_update_observer) g_update_observer(self, false);
}

const std::vector<void*>& creatures_updated()
{
    g_seen_out.swap(g_seen);
    g_seen.clear();
    std::sort(g_seen_out.begin(), g_seen_out.end());
    g_seen_out.erase(std::unique(g_seen_out.begin(), g_seen_out.end()), g_seen_out.end());
    return g_seen_out;
}

void discard_creature_updates()
{
    g_seen.clear();
    g_seen_out.clear();
}

void set_update_filter(UpdateFilter fn) { g_update_filter = fn; }
void set_extra_update_filter(UpdateFilter fn) { g_extra_update_filter = fn; }
void set_npc_ai_filter(UpdateFilter fn) { g_npc_ai_filter = fn; }
CreatureUpdateCounts creature_update_counts() { return g_creature_update_counts; }
void set_blocked_update_handler(BlockedUpdateHandler fn) { g_blocked_update_handler = fn; }
void set_update_observer(UpdateObserver fn) { g_update_observer = fn; }
bool is_destroyed(void* c) { return field<uint8_t>(c, 0x21) != 0; } // cGameData::mbIsDestroyed

// ---- combat via cCreatureAnimal TakeDamage (cCombatant vtable[6], this = creature+0x5A8) ----
typedef int(SK_THISCALL* TakeDamage_t)(void*, float, uint32_t, int, const void*, void*);
static TakeDamage_t o_TakeDamage;
static DamageFilter g_damage_filter;

static int SK_THISCALL h_TakeDamage(void* combatant, float dmg, uint32_t pol, int type, const void* pos, void* attacker)
{
    void* self = (uint8_t*)combatant - kCombatOff;
    // Attackers can be creatures, vehicles or other combatants. Their native virtual
    // owner conversion handles the different subobject offsets.
    void* att = attacker ? vcall<void*>(attacker, 0x0C / 4) : nullptr;
    if (g_damage_filter && !g_damage_filter(self, dmg, type, att))
        return 0;
    return o_TakeDamage(combatant, dmg, pol, type, pos, attacker);
}

void set_damage_filter(DamageFilter fn) { g_damage_filter = fn; }

void apply_damage(void* c, float dmg, int type, void* attacker)
{
    if (!o_TakeDamage)
        return;
    // the vector is the hit's push (knockback): the game passes (0,0,0) for bites and strikes.
    // It used to be the creature's position (|v| ~ 505): every forwarded hit threw the creature,
    // a kill ~80 m up (play_creature.py 2026-10-23, docs/re/creature.md 3)
    Vector3 push{};
    o_TakeDamage((uint8_t*)c + kCombatOff, dmg, attacker ? political_id(attacker) : 0xFFFFFFFFu, type, &push,
                 attacker ? (uint8_t*)attacker + kCombatOff : nullptr);
}

void set_health(void* c, float hp) { field<float>(c, kHpOff) = hp; }

// ---------------- any game object ----------------
void* noun_manager() { return a_NounGet ? cdecl_call<void*>(a_NounGet) : nullptr; }

static bool in_exe(const void* p)
{
    uintptr_t v = (uintptr_t)p, b = H->image_base();
    return v > b && v < b + 0x2000000;
}

void for_each_object(bool (*fn)(void* obj, uint32_t noun, void* user), void* user)
{
    uint8_t* nm = (uint8_t*)noun_manager();
    if (!nm)
        return;
    uint8_t* anchor = nm + 0x78;
    uint8_t* n = *(uint8_t**)anchor;
    for (int i = 0; n && n != anchor && i < 200000; i++) {
        uint8_t* next = *(uint8_t**)n;
        void* obj = n - 0xC;
        if (!IsBadReadPtr(obj, 0x30) && in_exe(*(void**)obj) && !fn(obj, noun_id(obj), user))
            return;
        n = next;
    }
}

uint32_t noun_id(void* obj) { return obj ? vcall<uint32_t>(obj, 0x20 / 4) : 0; }
void* cast(void* obj, uint32_t type) { return obj ? vcall<void*>(obj, 0x0C / 4, type) : nullptr; }
bool object_alive(void* obj) { return obj && !field<bool>(obj, 0x21); }

uint32_t g_ufos_freed;

bool ufo_listed(void* u)
{
    // the game frees UFOs on its own (DestroyNPCUFOs 0x1009310 on every context switch, also our
    // Defender puppets and mirrors): only a pointer still in the UFO noun list may be touched
    // (docs/re/space.md 1). Reads only the list, never the object.
    static uintptr_t get = H ? H->sym("Simulator::GetAllUFOs") : 0;
    void* nm = noun_manager();
    void* v = u && get && nm ? thiscall<void*>(get, nm) : nullptr;
    if (!v)
        return false;
    void** b = field<void**>(v, 0);
    void** e = field<void**>(v, 4);
    for (void** it = b; it && it < e; ++it)
        if (*it == u)
            return true;
    return false;
}

bool object_position(void* obj, net::Vec3& out)
{
    void* sp = cast(obj, 0x1186577);
    if (!sp)
        return false;
    auto& v = field<Vector3>(sp, 4);
    out = {v.x, v.y, v.z};
    return true;
}

void* find_object(uint32_t noun, const net::Vec3& pos, float max_dist)
{
    struct Q {
        uint32_t noun;
        net::Vec3 pos;
        float best;
        void* obj;
    } q{noun, pos, max_dist * max_dist, nullptr};
    for_each_object(
        [](void* o, uint32_t n, void* u) {
            auto* q = (Q*)u;
            net::Vec3 p;
            if (n == q->noun && object_alive(o) && object_position(o, p)) {
                float dx = p.x - q->pos.x, dy = p.y - q->pos.y, dz = p.z - q->pos.z, d = dx * dx + dy * dy + dz * dz;
                if (d < q->best) {
                    q->best = d;
                    q->obj = o;
                }
            }
            return true;
        },
        &q);
    return q.obj;
}

void hit(void* c, float dmg, int type, void* attacker)
{
    Vector3 push{}; // no knockback, like a bite (see apply_damage)
    h_TakeDamage((uint8_t*)c + kCombatOff, dmg, attacker ? political_id(attacker) : 0xFFFFFFFFu, type, &push,
                 attacker ? (uint8_t*)attacker + kCombatOff : nullptr);
}
static uintptr_t a_PlayAnim, a_PlayAnimTo;
static AnimListener g_anim_listener;
static bool g_in_puppet_anim;

typedef int(SK_THISCALL* PlayAnim_t)(void*, uint32_t, int, int);
typedef int(SK_THISCALL* PlayAnimTo_t)(void*, uint32_t, void*, int, int);
static PlayAnim_t o_PlayAnim;
static PlayAnimTo_t o_PlayAnimTo;

static int SK_THISCALL h_PlayAnim(void* self, uint32_t id, int block, int disable)
{
    if (g_anim_listener && !g_in_puppet_anim)
        g_anim_listener(self, id, block, (disable & 0xFF) != 0, false);
    return o_PlayAnim(self, id, block, disable);
}

static int SK_THISCALL h_PlayAnimTo(void* self, uint32_t id, void* other, int a, int block)
{
    if (g_anim_listener && !g_in_puppet_anim)
        g_anim_listener(self, id, block, false, true);
    return o_PlayAnimTo(self, id, other, a, block);
}

void set_anim_listener(AnimListener fn) { g_anim_listener = fn; }

void play_animation(void* c, uint32_t anim_id, int block, bool disable_blend)
{
    g_in_puppet_anim = true;
    o_PlayAnim(c, anim_id, block, disable_blend ? 1 : 0);
    g_in_puppet_anim = false;
}

typedef void(SK_THISCALL* NPCTickAI_t)(void*, float);
static NPCTickAI_t o_NPCTickAI;
static thread_local void* g_npc_ai_subject;

void* npc_ai_subject() { return g_npc_ai_subject; }

static void SK_THISCALL h_NPCTickAI(void* self, float dt)
{
    if (is_puppet(self))
        return; // remote players drive their puppets, no local AI
    if (g_npc_ai_filter && !g_npc_ai_filter(self)) {
        ++g_creature_update_counts.npc_ai_suppressed;
        return;
    }
    struct SubjectScope {
        void* previous;
        explicit SubjectScope(void* subject) : previous(g_npc_ai_subject) { g_npc_ai_subject = subject; }
        ~SubjectScope() { g_npc_ai_subject = previous; }
    } scope(self);
    o_NPCTickAI(self, dt);
}

bool init(const SkHost* host, SkMod* mod)
{
    H = host;
    M = mod;
    static const char* req[] = {
        "Simulator::cGameNounManager::Get",
        "Simulator::cGameNounManager::DestroyInstance",
        "Simulator::cCreatureAnimal::Create",
        "Simulator::cCreatureAnimal::NPCTickAI",
        "App::IGameModeManager::Get",
    };
    if (H->sym_require(M, req, 5) != 0) {
        g_ready = false;
        return false;
    }
    a_NounGet = H->sym(req[0]);
    a_DestroyInstance = H->sym(req[1]);
    a_CreatureCreate = H->sym(req[2]);
    a_NPCTickAI = H->sym(req[3]);
    a_ModeMgrGet = H->sym(req[4]);
    if (H->hook_create(M, (void*)a_NPCTickAI, (void*)h_NPCTickAI, (void**)&o_NPCTickAI) != 0)
        return false;
    if (uintptr_t a = H->sym("Simulator::cCreatureAnimal::Update"))
        H->hook_create(M, (void*)a, (void*)h_Update, (void**)&o_Update);
    if (uintptr_t a = H->sym("Simulator::cCreatureAnimal::TakeDamage"))
        H->hook_create(M, (void*)a, (void*)h_TakeDamage, (void**)&o_TakeDamage);
    if (uintptr_t a = H->sym("Simulator::cCreatureGameData::AddEvolutionPoints"))
        H->hook_create(M, (void*)a, (void*)h_AddEvo, (void**)&o_AddEvo);
    a_SpeciesMgrGet = H->sym("Editors::cSpeciesManager::Get");
    a_GetSpecies = H->sym("Editors::cSpeciesManager::GetSpeciesProfile");
    a_CreateHerd = H->sym("Simulator::cGameNounManager::CreateHerd");
    a_PlayAnim = H->sym("Simulator::cCreatureBase::PlayAnimation");
    a_PlayAnimTo = H->sym("Simulator::cCreatureBase::PlayAnimationTo");
    if (a_PlayAnim)
        H->hook_create(M, (void*)a_PlayAnim, (void*)h_PlayAnim, (void**)&o_PlayAnim);
    if (a_PlayAnimTo)
        H->hook_create(M, (void*)a_PlayAnimTo, (void*)h_PlayAnimTo, (void**)&o_PlayAnimTo);
    g_ready = true;
    return true;
}

bool ready() { return g_ready; }

uint32_t mode_id()
{
    if (!g_ready)
        return 0;
    void* mm = cdecl_call<void*>(a_ModeMgrGet);
    return mm ? vcall<uint32_t>(mm, 0x38 / 4) : 0;
}

bool in_creature_stage() { return mode_id() == 0x1654C01; }

void* avatar()
{
    if (!g_ready)
        return nullptr;
    uint32_t m = mode_id();
    if (m != 0x1654C01 && m != 0x1654C10) // creature stage, adventures (the captain)
        return nullptr;
    void* nm = cdecl_call<void*>(a_NounGet);
    if (!nm)
        return nullptr;
    return field<void*>(nm, kNounAvatarOff);
}

net::Vec3 position(void* c)
{
    auto& v = field<Vector3>(c, kPosOff);
    return {v.x, v.y, v.z};
}

net::Quat orientation(void* c)
{
    auto& q = field<Quaternion>(c, kRotOff);
    return {q.x, q.y, q.z, q.w};
}

net::Vec3 velocity(void* c)
{
    auto& v = field<Vector3>(c, kVelOff);
    return {v.x, v.y, v.z};
}

float health(void* c) { return field<float>(c, kHpOff); }
// cCombatant::GetMaxHitPoints (vtable 0x58); the +0x2C field holds 1.0 for avatars in this build
float max_health(void* c)
{
    float m = c ? skg::vcall<float>((uint8_t*)c + kCombatOff, 0x58 / 4) : 0.f;
    return m > 0 ? m : field<float>(c, kMaxHpOff);
}
bool is_dead(void* c) { return field<uint8_t>(c, kDeadOff) != 0; }
uint32_t species_instance(void* c) { return field<uint32_t>(c, kSpeciesKeyOff); }
void* species_profile(void* c) { return field<void*>(c, kSpeciesOff); }

void* avatar_herd()
{
    if (!g_ready || !avatar())
        return nullptr;
    void* nm = cdecl_call<void*>(a_NounGet);
    return nm ? field<void*>(nm, kNounAvatarHerdOff) : nullptr;
}

void* spawn_creature(const net::Vec3& pos, void* species, void* herd)
{
    if (!g_ready || !species || !herd)
        return nullptr;
    Vector3 p{pos.x, pos.y, pos.z};
    // static cCreatureAnimal* Create(const Vector3&, cSpeciesProfile*, int age,
    //                                cHerd*, bool isAvatar, bool reuse)   (cdecl)
    return cdecl_call<void*>(a_CreatureCreate, &p, species, 1, herd, 0, 0);
}

void destroy_creature(void* c)
{
    if (!g_ready || !c)
        return;
    puppet_unregister(c); // also used for UFOs and other cGameData puppets
    void* nm = cdecl_call<void*>(a_NounGet);
    if (nm)
        thiscall<void>(a_DestroyInstance, nm, c);
}

static void* spatial(void* c) { return ptr_add<void>(c, kSpatialOff); }

void teleport(void* c, const net::Vec3& pos, const net::Quat& rot)
{
    Vector3 p{pos.x, pos.y, pos.z};
    Quaternion q{rot.x, rot.y, rot.z, rot.w};
    vcall<void>(spatial(c), 0x44 / 4, &p, &q);
}

void set_position(void* c, const net::Vec3& pos)
{
    Vector3 p{pos.x, pos.y, pos.z};
    vcall<void>(spatial(c), 0x38 / 4, &p);
}

void set_orientation(void* c, const net::Quat& rot)
{
    Quaternion q{rot.x, rot.y, rot.z, rot.w};
    vcall<void>(spatial(c), 0x3C / 4, &q);
}

void move_to(void* c, const net::Vec3& dst, float stop_dist)
{
    Vector3 p{dst.x, dst.y, dst.z};
    // cLocomotiveObject::MoveTo(const Vector3&, float goalStop, float acceptableStop, bool)
    vcall<void>(spatial(c), 0xE0 / 4, &p, stop_dist, stop_dist * 2.f, 0);
}

void stop(void* c) { vcall<void>(spatial(c), 0xEC / 4); }

void set_speed(void* c, float speed) { vcall<void>(spatial(c), 0xC4 / 4, speed, 0); }
// dc04 cLocomotiveObject::GetStandardSpeed, verified spatial vtable +CC.
float standard_speed(void* c) { return vcall<float>(spatial(c), 0xCC / 4); }

float scale(void* c) { return vcall<float>(spatial(c), 0x34 / 4); }       // GetScale
float bounding_radius(void* c) { return vcall<float>(spatial(c), 0x70 / 4); } // GetBoundingRadius
void set_scale(void* c, float s) { vcall<void>(spatial(c), 0x40 / 4, s); } // SetScale

Key species_key(void* c)
{
    auto& k = field<ResourceKey>(c, kSpeciesKeyOff);
    return {k.instance, k.type, k.group};
}

void* species_by_key(const Key& k)
{
    if (!a_SpeciesMgrGet || !a_GetSpecies)
        return nullptr;
    void* mgr = cdecl_call<void*>(a_SpeciesMgrGet);
    ResourceKey rk{k.instance, k.type, k.group};
    return mgr ? thiscall<void*>(a_GetSpecies, mgr, &rk) : nullptr;
}

void* create_herd(const net::Vec3& pos, void* species)
{
    if (!a_CreateHerd || !species)
        return nullptr;
    void* nm = cdecl_call<void*>(a_NounGet);
    Vector3 p{pos.x, pos.y, pos.z};
    // CreateHerd(const Vector3&, cSpeciesProfile*, herdSize, isOwnedByAvatar, personality, createNest)
    void* h = nm ? thiscall<void*>(a_CreateHerd, nm, &p, species, 0, 0, 0, 0) : nullptr;
    herd_quiet(h);
    return h;
}

// A herd of our own (puppets, NPC proxies) must not grow: CreateHerd gives it a target size of 5
// and it was enabled, so the game filled it with 4 clones of the guest's creature on the host
// (real NPCs, mirrored to the guest too; play_creature.py 2026-10-23, docs/re/creature.md 6).
void herd_quiet(void* h)
{
    if (!h || !skg::readable(h, 0x21C))
        return;
    field<int32_t>(h, 0xF4) = 0; // mTargetHerdSize
    static uintptr_t enable = H->sym("Simulator::cHerd::SetEnabled");
    if (enable && field<uint8_t>(h, 0x218))
        thiscall<void>(enable, h, false); // mbEnabled
}

void* herd_of(void* c) { return field<void*>(c, kHerdOff); }

uint32_t political_id(void* c) { return vcall<uint32_t>(c, 0x4C / 4); } // cGameData::GetPoliticalID

float dna()
{
    uintptr_t get = H->sym("Simulator::cCreatureGameData::GetEvolutionPoints");
    return get ? cdecl_call<float>(get) : 0.f;
}

void dna_add_raw(float delta)
{
    if (o_AddEvo)
        o_AddEvo(delta);
}

void set_dna_listener(DnaListener fn) { g_dna_listener = fn; }

static void* tod_object()
{
    uintptr_t un = H->sym("SporeCoop::TimeOfDay_Unavailable"), get = H->sym("SporeCoop::TimeOfDay_GetObject");
    if (!un || !get || cdecl_call<int>(un) != 0)
        return nullptr;
    return cdecl_call<void*>(get);
}

bool time_of_day(net::Quat& q)
{
    void* o = tod_object();
    if (!o)
        return false;
    auto& r = field<Quaternion>(o, 0x10); // cSpatialObject::mOrientation
    q = {r.x, r.y, r.z, r.w};
    return true;
}

void set_time_of_day(const net::Quat& q)
{
    void* o = tod_object();
    uintptr_t apply = H->sym("SporeCoop::TimeOfDay_Apply");
    if (!o || !apply)
        return;
    Quaternion r{q.x, q.y, q.z, q.w};
    vcall<void>(o, 0x3C / 4, &r); // SetOrientation
    cdecl_call<void>(apply, o);
}

void puppet_register(void* c)
{
    if (!is_puppet(c))
        g_puppets.push_back(c);
}

void puppet_unregister(void* c)
{
    g_puppets.erase(std::remove(g_puppets.begin(), g_puppets.end(), c), g_puppets.end());
}

bool is_puppet(void* c) { return std::find(g_puppets.begin(), g_puppets.end(), c) != g_puppets.end(); }
const std::vector<void*>& puppets() { return g_puppets; }

} // namespace game
