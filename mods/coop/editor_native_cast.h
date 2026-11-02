// The native cast adapter owns and validates complete serialized class subtrees.
#pragma once
#include "net/editor_document.h"
#include <cstring>
namespace editor_native {
inline constexpr uint32_t kCastBlockProperty=0x43415354;
inline constexpr uint32_t kCastBlockFormat=0x31415343;
inline void canonical_cast_primary_asset(uint8_t* class_data){
    // Content has its own ResourceKey property and Content lease. Keeping it
    // out of the behavior block lets either field preserve the other's edits.
    const editor::ResourceKey empty{};
    std::memcpy(class_data,&empty,sizeof empty);
    const uint32_t machine=0;std::memcpy(class_data+0xC,&machine,sizeof machine);
    const uint64_t local=UINT64_MAX;std::memcpy(class_data+0x10,&local,sizeof local);
}
inline void canonical_cast_asset_hints(uint8_t* class_data){
    // 138A250: asset+0C is mMachineId (attribute 62B74271), populated by
    // native Save. Installed local keys and transferred bytes own our lookup.
    for(size_t offset:{0u,0x28u,0x50u}){
        const uint32_t local=0;std::memcpy(class_data+offset+0xC,&local,sizeof local);
    }
}
inline void behavior_class_reference(std::set<editor::ObjectId>& references,int32_t type,int32_t value){
    // EC8510/EC81B0 enumerate (2, classIndex). Type 1 is a team and type 3
    // carries an emote, so neither may be mistaken for a cast identity.
    if(type==2 && value>=0)references.insert({(2ull<<32)|uint32_t(value),1});
}
inline const std::map<uint32_t,editor::PropertySpec>& cast_property_schema(){
    static const std::map<uint32_t,editor::PropertySpec> schema{
        {kCastBlockProperty,{editor::ValueKind::Structured,0,0,0,false,false}},
        {editor::kClassAssetResourceProperty,{editor::ValueKind::Resource,0,0,0,false,false}}
    };
    return schema;
}
inline bool cast_schema(const editor::Document& document,std::string& error){
    if(document.class_schema.empty() && document.class_settings.empty())return true;
    if(document.class_schema!=cast_property_schema()){error="Unsupported native cast-property schema";return false;}
    return true;
}
} // namespace editor_native
