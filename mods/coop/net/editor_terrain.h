// Canonical primitive terrain recipe records. No native pointers or layouts are
// reconstructed from mouse input. Native Transform fields are retained exactly.
#pragma once
#include "editor_document.h"
#include <cmath>
#include <cstring>

namespace editor {
inline constexpr uint32_t kTerrainRecordFormat=0x31524654; // TFR1
inline constexpr size_t kTerrainTransformBytes=56,kTerrainModelBytes=68,kTerrainEffectBytes=84;
inline uint32_t terrain_word(const std::vector<uint8_t>& b,size_t at){
    return uint32_t(b[at])|(uint32_t(b[at+1])<<8)|(uint32_t(b[at+2])<<16)|(uint32_t(b[at+3])<<24);
}
inline float terrain_float(const std::vector<uint8_t>& b,size_t at){
    const auto bits=terrain_word(b,at);float v;std::memcpy(&v,&bits,4);return v;
}
inline bool valid_terrain_payload(TerrainKind kind,const StructuredValue& v){
    if(kind>TerrainKind::Model || v.format!=kTerrainRecordFormat || !v.references.empty() ||
       v.bytes.size()!=(kind==TerrainKind::Effect?kTerrainEffectBytes:kTerrainModelBytes))return false;
    // Effect keys may intentionally omit group/type and use native defaults.
    // Their nonzero effect instance is always required. Models need a typed key.
    if(!terrain_word(v.bytes,0) || (kind==TerrainKind::Model && !terrain_word(v.bytes,4)))return false;
    const auto flags=uint16_t(v.bytes[12])|(uint16_t(v.bytes[13])<<8);
    if(flags&~7u)return false;
    for(size_t at=16;at<68;at+=4)if(!std::isfinite(terrain_float(v.bytes,at)))return false;
    if(std::abs(terrain_float(v.bytes,16))>1e7f || std::abs(terrain_float(v.bytes,20))>1e7f ||
       std::abs(terrain_float(v.bytes,24))>1e7f || terrain_float(v.bytes,28)<=0 || terrain_float(v.bytes,28)>1e6f)return false;
    if(kind==TerrainKind::Effect){
        // Time/intensity/water are float data; the seed is an opaque uint32.
        for(size_t at:{size_t(68),size_t(76),size_t(80)})if(!std::isfinite(terrain_float(v.bytes,at)))return false;
    }
    return true;
}
} // namespace editor
