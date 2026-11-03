#pragma once
#include "editor_saved_dependencies.h"
#include "../updater/sha256.h"
#include "fixtures/editor_saved_dependencies/manifest.h"
#include <algorithm>
#include <filesystem>
#include <fstream>
#include <functional>
#include <iostream>
#include <map>
#include <stdexcept>
#include <tuple>

namespace saved_dependency_test {
using namespace editor_saved_dependencies;
namespace fs=std::filesystem;
inline uint64_t checks=0,cases=0;
inline void check(bool value,const std::string& text){++checks;if(!value)throw std::runtime_error(text);}
inline std::string digest(std::span<const uint8_t> bytes){return upd::sha256_hex(bytes.data(),bytes.size());}
inline auto order(Key key){return std::tuple{key.group,key.type,key.instance};}
struct Less {bool operator()(Key a,Key b)const{return order(a)<order(b);}};
inline Bytes file(const fs::path& path){const auto size=fs::file_size(path);check(size<=kMaxReceiptBytes,"fixture file bound");Bytes bytes(size_t(size),uint8_t{});
    std::ifstream stream(path,std::ios::binary);stream.read(reinterpret_cast<char*>(bytes.data()),std::streamsize(size));check(bool(stream),"fixture read");return bytes;}
inline Bytes encoded(const Receipt& receipt){Bytes bytes;std::string error;check(encode(receipt,bytes,error),"encode: "+error);return bytes;}
inline void put(Bytes& bytes,size_t at,uint32_t value){check(at<=bytes.size() && bytes.size()-at>=4,"test write bound");for(unsigned n=0;n<4;++n)bytes[at+n]=uint8_t(value>>(n*8));}
inline void reseal(Bytes& bytes){check(bytes.size()>=64,"seal bound");const auto hash=digest(std::span(bytes).first(bytes.size()-64));std::copy(hash.begin(),hash.end(),bytes.end()-64);}
struct Reader {
    std::span<const uint8_t> bytes;size_t at=0;
    uint32_t u32(){check(at<=bytes.size() && bytes.size()-at>=4,"test reader bound");uint32_t value=0;for(unsigned n=0;n<4;++n)value|=uint32_t(bytes[at++])<<(n*8);return value;}
    Key key(){const auto instance=u32(),type=u32(),group=u32();return {instance,type,group};}
    uint64_t u64(){const uint64_t low=u32();return low|(uint64_t(u32())<<32);}
    void text(){check(at<=bytes.size() && bytes.size()-at>=64,"test text bound");at+=64;}
};
inline uint32_t be(const Bytes& bytes,size_t at){check(at<=bytes.size() && bytes.size()-at>=4,"test Native word bound");
    return uint32_t(bytes[at])<<24|uint32_t(bytes[at+1])<<16|uint32_t(bytes[at+2])<<8|bytes[at+3];}
struct Field {size_t offset,size;};using Fields=std::map<uint32_t,Field>;
inline Fields record(const Bytes& bytes,size_t at,size_t& end){check(be(bytes,at)==0x01A80D26,"Native serializer magic");
    const auto count=be(bytes,at+8);end=at+8+be(bytes,at+4);check(count<=128 && end<=bytes.size(),"Native directory bounds");size_t cursor=at+12+size_t(count)*8;Fields fields;
    for(uint32_t n=0;n<count;++n){const auto key=be(bytes,at+12+size_t(n)*8),size=be(bytes,at+16+size_t(n)*8);
        check(cursor<=end && size<=end-cursor && fields.emplace(key,Field{cursor,size}).second,"Native field bounds/uniqueness");cursor+=size;}
    check(cursor==end,"Native record tail");return fields;}
inline void raw_reference(const Bytes& bytes,size_t at,const Reference& expected){size_t end=0;const auto fields=record(bytes,at,end);
    check(end-at==72,"actual asset serializer size");const auto key=fields.at(0x62B74270);
    const Key raw{be(bytes,key.offset+8),be(bytes,key.offset+4),be(bytes,key.offset)};const auto server=fields.at(0x62B74272).offset;
    check(raw==expected.raw && be(bytes,fields.at(0x62B74271).offset)==expected.machine &&
        ((uint64_t(be(bytes,server))<<32)|be(bytes,server+4))==expected.server &&
        be(bytes,fields.at(0x62B74273).offset)==expected.model,"all four genuine raw asset fields");}
struct Fixture {Bytes raw,golden;Domain domain;Inventory inventory;std::vector<Record> selected,admitted;Receipt receipt;};
inline Fixture fixture(){const fs::path root="tests/fixtures/editor_saved_dependencies";Fixture f;f.raw=file(root/"saved.native.bin");f.golden=file(root/"receipt.sdr");const auto package_bytes=file(root/"alias.package");
    check(digest(f.raw)==saved_fixture::raw_hash && digest(f.golden)==saved_fixture::receipt_hash && digest(package_bytes)==saved_fixture::package_hash,"exact retained genuine artifact SHAs");
    f.domain={kPolicy,"dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37",digest(Bytes{9,5})};std::string error;
    check(decode(f.golden,f.domain,f.receipt,error),"prototype independent golden receipt: "+error);f.inventory=f.receipt.inventory;
    check(f.inventory.saved==Key{0x29130B6D,kScenario,0x408A0000} && f.inventory.raw_hash==digest(f.raw) && f.inventory.raw_size==f.raw.size(),"genuine Saved identity");
    dbpf::Package package;check(package.load(package_bytes),"genuine immutable package");for(const auto& entry:package.entries()){Record row;row.key=entry.key;check(package.read(entry,row.data),"genuine package record");f.admitted.push_back(std::move(row));}
    std::sort(f.admitted.begin(),f.admitted.end(),[](const auto& a,const auto& b){return order(a.key)<order(b.key);});
    check(f.admitted.size()==4 && f.admitted.size()==f.receipt.records.size(),"four actual records");
    for(size_t n=0;n<f.admitted.size();++n)check(f.admitted[n].key==f.receipt.records[n].key && f.admitted[n].data==f.receipt.records[n].data,"exact genuine dependency key/bytes");
    const auto pollen=std::find_if(f.admitted.begin(),f.admitted.end(),[](const auto& r){return r.key.type==0x030BDEE3;});check(pollen!=f.admitted.end() && pollen->data.size()==193,"genuine193-byte metadata");
    const auto body=saved_fixture::body;Reader header{f.raw,body+0x40};const auto index=header.u32();Bytes decoded;
    check(index>=96 && body+index<=f.raw.size() && dbpf::refpack_decode(f.raw.data()+body+96,index-96,decoded),"genuine Native body decode");
    size_t end=0;const auto roots=record(decoded,0,end);check(end==decoded.size(),"genuine root no tail");
    check(be(decoded,roots.at(0x07749AD3).offset)==0 && f.inventory.crew_count==0,"genuine empty Crew");raw_reference(decoded,roots.at(0x07102E78).offset,f.inventory.references[0]);
    std::map<std::tuple<Kind,uint32_t,uint32_t>,const Reference*> refs;for(const auto& ref:f.inventory.references)check(refs.emplace(std::tuple{ref.kind,ref.index,ref.slot},&ref).second,"complete unique inventory");
    const auto classes=roots.at(0x072A9B58);size_t cursor=classes.offset;const auto count=be(decoded,cursor);cursor+=4;std::vector<uint32_t> ids;
    for(uint32_t n=0;n<count;++n){const auto id=be(decoded,cursor);cursor+=4;ids.push_back(id);const auto fields=record(decoded,cursor,end);cursor=end;
        for(auto [key,slot]:{std::pair{0x07114976u,0u},{0x96A94670u,1u},{0x96A94671u,2u}})raw_reference(decoded,fields.at(key).offset,*refs.at(std::tuple{Kind::Class,id,slot}));}
    std::sort(ids.begin(),ids.end());check(cursor==classes.offset+classes.size && count==119 && ids==f.inventory.classes && f.inventory.references.size()==358,"119 genuine classes/all358 raw slots");
    f.selected=f.admitted;f.selected.push_back({f.inventory.saved,f.raw});f.selected.push_back({{f.inventory.saved.instance,0x030BDEE3,f.inventory.saved.group},Bytes{1,2,3}});
    Receipt observed;check(make(f.domain,f.inventory.saved,f.raw,f.inventory,f.selected,f.admitted,observed,error) && encoded(observed)==f.golden,"independent prototype bytes reproduced");return f;}
}
