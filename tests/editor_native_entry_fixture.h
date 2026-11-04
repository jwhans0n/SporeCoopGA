#pragma once
#include "../mods/coop/world_editor_entry.h"

namespace native_entry_fixture {
using world_editor_entry::Cinematic;
Cinematic cine;
unsigned preflights=0,ends=0,resets=0,loading=0,switches=0,activations=0;
uint32_t group=0x408A0000;
std::array<uint8_t,0x30> request;
std::function<void()> on_end,on_preflight,on_group;
bool toolbar=false;
void* SK_CDECL reset_request(){++resets;return request.data();}
void* SK_CDECL get_loading(){return t_loading.data();}
void* SK_CDECL get_app(){return t_mode.data();}
void* SK_CDECL get_manager(){return t_display.data();}
void SK_THISCALL show(void*,bool,uint32_t mode,bool){CHECK(mode==modes::kScenario);++loading;}
bool SK_THISCALL switch_state(void*,uint32_t state){CHECK(state==0x21851EBE);++switches;return true;}
bool SK_THISCALL activate(void*,uint32_t mode){CHECK(mode==modes::kScenario);++activations;toolbar=!cine.active;t_game_mode_id=mode;return true;}
uintptr_t SK_CDECL symbol(const char* name){
    const std::string value=name;
    if(value=="SporeCoop::Editor::CreationGroup"){
        if(on_group){auto callback=std::move(on_group);callback();}
        return reinterpret_cast<uintptr_t>(&group);
    }
    if(value=="SporeCoop::Adventure::ResetRequest")return reinterpret_cast<uintptr_t>(reset_request);
    if(value=="SporeCoop::Adventure::LoadingScreenGet")return reinterpret_cast<uintptr_t>(get_loading);
    if(value=="SporeCoop::Adventure::LoadingScreenShow")return reinterpret_cast<uintptr_t>(show);
    if(value=="App::IStateManager::Get")return reinterpret_cast<uintptr_t>(get_app);
    if(value=="App::cStateManager::SwitchToState")return reinterpret_cast<uintptr_t>(switch_state);
    if(value=="App::IGameModeManager::Get")return reinterpret_cast<uintptr_t>(get_manager);
    if(value=="App::cGameModeManager::SetActiveMode")return reinterpret_cast<uintptr_t>(activate);
    return 0;
}
void test(){
    for(unsigned test=0;test<13;++test){
        const unsigned kind=test==0?1:test==1?0:test;
        reset();native_owner_thread=GetCurrentThreadId();t_game_mode_id=modes::kMenu;
        SkHost host=*H;host.sym=symbol;const auto* old_host=H;H=&host;
        cine={1,kind==0?0u:1u,kind==2?0x12345678:world_editor_entry::kMenuIntro};
        preflights=ends=resets=loading=switches=activations=0;request.fill(0);toolbar=false;
        editor::ResourceKey key{7,0x366A930D,group};const auto wanted=key;
        on_preflight={};on_end={};on_group={};
        if(kind==3)on_end=[]{++generation;};
        if(kind==4)on_end=[]{supported=false;};
        if(kind==5)on_end=[]{t_game_mode_id=modes::kScenario;};
        if(kind==6)on_end=[&]{key={9,0x366A930D,group};};
        if(kind==7)on_end=[&]{std::string nested;CHECK(!enter(wanted,nested));CHECK(nested.find("already")!=std::string::npos);};
        if(kind==8)on_preflight=[]{++generation;};
        if(kind==9)on_group=[]{++generation;};
        if(kind==10)on_group=[&]{++generation;H=&host;supported=true;};
        if(kind==11)t_game_mode_callback=[]{++generation;};
        if(kind==12)t_game_mode_callback=[&]{++generation;H=&host;supported=true;};
        std::string error;const bool ready=enter(key,error);
        const bool positive=kind==0||kind==1||kind==6||kind==7;
        CHECK(ready==positive);
        if(positive){CHECK(toolbar&&resets==1&&loading==1&&switches==1&&activations==1);CHECK(skg::field<editor::ResourceKey>(request.data(),0)==wanted);CHECK(error.empty());}
        else{CHECK(!toolbar&&!resets&&!loading&&!switches&&!activations);CHECK(!error.empty());}
        CHECK(preflights==(kind>=9?0u:1u));
        if(kind==2||kind==8)CHECK(!ends);
        on_preflight={};on_end={};on_group={};H=old_host;
    }
    reset();
}
} // namespace native_entry_fixture

namespace world {
bool prepare_editor_entry(std::function<bool()> current,std::string& error){
    using namespace native_entry_fixture;
    ++preflights;
    if(on_preflight)on_preflight();
    return world_editor_entry::prepare(std::move(current),[]{return cine;},[](uintptr_t manager){
        CHECK(manager==cine.manager);++ends;cine.active=0;cine.id=0;
        if(on_end)on_end();
    },error);
}
}
