// Actual Win32 native UI adapter: save through Done, not through invented XP.
#include <array>
#include <cstdio>
#include <map>
#include <string>
#include "adventure_results.h"
#include "sui.h"
#include "sporekit_game.hpp"

namespace {
using skg::field;
std::array<uint8_t,0xD0> mode{};
std::array<uint8_t,0x108> play{};
std::array<uint8_t,0x130> display{};
std::array<uint8_t,0xF8> results{};
std::array<uint8_t,0x160> cinematic{};
std::array<uint8_t,0x18> layout{};
std::array<uint8_t,0x8C> container{};
uint8_t* mode_ptr=mode.data();
int window,button,other;
bool shown=true,publish=false,applied=false;
uint32_t button_flags=3;
uint64_t run=7;
int failures,clicked,ended,notices;
const char* hash="wrong";
std::map<std::string,uintptr_t> symbols;
void check(bool value,const char* text) { if(!value) { ++failures; std::printf("FAIL: %s\n",text); } }
uintptr_t sym(const char* name) { return symbols[name]; }
const char* exe_hash() { return hash; }
void* SK_CDECL get_cinematic() { return cinematic.data(); }
sui::Win SK_THISCALL get_window(void* self)
{
    check(self==layout.data(),"container lookup receives UILayout, distinct from IWindow");
    return field<sui::Win>(container.data(),0x88);
}
void SK_THISCALL end(void* self)
{
    check(self==cinematic.data(),"terminal cinematic uses the exact native singleton this pointer");
    ++ended; field<uint32_t>(cinematic.data(),0x2C)=0;
    field<int>(play.data(),0x94)=2;
}
bool SK_THISCALL accept(void* self,void* source,const sui::Msg* message)
{
    ++clicked;
    check(self==results.data() && source==&button,"native UI handler preserves complete results and button arguments");
    check(message && message->source==&button && !message->field_04 && message->type==sui::kMsgComponentActivated &&
          message->a==0x07C79948 && !message->b && !message->c && !message->d,"Done receives the exact native UTFWin message layout");
    field<int>(results.data(),0xD4)=10;
    if(publish) adventure_results::accepted(play.data(),1);
    return true;
}
}
namespace game { uint32_t mode_id() { return 0x01654C10; } }
namespace sui {
bool visible(Win object) {
    check(object==&window || object==&button,"window vtables never receive a UILayout or its container");
    return shown && (object==&window || object==&button);
}
uint32_t flags(Win object) { return object==&button ? button_flags : 3; }
Win find(Win object,uint32_t id) { return object==&window && id==0x07C79948 ? &button : nullptr; }
}
int main()
{
    symbols={{"App::cScenarioMode::ptr",reinterpret_cast<uintptr_t>(&mode_ptr)},
             {"SporeCoop::Adventure::ResultsHandleUIMessage",reinterpret_cast<uintptr_t>(&accept)},
             {"SporeCoop::cCinematicManager_EndCinematic",reinterpret_cast<uintptr_t>(&end)},
             {"Simulator::cCinematicManager::Get",reinterpret_cast<uintptr_t>(&get_cinematic)},
             {"UTFWin::UILayout::GetContainerWindow",reinterpret_cast<uintptr_t>(&get_window)}};
    SkHost host{}; host.exe_sha256=exe_hash; host.sym=sym;
    adventure_results::Ctx context;
    context.run=[] { return run; };
    context.applied=[](uint64_t id,bool success) { return applied && id==run && success==(field<int>(play.data(),0x90)==5); };
    context.feed=[](const std::string&) { ++notices; };
    check(!adventure_results::init(&host,nullptr,context),"wrong executable cannot activate native result acceptance");
    hash="dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37";
    auto saved_handler=symbols["SporeCoop::Adventure::ResultsHandleUIMessage"];
    symbols["SporeCoop::Adventure::ResultsHandleUIMessage"]=0;
    check(!adventure_results::init(&host,nullptr,context),"missing verified native UI entry fails closed");
    symbols["SporeCoop::Adventure::ResultsHandleUIMessage"]=saved_handler;
    check(adventure_results::init(&host,nullptr,context),"reviewed native UI capabilities activate");
    field<uint32_t>(mode.data(),0xCC)=2; field<void*>(mode.data(),0x78)=play.data();
    field<void*>(mode.data(),0x6C)=display.data(); field<void*>(display.data(),0x12C)=results.data();
    field<void*>(results.data(),0x10)=play.data(); field<void*>(results.data(),0x18)=layout.data();
    field<void*>(layout.data(),0x14)=container.data(); field<void*>(container.data(),0x88)=&window;
    field<int>(play.data(),0x90)=3;
    check(!adventure_results::prepare_exit() && !clicked && !ended,"gameplay cannot be treated as a saved result");
    field<int>(play.data(),0x90)=5;
    check(!adventure_results::prepare_exit() && !ended,"actual terminal readback is mandatory before UI work");
    applied=true; field<int>(play.data(),0x94)=1;
    field<uint32_t>(cinematic.data(),0x2C)=1; field<uint32_t>(cinematic.data(),0x148)=90;
    check(!adventure_results::prepare_exit() && ended==1 && !clicked,"terminal cinematic completes before the ordinary result button");
    check(!adventure_results::prepare_exit() && clicked==1,"one ordinary Done request waits for native saved-choice publication");
    for(int i=0;i<1000;++i) check(!adventure_results::prepare_exit(),"a pending native save remains pending");
    check(clicked==1,"repeated exit polling cannot repeat native captain progression");
    adventure_results::accepted(&other,1); adventure_results::accepted(play.data(),3);
    check(!adventure_results::prepare_exit(),"foreign play objects and invalid choices cannot acknowledge saving");
    adventure_results::accepted(play.data(),1);
    field<void*>(results.data(),0x10)=nullptr;
    check(adventure_results::prepare_exit(),"native saved-choice ownership survives native panel cleanup");
    adventure_results::reset(); ++run; field<int>(play.data(),0x90)=6;
    field<void*>(results.data(),0x10)=play.data(); field<int>(results.data(),0xD4)=4;
    button_flags=1;
    check(!adventure_results::prepare_exit() && clicked==1 && notices==1,"mandatory reward selection cannot be skipped by host exit");
    check(!adventure_results::prepare_exit() && notices==1,"reward guidance is emitted once per run");
    field<int>(results.data(),0xD4)=0;
    check(!adventure_results::prepare_exit() && clicked==1,"disabled native Done is never forced");
    button_flags=3; shown=false;
    check(!adventure_results::prepare_exit() && clicked==1,"hidden native Done is never forced");
    shown=true; field<void*>(results.data(),0x10)=&other;
    check(!adventure_results::prepare_exit() && clicked==1,"a replacement results object cannot save the previous attempt");
    field<void*>(results.data(),0x10)=play.data(); publish=true; field<int>(results.data(),0xD4)=9;
    check(adventure_results::prepare_exit() && clicked==2,"native Done accepts a ready result and synchronous saved callback once");
    for(int i=0;i<1000;++i) check(adventure_results::prepare_exit(),"accepted native result remains ready for cleanup");
    check(clicked==2,"accepted result cannot repeat its native button path");
    adventure_results::reset();
    check(!adventure_results::prepare_exit(),"native initialization/reset invalidates saved-result ownership");
    std::printf("native adventure result saves: %d failure(s)\n",failures);
    return failures ? 1 : 0;
}
