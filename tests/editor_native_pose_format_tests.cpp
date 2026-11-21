#include "editor_native_pose_format.h"
#include "editor_native_refpack.h"
#include "net/dbpf.h"
#include "../updater/sha256.h"
#include "fixtures/editor_native_pose/manifest.h"
#include <algorithm>
#include <bit>
#include <cstdlib>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <map>
#include <new>

// Deterministic real allocation failures, no production allocation hook.
static int fail_after=-1;
void* operator new(size_t n){if(fail_after==0){fail_after=-1;throw std::bad_alloc();}if(fail_after>0)--fail_after;
    if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,size_t)noexcept{std::free(p);}
void operator delete[](void* p,size_t)noexcept{std::free(p);}

namespace {
using namespace editor_native_pose_format;
using Bytes=std::vector<uint8_t>;
size_t checks=0,negatives=0,oracles=0;
void check(bool v,const std::string& why){++checks;if(!v)throw std::runtime_error(why);}
uint32_t word(const Bytes& b,size_t at,bool big=false){check(at<=b.size() && b.size()-at>=4,"test word bound");
    uint32_t v=0;for(unsigned n=0;n<4;++n)v|=uint32_t(b[at+n])<<(big?24-8*n:8*n);return v;}
void put(Bytes& b,size_t at,uint32_t v,bool big=false){check(at<=b.size() && b.size()-at>=4,"test put bound");
    for(unsigned n=0;n<4;++n)b[at+n]=uint8_t(v>>(big?24-8*n:8*n));}
Bytes read(const std::filesystem::path& p){const auto n=std::filesystem::file_size(p);check(n && n<=kImageLimit,"fixture bound");
    Bytes b(size_t(n),uint8_t{});std::ifstream f(p,std::ios::binary);f.read(reinterpret_cast<char*>(b.data()),std::streamsize(n));check(bool(f),"fixture read");return b;}
struct Case {Bytes base,after;std::vector<MarkerRow> rows;std::vector<PoseEdit> edits;};
std::array<std::span<uint32_t>,10> values(MarkerBits& b){return {b.position,b.rotation,std::span(&b.scale,1),std::span(&b.relative_scale,1),
    std::span(&b.elevation,1),std::span(&b.pitch,1),std::span(&b.gain,1),b.destination_position,b.destination_rotation,std::span(&b.destination_scale,1)};}
const std::filesystem::path fixtures="tests/fixtures/editor_native_pose";
std::vector<MarkerRow> roster(){const auto b=read(fixtures/"roster.bin");check(upd::sha256_hex(b)==pose_fixture::roster_hash,"typed/raw roster SHA");size_t at=0;
    const auto next=[&]{const auto v=word(b,at);at+=4;return v;};check(next()==0x31524650 && next()==1,"roster signature");
    const auto count=next();check(count && count<=4096 && next()==pose_fixture::body,"roster profile");std::vector<MarkerRow> rows;
    for(uint32_t n=0;n<count;++n){MarkerRow r;const uint64_t low=next();r.object.id=low|(uint64_t(next())<<32);r.object.generation=next();r.slot=next();r.class_index=next();
        for(auto field:values(r.bits))for(auto& value:field)value=next();rows.push_back(r);}
    check(at==b.size(),"roster no tail");return rows;}
std::vector<PoseEdit> edits(){const auto b=read(fixtures/"edits.bin");check(upd::sha256_hex(b)==pose_fixture::edits_hash,"independent typed edit SHA");size_t at=0;
    const auto next=[&]{const auto v=word(b,at);at+=4;return v;};check(next()==0x31454650 && next()==14,"edit signature");std::vector<PoseEdit> all;
    for(unsigned n=0;n<14;++n){PoseEdit e;const uint64_t low=next();e.object.id=low|(uint64_t(next())<<32);e.object.generation=next();e.slot=next();e.class_index=next();
        for(auto& v:e.position)v=next();for(auto& v:e.rotation)v=next();all.push_back(e);}
    check(at==b.size(),"edits no tail");return all;}
Bytes accepted(const Case& c){Bytes output{1,2,3};std::string error="old";
    const auto result=patch(c.base,c.rows,c.edits,output,error);check(result==Result::Produced,"produce: "+error);check(error.empty(),"success clears error");return output;}
void rejected(Case c,Result expected){const auto base=c.base;const auto rows=c.rows;const auto changes=c.edits;
    Bytes output{0xDE,0xAD,0xBE,0xEF};const auto sentinel=output;std::string error="old";
    const auto result=patch(c.base,c.rows,c.edits,output,error);check(result==expected,"wrong rejection category: "+error);
    check(!error.empty() && output==sentinel,"atomic failure/reason");check(c.base==base && c.rows==rows && c.edits==changes,"inputs unchanged");++negatives;}
struct Field {size_t at,size,directory;};using Fields=std::map<uint32_t,Field>;
Fields attributes(const Bytes& b,size_t at,size_t& end){check(word(b,at,true)==0x01A80D26,"test serializer");
    const auto count=word(b,at+8,true);end=at+8+word(b,at+4,true);check(end<=b.size(),"test serializer end");
    size_t cursor=at+12+count*8;Fields f;for(uint32_t n=0;n<count;++n){const auto d=at+12+n*8;const auto size=word(b,d+4,true);
        check(cursor<=end && size<=end-cursor,"test payload");check(f.emplace(word(b,d,true),Field{cursor,size,d}).second,"test duplicate");cursor+=size;}
    check(cursor==end,"test record tail");return f;}
Bytes decoded(const Bytes& b){const auto body=pose_fixture::body;const auto index=word(b,body+0x40);
    Bytes result;check(dbpf::refpack_decode(b.data()+body+96,index-96,result),"test decode");return result;}
Bytes repack(const Bytes& base,const Bytes& raw){const auto body=pose_fixture::body;const auto old=word(base,body+0x40);
    Bytes packed;std::string error;check(editor_native_refpack::encode(raw,raw.size()>250000?editor_native_refpack::Window::Small:editor_native_refpack::Window::Large,packed,error),"test repack");
    Bytes b(base.begin(),base.begin()+body+96);b.insert(b.end(),packed.begin(),packed.end());const auto index=96+packed.size();
    b.insert(b.end(),base.begin()+body+old,base.end());put(b,body-4,uint32_t(index+36));put(b,body+0x40,uint32_t(index));
    put(b,body+index+24,uint32_t(packed.size())|0x80000000u);put(b,body+index+28,uint32_t(raw.size()));return b;}
Bytes packet(const Bytes& base,const Bytes& packed,uint32_t length){const auto body=pose_fixture::body;const auto old=word(base,body+0x40);
    Bytes b(base.begin(),base.begin()+body+96);b.insert(b.end(),packed.begin(),packed.end());const auto index=96+packed.size();
    b.insert(b.end(),base.begin()+body+old,base.end());put(b,body-4,uint32_t(index+36));put(b,body+0x40,uint32_t(index));
    put(b,body+index+24,uint32_t(packed.size())|0x80000000u);put(b,body+index+28,length);return b;}
std::vector<Fields> marker_fields(const Bytes& b){size_t end=0;const auto root=attributes(b,0,end);const auto list=root.at(0x07102E6C);
    const auto count=word(b,list.at,true);size_t cursor=list.at+4;std::vector<Fields> rows;
    for(uint32_t n=0;n<count;++n){rows.push_back(attributes(b,cursor,end));cursor=end;}check(cursor==list.at+list.size,"test marker tail");return rows;}
void closure(const Case& c,const Bytes& output){auto before=decoded(c.base);const auto after=decoded(output);const auto fields=marker_fields(before);
    for(const auto& e:c.edits){for(size_t n=0;n<3;++n)put(before,fields[e.slot].at(0x071147E6).at+n*4,e.position[n],true);
        if(e.rotate)for(size_t n=0;n<4;++n)put(before,fields[e.slot].at(0x071147E7).at+n*4,e.rotation[n],true);}
    check(before==after,"all decoded unknown bytes preserved");
    check(std::equal(c.base.begin(),c.base.begin()+pose_fixture::body-4,output.begin()),"source and entire prefix preserved");}
void negatives_for(Case original){
    const auto bad=[&](Result expected,const auto& change){auto c=original;change(c);rejected(std::move(c),expected);};
    bad(Result::Unsupported,[](auto& c){c.edits.clear();});bad(Result::Unsupported,[](auto& c){c.edits.resize(65,c.edits.front());});
    bad(Result::Error,[](auto& c){c.edits.push_back(c.edits.front());});
    bad(Result::Error,[](auto& c){c.rows.pop_back();});bad(Result::Error,[](auto& c){c.rows[0].object.id=0;});
    bad(Result::Error,[](auto& c){c.rows[0].object.generation=0;});bad(Result::Error,[](auto& c){c.rows[0].slot=UINT32_MAX;});
    bad(Result::Error,[](auto& c){c.rows[0].slot=c.rows[1].slot;});bad(Result::Error,[](auto& c){c.rows[0].object=c.rows[1].object;});
    bad(Result::Error,[](auto& c){++c.rows[0].class_index;});
    for(size_t f=0;f<10;++f)for(size_t n=0;n<values(original.rows[0].bits)[f].size();++n)
        bad(Result::Error,[&](auto& c){values(c.rows[0].bits)[f][n]^=1;});
    bad(Result::Error,[](auto& c){c.edits[0].slot=UINT32_MAX;});bad(Result::Error,[](auto& c){++c.edits[0].object.id;});
    bad(Result::Error,[](auto& c){++c.edits[0].object.generation;});bad(Result::Error,[](auto& c){++c.edits[0].class_index;});
    for(auto bits:{0x7F800000u,0xFF800000u,0x7FC00001u,0xFFC00000u}){
        bad(Result::Error,[&](auto& c){c.edits[0].position[0]=bits;});bad(Result::Error,[&](auto& c){c.edits[0].rotation[1]=bits;});}
    bad(Result::Error,[](auto& c){c.edits[0].rotation={0,0,0,0};});
    bad(Result::Error,[](auto& c){c.edits[0].position[0]=std::bit_cast<uint32_t>(1e8f);});
    bad(Result::Error,[](auto& c){c.base.clear();});bad(Result::Unsupported,[](auto& c){c.base.resize(kImageLimit+1);});
    for(size_t length:{size_t(1),size_t(7),pose_fixture::body-1,pose_fixture::body+95})bad(Result::Error,[&](auto& c){c.base.resize(length);});
    bad(Result::Unsupported,[](auto& c){put(c.base,4,16);});bad(Result::Unsupported,[](auto& c){put(c.base,pose_fixture::body-8,16);});
    bad(Result::Error,[](auto& c){put(c.base,pose_fixture::body-4,UINT32_MAX);});
    bad(Result::Error,[](auto& c){put(c.base,pose_fixture::body+0x40,UINT32_MAX);});
    bad(Result::Unsupported,[](auto& c){put(c.base,pose_fixture::body+0x44,1);});
    bad(Result::Unsupported,[](auto& c){c.base.push_back(0);put(c.base,pose_fixture::body-4,uint32_t(c.base.size()-pose_fixture::body));});
    bad(Result::Unsupported,[](auto& c){c.base[pose_fixture::body+96]=0xD0;}); // extended header rejected before decoder
    bad(Result::Error,[](auto& c){c.base[pose_fixture::body+98]^=0x80;});
    for(size_t n=0;n<5;++n){auto c=original;c.base=packet(c.base,Bytes(n,0),50);rejected(std::move(c),Result::Error);}
    {auto c=original;c.base=packet(c.base,{0x50,0xFB,0,0,50},16000001);rejected(std::move(c),Result::Unsupported);}
    const auto raw=decoded(original.base);const auto rows=marker_fields(raw);size_t end=0;const auto root=attributes(raw,0,end);
    const auto inner=[&](Result expected,const auto& change){auto c=original;auto bytes=raw;change(bytes);c.base=repack(c.base,bytes);rejected(std::move(c),expected);};
    inner(Result::Error,[](auto& b){put(b,4,UINT32_MAX,true);});inner(Result::Unsupported,[](auto& b){put(b,8,129,true);});
    inner(Result::Error,[](auto& b){put(b,20,word(b,12,true),true);});inner(Result::Error,[](auto& b){put(b,16,UINT32_MAX,true);});
    inner(Result::Error,[](auto& b){b.push_back(0);});
    inner(Result::Error,[&](auto& b){const auto f=root.at(0x072A9B58),g=root.at(0x07102E6A);
        put(b,f.directory+4,3,true);put(b,g.directory+4,uint32_t(g.size+f.size-3),true);});
    inner(Result::Error,[&](auto& b){put(b,rows[0].at(0x071147E5).at,UINT32_MAX,true);});
    for(auto key:{0x071147E6u,0x071147E7u,0x071147E8u,0x071147E9u,0x071147EAu,0x078D5A93u,0x078D5A94u,0x078D5895u,0x071148E7u,0x071148EAu})
        inner(Result::Unsupported,[&](auto& b){put(b,rows[0].at(key).directory,0xF00DBEEF,true);});
    inner(Result::Unsupported,[&](auto& b){put(b,rows[0].at(0x071148E9).at,1,true);});
    inner(Result::Error,[&](auto& b){b[rows[0].at(0x071147EB).at+4]=2;});
    inner(Result::Error,[&](auto& b){put(b,rows[0].at(0x076CA8D3).at,9,true);});
    inner(Result::Error,[&](auto& b){put(b,rows[0].at(0x071147E6).directory+4,UINT32_MAX,true);});
    for(auto key:{0x072A9B58u,0x07102E6Au,0x07102E6Cu})inner(Result::Unsupported,[&](auto& b){put(b,root.at(key).at,0,true);});
    inner(Result::Unsupported,[&](auto& b){put(b,root.at(0x07102E6C).at,4097,true);});
    // Opaque data is preserved, never blessed as a trusted Native BASE.
    auto opaque=original;auto altered=raw;bool changed=false;
    for(const auto& [key,f]:root)if(key!=0x072A9B58 && key!=0x07102E6A && key!=0x07102E6C && f.size){altered[f.at+f.size-1]^=1;changed=true;break;}
    check(changed,"opaque fixture field");opaque.base=repack(original.base,altered);closure(opaque,accepted(opaque));
    for(int n=0;n<24;++n){Bytes output{0xFA,0xCE};const auto old=output;std::string error;fail_after=n;
        const auto result=patch(original.base,original.rows,original.edits,output,error);fail_after=-1;
        check(result==Result::Error && output==old && !error.empty(),"allocation failure atomic");++negatives;}
}
void run(){auto rows=roster();const auto changes=edits();std::array<Bytes,15> images;
    for(size_t n=0;n<images.size();++n){images[n]=read(fixtures/("r"+std::to_string(n+48)+".native.bin"));
        check(upd::sha256_hex(images[n])==pose_fixture::hashes[n],"genuine artifact SHA");}
    const Case original{images[0],images[1],rows,{changes[0]}};
    for(size_t n=0;n<changes.size();++n){Case c{images[n],images[n+1],rows,{changes[n]}};
        for(unsigned role:{95u,96u}){(void)role;auto out=accepted(c);check(out==c.after,"whole genuine Native AFTER equality");closure(c,out);++oracles;
            auto only=c;only.edits[0].rotate=false;check(accepted(only)!=c.after,"position-only contract negative");
            auto inverse=c;inverse.base=out;inverse.edits[0].position=rows[changes[n].slot].bits.position;inverse.edits[0].rotation=rows[changes[n].slot].bits.rotation;
            inverse.rows[changes[n].slot].bits.position=changes[n].position;inverse.rows[changes[n].slot].bits.rotation=changes[n].rotation;
            check(accepted(inverse)==c.base,"whole exact inverse");}
        rows[changes[n].slot].bits.position=changes[n].position;rows[changes[n].slot].bits.rotation=changes[n].rotation;
    }
    auto multi=original;PoseEdit extra;extra.object=multi.rows[0].object;extra.slot=0;extra.class_index=multi.rows[0].class_index;
    extra.position=multi.rows[0].bits.position;extra.rotation=multi.rows[0].bits.rotation;extra.position[0]^=1;multi.edits.push_back(extra);
    closure(multi,accepted(multi));
    auto alias=original.base;std::string error;check(patch(alias,original.rows,original.edits,alias,error)==Result::Produced && alias==original.after,"input/output alias atomic success");
    auto stale_alias=original.base;auto stale_rows=original.rows;stale_rows[0].bits.gain^=1;
    check(patch(stale_alias,stale_rows,original.edits,stale_alias,error)==Result::Error && stale_alias==original.base,"input/output alias atomic rejection");
    auto reordered=original;std::reverse(reordered.rows.begin(),reordered.rows.end());check(accepted(reordered)==original.after,"full roster order independent, slots strict");
    negatives_for(original);
}
}
int main(){try{run();std::cout<<"PASS pose format checks="<<checks<<" genuine_pairs="<<oracles<<" atomic_negatives="<<negatives<<" provenance=not_minted\n";return 0;}
    catch(const std::exception& e){fail_after=-1;std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
