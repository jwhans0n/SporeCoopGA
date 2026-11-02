// Exact native placement-bound receipts. Code changes affect process memory
// only and are retired with the active shared project's authoring context.
#pragma once
#include <array>
#include <cstdint>
#include <cstring>
#include <string>
#include <windows.h>

namespace editor_native_complexity {
inline constexpr uint32_t kNativePlacements=1024,kExtendedPlacements=4096;
class Bounds {
    std::array<uint8_t*,2> sites{};
    bool enabled=false;
    static bool matches(const uint8_t* site,uint32_t limit,size_t index){
        uint32_t immediate=0;if(!site)return false;std::memcpy(&immediate,site+1,4);
        const std::array<uint8_t,6> branch{0x0F,0x83,uint8_t(index?0x9D:0xD7),uint8_t(index?0:1),0,0};
        return site[0]==0x3D && immediate==limit && !std::memcmp(site+5,branch.data(),branch.size());
    }
    static bool write(uint8_t* site,uint32_t limit){
        DWORD prior=0;if(!VirtualProtect(site,7,PAGE_EXECUTE_READWRITE,&prior))return false;
        uint32_t before=0;std::memcpy(&before,site+1,4);
        std::memcpy(site+1,&limit,4);FlushInstructionCache(GetCurrentProcess(),site,7);
        DWORD unused=0;if(VirtualProtect(site,7,prior,&unused)!=FALSE)return true;
        std::memcpy(site+1,&before,4);FlushInstructionCache(GetCurrentProcess(),site,7);
        VirtualProtect(site,7,prior,&unused);return false;
    }
public:
    bool initialize(uintptr_t create,uintptr_t clone){
        sites={reinterpret_cast<uint8_t*>(create),reinterpret_cast<uint8_t*>(clone)};enabled=false;
        return matches(sites[0],kNativePlacements,0) && matches(sites[1],kNativePlacements,1);
    }
    bool set(bool desired,std::string& error){
        if(desired==enabled)return true;
        const auto before=enabled?kExtendedPlacements:kNativePlacements;
        const auto after=desired?kExtendedPlacements:kNativePlacements;
        if(!matches(sites[0],before,0) || !matches(sites[1],before,1)){error="Native placement bounds conflict with another code patch";return false;}
        if(!write(sites[0],after)){error="Cannot update the first native placement bound";return false;}
        if(!write(sites[1],after)){
            write(sites[0],before);error="Cannot update the second native placement bound";return false;
        }
        enabled=desired;return true;
    }
    bool active()const{return enabled;}
};
} // namespace editor_native_complexity
