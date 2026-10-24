// Win32 ABI and authority regression for collector-only pickups and teleporters.
#include <array>
#include <chrono>
#include <cstdio>
#include <map>
#include <memory>
#include <thread>
#include <vector>

#include "adventure_pickups.h"
#include "game.h"
#include "sporekit_game.hpp"

namespace fixture {
using skg::field;
struct Properties { void** vt=nullptr;float duration=5,amount=3;net::Vec3 exit{20,30,40};bool valid=true; };
struct Object { std::array<uint8_t,0x1100> bytes{};Properties props;net::Vec3 position{};net::Quat rotation{0,0,0,1};bool item=false; };
std::vector<std::unique_ptr<Object>> objects;
std::map<void*,Object*> lookup;
std::map<std::pair<uint32_t,uint32_t>,void*> identities;
std::vector<std::pair<SkTaskFn,void*>> tasks;
std::array<uint8_t,0xD0> mode{};
std::array<uint8_t,0x110> play{};
void* mode_pointer=mode.data();
void* powerup_vt[3]{};void* spatial_vt[32]{};void* combatant_vt[24]{};void* prop_vt[12]{};
std::array<uint8_t,0x14> effect_property{};
void* captains[2]{};
uint64_t run=10;int act=0,failures;double simulation=100,wall=100;bool active=true,held=false;
unsigned speed_calls,effect_calls[2]{},collect_calls,goal_calls,destroy_calls,warp_calls,tele_callbacks,heals[2]{};
unsigned aborted;
float healed[2]{};
std::vector<adventure_pickups::Identity> retired;
#define CHECK(c) do {if(!(c)){std::printf("FAIL %d: %s\n",__LINE__,#c);++fixture::failures;}}while(0)
Object* object(void* value) {auto i=lookup.find(value);return i==lookup.end()?nullptr:i->second;}
void* make(bool item,int kind,uint32_t marker,uint32_t incarnation) {
    auto p=std::make_unique<Object>();p->item=item;p->props.vt=prop_vt;void* raw=p->bytes.data();
    field<void**>(raw,0x130)=powerup_vt;field<void**>(raw,0xC0)=spatial_vt;field<void**>(raw,0x5A8)=combatant_vt;
    field<int>(raw,0x134)=kind;field<void*>(raw,0x148)=&p->props;field<uint32_t>(raw,0x24)=incarnation;
    lookup[raw]=p.get();identities[{marker,incarnation}]=raw;objects.push_back(std::move(p));return raw;
}
void* SK_THISCALL to_object(void* p) {return static_cast<uint8_t*>(p)-0x130;}
float SK_THISCALL radius(void*) {return 1;}
float SK_THISCALL maximum(void*) {return 120;}
bool SK_THISCALL get_property(void*,uint32_t key,void** result) {*result=effect_property.data();return key==0x72AEEDA0;}
float SK_CDECL property(void* props,uint32_t key,float fallback) {
    auto* p=static_cast<Properties*>(props);return !p?fallback:key==0xAAC3B137?p->duration:key==0x6219412B?p->amount:fallback;
}
void SK_THISCALL speed(void* captain,int modifier,bool enabled) {
    CHECK(modifier==7);++speed_calls;
    if(enabled) field<uint32_t>(captain,0xFA4)|=128;else field<uint32_t>(captain,0xFA4)&=~128u;
}
void SK_CDECL effect(void* captain,uint32_t value) {CHECK(value==77);++effect_calls[captain==captains[1]?1:0];}
void SK_THISCALL collect(void* p,void* item,void* captain) {CHECK(p==play.data()&&object(item)&&object(captain)&&(field<uint32_t>(captain,0xB58)&512));++collect_calls;}
void SK_THISCALL goals(void* p) {CHECK(p==play.data());++goal_calls;}
bool SK_CDECL destination(void* value,net::Vec3* pos,net::Quat* rot) {
    auto* p=static_cast<Properties*>(field<void*>(value,0x18));*pos=p->exit;*rot={0,0,0,1};return p->valid;
}
void SK_CDECL teleport(void* value,void* captain,const net::Vec3* pos,const net::Quat* rot,bool player) {
    CHECK(value&&player);object(captain)->position=*pos;object(captain)->rotation=*rot;
    field<bool>(captain,0x137)=false;++warp_calls;
}
void SK_CDECL original(void*,void*) {CHECK(false);}
std::map<std::string,uintptr_t> symbols;
uintptr_t sym(const char* name) {auto i=symbols.find(name);return i==symbols.end()?0:i->second;}
int hook(SkMod*,void*,void*,void** saved) {*saved=reinterpret_cast<void*>(original);return 0;}
const char* hash() {return "dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37";}
void queue(SkTaskFn f,void* data) {tasks.emplace_back(f,data);}
double time() {return wall;}
void drain() {while(!tasks.empty()){auto work=std::move(tasks);tasks.clear();for(auto [f,p]:work)f(p);}}
void tick() {adventure_pickups::tick();drain();}
void put64(net::Writer& w,uint64_t n) {w.u32(uint32_t(n));w.u32(uint32_t(n>>32));}
net::Writer packet(uint8_t kind,uint64_t rev,uint8_t player,uint8_t type,adventure_pickups::Identity id) {
    net::Writer w;w.u8(kind);put64(w,run);put64(w,rev);w.u8(uint8_t(act));w.u8(player);w.u8(type);w.u32(id.marker);w.u32(id.incarnation);return w;
}
void receive(uint8_t from,const net::Writer& w) {net::Reader r(w.data.data(),w.size());adventure_pickups::on_message(from,r);}
bool connect(net::Session& h,net::Session& g) {
    std::string error;if(!g.join("127.0.0.1:"+std::to_string(h.port()),"Guest",error)) return false;
    for(int i=0;i<1000;++i) {h.update();g.update();if(g.connected()) return true;std::this_thread::sleep_for(std::chrono::milliseconds(1));}return false;
}
} // namespace fixture

namespace game {
void* cast(void* value,uint32_t type) {
    auto* p=fixture::object(value);if(!p)return nullptr;
    if(type==0x08367427)return p->item?static_cast<uint8_t*>(value)+0x130:nullptr;
    if(type==0x01186577)return static_cast<uint8_t*>(value)+0xC0;
    if(type==0x013F94D4)return static_cast<uint8_t*>(value)+0x5A8;
    return nullptr;
}
bool object_alive(void* value) {return fixture::object(value)&&!skg::field<bool>(value,0x20);}
bool object_position(void* value,net::Vec3& out) {auto* p=fixture::object(value);if(!p)return false;out=p->position;return true;}
net::Quat orientation(void* value) {return fixture::object(value)->rotation;}
float bounding_radius(void*) {return 1;}
void destroy_creature(void* value) {CHECK(!skg::field<bool>(value,0x20));skg::field<bool>(value,0x20)=true;++fixture::destroy_calls;}
} // namespace game

// Include the adapter so the test can switch its singleton between a real host
// and guest session, and assert the run-scoped expiry state without game hooks.
#include "../mods/coop/adventure_pickups.cpp"

int main() {
    using namespace fixture;namespace pickups=adventure_pickups;
    powerup_vt[1]=reinterpret_cast<void*>(to_object);spatial_vt[0x70/4]=reinterpret_cast<void*>(radius);
    combatant_vt[0x58/4]=reinterpret_cast<void*>(maximum);prop_vt[0x24/4]=reinterpret_cast<void*>(get_property);
    field<uint16_t>(effect_property.data(),0x12)=0x0A;field<uint32_t>(effect_property.data(),0)=77;
    field<int>(mode.data(),0xCC)=2;field<void*>(mode.data(),0x78)=play.data();
    captains[0]=make(false,0,500,1);captains[1]=make(false,0,501,2);
    net::Session host,guest;host.build=guest.build=1;std::string error;CHECK(host.host(0,"Host",error));CHECK(connect(host,guest));
    symbols={{"App::cScenarioMode::ptr",reinterpret_cast<uintptr_t>(&mode_pointer)},
        {"App::Property::GetFloat",reinterpret_cast<uintptr_t>(property)},
        {"SporeCoop::Adventure::SetSpeedModifier",reinterpret_cast<uintptr_t>(speed)},
        {"SporeCoop::Adventure::PickupEffect",reinterpret_cast<uintptr_t>(effect)},
        {"SporeCoop::Adventure::OnCollect",reinterpret_cast<uintptr_t>(collect)},
        {"Simulator::cScenarioPlayMode::UpdateGoals",reinterpret_cast<uintptr_t>(goals)},
        {"SporeCoop::Adventure::TeleportDestination",reinterpret_cast<uintptr_t>(destination)},
        {"SporeCoop::Adventure::TeleportCaptain",reinterpret_cast<uintptr_t>(teleport)}};
    for(const char* name:{"ActivateMedKit","ActivateSpeedBoost","ActivateDamageBoost","ActivateArmorBoost","ActivateTeleporter"})
        symbols[std::string("SporeCoop::Adventure::")+name]=reinterpret_cast<uintptr_t>(original);
    SkHost api{};api.sym=sym;api.hook_create=hook;api.exe_sha256=hash;api.run_outside_frame=queue;api.time_seconds=time;
    pickups::Ctx c;c.sess=&host;c.run=[] {return run;};c.act=[] {return act;};c.active=[] {return active;};c.held=[] {return held;};c.simulation_time=[] {return simulation;};
    c.captain_of=[](uint8_t p)->void* {return p<2?captains[p]:nullptr;};c.captain_can_act=[](uint8_t p) {return p<2&&!field<bool>(captains[p],0xB5E);};
    c.identity=[](void* value,pickups::Identity& id) {for(auto [key,p]:identities)if(p==value){id={key.first,key.second};return true;}return false;};
    c.object_for=[](pickups::Identity id)->void* {auto i=identities.find({id.marker,id.incarnation});return i==identities.end()?nullptr:i->second;};
    c.retire=[](pickups::Identity id){retired.push_back(id);};c.captain_heal=[](uint8_t p,float hp){++heals[p];healed[p]=hp;return true;};
    c.teleport=[](uint8_t p,uint64_t rev,const net::Vec3& pos,const net::Quat& rot){CHECK(p<2&&rev);if(captains[p]){object(captains[p])->position=pos;object(captains[p])->rotation=rot;}++tele_callbacks;};
    c.failed=[](const std::string& reason){CHECK(reason.find("pending queue")!=std::string::npos);++aborted;};
    CHECK(pickups::init(&api,nullptr,c));tick();
    void* hp=make(true,1,1,10);CHECK(pickups::activate(hp,captains[1]));tick();
    CHECK(heals[1]==1&&heals[0]==0&&healed[1]==120&&destroy_calls==1&&collect_calls==1&&effect_calls[1]==1&&effect_calls[0]==0);
    CHECK(!(field<uint32_t>(captains[1],0xB58)&512));
    CHECK(pickups::activate(hp,captains[0]));tick();CHECK(heals[0]==0&&destroy_calls==1);
    void* boost=make(true,4,2,11);CHECK(pickups::activate(boost,captains[0]));
    receive(1,packet(152,20,1,4,{2,11}));tick();
    CHECK(field<bool>(captains[0],0xB10)&&field<float>(captains[0],0xB14)==3&&!field<bool>(captains[1],0xB10));
    // Refresh uses the latest magnitude and restarts the same effect timer.
    simulation=103;wall+=1;void* refresh=make(true,4,3,12);object(refresh)->props.amount=5;
    pickups::activate(refresh,captains[0]);tick();simulation=106;tick();CHECK(field<bool>(captains[0],0xB10)&&field<float>(captains[0],0xB14)==5);
    held=true;wall+=20;tick();CHECK(field<bool>(captains[0],0xB10));held=false;simulation=109;tick();CHECK(!field<bool>(captains[0],0xB10));
    void* armor_item=make(true,5,4,13);object(armor_item)->props.amount=4;pickups::activate(armor_item,captains[1]);tick();
    CHECK(!field<bool>(captains[1],0xB18)&&pickups::adjust_damage(1,80)==20&&pickups::adjust_damage(0,80)==80);
    void* speed_item=make(true,3,5,14);pickups::activate(speed_item,captains[1]);tick();
    CHECK((field<uint32_t>(captains[1],0xFA4)&128)&&!(field<uint32_t>(captains[0],0xFA4)&128));
    // Distant, wrong-owner, dead and invalid-exit claims cannot consume anything.
    void* distant=make(true,1,6,15);object(distant)->position={100,0,0};receive(1,packet(152,21,1,1,{6,15}));tick();CHECK(game::object_alive(distant));
    receive(1,packet(152,22,0,1,{6,15}));tick();CHECK(game::object_alive(distant));
    void* tele=make(true,8,7,16);object(tele)->props.valid=false;pickups::activate(tele,captains[0]);tick();CHECK(warp_calls==0);
    object(tele)->props.valid=true;wall+=1;pickups::activate(tele,captains[0]);tick();
    CHECK(warp_calls==1&&tele_callbacks==1&&object(captains[0])->position.x==20&&object(captains[1])->position.x==0&&game::object_alive(tele));
    CHECK(field<bool>(captains[0],0xE60));object(tele)->position={20,30,40};wall+=1;pickups::activate(tele,captains[0]);tick();CHECK(warp_calls==1);
    simulation+=2.1;tick();CHECK(!field<bool>(captains[0],0xE60));
    // A stalled peer bounds reliable publication and rejects excess claims
    // before consuming them. They remain available for a later contact retry.
    net::Writer filler;filler.u8(0);
    while(host.pending_to(1)<64) host.send_reliable_to(1,net::MSG_ADVENTURE,filler);
    unsigned survivors=0;
    for(uint32_t i=0;i<550;++i) {
        void* queued_item=make(true,1,1000+i,1000+i);object(queued_item)->position=object(captains[0])->position;
        pickups::activate(queued_item,captains[0]);tick();if(game::object_alive(queued_item)) ++survivors;
    }
    CHECK(survivors>=54&&pickups::outgoing[1].size()<=512&&host.pending_to(1)<=64);
    // Reset invalidates already queued callbacks and restores saved native stats.
    void* cancelled=make(true,1,8,17);object(cancelled)->position=object(captains[0])->position;
    pickups::activate(cancelled,captains[0]);pickups::tick();pickups::reset();++run;drain();CHECK(game::object_alive(cancelled));
    CHECK(!(field<uint32_t>(captains[1],0xFA4)&128)&&!field<bool>(captains[1],0xB18));
    // Guest host-events are trusted only from host and grants never heal other captains.
    pickups::C.sess=&guest;tick();unsigned before=heals[0]+heals[1];
    void* remote=make(true,4,9,18);auto grant=packet(153,50,1,4,{9,18});grant.f32(7);grant.f32(float(simulation+5));grant.u32(77);
    receive(1,grant);tick();CHECK(game::object_alive(remote));receive(0,grant);tick();
    CHECK(!game::object_alive(remote)&&field<bool>(captains[1],0xB10)&&field<float>(captains[1],0xB14)==7&&heals[0]+heals[1]==before);
    simulation+=20;tick();CHECK(field<bool>(captains[1],0xB10)); // host alone expires effects
    auto old_expire=packet(154,51,1,4,{9,18});put64(old_expire,49);receive(0,old_expire);tick();CHECK(field<bool>(captains[1],0xB10));
    auto expire=packet(154,52,1,4,{9,18});put64(expire,50);receive(0,expire);tick();CHECK(!field<bool>(captains[1],0xB10));
    // Older grants still retire their pickup but cannot replace a newer boost.
    void* newest=make(true,4,10,19);auto newer=packet(153,70,1,4,{10,19});newer.f32(9);newer.f32(float(simulation+10));newer.u32(77);
    void* oldest=make(true,4,11,20);auto older=packet(153,60,1,4,{11,20});older.f32(2);older.f32(float(simulation+5));older.u32(77);
    receive(0,newer);receive(0,older);tick();CHECK(!game::object_alive(newest)&&!game::object_alive(oldest)&&field<float>(captains[1],0xB14)==9);
    // Events from a later act wait for its world baseline; an old run never applies.
    void* future=make(true,3,12,21);++act;auto next_act=packet(153,80,0,3,{12,21});next_act.f32(1);next_act.f32(float(simulation+10));next_act.u32(77);--act;
    receive(0,next_act);tick();CHECK(game::object_alive(future));++act;tick();CHECK(!game::object_alive(future));
    void* delayed=make(true,5,13,22);--act;auto prior_act=packet(153,81,0,5,{13,22});++act;
    prior_act.f32(4);prior_act.f32(float(simulation+5));prior_act.u32(77);receive(0,prior_act);tick();CHECK(!game::object_alive(delayed));
    // An expiry delivered ahead of its grant is a fence, not a lost callback.
    void* expired=make(true,4,14,23);auto expiry_first=packet(154,100,1,4,{14,23});put64(expiry_first,99);
    auto grant_last=packet(153,99,1,4,{14,23});grant_last.f32(11);grant_last.f32(float(simulation+5));grant_last.u32(77);
    receive(0,expiry_first);receive(0,grant_last);tick();CHECK(!game::object_alive(expired)&&!field<bool>(captains[1],0xB10));
    --run;auto old_run=packet(153,91,0,3,{13,22});++run;
    old_run.f32(1);old_run.f32(float(simulation+10));old_run.u32(77);receive(0,old_run);tick();CHECK(pickups::pending.empty());
    // A missing observer puppet must not block the generation fence which
    // allows its new-generation movement to create that puppet in the first place.
    auto teleport_event=[](uint64_t revision,float x) {
        auto w=packet(155,revision,0,8,{7,16});w.f32(x);w.f32(30);w.f32(40);
        w.f32(0);w.f32(0);w.f32(0);w.f32(1);w.f32(float(simulation+2));return w;
    };
    void* previous_captain=captains[0];captains[0]=nullptr;
    unsigned callbacks_before=tele_callbacks,warps_before=warp_calls;
    auto older_teleport=teleport_event(110,41);receive(0,older_teleport);tick();
    CHECK(tele_callbacks==callbacks_before+1&&warp_calls==warps_before&&pickups::teleports[0]==110&&!pickups::pending.empty());
    auto newer_teleport=teleport_event(111,61);receive(0,newer_teleport);tick();
    CHECK(tele_callbacks==callbacks_before+2&&warp_calls==warps_before&&pickups::teleports[0]==111);
    captains[0]=previous_captain;tick();
    CHECK(warp_calls==warps_before+1&&tele_callbacks==callbacks_before+2&&object(captains[0])->position.x==61&&pickups::pending.empty());
    receive(0,older_teleport);receive(0,newer_teleport);tick();CHECK(warp_calls==warps_before+1&&tele_callbacks==callbacks_before+2);
    // Grants received before a captain exists retain their native fields and
    // artwork for that binding, then reapply once when a model is recreated.
    captains[1]=nullptr;unsigned artwork_before=effect_calls[1];
    void* unbound=make(true,4,15,24);auto unbound_grant=packet(153,120,1,4,{15,24});
    unbound_grant.f32(6);unbound_grant.f32(float(simulation+5));unbound_grant.u32(77);
    receive(0,unbound_grant);tick();CHECK(!game::object_alive(unbound)&&effect_calls[1]==artwork_before);
    captains[1]=make(false,0,601,5000);tick();
    CHECK(field<bool>(captains[1],0xB10)&&field<float>(captains[1],0xB14)==6&&effect_calls[1]==artwork_before+1);
    tick();CHECK(effect_calls[1]==artwork_before+1);
    field<uint32_t>(captains[1],0x24)=5001;field<bool>(captains[1],0xB10)=false;tick();
    CHECK(field<bool>(captains[1],0xB10)&&effect_calls[1]==artwork_before+2);
    tick();CHECK(effect_calls[1]==artwork_before+2);
    // An expiry arriving while a previously materialized binding is absent
    // clears it once upon return, without playing another positive effect.
    previous_captain=captains[1];captains[1]=nullptr;
    auto unbound_expiry=packet(154,121,1,4,{15,24});put64(unbound_expiry,120);receive(0,unbound_expiry);tick();
    captains[1]=previous_captain;tick();CHECK(!field<bool>(captains[1],0xB10)&&effect_calls[1]==artwork_before+2);
    tick();CHECK(effect_calls[1]==artwork_before+2);
    // More than1024 distinct respawn incarnations cannot exhaust a contact
    // throttle for the rest of the run. Verify the next request over real UDP.
    for(uint32_t i=0;i<1025;++i) {
        wall+=1;void* respawned=make(true,1,2000+i,2000+i);pickups::activate(respawned,captains[1]);
    }
    for(int i=0;i<200&&(host.pending_to(1)||guest.pending_to(0));++i) {
        host.update();guest.update();std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    uint32_t observed_marker=0;
    host.cb.custom=[&](uint8_t kind,uint8_t,net::Reader& r) {
        if(kind==net::MSG_ADVENTURE&&r.left()>=28&&r.cur()[0]==152) std::memcpy(&observed_marker,r.cur()+20,4);
    };
    wall+=1;void* later=make(true,1,9999,9999);pickups::activate(later,captains[1]);
    for(int i=0;i<200&&observed_marker!=9999;++i) {
        guest.update();host.update();std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
    CHECK(observed_marker==9999&&game::object_alive(later));
    // Accepted host events are retained up to1024, then cancel the attempt from
    // an outside-frame drain. Shared holds cannot defer this failure forever.
    for(uint32_t i=0;i<1025;++i) {
        auto burst=packet(153,20000+i,1,4,{20000+i,20000+i});
        burst.f32(2);burst.f32(float(simulation+5));burst.u32(77);receive(0,burst);
    }
    CHECK(aborted==0&&pickups::pending.size()==1024&&pickups::overflow);
    held=true;tick();CHECK(aborted==1);tick();CHECK(aborted==1);held=false;
    pickups::reset();++run;
    void* retry=make(true,4,30000,30000);auto retry_grant=packet(153,1,1,4,{30000,30000});
    retry_grant.f32(2);retry_grant.f32(float(simulation+5));retry_grant.u32(77);receive(0,retry_grant);tick();
    CHECK(!game::object_alive(retry)&&field<bool>(captains[1],0xB10)&&aborted==1&&!pickups::overflow);
    // Deferring an accepted guest event uses the same1024 bound as initial
    // reception. A smaller128 requeue cap silently lost the rest of a burst.
    std::vector<void*> deferred;
    for(uint32_t i=0;i<200;++i) {
        deferred.push_back(make(true,1,40000+i,40000+i));
        ++act;auto future=packet(153,40000+i,1,1,{40000+i,40000+i});--act;
        future.f32(0);future.f32(float(simulation));future.u32(0);receive(0,future);
    }
    tick();CHECK(pickups::pending.size()==200);tick();CHECK(pickups::pending.size()==200);
    ++act;tick();CHECK(pickups::pending.empty()&&aborted==1);
    for(void* value:deferred) CHECK(!game::object_alive(value));
    pickups::reset();std::printf("adventure pickups: %d failures\n",failures);return failures?1:0;
}
