// Owned native terrain recipe codec. The serialized scenario contains a planet
// PropertyList followed by its independent adventure DBPF body. Never patch a
// loaded save or borrow the live TerraformMode's vectors.
#pragma once
#include "net/editor_terrain.h"
#include <algorithm>
#include <array>
#include <deque>
#include <limits>
#include <set>

namespace editor_native_terrain {
inline constexpr editor::ObjectId kPlanetObject{11ull<<32,1};
inline constexpr uint32_t kPropertyFormat=0x31525054; // TPR1
inline constexpr uint32_t kEffects=0x02A907B5,kTransforms=0x02A907B6,kTimes=0x03A23F97;
inline constexpr uint32_t kSeeds=0x03DD8E59,kIntensities=0x03DD8E5A,kWater=0x7A6AA70B;
inline constexpr uint32_t kModels=0x03A90C57,kModelTransforms=0x03A90C5F;
inline constexpr size_t kPropertyLimit=4096,kRecipeLimit=8192,kBytesLimit=32u<<20;
struct Property {uint16_t type=0,flags=0;std::vector<uint8_t> value;bool operator==(const Property&)const=default;};
struct Properties {std::array<uint8_t,8> header{};std::map<uint32_t,Property> fields;std::vector<uint8_t> tail;};
inline uint32_t be32(const uint8_t* p){return uint32_t(p[0])<<24|uint32_t(p[1])<<16|uint32_t(p[2])<<8|p[3];}
inline uint16_t be16(const uint8_t* p){return uint16_t(p[0])<<8|p[1];}
inline void u16(std::vector<uint8_t>& b,uint16_t v){b.push_back(uint8_t(v>>8));b.push_back(uint8_t(v));}
inline void u32(std::vector<uint8_t>& b,uint32_t v){for(int n=24;n>=0;n-=8)b.push_back(uint8_t(v>>n));}
inline const std::map<uint32_t,uint16_t>& planet_properties(){
    static const std::map<uint32_t,uint16_t> result{
        {0x00B2CCCB,0x20},{0x02A907B7,0xA},{0x03A23F98,0x31},{0x03A23F99,0x31},
        {0x03ABB0AF,0x20},{0x04E13CB7,0x20},{0x0536250C,0xA},{0x0536250D,0xA},
        {0x072BEC91,0xD},{0x073137BB,0xD},{0x073154E0,0x9},{0x07B18058,1},
        {0x0B208B41,0x31},{0x48C74C5C,0x20},{0x56B14F05,0xA},{0x8E390196,1},
        {0x968C496A,0x31},{0x968C496B,0x31},{0xB6929C92,0xD},{0xB6929C93,0x31},
        {0xB6929C94,0x31},{0xB6929C95,0x31},{0xD2F669EC,0xA}};
    return result;
}
inline bool recipe_property(uint32_t id){return id==kEffects || id==kTransforms || id==kTimes || id==kSeeds ||
    id==kIntensities || id==kWater || id==kModels || id==kModelTransforms;}
inline bool decode(const std::vector<uint8_t>& bytes,Properties& out,std::string& error){
    auto bad=[&](){error="Invalid or unsupported native terrain property stream";return false;};
    if(bytes.size()<12 || bytes.size()>kBytesLimit || editor::terrain_word(bytes,0)!=0xB523D4F9 || editor::terrain_word(bytes,4)!=17)return bad();
    Properties result;std::copy_n(bytes.begin(),8,result.header.begin());const auto count=be32(bytes.data()+8);
    if(count>kPropertyLimit)return bad();size_t at=12;
    for(uint32_t i=0;i<count;++i){
        if(bytes.size()-at<8)return bad();const auto id=be32(bytes.data()+at);Property p{be16(bytes.data()+at+4),be16(bytes.data()+at+6),{}};at+=8;
        const auto start=at;size_t size=0;
        if(p.flags&0x80){
            if(bytes.size()-at<8)return bad();const auto items=be32(bytes.data()+at),width=be32(bytes.data()+at+4);at+=8;
            if(items>kRecipeLimit || width>kBytesLimit || (items && width>(bytes.size()-at)/items))return bad();
            if(p.type==0x12 || p.type==0x13){
                for(uint32_t j=0;j<items;++j){if(bytes.size()-at<4)return bad();const auto chars=be32(bytes.data()+at);at+=4;
                    const size_t unit=p.type==0x13?2:1;if(chars>(bytes.size()-at)/unit)return bad();at+=size_t(chars)*unit;}
            }else at+=size_t(items)*width;
        }else{
            switch(p.type){
            case 0:case 0x20:case 0x31:case 0x32:case 0x33:case 0x34:size=16;break;
            case 1:case 2:case 5:case 6:size=1;break;
            case 3:case 7:case 8:size=2;break;
            case 4:case 9:case 0xA:case 0xD:size=4;break;
            case 0xB:case 0xC:case 0xE:case 0x30:size=8;break;
            case 0x38:size=56;break;
            case 0x12:case 0x13:{if(bytes.size()-at<4)return bad();const auto chars=be32(bytes.data()+at);const size_t unit=p.type==0x13?2:1;
                if(chars>(bytes.size()-at-4)/unit)return bad();size=4+size_t(chars)*unit;break;}
            default:return bad();
            }
            if(size>bytes.size()-at)return bad();at+=size;
        }
        p.value.assign(bytes.begin()+start,bytes.begin()+at);if(!result.fields.emplace(id,std::move(p)).second)return bad();
    }
    result.tail.assign(bytes.begin()+at,bytes.end());out=std::move(result);error.clear();return true;
}
inline bool encode(const Properties& p,std::vector<uint8_t>& out,std::string& error){
    if(p.fields.size()>kPropertyLimit){error="Native terrain has too many properties";return false;}
    std::vector<uint8_t> bytes(p.header.begin(),p.header.end());u32(bytes,uint32_t(p.fields.size()));
    for(const auto& [id,f]:p.fields){u32(bytes,id);u16(bytes,f.type);u16(bytes,f.flags);bytes.insert(bytes.end(),f.value.begin(),f.value.end());
        if(bytes.size()>kBytesLimit){error="Native terrain property bytes exceed their bound";return false;}}
    if(p.tail.size()>kBytesLimit-bytes.size()){error="Native terrain and adventure exceed their bound";return false;}
    bytes.insert(bytes.end(),p.tail.begin(),p.tail.end());out=std::move(bytes);return true;
}
inline editor::StructuredValue property_value(Property p){
    // Generic native scalar keys and Vector3 values serialize a reserved fourth
    // word. It is not authored identity/color data and varies between processes.
    if(!(p.flags&0x80) && p.value.size()==16 && (p.type==0x20 || p.type==0x31 || p.type==0x32))std::fill(p.value.begin()+12,p.value.end(),0);
    editor::StructuredValue value;value.format=kPropertyFormat;u16(value.bytes,p.type);u16(value.bytes,p.flags);
    value.bytes.insert(value.bytes.end(),p.value.begin(),p.value.end());return value;
}
inline bool native_property(uint32_t id,const editor::Value& value,Property& out,std::string& error){
    const auto spec=planet_properties().find(id);const auto* block=std::get_if<editor::StructuredValue>(&value);
    if(spec==planet_properties().end() || !block || block->format!=kPropertyFormat || !block->references.empty() || block->bytes.size()<4 || block->bytes.size()>editor::Limits{}.structured_bytes){error="Invalid canonical planet property";return false;}
    Property p{be16(block->bytes.data()),be16(block->bytes.data()+2),{block->bytes.begin()+4,block->bytes.end()}};
    if(p.type!=spec->second){error="A canonical planet property changed native type";return false;}
    const bool array=id==0x03ABB0AF || id==0x04E13CB7 || id==0x072BEC91 || id==0x073154E0 || id==0xB6929C92 || id==0xB6929C93;
    const uint32_t width=p.type==0x20 || p.type==0x31?12:4;
    if((array && (p.flags!=0x9C || p.value.size()<8 || be32(p.value.data()+4)!=width)) || (!array && p.flags)){
        error="A canonical planet property changed native array shape";return false;
    }
    Properties wrapper;wrapper.header={0xF9,0xD4,0x23,0xB5,17,0,0,0};wrapper.fields[id]=p;std::vector<uint8_t> encoded;Properties decoded;
    if(!encode(wrapper,encoded,error) || !decode(encoded,decoded,error) || decoded.fields.at(id)!=p || property_value(p)!=*block)return false;
    if(p.type==1 && (p.value.size()!=1 || p.value[0]>1)){error="A canonical planet flag is not Boolean";return false;}
    auto finite_bits=[](uint32_t bits){float v;std::memcpy(&v,&bits,4);return std::isfinite(v);};
    if(p.type==0xD){const size_t start=p.flags&0x80?8:0;for(size_t at=start;at+4<=p.value.size();at+=4)
        if(!finite_bits(be32(p.value.data()+at))){error="A planet float property is not finite";return false;}}
    if(p.type==0x31){const size_t start=p.flags&0x80?8:0;const size_t width=p.flags&0x80?12:16;
        for(size_t at=start;at+width<=p.value.size();at+=width)for(size_t component=0;component<12;component+=4)
            if(!finite_bits(editor::terrain_word(p.value,at+component))){error="A planet vector property is not finite";return false;}}
    out=std::move(p);return true;
}
inline bool array(const Properties& p,uint32_t id,uint16_t type,uint32_t width,std::vector<uint8_t>& bytes,size_t& count,std::string& error){
    auto f=p.fields.find(id);if(f==p.fields.end()){bytes.clear();count=0;return true;}
    const auto& v=f->second;if(v.type!=type || !(v.flags&0x80) || v.value.size()<8 || be32(v.value.data()+4)!=width){error="Invalid native terrain recipe column";return false;}
    count=be32(v.value.data());if(count>kRecipeLimit || v.value.size()!=8+count*width){error="Native terrain recipe column exceeds its bounds";return false;}
    bytes.assign(v.value.begin()+8,v.value.end());return true;
}
inline std::vector<uint8_t> identity_bytes(const editor::StructuredValue& payload){
    auto bytes=payload.bytes;
    // Native Transform::operator= (537E10) increments its destination WORD+2
    // when flags or geometric values change; PreTransformBy does likewise.
    // ToMatrix4 never reads that counter. A rebuild can therefore change it on
    // otherwise identical old stamps. Keep native payloads intact, but exclude
    // only this bookkeeping word from record matching.
    if(bytes.size()>=16)bytes[14]=bytes[15]=0;
    return bytes;
}
inline bool project(const std::vector<uint8_t>& bytes,const editor::Document& identity,editor::Document& document,std::string& error,bool allow_recipe_replacement=false){
    Properties p;if(!decode(bytes,p,error))return false;
    document.terrain.clear();document.settings.erase(kPlanetObject);
    editor::AuthoringSettings planet;planet.object=kPlanetObject;planet.kind=editor::SettingsKind::Planet;
    for(const auto& [id,type]:planet_properties()){
        document.settings_schema[id]={editor::ValueKind::Structured,0,0,0,true,false};
        auto f=p.fields.find(id);if(f==p.fields.end())continue;
        if(f->second.type!=type){error="A native planet property has an unsupported type";return false;}
        auto value=property_value(f->second);Property checked;if(!native_property(id,value,checked,error))return false;
        planet.properties[id]=std::move(value);
    }
    document.settings.emplace(kPlanetObject,std::move(planet));document.next_id=std::max({document.next_id,identity.next_id,uint64_t(13ull<<32)});
    const auto old=editor::terrain_recipe(identity);std::set<editor::ObjectId> used;uint64_t temporary=document.next_id;
    uint64_t order=identity.next_terrain_order;
    for(auto kind:{editor::TerrainKind::Effect,editor::TerrainKind::Model}){
        std::array<std::vector<uint8_t>,6> columns;std::array<size_t,6> counts{};
        const std::array<uint32_t,6> ids=kind==editor::TerrainKind::Effect?std::array<uint32_t,6>{kEffects,kTransforms,kTimes,kSeeds,kIntensities,kWater}:std::array<uint32_t,6>{kModels,kModelTransforms,0,0,0,0};
        const std::array<uint16_t,6> types{0x20,0x38,0xD,0xA,0xD,0xD};const std::array<uint32_t,6> widths{12,56,4,4,4,4};
        const size_t fields=kind==editor::TerrainKind::Effect?6:2;
        for(size_t i=0;i<fields;++i)if(!array(p,ids[i],types[i],widths[i],columns[i],counts[i],error))return false;
        for(size_t i=1;i<fields;++i)if(counts[i]!=counts[0]){error="The native terrain recipe columns have different lengths";return false;}
        std::vector<editor::StructuredValue> incoming;std::set<std::vector<uint8_t>> present;
        for(size_t i=0;i<counts[0];++i){
            editor::StructuredValue payload;payload.format=editor::kTerrainRecordFormat;
            for(size_t c=0;c<fields;++c)payload.bytes.insert(payload.bytes.end(),columns[c].begin()+i*widths[c],columns[c].begin()+(i+1)*widths[c]);
            // Numeric scalar arrays use the property's big-endian codec; keys
            // and Transform are native primitive bytes and retain their order.
            if(kind==editor::TerrainKind::Effect)for(size_t c=68;c<84;c+=4)std::reverse(payload.bytes.begin()+c,payload.bytes.begin()+c+4);
            if(!editor::valid_terrain_payload(kind,payload)){error="The native terrain recipe contains an invalid transform or scalar";return false;}
            present.insert(identity_bytes(payload));incoming.push_back(std::move(payload));
        }
        std::vector<const editor::TerrainRecord*> previous;std::map<std::vector<uint8_t>,std::deque<const editor::TerrainRecord*>> matches;
        for(const auto& v:old)if(v.kind==kind){previous.push_back(&v);matches[identity_bytes(v.payload)].push_back(&v);}
        uint64_t last_order=0;
        for(size_t i=0;i<incoming.size();++i){
            editor::TerrainRecord row;row.kind=kind;row.payload=std::move(incoming[i]);
            auto exact=matches.find(identity_bytes(row.payload));
            if(exact!=matches.end() && !exact->second.empty()){
                const auto* prior=exact->second.front();exact->second.pop_front();row.object=prior->object;row.order=prior->order;row.revision=prior->revision;
                // Counter-only native updates are not authored edits. Preserve
                // the exact committed payload, including each duplicate's own
                // counter, so an append needs no leases on unrelated stamps.
                row.payload=prior->payload;
            }else {
                if(incoming.size()!=previous.size() && std::any_of(previous.begin(),previous.end(),[&](const auto* old){return !present.contains(identity_bytes(old->payload));})){
                    if(allow_recipe_replacement){
                        // Only a verified whole-recipe Load boundary may opt in.
                        // All old records are checked deletions, all incoming
                        // native records get fresh IDs and immutable order. No
                        // ambiguous old row is guessed to be the same stamp.
                        editor::Document replacement;replacement.next_id=std::max(identity.next_id,document.next_id);
                        replacement.next_terrain_order=identity.next_terrain_order;
                        return project(bytes,replacement,document,error,false);
                    }
                    error="A native terrain gesture changed an ambiguous record identity";return false;
                }
                if(i<previous.size() && !used.contains(previous[i]->object) && !present.contains(identity_bytes(previous[i]->payload))){
                    row.object=previous[i]->object;row.order=previous[i]->order;row.revision=previous[i]->revision;
                }
                else {if(temporary==UINT64_MAX || order==UINT64_MAX){error="The terrain identity allocator is exhausted";return false;}row.object={temporary++,1};row.order=order++;}}
            if(row.order<=last_order){
                if(allow_recipe_replacement){
                    editor::Document replacement;replacement.next_id=std::max(identity.next_id,document.next_id);
                    replacement.next_terrain_order=identity.next_terrain_order;
                    return project(bytes,replacement,document,error,false);
                }
                error="A native terrain gesture reordered committed recipe records";return false;
            }
            last_order=row.order;
            used.insert(row.object);document.terrain.emplace(row.object,std::move(row));
        }
    }
    document.next_id=std::max(document.next_id,temporary);document.next_terrain_order=order;return true;
}
inline bool merge(const std::vector<uint8_t>& base,const editor::Document& document,std::vector<uint8_t>& out,std::string& error){
    Properties p;if(!decode(base,p,error))return false;
    auto settings=document.settings.find(kPlanetObject);if(settings==document.settings.end()){error="The canonical planet settings are missing";return false;}
    for(const auto& [id,type]:planet_properties()){
        (void)type;auto f=settings->second.properties.find(id);if(f==settings->second.properties.end()){p.fields.erase(id);continue;}
        Property native;if(!native_property(id,f->second,native,error))return false;p.fields[id]=std::move(native);
    }
    const auto ordered=editor::terrain_recipe(document);
    for(auto kind:{editor::TerrainKind::Effect,editor::TerrainKind::Model}){
        const std::array<uint32_t,6> ids=kind==editor::TerrainKind::Effect?std::array<uint32_t,6>{kEffects,kTransforms,kTimes,kSeeds,kIntensities,kWater}:std::array<uint32_t,6>{kModels,kModelTransforms,0,0,0,0};
        const std::array<uint16_t,6> types{0x20,0x38,0xD,0xA,0xD,0xD};const std::array<uint32_t,6> widths{12,56,4,4,4,4};
        const size_t fields=kind==editor::TerrainKind::Effect?6:2;const auto count=std::count_if(ordered.begin(),ordered.end(),[&](const auto& row){return row.kind==kind;});
        std::array<Property,6> native;
        for(size_t c=0;c<fields;++c){native[c].type=types[c];native[c].flags=0x9C;u32(native[c].value,uint32_t(count));u32(native[c].value,widths[c]);}
        for(const auto& row:ordered)if(row.kind==kind){
            if(!editor::valid_terrain_payload(kind,row.payload)){error="Invalid canonical terrain recipe record";return false;}
            size_t at=0;for(size_t c=0;c<fields;++c){auto start=native[c].value.size();native[c].value.insert(native[c].value.end(),row.payload.bytes.begin()+at,row.payload.bytes.begin()+at+widths[c]);
                if(c>=2)std::reverse(native[c].value.begin()+start,native[c].value.end());at+=widths[c];}
        }
        for(size_t c=0;c<fields;++c){if(!count && !p.fields.contains(ids[c]))continue;p.fields[ids[c]]=std::move(native[c]);}
    }
    return encode(p,out,error);
}
inline bool normalize_supported(std::vector<uint8_t>& changed,const std::vector<uint8_t>& before,std::string& error){
    Properties left,right;if(!decode(before,left,error) || !decode(changed,right,error))return false;
    // F37530 clears this obsolete key-array property with a default Property:
    // only its type/flags are initialized. Native Write still emits all sixteen
    // unused union bytes. Preserve presence and every semantic shape exactly;
    // normalize only the unused bytes when BOTH images contain that None value.
    constexpr uint32_t cleared_keys=0x040539D4;
    const auto a=left.fields.find(cleared_keys),b=right.fields.find(cleared_keys);
    const auto empty=[](const Property& p){return p.type==0 && p.flags==0 && p.value.size()==16;};
    if(a!=left.fields.end() && b!=right.fields.end() && empty(a->second) && empty(b->second))
        b->second.value=a->second.value;
    for(auto i=right.fields.begin();i!=right.fields.end();)if(recipe_property(i->first) || planet_properties().contains(i->first))i=right.fields.erase(i);else ++i;
    for(const auto& [id,value]:left.fields)if(recipe_property(id) || planet_properties().contains(id))right.fields[id]=value;
    return encode(right,changed,error);
}
} // namespace editor_native_terrain
