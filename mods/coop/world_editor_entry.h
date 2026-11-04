// Exact menu-intro preflight policy. Callers provide fresh observations and own
// the main-thread/outside-Present lifetime fence; no Native owner crosses calls.
#pragma once
#include <cstdint>
#include <string>

namespace world_editor_entry {
inline constexpr uint32_t kMenuIntro=0x8915F9BBu;
struct Cinematic {
    uintptr_t manager=0;
    uint32_t active=0,id=0;
    bool operator==(const Cinematic&)const=default;
};

template<class Current,class Observe,class End>
bool prepare(Current current,Observe observe,End end,std::string& error){
    const auto expired=[&]{error="The native Adventure Editor entry context changed";return false;};
    if(!current())return expired();
    const auto before=observe();
    if(!current())return expired();
    // The final observation follows the callback-bearing fence. A same-owner
    // policy callback cannot swap the cinematic between authority and use.
    if(observe()!=before)return expired();
    if(!before.manager){error="The native cinematic manager is not initialized";return false;}
    if(before.active){
        if(before.id!=kMenuIntro){error="Wait for the current cinematic to finish before entering the Adventure Editor";return false;}
        if(!current() || observe()!=before)return expired();
        end(before.manager);
    }
    if(!current())return expired();
    const auto after=observe();
    if(!current() || observe()!=after)return expired();
    if(after.manager!=before.manager || after.active || (before.active && after.id)){
        error="The Galaxy menu intro did not finish before Adventure Editor entry";return false;
    }
    error.clear();return true;
}
} // namespace world_editor_entry
