// BUG-1275: actual GA handlers and shared dispatcher, with a controlled Civ slot.
// Actual Civ policy is qualified separately by native campaign smoke.
#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <cstdio>
#include <deque>
#include <functional>
#include <map>
#include <memory>
#include <string>
#include <thread>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "civ_logic.h"
#include "modes.h"
#include "net/session.h"
#include "sporekit_game.hpp"
#include "adventure_world_vehicles.h"
#include "native_behavior_tree.h"

namespace fixture {
int failures,duplicates,removes;
unsigned checks,native_calls;
bool active=false,host=false,live=true,paused=false,away=false,reject_owner=false,native_result=true;
bool away_fix=true;
net::Session* civ_session;
constexpr uintptr_t kVehicleVt=0x11223344;
uint64_t run=8;
uint32_t mode=modes::kCiv;
std::array<uint8_t,32> vehicle{},other{};
std::array<uint32_t,9> words{0x3E800000,0x89ABCDEF,0x01234567,0x11223344,0,0x44332211,
                              0xAA55AA55,0x55AA55AA,0xFEDCBA98};
std::map<uintptr_t,void*> detours;
void check(bool value,const char* what) {++checks;if(!value){++failures;std::printf("FAIL: %s\n",what);}}
bool SK_CDECL native_tree(uint32_t a,uint32_t b,uint32_t c,uint32_t d,uint32_t e,
                          uint32_t f,uint32_t g,uint32_t h,uint32_t i) {
    ++native_calls;
    check(std::array<uint32_t,9>{a,b,c,d,e,f,g,h,i}==words,"all nine native stack words retain their exact bits");
    return native_result;
}
void SK_THISCALL native_weapons(void*,uint32_t) {}
void SK_THISCALL native_timers(void*) {}
void SK_THISCALL native_actor(void*,void*,float) {}
uintptr_t symbol(const char* n) {
    std::string s=n;
    if(s=="SporeCoop::Adventure::TickBehaviorTree" || s=="Simulator::BehaviorTreeTickGeneric")return uintptr_t(native_tree);
    if(s=="SporeCoop::Adventure::TickVehicleWeapons")return uintptr_t(native_weapons);
    if(s=="SporeCoop::Adventure::TickVehicleTimers")return uintptr_t(native_timers);
    if(s=="SporeCoop::Adventure::ProcessActor")return uintptr_t(native_actor);
    return 0;
}
int create(SkMod*,void* target,void* callback,void** original) {
    if(target==reinterpret_cast<void*>(native_tree) && reject_owner)return 7;
    if(!detours.emplace(uintptr_t(target),callback).second){++duplicates;return 3;}
    *original=target;return 0;
}
int remove(SkMod*,void* target) {++removes;return detours.erase(uintptr_t(target))?0:1;}
const char* hash() {return "dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37";}
bool invoke(void* agent) {
    words[4]=uint32_t(uintptr_t(agent));
    auto callback=reinterpret_cast<decltype(&native_tree)>(detours.at(uintptr_t(native_tree)));
    return callback(words[0],words[1],words[2],words[3],words[4],words[5],words[6],words[7],words[8]);
}
bool other_filter(void*) {return true;}
bool civ_filter(void* agent) {
    bool guest=civ_session && civ_session->connected() && civ_session->role()==net::Role::Client;
    return !(agent && guest && mode==modes::kCiv && !civlogic::local_play(away_fix,guest,away) &&
             !IsBadReadPtr(agent,4) && *static_cast<uintptr_t*>(agent)==kVehicleVt);
}
bool SK_CDECL old_civ_tick(uint32_t a,uint32_t b,uint32_t c,uint32_t d,uint32_t agent,
                           uint32_t f,uint32_t g,uint32_t h,uint32_t i) {
    return civ_filter(reinterpret_cast<void*>(uintptr_t(agent))) && native_tree(a,b,c,d,agent,f,g,h,i);
}
void pump(net::Session& h,net::Session& g) {g.update();h.update();g.update();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
}

int main(int argc,char** argv)
{
    using namespace fixture;
    SkHost api{};api.sym=symbol;api.hook_create=create;api.hook_remove=remove;api.exe_sha256=hash;
    adventure_world_vehicles::Ctx context;
    context.active=[] {return active;};context.host=[] {return host;};context.run=[] {return run;};
    context.paused=[] {return paused;};context.is_mirrored=[](void* p) {return live && p==vehicle.data();};
    net::Session session,guest;std::string error;
    check(session.host(0,"Host",error),"real host Session starts");
    check(guest.join("127.0.0.1:"+std::to_string(session.port()),"Guest",error),"real guest Session joins");
    for(int i=0;i<1000 && !guest.connected();++i)pump(session,guest);
    check(guest.connected(),"current compatible guest completes handshake");
    civ_session=&guest;
    skg::field<uintptr_t>(vehicle.data(),0)=kVehicleVt;
    skg::field<uintptr_t>(other.data(),0)=0x55667788;
    bool civ_first=argc>1 && !std::strcmp(argv[1],"--civ-first");
    bool old=argc>1 && !std::strcmp(argv[1],"--old");
    auto bind_civ=[&] {return native_behavior_tree::subscribe(&api,nullptr,uintptr_t(native_tree),
                                                            native_behavior_tree::Scope::Civilization,civ_filter);};
    reject_owner=true;
    if(civ_first)check(!bind_civ(),"Civ slot rejects unavailable native tree ownership");
    else check(!adventure_world_vehicles::init(&api,nullptr,context) && removes==3,
               "failed shared owner rolls back all three private GA hooks");
    check(detours.empty() && !native_behavior_tree::available(uintptr_t(native_tree)),
          "failed allocation leaves no partial native capability");
    reject_owner=false;
    if(civ_first) {
        check(bind_civ(),"early Civ slot obtains the one native owner");
        check(adventure_world_vehicles::init(&api,nullptr,context),"later GA subscribes to the Civ-created owner");
    } else {
        check(adventure_world_vehicles::init(&api,nullptr,context),"early GA obtains the one native owner");
        if(old) {
            void* original=nullptr;
            check(api.hook_create(nullptr,reinterpret_cast<void*>(native_tree),reinterpret_cast<void*>(old_civ_tick),
                                  &original)==0,"later Civ attaches without a second native tree detour");
            check(duplicates==0,"the native nine-word behavior tree entry has one owner");
            std::printf("shared_behavior_tree_tests: FAIL (%d failure(s), old duplicate owner)\n",failures);
            return 1;
        }
        check(bind_civ(),"later Civ slot subscribes to the GA-created owner");
    }
    check(detours.size()==4 && duplicates==0,"both init orders create one shared tree and three private GA detours");

    unsigned before=native_calls;
    check(!invoke(vehicle.data()) && native_calls==before,"controlled Civ guest policy blocks native vehicle AI");
    away=true;
    check(invoke(vehicle.data()) && native_calls==before+1,"Civ host-away local play resumes native vehicle AI");
    away_fix=false;
    check(!invoke(vehicle.data()) && native_calls==before+1,"Civ's existing away switch preserves its old frozen behavior");
    away_fix=true;away=false;
    civ_session=&session;
    check(invoke(vehicle.data()),"Civ host retains native tree simulation");
    civ_session=&guest;
    check(invoke(other.data()),"Civ nonvehicle agents retain native behavior");
    check(invoke(nullptr),"native null-agent path remains intact");
    check(invoke(reinterpret_cast<void*>(1)),"unreadable Civ agent is passed to native without a policy dereference");
    for(uint32_t stage:{modes::kCreature,modes::kTribe,modes::kSpace,modes::kScenario,0u}) {
        mode=stage;check(invoke(vehicle.data()),"Civ policy never blocks another stage");
    }

    mode=modes::kScenario;active=true;before=native_calls;
    auto blocked=adventure_world_vehicles::counters().trees;
    check(!invoke(vehicle.data()) && native_calls==before,"actual GA guest mirror policy owns vehicle AI");
    check(adventure_world_vehicles::counters().trees==blocked+1,"GA rejection is counted once");
    check(invoke(other.data()),"GA unbound native agents retain simulation");
    live=false;check(invoke(vehicle.data()),"retired GA mirror retains native cleanup");live=true;
    host=true;check(invoke(vehicle.data()),"GA host retains authoritative vehicle simulation");host=false;
    run=0;check(invoke(vehicle.data()),"uncommitted GA run does not filter native AI");run=8;
    active=false;check(invoke(vehicle.data()),"inactive GA does not filter native AI");active=true;
    paused=true;host=true;before=native_calls;
    check(!invoke(vehicle.data()) && !invoke(other.data()) && native_calls==before,
          "shared GA hold preserves its existing host and nonmirror tree fence");
    check(invoke(nullptr),"GA hold retains the native null-agent path");
    paused=false;
    native_result=false;check(!invoke(vehicle.data()),"native false AL result survives forwarding");native_result=true;
    check(invoke(vehicle.data()),"native true AL result survives forwarding");
    before=native_calls;
    for(int i=0;i<10000;++i)check(invoke(vehicle.data()),"repeated nine-word cdecl forwarding retains the bool result");
    check(native_calls==before+10000,"native forward runs exactly once per accepted tree tick");

    using native_behavior_tree::Scope;
    check(!native_behavior_tree::subscribe(&api,nullptr,uintptr_t(native_tree)+1,Scope::Civilization,other_filter),
          "wrong alias identity cannot bind a filter");
    SkHost different=api;
    check(!native_behavior_tree::subscribe(&different,nullptr,uintptr_t(native_tree),Scope::Civilization,civ_filter),
          "another native host cannot reuse this owner's trampoline");
    check(!native_behavior_tree::subscribe(&api,nullptr,uintptr_t(native_tree),Scope::Civilization,other_filter),
          "another filter cannot replace the current Civ owner");
    native_behavior_tree::unsubscribe(Scope::Civilization,other_filter);
    adventure_world_vehicles::shutdown();mode=modes::kCiv;host=false;
    check(!invoke(vehicle.data()),"GA shutdown preserves the live Civ policy");
    native_behavior_tree::unsubscribe(Scope::Civilization,civ_filter);
    check(invoke(vehicle.data()),"final consumer teardown restores native passthrough");
    check(detours.size()==4 && duplicates==0,"shared native hook is retained until module unload without recreating it");
    std::printf("shared_behavior_tree_tests: %s (%u checks, %d failure(s))\n",failures?"FAIL":"PASS",checks,failures);
    return failures?1:0;
}
