// Exact owned provenance for Terraform's editor-proxy rebuild. Native F35E10
// writes proxy positions back to recipe rows; those derived float round trips
// must not become unrelated authored edits. Scope/owner fencing is supplied by
// the native adapter. This helper owns only bounded primitive bytes.
#pragma once
#include "editor_native_terrain.h"

namespace editor_native_terrain_materialization {
using Columns=std::array<std::vector<uint8_t>,6>;
using AuthoredRows=std::array<std::set<size_t>,2>;
struct Recipe {
    std::array<std::vector<editor::StructuredValue>,2> rows;
    bool operator==(const Recipe&)const=default;
};
inline size_t index(editor::TerrainKind kind){return size_t(kind);}
inline constexpr std::array<uint32_t,6> widths{12,56,4,4,4,4};
inline bool from_columns(editor::TerrainKind kind,const Columns& columns,Recipe& out,std::string& error){
    if(kind>editor::TerrainKind::Model){error="Invalid native terrain recipe kind";return false;}
    const size_t fields=kind==editor::TerrainKind::Effect?6:2;
    if(columns[0].size()%12){error="The native terrain key column is truncated";return false;}
    const auto count=columns[0].size()/12;
    if(count>editor_native_terrain::kRecipeLimit){error="The native terrain recipe exceeds its bound";return false;}
    for(size_t c=0;c<fields;++c)if(columns[c].size()!=count*widths[c]){
        error="The native terrain recipe columns have different lengths";return false;
    }
    std::vector<editor::StructuredValue> rows;rows.reserve(count);
    for(size_t n=0;n<count;++n){
        editor::StructuredValue value;value.format=editor::kTerrainRecordFormat;
        for(size_t c=0;c<fields;++c)value.bytes.insert(value.bytes.end(),columns[c].begin()+n*widths[c],columns[c].begin()+(n+1)*widths[c]);
        if(!editor::valid_terrain_payload(kind,value)){error="A materialized native terrain record is invalid";return false;}
        rows.push_back(std::move(value));
    }
    out.rows[index(kind)]=std::move(rows);error.clear();return true;
}
inline bool from_bytes(const std::vector<uint8_t>& bytes,Recipe& out,std::string& error){
    editor_native_terrain::Properties p;if(!editor_native_terrain::decode(bytes,p,error))return false;
    Recipe result;
    for(auto kind:{editor::TerrainKind::Effect,editor::TerrainKind::Model}){
        const auto ids=kind==editor::TerrainKind::Effect?
            std::array<uint32_t,6>{editor_native_terrain::kEffects,editor_native_terrain::kTransforms,editor_native_terrain::kTimes,editor_native_terrain::kSeeds,editor_native_terrain::kIntensities,editor_native_terrain::kWater}:
            std::array<uint32_t,6>{editor_native_terrain::kModels,editor_native_terrain::kModelTransforms,0,0,0,0};
        const std::array<uint16_t,6> types{0x20,0x38,0xD,0xA,0xD,0xD};
        Columns columns;const size_t fields=kind==editor::TerrainKind::Effect?6:2;
        for(size_t c=0;c<fields;++c){size_t count=0;
            if(!editor_native_terrain::array(p,ids[c],types[c],widths[c],columns[c],count,error))return false;
            if(c>=2)for(size_t n=0;n<columns[c].size();n+=4)std::reverse(columns[c].begin()+n,columns[c].begin()+n+4);
        }
        if(!from_columns(kind,columns,result,error))return false;
    }
    out=std::move(result);return true;
}
inline uint16_t counter(const std::vector<uint8_t>& bytes){return uint16_t(bytes[14])|(uint16_t(bytes[15])<<8);}
inline bool proxy_output(editor::TerrainKind kind,const editor::StructuredValue& before,const editor::StructuredValue& after){
    if(!editor::valid_terrain_payload(kind,before)||!editor::valid_terrain_payload(kind,after))return false;
    if(before==after)return true; // Failed native proxy resolution leaves its row alone.
    if(after.bytes[12]!=(before.bytes[12]|4) || after.bytes[13]!=before.bytes[13] ||
       counter(after.bytes)!=uint16_t(counter(before.bytes)+1))return false;
    for(size_t n=0;n<before.bytes.size();++n){
        if(n==12 || (n>=14&&n<28))continue; // counter and offset, never scale/matrix.
        if(before.bytes[n]!=after.bytes[n])return false;
    }
    return true;
}
class Materialization {
    // Full rendered payload, not a float tolerance or resource/index guess.
    std::array<std::map<std::vector<uint8_t>,editor::StructuredValue>,2> rendered;
    std::deque<decltype(rendered)> previous;
    const editor::StructuredValue* authored(size_t kind,const std::vector<uint8_t>& bytes)const{
        auto at=rendered[kind].find(bytes);if(at!=rendered[kind].end())return &at->second;
        for(auto i=previous.rbegin();i!=previous.rend();++i){at=(*i)[kind].find(bytes);if(at!=(*i)[kind].end())return &at->second;}
        return nullptr;
    }
    static size_t cost(const decltype(rendered)& maps){size_t total=0;for(const auto& kind:maps)for(const auto& [key,value]:kind)total+=key.size()+value.bytes.size();return total;}
public:
    void clear(){for(auto& kind:rendered)kind.clear();previous.clear();}
    bool empty()const{return rendered[0].empty()&&rendered[1].empty()&&std::all_of(previous.begin(),previous.end(),[](const auto& row){return row[0].empty()&&row[1].empty();});}
    size_t bytes()const{size_t total=cost(rendered);for(const auto& row:previous)total+=cost(row);return total;}
    size_t owned_bytes()const{
        size_t total=bytes()+sizeof(*this);
        const auto charge=[&](const auto& maps){for(const auto& kind:maps){
            total+=sizeof(kind)+kind.size()*128;
            for(const auto& [key,value]:kind)total+=key.capacity()-key.size()+value.bytes.capacity()-value.bytes.size()+value.references.capacity()*sizeof(editor::ObjectId);
        }};
        charge(rendered);for(const auto& maps:previous)charge(maps);return total;
    }
    // For an owned suspension receipt only. Every replacement still requires
    // an exact complete rendered payload already present in this witness.
    void normalize_recipe(Recipe& recipe)const{
        for(size_t kind=0;kind<2;++kind)for(auto& row:recipe.rows[kind])
            if(const auto* source=authored(kind,row.bytes))row=*source;
    }
    void forget(editor::TerrainKind kind,const editor::StructuredValue& value){
        if(kind>editor::TerrainKind::Model)return;
        auto erase=[&](auto& maps){for(auto at=maps[index(kind)].begin();at!=maps[index(kind)].end();){
            const auto& candidate=at->second.bytes;bool same=candidate.size()==value.bytes.size();
            for(size_t n=0;same&&n<candidate.size();++n)if(n!=14&&n!=15&&candidate[n]!=value.bytes[n])same=false;
            if(same)at=maps[index(kind)].erase(at);else ++at;
        }};
        erase(rendered);for(auto& row:previous)erase(row);
    }
    bool observe(const Recipe& before,const Recipe& after,std::string& error,const AuthoredRows& fresh={}){
        // Verified F36750 -> F366B0 -> F33870 imports each Transform through
        // 537E10 before F35E10. That assignment retains/increments the new
        // destination's counter instead of copying source WORD+2. Chain only
        // exact other bytes through every retained candidate; never infer XYZ.
        // This index borrows only our owned values for this synchronous call.
        struct ImportSource {const editor::StructuredValue* value=nullptr;bool ambiguous=false;};
        std::array<std::map<std::vector<uint8_t>,ImportSource>,2> imports;
        constexpr size_t index_limit=16u<<20,combined_limit=64u<<20;
        size_t index_bytes=sizeof(imports);const size_t retained_owned=owned_bytes();
        const auto import_key=[](std::vector<uint8_t> bytes){if(bytes.size()>=16)bytes[14]=bytes[15]=0;return bytes;};
        const auto include=[&](const auto& maps){
            for(size_t k=0;k<2;++k)for(const auto& [key,value]:maps[k]){
                auto normalized=import_key(key);auto found=imports[k].find(normalized);
                if(found!=imports[k].end()){if(*found->second.value!=value)found->second.ambiguous=true;continue;}
                const size_t charge=128+sizeof(ImportSource)+normalized.capacity();
                if(charge>index_limit-index_bytes || retained_owned>combined_limit-index_bytes-charge)return false;
                index_bytes+=charge;imports[k].emplace(std::move(normalized),ImportSource{&value,false});
            }
            return true;
        };
        if(retained_owned>combined_limit || !include(rendered)){
            error="Native terrain import witnesses exceed their bound";return false;
        }
        for(const auto& maps:previous)if(!include(maps)){error="Native terrain import witnesses exceed their bound";return false;}
        decltype(rendered) next;
        for(auto kind:{editor::TerrainKind::Effect,editor::TerrainKind::Model}){
            const auto k=index(kind);const auto& a=before.rows[k];const auto& b=after.rows[k];
            if(a.size()>editor_native_terrain::kRecipeLimit || a.size()!=b.size()){
                error="A native terrain materialization changed recipe lengths";return false;
            }
            for(size_t n=0;n<a.size();++n){
                if(!proxy_output(kind,a[n],b[n])){
                    error="A native terrain materialization changed an unaudited recipe field";return false;
                }
                const editor::StructuredValue* old=nullptr;
                if(!fresh[k].contains(n)){
                    const auto found=imports[k].find(import_key(a[n].bytes));
                    if(found!=imports[k].end()){
                        if(found->second.ambiguous){error="Native terrain import counters have ambiguous authored provenance";return false;}
                        old=found->second.value;
                    }
                }
                const auto& source=old?*old:a[n];
                const auto* earlier=authored(k,b[n].bytes);
                auto [at,inserted]=next[k].emplace(b[n].bytes,source);
                if((!inserted && at->second!=source) || (earlier && *earlier!=source)){
                    error="Native terrain proxy positions have ambiguous authored provenance";return false;
                }
            }
        }
        auto retained=previous;if(!empty())retained.push_back(rendered);
        while(retained.size()>3)retained.pop_front();
        size_t retained_bytes=cost(next);for(const auto& row:retained)retained_bytes+=cost(row);
        if(retained_bytes>(32u<<20)){error="Native terrain materialization witnesses exceed their bound";return false;}
        rendered=std::move(next);previous=std::move(retained);error.clear();return true;
    }
    bool normalize(std::vector<uint8_t>& bytes,std::string& error)const{
        if(empty())return true;
        editor_native_terrain::Properties p;if(!editor_native_terrain::decode(bytes,p,error))return false;
        Recipe recipe;if(!from_bytes(bytes,recipe,error))return false;
        bool changed=false;
        for(auto kind:{editor::TerrainKind::Effect,editor::TerrainKind::Model}){
            const auto k=index(kind);const auto transform=kind==editor::TerrainKind::Effect?editor_native_terrain::kTransforms:editor_native_terrain::kModelTransforms;
            auto field=p.fields.find(transform);
            for(size_t n=0;n<recipe.rows[k].size();++n){
                const auto& row=recipe.rows[k][n];const auto* found=authored(k,row.bytes);
                if(!found||*found==row)continue;
                if(field==p.fields.end()||field->second.value.size()<8+(n+1)*56){error="The native terrain transform witness is truncated";return false;}
                // Only the audited Transform bytes differ. Keys, effect scalar
                // columns, unknown planet fields and DBPF tail remain exact.
                std::copy_n(found->bytes.begin()+12,56,field->second.value.begin()+8+n*56);changed=true;
            }
        }
        if(!changed)return true;
        std::vector<uint8_t> normalized;if(!editor_native_terrain::encode(p,normalized,error))return false;
        bytes=std::move(normalized);return true;
    }
};
} // namespace editor_native_terrain_materialization
