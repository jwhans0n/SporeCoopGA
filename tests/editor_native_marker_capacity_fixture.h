// Production constructor hook and exact code profiles, with controlled Native
// allocator/row-copy contracts. No game, profile, resource mount or Native call.
namespace marker_capacity_fixture {
std::map<void*,size_t> arenas;
std::function<void()> allocation_callback,ctor_callback;
bool fail_allocation=false,throw_allocation=false;
unsigned constructors=0,enhancements=0,arena_frees=0;
std::vector<void*> freed;

void* raw_arena(size_t bytes){
    auto* p=std::malloc(bytes);CHECK(p);CHECK(arenas.emplace(p,bytes).second);return p;
}
void SK_CDECL free_arena(void* p){
    CHECK(p&&arenas.erase(p)==1);freed.push_back(p);++arena_frees;std::free(p);
}
void* SK_CDECL allocate_arena(size_t bytes,const char*,int,int,int,int){
    CHECK(bytes==kMarkerCapacityBytes);++enhancements;
    if(throw_allocation)throw std::bad_alloc();
    if(fail_allocation)return nullptr;
    auto* p=raw_arena(bytes);
    if(allocation_callback){auto callback=std::move(allocation_callback);allocation_callback={};callback();}
    return p;
}
void* SK_THISCALL native_ctor(void* self){
    ++constructors;std::memset(self,0,kResourceSize);
    skg::field<uintptr_t>(self,0)=uintptr_t(t_resource_vt.data());
    auto* begin=raw_arena(1024*kMarkerStride);
    skg::field<void*>(self,0x2C10)=begin;skg::field<void*>(self,0x2C14)=begin;
    skg::field<uintptr_t>(self,0x2C18)=uintptr_t(begin)+1024*kMarkerStride;
    skg::field<uint32_t>(self,0x2C24)=skg::field<uint32_t>(self,0x2C28)=0x3FFFFFFF;
    // An unrelated Native field must survive the capacity-only enhancement.
    skg::field<uint32_t>(self,0x88)=0x1234ABCD;
    if(ctor_callback){auto callback=std::move(ctor_callback);ctor_callback={};callback();}
    return self;
}
void prepare_profile(){
    namespace profile=marker_capacity_profile;
    static uint8_t* image=nullptr;
    constexpr uintptr_t lo=0xDFF000,hi=0xF48000;
    if(!image){image=static_cast<uint8_t*>(VirtualAlloc(nullptr,hi-lo,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));CHECK(image);}
    const auto bias=uint32_t(uintptr_t(image)-lo);std::array<uintptr_t,5> sites{};
    for(size_t i=0;i<sites.size();++i){
        const auto& code=profile::kCode[i];auto* target=image+code.preferred-lo;
        std::memcpy(target,code.bytes.data(),code.bytes.size());
        for(auto relocation:code.relocations){uint32_t value=0;std::memcpy(&value,target+relocation,4);value+=bias;std::memcpy(target+relocation,&value,4);}
        sites[i]=uintptr_t(target);
    }
    a_resource_vtable=uintptr_t(t_resource_vt.data());original_resource_ctor=native_ctor;
    if(!a_marker_pool_free)a_marker_pool_free=uintptr_t(free_arena);
    CHECK(marker_capacity_preflight(sites));CHECK(marker_capacity_pin_installed());CHECK(marker_capacity_code_current());
}
void setup(){
    CHECK(arenas.empty());reset();
    a_alloc=uintptr_t(allocate_arena);a_marker_pool_free=uintptr_t(free_arena);
    prepare_profile();
    constructors=enhancements=arena_frees=0;freed.clear();
    fail_allocation=throw_allocation=false;allocation_callback=ctor_callback={};
}
struct Resource {
    std::array<uint8_t,kResourceSize> bytes{};
    explicit Resource(bool enhanced=true){CHECK((enhanced?resource_ctor_capacity(bytes.data()):native_ctor(bytes.data()))==bytes.data());}
    Resource(const Resource&)=delete;
    ~Resource(){auto* p=skg::field<void*>(bytes.data(),0x2C10);if(p&&arenas.contains(p))free_arena(p);}
    uintptr_t begin()const{return skg::field<uintptr_t>(const_cast<uint8_t*>(bytes.data()),0x2C10);}
    size_t count()const{return (skg::field<uintptr_t>(const_cast<uint8_t*>(bytes.data()),0x2C14)-begin())/kMarkerStride;}
    size_t capacity()const{return (skg::field<uintptr_t>(const_cast<uint8_t*>(bytes.data()),0x2C18)-begin())/kMarkerStride;}
    // Faithful allocation/capacity effects of F2E0D0. These test rows own no
    // heap companions; production Read/Copy opaque closure has separate tests.
    void reserve(size_t count){
        if(count<=capacity())return;
        auto* replacement=raw_arena(count*kMarkerStride);const auto size=this->count();
        std::memcpy(replacement,reinterpret_cast<void*>(begin()),size*kMarkerStride);free_arena(reinterpret_cast<void*>(begin()));
        skg::field<void*>(bytes.data(),0x2C10)=replacement;
        skg::field<uintptr_t>(bytes.data(),0x2C14)=uintptr_t(replacement)+size*kMarkerStride;
        skg::field<uintptr_t>(bytes.data(),0x2C18)=uintptr_t(replacement)+count*kMarkerStride;
    }
    void read(size_t count){
        reserve(count);std::memset(reinterpret_cast<void*>(begin()),0,count*kMarkerStride);
        for(size_t i=0;i<count;++i){auto* row=reinterpret_cast<void*>(begin()+i*kMarkerStride);
            skg::field<uint32_t>(row,0)=0x40000000;skg::field<uint32_t>(row,4)=1306;skg::field<uint32_t>(row,0x22C)=uint32_t(i);}
        skg::field<uintptr_t>(bytes.data(),0x2C14)=begin()+count*kMarkerStride;
    }
    void copy(const Resource& source){
        reserve(source.count());std::memcpy(reinterpret_cast<void*>(begin()),reinterpret_cast<void*>(source.begin()),source.count()*kMarkerStride);
        skg::field<uintptr_t>(bytes.data(),0x2C14)=begin()+source.count()*kMarkerStride;
    }
};
struct Scene {
    Resource resource;
    std::vector<uintptr_t> actors;
    // Scenario maps own stable slots and actors; actor+18 alone caches the
    // physical marker address that must remain within the current arena.
    std::map<size_t,size_t> slots_to_actors,actors_to_slots;
    explicit Scene(bool enhanced=true):resource(enhanced){}
    void bind(){
        actors.clear();slots_to_actors.clear();actors_to_slots.clear();
        for(size_t i=0;i<resource.count();++i){actors.push_back(resource.begin()+i*kMarkerStride+4);slots_to_actors[i]=i;actors_to_slots[i]=i;}
    }
    bool broadcast()const{
        for(size_t i=0;i<actors.size();++i){
            if(actors[i]!=resource.begin()+i*kMarkerStride+4)return false;
            if(skg::field<uint32_t>(reinterpret_cast<void*>(actors[i]),0)!=1306)return false;
            if(slots_to_actors.at(i)!=i||actors_to_slots.at(i)!=i)return false;
        }
        return true;
    }
    bool append(bool extended){
        const auto count=resource.count();if(count>=size_t(extended?4096:1024))return false;
        if(count==resource.capacity())resource.reserve(std::max(size_t(1),count*2));
        auto* row=reinterpret_cast<void*>(resource.begin()+count*kMarkerStride);
        std::memset(row,0,kMarkerStride);skg::field<uint32_t>(row,0)=0x40000000;skg::field<uint32_t>(row,4)=1306;
        skg::field<uintptr_t>(resource.bytes.data(),0x2C14)=resource.begin()+(count+1)*kMarkerStride;
        actors.push_back(uintptr_t(row)+4);slots_to_actors[count]=count;actors_to_slots[count]=count;
        return true;
    }
};
struct WorkerResult {size_t capacity=0;unsigned depth=99;};
DWORD WINAPI worker_constructor(void* value){
    auto& result=*static_cast<WorkerResult*>(value);
    {Resource resource;result.capacity=resource.capacity();result.depth=marker_capacity_depth;}
    return 0;
}
void test(){
    // The unchanged Native capacity model reproduces the actual C58 lifetime
    // failure without dereferencing freed memory in this controlled process.
    setup();{Scene old(false);old.resource.read(1025);old.bind();CHECK(old.resource.capacity()==1025&&old.broadcast());
        const auto before=old.resource.begin();CHECK(old.append(true));CHECK(old.resource.capacity()==2050&&old.resource.begin()!=before&&!old.broadcast());}
    CHECK(arenas.empty());
    for(const size_t initial:{size_t(1024),size_t(1025),size_t(4095)}){
        setup();{Scene scene;CHECK(marker_capacity_ready(scene.resource.bytes.data()));
            CHECK(scene.resource.capacity()==4096&&skg::field<uint32_t>(scene.resource.bytes.data(),0x88)==0x1234ABCD);
            scene.resource.read(initial);scene.bind();const auto begin=scene.resource.begin();CHECK(scene.broadcast());
            CHECK(scene.append(true)&&scene.resource.count()==initial+1&&scene.resource.begin()==begin&&scene.broadcast());
            if(initial==4095){const auto count=scene.resource.count();CHECK(!scene.append(true)&&scene.resource.begin()==begin&&scene.resource.count()==count&&scene.broadcast());}}
        CHECK(arenas.empty());
    }
    setup();{Scene scene;scene.resource.read(1024);scene.bind();const auto begin=scene.resource.begin();
        CHECK(!scene.append(false)&&scene.resource.count()==1024&&scene.resource.begin()==begin&&scene.broadcast());}
    // Copy, canonical recovery and late-join reconstruction preserve the
    // destination's reserve, including when the source has a smaller arena.
    setup();{Resource source(false);source.read(1025);Scene restored;restored.resource.read(419);restored.bind();
        const auto begin=restored.resource.begin();restored.actors.clear();restored.slots_to_actors.clear();restored.actors_to_slots.clear();
        restored.resource.copy(source);CHECK(restored.resource.capacity()==4096&&restored.resource.begin()==begin);
        restored.bind();CHECK(restored.broadcast()&&restored.append(true)&&restored.broadcast());
        Resource late_join;const auto join_begin=late_join.begin();late_join.copy(restored.resource);
        CHECK(late_join.capacity()==4096&&late_join.begin()==join_begin&&late_join.count()==1026);
        source.read(3);late_join.copy(source);CHECK(late_join.capacity()==4096&&late_join.begin()==join_begin&&late_join.count()==3);}
    for(unsigned failure=0;failure<2;++failure){
        setup();fail_allocation=!failure;throw_allocation=bool(failure);
        {Resource resource;CHECK(resource.capacity()==1024&&!marker_capacity_ready(resource.bytes.data())&&enhancements==1&&arena_frees==0);
            std::string error;CHECK(!marker_capacity_require(resource.bytes.data(),error)&&!error.empty());}
        CHECK(arenas.empty());
    }
    // Constructor callback retirement happens before any enhancement alloc.
    setup();ctor_callback=[]{prepare_profile();};{Resource resource;CHECK(resource.capacity()==1024&&enhancements==0);}
    CHECK(arenas.empty());
    for(unsigned mutation=0;mutation<8;++mutation){
        setup();const auto old_host=H;auto* old_module=M;const auto old_generation=generation;
        std::array<uint8_t,kResourceSize> bytes{};
        allocation_callback=[&,mutation]{
            if(mutation==0)H=nullptr;
            if(mutation==1)M=reinterpret_cast<SkMod*>(uintptr_t(17));
            if(mutation==2)++generation;
            if(mutation==3)prepare_profile(); // Same addresses, new lifecycle.
            if(mutation==4)skg::field<uintptr_t>(bytes.data(),0x2C14)+=kMarkerStride;
            if(mutation==5)skg::field<uintptr_t>(bytes.data(),0x2C18)+=kMarkerStride;
            if(mutation==6)++skg::field<uint32_t>(bytes.data(),0x2C24);
            if(mutation==7)marker_capacity_retire();
        };
        CHECK(resource_ctor_capacity(bytes.data())==bytes.data());
        CHECK(!marker_capacity_ready(bytes.data())&&enhancements==1&&arena_frees==1&&arenas.size()==1);
        H=old_host;M=old_module;generation=old_generation;
        free_arena(skg::field<void*>(bytes.data(),0x2C10));CHECK(arenas.empty());
    }
    // Nested constructors have independent empty-pool ownership and unwind
    // their depth/service scopes before the outer constructor publishes.
    setup();allocation_callback=[]{Resource nested;CHECK(nested.capacity()==4096&&marker_capacity_depth==1);};
    {Resource outer;CHECK(outer.capacity()==4096&&constructors==2&&enhancements==2&&marker_capacity_depth==0);}
    CHECK(arenas.empty());
    // A foreign patch cannot become a new capacity profile. Every byte of all
    // five bodies, including relocated words, is checked by production code.
    for(size_t index=0;index<marker_capacity_sites.size();++index){
        setup();auto* byte=reinterpret_cast<uint8_t*>(marker_capacity_sites[index]+marker_capacity_profile::kCode[index].bytes.size()-1);
        *byte^=1;CHECK(!marker_capacity_code_current());{Resource resource;CHECK(resource.capacity()==1024&&enhancements==0);}
        CHECK(arenas.empty());
    }
    setup();{Resource existing(false);existing.read(1025);const auto begin=existing.begin();
        std::string error;CHECK(!marker_capacity_require(existing.bytes.data(),error)&&existing.begin()==begin&&existing.capacity()==1025);}
    // Native's resource factory may construct off the policy-owner thread.
    // It gets immutable allocator/code values, with no Runtime policy/getter
    // calls. Retirement while its allocator waits discards only the new arena.
    for(unsigned retire=0;retire<2;++retire){
        setup();WorkerResult result;const auto prior_service=service_depth;
        HANDLE entered=nullptr,resume=nullptr;
        if(retire){
            entered=CreateEventW(nullptr,TRUE,FALSE,nullptr);resume=CreateEventW(nullptr,TRUE,FALSE,nullptr);CHECK(entered&&resume);
            allocation_callback=[=]{CHECK(SetEvent(entered));CHECK(WaitForSingleObject(resume,10000)==WAIT_OBJECT_0);};
        }
        DWORD id=0;HANDLE thread=CreateThread(nullptr,0,worker_constructor,&result,0,&id);CHECK(thread&&id!=GetCurrentThreadId());
        if(retire){CHECK(WaitForSingleObject(entered,10000)==WAIT_OBJECT_0);marker_capacity_retire();CHECK(SetEvent(resume));}
        CHECK(WaitForSingleObject(thread,10000)==WAIT_OBJECT_0);CHECK(CloseHandle(thread));
        if(retire){CHECK(CloseHandle(entered));CHECK(CloseHandle(resume));}
        CHECK(result.capacity==size_t(retire?1024:4096)&&result.depth==0&&service_depth==prior_service&&arenas.empty());
    }
    CHECK(arenas.empty());prepare_profile();reset();
}
} // namespace marker_capacity_fixture
