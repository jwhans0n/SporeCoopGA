// Verified native marker +24 scale-range companion and legacy schema policy.
#pragma once
#include "net/editor_document.h"
#include <cmath>
#include <limits>

namespace editor_native {
inline constexpr uint32_t kRelativeScaleProperty=0x071147E9;
inline const std::map<uint32_t,editor::PropertySpec>& relative_scale_schema(){
    static const std::map<uint32_t,editor::PropertySpec> schema{{kRelativeScaleProperty,{editor::ValueKind::Number,
        std::numeric_limits<float>::lowest(),std::numeric_limits<float>::max(),0,false,false}}};
    return schema;
}
inline bool legacy_scale_blocked(uint32_t action,bool tracked){return action==6 && !tracked;}
inline bool clone_scale_blocked(uint32_t action,bool gesture,bool clone_source){
    return action==6 && gesture && clone_source;
}
inline bool relative_scale_mode(const editor::Document& document,bool& tracked,std::string& error){
    tracked=!document.schema.empty();
    if(tracked && document.schema!=relative_scale_schema()){
        error="Unsupported native placement-property schema";return false;
    }
    return true;
}
inline bool relative_scale_value(const editor::Placement& placement,bool tracked,float& value,std::string& error){
    if(!tracked){
        if(placement.properties.empty())return true;
        error="Legacy native placements cannot contain editable properties";return false;
    }
    auto it=placement.properties.find(kRelativeScaleProperty);
    const auto* number=it==placement.properties.end()?nullptr:std::get_if<float>(&it->second);
    if(placement.properties.size()!=1 || !number || !std::isfinite(*number)){
        error="Native placement requires its exact finite scale-range companion";return false;
    }
    value=*number;return true;
}
inline bool project_relative_scale(editor::Placement& placement,bool tracked,float value,std::string& error){
    if(!tracked)return true;
    if(!std::isfinite(value)){error="Native scale-range companion is not finite";return false;}
    placement.properties[kRelativeScaleProperty]=value;return true;
}
} // namespace editor_native
