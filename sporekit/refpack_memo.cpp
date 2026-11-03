// Exact native compression memo only. Every ScenarioWrite and its opaque guards
// still execute. No snapshot, native owner or caller pointer is retained.
#include "core.h"
#include "refpack_memo.h"
#include "refpack_memo_profile.h"
#include "../sdk/include/sporekit_game.hpp"
#include <array>
#include <atomic>
#include <cstring>
#include <memory>
#include <mutex>

namespace sk {
namespace {
using Raw=uint32_t(SK_CDECL*)(void*,const void*,uint32_t,uint32_t,uint32_t*,uint32_t*);
Raw raw_original=nullptr;
uintptr_t raw_address=0,caller_address=0,hash_tag=0,link_tag=0,memset_thunk=0,memcpy_thunk=0;
std::atomic<bool> memo_enabled=false;
bool memo_attempted=false;
std::atomic<const char*> memo_reason="not-initialized";
constexpr size_t budget=32u<<20;
struct Entry {
    std::unique_ptr<uint8_t[]> input,output;
    uint32_t size=0,mask=0,encoded=0;uint64_t age=0;
    size_t bytes()const{return size_t(size)+encoded;}
};
// A native write can alternate multiple nested byte streams. Retain their
// exact results together rather than evicting each level on every write.
std::array<Entry,8> memo_entries;
std::mutex memo_mutex;
size_t memo_bytes=0;uint64_t memo_age=0;
thread_local bool memo_busy=false;
std::atomic<uint64_t> memo_hits=0,memo_misses=0,memo_bypass=0,memo_contention=0,memo_failures=0,memo_calls=0;
uint32_t SK_CDECL raw_detour(void*,const void*,uint32_t,uint32_t,uint32_t*,uint32_t*);

bool code_profile(bool installed){
    if(!raw_address || !caller_address || !skg::readable(reinterpret_cast<void*>(raw_address),refpack_profile::raw.size()) ||
       !skg::readable(reinterpret_cast<void*>(caller_address),refpack_profile::caller.size()))return false;
    const auto* code=reinterpret_cast<const uint8_t*>(raw_address);
    size_t begin=0;
    if(installed){
        int32_t jump=0;std::memcpy(&jump,code+1,4);
        if(code[0]!=0xE9 || raw_address+5+uint32_t(jump)!=uintptr_t(raw_detour))return false;
        begin=5;
    }
    if(std::memcmp(code+begin,refpack_profile::raw.data()+begin,refpack_profile::raw.size()-begin))return false;
    auto caller=refpack_profile::caller;
    const std::array<uintptr_t,2> tags{hash_tag,link_tag};
    for(size_t i=0;i<tags.size();++i){const auto value=uint32_t(tags[i]);std::memcpy(caller.data()+refpack_profile::caller_operands[i],&value,4);}
    if(std::memcmp(reinterpret_cast<const void*>(caller_address),caller.data(),caller.size()))return false;
    for(size_t i=0;i<refpack_profile::crt_calls.size();++i){
        const auto offset=refpack_profile::crt_calls[i];int32_t displacement=0;std::memcpy(&displacement,code+offset+1,4);
        if(raw_address+offset+5+uint32_t(displacement)!=(i?memcpy_thunk:memset_thunk))return false;
    }
    return true;
}
bool disjoint(uintptr_t a,size_t an,uintptr_t b,size_t bn){
    return an<=UINTPTR_MAX-a && bn<=UINTPTR_MAX-b && (a+an<=b || b+bn<=a);
}
uint32_t invoke(void* out,const void* in,uint32_t n,uint32_t mask,uint32_t* hash,uint32_t* links){
    ++memo_calls;return raw_original(out,in,n,mask,hash,links);
}
uint32_t memo_raw(void* out,const void* in,uint32_t n,uint32_t mask,uint32_t* hash,uint32_t* links,uintptr_t caller){
    const uint64_t bound=uint64_t(n)*20/16+27;
    if(!memo_enabled.load() || memo_busy || caller!=caller_address+0xD0 || !out || !in || !hash || !links ||
       n<50 || n>16000000 || (mask!=0x3FFF && mask!=0x1FFFF) || uint64_t(n)+bound+sizeof(memo_entries)>budget ||
       !disjoint(uintptr_t(in),n,uintptr_t(out),size_t(bound)) || !code_profile(true)){
        ++memo_bypass;return invoke(out,in,n,mask,hash,links);
    }
    struct Busy {Busy(){memo_busy=true;}~Busy(){memo_busy=false;}} busy;
    std::unique_lock<std::mutex> lock(memo_mutex,std::try_to_lock);
    if(!lock.owns_lock()){++memo_contention;return invoke(out,in,n,mask,hash,links);}
    for(auto& entry:memo_entries)if(entry.input && entry.size==n && entry.mask==mask && !std::memcmp(entry.input.get(),in,n)){
        std::memcpy(out,entry.output.get(),entry.encoded);entry.age=++memo_age;++memo_hits;return entry.encoded;
    }
    ++memo_misses;
    auto evict=[](Entry& entry){memo_bytes-=entry.bytes();entry={};};
    // Reserve the complete worst-case staging budget before allocating. No
    // in-flight shared entries escape the lock; contention never waits on RAW.
    auto free_entry=[]()->Entry*{for(auto& entry:memo_entries)if(!entry.input)return &entry;return nullptr;};
    while(memo_bytes+size_t(n)+size_t(bound)+sizeof(memo_entries)>budget || !free_entry()){
        Entry* victim=nullptr;
        for(auto& entry:memo_entries)if(entry.input && (!victim || entry.age<victim->age))victim=&entry;
        if(!victim)return invoke(out,in,n,mask,hash,links);
        evict(*victim);
    }
    bool called=false,completed=false;uint32_t result=0;
    try{
        auto input=std::make_unique<uint8_t[]>(n);std::memcpy(input.get(),in,n);
        called=true;result=invoke(out,in,n,mask,hash,links);completed=true;
        if(!result || result>bound || std::memcmp(input.get(),in,n) || !code_profile(true))return result;
        auto output=std::make_unique<uint8_t[]>(result);std::memcpy(output.get(),out,result);
        auto& entry=*free_entry();
        entry={std::move(input),std::move(output),n,mask,result,++memo_age};memo_bytes+=entry.bytes();return result;
    }catch(...){if(called && !completed)throw;++memo_failures;if(completed)return result;}
    return invoke(out,in,n,mask,hash,links);
}
uint32_t SK_CDECL raw_detour(void* out,const void* in,uint32_t n,uint32_t mask,uint32_t* hash,uint32_t* links){
    return memo_raw(out,in,n,mask,hash,links,uintptr_t(__builtin_return_address(0)));
}
}
void refpack_memo_init(){
    if(memo_attempted)return;memo_attempted=true;
    if(ini_int(mods_dirw()+L"\\sporekit.ini","sporekit","refpack_memo",0)!=1){memo_reason="opt-out";return;}
    if(!skg::supported_gog2024(exe_sha256().c_str())){memo_reason="unsupported-exe";return;}
    raw_address=sym("SporeKit::RefPack::Raw");caller_address=sym("SporeKit::RefPack::AllocatingEncoder");
    hash_tag=sym("SporeKit::RefPack::HashAllocationTag");link_tag=sym("SporeKit::RefPack::LinkAllocationTag");
    memset_thunk=sym("SporeKit::RefPack::MemsetThunk");memcpy_thunk=sym("SporeKit::RefPack::MemcpyThunk");
    if(!hash_tag || !link_tag || !memset_thunk || !memcpy_thunk || !code_profile(false)){memo_reason="code-conflict";return;}
    if(hook_create(&g_kit_mod,reinterpret_cast<void*>(raw_address),reinterpret_cast<void*>(raw_detour),reinterpret_cast<void**>(&raw_original))!=0 || !raw_original){memo_reason="hook-failed";return;}
    memo_enabled=true;memo_reason="verified";
}
std::string refpack_memo_info(){
    std::unique_lock<std::mutex> lock(memo_mutex,std::try_to_lock);
    size_t entries=0;if(lock.owns_lock())for(const auto& entry:memo_entries)entries+=bool(entry.input);
    return "refpack_memo enabled="+std::to_string(memo_enabled.load())+" reason="+memo_reason.load()+
        " hits="+std::to_string(memo_hits.load())+" misses="+std::to_string(memo_misses.load())+
        " native_calls="+std::to_string(memo_calls.load())+" bypass="+std::to_string(memo_bypass.load())+
        " contention="+std::to_string(memo_contention.load())+" failures="+std::to_string(memo_failures.load())+
        " entries="+(lock.owns_lock()?std::to_string(entries):"busy")+
        " bytes="+(lock.owns_lock()?std::to_string(memo_bytes):"busy");
}
}
