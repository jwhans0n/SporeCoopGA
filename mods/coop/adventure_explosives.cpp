#include "adventure_explosives.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <deque>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <utility>

#include "sporekit_game.hpp"

namespace adventure_explosives {
static const SkHost* H;
static Ctx C;
static bool g_ok, g_visual, g_applying, g_overflow, g_overflow_reported, g_invalid_effect;
static unsigned g_damage_depth;
static uint64_t g_run, g_generation, g_serial;
static uint8_t g_act;
static uintptr_t a_Physics;
using Pair = void(SK_CDECL*)(void*,void*);
using Single = void(SK_CDECL*)(void*);
using Timers = void(SK_CDECL*)(float);
using Radius = void(SK_CDECL*)(void*,void*,float,float,float);
using Impulse = void(SK_THISCALL*)(void*,void*,const net::Vec3*,bool);
using Schedule = void(SK_CDECL*)(void*,void*,float);
struct Quat { float x=0,y=0,z=0,w=1; };
using Effect = void(SK_CDECL*)(const net::Vec3*,const Quat*,float,uint32_t);
static Pair o_Mine,o_Contact,o_Arm,o_Explode;
static Single o_Bomb,o_BombExpiry,o_ExplosionExpiry;
static Schedule o_Schedule;
static Timers o_Timers,o_Powerups;
static Radius o_Radius,o_UnownedRadius;
static Impulse o_Impulse;
static Effect o_Effect;
enum Kind : uint8_t { Activate=144,Triggered=145,Detonated=146,Arm=147,Armed=148,Throw=149,Thrown=150 };
enum class Phase : uint8_t { Available,Triggered,Retired };
struct State {
    Phase phase=Phase::Available;
    bool armed=false, thrown=false;
    uint8_t owner=0xFF;
    uint64_t revision=0;
};
static std::map<Identity,State> g_states;
// Native creation can invoke its fuse callback before scenario marker binding.
// The incarnation comes from the live callback object, never a retained pointer.
static std::set<std::pair<void*,uint32_t>> g_native_fuses;
static std::map<std::pair<void*,uint32_t>,uint64_t> g_owned_callbacks;
static std::map<std::pair<uint8_t,Identity>,double> g_requests;
struct Message {
    uint8_t kind=0,player=0xFF,type=0,act=0;
    uint64_t run=0,serial=0,generation=0;
    Identity id;
    net::Vec3 impulse;
    net::Vec3 position;
    Quat orientation;
    float scale=1;
    uint32_t effect=0;
};
// F24730 supplies the exact authored presentation before applying blast damage.
// Capture those arguments rather than guessing a palette or class layout.
struct EffectCapture { Message* event; bool sent=false; };
static EffectCapture* g_capture;
static const Message* g_guest_effect;
struct Pending { Message message; uint8_t peer=0; };
static std::deque<Pending> g_outgoing;
static std::deque<Message> g_waiting;
static std::map<uint8_t,uint64_t> g_last_request;
static size_t g_activated,g_detonated,g_throws,g_rejected;

static bool host() { return C.sess && C.sess->role()==net::Role::Host; }
static bool active() { return g_ok && C.sess && C.sess->connected() && C.active && C.active() && C.run && C.run(); }
static bool held() { return C.held && C.held(); }
static void sync_scope()
{
    if(g_run!=C.run()) { reset(); g_run=C.run(); g_act=C.act(); }
    if(g_act!=C.act()) {
        auto waiting=std::move(g_waiting); auto fuses=std::move(g_native_fuses);
        reset(); g_native_fuses=std::move(fuses); g_run=C.run(); g_act=C.act();
        for(auto& m:waiting) if(m.run==g_run && m.act>=g_act) { m.generation=g_generation; g_waiting.push_back(m); }
    }
}
static bool explosive(int type) { return type==6 || type==10 || type==13 || type==15; }
static void put64(net::Writer& w,uint64_t v) { w.u32(uint32_t(v)); w.u32(uint32_t(v>>32)); }
static uint64_t get64(net::Reader& r) { uint64_t v=r.u32(); return v|uint64_t(r.u32())<<32; }
static bool finite(net::Vec3 p) { return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z); }
static bool valid_effect(const Message& m)
{
    float q=m.orientation.x*m.orientation.x+m.orientation.y*m.orientation.y+
            m.orientation.z*m.orientation.z+m.orientation.w*m.orientation.w;
    return finite(m.position) && std::fabs(m.position.x)<=1.e7f && std::fabs(m.position.y)<=1.e7f &&
        std::fabs(m.position.z)<=1.e7f && std::isfinite(q) && q>=0.0001f && q<=10000.f &&
        std::isfinite(m.scale) && m.scale>=0 && m.scale<=1.e6f;
}
static bool touching(void* captain,void* item)
{
    net::Vec3 a,b,c,d;
    if(!C.bounds(captain,a,b) || !C.bounds(item,c,d) || !finite(a) || !finite(b) || !finite(c) || !finite(d) ||
        a.x>b.x || a.y>b.y || a.z>b.z || c.x>d.x || c.y>d.y || c.z>d.z) return false;
    auto length=[](net::Vec3 p) { return std::sqrt(p.x*p.x+p.y*p.y+p.z*p.z); };
    float ra=0.5f*length({b.x-a.x,b.y-a.y,b.z-a.z});
    float rb=0.5f*length({d.x-c.x,d.y-c.y,d.z-c.z});
    net::Vec3 delta{0.5f*(a.x+b.x-c.x-d.x),0.5f*(a.y+b.y-c.y-d.y),0.5f*(a.z+b.z-c.z-d.z)};
    // A small tolerance covers host movement sampling between native contact
    // and reliable arrival. Authored size is derived entirely from native bounds.
    return std::isfinite(ra) && std::isfinite(rb) && length(delta)<=ra+rb+0.75f;
}
static void* object(void* powerup) { return powerup && skg::readable(powerup,0x3C) ? skg::vcall<void*>(powerup,1) : nullptr; }
static int type(void* powerup) { return powerup && skg::readable(powerup,0x3C) ? skg::field<int>(powerup,4) : 0; }
static bool identify(void* p,Identity& id) { auto* o=object(p); return o && C.identify && C.identify(o,id); }
static Message message(uint8_t kind,Identity id,uint8_t player,uint8_t type)
{
    Message m; m.kind=kind; m.id=id; m.player=player; m.type=type;
    m.run=C.run(); m.act=C.act(); m.generation=g_generation; m.serial=++g_serial; return m;
}
static net::Writer encode(const Message& m)
{
    net::Writer w; w.u8(m.kind); put64(w,m.run); w.u8(m.act); put64(w,m.serial);
    w.u32(m.id.marker); w.u32(m.id.incarnation); w.u8(m.player); w.u8(m.type);
    if(m.kind==Throw || m.kind==Thrown) { w.f32(m.impulse.x); w.f32(m.impulse.y); w.f32(m.impulse.z); }
    if(m.kind==Detonated) {
        w.f32(m.position.x); w.f32(m.position.y); w.f32(m.position.z);
        w.f32(m.orientation.x); w.f32(m.orientation.y); w.f32(m.orientation.z); w.f32(m.orientation.w);
        w.f32(m.scale); w.u32(m.effect);
    }
    return w;
}
static void publish(Message m)
{
    // Interactions are sparse; stop accepting new requests rather than allowing
    // native mutations to outrun the bounded reliable event queue.
    auto queue=[&](uint8_t peer) {
        // A later final state subsumes pending earlier states of that identity.
        // Keep request ordering (arm, drop, throw) intact on the client.
        if(host() && m.kind==Detonated) {
            for(auto it=g_outgoing.begin();it!=g_outgoing.end();) {
                if(it->peer==peer && !(it->message.id<m.id) && !(m.id<it->message.id)) it=g_outgoing.erase(it);
                else ++it;
            }
        }
        if(g_outgoing.size()>=8192) { g_overflow=true; return; }
        g_outgoing.push_back({m,peer});
    };
    if(host()) { for(const auto& p:C.sess->players()) if(p.id) queue(p.id); }
    else queue(0);
}
static bool room() { return g_outgoing.size()<384; }
static void SK_CDECL effect(const net::Vec3* position,const Quat* orientation,float scale,uint32_t id)
{
    if(g_capture && !g_capture->sent && position && orientation) {
        auto& m=*g_capture->event;
        m.position=*position; m.orientation=*orientation; m.scale=scale; m.effect=id;
        if(valid_effect(m)) { publish(m); g_capture->sent=true; }
    }
    if(g_visual && g_guest_effect) {
        const auto& m=*g_guest_effect;
        o_Effect(&m.position,&m.orientation,m.scale,m.effect);
    } else o_Effect(position,orientation,scale,id);
}
static void own_callback(void* powerup)
{
    void* native=object(powerup);
    if(native && skg::readable(native,0x28))
        g_owned_callbacks[{native,skg::field<uint32_t>(native,0x24)}]=C.run();
}
static bool stale_callback(void* task)
{
    if(!task || !skg::readable(task,8)) return false;
    void* native=skg::field<void*>(task,4);
    if(!native || !skg::readable(native,0x28)) return false;
    auto it=g_owned_callbacks.find({native,skg::field<uint32_t>(native,0x24)});
    if(it==g_owned_callbacks.end()) return false;
    bool stale=!active() || it->second!=C.run();
    g_owned_callbacks.erase(it); return stale;
}
static void SK_CDECL bomb_expiry(void* task) { if(!stale_callback(task)) o_BombExpiry(task); }
static void SK_CDECL explosion_expiry(void* task) { if(!stale_callback(task)) o_ExplosionExpiry(task); }
static void SK_CDECL schedule(void* powerup,void* attacker,float delay)
{
    if(active() && delay>0) own_callback(powerup);
    o_Schedule(powerup,attacker,delay);
}
static bool connected(uint8_t player)
{
    for(const auto& p:C.sess->players()) if(p.id==player) return true;
    return false;
}
static uint8_t owner_of_combatant(void* combatant)
{
    if(!combatant || !skg::readable(combatant,sizeof(void*))) return 0xFF;
    return C.owner(skg::vcall<void*>(combatant,3));
}
struct DamageScope {
    DamageScope() { ++g_damage_depth; }
    ~DamageScope() { --g_damage_depth; }
};
struct ApplyScope {
    bool old;
    ApplyScope():old(g_applying) { g_applying=true; }
    ~ApplyScope() { g_applying=old; }
};
static void activate(void* powerup,void* actor,bool grenade)
{
    if(!active()) { (grenade?o_Contact:o_Mine)(powerup,actor); return; }
    sync_scope();
    if(held() || g_visual || !room()) return;
    int native_type=type(powerup);
    if(native_type!=(grenade?13:6)) return;
    Identity id;
    if(!identify(powerup,id)) return;
    uint8_t player=grenade?C.owner(actor):owner_of_combatant(actor);
    if(!host()) {
        if(player!=C.sess->my_id() || !C.can_act(player)) return;
        auto key=std::make_pair(player,id); double now=H->time_seconds();
        if(g_requests.count(key) && now-g_requests[key]<0.25) return;
        g_requests[key]=now; publish(message(Activate,id,player,uint8_t(native_type))); return;
    }
    if(player!=0xFF && !C.can_act(player)) return;
    auto& state=g_states[id];
    if(state.phase!=Phase::Available) return;
    if(grenade) {
        // The native function owns the armed check and the contact-reference
        // grace period. Do not claim the incarnation until native accepts it.
        if(!skg::field<bool>(powerup,0x30)) return;
        if(skg::field<void*>(powerup,0x38)==actor && skg::field<float>(powerup,0x34)<1.f) return;
    }
    state.phase=Phase::Triggered;
    auto event=message(Triggered,id,player,uint8_t(native_type)); state.revision=event.serial; publish(event);
    ++g_activated;
    DamageScope damage;
    (grenade?o_Contact:o_Mine)(powerup,actor);
}
static void SK_CDECL mine(void* p,void* actor) { activate(p,actor,false); }
static void SK_CDECL contact(void* p,void* actor) { activate(p,actor,true); }

static void SK_CDECL explode(void* powerup,void* attacker)
{
    if(!active()) { o_Explode(powerup,attacker); return; }
    sync_scope();
    if(g_visual) { o_Explode(powerup,attacker); return; }
    // Guests never execute native explosion damage or schedule their own chains.
    if(!host() || held() || !explosive(type(powerup))) return;
    Identity id;
    if(!identify(powerup,id)) return;
    auto& state=g_states[id];
    if(state.phase==Phase::Retired) return;
    state.phase=Phase::Retired;
    auto event=message(Detonated,id,0xFF,uint8_t(type(powerup))); state.revision=event.serial;
    ++g_detonated;
    EffectCapture capture{&event}; auto* previous=g_capture; g_capture=&capture;
    DamageScope damage;
    o_Explode(powerup,attacker);
    g_capture=previous;
    // The verified native implementation always invokes its presentation helper.
    // A malformed native pose cannot be replicated safely; cancel through the
    // existing deferred failure path instead of leaving peers in divergent state.
    if(!capture.sent) g_invalid_effect=true;
}
static void SK_CDECL radius(void* source,void* attacker,float r,float damage,float force)
{
    if(active() && (!host() || held() || g_visual)) return;
    o_Radius(source,attacker,r,damage,force);
}
static void SK_CDECL unowned_radius(void* source,void* position,float r,float damage,float force)
{
    if(active() && (!host() || held() || g_visual)) return;
    o_UnownedRadius(source,position,r,damage,force);
}
static void SK_CDECL timers(float dt)
{
    // Native timer elapsed and grenade in-flight age are simulation-time state.
    if(active() && held()) return;
    o_Timers(dt);
}
static void SK_CDECL powerups(float dt)
{
    // Service initial respawner children even during loading/beam-down: native
    // Kill target setup resolves parent9 to its physical child and skips null.
    // Zero simulation time preserves grenade grace and authored respawn clocks;
    // interaction and explosion authority remain held-gated independently.
    o_Powerups(active() && held() ? 0.f : dt);
}
static void SK_CDECL bomb(void* powerup)
{
    if(!active()) { o_Bomb(powerup); return; }
    sync_scope();
    if(!host() || type(powerup)!=10) return;
    // Native attach/bounce callbacks can repeat. Preserve a running fuse rather
    // than inserting a second timer for the same physical incarnation.
    void* native=object(powerup);
    if(!native || !skg::readable(native,0x28)) return;
    bool first=g_native_fuses.insert({native,skg::field<uint32_t>(native,0x24)}).second;
    own_callback(powerup);
    Identity id;
    if(!identify(powerup,id)) { if(first) o_Bomb(powerup); return; }
    auto& state=g_states[id]; if(state.armed || state.phase==Phase::Retired) return;
    state.armed=true; state.phase=Phase::Triggered;
    auto event=message(Triggered,id,0xFF,10); state.revision=event.serial; publish(event);
    if(first) o_Bomb(powerup);
}
static void SK_CDECL arm(void* powerup,void* contact_reference)
{
    if(!active()) { o_Arm(powerup,contact_reference); return; }
    sync_scope();
    if(g_applying) { o_Arm(powerup,contact_reference); return; }
    if(held() || !room() || type(powerup)!=13) return;
    Identity id; if(!identify(powerup,id)) return;
    uint8_t player=C.owner(contact_reference);
    if(player==0xFF || !C.can_act(player) || !C.owns_throw(player,object(powerup))) return;
    if(!host()) {
        if(player!=C.sess->my_id()) return;
        publish(message(Arm,id,player,13));
        // Prediction supplies only the native grace fields. Guest contact and
        // blast authority remain disabled until the host accepts the throw.
        o_Arm(powerup,contact_reference); return;
    }
    auto& state=g_states[id]; if(state.phase==Phase::Retired || state.armed) return;
    state.armed=true; state.owner=player;
    o_Arm(powerup,contact_reference);
    auto event=message(Armed,id,player,13); state.revision=event.serial; publish(event);
}
static void SK_THISCALL impulse(void* self,void* spatial,const net::Vec3* velocity,bool add)
{
    if(!active() || g_applying || g_damage_depth || g_visual || !spatial || !velocity) {
        o_Impulse(self,spatial,velocity,add); return;
    }
    void* item=C.from_spatial(spatial),*powerup=C.powerup(item);
    int native_type=type(powerup);
    if(native_type!=10 && native_type!=13) { o_Impulse(self,spatial,velocity,add); return; }
    sync_scope();
    if(held() || !room() || !finite(*velocity)) return;
    Identity id; if(!C.identify(item,id)) return;
    uint8_t player=0xFF;
    for(const auto& p:C.sess->players()) if(C.can_act(p.id) && C.owns_throw(p.id,item)) { player=p.id; break; }
    if(player==0xFF) { if(host()) o_Impulse(self,spatial,velocity,add); return; }
    auto& state=g_states[id]; if(state.phase==Phase::Retired || state.thrown) return;
    auto event=message(host()?Thrown:Throw,id,player,uint8_t(native_type)); event.impulse=*velocity;
    if(!host()) {
        if(player!=C.sess->my_id()) return;
        publish(event); o_Impulse(self,spatial,velocity,add); return;
    }
    state.thrown=true; state.owner=player; state.revision=event.serial;
    o_Impulse(self,spatial,velocity,add); publish(event); ++g_throws;
}

static void apply(void* argument)
{
    std::unique_ptr<Message> m(static_cast<Message*>(argument));
    if(!active() || m->generation!=g_generation || m->run!=C.run() || m->act<C.act()) return;
    if(held() || m->act>C.act()) {
        if(!host()) {
            for(auto& pending:g_waiting) if(pending.act==m->act && !(pending.id<m->id) && !(m->id<pending.id)) {
                if(m->serial>pending.serial) pending=*m;
                return;
            }
            if(g_waiting.size()>=8192) g_overflow=true;
            else g_waiting.push_back(*m);
        }
        return;
    }
    if(!host()) {
        auto previous=g_states.find(m->id);
        if(previous!=g_states.end() && (previous->second.phase==Phase::Retired || m->serial<=previous->second.revision)) return;
    }
    void* item=C.object(m->id),*powerup=C.powerup(item);
    if(!powerup || type(powerup)!=m->type || !explosive(m->type)) {
        if(!host() && m->kind==Detonated) {
            auto& state=g_states[m->id];
            state.phase=Phase::Retired; state.revision=m->serial; C.retire(m->id);
            // The native respawner may not have produced or bound this short-
            // lived incarnation locally. Show the host's actual effect once;
            // never destroy a same-marker sibling or a newer respawn by guess.
            g_visual=true; o_Effect(&m->position,&m->orientation,m->scale,m->effect); g_visual=false;
            ++g_detonated;
        } else ++g_rejected;
        return;
    }
    auto& state=g_states[m->id];
    if(host()) {
        if(!connected(m->player) || !C.can_act(m->player) || !C.captain(m->player)) { ++g_rejected; return; }
        if(m->kind==Activate) {
            // The sender cannot supply damage, radius, fuse or trigger reference.
            // A contact claim must be close to the physical object on the host.
            if((m->type!=6 && m->type!=13) || !touching(C.captain(m->player),item)) { ++g_rejected; return; }
            if(m->type==6) activate(powerup,C.combatant(C.captain(m->player)),false);
            else activate(powerup,C.captain(m->player),true);
        } else if(m->kind==Arm) {
            if(m->type!=13 || !C.owns_throw(m->player,item)) { ++g_rejected; return; }
            arm(powerup,C.captain(m->player));
        } else if(m->kind==Throw) {
            float speed2=m->impulse.x*m->impulse.x+m->impulse.y*m->impulse.y+m->impulse.z*m->impulse.z;
            if(!C.owns_throw(m->player,item) || !C.nearby(C.captain(m->player),item,25.f) ||
                !finite(m->impulse) || !std::isfinite(speed2) || speed2>250.f*250.f || state.thrown || state.phase==Phase::Retired) { ++g_rejected; return; }
            void* spatial=skg::vcall<void*>(powerup,2);
            void* physics=skg::cdecl_call<void*>(a_Physics);
            if(!spatial || !physics) return;
            if(m->type==13 && !state.armed) arm(powerup,C.captain(m->player));
            ApplyScope applying;
            o_Impulse(physics,spatial,&m->impulse,true);
            state.thrown=true; state.owner=m->player;
            auto event=message(Thrown,m->id,m->player,m->type); event.impulse=m->impulse;
            state.revision=event.serial; publish(event); ++g_throws;
        }
        return;
    }
    state.revision=m->serial;
    ApplyScope applying;
    if(m->kind==Detonated) {
        state.phase=Phase::Retired;
        // Retire before invoking native destruction so delayed snapshots cannot
        // recreate a consumed explosive. Native radius entry points are gated.
        C.retire(m->id); g_visual=true; g_guest_effect=m.get();
        o_Explode(powerup,nullptr);
        g_guest_effect=nullptr; g_visual=false; ++g_detonated;
    } else if(m->kind==Triggered) {
        state.phase=Phase::Triggered;
        // The native bomb helper supplies its fuse presentation. Its local
        // expiry cannot damage or detonate because both paths are host gated.
        if(m->type==10 && !state.armed) { state.armed=true; own_callback(powerup); o_Bomb(powerup); }
    }
    else if(m->kind==Armed && C.captain(m->player)) {
        state.armed=true; state.owner=m->player; o_Arm(powerup,C.captain(m->player));
    } else if(m->kind==Thrown && finite(m->impulse)) {
        // Held-event coalescing can replace Armed with this later state. The
        // throw carries its owner, so restore the native reference as well.
        if(m->type==13 && !state.armed && C.captain(m->player)) {
            state.armed=true; o_Arm(powerup,C.captain(m->player));
        }
        // The originating client already supplied this predicted impulse. Other
        // guests display host transforms and must not run another projectile.
        state.thrown=true; state.owner=m->player;
    }
}

bool init(const SkHost* host_api,SkMod* mod,Ctx context)
{
    H=host_api; C=std::move(context); g_ok=false; reset();
    constexpr const char* hash="dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37";
    if(!H || !H->sym || !H->hook_create || !H->run_outside_frame || !H->time_seconds || !C.sess || !C.run || !C.act || !C.active ||
        !H->exe_sha256 || !H->exe_sha256() || std::strcmp(H->exe_sha256(),hash) ||
        !C.identify || !C.object || !C.powerup || !C.combatant || !C.from_spatial || !C.captain || !C.owner ||
        !C.can_act || !C.owns_throw || !C.nearby || !C.bounds || !C.retire || !C.failed) return false;
    struct Hook { const char* symbol; void* detour; void** original; };
    const Hook hooks[]={
        {"SporeCoop::Adventure::ActivateMine",(void*)mine,(void**)&o_Mine},
        {"SporeCoop::Adventure::ActivateGrenade",(void*)contact,(void**)&o_Contact},
        {"SporeCoop::Adventure::ArmGrenade",(void*)arm,(void**)&o_Arm},
        {"SporeCoop::Adventure::StartBombFuse",(void*)bomb,(void**)&o_Bomb},
        {"SporeCoop::Adventure::BombFuseExpiry",(void*)bomb_expiry,(void**)&o_BombExpiry},
        {"SporeCoop::Adventure::ExplosionDelayExpiry",(void*)explosion_expiry,(void**)&o_ExplosionExpiry},
        {"SporeCoop::Adventure::SchedulePowerupExplosion",(void*)schedule,(void**)&o_Schedule},
        {"SporeCoop::Adventure::ExplodePowerup",(void*)explode,(void**)&o_Explode},
        {"SporeCoop::Adventure::ExplosionEffect",(void*)effect,(void**)&o_Effect},
        {"SporeCoop::Adventure::ExplosionRadius",(void*)radius,(void**)&o_Radius},
        {"SporeCoop::Adventure::ExplosionUnownedRadius",(void*)unowned_radius,(void**)&o_UnownedRadius},
        {"SporeCoop::Adventure::TickPowerupTimers",(void*)timers,(void**)&o_Timers},
        {"SporeCoop::Adventure::TickPowerups",(void*)powerups,(void**)&o_Powerups},
        {"Simulator::cPhysicsWorld::ApplyImpulse",(void*)impulse,(void**)&o_Impulse},
    };
    a_Physics=H->sym("Simulator::cPhysicsWorld::Get");
    if(!a_Physics) return false;
    for(const auto& h:hooks) if(!H->sym(h.symbol)) return false;
    for(const auto& h:hooks) if(H->hook_create(mod,(void*)H->sym(h.symbol),h.detour,h.original)!=0) return false;
    g_ok=true; return true;
}
bool available() { return g_ok; }
void picked(Identity id,uint8_t player)
{
    if(!active() || !connected(player)) return;
    sync_scope();
    void* native=C.object(id),*powerup=C.powerup(native);
    if(type(powerup)!=10) return;
    auto state=g_states.find(id);
    if(state==g_states.end() || state->second.phase==Phase::Retired) return;
    state->second.thrown=false;
    state->second.owner=player;
}
void reset()
{
    ++g_generation; g_run=0; g_serial=0; g_act=0;
    g_visual=g_applying=g_overflow=g_overflow_reported=g_invalid_effect=false;
    g_damage_depth=0; g_capture=nullptr; g_guest_effect=nullptr;
    g_states.clear(); g_native_fuses.clear(); g_requests.clear(); g_outgoing.clear(); g_waiting.clear(); g_last_request.clear();
    g_activated=g_detonated=g_throws=g_rejected=0;
}
void tick()
{
    if(!active()) { if(g_run) reset(); return; }
    sync_scope();
    if(g_overflow || g_invalid_effect) {
        if(!g_overflow_reported) {
            g_overflow_reported=true;
            C.failed(g_invalid_effect ? "Adventure explosive synchronization received an invalid native effect pose." :
                "Adventure explosive synchronization exceeded its reliable queue limit.");
        }
        return;
    }
    if(!held()) {
        auto waiting=std::move(g_waiting); g_waiting.clear();
        for(auto& m:waiting) H->run_outside_frame(apply,new Message(m));
    }
    unsigned sent=0;
    for(auto it=g_outgoing.begin();it!=g_outgoing.end() && sent<24;) {
        if(!connected(it->peer)) { it=g_outgoing.erase(it); continue; }
        if(C.sess->pending_to(it->peer)>=96) { ++it; continue; }
        auto w=encode(it->message);
        if(C.sess->send_reliable_to(it->peer,net::MSG_ADVENTURE,w)) { it=g_outgoing.erase(it); ++sent; }
        else ++it;
    }
}
void on_message(uint8_t from,net::Reader& r)
{
    if(!active()) return;
    // Do not discard an early host event solely because native SetAct is still queued.
    if(g_run!=C.run()) { reset(); g_run=C.run(); g_act=C.act(); }
    Message m; m.kind=r.u8(); m.run=get64(r); m.act=r.u8(); m.serial=get64(r);
    m.id={r.u32(),r.u32()}; m.player=r.u8(); m.type=r.u8();
    if(m.kind==Throw || m.kind==Thrown) m.impulse={r.f32(),r.f32(),r.f32()};
    if(m.kind==Detonated) {
        m.position={r.f32(),r.f32(),r.f32()};
        m.orientation={r.f32(),r.f32(),r.f32(),r.f32()}; m.scale=r.f32(); m.effect=r.u32();
    }
    if(!r.ok() || r.left() || !m.run || !m.serial || m.run!=C.run() || m.act<C.act() || m.act>=8 ||
        !explosive(m.type) || !m.id.incarnation || (m.kind==Detonated && !valid_effect(m))) return;
    if(host()) {
        if(!room() || m.act!=C.act() || (m.kind!=Activate && m.kind!=Arm && m.kind!=Throw) || from!=m.player || !connected(from) ||
            m.serial<=g_last_request[from]) return;
        g_last_request[from]=m.serial;
    } else if(from!=0 || (m.kind!=Triggered && m.kind!=Detonated && m.kind!=Armed && m.kind!=Thrown)) return;
    m.generation=g_generation; H->run_outside_frame(apply,new Message(m));
}
bool environmental_damage() { return active() && host() && g_damage_depth && !g_visual; }
bool visual_only() { return active() && g_visual; }
std::string status()
{
    std::ostringstream s; s<<"explosives "<<(g_ok?"ready":"unavailable")<<" activated="<<g_activated
        <<" detonated="<<g_detonated<<" thrown="<<g_throws<<" rejected="<<g_rejected<<" queued="<<g_outgoing.size(); return s.str();
}
} // namespace adventure_explosives
