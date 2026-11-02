#pragma once

#include <algorithm>
#include <bit>
#include <string>
#include <vector>

#include "net/editor_document.h"

// Bounded descriptions of owned document values. Never dereferences a native
// pointer or converts an observed difference into authoring permission.
namespace editor_placement_diagnostic {
struct Difference { std::string field,before,after; };
inline std::string hex(uint64_t value,unsigned width){
    std::string result(width,'0');for(unsigned i=0;i<width;++i){result[width-i-1]="0123456789ABCDEF"[value&15];value>>=4;}return result;
}
inline std::string identity(editor::ObjectId value){return hex(value.id,16)+":"+hex(value.generation,8);}
inline std::string bytes(const uint8_t* data,size_t size,size_t offset){
    std::string result;for(size_t i=offset;i<size && i-offset<16;++i)result+=hex(data[i],2);return result;
}
inline size_t changed_byte(const uint8_t* left,size_t a,const uint8_t* right,size_t b){
    size_t offset=0;while(offset<std::min(a,b) && left[offset]==right[offset])++offset;return offset;
}
inline std::string bits(const editor::Value* value,size_t offset=0,size_t reference=0){
    if(!value)return "absent";
    if(auto v=std::get_if<editor::Vec3>(value))return "vec3:"+hex(std::bit_cast<uint32_t>(v->x),8)+","+hex(std::bit_cast<uint32_t>(v->y),8)+","+hex(std::bit_cast<uint32_t>(v->z),8);
    if(auto v=std::get_if<editor::Rotation>(value))return "quat:"+hex(std::bit_cast<uint32_t>(v->x),8)+","+hex(std::bit_cast<uint32_t>(v->y),8)+","+hex(std::bit_cast<uint32_t>(v->z),8)+","+hex(std::bit_cast<uint32_t>(v->w),8);
    if(auto v=std::get_if<float>(value))return "float:"+hex(std::bit_cast<uint32_t>(*v),8);
    if(auto v=std::get_if<bool>(value))return *v?"bool:01":"bool:00";
    if(auto v=std::get_if<int64_t>(value))return "integer:"+hex(uint64_t(*v),16);
    if(auto v=std::get_if<editor::ObjectId>(value))return "reference:"+identity(*v);
    if(auto v=std::get_if<editor::ResourceKey>(value))return "resource:"+hex(v->group,8)+"!"+hex(v->instance,8)+"."+hex(v->type,8);
    if(auto v=std::get_if<std::string>(value))return "text:bytes="+std::to_string(v->size())+",at="+std::to_string(offset)+",bits="+bytes(reinterpret_cast<const uint8_t*>(v->data()),v->size(),offset);
    const auto& v=std::get<editor::StructuredValue>(*value);
    auto result="structured:format="+hex(v.format,8)+",bytes="+std::to_string(v.bytes.size())+",at="+std::to_string(offset)+",bits="+bytes(v.bytes.data(),v.bytes.size(),offset)+",refs="+std::to_string(v.references.size())+",ref_at="+std::to_string(reference);
    for(size_t i=reference;i<v.references.size() && i-reference<2;++i)result+=","+identity(v.references[i]);
    return result;
}
inline std::vector<Difference> describe(const editor::Placement* before,const editor::Placement* after){
    std::vector<Difference> result;result.reserve(8);
    if(!before || !after){result.push_back({"presence",before?"present":"absent",after?"present":"absent"});return result;}
    const auto add=[&](std::string field,const editor::Value* left,const editor::Value* right){
        if(result.size()==8 || (left && right && *left==*right) || (!left && !right))return;
        size_t offset=0,reference=0;
        if(left && right){
            if(auto a=std::get_if<std::string>(left))if(auto b=std::get_if<std::string>(right))offset=changed_byte(reinterpret_cast<const uint8_t*>(a->data()),a->size(),reinterpret_cast<const uint8_t*>(b->data()),b->size());
            if(auto a=std::get_if<editor::StructuredValue>(left))if(auto b=std::get_if<editor::StructuredValue>(right)){
                offset=changed_byte(a->bytes.data(),a->bytes.size(),b->bytes.data(),b->bytes.size());
                while(reference<std::min(a->references.size(),b->references.size()) && a->references[reference]==b->references[reference])++reference;
            }
        }
        result.push_back({std::move(field),bits(left,offset,reference),bits(right,offset,reference)});
    };
    const auto primitive=[&](const char* field,editor::Value left,editor::Value right){add(field,&left,&right);};
    primitive("class",before->class_id,after->class_id);primitive("act",before->act_id,after->act_id);
    primitive("protected",before->protected_object,after->protected_object);
    primitive("position",before->position,after->position);primitive("rotation",before->rotation,after->rotation);primitive("scale",before->scale,after->scale);
    auto left=before->properties.begin(),right=after->properties.begin();
    while(result.size()<8 && (left!=before->properties.end() || right!=after->properties.end())){
        const auto key=left==before->properties.end()?right->first:right==after->properties.end()?left->first:std::min(left->first,right->first);
        const editor::Value* a=nullptr;const editor::Value* b=nullptr;
        if(left!=before->properties.end() && left->first==key){a=&left->second;++left;}
        if(right!=after->properties.end() && right->first==key){b=&right->second;++right;}
        add("property:"+hex(key,8),a,b);
    }
    return result;
}
}
