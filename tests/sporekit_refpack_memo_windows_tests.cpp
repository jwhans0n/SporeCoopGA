// Real Kit memo + exact extracted GOG RAW. No game/module/native caller is run.
#include "../sporekit/core.h"
#include "../mods/coop/net/dbpf.h"
#include "../updater/sha256.h"
#include "fixtures/editor_native_pose/manifest.h"
#include <array>
#include <atomic>
#include <condition_variable>
#include <cstdio>
#include <cstring>
#include <fstream>
#include <mutex>
#include <new>
#include <stdexcept>
#include <thread>

static unsigned checks=0;
static void check(bool value,const char* text){++checks;if(!value)throw std::runtime_error(text);}
static std::atomic<unsigned> fail_array=0;
void* operator new[](size_t n){auto remaining=fail_array.load();if(remaining && fail_array.fetch_sub(1)==1)throw std::bad_alloc();return ::operator new(n);}
void operator delete[](void* p)noexcept{::operator delete(p);}

#include "../sporekit/refpack_memo.cpp"

namespace {
using Raw=uint32_t(SK_CDECL*)(void*,const void*,uint32_t,uint32_t,uint32_t*,uint32_t*);
Raw extracted=nullptr;
std::array<uint8_t,0x3D0+269> image;
std::atomic<unsigned> native_calls=0;
bool opt_in=true,unsupported=false,install_failure=false,reenter=false;
unsigned installs=0;
bool override_result=false;
bool original_throws=false;
std::mutex block_mutex;std::condition_variable block_cv;
bool block_native=false,entered_native=false,release_native=false;
std::vector<uint8_t> nested_input(80,0x44),nested_output(160);
std::vector<uint32_t> nested_hash(65536),nested_links(0x4000);
uint32_t SK_CDECL original(void* out,const void* in,uint32_t n,uint32_t mask,uint32_t* hash,uint32_t* links){
    const auto call=++native_calls;
    if(original_throws)throw std::runtime_error("controlled original exception");
    if(override_result)return 7;
    if(block_native && call==1){std::unique_lock<std::mutex> lock(block_mutex);entered_native=true;block_cv.notify_all();block_cv.wait(lock,[]{return release_native;});}
    if(reenter){reenter=false;const auto count=sk::memo_raw(nested_output.data(),nested_input.data(),uint32_t(nested_input.size()),0x3FFF,
        nested_hash.data(),nested_links.data(),sk::caller_address+0xD0);check(count>0,"reentrant RAW original result");}
    return extracted(out,in,n,mask,hash,links);
}
uintptr_t call_target(size_t offset){int32_t value;std::memcpy(&value,image.data()+offset+1,4);return uintptr_t(image.data())+offset+5+uint32_t(value);}
void reset(){
    sk::memo_enabled=false;sk::memo_attempted=false;sk::memo_entries={};sk::memo_bytes=sk::memo_age=0;
    sk::memo_hits=sk::memo_misses=sk::memo_bypass=sk::memo_contention=sk::memo_failures=sk::memo_calls=0;
    sk::raw_original=nullptr;sk::raw_address=sk::caller_address=0;sk::memo_busy=false;
    native_calls=0;installs=0;opt_in=true;unsupported=install_failure=reenter=override_result=original_throws=false;fail_array=0;
    block_native=entered_native=release_native=false;
    image.fill(0xCC);std::copy(sk::refpack_profile::raw.begin(),sk::refpack_profile::raw.end(),image.begin());
    auto caller=sk::refpack_profile::caller;
    const std::array<uint32_t,2> tags{0x11110000,0x22220000};
    for(size_t i=0;i<2;++i)std::memcpy(caller.data()+sk::refpack_profile::caller_operands[i],&tags[i],4);
    std::copy(caller.begin(),caller.end(),image.begin()+0x3D0);
}
std::vector<uint8_t> file(const char* name){std::ifstream in(name,std::ios::binary|std::ios::ate);check(bool(in),"retained genuine input");const auto size=in.tellg();
    check(size>0 && size<16000000,"genuine input bound");std::vector<uint8_t> bytes(static_cast<size_t>(size));in.seekg(0);in.read(reinterpret_cast<char*>(bytes.data()),size);check(bool(in),"genuine input read");return bytes;}
struct Encoder {
    void* code=nullptr;
    Encoder(){code=VirtualAlloc(nullptr,sk::refpack_profile::raw.size(),MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE);check(code,"extracted code allocation");
        auto* p=static_cast<uint8_t*>(code);std::memcpy(p,sk::refpack_profile::raw.data(),sk::refpack_profile::raw.size());
        for(size_t i=0;i<sk::refpack_profile::crt_calls.size();++i){const auto at=sk::refpack_profile::crt_calls[i];
            const auto target=i?uintptr_t(&std::memcpy):uintptr_t(&std::memset);const auto delta=uint32_t(target-uintptr_t(p+at+5));std::memcpy(p+at+1,&delta,4);}
        DWORD old=0;check(VirtualProtect(code,sk::refpack_profile::raw.size(),PAGE_EXECUTE_READ,&old),"extracted RX");
        check(FlushInstructionCache(GetCurrentProcess(),code,sk::refpack_profile::raw.size()),"extracted instruction cache");extracted=reinterpret_cast<Raw>(code);
    }
    ~Encoder(){VirtualFree(code,0,MEM_RELEASE);}
};
struct Buffers {
    std::vector<uint8_t> input,output;std::vector<uint32_t> hash,links;
    explicit Buffers(std::vector<uint8_t> bytes,uint32_t mask=0x3FFF):input(std::move(bytes)),output(input.size()*20/16+64,0xC7),hash(65536,0xA1A1A1A1),links(size_t(mask)+1,0xB2B2B2B2){}
    uint32_t call(uint32_t mask=0x3FFF,uintptr_t caller=0){return sk::memo_raw(output.data(),input.data(),uint32_t(input.size()),mask,hash.data(),links.data(),caller?caller:sk::caller_address+0xD0);}
};
void initialized(){sk::refpack_memo_init();check(sk::memo_enabled && installs==1,"verified opt-in install");check(sk::code_profile(true),"owned installed profile");}
}
namespace sk {
SkMod g_kit_mod;
const std::wstring& mods_dirw(){static const std::wstring directory=L"controlled";return directory;}
int ini_int(const std::wstring&,const char*,const char*,int){return opt_in?1:0;}
std::string exe_sha256(){return unsupported?"unknown":"dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37";}
uintptr_t sym(const char* name){const std::string value(name);
    if(value=="SporeKit::RefPack::Raw")return uintptr_t(image.data());
    if(value=="SporeKit::RefPack::AllocatingEncoder")return uintptr_t(image.data()+0x3D0);
    if(value=="SporeKit::RefPack::HashAllocationTag")return 0x11110000;
    if(value=="SporeKit::RefPack::LinkAllocationTag")return 0x22220000;
    if(value=="SporeKit::RefPack::MemsetThunk")return call_target(refpack_profile::crt_calls[0]);
    if(value=="SporeKit::RefPack::MemcpyThunk")return call_target(refpack_profile::crt_calls[1]);return 0;
}
int hook_create(SkMod* owner,void* target,void* detour,void** old){check(owner==&g_kit_mod && target==image.data(),"process-lifetime Kit owns trampoline");++installs;
    if(install_failure)return 1;*old=reinterpret_cast<void*>(original);image[0]=0xE9;const auto delta=uint32_t(uintptr_t(detour)-uintptr_t(image.data()+5));std::memcpy(image.data()+1,&delta,4);return 0;
}
}
int main(){try{
    Encoder encoder;const auto native=file("tests/fixtures/editor_native_pose/r48.native.bin");
    check(upd::sha256_hex(native)==pose_fixture::hashes[0],"pinned genuine Native fixture");
    uint32_t index=0;std::memcpy(&index,native.data()+pose_fixture::body+0x40,4);
    const std::vector<uint8_t> compressed(native.begin()+pose_fixture::body+96,native.begin()+pose_fixture::body+index);
    std::vector<uint8_t> input;check(dbpf::refpack_decode(compressed.data(),compressed.size(),input),"genuine decode");
    reset();initialized();Buffers a(input);const auto first=a.call();check(first+5==compressed.size(),"Native raw size");
    check(std::equal(a.output.begin(),a.output.begin()+first,compressed.begin()+5),"genuine Native raw oracle");
    const auto output=a.output;std::fill(a.hash.begin(),a.hash.end(),0x12345678);std::fill(a.links.begin(),a.links.end(),0x87654321);
    const auto hash=a.hash,links=a.links;check(a.call()==first && native_calls==1 && sk::memo_hits==1,"identical full input hit");
    check(a.output==output && a.hash==hash && a.links==links,"hit reproduces exact bytes and leaves dead scratch untouched");
    Buffers moved(input);check(moved.call()==first && native_calls==1,"different pointer identical bytes hit");
    const auto before_query=native_calls.load();
    check(sk::memo_raw(nullptr,input.data(),uint32_t(input.size()),0x3FFF,a.hash.data(),a.links.data(),sk::caller_address+0xE9)==first && native_calls==before_query+1,"real null-output query remains native");
    for(size_t at:{size_t(0),input.size()/2,input.size()-1}){Buffers changed(input);changed.input[at]^=1;const auto before=native_calls.load();check(changed.call()>0 && native_calls==before+1,"every changed input region misses");}
    reset();initialized();Buffers large(input,0x1FFFF);large.call(0x1FFFF);const auto before_mask=native_calls.load();large.call();check(native_calls==before_mask+1,"mask is exact key");
    override_result=true;for(unsigned mode=0;mode<6;++mode){const auto before=native_calls.load();
        if(mode==0)sk::memo_raw(nullptr,input.data(),uint32_t(input.size()),0x3FFF,a.hash.data(),a.links.data(),sk::caller_address+0xE9);
        if(mode==1)a.call(0x3FFF,sk::caller_address+1);
        if(mode==2)sk::memo_raw(a.output.data(),input.data(),16000001,0x3FFF,a.hash.data(),a.links.data(),sk::caller_address+0xD0);
        if(mode==3)sk::memo_raw(a.output.data(),input.data(),UINT32_MAX,0x3FFF,a.hash.data(),a.links.data(),sk::caller_address+0xD0);
        if(mode==4)a.call(0x1234);
        if(mode==5)sk::memo_raw(a.output.data(),input.data(),15000000,0x3FFF,a.hash.data(),a.links.data(),sk::caller_address+0xD0);
        check(native_calls==before+1,"query/foreign/bounds/mask/budget original fallback");}
    override_result=false;
    for(unsigned failure=1;failure<=2;++failure){reset();initialized();Buffers value(input);fail_array=failure;
        check(value.call()==first && native_calls==1 && sk::memo_failures==1 && !sk::memo_entries[0].input,"allocation failure returns one genuine Native result");}
    reset();initialized();reenter=true;Buffers recursive(input);check(recursive.call()==first && native_calls==2 && sk::memo_bypass>0,"TLS reentry bypasses lock safely");
    reset();initialized();Buffers held(input),contended(input);block_native=true;
    std::thread worker([&]{held.call();});{std::unique_lock<std::mutex> lock(block_mutex);block_cv.wait(lock,[]{return entered_native;});}
    check(sk::refpack_memo_info().find("bytes=busy")!=std::string::npos,"diagnostic never waits for native compression");
    contended.call();check(sk::memo_contention==1 && native_calls==2,"contended call bypasses without waiting for native miss");
    {std::lock_guard<std::mutex> lock(block_mutex);release_native=true;}block_cv.notify_all();
    worker.join();check(sk::memo_bytes+sizeof(sk::memo_entries)<=sk::budget,"bounded concurrent ownership");
    reset();initialized();original_throws=true;bool propagated=false;
    try{a.call();}catch(const std::runtime_error&){propagated=true;}
    check(propagated && !sk::memo_busy && sk::memo_failures==0,"original exception propagates with guards unwound");
    original_throws=false;check(a.call()==first,"later call succeeds after original exception");
    reset();initialized();for(unsigned i=0;i<5;++i){auto data=input;data[i]^=uint8_t(i+1);Buffers value(std::move(data));value.call();
        check(unsigned(bool(sk::memo_entries[0].input))+unsigned(bool(sk::memo_entries[1].input))<=2 && sk::memo_bytes+sizeof(sk::memo_entries)<=sk::budget,"two-entry eviction bound");}
    reset();opt_in=false;sk::refpack_memo_init();check(!installs && !sk::memo_enabled,"INI opt-out untouched");
    reset();unsupported=true;sk::refpack_memo_init();check(!installs,"unknown executable untouched");
    reset();image[700]^=1;sk::refpack_memo_init();check(!installs,"foreign RAW code rejected");
    reset();image[0x3D0+200]^=1;sk::refpack_memo_init();check(!installs,"foreign caller rejected");
    reset();install_failure=true;sk::refpack_memo_init();check(!sk::memo_enabled,"hook failure does not enable memo");
    reset();initialized();Buffers profile(input);profile.call();const auto before=native_calls.load();image[700]^=1;
    profile.call();check(native_calls==before+1 && sk::memo_bypass>0,"later code drift bypasses old entry");
    check(sk::refpack_memo_info().find("refpack_memo enabled=")!=std::string::npos,"passive diagnostic");
    std::printf("Kit RefPack memo: %u checks passed\n",checks);return 0;
}catch(const std::exception& error){std::printf("FAIL: %s\n",error.what());return 1;}}
