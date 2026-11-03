#include "editor_saved_dependencies.h"
#include "../../updater/sha256.h"
#include <algorithm>
#include <cstdio>
#include <map>
#include <set>
#include <stdexcept>
#include <tuple>

namespace editor_saved_dependencies {
namespace {
void need(bool okay,const char* text){if(!okay)throw std::runtime_error(text);}
auto order(Key k){return std::tuple{k.group,k.type,k.instance};}
struct Less {bool operator()(Key a,Key b)const{return order(a)<order(b);}};
bool full(Key k){return k.instance && k.type;}
bool empty(Key k){return k==Key{};}
bool hash(const std::string& s){return s.size()==64 && std::all_of(s.begin(),s.end(),[](char c){return (c>='0'&&c<='9')||(c>='a'&&c<='f');}) && s!=std::string(64,'0');}
std::string digest(std::span<const uint8_t> b){return upd::sha256_hex(b.data(),b.size());}
std::string key_text(Key key){char text[32];std::snprintf(text,sizeof text,"%08X!%08X.%08X",key.group,key.instance,key.type);return text;}
bool family(uint32_t type){return type==0x2B978C46 || type==0x2399BE55 || type==0x24682294 || type==0x476A98C7 || type==0x438F6347 || type==0x3D97A8E4;}
bool companion(Key k,Key asset){
    if(k.instance!=asset.instance)return false;
    if(k.group==asset.group)return k.type==0x1A99B06B || k.type==asset.type || k.type==0x030BDEE3 || k.type==0x02D5C9AF || k.type==0x2F7D0004 || k.type==0x0F43029A;
    return (asset.group!=UINT32_MAX && k.group==asset.group+1 && k.type==0x2F7D0004) ||
           (k.group==((asset.group&0xFFFF0000u)|0xE500u) && k.type==0x00B1B104);
}
bool root_companion(Key k,Key saved){
    if(k.instance!=saved.instance)return false;
    if(k.group==saved.group)return k.type==0x030BDEE3 || k.type==0x02D5C9AF || k.type==0x2F7D0004 || k.type==0xFD60F0C1;
    return (k.type==0x05528D58 && k.group==0x40A00200) || (k.type==0x00B1B104 && k.group==0x408AE500);
}
void validate(const Receipt& r){
    need(r.domain.policy==kPolicy && hash(r.domain.executable) && hash(r.domain.profile),"Receipt domain/profile");
    const auto& in=r.inventory;
    need(full(in.saved) && in.saved.type==kScenario && in.raw_size && in.raw_size<=kMaxRecordBytes && hash(in.raw_hash),"Genuine Saved image identity");
    need(in.crew_count<=32 && in.classes.size()<=1024 && in.references.size()==1+in.crew_count+3*in.classes.size(),"Complete reference inventory count");
    for(size_t n=0;n<in.classes.size();++n)need(in.classes[n]<=INT32_MAX && (!n || in.classes[n-1]<in.classes[n]),"Complete unique class roster");
    std::vector<std::tuple<Kind,uint32_t,uint32_t>> slots;slots.emplace_back(Kind::Captain,0,0);
    for(uint32_t n=0;n<in.crew_count;++n)slots.emplace_back(Kind::Crew,n,0);
    for(auto id:in.classes)for(uint32_t n=0;n<3;++n)slots.emplace_back(Kind::Class,id,n);
    std::set<Key,Less> needed;
    for(size_t n=0;n<in.references.size();++n){const auto& ref=in.references[n];
        need(std::tuple{ref.kind,ref.index,ref.slot}==slots[n],"Reference slot/order mismatch");
        need(ref.records.size()<=kMaxCompanions,"Reference companion bound");
        if(ref.origin==Origin::Empty){need(empty(ref.raw) && empty(ref.effective) && (ref.server==0 || ref.server==UINT64_MAX) && ref.records.empty() && ref.stock_database==UINT32_MAX,"Empty reference shape");continue;}
        need(ref.origin==Origin::Stock || ref.origin==Origin::Immutable,"Unknown reference origin");
        need(full(ref.effective),"Effective reference identity");
        if(ref.server==0 || ref.server==UINT64_MAX)need(full(ref.raw) && ref.raw==ref.effective,"Local raw/effective reference mismatch");
        else need(ref.raw.type,"Server reference raw identity");
        if(ref.origin==Origin::Stock){need(ref.stock_database<13 && ref.records.empty(),"Explicit verified stock omission required");continue;}
        need(ref.stock_database==UINT32_MAX && family(ref.effective.type),"Unsupported immutable asset family");
        need(!ref.records.empty(),"Immutable reference has no closure");bool body=false;
        for(size_t i=0;i<ref.records.size();++i){const auto key=ref.records[i];
            need(full(key) && companion(key,ref.effective) && (!i || order(ref.records[i-1])<order(key)),"Unsupported/duplicate immutable companion relation");
            body|=key==ref.effective || key==Key{ref.effective.instance,0x1A99B06B,ref.effective.group};needed.insert(key);}
        need(body,"Immutable primary/BEM body closure required");
    }
    need(r.records.size()<=kMaxRecords && r.records.size()==needed.size(),"Exact selected closure record count");uint64_t bytes=0;
    for(size_t n=0;n<r.records.size();++n){const auto& record=r.records[n];
        need(full(record.key) && record.key.type!=kScenario && !root_companion(record.key,in.saved) && needed.contains(record.key) &&
             (!n || order(r.records[n-1].key)<order(record.key)),"Unreferenced/root/duplicate dependency record");
        need(!record.data.empty() && record.data.size()<=kMaxRecordBytes && record.data.size()<=kMaxPayloadBytes-bytes,"Dependency payload budget");bytes+=record.data.size();}
}
}
bool make(const Domain& domain,Key saved,std::span<const uint8_t> genuine_raw,const Inventory& inventory,
                 const std::vector<Record>& collected,const std::vector<Record>& admitted,Receipt& out,std::string& error){
    try{
        need(domain.policy==kPolicy && hash(domain.executable) && hash(domain.profile),"Receipt domain/profile");
        need(genuine_raw.size() && genuine_raw.size()<=kMaxRecordBytes,"Genuine raw Saved image budget");
        need(inventory.saved==saved && inventory.raw_size==genuine_raw.size() && inventory.raw_hash==digest(genuine_raw),"Inventory is not bound to the genuine raw Saved image");
        need(inventory.classes.size()<=1024 && inventory.references.size()<=kMaxReferences,"Owned reference inventory bound");
        for(const auto& ref:inventory.references)need(ref.records.size()<=kMaxCompanions,"Owned companion bound");
        need(collected.size()<=kMaxRecords && admitted.size()<=kMaxRecords,"Admission inventory bound");
        std::map<Key,const Bytes*,Less> canonical,selected;uint64_t admitted_bytes=0,collected_bytes=0;
        for(const auto& row:admitted){need(full(row.key) && canonical.emplace(row.key,&row.data).second && row.data.size()<=kMaxRecordBytes && row.data.size()<=kMaxPayloadBytes-admitted_bytes,"Immutable admission list duplicate/budget");admitted_bytes+=row.data.size();}
        Receipt candidate;candidate.domain=domain;candidate.inventory=inventory;
        for(const auto& row:collected){
            need(full(row.key) && selected.emplace(row.key,&row.data).second && row.data.size()<=kMaxRecordBytes && row.data.size()<=kMaxPayloadBytes-collected_bytes,"Collected list duplicate/budget");collected_bytes+=row.data.size();
            if(row.key==saved || root_companion(row.key,saved))continue;
            need(row.key.type!=kScenario,"Foreign/old transport scenario root is not a dependency");
            const auto prior=canonical.find(row.key);
            if(prior==canonical.end())throw std::runtime_error("Selected custom dependency was not admitted immutably: "+key_text(row.key)+" missing admitted record");
            if(*prior->second!=row.data)throw std::runtime_error("Selected custom dependency was not admitted immutably: "+key_text(row.key)+" different bytes");
            candidate.records.push_back(row);}
        std::sort(candidate.records.begin(),candidate.records.end(),[](const auto& a,const auto& b){return order(a.key)<order(b.key);});
        validate(candidate);out=std::move(candidate);error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
namespace {
struct Writer {
    Bytes bytes;
    void u32(uint32_t n){for(unsigned i=0;i<4;++i)bytes.push_back(uint8_t(n>>(i*8)));}
    void u64(uint64_t n){u32(uint32_t(n));u32(uint32_t(n>>32));}
    void key(Key k){u32(k.instance);u32(k.type);u32(k.group);}
    void text(const std::string& s){need(s.size()==64,"Hash width");bytes.insert(bytes.end(),s.begin(),s.end());}
};
struct Reader {
    std::span<const uint8_t> bytes;size_t at=0;
    uint32_t u32(){need(at<=bytes.size() && bytes.size()-at>=4,"Receipt word bound");uint32_t n=0;for(unsigned i=0;i<4;++i)n|=uint32_t(bytes[at++])<<(8*i);return n;}
    uint64_t u64(){const auto lo=u32();return uint64_t(lo)|(uint64_t(u32())<<32);}
    Key key(){const auto instance=u32(),type=u32(),group=u32();return {instance,type,group};}
    std::string text(){need(at<=bytes.size() && bytes.size()-at>=64,"Receipt hash bound");std::string s(bytes.begin()+at,bytes.begin()+at+64);at+=64;need(hash(s),"Invalid receipt hash");return s;}
    Bytes data(uint32_t n){need(at<=bytes.size() && n<=bytes.size()-at,"Receipt payload bound");Bytes b(bytes.begin()+at,bytes.begin()+at+n);at+=n;return b;}
};
}
bool encode(const Receipt& receipt,Bytes& out,std::string& error){
    try{validate(receipt);Writer w;w.u32(0x31524453);w.u32(1);w.u32(receipt.domain.policy);w.text(receipt.domain.executable);w.text(receipt.domain.profile);
        const auto& in=receipt.inventory;w.key(in.saved);w.u32(in.raw_size);w.text(in.raw_hash);w.u32(in.crew_count);w.u32(uint32_t(in.classes.size()));
        for(auto id:in.classes)w.u32(id);w.u32(uint32_t(in.references.size()));
        for(const auto& ref:in.references){w.u32(uint32_t(ref.kind));w.u32(ref.index);w.u32(ref.slot);w.key(ref.raw);w.u64(ref.server);w.u32(ref.machine);w.u32(ref.model);
            w.key(ref.effective);w.u32(uint32_t(ref.origin));w.u32(ref.stock_database);w.u32(uint32_t(ref.records.size()));for(auto key:ref.records)w.key(key);}
        w.u32(uint32_t(receipt.records.size()));for(const auto& row:receipt.records){w.key(row.key);w.u32(uint32_t(row.data.size()));w.text(digest(row.data));w.bytes.insert(w.bytes.end(),row.data.begin(),row.data.end());}
        need(w.bytes.size()<=kMaxReceiptBytes-64,"Receipt file budget");w.text(digest(w.bytes));out=std::move(w.bytes);error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool decode(std::span<const uint8_t> bytes,const Domain& expected,Receipt& out,std::string& error){
    try{need(bytes.size()>=64 && bytes.size()<=kMaxReceiptBytes,"Receipt file bound");
        const auto body=bytes.first(bytes.size()-64);const std::string seal(bytes.end()-64,bytes.end());need(hash(seal) && digest(body)==seal,"Receipt checksum mismatch");
        Reader r{body};need(r.u32()==0x31524453 && r.u32()==1,"Unknown Saved dependency receipt version");Receipt candidate;
        candidate.domain.policy=r.u32();candidate.domain.executable=r.text();candidate.domain.profile=r.text();need(candidate.domain==expected,"Receipt belongs to another executable/profile");
        auto& in=candidate.inventory;in.saved=r.key();in.raw_size=r.u32();in.raw_hash=r.text();in.crew_count=r.u32();const auto classes=r.u32();need(classes<=1024 && in.crew_count<=32,"Class/Crew roster bound");
        for(uint32_t n=0;n<classes;++n)in.classes.push_back(r.u32());const auto refs=r.u32();need(refs<=kMaxReferences && refs==1+in.crew_count+3*classes,"Complete reference inventory bound");
        for(uint32_t n=0;n<refs;++n){Reference ref;ref.kind=Kind(r.u32());ref.index=r.u32();ref.slot=r.u32();ref.raw=r.key();ref.server=r.u64();ref.machine=r.u32();ref.model=r.u32();
            ref.effective=r.key();ref.origin=Origin(r.u32());ref.stock_database=r.u32();const auto count=r.u32();need(count<=kMaxCompanions,"Companion list bound");for(uint32_t i=0;i<count;++i)ref.records.push_back(r.key());in.references.push_back(std::move(ref));}
        const auto count=r.u32();need(count<=kMaxRecords,"Dependency record count");uint64_t payload=0;
        for(uint32_t n=0;n<count;++n){Record row;row.key=r.key();const auto size=r.u32();need(size<=kMaxRecordBytes && size<=kMaxPayloadBytes-payload,"Dependency payload bound");payload+=size;const auto expected_hash=r.text();row.data=r.data(size);need(digest(row.data)==expected_hash,"Dependency record hash mismatch");candidate.records.push_back(std::move(row));}
        need(r.at==body.size(),"Receipt has trailing data");validate(candidate);out=std::move(candidate);error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
bool inspect_collisions(const Receipt& receipt,const std::vector<Resident>& observations,CollisionPlan& out,std::string& error){
    try{validate(receipt);need(observations.size()==receipt.records.size(),"Incomplete installed collision observations");CollisionPlan candidate;
        uint64_t bytes=0;for(size_t n=0;n<receipt.records.size();++n){const auto& row=receipt.records[n];const auto& observed=observations[n];
            need(observed.requested==row.key,"Installed collision observation order");bytes+=row.data.size();
            if(observed.state==Installed::Missing){need(empty(observed.effective) && observed.data.empty(),"Malformed missing record observation");candidate.missing.push_back(row.key);}
            else need(observed.state==Installed::Exact && observed.effective==row.key && observed.data==row.data,"Installed alias/key/bytes conflict or unknown observation");}
        candidate.payload_bytes=uint32_t(bytes);out=std::move(candidate);error.clear();return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
}
