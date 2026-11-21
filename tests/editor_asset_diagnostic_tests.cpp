#include "editor_asset_diagnostic.h"
#include <cstdlib>
#include <iostream>
#include <set>
using namespace editor_asset_diagnostic;
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::cerr<<"FAIL "<<__LINE__<<": "<<#x<<'\n';std::exit(1);}}while(0)
static size_t occurrences(const std::string& text,const std::string& part){size_t count=0,at=0;while((at=text.find(part,at))!=std::string::npos){++count;at+=part.size();}return count;}
static editor::Document document(size_t count){
    editor::Document doc;doc.project.fill(0xff);doc.epoch=UINT64_MAX;doc.revision=UINT64_MAX-1;
    for(size_t i=0;i<count;++i){
        editor::ClassActSettings row;row.object={UINT64_MAX-8192+i,UINT32_MAX};row.class_id={UINT64_MAX-4096+i,UINT32_MAX};
        row.properties[editor::kClassAssetResourceProperty]=editor::ResourceKey{UINT32_MAX,uint32_t(i),UINT32_MAX};
        row.properties[editor_native::kCastBlockProperty]=editor::StructuredValue{0x1234,{uint8_t(i),2,3},{}};
        row.field_revisions[{editor::Field::Property,editor::kClassAssetResourceProperty}]=UINT64_MAX-2;
        doc.class_settings.emplace(row.object,std::move(row));
    }
    return doc;
}
int main(){
    Query request;CHECK(query({"assets"},request)&&!request.paged&&!request.only);
    CHECK(query({"assets","0","24"},request)&&request.paged&&!request.first&&request.count==24);
    CHECK(query({"assets","8192","1"},request)&&request.first==8192&&request.count==1);
    for(const auto& args:std::vector<std::vector<std::string>>{{},{"assets","0"},{"assets","0","0"},{"assets","0","25"},
        {"assets","8193","24"},{"assets","-1","24"},{"assets","1x","24"},{"assets","0","+1"},{"assets","0","1","extra"},
        {"assets","18446744073709551616","1"},{"asset","0:1"},{"asset","1:0"},{"asset","1:4294967296"},{"asset","1:2:3"}}){
        Query unchanged;unchanged.first=91;CHECK(!query(args,unchanged)&&unchanged.first==91);
    }
    const std::string hash(64,'a');
    for(size_t count:{size_t(0),size_t(1),size_t(24),size_t(119),size_t(1024)}){
        const auto doc=document(count),before=doc;std::set<std::string> rows;
        const auto header="revision="+std::to_string(doc.revision)+" durable=7 canonicaldocumenthash="+hash+"\n";
        for(size_t first=0;first<count || first==0;first+=24){
            CHECK(query({"assets",std::to_string(first),"24"},request));const auto text=format(doc,7,hash,request);
            CHECK(text.starts_with(header)&&text.size()<=kReplyBytes&&text.size()<8191);
            CHECK(text.find("assets_page project=ffffffffffffffffffffffffffffffff epoch=18446744073709551615 first="+std::to_string(first))!=std::string::npos);
            const auto returned=std::min(size_t(24),count-first);
            CHECK(text.find(" count=24 returned="+std::to_string(returned)+" next="+std::to_string(first+returned)+" total="+std::to_string(count)+"\n")!=std::string::npos);
            CHECK(occurrences(text,"\nasset ")==returned&&text.find("truncated=1")==std::string::npos);
            size_t at=0;while((at=text.find("asset class=",at))!=std::string::npos){const auto end=text.find('\n',at);CHECK(end!=std::string::npos&&end-at<256);CHECK(rows.insert(text.substr(at,end-at)).second);at=end;}
            if(!count)break;
        }
        CHECK(rows.size()==count&&doc==before);
        CHECK(query({"assets",std::to_string(count),"24"},request));auto text=format(doc,7,hash,request);
        CHECK(!occurrences(text,"\nasset ")&&text.find(" returned=0 next="+std::to_string(count))!=std::string::npos);
        CHECK(query({"assets",std::to_string(count+1),"24"},request));CHECK(format(doc,7,hash,request).find("offset exceeds total")!=std::string::npos);
        CHECK(query({"assets"},request));text=format(doc,7,hash,request);
        CHECK(text.starts_with(header)&&text.size()<8191&&occurrences(text,"\nasset ")==std::min(size_t(24),count));
        CHECK((text.find("truncated=1\n")!=std::string::npos)==(count>24));
        if(count){
            const auto id=doc.class_settings.begin()->second.class_id;
            CHECK(query({"asset",std::to_string(id.id)+":"+std::to_string(id.generation)},request));text=format(doc,7,hash,request);
            CHECK(occurrences(text,"\nasset ")==1&&text.find("truncated=")==std::string::npos);
            CHECK(text.find("behavior_sha256="+upd::sha256_hex(std::vector<uint8_t>{0,2,3})+"\n")!=std::string::npos);
            CHECK(text.find("content_revision=18446744073709551613")!=std::string::npos);
        }
        CHECK(query({"asset","1:1"},request));CHECK(format(doc,7,hash,request).ends_with("asset_not_found=1\n"));
    }
    auto invalid=document(1);invalid.class_settings.begin()->second.properties.erase(editor_native::kCastBlockProperty);
    CHECK(query({"assets","0","24"},request));CHECK(format(invalid,7,hash,request).find("total=0\n")!=std::string::npos);
    CHECK(format(invalid,7,"bad",request).find("diagnostic bounds")!=std::string::npos);
    std::cout<<"asset diagnostics: "<<checks<<" checks passed\n";
}
