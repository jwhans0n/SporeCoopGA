#include "editor_native_refpack.h"
#include "net/dbpf.h"
#include "../updater/sha256.h"
#include "fixtures/editor_native_pose/refpack_golden.h"
#include "fixtures/editor_native_pose/manifest.h"
#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <fstream>
#include <iostream>
#include <new>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#else
#include <sys/mman.h>
#include <unistd.h>
#endif

static int fail_after=-1;
void* operator new(size_t n){if(fail_after==0){fail_after=-1;throw std::bad_alloc();}if(fail_after>0)--fail_after;
    if(void* p=std::malloc(n?n:1))return p;throw std::bad_alloc();}
void* operator new[](size_t n){return ::operator new(n);}
void operator delete(void* p)noexcept{std::free(p);}
void operator delete[](void* p)noexcept{std::free(p);}
void operator delete(void* p,size_t)noexcept{std::free(p);}
void operator delete[](void* p,size_t)noexcept{std::free(p);}

namespace {
using namespace editor_native_refpack;
using Bytes=std::vector<uint8_t>;
size_t checks=0,cases=0;
void check(bool v,const char* reason){++checks;if(!v)throw std::runtime_error(reason);}
struct Guarded {
    uint8_t* allocation=nullptr;uint8_t* data=nullptr;size_t total=0,page=0,accessible=0,size=0;
    explicit Guarded(const Bytes& input):size(input.size()){
#ifdef _WIN32
        SYSTEM_INFO info{};GetSystemInfo(&info);page=info.dwPageSize;
#else
        page=size_t(sysconf(_SC_PAGESIZE));
#endif
        accessible=((size+page-1)/page)*page;total=accessible+2*page;
#ifdef _WIN32
        allocation=static_cast<uint8_t*>(VirtualAlloc(nullptr,total,MEM_RESERVE,PAGE_NOACCESS));check(allocation,"reserve guarded input");
        check(VirtualAlloc(allocation+page,accessible,MEM_COMMIT,PAGE_READWRITE)!=nullptr,"commit guarded input");
#else
        auto* p=mmap(nullptr,total,PROT_NONE,MAP_PRIVATE|MAP_ANONYMOUS,-1,0);check(p!=MAP_FAILED,"reserve guarded input");allocation=static_cast<uint8_t*>(p);
        check(mprotect(allocation+page,accessible,PROT_READ|PROT_WRITE)==0,"commit guarded input");
#endif
        std::memset(allocation+page,0xA5,accessible);data=allocation+page+accessible-size;std::memcpy(data,input.data(),size);
#ifdef _WIN32
        DWORD prior=0;check(VirtualProtect(allocation+page,accessible,PAGE_READONLY,&prior)!=0,"readonly guarded input");
#else
        check(mprotect(allocation+page,accessible,PROT_READ)==0,"readonly guarded input");
#endif
    }
    void verify(const Bytes& original){check(std::equal(original.begin(),original.end(),data),"readonly input unchanged");
        check(std::all_of(allocation+page,data,[](uint8_t b){return b==0xA5;}),"leading input canary");}
    ~Guarded(){if(!allocation)return;
#ifdef _WIN32
        VirtualFree(allocation,0,MEM_RELEASE);
#else
        munmap(allocation,total);
#endif
    }
};
Bytes corpus(size_t size,bool random){Bytes b(size);uint32_t state=0x1B873593;
    for(size_t n=0;n<size;++n){state^=state<<13;state^=state>>17;state^=state<<5;b[n]=random?uint8_t(state):uint8_t((n/257)%5);}return b;}
Bytes encoded(const Bytes& bytes,Window window){Guarded input(bytes);Bytes out{0xFA,0xCE};std::string error="old";
    check(encode(std::span(input.data,input.size),window,out,error),"encode guarded input");check(error.empty(),"encode clears error");input.verify(bytes);
    check(out.size()<=bytes.size()*20/16+32,"Native capacity formula");Bytes decoded;
    check(dbpf::refpack_decode(out.data(),out.size(),decoded) && decoded==bytes,"independent exact decode");return out;}
Bytes read(){std::ifstream f("tests/fixtures/editor_native_pose/r48.native.bin",std::ios::binary|std::ios::ate);check(bool(f),"genuine fixture");
    const auto size=f.tellg();check(size>0 && size<=(16u<<20),"genuine fixture bound");Bytes b(size_t(size),uint8_t{});f.seekg(0);f.read(reinterpret_cast<char*>(b.data()),size);check(bool(f),"genuine fixture read");return b;}
uint32_t word(const Bytes& b,size_t at){check(at<=b.size() && b.size()-at>=4,"fixture word");return uint32_t(b[at])|uint32_t(b[at+1])<<8|uint32_t(b[at+2])<<16|uint32_t(b[at+3])<<24;}
void run(){size_t golden=0;
    const auto test=[&](const Bytes& b,Window w){const auto actual=encoded(b,w);check(upd::sha256_hex(actual)==refpack_fixture::hashes.at(golden++),"exact frozen Native differential golden");++cases;};
    for(size_t n:std::array<size_t,14>{1,2,3,4,49,50,111,112,113,1027,1028,250000,250001,16000000})for(bool random:{false,true})
        test(corpus(n,random),n>250000?Window::Small:Window::Large);
    for(uint32_t distance:{1u,1024u,1025u,16384u,16385u,131071u,131072u})for(uint32_t length:{3u,4u,5u,10u,11u,67u,68u,1028u}){
        auto b=corpus(distance+length+64,true);for(uint32_t n=0;n<length;++n)b[distance+n]=b[n];test(b,Window::Large);}
    auto gain=corpus(60000,true);for(uint32_t n=0;n<3;++n)gain[20000+n]=gain[n];gain[20003]=uint8_t(gain[3]^0xFF);
    std::copy_n(gain.begin(),100,gain.begin()+40000);test(gain,Window::Large);check(golden==85,"complete golden corpus");
    const auto native=read();check(upd::sha256_hex(native)==pose_fixture::hashes[0],"genuine fixture SHA");const auto body=pose_fixture::body;const auto index=word(native,body+0x40);
    Bytes expected(native.begin()+body+96,native.begin()+body+index),decoded;
    check(dbpf::refpack_decode(expected.data(),expected.size(),decoded),"genuine body decode");
    for(unsigned n=0;n<3;++n){check(encoded(decoded,Window::Small)==expected,"genuine full compressed Native bytes");++cases;}
    check(encoded(decoded,Window::Large)==encoded(decoded,Window::Large),"alternate-window deterministic");++cases;
    for(const auto& input:{Bytes{},Bytes(kInputLimit+1)}){Bytes out{1,2,3};const auto old=out;std::string error;
        check(!encode(input,Window::Small,out,error) && out==old && !error.empty(),"invalid size atomic");}
    {Bytes out{1,2,3};const auto old=out;std::string error;check(!encode(gain,Window(0),out,error) && out==old && !error.empty(),"invalid window atomic");}
    for(int n=0;n<3;++n){Bytes out{1,2,3};const auto old=out;std::string error;fail_after=n;const bool okay=encode(gain,Window::Large,out,error);fail_after=-1;
        check(!okay && out==old && !error.empty(),"allocation atomic");}
    auto alias=gain;std::string error;check(encode(alias,Window::Large,alias,error),"codec alias success");Bytes restored;
    check(dbpf::refpack_decode(alias.data(),alias.size(),restored) && restored==gain,"codec alias content");
}
}
int main(){try{run();std::cout<<"PASS RefPack checks="<<checks<<" guarded_cases="<<cases<<" native_process_calls=0\n";return 0;}
    catch(const std::exception& e){fail_after=-1;std::cerr<<"FAIL after "<<checks<<": "<<e.what()<<'\n';return 1;}}
