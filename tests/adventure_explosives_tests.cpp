// Win32 ABI regression for native explosive entry points and scoped authority.
#include "../mods/coop/adventure_explosives.cpp"

#include <array>
#include <chrono>
#include <cstdio>
#include <cstring>
#include <thread>
#include <vector>

namespace ex=adventure_explosives;
using skg::field;
static int failures;
#define CHECK(c) do { if(!(c)) { std::printf("FAIL %d: %s\n",__LINE__,#c); ++failures; } } while(0)
static_assert(sizeof(void*)==4,"native ABI regression requires Win32");
namespace {
struct Object {
    std::array<uint8_t,0x3C> powerup{};
    std::array<uint8_t,0x40> combatant{};
    ex::Identity id;
    bool removed=false;
};
std::vector<std::unique_ptr<Object>> objects;
std::array<std::array<uint8_t,0x20>,4> captains{};
void* powerup_table[3]{};
void* combatant_table[4]{};
unsigned activations,explosions,blasts,arms,bombs,timer_ticks,powerup_ticks,impulses,retirements,expiry_calls,queue_failures,effects;
unsigned prepared_throws;
net::Vec3 last_effect_position;
ex::Quat last_effect_orientation;
float last_effect_scale;
uint32_t last_effect_id;
bool paused=false,enabled=true,in_range=true,owns=true,bound=true;
bool giant=false;
float last_powerup_dt=-1;
uint64_t run=7;
uint8_t act=0;
double now=1;
std::vector<std::pair<SkTaskFn,void*>> tasks;
std::map<std::string,uintptr_t> symbols;
void queue(SkTaskFn f,void* p) { tasks.emplace_back(f,p); }
void drain() { auto pending=std::move(tasks); tasks.clear(); for(auto [f,p]:pending) f(p); }
Object* find(void* p)
{
    for(auto& o:objects) if(p==o.get() || p==o->powerup.data() || p==o->combatant.data()) return o.get();
    return nullptr;
}
void* SK_THISCALL game_data(void* p) { return find(p); }
void* SK_THISCALL spatial(void* p) { return find(p); }
void* SK_CDECL physics() { return reinterpret_cast<void*>(0x1234); }
void SK_CDECL native_radius(void*,void*,float,float,float)
{ ++blasts; CHECK(ex::environmental_damage()); CHECK(!ex::visual_only()); }
void SK_CDECL native_effect(const net::Vec3* p,const ex::Quat* q,float scale,uint32_t id)
{ ++effects; last_effect_position=*p; last_effect_orientation=*q; last_effect_scale=scale; last_effect_id=id; }
void SK_CDECL native_explode(void* p,void*)
{
    ++explosions;
    if(ex::visual_only()) CHECK(!ex::environmental_damage());
    else CHECK(ex::environmental_damage());
    net::Vec3 position{10,20,30}; ex::Quat orientation{0,0,0,1};
    ex::effect(&position,&orientation,4,0x12345678);
    ex::radius(nullptr,nullptr,20,5,2); ex::unowned_radius(nullptr,nullptr,20,5,2);
    find(p)->removed=true;
}
void SK_CDECL native_mine(void* p,void*)
{ ++activations; CHECK(ex::environmental_damage()); ex::explode(p,nullptr); }
void SK_CDECL native_contact(void* p,void*)
{ ++activations; CHECK(ex::environmental_damage()); ex::explode(p,nullptr); }
void SK_CDECL native_arm(void* p,void* reference)
{ ++arms; field<bool>(p,0x30)=true; field<float>(p,0x34)=0; field<void*>(p,0x38)=reference; }
void SK_CDECL native_bomb(void*) { ++bombs; }
void SK_CDECL native_expiry(void*) { ++expiry_calls; }
void SK_CDECL native_schedule(void*,void*,float) {}
void SK_CDECL native_timers(float) { ++timer_ticks; }
void SK_CDECL native_powerups(float dt) { ++powerup_ticks; last_powerup_dt=dt; }
void SK_THISCALL native_impulse(void* p,void*,const net::Vec3*,bool)
{ CHECK(p==physics()); ++impulses; }
uintptr_t symbol(const char* n) { return symbols[n]; }
int hook(SkMod*,void* target,void*,void** original) { *original=target; return 0; }
double time_now() { return now; }
const char* exe_hash() { return "dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37"; }
Object* make(int type)
{
    auto o=std::make_unique<Object>(); o->id={uint32_t(objects.size()+1),uint32_t(objects.size()+100)};
    field<void**>(o->powerup.data(),0)=powerup_table; field<int>(o->powerup.data(),4)=type;
    field<void**>(o->combatant.data(),0)=combatant_table;
    objects.push_back(std::move(o)); return objects.back().get();
}
bool connect(net::Session& h,net::Session& g)
{
    std::string error;
    if(!g.join("127.0.0.1:"+std::to_string(h.port()),"Guest",error)) return false;
    for(int i=0;i<1000;++i) {
        g.update(); h.update(); g.update(); if(g.connected()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}
void incoming(uint8_t from,const ex::Message& m) { auto w=ex::encode(m); net::Reader r(w.data.data(),w.size()); ex::on_message(from,r); }
} // namespace

int main()
{
    powerup_table[1]=reinterpret_cast<void*>(game_data); powerup_table[2]=reinterpret_cast<void*>(spatial);
    combatant_table[3]=reinterpret_cast<void*>(game_data);
    symbols={
        {"SporeCoop::Adventure::ActivateMine",(uintptr_t)native_mine},
        {"SporeCoop::Adventure::ActivateGrenade",(uintptr_t)native_contact},
        {"SporeCoop::Adventure::ArmGrenade",(uintptr_t)native_arm},
        {"SporeCoop::Adventure::StartBombFuse",(uintptr_t)native_bomb},
        {"SporeCoop::Adventure::BombFuseExpiry",(uintptr_t)native_expiry},
        {"SporeCoop::Adventure::ExplosionDelayExpiry",(uintptr_t)native_expiry},
        {"SporeCoop::Adventure::SchedulePowerupExplosion",(uintptr_t)native_schedule},
        {"SporeCoop::Adventure::ExplodePowerup",(uintptr_t)native_explode},
        {"SporeCoop::Adventure::ExplosionEffect",(uintptr_t)native_effect},
        {"SporeCoop::Adventure::ExplosionRadius",(uintptr_t)native_radius},
        {"SporeCoop::Adventure::ExplosionUnownedRadius",(uintptr_t)native_radius},
        {"SporeCoop::Adventure::TickPowerupTimers",(uintptr_t)native_timers},
        {"SporeCoop::Adventure::TickPowerups",(uintptr_t)native_powerups},
        {"Simulator::cPhysicsWorld::Get",(uintptr_t)physics},
        {"Simulator::cPhysicsWorld::ApplyImpulse",(uintptr_t)native_impulse},
    };
    SkHost api{}; api.sym=symbol; api.hook_create=hook; api.run_outside_frame=queue; api.time_seconds=time_now; api.exe_sha256=exe_hash;
    net::Session host,guest; host.build=guest.build=1;
    std::string error; CHECK(host.host(0,"Host",error)); CHECK(connect(host,guest));
    ex::Ctx ctx; ctx.sess=&host; ctx.run=[] { return run; }; ctx.act=[] { return act; };
    ctx.active=[] { return enabled; }; ctx.held=[] { return paused; };
    ctx.identify=[](void* p,ex::Identity& id) { auto* o=find(p); if(!o || !bound) return false; id=o->id; return true; };
    ctx.object=[](ex::Identity id)->void* { for(auto& o:objects) if(!o->removed && !(o->id<id) && !(id<o->id)) return o.get(); return nullptr; };
    ctx.powerup=[](void* p)->void* { auto* o=find(p); return o?o->powerup.data():nullptr; };
    ctx.combatant=[](void* p)->void* { auto* o=find(p); return o?o->combatant.data():nullptr; };
    ctx.from_spatial=[](void* p)->void* { return find(p); };
    ctx.captain=[](uint8_t p)->void* { return p<4?captains[p].data():nullptr; };
    ctx.owner=[](void* p) { for(uint8_t i=0;i<4;++i) if(p==captains[i].data()) return i; return uint8_t(0xFF); };
    ctx.can_act=[](uint8_t p) { return p<2; };
    ctx.owns_throw=[](uint8_t,void*) { return owns; };
    ctx.nearby=[](void*,void*,float) { return in_range; };
    ctx.bounds=[](void* p,net::Vec3& minimum,net::Vec3& maximum) {
        float center=find(p)?(in_range?(giant?8.f:1.f):100.f):0.f;
        float extent=giant?5.f:0.5f; minimum={center-extent,-extent,-extent}; maximum={center+extent,extent,extent}; return true;
    };
    ctx.retire=[](ex::Identity) { ++retirements; };
    ctx.failed=[](const std::string&) { ++queue_failures; };
    ctx.explosive_thrown=[](ex::Identity,uint8_t player) { CHECK(ex::host() && player<2); ++prepared_throws; };
    CHECK(ex::init(&api,nullptr,ctx)); ex::tick();
    // All four explosive kinds use the native actual-explosion path once.
    for(int type:{6,10,13,15}) {
        auto* o=make(type); unsigned before=explosions;
        ex::explode(o->powerup.data(),nullptr); ex::explode(o->powerup.data(),nullptr);
        CHECK(explosions==before+1 && o->removed); CHECK(!ex::environmental_damage());
        CHECK(ex::g_outgoing.back().message.effect==0x12345678 && ex::g_outgoing.back().message.scale==4);
    }
    CHECK(blasts==8);
    auto* mine=make(6); auto contact_request=ex::message(ex::Activate,mine->id,1,6);
    in_range=false; incoming(1,contact_request); drain(); CHECK(!mine->removed);
    in_range=true; contact_request=ex::message(ex::Activate,mine->id,1,6); incoming(1,contact_request); drain(); CHECK(mine->removed);
    unsigned before=activations;
    contact_request=ex::message(ex::Activate,mine->id,1,6); incoming(1,contact_request); drain(); CHECK(activations==before);
    auto* giant_mine=make(6); giant=true; contact_request=ex::message(ex::Activate,giant_mine->id,1,6);
    incoming(1,contact_request); drain(); CHECK(giant_mine->removed); giant=false;
    auto* timed=make(10); paused=true; bound=false; ex::bomb(timed->powerup.data()); CHECK(bombs==1);
    bound=true; ex::bomb(timed->powerup.data()); ex::bomb(timed->powerup.data()); CHECK(bombs==1);
    ex::timers(1); ex::powerups(1); CHECK(!timer_ticks && powerup_ticks==1 && last_powerup_dt==0);
    paused=false; ex::timers(1); ex::powerups(1); CHECK(timer_ticks==1 && powerup_ticks==2 && last_powerup_dt==1);
    // Cleanup flushes callbacks synchronously. A cancelled run's fuse/chain
    // must be discarded before its native callback can kill another object.
    std::array<uint8_t,8> old_task{}; field<void*>(old_task.data(),4)=timed;
    ++run; ex::tick(); ex::bomb_expiry(old_task.data()); CHECK(expiry_calls==0);
    auto* delayed=make(15); ex::schedule(delayed->powerup.data(),nullptr,0.25f);
    field<void*>(old_task.data(),4)=delayed; ex::reset(); ++run; ex::tick();
    ex::explosion_expiry(old_task.data()); CHECK(expiry_calls==0);
    // Native contact-reference grace is preserved and cannot consume early.
    auto* grenade=make(13); ex::arm(grenade->powerup.data(),captains[0].data()); CHECK(arms==1);
    before=activations; ex::contact(grenade->powerup.data(),captains[0].data()); CHECK(activations==before);
    field<float>(grenade->powerup.data(),0x34)=1.1f;
    ex::contact(grenade->powerup.data(),captains[0].data()); CHECK(activations==before+1 && grenade->removed);
    // Replaced runs and failed distance/ownership checks cannot arm or throw.
    auto* thrown=make(13); auto request=ex::message(ex::Arm,thrown->id,1,13);
    incoming(1,request); ++run; ex::tick(); drain(); CHECK(!field<bool>(thrown->powerup.data(),0x30));
    request=ex::message(ex::Arm,thrown->id,1,13); owns=false; incoming(1,request); drain(); CHECK(!field<bool>(thrown->powerup.data(),0x30));
    owns=true; request=ex::message(ex::Arm,thrown->id,1,13); incoming(1,request); drain(); CHECK(field<bool>(thrown->powerup.data(),0x30));
    auto toss=ex::message(ex::Throw,thrown->id,1,13); toss.impulse={1000,0,0};
    incoming(1,toss); drain(); CHECK(!impulses);
    toss=ex::message(ex::Throw,thrown->id,1,13); toss.impulse={5,3,2}; in_range=false; incoming(1,toss); drain(); CHECK(!impulses);
    in_range=true; toss=ex::message(ex::Throw,thrown->id,1,13); toss.impulse={5,3,2}; incoming(1,toss); drain(); CHECK(impulses==1 && prepared_throws==1);
    incoming(1,toss); drain(); CHECK(impulses==1 && prepared_throws==1);
    // A duplicate throw stays rejected. Only a new trusted native pickup of
    // the same timed bomb clears its throw guard, never its existing fuse.
    auto* repicked=make(10); ex::bomb(repicked->powerup.data());
    unsigned old_bombs=bombs,old_impulses=impulses;
    ex::impulse(physics(),repicked,&toss.impulse,true);
    CHECK(impulses==old_impulses+1 && ex::g_states[repicked->id].thrown);
    ex::impulse(physics(),repicked,&toss.impulse,true); CHECK(impulses==old_impulses+1);
    ex::picked(repicked->id,1); CHECK(!ex::g_states[repicked->id].thrown && ex::g_states[repicked->id].armed);
    ex::bomb(repicked->powerup.data()); CHECK(bombs==old_bombs);
    ex::impulse(physics(),repicked,&toss.impulse,true); CHECK(impulses==old_impulses+2);
    ex::impulse(physics(),repicked,&toss.impulse,true); CHECK(impulses==old_impulses+2);
    ex::g_states[repicked->id].phase=ex::Phase::Retired;
    ex::picked(repicked->id,1); CHECK(ex::g_states[repicked->id].thrown);
    ex::picked(thrown->id,1); CHECK(ex::g_states[thrown->id].thrown); // grenade13 retains its guard
    // Guest-approved effects retain native removal without any local radius damage.
    ctx.sess=&guest; CHECK(ex::init(&api,nullptr,ctx)); ex::tick();
    auto* foreign_grenade=make(13); unsigned old_arms=arms;
    ex::arm(foreign_grenade->powerup.data(),captains[0].data());
    CHECK(arms==old_arms && !field<bool>(foreign_grenade->powerup.data(),0x30));
    auto armed_event=ex::message(ex::Armed,foreign_grenade->id,0,13);
    auto thrown_event=ex::message(ex::Thrown,foreign_grenade->id,0,13); thrown_event.impulse={1,2,3};
    paused=true; incoming(0,armed_event); incoming(0,thrown_event); drain();
    CHECK(ex::g_waiting.size()==1); paused=false; ex::tick(); drain();
    CHECK(field<bool>(foreign_grenade->powerup.data(),0x30) && field<void*>(foreign_grenade->powerup.data(),0x38)==captains[0].data());
    auto* guest_repick=make(10); auto old_throw=ex::message(ex::Thrown,guest_repick->id,guest.my_id(),10);
    old_throw.impulse={1,0,0}; incoming(0,old_throw); drain();
    CHECK(ex::g_states[guest_repick->id].thrown);
    ex::picked(guest_repick->id,guest.my_id()); CHECK(!ex::g_states[guest_repick->id].thrown);
    old_impulses=impulses; ex::impulse(physics(),guest_repick,&old_throw.impulse,true);
    // The fake inventory selects player0; it cannot predict a foreign throw.
    CHECK(impulses==old_impulses);
    auto* barrel=make(15); before=blasts; unsigned before_explosions=explosions;
    ex::explode(barrel->powerup.data(),nullptr); CHECK(explosions==before_explosions);
    auto event=ex::message(ex::Detonated,barrel->id,0xFF,15);
    event.position={100,200,300}; event.orientation={0,0,1,0}; event.scale=3; event.effect=123;
    incoming(1,event); drain(); CHECK(!barrel->removed); // A guest cannot forge host effects.
    incoming(0,event); paused=true; drain(); CHECK(!barrel->removed); // queued before the final pause intent
    paused=false; ex::tick(); drain(); CHECK(barrel->removed && blasts==before && retirements==1);
    CHECK(last_effect_position.x==100 && last_effect_orientation.z==1 && last_effect_scale==3 && last_effect_id==123);
    incoming(0,event); drain(); CHECK(retirements==1);
    // Future-act events survive native SetAct ordering and are applied once.
    auto* future=make(6); event=ex::message(ex::Detonated,future->id,0xFF,6); event.act=1;
    incoming(0,event); drain(); CHECK(!future->removed); act=1; ex::tick(); drain(); CHECK(future->removed && retirements==2);
    // A short-lived object can detonate before its guest binding exists. Show
    // the host's exact native FX once and retire only that authoritative ID.
    auto* newer=make(6); newer->id={999,1000};
    event=ex::message(ex::Detonated,{999,999},0xFF,6);
    event.position={50,60,70}; event.scale=6; event.effect=987;
    unsigned old_effects=effects;
    incoming(0,event); drain(); CHECK(retirements==3 && effects==old_effects+1 && !newer->removed);
    CHECK(last_effect_position.x==50 && last_effect_scale==6 && last_effect_id==987);
    incoming(0,event); drain(); CHECK(effects==old_effects+1 && retirements==3);
    // A delayed baseline/binding for the spent ID cannot produce another effect.
    auto* late=make(6); late->id={999,999}; event.serial++;
    incoming(0,event); drain(); CHECK(!late->removed && effects==old_effects+1);
    // Already queued host detonation work rechecks the hold, and cancellation
    // discards its deferred visual/removal instead of executing on a later run.
    auto* cancelled=make(6); auto cancelled_event=ex::message(ex::Detonated,cancelled->id,0xFF,6);
    incoming(0,cancelled_event); paused=true; drain();
    CHECK(!cancelled->removed && ex::g_waiting.size()==1);
    ex::reset(); ++run; paused=false; ex::tick(); drain();
    CHECK(!cancelled->removed && effects==old_effects+1 && retirements==3);
    auto invalid=ex::message(ex::Detonated,{998,998},0xFF,6); invalid.scale=INFINITY;
    incoming(0,invalid); drain(); CHECK(retirements==3 && effects==old_effects+1);
    ctx.sess=&host; CHECK(ex::init(&api,nullptr,ctx)); ex::tick();
    for(unsigned i=0;i<8193;++i) ex::publish(ex::message(ex::Triggered,{1000+i,1000+i},0xFF,6));
    CHECK(ex::g_outgoing.size()==8192); ex::tick(); ex::tick(); CHECK(queue_failures==1);
    ex::reset(); drain(); guest.leave("done"); host.leave("done");
    std::printf("adventure explosives: %d failures\n",failures); return failures?1:0;
}
