// Included after the stream ABI. Capacity is acquired only while a native
// ScenarioResource constructor still owns an empty, unpublished marker pool.
#include "editor_native_marker_capacity_profile.h"
using ResourceCtorFn=void*(SK_THISCALL*)(void*);
ResourceCtorFn original_resource_ctor;
std::atomic<ResourceCtorFn> marker_capacity_fallback_ctor{nullptr};
std::atomic<bool> marker_capacity_installing{false};
uintptr_t a_marker_pool_free,a_marker_pool_reserve,a_read_marker_rows,a_resource_vtable;
std::array<uintptr_t,5> marker_capacity_sites{};
std::array<uint8_t,marker_capacity_profile::kCtor.size()> marker_capacity_installed_ctor{};
bool marker_capacity_hook_installed=false,marker_capacity_profile_pinned=false;
uint64_t marker_capacity_lifecycle=0;
thread_local unsigned marker_capacity_depth=0;
constexpr size_t kMarkerStride=0x238,kMarkerCapacityBytes=size_t(kPlacementLimit)*kMarkerStride;
struct MarkerCapacityPolicy {
    const SkHost* host;SkMod* module;DWORD owner_thread;
    ResourceCtorFn ctor;uintptr_t allocate,free,vtable;
    std::array<uintptr_t,5> sites;
    std::array<uint8_t,marker_capacity_profile::kCtor.size()> installed_ctor;
};
std::shared_ptr<const MarkerCapacityPolicy> marker_capacity_policy;
void marker_capacity_retire(){std::atomic_store_explicit(&marker_capacity_policy,std::shared_ptr<const MarkerCapacityPolicy>{},std::memory_order_release);}

bool marker_capacity_expected_code(uintptr_t site,size_t index,uint32_t bias){
    const auto& code=marker_capacity_profile::kCode[index];
    if(site!=uint32_t(code.preferred+bias) || !skg::readable(reinterpret_cast<void*>(site),code.bytes.size()))return false;
    size_t relocation=0;
    for(size_t byte=0;byte<code.bytes.size();){
        if(relocation<code.relocations.size() && byte==code.relocations[relocation]){
            uint32_t wanted=0,actual=0;std::memcpy(&wanted,code.bytes.data()+byte,4);
            std::memcpy(&actual,reinterpret_cast<void*>(site+byte),4);
            if(actual!=wanted+bias)return false;byte+=4;++relocation;
        }else{if(*reinterpret_cast<const uint8_t*>(site+byte)!=code.bytes[byte])return false;++byte;}
    }
    return true;
}
bool marker_capacity_preflight(std::array<uintptr_t,5> sites){
    marker_capacity_retire();
    marker_capacity_hook_installed=marker_capacity_profile_pinned=false;
    if(marker_capacity_lifecycle==UINT64_MAX)return false;++marker_capacity_lifecycle;
    const auto bias=uint32_t(sites[0]-marker_capacity_profile::kCode[0].preferred);
    for(size_t index=0;index<sites.size();++index)if(!marker_capacity_expected_code(sites[index],index,bias))return false;
    marker_capacity_sites=sites;return true;
}
bool marker_capacity_pin_installed(){
    if(!original_resource_ctor || !skg::readable(reinterpret_cast<void*>(marker_capacity_sites[0]),marker_capacity_installed_ctor.size()))return false;
    marker_capacity_fallback_ctor.store(original_resource_ctor,std::memory_order_release);
    std::memcpy(marker_capacity_installed_ctor.data(),reinterpret_cast<void*>(marker_capacity_sites[0]),marker_capacity_installed_ctor.size());
    try{
        auto value=std::make_shared<MarkerCapacityPolicy>(MarkerCapacityPolicy{H,M,native_owner_thread?native_owner_thread:GetCurrentThreadId(),
            original_resource_ctor,a_alloc,a_marker_pool_free,a_resource_vtable,marker_capacity_sites,marker_capacity_installed_ctor});
        std::atomic_store_explicit(&marker_capacity_policy,std::shared_ptr<const MarkerCapacityPolicy>(std::move(value)),std::memory_order_release);
    }catch(const std::bad_alloc&){return false;}
    marker_capacity_profile_pinned=marker_capacity_hook_installed=true;return true;
}
bool marker_capacity_policy_code(const MarkerCapacityPolicy& policy){
    if(!policy.vtable || !skg::readable(reinterpret_cast<void*>(policy.sites[0]),policy.installed_ctor.size()) ||
       std::memcmp(reinterpret_cast<void*>(policy.sites[0]),policy.installed_ctor.data(),policy.installed_ctor.size()))return false;
    const auto bias=uint32_t(policy.sites[0]-marker_capacity_profile::kCode[0].preferred);
    for(size_t index=1;index<policy.sites.size();++index)
        if(!marker_capacity_expected_code(policy.sites[index],index,bias))return false;
    return true;
}
bool marker_capacity_code_current(){
    const auto policy=std::atomic_load_explicit(&marker_capacity_policy,std::memory_order_acquire);
    return marker_capacity_hook_installed && marker_capacity_profile_pinned && policy && marker_capacity_policy_code(*policy);
}
bool marker_capacity_ready(void* resource){
    if(!skg::readable(resource,kResourceSize))return false;
    const auto begin=skg::field<uintptr_t>(resource,0x2C10),end=skg::field<uintptr_t>(resource,0x2C14),cap=skg::field<uintptr_t>(resource,0x2C18);
    return begin && end>=begin && cap>=end && (end-begin)%kMarkerStride==0 && (cap-begin)%kMarkerStride==0 &&
        (end-begin)/kMarkerStride<=kPlacementLimit && cap-begin>=kMarkerCapacityBytes &&
        skg::readable(reinterpret_cast<void*>(begin),kMarkerCapacityBytes);
}
bool marker_capacity_scene_current(){
    // Optional existing-scene observation. No scene, Native getter or policy
    // callback is required for the global code/tuning capability.
    auto* m=mode();auto* d=data(m);auto* r=resource(d);
    return !skg::readable(m,0xDC) || skg::field<int>(m,0xCC)!=1 || !r || marker_capacity_ready(r);
}
bool marker_capacity_require(void* resource,std::string& error){
    if(marker_capacity_code_current() && marker_capacity_ready(resource))return true;
    error="Extended Complexity needs a fresh native scene with its complete 4096-marker reserve. The current draft is preserved.";
    return false;
}
void* SK_THISCALL resource_ctor_capacity(void* self){
    const auto policy=std::atomic_load_explicit(&marker_capacity_policy,std::memory_order_acquire);
    auto ctor=policy?policy->ctor:marker_capacity_fallback_ctor.load(std::memory_order_acquire);
    // The SDK publishes its trampoline before hook activation returns. A
    // worker entering during that short handoff waits for the atomic value;
    // it never reads main-thread installation storage or any Scene globals.
    while(!ctor && marker_capacity_installing.load(std::memory_order_acquire)){
        SwitchToThread();ctor=marker_capacity_fallback_ctor.load(std::memory_order_acquire);
    }
    const bool main=policy && GetCurrentThreadId()==policy->owner_thread;
    const auto generation_pin=main?generation:0;
    const auto lifecycle=main?marker_capacity_lifecycle:0;
    const auto allocate=policy?policy->allocate:0,free=policy?policy->free:0,vtable=policy?policy->vtable:0;
    const bool enabled=policy && marker_capacity_policy_code(*policy);
    const auto context=[&]{return enabled && std::atomic_load_explicit(&marker_capacity_policy,std::memory_order_acquire)==policy &&
        marker_capacity_policy_code(*policy) && (!main || (marker_capacity_hook_installed && marker_capacity_profile_pinned && marker_capacity_lifecycle==lifecycle &&
        H==policy->host && M==policy->module && generation==generation_pin &&
        original_resource_ctor==ctor && a_alloc==allocate && a_marker_pool_free==free && a_resource_vtable==vtable && marker_capacity_sites==policy->sites));};
    struct Scope {bool main;explicit Scope(bool value):main(value){if(main)++service_depth;++marker_capacity_depth;}
        ~Scope(){--marker_capacity_depth;if(main)--service_depth;}} scope(main);
    auto* result=ctor(self);
    if(result!=self || marker_capacity_depth>8 || !context() || !allocate || !free ||
       !skg::readable(self,kResourceSize) || skg::field<uintptr_t>(self,0)!=vtable)return result;
    const std::array<uintptr_t,3> before{skg::field<uintptr_t>(self,0x2C10),skg::field<uintptr_t>(self,0x2C14),skg::field<uintptr_t>(self,0x2C18)};
    const std::array<uint32_t,2> metadata{skg::field<uint32_t>(self,0x2C24),skg::field<uint32_t>(self,0x2C28)};
    if(!before[0] || before[1]!=before[0] || before[2]<before[0] ||
       before[2]-before[0]!=size_t(1024)*kMarkerStride || metadata!=std::array<uint32_t,2>{0x3FFFFFFF,0x3FFFFFFF})return result;
    void* owned=nullptr;
    try{owned=skg::cdecl_call<void*>(allocate,kMarkerCapacityBytes,"CoopScenarioMarkers",0,0,0,0);}catch(...){return result;}
    if(!owned)return result; // Native callers require the valid original self.
    const auto unchanged=[&]{return context() &&
        skg::readable(self,kResourceSize) && skg::field<uintptr_t>(self,0)==vtable &&
        skg::field<uintptr_t>(self,0x2C10)==before[0] && skg::field<uintptr_t>(self,0x2C14)==before[1] &&
        skg::field<uintptr_t>(self,0x2C18)==before[2] && skg::field<uint32_t>(self,0x2C24)==metadata[0] &&
        skg::field<uint32_t>(self,0x2C28)==metadata[1];};
    const auto begin=reinterpret_cast<uintptr_t>(owned);
    if(begin>UINTPTR_MAX-kMarkerCapacityBytes || !skg::readable(owned,kMarkerCapacityBytes) || !unchanged()){
        skg::cdecl_call<void>(free,owned);return result;
    }
    // The native empty Reserve path performs no slot initialization and changes
    // only these three pointers. Publish before retiring our old empty arena;
    // there are no actor bindings, row owners or native callbacks to rebind.
    skg::field<uintptr_t>(self,0x2C10)=begin;
    skg::field<uintptr_t>(self,0x2C14)=begin;
    skg::field<uintptr_t>(self,0x2C18)=begin+kMarkerCapacityBytes;
    skg::cdecl_call<void>(free,reinterpret_cast<void*>(before[0]));
    return result;
}
