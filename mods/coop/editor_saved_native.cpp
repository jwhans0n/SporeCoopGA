#include "editor_saved_native.h"
#include "editor_saved_dependencies_store.h"
#include "editor_native_save.h"
#include "adventure_collect.h"
#include "adventure_assets.h"
#include "sporekit_game.hpp"
#include "../../updater/sha256.h"
#include <algorithm>
#include <cstdio>
#include <filesystem>
#include <map>
#include <memory>
#include <optional>
#include <stdexcept>
#include <tuple>
#include <windows.h>

namespace editor_saved_native {
namespace {
namespace saved=editor_saved_dependencies;
const SkHost* H=nullptr;SkMod* M=nullptr;
uintptr_t parent_address=0;
uint64_t module_generation=0,next_lifetime=0;
bool available=false,stopping=false,retiring=false;
unsigned unretained_callbacks=0;
uint64_t deferred=0,resumed=0,restored=0,cancelled=0,failed=0;
using Ctor=void*(SK_THISCALL*)(void*);
using Dtor=void(SK_THISCALL*)(void*);
using Start=bool(SK_THISCALL*)(void*,void*,bool,bool);
using Advance=bool(SK_THISCALL*)(void*,bool);
using Reset=void(SK_THISCALL*)(void*,bool,uint32_t);
using Idle=void(SK_THISCALL*)(void*);
Ctor original_ctor=nullptr;Dtor original_dtor=nullptr;
Start original_start=nullptr;Advance original_advance=nullptr;
Reset original_reset=nullptr;Idle original_idle=nullptr;
struct Ticket {
    uint64_t module=0,lifetime=0,serial=0;uintptr_t controller=0,root=0;
    dbpf::Key key;double began=0;
    bool operator==(const Ticket&)const=default;
};
struct Life {uint64_t lifetime=0,serial=0;std::optional<Ticket> pending;};
std::map<void*,Life> loaders;
struct Entry {void* self=nullptr;void* root=nullptr;bool check_only=false;};
std::optional<Entry> entering;
constexpr size_t kMaxLoaders=32;

void release_callback_module(void* value){FreeLibrary(static_cast<HMODULE>(value));}
struct CallbackLease {
    const SkHost* host=H;HMODULE module=nullptr;
    CallbackLease(){
        if(!host || !host->run_outside_frame || !GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS,
            reinterpret_cast<LPCWSTR>(release_callback_module),&module))++unretained_callbacks;
    }
    ~CallbackLease(){
        if(!module){--unretained_callbacks;return;}
        // The SDK task takes a SECOND DLL ref before this hook returns. Its
        // Kit-owned deleter releases that ref after our release helper returns.
        // Never FreeLibrary the last ref from a destructor inside this DLL.
        try{host->run_outside_frame(release_callback_module,module);}
        catch(...){/* Retain the OS ref until process exit if enqueue fails. */}
    }
    CallbackLease(const CallbackLease&)=delete;CallbackLease& operator=(const CallbackLease&)=delete;
};

bool kit_capable(){
    unsigned major=0,minor=0,patch=0;char tail=0;
    const char* version=H&&H->kit_version?H->kit_version():nullptr;
    return version && std::sscanf(version,"%u.%u.%u%c",&major,&minor,&patch,&tail)>=3 &&
        (major>0 || minor>3 || (minor==3 && patch>=5));
}
bool domain(saved::Domain& value,std::filesystem::path& directory,std::string& error){
    const auto address=H&&H->sym?H->sym("Resource::GetDatabaseByID"):0;
    void* database=address?skg::cdecl_call<void*>(address,0x11AC199u):nullptr;
    const wchar_t* location=database?skg::vcall<const wchar_t*>(database,0x28/4):nullptr;
    std::wstring text;bool complete=false;
    for(size_t n=0;location && n<1000 && skg::readable(location+n,sizeof(wchar_t));++n){
        if(!location[n]){complete=true;break;}text+=location[n];
    }
    if(!complete || text.empty()){error="The Native Saved dependency profile is unavailable";return false;}
    auto profile=std::filesystem::absolute(std::filesystem::path(text).parent_path()).lexically_normal();
    auto normalized=profile.generic_wstring();
    for(auto& c:normalized)if(c>=L'A'&&c<=L'Z')c+=L'a'-L'A';
    // Windows wchar_t is UTF16 on every supported target. Hash explicitly
    // ordered words so a host codec never depends on its own wchar_t width.
    std::vector<uint8_t> identity;identity.reserve(normalized.size()*2);
    for(auto c:normalized){identity.push_back(uint8_t(c));identity.push_back(uint8_t(uint32_t(c)>>8));}
    value={saved::kPolicy,H->exe_sha256(),upd::sha256_hex(identity.data(),identity.size())};
    directory=profile/L"SporeCoop"/L"saved-dependencies";return true;
}
bool slots_match(const saved::Inventory& a,const saved::Inventory& b){
    if(a.saved!=b.saved || a.crew_count!=b.crew_count || a.classes!=b.classes || a.references.size()!=b.references.size())return false;
    for(size_t n=0;n<a.references.size();++n){const auto& x=a.references[n];const auto& y=b.references[n];
        if(std::tie(x.kind,x.index,x.slot,x.raw,x.server,x.machine,x.model)!=
           std::tie(y.kind,y.index,y.slot,y.raw,y.server,y.machine,y.model))return false;
    }
    return true;
}
bool raw_matches(const saved::Inventory& a,const saved::Inventory& b){
    return a.raw_size==b.raw_size && a.raw_hash==b.raw_hash && slots_match(a,b);
}
bool active_loader(void* self){
    if(!parent_address || !skg::readable(reinterpret_cast<void*>(parent_address),sizeof(void*)))return false;
    auto* parent=*reinterpret_cast<void**>(parent_address);
    return skg::readable(parent,0x20) && skg::field<void*>(parent,0x1C)==self;
}
bool native_pending(const Ticket& value){
    if(!available || stopping || value.module!=module_generation)return false;
    auto* self=reinterpret_cast<void*>(value.controller);const auto found=loaders.find(self);
    if(found==loaders.end() || found->second.lifetime!=value.lifetime || found->second.serial!=value.serial ||
       !found->second.pending || *found->second.pending!=value || !skg::readable(self,0xA8))return false;
    auto* root=skg::field<void*>(self,0x18);
    return uintptr_t(root)==value.root && skg::field<uint32_t>(self,0x1C)==1 &&
        skg::field<uint32_t>(self,0x2C)==0 && skg::field<uint32_t>(self,0x90)==0 &&
        skg::readable(root,0x14);
}
bool current(const Ticket& value){
    return native_pending(value) && active_loader(reinterpret_cast<void*>(value.controller)) &&
        skg::field<dbpf::Key>(reinterpret_cast<void*>(value.root),8)==value.key;
}
void retire(const Ticket& value){
    auto found=loaders.find(reinterpret_cast<void*>(value.controller));
    if(found!=loaders.end() && found->second.lifetime==value.lifetime && found->second.serial==value.serial)
        found->second.pending.reset();
}
struct Held {
    void* value=nullptr;size_t release_slot=0;
    Held(void* p,size_t add,size_t release):value(p),release_slot(release){skg::vcall<int>(value,add);}
    ~Held(){skg::vcall<int>(value,release_slot);}
    Held(const Held&)=delete;Held& operator=(const Held&)=delete;
};
void cancel(const Ticket& value){
    if(!native_pending(value))return;
    auto* self=reinterpret_cast<void*>(value.controller);
    // Cleanup can outlive the active parent. The observed constructor/destructor
    // lifetime and exact request still prove this controller is alive and owns
    // this root. Only forward admission/resume requires the active parent too.
    Held controller(self,1,2);
    if(!native_pending(value))return;
    // Native's normal cancellation, also used by EC56DE and parent teardown
    // EC5192. No root reference is taken through a retired parent.
    retire(value);++cancelled;original_reset(self,true,0);
}
bool restore_dependencies(const Ticket& ticket,std::string& error){
    const auto fence=[&]{return current(ticket);};
    saved::Domain identity;std::filesystem::path directory;
    if(!domain(identity,directory,error) || !fence())return false;
    std::vector<uint8_t> raw;
    if(!editor_native_save::read_saved({ticket.key.instance,ticket.key.type,ticket.key.group},raw,error) || !fence())return false;
    saved::Receipt receipt;
    if(!saved::load(directory,identity,ticket.key,raw,receipt,error) || !fence())return false;
    saved::Inventory inventory;
    if(!adventure_collect::saved_references(ticket.key,raw,inventory,error) || !fence())return false;
    if(!raw_matches(receipt.inventory,inventory)){error="The dependency receipt does not match Native's complete Saved roster";return false;}
    saved::Inventory selected_root;
    if(!adventure_collect::selected_saved_references(reinterpret_cast<void*>(ticket.root),selected_root,error) || !fence() ||
       !slots_match(inventory,selected_root)){
        error="The actual selected Native root has a different dependency roster";return false;
    }
    if(!adventure_collect::validate_uploaded_records(ticket.key,receipt.records,error) || !fence())return false;
    if(!adventure_assets::register_records(receipt.records,error) || !fence())return false;
    saved::Inventory observed;std::vector<dbpf::Record> selected;saved::Receipt verified;
    if(!adventure_collect::saved_dependencies(ticket.key,raw,observed,selected,error) || !fence() ||
       !saved::make(identity,ticket.key,raw,observed,selected,receipt.records,verified,error))return false;
    std::vector<uint8_t> expected,actual;
    if(!saved::encode(receipt,expected,error) || !saved::encode(verified,actual,error) || actual!=expected){
        error="Installed Saved dependencies differ from their verified receipt";return false;
    }
    std::vector<uint8_t> settled;
    if(!editor_native_save::read_saved({ticket.key.instance,ticket.key.type,ticket.key.group},settled,error) ||
       settled!=raw || !fence()){error="The selected Saved image changed during dependency restoration";return false;}
    if(!adventure_collect::selected_saved_references(reinterpret_cast<void*>(ticket.root),selected_root,error) || !fence() ||
       !slots_match(inventory,selected_root)){
        error="The selected Native dependency roster changed during admission";return false;
    }
    return true;
}
void dispatch(void* payload){
    std::unique_ptr<Ticket> owned(static_cast<Ticket*>(payload));const auto ticket=*owned;
    if(!current(ticket)){cancel(ticket);return;}
    // Native references are acquired and destroyed only outside Present.
    Held controller(reinterpret_cast<void*>(ticket.controller),1,2);
    if(!current(ticket)){cancel(ticket);return;}
    Held root(reinterpret_cast<void*>(ticket.root),0,1);
    if(!current(ticket)){cancel(ticket);return;}
    if(H->time_seconds()-ticket.began>5){cancel(ticket);return;}
    std::string error;bool okay=false;
    try{okay=restore_dependencies(ticket,error);}
    catch(const std::exception& e){error=e.what();}
    if(!current(ticket)){cancel(ticket);return;}
    if(okay){++restored;H->log(M,SK_INFO,"Restored exact Saved dependencies for %08X",ticket.key.instance);}
    else{++failed;H->log(M,SK_DEBUG,"Saved dependency restoration skipped: %s",error.c_str());}
    retire(ticket);++resumed;
    // Missing, invalid or conflicting receipts all retain the unmodified Native
    // path, including its original availability/model/server warnings.
    original_advance(reinterpret_cast<void*>(ticket.controller),false);
}
void* SK_THISCALL construct(void* self){
    CallbackLease callback;
    void* result=original_ctor(self);
    if(available && !stopping && !retiring && next_lifetime!=UINT64_MAX && loaders.size()<kMaxLoaders)
        try{loaders[self]={++next_lifetime,0,{}};}catch(const std::bad_alloc&){}
    return result;
}
void SK_THISCALL destroy(void* self){CallbackLease callback;loaders.erase(self);original_dtor(self);}
void SK_THISCALL reset(void* self,bool notify,uint32_t reason){
    CallbackLease callback;
    auto found=loaders.find(self);
    if(found!=loaders.end()){
        found->second.pending.reset();
        if(found->second.serial!=UINT64_MAX)++found->second.serial;else loaders.erase(found);
    }
    original_reset(self,notify,reason);
}
bool SK_THISCALL start(void* self,void* root,bool ui,bool check_only){
    CallbackLease callback;
    const auto previous=entering;entering=Entry{self,root,check_only};
    struct Restore {std::optional<Entry> old;~Restore(){entering=old;}} restore{previous};
    return original_start(self,root,ui,check_only);
}
bool SK_THISCALL advance(void* self,bool check_only){
    CallbackLease callback;
    auto found=loaders.find(self);
    if(!callback.module || !available || stopping || retiring || check_only || found==loaders.end())return original_advance(self,check_only);
    if(found->second.pending && current(*found->second.pending))return true;
    if(!entering || entering->self!=self || entering->check_only || !active_loader(self) ||
       !skg::readable(self,0xA8) || skg::field<uint32_t>(self,0x1C)!=1 ||
       skg::field<uint32_t>(self,0x2C)!=0 || skg::field<uint32_t>(self,0x90)!=0 ||
       skg::field<void*>(self,0x18)!=entering->root || !skg::readable(entering->root,0x14))return original_advance(self,check_only);
    const auto key=skg::field<dbpf::Key>(entering->root,8);
    if(!key.instance || key.type!=saved::kScenario)return original_advance(self,check_only);
    Ticket value{module_generation,found->second.lifetime,found->second.serial,uintptr_t(self),uintptr_t(entering->root),key,H->time_seconds()};
    auto task=std::unique_ptr<Ticket>(new(std::nothrow) Ticket(value));
    if(!task)return original_advance(self,check_only);
    found->second.pending=value;
    try{H->run_outside_frame(dispatch,task.get());task.release();++deferred;return true;}
    catch(...){retire(value);return original_advance(self,check_only);}
}
void SK_THISCALL idle(void* self){
    CallbackLease callback;
    const auto found=loaders.find(self);
    if(found!=loaders.end() && found->second.pending && native_pending(*found->second.pending))return;
    original_idle(self);
}
}
bool init(const SkHost* host,SkMod* mod){
    shutdown();H=host;M=mod;stopping=retiring=false;
    if(module_generation==UINT64_MAX || !H || !H->exe_sha256 || !skg::supported_gog2024(H->exe_sha256()) ||
       !H->sym || !H->run_outside_frame || !H->time_seconds || !H->hook_create || !kit_capable())return false;
    ++module_generation;
    parent_address=H->sym("App::cScenarioMode::ptr");
    if(!parent_address)return false;
    struct Hook{const char* name;void* detour;void** original;};
    const Hook hooks[]{
        {"SporeCoop::Editor::LoadControllerCtor",reinterpret_cast<void*>(construct),reinterpret_cast<void**>(&original_ctor)},
        {"SporeCoop::Editor::LoadControllerDtor",reinterpret_cast<void*>(destroy),reinterpret_cast<void**>(&original_dtor)},
        {"SporeCoop::Editor::LoadControllerStart",reinterpret_cast<void*>(start),reinterpret_cast<void**>(&original_start)},
        {"SporeCoop::Editor::LoadControllerAdvance",reinterpret_cast<void*>(advance),reinterpret_cast<void**>(&original_advance)},
        {"SporeCoop::Editor::LoadControllerReset",reinterpret_cast<void*>(reset),reinterpret_cast<void**>(&original_reset)},
        {"SporeCoop::Editor::LoadControllerIdle",reinterpret_cast<void*>(idle),reinterpret_cast<void**>(&original_idle)}};
    std::array<void*,6> installed{};size_t count=0;
    for(const auto& hook:hooks){auto* address=reinterpret_cast<void*>(H->sym(hook.name));
        if(!address || H->hook_create(M,address,hook.detour,hook.original)!=0){
            if(H->hook_remove)while(count)H->hook_remove(M,installed[--count]);
            return false;
        }
        installed[count++]=address;
    }
    available=true;return true;
}
void shutdown(){
    if(unretained_callbacks)throw std::runtime_error("Native loader callbacks could not retain their DLL; retry mod unload after they return");
    // Kit0.3.5 fences on_unload to main/outsidePresent before removing hooks.
    // Retire each actual pending Native state, not merely its queued ticket.
    retiring=true;
    if(available){
        for(;;){
            const auto found=std::find_if(loaders.begin(),loaders.end(),[](const auto& row){return bool(row.second.pending);});
            if(found==loaders.end())break;
            const auto value=*found->second.pending;
            if(native_pending(value))cancel(value);else retire(value);
        }
    }
    stopping=true;available=false;loaders.clear();entering.reset();
    if(module_generation!=UINT64_MAX)++module_generation;
}
bool persist(dbpf::Key key,const std::vector<uint8_t>& raw,const std::vector<dbpf::Record>& admitted,
             const std::function<bool()>& fence,std::string& error){
    try{
        if(raw.empty() || raw.size()>saved::kMaxRecordBytes || admitted.size()>saved::kMaxRecords){
            error="The Saved dependency inputs exceed their bounds";return false;
        }
        uint64_t bytes=0;
        for(const auto& record:admitted){
            if(record.data.size()>saved::kMaxRecordBytes || record.data.size()>saved::kMaxPayloadBytes-bytes){
                error="The canonical Saved dependency closure exceeds its byte bounds";return false;
            }
            bytes+=record.data.size();
        }
        const auto owned_raw=raw;const auto owned_admitted=admitted;const auto owned_fence=fence;
        const auto module=module_generation;
        const auto current=[&]{
            const auto local=[&]{return available && !stopping && module_generation==module;};
            return local() && owned_fence && owned_fence() && local();
        };
        if(!current()){error="Saved dependency persistence has no current Native operation";return false;}
        saved::Domain identity;std::filesystem::path directory;saved::Inventory inventory;
        std::vector<dbpf::Record> selected;saved::Receipt receipt;
        if(!domain(identity,directory,error) || !current() ||
           !adventure_collect::saved_dependencies(key,owned_raw,inventory,selected,error) || !current() ||
           !saved::make(identity,key,owned_raw,inventory,selected,owned_admitted,receipt,error))return false;
        const auto published=saved::publish(directory,receipt,error);
        if(published!=saved::Publication::Published && published!=saved::Publication::Unchanged)return false;
        if(!current()){error="The Saved operation changed before dependency confirmation";return false;}
        return true;
    }catch(const std::exception& e){error=e.what();return false;}
}
std::string status(){
    return "saved_dependencies available="+std::to_string(available)+" deferred="+std::to_string(deferred)+
        " resumed="+std::to_string(resumed)+" restored="+std::to_string(restored)+" cancelled="+std::to_string(cancelled)+
        " failed="+std::to_string(failed)+" loaders="+std::to_string(loaders.size());
}
}
