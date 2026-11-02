// Verified native act/captain projection. Serialized subtrees remain owned.
#pragma once
#include "net/editor_document.h"
#include "editor_native_terrain.h"

namespace editor_native {
inline constexpr uint32_t kActBlockProperty=0x41435453, kActBlockFormat=0x31544341;
inline constexpr uint32_t kCaptainAssetProperty=0x43415001, kCaptainLockedProperty=0x43415002;
inline constexpr uint32_t kCaptainPositionProperty=0x43415003, kCaptainRotationProperty=0x43415004;
inline constexpr uint32_t kCaptainScaleProperty=0x43415005, kCaptainHealthProperty=0x43415006;
inline constexpr uint32_t kCaptainInvulnerableProperty=0x43415007;
inline constexpr editor::ObjectId kCaptainObject{8ull<<32,1};
inline constexpr uint32_t kCrewRosterProperty=0x43524501,kCrewCapacityProperty=0x43524502,kCrewRosterFormat=0x31575243;
inline constexpr editor::ObjectId kCrewObject{10ull<<32,1};
inline constexpr editor::ObjectId kProjectObject{12ull<<32,1};
inline constexpr uint32_t kExtendedComplexityProperty=0x434F4D50;
inline constexpr editor::ObjectId act_settings_id(uint32_t act){return {(7ull<<32)|act,1};}
// Resource/checkpoint envelopes carry the authoritative typed document rather
// than duplicating its captain identity. Legacy documents acquire this row only
// after verified native expansion; leave their existing sidecars untouched.
inline bool captain_identity(const editor::Document& document,editor::ResourceKey& asset,bool& locked,std::string& error){
    const auto found=document.settings.find(kCaptainObject);
    if(found==document.settings.end()){
        if(document.settings_schema.contains(kCaptainAssetProperty) || document.settings_schema.contains(kCaptainLockedProperty)){
            error="The authored captain settings are missing";return false;
        }
        error.clear();return true;
    }
    const auto& row=found->second;
    if(row.object!=kCaptainObject || row.kind!=editor::SettingsKind::Captain || row.act_id!=editor::ObjectId{}){
        error="The authored captain identity is invalid";return false;
    }
    const auto key=row.properties.find(kCaptainAssetProperty),lock=row.properties.find(kCaptainLockedProperty);
    const auto* value=key==row.properties.end()?nullptr:std::get_if<editor::ResourceKey>(&key->second);
    const auto* flag=lock==row.properties.end()?nullptr:std::get_if<bool>(&lock->second);
    if(!value || !flag || ((!value->instance || !value->type) && *value!=editor::ResourceKey{})){
        error="The authored captain properties are invalid";return false;
    }
    asset=*value;locked=*flag;error.clear();return true;
}
inline const std::map<uint32_t,editor::PropertySpec>& authoring_property_schema(){
    using K=editor::ValueKind;
    static const auto schema=[] {std::map<uint32_t,editor::PropertySpec> result{
        {kActBlockProperty,{K::Structured,0,0,0,true,false}},
        {kCaptainAssetProperty,{K::Resource,0,0,0,true,true}},
        {kCaptainLockedProperty,{K::Boolean,0,0,0,true,false}},
        {kCaptainPositionProperty,{K::Position,-1e7,1e7,0,true,false}},
        {kCaptainRotationProperty,{K::Rotation,0,0,0,true,false}},
        {kCaptainScaleProperty,{K::Number,0.000001,1000,0,true,false}},
        {kCaptainHealthProperty,{K::Number,0,1e6,0,true,false}},
        {kCaptainInvulnerableProperty,{K::Boolean,0,0,0,true,false}},
        {kCrewRosterProperty,{K::Structured,0,0,0,true,false}},
        {kCrewCapacityProperty,{K::Integer,0,3,0,true,false}},
        {kExtendedComplexityProperty,{K::Boolean,0,0,0,true,false}}
    };for(const auto& [id,type]:editor_native_terrain::planet_properties()){
        (void)type;result[id]={K::Structured,0,0,0,true,false};
    }return result;}();
    return schema;
}
} // namespace editor_native
