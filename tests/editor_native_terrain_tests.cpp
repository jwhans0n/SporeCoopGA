// Native planet-prefix regressions, independent of the live Adventure scene.
#include "editor_native_terrain.h"
#include "editor_native_terrain_materialization.h"
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
static void transform_counters(){
    const auto source=baseline();editor::Document d;std::string error;CHECK(project(source,{},d,error));
    for(auto row:{stroke(100,1,13),stroke(101,2,13),stroke(102,3,23,editor::TerrainKind::Model),stroke(103,4,23,editor::TerrainKind::Model)})
        d.terrain.emplace(row.object,std::move(row));
    // Equal authored stamps can have independent native assignment counters.
    d.terrain.at({101,1}).payload.bytes[14]=0xFE;d.terrain.at({101,1}).payload.bytes[15]=0xFF;
    d.terrain.at({103,1}).payload.bytes[14]=42;d.next_terrain_order=5;
    std::vector<uint8_t> before;CHECK(merge(source,d,before,error));
    auto changed=d;
    for(auto& [id,row]:changed.terrain){(void)id;row.payload.bytes[14]=0;row.payload.bytes[15]=0;}
    std::vector<uint8_t> bytes;CHECK(merge(source,changed,bytes,error));const auto untouched=bytes;
    editor::Document actual;CHECK(project(bytes,d,actual,error));CHECK(actual.terrain==d.terrain);CHECK(bytes==untouched);
    // The actual native failure changed old effect AND model counters while
    // appending an effect. Two concurrent-style appends keep every old ID,
    // payload/revision/order, and each new record's native counter bytes.
    auto first=stroke(104,5,31),second=stroke(105,6,33);first.payload.bytes[14]=7;second.payload.bytes[14]=9;
    changed.terrain.emplace(first.object,first);changed.terrain.emplace(second.object,second);changed.next_terrain_order=7;
    CHECK(merge(source,changed,bytes,error));CHECK(project(bytes,d,actual,error));CHECK(actual.terrain.size()==6);
    for(const auto& [id,row]:d.terrain)CHECK(actual.terrain.at(id)==row);
    const auto ordered=editor::terrain_recipe(actual);CHECK(ordered[4].payload==first.payload&&ordered[5].payload==second.payload);
    CHECK(ordered[4].order==5&&ordered[5].order==6);CHECK(actual.next_terrain_order==7);
    editor::Document again;CHECK(project(bytes,actual,again,error));CHECK(again.terrain==actual.terrain);
    std::vector<uint8_t> canonical;CHECK(merge(bytes,actual,canonical,error));CHECK(project(canonical,actual,again,error));CHECK(again.terrain==actual.terrain);
    // Numeric values, activation flags, resource keys and effect scalars remain
    // exact authored identity. A count change cannot conceal any such edit.
    for(size_t offset:{size_t(0),size_t(12),size_t(16),size_t(32),size_t(68),size_t(72),size_t(76),size_t(80)}){
        auto bad=changed;bad.terrain.at({100,1}).payload.bytes[offset]^=1;
        CHECK(merge(source,bad,bytes,error));CHECK(!project(bytes,d,again,error));
    }
    // Distinct old stamps arriving in a different native order must never be
    // silently sorted back to committed order. Only checked whole-recipe Load
    // can assign fresh identities for that replacement.
    auto unique=d;real(unique.terrain.at({101,1}).payload.bytes,16,17);
    auto reordered=unique;std::swap(reordered.terrain.at({100,1}).payload,reordered.terrain.at({101,1}).payload);
    CHECK(merge(source,reordered,bytes,error));CHECK(!project(bytes,unique,again,error));
    CHECK(project(bytes,unique,again,error,true));CHECK(again.terrain.size()==unique.terrain.size());
    for(const auto& [id,row]:unique.terrain){(void)row;CHECK(!again.terrain.contains(id));}
    const auto replaced=editor::terrain_recipe(again);CHECK(replaced[0].payload==reordered.terrain.at({100,1}).payload);
    CHECK(replaced[1].payload==reordered.terrain.at({101,1}).payload);
    // Removing a distinct stamp preserves remaining immutable order even when
    // a native assignment counter changed on the survivor.
    auto erased=unique;erased.terrain.erase({100,1});erased.terrain.at({101,1}).payload.bytes[14]=11;
    CHECK(merge(source,erased,bytes,error));CHECK(project(bytes,unique,again,error));CHECK(again.terrain.size()==3);
    CHECK(again.terrain.at({101,1})==unique.terrain.at({101,1}));CHECK(!again.terrain.contains({100,1}));
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
static void captured_append(const char* before_path,const char* after_path){
    auto read=[](const char* path){std::ifstream in(path,std::ios::binary);CHECK(bool(in));return std::vector<uint8_t>((std::istreambuf_iterator<char>(in)),{});};
    const auto before=read(before_path),after=read(after_path),unchanged=after;std::string error;editor::Document d,result;
    namespace material=editor_native_terrain_materialization;
    material::Recipe input,output;CHECK(material::from_bytes(before,input,error));CHECK(material::from_bytes(after,output,error));
    // Retained actual native evidence has one new effect and unchanged models.
    // The 282 old rows independently prove the audited proxy transition; the
    // newly authored record is deliberately absent from this old-row witness.
    CHECK(output.rows[0].size()==input.rows[0].size()+1);output.rows[0].pop_back();
    material::Materialization witness;CHECK(witness.observe(input,output,error));auto normalized=after;
    CHECK(witness.normalize(normalized,error));CHECK(after==unchanged);
    CHECK(project(before,{},d,error));const auto projected=project(normalized,d,result,error);CHECK(projected);
    if(!projected){std::printf("Captured append rejection: %s\n",error.c_str());return;}
    CHECK(result.terrain.size()==d.terrain.size()+1);CHECK(after==unchanged);
    for(const auto& [id,row]:d.terrain)CHECK(result.terrain.at(id)==row);
    const auto recipe=editor::terrain_recipe(result);CHECK(recipe.back().kind==editor::TerrainKind::Effect);
    editor::Document stable;CHECK(project(normalized,result,stable,error));CHECK(stable.terrain==result.terrain);
    std::printf("Captured native append: %zu -> %zu records, all committed payloads preserved\n",d.terrain.size(),result.terrain.size());
}
static void materialization(){
    namespace material=editor_native_terrain_materialization;
    const auto source=baseline();editor::Document d;std::string error;CHECK(project(source,{},d,error));
    for(auto row:{stroke(100,1,13),stroke(101,2,17),stroke(102,3,23,editor::TerrainKind::Model)})d.terrain.emplace(row.object,std::move(row));
    d.next_terrain_order=4;std::vector<uint8_t> before;CHECK(merge(source,d,before,error));
    auto input_doc=d,rendered=d;
    for(auto& [id,row]:rendered.terrain){(void)id;row.payload.bytes[14]++;real(row.payload.bytes,16,std::nextafter(editor::terrain_float(row.payload.bytes,16),1000.f));}
    std::vector<uint8_t> after;CHECK(merge(source,rendered,after,error));material::Recipe input,output;
    CHECK(material::from_bytes(before,input,error));CHECK(material::from_bytes(after,output,error));
    material::Materialization witness;CHECK(witness.observe(input,output,error));CHECK(witness.bytes()>0);
    auto normalized=after;CHECK(witness.normalize(normalized,error));CHECK(normalized==before);CHECK(after!=before);
    // A second rebuild is chained by exact prior rendered bytes, never epsilon.
    auto twice=rendered;for(auto& [id,row]:twice.terrain){(void)id;row.payload.bytes[14]++;real(row.payload.bytes,16,std::nextafter(editor::terrain_float(row.payload.bytes,16),1000.f));}
    std::vector<uint8_t> second;CHECK(merge(source,twice,second,error));material::Recipe twice_recipe;CHECK(material::from_bytes(second,twice_recipe,error));
    CHECK(witness.observe(output,twice_recipe,error));normalized=second;CHECK(witness.normalize(normalized,error));CHECK(normalized==before);
    normalized=after;CHECK(witness.normalize(normalized,error));CHECK(normalized==before); // deferred before-history copy
    // Actual author changes have no exact previous rendered witness. Their
    // genuine new input survives normalization while untouched rows chain.
    auto edited=twice;real(edited.terrain.at({100,1}).payload.bytes,16,100);
    auto edited_output=edited;for(auto& [id,row]:edited_output.terrain){(void)id;row.payload.bytes[14]++;real(row.payload.bytes,16,std::nextafter(editor::terrain_float(row.payload.bytes,16),1000.f));}
    std::vector<uint8_t> edited_bytes,edited_output_bytes;CHECK(merge(source,edited,edited_bytes,error));CHECK(merge(source,edited_output,edited_output_bytes,error));
    material::Recipe edited_input,edited_after;CHECK(material::from_bytes(edited_bytes,edited_input,error));CHECK(material::from_bytes(edited_output_bytes,edited_after,error));
    CHECK(witness.observe(edited_input,edited_after,error));normalized=edited_output_bytes;CHECK(witness.normalize(normalized,error));
    auto wanted=d;wanted.terrain.at({100,1})=edited.terrain.at({100,1});std::vector<uint8_t> wanted_bytes;CHECK(merge(source,wanted,wanted_bytes,error));CHECK(normalized==wanted_bytes);
    // No unaudited mutation can establish provenance. Failures retain the old
    // witness atomically, and a later valid rebuild can retry.
    for(size_t offset:{size_t(0),size_t(12),size_t(28),size_t(32),size_t(64),size_t(68),size_t(72),size_t(76),size_t(80)}){
        auto bad=edited_after;bad.rows[0][0].bytes[offset]^=1;CHECK(!witness.observe(edited_input,bad,error));
        normalized=edited_output_bytes;CHECK(witness.normalize(normalized,error));CHECK(normalized==wanted_bytes);
    }
    auto bad=edited_after;bad.rows[0][0].bytes[14]++;CHECK(!witness.observe(edited_input,bad,error));
    bad=edited_after;bad.rows[0].pop_back();CHECK(!witness.observe(edited_input,bad,error));
    bad=edited_input;bad.rows[0][0].references.push_back({1,1});CHECK(!witness.observe(bad,edited_after,error));
    // Equal rendered rows with different authored geometry are ambiguous and
    // cannot be normalized by guessing their index or resource identity.
    auto collision_in=input,collision_out=output;collision_out.rows[0][1]=collision_out.rows[0][0];
    CHECK(!witness.observe(collision_in,collision_out,error));
    collision_in.rows[0][1]=collision_in.rows[0][0];CHECK(witness.observe(collision_in,collision_out,error));
    // An actual new/edited row can exactly equal a previous rendered payload.
    // Native Append/Update provenance explicitly forbids borrowing that row's
    // old authored geometry. Ambiguous output aliasing fails closed.
    witness.clear();CHECK(witness.observe(input,output,error));
    auto fresh_input=output,fresh_output=twice_recipe;material::AuthoredRows fresh;fresh[0].insert(0);
    CHECK(witness.observe(fresh_input,fresh_output,error,fresh));normalized=second;CHECK(witness.normalize(normalized,error));
    auto fresh_wanted=d;fresh_wanted.terrain.at({100,1})=rendered.terrain.at({100,1});CHECK(merge(source,fresh_wanted,wanted_bytes,error));CHECK(normalized==wanted_bytes);
    witness.clear();CHECK(witness.observe(input,output,error));
    auto appended_input=output,appended_output=twice_recipe;
    appended_input.rows[0].push_back(output.rows[0][0]);appended_output.rows[0].push_back(twice_recipe.rows[0][0]);
    fresh={};fresh[0].insert(appended_input.rows[0].size()-1);CHECK(!witness.observe(appended_input,appended_output,error,fresh));
    witness.forget(editor::TerrainKind::Effect,input.rows[0][0]);
    CHECK(witness.observe(fresh_input,fresh_output,error,fresh)); // forgotten actual input remains new authored data
    // A payload that merely resembles a known rendered output stays untouched.
    witness.clear();CHECK(witness.empty()&&witness.bytes()==0);CHECK(witness.observe(input,output,error));
    auto unrelated=rendered;real(unrelated.terrain.at({100,1}).payload.bytes,16,99);std::vector<uint8_t> other;CHECK(merge(source,unrelated,other,error));
    const auto original=other;CHECK(witness.normalize(other,error));editor::Document projected;CHECK(project(other,d,projected,error));
    CHECK(editor::terrain_float(projected.terrain.at({100,1}).payload.bytes,16)==99);CHECK(other!=before&&original!=before);
    auto truncated=after;truncated.resize(12);const auto retained=truncated;CHECK(!witness.normalize(truncated,error));CHECK(truncated==retained);
    material::Columns columns;columns[0].resize(13);material::Recipe sentinel=input;CHECK(!material::from_columns(editor::TerrainKind::Effect,columns,sentinel,error));CHECK(sentinel==input);
    columns[0].resize(12);CHECK(!material::from_columns(editor::TerrainKind::Effect,columns,sentinel,error));CHECK(sentinel==input);
    // Deleting the final recipe row still leaves a deferred owned before-copy
    // that needs its exact prior materialization proof.
    witness.clear();CHECK(witness.observe(input,output,error));material::Recipe none;
    CHECK(witness.observe(none,none,error));normalized=after;CHECK(witness.normalize(normalized,error));CHECK(normalized==before);
}
static void import_counter_chaining(){
    namespace material=editor_native_terrain_materialization;
    const auto base=baseline();editor::Document d;std::string error;CHECK(project(base,{},d,error));
    for(auto row:{stroke(100,1,13),stroke(101,2,17),stroke(102,3,23,editor::TerrainKind::Model)})d.terrain.emplace(row.object,std::move(row));
    d.next_terrain_order=4;std::vector<uint8_t> authored;CHECK(merge(base,d,authored,error));
    material::Recipe input;CHECK(material::from_bytes(authored,input,error));
    const auto proxy=[](material::Recipe rows){
        for(auto& kind:rows.rows)for(auto& row:kind){
            auto& b=row.bytes;b[12]|=4;const auto count=uint16_t(material::counter(b)+1);b[14]=uint8_t(count);b[15]=uint8_t(count>>8);
            real(b,16,std::nextafter(editor::terrain_float(b,16),1000.f));
        }
        return rows;
    };
    const auto imported=[](material::Recipe rows,uint16_t count){for(auto& kind:rows.rows)for(auto& row:kind){row.bytes[14]=uint8_t(count);row.bytes[15]=uint8_t(count>>8);}return rows;};
    const auto encode_rows=[&](const material::Recipe& rows){auto document=d;size_t indices[2]{};
        for(auto& [id,row]:document.terrain){(void)id;row.payload=rows.rows[size_t(row.kind)].at(indices[size_t(row.kind)]++);}
        std::vector<uint8_t> bytes;CHECK(merge(base,document,bytes,error));return bytes;};
    material::Materialization witness;const auto first=proxy(input);CHECK(witness.observe(input,first,error));
    auto reload=imported(first,1);auto raw_reload=encode_rows(reload),unchanged=raw_reload;
    CHECK(witness.normalize(raw_reload,error)&&raw_reload==unchanged); // normalize never ignores counters.
    auto second=proxy(reload);CHECK(witness.observe(reload,second,error));auto normalized=encode_rows(second);
    CHECK(witness.normalize(normalized,error)&&normalized==authored);
    // The index includes both current and retained maps, and every candidate
    // agrees on its exact original payload across repeated native imports.
    auto old_reload=imported(first,9),old_after=proxy(old_reload);
    CHECK(witness.observe(old_reload,old_after,error));normalized=encode_rows(old_after);CHECK(witness.normalize(normalized,error)&&normalized==authored);
    auto later=imported(old_after,0xFFFF),later_after=proxy(later);
    CHECK(witness.observe(later,later_after,error));normalized=encode_rows(later_after);CHECK(witness.normalize(normalized,error)&&normalized==authored);
    // Real key/flag/matrix/scalar changes are never counter aliases.
    for(size_t offset:{size_t(0),size_t(12),size_t(32),size_t(68),size_t(72),size_t(76),size_t(80)}){
        witness.clear();CHECK(witness.observe(input,first,error));auto changed=imported(first,7);
        changed.rows[0][0].bytes[offset]^=offset==12?2:1;auto after=proxy(changed);
        CHECK(witness.observe(changed,after,error));normalized=encode_rows(after);CHECK(witness.normalize(normalized,error));
        auto wanted=d;wanted.terrain.at({100,1}).payload=changed.rows[0][0];std::vector<uint8_t> expected;CHECK(merge(base,wanted,expected,error));CHECK(normalized==expected);
    }
    // Geometrically identical rendered rows can have different FULL authored
    // sources. A reset counter cannot choose one, even if an exact key exists.
    material::Recipe collision=input;collision.rows[0][1]=collision.rows[0][0];real(collision.rows[0][1].bytes,16,17);collision.rows[0][1].bytes[14]=2;
    auto output=proxy(collision);real(output.rows[0][0].bytes,16,21);real(output.rows[0][1].bytes,16,21);
    witness.clear();CHECK(witness.observe(collision,output,error));auto ambiguous=imported(output,2),ambiguous_after=proxy(ambiguous);
    const auto retained_bytes=witness.owned_bytes();CHECK(!witness.observe(ambiguous,ambiguous_after,error)&&error.find("ambiguous")!=std::string::npos);
    CHECK(witness.owned_bytes()==retained_bytes);
    // Actual Update/Append provenance skips chaining entirely, including an
    // input that is an old rendered output with only its counter reset.
    witness.clear();CHECK(witness.observe(input,first,error));material::AuthoredRows fresh;fresh[0].insert(0);
    reload=imported(first,7);second=proxy(reload);CHECK(witness.observe(reload,second,error,fresh));normalized=encode_rows(second);CHECK(witness.normalize(normalized,error));
    auto wanted=d;wanted.terrain.at({100,1}).payload=reload.rows[0][0];std::vector<uint8_t> expected;CHECK(merge(base,wanted,expected,error));CHECK(normalized==expected);
    witness.clear();CHECK(witness.observe(input,first,error));auto appended=imported(first,1);appended.rows[0].push_back(appended.rows[0][0]);
    auto appended_after=proxy(appended);fresh={};fresh[0].insert(appended.rows[0].size()-1);
    CHECK(witness.observe(appended,appended_after,error)); // no native edit mark would borrow both old sources
    witness.clear();CHECK(witness.observe(input,first,error));
    CHECK(!witness.observe(appended,appended_after,error,fresh)); // fresh row must not borrow the old source
    // At the native bound the lookup remains one ordered index, not a scan of
    // all prior rows per input. All retained generations stay explicitly bound.
    material::Recipe many;for(size_t n=0;n<kRecipeLimit;++n){auto row=stroke(100+n,1,13).payload;little(row.bytes,0,uint32_t(n+1));many.rows[0].push_back(std::move(row));}
    witness.clear();for(unsigned round=0;round<5;++round){auto out=proxy(many);CHECK(witness.observe(many,out,error));CHECK(witness.owned_bytes()<(64u<<20));many=imported(out,uint16_t(round+17));}
    auto oversized=many;oversized.rows[0].push_back(oversized.rows[0][0]);CHECK(!witness.observe(oversized,proxy(oversized),error));
}
int main(int argc,char** argv){roundtrip();rejected();transform_counters();materialization();import_counter_chaining();if(argc>1)captured(argv[1]);if(argc>2)captured_append(argv[1],argv[2]);std::printf("%d checks, %d failures\n",checks,failures);return failures?1:0;}
