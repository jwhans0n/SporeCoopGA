// Verified native marker +24 scale-range companion and legacy schema policy.
#pragma once
#include "net/editor_document.h"
#include "editor_native_cast.h"
#include <cmath>
#include <algorithm>
#include <limits>
#include <bit>

namespace editor_native {
inline constexpr uint32_t kRelativeScaleProperty=0x071147E9;
inline const std::map<uint32_t,editor::PropertySpec>& relative_scale_schema(){
    static const std::map<uint32_t,editor::PropertySpec> schema{{kRelativeScaleProperty,{editor::ValueKind::Number,
        std::numeric_limits<float>::lowest(),std::numeric_limits<float>::max(),0,false,false}}};
    return schema;
}
inline constexpr uint32_t kElevationProperty = 0x454C4556;
inline constexpr uint32_t kSoundPitchProperty = 0x078D5A93;
inline constexpr uint32_t kSoundGainProperty = 0x078D5A94;
inline constexpr uint32_t kDestinationPositionProperty = 0x44504F53;
inline constexpr uint32_t kDestinationRotationProperty = 0x44524F54;
inline constexpr uint32_t kDestinationScaleProperty = 0x4453434C;
inline const std::map<uint32_t,editor::PropertySpec>& placement_property_schema() {
    static const auto schema=[] {
        auto result=relative_scale_schema();
        result[kElevationProperty]={editor::ValueKind::Number,-1e7,1e7,0,true,false};
        result[kSoundPitchProperty]={editor::ValueKind::Number,0,1e6,0,true,false};
        result[kSoundGainProperty]={editor::ValueKind::Number,0,1e6,0,true,false};
        result[kDestinationPositionProperty]={editor::ValueKind::Position,-1e7,1e7,0,true,false};
        result[kDestinationRotationProperty]={editor::ValueKind::Rotation,-1,1,0,true,false};
        result[kDestinationScaleProperty]={editor::ValueKind::Number,0.000001,1000,0,true,false};
        return result;
    }();
    return schema;
}
inline const std::map<uint32_t,editor::PropertySpec>& legacy_extended_schema(){
    static const auto schema=[] {auto result=placement_property_schema();result.erase(kRelativeScaleProperty);return result;}();
    return schema;
}
inline bool legacy_scale_blocked(uint32_t action,bool tracked){return action==6 && !tracked;}
inline bool clone_scale_blocked(uint32_t action,bool gesture,bool clone_source){
    return action==6 && gesture && clone_source;
}
inline bool relative_scale_mode(const editor::Document& document,bool& tracked,std::string& error){
    if(!cast_schema(document,error))return false;
    tracked=document.schema.contains(kRelativeScaleProperty);
    if(!document.schema.empty() && document.schema!=relative_scale_schema() && document.schema!=placement_property_schema() && document.schema!=legacy_extended_schema()){
        error="Unsupported native placement-property schema";return false;
    }
    return true;
}
inline bool relative_scale_value(const editor::Placement& placement,bool tracked,float& value,std::string& error){
    if(!tracked){
        if(std::all_of(placement.properties.begin(),placement.properties.end(),[](const auto& row){return legacy_extended_schema().contains(row.first);}))return true;
        error="Legacy native placements cannot contain editable properties";return false;
    }
    auto it=placement.properties.find(kRelativeScaleProperty);
    const auto* number=it==placement.properties.end()?nullptr:std::get_if<float>(&it->second);
    if(!number || !std::isfinite(*number) || std::any_of(placement.properties.begin(),placement.properties.end(),[](const auto& row){return !placement_property_schema().contains(row.first);})){
        error="Native placement requires its exact finite scale-range companion";return false;
    }
    value=*number;return true;
}
inline bool project_relative_scale(editor::Placement& placement,bool tracked,float value,std::string& error){
    if(!tracked)return true;
    if(!std::isfinite(value)){error="Native scale-range companion is not finite";return false;}
    placement.properties[kRelativeScaleProperty]=value;return true;
}
inline bool same_native_properties(const std::map<uint32_t,editor::Value>& actual,const std::map<uint32_t,editor::Value>& wanted){
    if(actual.size()!=wanted.size())return false;
    for(const auto& [key,value]:actual){
        auto found=wanted.find(key);if(found==wanted.end())return false;
        if(value==found->second)continue;
        if(key!=kSoundPitchProperty && key!=kSoundGainProperty)return false;
        const auto* a=std::get_if<float>(&value);const auto* b=std::get_if<float>(&found->second);
        if(!a || !b || !std::isfinite(*a) || !std::isfinite(*b) || *a<0 || *b<0)return false;
        const auto x=std::bit_cast<uint32_t>(*a),y=std::bit_cast<uint32_t>(*b);
        // Native sound controls reconstruct these floats through their sliders.
        // The exact-image pitch reproduction differs by 10 ULP after restore.
        if(std::max(x,y)-std::min(x,y)>16)return false;
    }
    return true;
}
} // namespace editor_native
