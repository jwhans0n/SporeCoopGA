// BUG-1275: the native HoldObject/PickUpItem alias has one owner. Exercise the
// actual campaign and GA callbacks, native thiscall forwarding and duplicate-hook rejection.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <cwchar>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <string>
#include <thread>
#include <tuple>
#include <type_traits>
#include <variant>
#include <vector>
#include <windows.h>

#include "call_guard.h"
#include "savestrip.h"
#include "fixes.h"
#include "game.h"
#include "drama.h"
#include "npc.h"
#include "sporekit_game.hpp"
#include "sporekit_build.hpp"
#include "adventure_world_lifecycle.h"
#include "adventure_world_actions.h"
#include "adventure_world_vehicles.h"
#include "adventure_relationships.h"
#include "adventure_targeting.h"
#include "adventure_flow.h"
#include "adventure_pickups.h"
#include "adventure_explosives.h"
#include "net/adventure_health.h"
#include "net/adventure_actor_motion.h"
#include "net/adventure.h"
#include "net/session.h"

// Give the included modules fixture-local linkage so unrelated public diagnostics
// do not pull in the rest of the game. The tested callback bodies are unchanged.
namespace {
#include "../mods/coop/player_rules.cpp"
#include "../mods/coop/adventure_world.cpp"
}

namespace fixture {
using skg::field;
int failures, hold_calls, release_calls, duplicates;
bool creature_stage, paused, enabled = true, may_act = true;
uint64_t current_run = 12;
std::array<uint8_t,0x1100> captain{}, partner{}, npc_actor{}, item{};
std::array<uint8_t,0x24> partner_slot{};
std::array<uint8_t,0x238> marker{};
std::array<uint8_t,0xD0> mode{}, data{};
std::array<uint8_t,0x110> play{};
void* mode_ptr = mode.data();
void* spatial_vt[48]{};
struct Spatial { void** vt = spatial_vt; } spatial;
std::map<void*,void*> detours;
void* rejected_target;
uint32_t last_slot;
int release_a, release_b;
void* last_creature;
bool last_replace, native_result = true;
void check(bool value, const char* what) {
    if(!value) { ++failures; std::printf("FAIL: %s\n",what); }
}
bool SK_THISCALL native_hold(void* c,void* obj,uint32_t slot,bool replace) {
    check(obj==&spatial,"native HoldObject retains exact spatial identity");
    ++hold_calls;last_creature=c;last_slot=slot;last_replace=replace;return native_result;
}
bool SK_THISCALL native_release(void* c,void* obj,int a,int b) {
    check(obj==&spatial,"native ReleaseObject retains exact spatial identity");
    ++release_calls;last_creature=c;release_a=a;release_b=b;return native_result;
}
void* SK_THISCALL to_data(void* p,uint32_t type) {
    check(p==&spatial && type==0x017F243B,"native spatial Cast receives verified interface");return item.data();
}
int hook(SkMod*,void* target,void* callback,void** original) {
    if(target==rejected_target) return 1; // a failed hook allocation supplies no trampoline
    if(detours.count(target)) {++duplicates;return 3;} // MH_ERROR_ALREADY_CREATED
    detours[target]=callback;*original=target;return 0;
}
uintptr_t symbol(const char* n) {
    if(!std::strcmp(n,"SporeCoop::Creature_HoldObject") || !std::strcmp(n,"SporeCoop::Adventure::PickUpItem"))
        return reinterpret_cast<uintptr_t>(native_hold);
    if(!std::strcmp(n,"SporeCoop::Creature_ReleaseObject") || !std::strcmp(n,"SporeCoop::Adventure::DropItem"))
        return reinterpret_cast<uintptr_t>(native_release);
    return 0;
}
void log(SkMod*,int,const char*,...) {}
double time() {return 10;}
bool invoke(void* c,uint32_t slot=2,bool replace=true) {
    return reinterpret_cast<player_rules::Hold_t>(detours.at(reinterpret_cast<void*>(native_hold)))(c,&spatial,slot,replace);
}
bool other_interceptor(void*,void*,uint32_t,bool,player_rules::HoldNative,bool&) {return false;}
void pump(net::Session& host,net::Session& guest,int count=8) {
    for(int i=0;i<count;++i) {
        guest.update();host.update();guest.update();std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
}
} // namespace fixture

namespace game {
bool in_creature_stage() {return fixture::creature_stage;}
void* avatar() {return fixture::captain.data();}
uint32_t noun_id(void*) {return 0x02A8FB3F;}
bool object_position(void*,net::Vec3& p) {p={1,2,3};return true;}
bool listed(const void* p,uint32_t) {return p!=nullptr;}
bool alive(const LiveRef& ref) {return ref.p && skg::field<uint32_t>(ref.p,0x24)==ref.serial;}
bool is_puppet(void*) {return false;}
}
namespace adventure_targeting {void reset() {}}
namespace adventure_world_vehicles {void reset() {} void shutdown() {}}

int main(int argc,char** argv)
{
    using namespace fixture;
    SkHost api{};api.sym=symbol;api.hook_create=hook;api.log=log;api.time_seconds=time;
    spatial_vt[0xB8/4]=reinterpret_cast<void*>(to_data);
    field<uint32_t>(mode.data(),0xCC)=2;field<void*>(mode.data(),0x78)=play.data();
    field<void*>(mode.data(),0x74)=data.data();field<int>(play.data(),0xB8)=0;
    field<void*>(item.data(),0x18)=marker.data();field<int>(item.data(),0x1C)=4;
    field<uint32_t>(item.data(),0x24)=123;field<int>(marker.data(),0)=4;field<uint32_t>(marker.data(),0x228)=5;
    net::Session session,guest;std::string error;
    check(session.host(0,"Host",error),"real Session hosts the campaign/GA callback context");
    check(guest.join("127.0.0.1:"+std::to_string(session.port()),"Guest",error),"real guest joins the native fixture");
    for(int i=0;i<1000 && !guest.connected();++i) pump(session,guest,1);
    check(guest.connected(),"real guest has completed the compatible handshake");
    player_rules::H=&api;player_rules::C.sess=&session;
    adventure_world::H=&api;adventure_world::C.sess=&session;
    adventure_world::C.run=[] {return current_run;};adventure_world::C.active=[] {return enabled;};
    adventure_world::C.held=[] {return paused;};adventure_world::C.captain_can_act=[](uint8_t) {return may_act;};
    adventure_world::C.captain_of=[](uint8_t id)->void* {return id==0?captain.data():partner.data();};
    adventure_world::a_Mode=reinterpret_cast<uintptr_t>(&mode_ptr);
    adventure_world::a_GetMarker=1; // the verified direct native marker path is used
    player_rules::init_carry_hooks();
    bool old=argc>1 && !std::strcmp(argv[1],"--old");
    bool bound=old?api.hook_create(nullptr,reinterpret_cast<void*>(native_hold),reinterpret_cast<void*>(adventure_world::pick_item),
                                  reinterpret_cast<void**>(&adventure_world::o_Pick))==0:
                    adventure_world::bind_item_hooks(symbol("SporeCoop::Adventure::PickUpItem"),
                                                    symbol("SporeCoop::Adventure::DropItem"));
    check(bound,"GA binds the existing HoldObject alias without a duplicate native hook");
    check(duplicates==0,"all native targets have a single hook owner");
    check(detours.size()==2,"the two verified native entries each have exactly one detour");
    adventure_world::g_ok=bound;
    enabled=false;
    creature_stage=true;unsigned before=player_rules::g_hold_tx;
    check(invoke(captain.data()),"the actual campaign callback accepts native pickup");
    check(player_rules::g_hold_tx==before+1,"campaign pickup still emits its upstream carry relay");
    check(hold_calls==1 && last_creature==captain.data() && last_slot==2 && last_replace,
          "campaign callback forwards native this/slot/replace exactly once");
    if(old) {std::printf("adventure_shared_hold_tests: FAIL (%d failure(s), old duplicate owner)\n",failures);return 1;}
    player_rules::g_in_relay=true;
    check(invoke(captain.data(),0xFFFFFFFEu,false),"campaign replay reaches the native hold");
    check(last_slot==0xFFFFFFFEu && !last_replace && player_rules::g_hold_tx==before+1,
          "campaign replay retains stack bits without another carry message");
    player_rules::g_in_relay=false;
    native_result=false;check(!invoke(captain.data()),"native campaign failure is retained");native_result=true;
    check(player_rules::g_hold_tx==before+1,"failed campaign hold sends no relay");
    auto release=reinterpret_cast<player_rules::Release_t>(detours.at(reinterpret_cast<void*>(native_release)));
    unsigned drops=player_rules::g_drop_tx;
    check(release(captain.data(),&spatial,3,7),"campaign release uses its existing detour");
    check(release_a==3 && release_b==7 && player_rules::g_drop_tx==drops+1,
          "campaign release preserves both native int arguments and the drop relay");

    enabled=true;creature_stage=false;paused=true;
    int native_before=hold_calls;
    check(!invoke(captain.data()),"GA shared hold blocks native mutation");
    check(hold_calls==native_before,"held GA pickup never reaches native HoldObject");
    paused=false;adventure_world::g_applying=true;
    check(invoke(captain.data(),5,false),"GA authoritative application forwards to native");
    check(hold_calls==native_before+1 && last_slot==5 && !last_replace && !adventure_world::g_interaction_sequence,
          "GA application forwards once without echoing an interaction");
    adventure_world::g_applying=false;
    check(invoke(npc_actor.data()),"unowned NPC hold retains native behavior");
    check(hold_calls==native_before+2 && !adventure_world::g_interaction_sequence,
          "NPC passthrough sends no captain request");
    check(invoke(captain.data()),"host captain pickup uses the actual GA handler");
    check(hold_calls==native_before+3 && adventure_world::g_interaction_sequence==1,
          "host pickup calls native once and emits one synchronized interaction");
    check(player_rules::g_hold_tx==before+1,"GA pickup never emits the campaign carry relay");
    native_result=false;check(!invoke(captain.data()),"native host pickup failure is retained");native_result=true;
    check(adventure_world::g_interaction_sequence==1,"failed host pickup sends no GA interaction");
    may_act=false;native_before=hold_calls;check(!invoke(captain.data()),"downed host captain cannot pick up");may_act=true;
    check(hold_calls==native_before,"ineligible captain causes no native mutation");
    field<void*>(partner.data(),0xE40)=partner_slot.data();field<void*>(partner.data(),0xE44)=partner_slot.data()+partner_slot.size();
    field<void*>(partner_slot.data(),0x20)=&spatial;
    check(!invoke(captain.data()),"host rejects an item already held by another captain");
    check(hold_calls==native_before,"competing captain ownership causes no native mutation");
    field<void*>(partner_slot.data(),0x20)=nullptr;

    pump(session,guest);
    unsigned requests=0;
    session.cb.custom=[&](uint8_t type,uint8_t from,net::Reader& r) {
        check(type==net::MSG_ADVENTURE && from==guest.my_id(),"guest sends on the current adventure route");
        check(r.u8()==adventure_world::Interaction && adventure_world::get_run(r)==current_run,
              "guest pickup carries the current run and Interaction kind");
        ++requests;
    };
    adventure_world::C.sess=&guest;
    adventure_world::g_native.push_back({item.data(),&spatial,nullptr,5,123,game::noun_id(item.data())});
    adventure_world::g_bindings[{5,123}]={item.data(),123,0,{},{}};
    auto interactions=adventure_world::g_interaction_sequence;
    check(invoke(partner.data()),"local guest pickup is submitted to the host");
    check(hold_calls==native_before && adventure_world::g_interaction_sequence==interactions+1,
          "guest sends one request and does not predict native attachment");
    pump(session,guest);
    check(requests==1,"one actual reliable pickup request reaches the host");
    check(!invoke(captain.data()),"guest cannot submit a different captain's pickup");
    check(hold_calls==native_before,"remote captain rejection never reaches native HoldObject");
    paused=true;check(!invoke(partner.data()),"guest respects the shared authoring/gameplay hold");paused=false;
    check(adventure_world::o_Pick(partner.data(),&spatial,1,false),"accepted GA attachment uses the validated native forward");
    check(hold_calls==native_before+1 && adventure_world::g_interaction_sequence==interactions+1,
          "accepted attachment calls native once without replaying the guest request");
    check(adventure_world::o_Drop(partner.data(),&spatial,11,13),"GA drop uses the existing release owner's native forward");
    check(release_a==11 && release_b==13 && player_rules::g_drop_tx==drops+1,
          "GA release preserves both int stack arguments without a campaign relay");

    auto hold_address=symbol("SporeCoop::Creature_HoldObject"),drop_address=symbol("SporeCoop::Creature_ReleaseObject");
    check(!player_rules::intercept_adventure_hold(hold_address,other_interceptor),"another handler cannot replace the active GA owner");
    player_rules::clear_adventure_hold(other_interceptor);
    check(player_rules::g_adventure_hold==adventure_world::intercept_pick,"a stale removal cannot clear the current GA handler");
    check(!player_rules::hold_original(hold_address+1) && !player_rules::release_original(drop_address+1),
          "native forwards reject mismatched entry identities");
    adventure_world::shutdown();
    check(!player_rules::g_adventure_hold && !adventure_world::available(),"shutdown removes GA dispatch before context teardown");
    creature_stage=true;
    check(invoke(captain.data()),"campaign carry remains usable after GA shutdown");
    check(player_rules::g_hold_tx==before+2,"shutdown restores the unchanged upstream campaign relay");
    detours.clear();rejected_target=reinterpret_cast<void*>(native_hold);
    player_rules::init_carry_hooks();
    check(!adventure_world::bind_item_hooks(hold_address,drop_address),"missing native hold owner fails closed");
    detours.clear();rejected_target=reinterpret_cast<void*>(native_release);
    player_rules::init_carry_hooks();
    check(!adventure_world::bind_item_hooks(hold_address,drop_address),"missing native release owner fails closed");
    check(!player_rules::g_adventure_hold && duplicates==0,"failed readiness never registers a handler or another native hook");
    std::printf("adventure_shared_hold_tests: %s (%d failure(s))\n",failures?"FAIL":"PASS",failures);
    return failures?1:0;
}
