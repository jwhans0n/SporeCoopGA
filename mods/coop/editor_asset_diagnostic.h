// Read-only owned-document diagnostics. No Native calls or authoring authority.
#pragma once
#include "editor_native_cast.h"
#include "../../updater/sha256.h"
#include <algorithm>
#include <cstdio>
#include <optional>
#include <string>
#include <vector>

namespace editor_asset_diagnostic {
inline constexpr size_t kPageRows=24,kMaximumRows=1024*8,kReplyBytes=24*256+512+32;
static_assert(kReplyBytes<8191); // Kit's 8 KiB out_printf buffer includes NUL.
struct Query {size_t first=0,count=kPageRows;bool paged=false;std::optional<editor::ObjectId> only;};
inline bool decimal(const std::string& text,uint64_t maximum,uint64_t& value){
    if(text.empty() || text.size()>20)return false;value=0;
    for(char ch:text){
        if(ch<'0'||ch>'9'||value>(maximum-uint64_t(ch-'0'))/10)return false;
        value=value*10+uint64_t(ch-'0');
    }
    return true;
}
inline bool query(const std::vector<std::string>& args,Query& out){
    Query result;uint64_t first=0,count=0;
    if(args.size()==1 && args[0]=="assets"){out=result;return true;}
    if(args.size()==3 && args[0]=="assets" && decimal(args[1],kMaximumRows,first) &&
        decimal(args[2],kPageRows,count) && count){result.first=size_t(first);result.count=size_t(count);result.paged=true;out=result;return true;}
    if(args.size()==2 && args[0]=="asset"){
        const auto colon=args[1].find(':');uint64_t id=0,generation=0;
        if(args[1].size()<=32 && colon!=std::string::npos && decimal(args[1].substr(0,colon),UINT64_MAX,id) && id &&
            decimal(args[1].substr(colon+1),UINT32_MAX,generation) && generation){
            result.only=editor::ObjectId{id,uint32_t(generation)};out=result;return true;
        }
    }
    return false;
}
inline std::string format(const editor::Document& doc,editor::Revision durable,const std::string& hash,const Query& request){
    if(hash.size()!=64 || hash.find_first_not_of("0123456789abcdef")!=std::string::npos ||
        request.count<1 || request.count>kPageRows || request.first>kMaximumRows || doc.class_settings.size()>kMaximumRows)
        return "Shared assets unavailable: diagnostic bounds";
    struct Row {editor::ObjectId id;const editor::ClassActSettings* settings;editor::ResourceKey key;const editor::StructuredValue* behavior;};
    std::vector<Row> rows;
    for(const auto& [id,row]:doc.class_settings){
        if(request.only && row.class_id!=*request.only)continue;
        const auto content=editor::value(doc,id,{editor::Field::Property,editor::kClassAssetResourceProperty});
        const auto* key=content?std::get_if<editor::ResourceKey>(&*content):nullptr;if(!key)continue;
        const auto block=row.properties.find(editor_native::kCastBlockProperty);
        const auto* behavior=block==row.properties.end()?nullptr:std::get_if<editor::StructuredValue>(&block->second);if(!behavior)continue;
        rows.push_back({id,&row,*key,behavior});if(request.only)break;
    }
    if(request.first>rows.size())return "Shared assets unavailable: page offset exceeds total";
    std::string result="revision="+std::to_string(doc.revision)+" durable="+std::to_string(durable)+" canonicaldocumenthash="+hash+"\n";
    const auto end=std::min(rows.size(),request.first+request.count);
    if(request.paged){
        char project[33];for(size_t i=0;i<doc.project.size();++i)std::snprintf(project+i*2,3,"%02x",unsigned(doc.project[i]));
        result+="assets_page project="+std::string(project)+" epoch="+std::to_string(doc.epoch)+" first="+std::to_string(request.first)+
            " count="+std::to_string(request.count)+" returned="+std::to_string(end-request.first)+" next="+std::to_string(end)+" total="+std::to_string(rows.size())+"\n";
    }
    for(size_t i=request.first;i<end;++i){
        const auto& row=rows[i];char text[256];
        const auto length=std::snprintf(text,sizeof text,"asset class=%llu:%u settings=%llu:%u key=%08X!%08X.%08X content_revision=%llu behavior_sha256=",
            static_cast<unsigned long long>(row.settings->class_id.id),row.settings->class_id.generation,
            static_cast<unsigned long long>(row.id.id),row.id.generation,row.key.group,row.key.instance,row.key.type,
            static_cast<unsigned long long>(editor::field_revision(doc,row.id,{editor::Field::Property,editor::kClassAssetResourceProperty})));
        if(length<0 || size_t(length)>=sizeof text)return "Shared assets unavailable: row bound";
        result+=std::string(text)+upd::sha256_hex(row.behavior->bytes)+"\n";
    }
    if(request.only && rows.empty())result+="asset_not_found=1\n";
    if(!request.paged && !request.only && end<rows.size())result+="truncated=1\n";
    if(result.size()>kReplyBytes)return "Shared assets unavailable: reply bound";
    return result;
}
}
