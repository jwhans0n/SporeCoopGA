// Pure policy for the verified native transform handles, without native calls.
#pragma once
#include <array>
#include <cstdint>

namespace editor_native {
inline bool supported_tool_auxiliary(uint32_t action,uintptr_t auxiliary,
                                     const std::array<uintptr_t,3>& rotation_handles,
                                     uintptr_t ball=0,uintptr_t destination=0,uintptr_t secondary_rotation=0){
    if(!auxiliary)return true;
    if(action!=1 && action!=6)return false;
    for(auto handle:rotation_handles)if(auxiliary==handle)return true;
    if(action==1 && (auxiliary==ball || auxiliary==destination || auxiliary==secondary_rotation))return true;
    return false;
}
} // namespace editor_native
