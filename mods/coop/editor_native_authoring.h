// Verified native act/captain projection. Serialized subtrees remain owned.
#pragma once
#include "net/editor_document.h"

namespace editor_native {
inline constexpr uint32_t kActBlockProperty=0x41435453, kActBlockFormat=0x31544341;
inline constexpr uint32_t kCaptainAssetProperty=0x43415001, kCaptainLockedProperty=0x43415002;
inline constexpr uint32_t kCaptainPositionProperty=0x43415003, kCaptainRotationProperty=0x43415004;
inline constexpr uint32_t kCaptainScaleProperty=0x43415005, kCaptainHealthProperty=0x43415006;
inline constexpr uint32_t kCaptainInvulnerableProperty=0x43415007;
inline constexpr editor::ObjectId kCaptainObject{8ull<<32,1};
inline constexpr editor::ObjectId act_settings_id(uint32_t act){return {(7ull<<32)|act,1};}
inline const std::map<uint32_t,editor::PropertySpec>& authoring_property_schema(){
    using K=editor::ValueKind;
    static const std::map<uint32_t,editor::PropertySpec> schema{
        {kActBlockProperty,{K::Structured,0,0,0,true,false}},
        {kCaptainAssetProperty,{K::Resource,0,0,0,true,true}},
        {kCaptainLockedProperty,{K::Boolean,0,0,0,true,false}},
        {kCaptainPositionProperty,{K::Position,-1e7,1e7,0,true,false}},
        {kCaptainRotationProperty,{K::Rotation,0,0,0,true,false}},
        {kCaptainScaleProperty,{K::Number,0.000001,1000,0,true,false}},
        {kCaptainHealthProperty,{K::Number,0,1e6,0,true,false}},
        {kCaptainInvulnerableProperty,{K::Boolean,0,0,0,true,false}}
    };
    return schema;
}
} // namespace editor_native
