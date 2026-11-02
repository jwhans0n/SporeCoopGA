// Native planet-prefix regressions, independent of the live Adventure scene.
#include "editor_native_terrain.h"
#include <cstdio>
#include <fstream>
#include <iterator>
#include <limits>

using namespace editor_native_terrain;
static int checks=0,failures=0;
#define CHECK(x) do {++checks;if(!(x)){++failures;std::printf("FAIL %d: %s\n",__LINE__,#x);}}while(0)
static void little(std::vector<uint8_t>& b,size_t offset,uint32_t value){
    for(size_t n=0;n<4;++n)b[offset+n]=uint8_t(value>>(n*8));
}
static void real(std::vector<uint8_t>& b,size_t offset,float value){uint32_t bits;std::memcpy(&bits,&value,4);little(b,offset,bits);}
static editor::TerrainRecord stroke(uint64_t id,uint64_t order,float radius,editor::TerrainKind kind=editor::TerrainKind::Effect){
    editor::TerrainRecord row;row.object={id,1};row.order=order;row.kind=kind;row.revision=7;
    row.payload.format=editor::kTerrainRecordFormat;row.payload.bytes.resize(kind==editor::TerrainKind::Effect?84:68);
    auto& b=row.payload.bytes;little(b,0,0x12345678);little(b,4,kind==editor::TerrainKind::Model?0x1A2B3C4D:0);little(b,8,0x406);
    b[12]=6;b[14]=1;real(b,16,radius);real(b,28,1);real(b,32,1);real(b,48,1);real(b,64,1);
    if(kind==editor::TerrainKind::Effect){real(b,68,2);little(b,72,0xF123ABCD);real(b,76,1);real(b,80,0.3f);}
    return row;
}
static std::vector<uint8_t> baseline(){
    Properties p;p.header={0xF9,0xD4,0x23,0xB5,17,0,0,0};p.tail={'D','B','P','F',0,1,2,3};
    p.fields[0xFFFFFFFF]={0xA,0,{1,2,3,4}};p.fields[0x07B18058]={1,0,{1}};
    p.fields[0x02A907B7]={0xA,0,{0x21,0x43,0x65,0x87}};
    std::vector<uint8_t> bytes;std::string error;CHECK(encode(p,bytes,error));return bytes;
}
static void roundtrip(){
    const auto source=baseline();editor::Document empty;std::string error;CHECK(project(source,{},empty,error));
    CHECK(empty.settings.at(kPlanetObject).properties.size()==2);CHECK(empty.terrain.empty());
    auto d=empty;d.terrain.emplace(editor::ObjectId{100,1},stroke(100,2,13));d.terrain.emplace(editor::ObjectId{101,1},stroke(101,1,17));
    d.terrain.emplace(editor::ObjectId{102,1},stroke(102,3,23,editor::TerrainKind::Model));d.next_id=13ull<<32;d.next_terrain_order=4;
    std::vector<uint8_t> merged;CHECK(merge(source,d,merged,error));Properties decoded;CHECK(decode(merged,decoded,error));
    CHECK(decoded.tail==std::vector<uint8_t>({'D','B','P','F',0,1,2,3}));CHECK(decoded.fields.at(0xFFFFFFFF).value==std::vector<uint8_t>({1,2,3,4}));
    editor::Document read;CHECK(project(merged,d,read,error));CHECK(read.terrain==d.terrain);CHECK(read.next_terrain_order==4);
    const auto ordered=editor::terrain_recipe(read);CHECK(ordered.size()==3);CHECK(ordered[0].object==editor::ObjectId(101,1));
    CHECK(editor::terrain_float(ordered[0].payload.bytes,16)==17);CHECK(editor::terrain_word(ordered[0].payload.bytes,72)==0xF123ABCD);
    // A modified existing stroke retains its stable identity. A separate erase
    // preserves surviving exact matches, including after a concurrent append.
    auto changed=d;real(changed.terrain.at({100,1}).payload.bytes,16,99);
    CHECK(merge(source,changed,merged,error));editor::Document actual;CHECK(project(merged,d,actual,error));
    CHECK(actual.terrain.at({102,1}).payload==d.terrain.at({102,1}).payload);
    CHECK(actual.terrain.at({100,1}).payload==changed.terrain.at({100,1}).payload);
    CHECK(actual.terrain.size()==3);
    changed=d;changed.terrain.erase({101,1});changed.terrain.emplace(editor::ObjectId{103,1},stroke(103,4,31));changed.next_terrain_order=5;
    CHECK(merge(source,changed,merged,error));CHECK(project(merged,changed,actual,error));CHECK(actual.terrain==changed.terrain);
    // Repeating identical brush records is supported and preserves both IDs.
    auto duplicate=d;duplicate.terrain.at({101,1}).payload=duplicate.terrain.at({100,1}).payload;
    CHECK(merge(source,duplicate,merged,error));CHECK(project(merged,duplicate,actual,error));CHECK(actual.terrain==duplicate.terrain);
    // Canonical merge overwrites only typed recipe/global fields. The DBPF
    // adventure tail and unknown planet property remain exact.
    auto normalized=merged;CHECK(normalize_supported(normalized,source,error));CHECK(normalized==source);
    // A verified whole-planet Load can change both existing payloads and count.
    // Ordinary brushes keep rejecting ambiguity. The opted-in replacement
    // deletes every old identity and appends fresh records in native order.
    changed=d;real(changed.terrain.at({100,1}).payload.bytes,16,99);
    changed.terrain.emplace(editor::ObjectId{103,1},stroke(103,4,31));changed.next_terrain_order=5;
    CHECK(merge(source,changed,merged,error));CHECK(!project(merged,d,actual,error));
    CHECK(project(merged,d,actual,error,true));CHECK(actual.terrain.size()==4);
    CHECK(std::none_of(d.terrain.begin(),d.terrain.end(),[&](const auto& old){return actual.terrain.contains(old.first);}));
    CHECK(actual.next_terrain_order==8);
    const auto replacement=editor::terrain_recipe(actual),wanted=editor::terrain_recipe(changed);
    for(const auto kind:{editor::TerrainKind::Effect,editor::TerrainKind::Model}){
        std::vector<editor::StructuredValue> a,b;
        for(const auto& row:replacement)if(row.kind==kind){a.push_back(row.payload);CHECK(row.object.id>=d.next_id&&row.revision==0);}
        for(const auto& row:wanted)if(row.kind==kind)b.push_back(row.payload);
        CHECK(a==b);
    }
    editor::Document stable;CHECK(project(merged,actual,stable,error));CHECK(stable.terrain==actual.terrain);
    std::vector<uint8_t> rewritten;CHECK(merge(source,actual,rewritten,error));CHECK(rewritten==merged);
}
static void rejected(){
    auto source=baseline();editor::Document d;std::string error;CHECK(project(source,{},d,error));
    auto row=stroke(100,1,10);d.terrain.emplace(row.object,row);d.next_terrain_order=2;
    std::vector<uint8_t> bytes;CHECK(merge(source,d,bytes,error));
    Properties p;CHECK(decode(bytes,p,error));auto broken=p;broken.fields[kTimes].value[3]=2;CHECK(encode(broken,bytes,error));
    editor::Document actual;CHECK(!project(bytes,d,actual,error));
    row.payload.references.push_back({1,1});CHECK(!editor::valid_terrain_payload(row.kind,row.payload));
    row=stroke(100,1,10);real(row.payload.bytes,28,0);CHECK(!editor::valid_terrain_payload(row.kind,row.payload));
    row=stroke(100,1,10);real(row.payload.bytes,16,std::numeric_limits<float>::infinity());CHECK(!editor::valid_terrain_payload(row.kind,row.payload));
    row=stroke(100,1,10);row.payload.bytes[12]=8;CHECK(!editor::valid_terrain_payload(row.kind,row.payload));
    auto property=d.settings.at(kPlanetObject).properties.at(0x02A907B7);std::get<editor::StructuredValue>(property).bytes[1]=0xD;
    Property out;CHECK(!native_property(0x02A907B7,property,out,error));
    for(size_t n=0;n<source.size()-p.tail.size();++n){auto short_bytes=source;short_bytes.resize(n);CHECK(!decode(short_bytes,p,error));}
}
static void captured(const char* path){
    std::ifstream in(path,std::ios::binary);CHECK(bool(in));if(!in)return;
    const std::vector<uint8_t> bytes((std::istreambuf_iterator<char>(in)),{});Properties p;std::string error;
    CHECK(decode(bytes,p,error));editor::Document d;CHECK(project(bytes,{},d,error));CHECK(!d.terrain.empty());
    std::vector<uint8_t> merged;CHECK(merge(bytes,d,merged,error));editor::Document roundtrip;
    CHECK(project(merged,d,roundtrip,error));CHECK(roundtrip.terrain==d.terrain);
    Properties q;CHECK(decode(merged,q,error));CHECK(q.tail==p.tail);
    for(const auto& [id,value]:p.fields)if(!recipe_property(id) && !planet_properties().contains(id))CHECK(q.fields.at(id)==value);
    auto replacement=d;auto first=replacement.terrain.begin();CHECK(first!=replacement.terrain.end());
    if(first!=replacement.terrain.end()){
        real(first->second.payload.bytes,16,editor::terrain_float(first->second.payload.bytes,16)+.125f);
        auto append=first->second;append.object={replacement.next_id++,1};append.order=replacement.next_terrain_order++;append.revision=0;
        real(append.payload.bytes,16,editor::terrain_float(append.payload.bytes,16)+.125f);replacement.terrain.emplace(append.object,append);
        CHECK(merge(bytes,replacement,merged,error));editor::Document replaced;
        CHECK(!project(merged,d,replaced,error));CHECK(project(merged,d,replaced,error,true));
        CHECK(replaced.terrain.size()==d.terrain.size()+1);
        CHECK(std::none_of(d.terrain.begin(),d.terrain.end(),[&](const auto& old){return replaced.terrain.contains(old.first);}));
        CHECK(decode(merged,q,error)&&q.tail==p.tail);
        for(const auto& [id,value]:p.fields)if(!recipe_property(id) && !planet_properties().contains(id))CHECK(q.fields.at(id)==value);
    }
    std::printf("Captured native fixture: %zu recipe records, %zu properties, %zu preserved adventure bytes\n",d.terrain.size(),p.fields.size(),p.tail.size());
}
int main(int argc,char** argv){roundtrip();rejected();if(argc>1)captured(argv[1]);std::printf("%d checks, %d failures\n",checks,failures);return failures?1:0;}
