#include "editor_saved_dependencies_fixture.h"
#include <cstdio>
#include <cstdlib>
#include <new>
#include <type_traits>
static int fail_after=-1;
void* operator new(size_t size){if(fail_after==0){fail_after=-1;throw std::bad_alloc();}if(fail_after>0)--fail_after;if(void* p=std::malloc(size?size:1))return p;throw std::bad_alloc();}
void* operator new[](size_t size){return ::operator new(size);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,size_t)noexcept{std::free(p);}
void operator delete[](void* p,size_t)noexcept{std::free(p);}
static_assert(std::is_same_v<decltype(&editor_saved_dependencies::inspect_collisions),bool(*)(const editor_saved_dependencies::Receipt&,
    const std::vector<editor_saved_dependencies::Resident>&,editor_saved_dependencies::CollisionPlan&,std::string&)>);
namespace saved_dependency_test {
static void unchanged_decode(const Bytes& input,const Fixture& f,const std::string& name){Receipt out=f.receipt;const auto old=encoded(out);std::string error;
    check(!decode(input,f.domain,out,error),name+": unexpected decode");check(!error.empty() && encoded(out)==old,name+": non-atomic decode");++cases;}
static void codec_cases(const Fixture& f){
    auto b=encoded(f.receipt);Receipt roundtrip;std::string error;check(decode(b,f.domain,roundtrip,error) && encoded(roundtrip)==b,"full receipt deterministic roundtrip");++cases;
    const auto bad=[&](const std::string& name,const std::function<void(Inventory&,std::vector<Record>&,std::vector<Record>&)>& edit){auto inv=f.inventory;auto selected=f.selected;auto admitted=f.admitted;edit(inv,selected,admitted);Receipt out=f.receipt;const auto before=encoded(out);
        check(!make(f.domain,f.inventory.saved,f.raw,inv,selected,admitted,out,error),name+": unexpected make");check(!error.empty() && encoded(out)==before,name+": non-atomic make");++cases;};
    bad("normalized/different raw image hash",[](auto& i,auto&,auto&){i.raw_hash[0]=i.raw_hash[0]=='0'?'1':'0';});
    bad("wrong raw length",[](auto& i,auto&,auto&){++i.raw_size;});bad("wrong Saved identity",[](auto& i,auto&,auto&){++i.saved.instance;});
    bad("missing Captain observation",[](auto& i,auto&,auto&){i.references.erase(i.references.begin());});
    bad("incomplete secondary class slot",[](auto& i,auto&,auto&){i.references.erase(i.references.begin()+2);});
    bad("duplicate class slot",[](auto& i,auto&,auto&){i.references[2]=i.references[1];});
    bad("omitted class roster row",[](auto& i,auto&,auto&){i.classes.pop_back();});
    bad("wrong full class identity",[](auto& i,auto&,auto&){++i.references.back().index;});
    bad("stock inferred from absence",[](auto& i,auto&,auto&){for(auto& r:i.references)if(r.origin==Origin::Stock){r.stock_database=UINT32_MAX;break;}});
    bad("stock carries custom payload",[](auto& i,auto&,auto&){for(auto& r:i.references)if(r.origin==Origin::Stock){r.records.push_back({1,2,3});break;}});
    bad("unsupported origin",[](auto& i,auto&,auto&){i.references[0].origin=Origin(255);});
    bad("local/effective identity changed",[](auto& i,auto&,auto&){for(auto& r:i.references)if(r.origin==Origin::Immutable){++r.effective.instance;break;}});
    {auto i=f.inventory;auto selected=f.selected;for(auto& r:i.references)if(r.origin==Origin::Immutable)std::erase_if(r.records,[](auto k){return k.type==0x030BDEE3;});
        std::erase_if(selected,[](const auto& r){return r.key.type==0x030BDEE3 && r.key.group==0x40646200;});Receipt optional;
        check(make(f.domain,i.saved,f.raw,i,selected,f.admitted,optional,error) && optional.records.size()==3,"existing Collector metadata is optional");++cases;}
    bad("missing required BEM bytes",[](auto&,auto& s,auto&){std::erase_if(s,[](const auto& r){return r.key.type==0x1A99B06B;});});
    bad("custom bytes absent from admission",[](auto&,auto&,auto& a){a.pop_back();});
    bad("same admitted key different bytes",[](auto&,auto&,auto& a){a.front().data.back()^=1;});
    bad("same selected key different bytes",[](auto&,auto& s,auto&){s.front().data.back()^=1;});
    for(bool changed:{false,true}){
        auto admitted=f.admitted;auto selected=f.selected;const auto key=admitted.front().key;
        if(changed)admitted.front().data.back()^=1;else admitted.erase(admitted.begin());
        Receipt retained=f.receipt;
        check(!make(f.domain,f.inventory.saved,f.raw,f.inventory,selected,admitted,retained,error),"admission diagnostic remains rejecting");
        char label[32];std::snprintf(label,sizeof label,"%08X!%08X.%08X",key.group,key.instance,key.type);
        check(error.find(label)!=std::string::npos && error.find(changed?"different bytes":"missing admitted record")!=std::string::npos &&
            encoded(retained)==f.golden,"exact key and missing/different diagnostic without payload disclosure or publication");++cases;
    }
    bad("duplicate selected record",[](auto&,auto& s,auto&){s.push_back(s.front());});
    bad("duplicate admitted record",[](auto&,auto&,auto& a){a.push_back(a.front());});
    bad("old transport root retained",[](auto&,auto& s,auto& a){Record old{{0xE303AC15,kScenario,0x408A0000},Bytes{1,2}};s.push_back(old);a.push_back(old);});
    bad("arbitrary unrelated historical record",[](auto&,auto& s,auto& a){Record old{{0x7D123456,0x030BDEE3,0x40646200},Bytes{1,2}};s.push_back(old);a.push_back(old);});
    bad("foreign companion relation",[](auto& i,auto& s,auto& a){Record foreign{{0x7D111111,0x030BDEE3,0x40646200},Bytes{1,2}};s.push_back(foreign);a.push_back(foreign);for(auto& r:i.references)if(r.origin==Origin::Immutable){r.records.push_back(foreign.key);std::sort(r.records.begin(),r.records.end(),Less{});break;}});
    auto extra=f.admitted;extra.push_back({{0x7D123456,0x030BDEE3,0x40646200},Bytes{1,2}});Receipt selected_only;
    check(make(f.domain,f.inventory.saved,f.raw,f.inventory,f.selected,extra,selected_only,error) && encoded(selected_only)==b,"unused admitted history is not persisted");++cases;
    // Controlled Crew observations exercise Captain/Crew/all-three-class shape.
    auto crew=f.inventory;Reference crew_ref=crew.references[0];crew_ref.kind=Kind::Crew;crew_ref.index=0;crew.crew_count=1;crew.references.insert(crew.references.begin()+1,crew_ref);Receipt with_crew;
    check(make(f.domain,f.inventory.saved,f.raw,crew,f.selected,f.admitted,with_crew,error),"explicit complete Crew slot");++cases;
    auto omitted=with_crew;omitted.inventory.references.erase(omitted.inventory.references.begin()+1);Bytes sentinel{5,4,3};const auto keep=sentinel;
    check(!encode(omitted,sentinel,error) && sentinel==keep,"Crew omission encode atomic");++cases;
    auto no_classes=f.receipt;no_classes.inventory.classes.clear();no_classes.inventory.references.resize(1);no_classes.records.clear();
    check(encode(no_classes,sentinel,error) && decode(sentinel,f.domain,roundtrip,error),"empty class roster does not impose an authoring restriction");++cases;
    for(size_t n:{size_t(0),size_t(1),size_t(7),size_t(63),b.size()/2,b.size()-1}){auto short_file=b;short_file.resize(n);unchanged_decode(short_file,f,"truncated receipt");}
    for(size_t at:{size_t(0),size_t(4),size_t(8),size_t(12),size_t(76),size_t(140),size_t(152),b.size()/2,b.size()-65,b.size()-1}){auto corrupt=b;corrupt[at]^=1;unchanged_decode(corrupt,f,"receipt corruption/hash");}
    for(auto [at,value]:{std::pair{size_t(0),0u},{size_t(4),2u},{size_t(8),2u},{size_t(152),kMaxRecordBytes+1}}){auto corrupt=b;put(corrupt,at,value);reseal(corrupt);unchanged_decode(corrupt,f,"resealed invalid profile/header");}
    auto trailing=b;trailing.insert(trailing.end()-64,0);reseal(trailing);unchanged_decode(trailing,f,"resealed trailing byte");
    auto wrong=f.domain;wrong.profile=digest(Bytes{9,6});Receipt old=f.receipt;check(!decode(b,wrong,old,error) && encoded(old)==b,"cross-profile decode atomic");++cases;
    wrong=f.domain;wrong.executable=digest(Bytes{1,2,3});check(!decode(b,wrong,old,error) && encoded(old)==b,"cross-executable decode atomic");++cases;
    Reader locate{std::span(b).first(b.size()-64)};locate.u32();locate.u32();locate.u32();locate.text();locate.text();locate.key();locate.u32();locate.text();locate.u32();
    const auto class_count_at=locate.at;const auto classes=locate.u32();for(uint32_t n=0;n<classes;++n)locate.u32();const auto refs_at=locate.at;const auto refs=locate.u32();
    for(uint32_t n=0;n<refs;++n){locate.u32();locate.u32();locate.u32();locate.key();locate.u64();locate.u32();locate.u32();locate.key();locate.u32();locate.u32();const auto keys=locate.u32();for(uint32_t k=0;k<keys;++k)locate.key();}
    const auto count_at=locate.at;const auto count=locate.u32();std::vector<size_t> size_at,payload_at,record_key_at;
    for(uint32_t n=0;n<count;++n){record_key_at.push_back(locate.at);locate.key();size_at.push_back(locate.at);const auto size=locate.u32();locate.text();payload_at.push_back(locate.at);locate.at+=size;}
    check(locate.at==b.size()-64 && count==4,"test codec offset walk");
    for(auto [at,value]:{std::pair{class_count_at,1025u},{refs_at,kMaxReferences+1},{count_at,kMaxRecords+1},{size_at[0],kMaxRecordBytes+1}}){auto corrupt=b;put(corrupt,at,value);reseal(corrupt);unchanged_decode(corrupt,f,"resealed allocation/count bound");}
    for(auto at:payload_at){auto corrupt=b;corrupt[at]^=1;reseal(corrupt);unchanged_decode(corrupt,f,"per-record payload hash under valid outer checksum");}
    auto root_record=b;put(root_record,record_key_at[0]+4,kScenario);reseal(root_record);unchanged_decode(root_record,f,"resealed scenario root as dependency");
    auto duplicate=b;std::copy_n(duplicate.begin()+record_key_at[0],12,duplicate.begin()+record_key_at[1]);reseal(duplicate);unchanged_decode(duplicate,f,"resealed duplicate dependency key");
}

static void family_cases(const Fixture& f){
    for(uint32_t type:{0x2B978C46u,0x2399BE55u,0x24682294u,0x476A98C7u,0x438F6347u,0x3D97A8E4u}){
        auto inventory=f.inventory;for(auto& ref:inventory.references)if(ref.origin==Origin::Immutable)ref.raw.type=ref.effective.type=type;
        Receipt output;std::string error;check(make(f.domain,inventory.saved,f.raw,inventory,f.selected,f.admitted,output,error),"existing Collector family fixed companion profile: "+error);++cases;
    }
    auto inventory=f.inventory;for(auto& ref:inventory.references)if(ref.origin==Origin::Immutable)ref.raw.type=ref.effective.type=0xDEADBEEF;
    Receipt output=f.receipt;std::string error;check(!make(f.domain,inventory.saved,f.raw,inventory,f.selected,f.admitted,output,error) && encoded(output)==f.golden,"unknown immutable family atomic");++cases;
    auto selected=f.selected,admitted=f.admitted;Record guessed{{inventory.saved.instance,0x2F7D0004,inventory.saved.group+1},Bytes{1,2}};selected.push_back(guessed);admitted.push_back(guessed);
    check(!make(f.domain,f.inventory.saved,f.raw,f.inventory,selected,admitted,output,error) && encoded(output)==f.golden,"unverified root+1 thumbnail cannot be silently discarded");++cases;
    for(bool primary:{false,true}){auto inventory=f.inventory;Key asset;
        for(const auto& ref:inventory.references)if(ref.origin==Origin::Immutable){asset=ref.effective;break;}
        const Key body=primary?asset:Key{asset.instance,0x1A99B06B,asset.group};
        for(auto& ref:inventory.references)if(ref.origin==Origin::Immutable)ref.records={body};
        const std::vector<Record> selected_body{{body,Bytes{7,8,9}}};Receipt body_only;
        check(make(f.domain,inventory.saved,f.raw,inventory,selected_body,selected_body,body_only,error) && body_only.records.size()==1,"existing native primary-only/BEM-only closure");++cases;
    }
    {auto inventory=f.inventory;auto selected=f.admitted;
        std::erase_if(selected,[](const auto& record){return record.key.type==0x1A99B06B;});
        for(auto& ref:inventory.references)if(ref.origin==Origin::Immutable)std::erase_if(ref.records,[](auto key){return key.type==0x1A99B06B;});
        check(!make(f.domain,inventory.saved,f.raw,inventory,selected,f.admitted,output,error) && encoded(output)==f.golden,"metadata/images without primary or BEM body reject");++cases;
    }
}
static void collision_cases(const Fixture& f){
    std::vector<Resident> observations;for(const auto& row:f.receipt.records)observations.push_back({row.key,{},Installed::Missing,{}});
    uint32_t payload=0;for(const auto& row:f.receipt.records)payload+=uint32_t(row.data.size());CollisionPlan output;std::string error;
    check(inspect_collisions(f.receipt,observations,output,error) && output.missing.size()==4 && output.payload_bytes==payload,"exact cold collision facts");++cases;
    const auto bad=[&](auto changed){CollisionPlan keep;keep.payload_bytes=99;keep.missing={{1,2,3}};const auto old=keep.missing;
        check(!inspect_collisions(f.receipt,changed,keep,error) && keep.payload_bytes==99 && keep.missing==old && !error.empty(),"collision rejection atomic");++cases;};
    auto changed=observations;changed[0].state=Installed::Unknown;bad(changed);
    changed=observations;changed[0]={f.receipt.records[0].key,f.receipt.records[0].key,Installed::Exact,f.receipt.records[0].data};changed[0].effective.instance^=1;bad(changed);
    changed=observations;changed[0]={f.receipt.records[0].key,f.receipt.records[0].key,Installed::Exact,f.receipt.records[0].data};changed[0].data.back()^=1;bad(changed);
    changed=observations;changed.pop_back();bad(changed);changed=observations;changed[0].data={1};bad(changed);
    changed=observations;std::swap(changed[0],changed[1]);bad(changed);
    for(size_t n=0;n<observations.size();++n)observations[n]={f.receipt.records[n].key,f.receipt.records[n].key,Installed::Exact,f.receipt.records[n].data};
    check(inspect_collisions(f.receipt,observations,output,error) && output.missing.empty() && output.payload_bytes==payload,"all exact resident bytes; no ownership or budget granted");++cases;
}
static void zero_group_cases(const Fixture& f){
    auto inventory=f.inventory;size_t stock=inventory.references.size();
    for(size_t n=0;n<inventory.references.size();++n)if(inventory.references[n].origin==Origin::Stock){stock=n;break;}
    check(stock<inventory.references.size(),"controlled stock reference exists");
    inventory.references[stock].raw.group=inventory.references[stock].effective.group=0;
    Receipt output;std::string error;check(make(f.domain,inventory.saved,f.raw,inventory,f.selected,f.admitted,output,error),"stock group-zero key accepted");
    Receipt restored;const auto bytes=encoded(output);check(decode(bytes,f.domain,restored,error) && encoded(restored)==bytes && restored.inventory.references[stock].effective.group==0,"stock group-zero exact roundtrip");++cases;
    const auto reject=[&](auto change){auto changed=inventory;change(changed.references[stock]);Receipt keep=f.receipt;
        check(!make(f.domain,changed.saved,f.raw,changed,f.selected,f.admitted,keep,error) && !error.empty() && encoded(keep)==f.golden,"group-zero still requires valid local/effective identity");++cases;};
    reject([](auto& ref){ref.raw.instance=0;});reject([](auto& ref){ref.raw.type=0;});
    reject([](auto& ref){ref.effective.instance=0;});reject([](auto& ref){ref.effective.type=0;});reject([](auto& ref){ref.effective={};});
    // A server-backed raw key also retains group zero; its independently
    // observed effective identity remains mandatory and complete.
    auto server=inventory;server.references[stock].server=1234;
    check(make(f.domain,server.saved,f.raw,server,f.selected,f.admitted,output,error) && decode(encoded(output),f.domain,restored,error),"server raw group-zero roundtrip");++cases;
    auto records=f.admitted;Key asset;
    for(const auto& ref:inventory.references)if(ref.origin==Origin::Immutable){asset=ref.effective;break;}
    const auto remap=[&](Key key){if(key.group==asset.group)key.group=0;else if(key.group==asset.group+1)key.group=1;
        else if(key.type==0x00B1B104 && key.group==((asset.group&0xFFFF0000u)|0xE500u))key.group=0xE500;return key;};
    for(auto& row:records)row.key=remap(row.key);
    for(auto& ref:inventory.references)if(ref.origin==Origin::Immutable){ref.raw.group=ref.effective.group=0;for(auto& key:ref.records)key=remap(key);std::sort(ref.records.begin(),ref.records.end(),Less{});}
    check(make(f.domain,inventory.saved,f.raw,inventory,records,records,output,error),"already-admitted immutable group-zero closure");
    const auto encoded_zero=encoded(output);check(decode(encoded_zero,f.domain,restored,error) && encoded(restored)==encoded_zero && restored.records.size()==4,"all group-zero companions roundtrip");++cases;
    for(bool missing_type:{false,true}){auto invalid=records;if(missing_type)invalid[0].key.type=0;else invalid[0].key.instance=0;Receipt keep=f.receipt;
        check(!make(f.domain,inventory.saved,f.raw,inventory,invalid,invalid,keep,error) && encoded(keep)==f.golden,"group-zero record still requires instance/type");++cases;}
}
}
namespace saved_dependency_test {
static void allocation_cases(const Fixture& f){for(int n=0;n<8;++n){
    std::string error;Receipt output=f.receipt;const auto old=encoded(output);fail_after=n;
    const bool made=make(f.domain,f.inventory.saved,f.raw,f.inventory,f.selected,f.admitted,output,error);fail_after=-1;
    check(!made && !error.empty() && encoded(output)==old,"make allocation failure atomic");++cases;
    Bytes bytes{1,2,3};const auto sentinel=bytes;error.clear();fail_after=n;const bool wrote=encode(f.receipt,bytes,error);fail_after=-1;
    check(!wrote && !error.empty() && bytes==sentinel,"encode allocation failure atomic");++cases;
    error.clear();fail_after=n;const bool read=decode(f.golden,f.domain,output,error);fail_after=-1;
    check(!read && !error.empty() && encoded(output)==old,"decode allocation failure atomic");++cases;
}}
}
int main(){using namespace saved_dependency_test;try{const auto f=fixture();codec_cases(f);family_cases(f);collision_cases(f);zero_group_cases(f);allocation_cases(f);
    std::cout<<"PASS Saved dependency codec checks="<<checks<<" cases="<<cases<<" actual_classes=119 raw_slots=358 records=4 native_issuer=not_minted\n";return 0;
}catch(const std::exception& e){std::cerr<<"FAIL checks="<<checks<<" cases="<<cases<<": "<<e.what()<<'\n';return 1;}}
