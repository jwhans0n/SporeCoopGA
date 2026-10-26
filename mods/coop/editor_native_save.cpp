#include "editor_native_save.h"
#include "editor_native.h"
#include "net/adventure_assets.h"
#include "net/dbpf.h"
#include "sporekit_game.hpp"
#include <memory>
#include <optional>
#include <utility>

namespace editor_native_save {
namespace {
const SkHost* H;
SkMod* M;
Ctx C;
uintptr_t a_mode,a_rm,a_vtable,a_can_save,a_begin_save,a_pipeline,a_hold;
uintptr_t a_preview,a_return,a_cleanup,a_return_display,a_display,a_captain,a_validate_placement,a_command;
bool supported=false,queued=false;
bool return_notified=false;
bool owned_native=false;
struct NativeScope{bool before=owned_native;NativeScope(){owned_native=true;}~NativeScope(){owned_native=before;}};
uint64_t generation=1;
constexpr uint32_t kAdventure=0x366A930D,kScenarioClass=0xE742574A;
using Key=editor::ResourceKey;
struct Pending {
    void* mode=nullptr;
    void* data=nullptr;
    void* ui=nullptr;
    void* resource=nullptr;
    Key before,after;
    bool copy=false,executed=false,success=false;
};
std::optional<Pending> pending;
using Pipeline=bool(SK_THISCALL*)(void*);
Pipeline original_pipeline;
using Return=void(SK_THISCALL*)(void*);
Return original_return;
using Command=void(SK_THISCALL*)(void*,int);
Command original_command,original_display;
bool shared(){return C.shared_active && C.shared_active();}
void SK_THISCALL command_native(void* self,int action){
    if(shared() && !owned_native)return;
    original_command(self,action);
}
void SK_THISCALL display_native(void* self,int action){
    // Initial editor entry selects Build after constructing its native shell.
    // Idempotent Build refreshes are also safe; leaving Play/Terraform is not.
    void* m=a_mode?*reinterpret_cast<void**>(a_mode):nullptr;
    const bool initial_build=action==0 && skg::readable(m,0xDC) &&
        skg::field<int>(m,0xCC)==1 && skg::field<int>(m,0xD0)==0 &&
        skg::field<void*>(m,0x14)==self && skg::readable(self,0x98) &&
        skg::field<uintptr_t>(self,0)==a_vtable && (skg::field<int>(self,0x1C)==-1 || skg::field<int>(self,0x1C)==0);
    if(shared() && !owned_native && !initial_build)return;
    original_display(self,action);
}

void* mode(){return a_mode?*reinterpret_cast<void**>(a_mode):nullptr;}
bool current(const Pending& p){
    return editor_native::state().ready && mode()==p.mode && skg::readable(p.mode,0xDC) &&
        skg::field<int>(p.mode,0xCC)==1 && skg::field<int>(p.mode,0xD0)==0 &&
        skg::field<void*>(p.mode,0x74)==p.data && skg::field<void*>(p.mode,0x14)==p.ui &&
        skg::readable(p.data,0x128) && skg::field<void*>(p.data,0x10)==p.resource &&
        skg::readable(p.resource,0x2CB0) && skg::readable(p.ui,0x98) &&
        skg::field<uintptr_t>(p.ui,0)==a_vtable;
}
bool ready(Pending& p,std::string& error){
    p.mode=mode();
    if(!supported || !skg::readable(p.mode,0xDC) || skg::field<int>(p.mode,0xCC)!=1 ||
       skg::field<int>(p.mode,0xD0)!=0){error="The native Adventure Editor is not active";return false;}
    p.ui=skg::field<void*>(p.mode,0x14);p.data=skg::field<void*>(p.mode,0x74);
    p.resource=skg::readable(p.data,0x128)?skg::field<void*>(p.data,0x10):nullptr;
    if(!current(p)){error="The native editor resource or save UI is unavailable";return false;}
    const auto state=editor_native::state();
    if(pending || state.gesture || skg::field<uint32_t>(p.data,0x18) ||
       skg::field<uint32_t>(p.ui,0x48) || skg::field<uint32_t>(p.ui,0x54) ||
       skg::field<uint32_t>(p.ui,0x58)) {error="Finish the current native gesture or dialog first";return false;}
    p.before=skg::field<Key>(p.resource,8);error.clear();return true;
}

// Bypass the parsed-resource cache and independently read the installed native
// record. These are the same dc04 IRecord/IStream slots as adventure_collect.
bool persisted(const Key& key,std::string& error,std::vector<uint8_t>* output=nullptr){
    void* rm=a_rm?*reinterpret_cast<void**>(a_rm):nullptr;
    dbpf::Key wanted{key.instance,key.type,key.group},real{};
    if(!rm || !key.instance || key.type!=kAdventure){error="Native save did not produce an adventure key";return false;}
    void* database=skg::vcall<void*>(rm,0x30/4,&wanted,&real,(void*)nullptr);
    if(!database || !(real==wanted)){error="Native saved adventure record is not installed";return false;}
    void* record=nullptr;
    if(!skg::vcall<bool>(database,0x34/4,&real,&record,1,6,true,(void*)nullptr) || !record){
        error="Cannot reopen the native saved adventure record";return false;
    }
    auto release_record=[](void* value){skg::vcall<bool>(value,0x24/4);skg::vcall<int>(value,0x08/4);};
    std::unique_ptr<void,decltype(release_record)> owned_record(record,release_record);
    void* stream=skg::vcall<void*>(record,0x18/4);
    const uint32_t size=stream?skg::vcall<uint32_t>(stream,0x1C/4):0;
    if(!size || size>net::adventure_content::kMaxRecordBytes || !skg::vcall<bool>(stream,0x28/4,0,0)){
        error="Native saved adventure record is empty, unavailable or oversized";return false;
    }
    std::vector<uint8_t> bytes(size);uint32_t offset=0;
    while(offset<size){const int n=skg::vcall<int>(stream,0x30/4,bytes.data()+offset,size-offset);
        if(n<=0 || uint32_t(n)>size-offset){error="Native saved adventure record is incomplete";return false;}offset+=uint32_t(n);}
    owned_record.reset();
    void* object=nullptr;
    const bool loaded=skg::vcall<bool>(rm,0x1C/4,&wanted,&object,(void*)nullptr,(void*)nullptr,(void*)nullptr,(void*)nullptr);
    auto release=[](void* value){if(value)skg::vcall<int>(value,0x04/4);};
    std::unique_ptr<void,decltype(release)> owned(object,release);
    void* scenario=loaded&&object?skg::vcall<void*>(object,0x0C/4,kScenarioClass):nullptr;
    if(!skg::readable(scenario,0x2CB0) || skg::field<Key>(scenario,8)!=key){
        error="Native saved adventure could not be parsed independently";return false;
    }
    if(output)*output=std::move(bytes);
    return true;
}
bool SK_THISCALL pipeline(void* self){
    const bool watching=pending && pending->data==self && current(*pending);
    const bool success=original_pipeline(self);
    if(watching && pending && pending->data==self && current(*pending)){
        pending->executed=true;pending->success=success;
        pending->after=skg::field<Key>(pending->resource,8);
    }
    return success;
}
void SK_THISCALL return_native(void* self){
    if(C.owned_preview && C.owned_preview()){
        if(!return_notified){return_notified=true;if(C.return_requested)C.return_requested();}
        return;
    }
    return_notified=false;original_return(self);
}
void fail(const std::string& error){
    if(pending && current(*pending) && skg::field<uint32_t>(pending->ui,0x58)==2){
        skg::field<uint32_t>(pending->ui,0x58)=0;
        skg::thiscall<void>(a_hold,pending->ui,true);
    }
    pending.reset();if(C.error)C.error(error);
}
void poll(){
    if(!pending)return;
    if(!current(*pending)){pending.reset();if(C.cancelled)C.cancelled();return;}
    if(!pending->executed)return;
    if(!pending->success){fail("The native adventure save failed");return;}
    if(!pending->after.instance || pending->after.type!=kAdventure || pending->after==pending->before){
        fail("The native save did not establish a new adventure identity");return;
    }
    // The native successful-save message owns its confirmation dialog. Wait for
    // the player to dismiss it normally; never accept or close foreign dialogs.
    if(skg::field<uint32_t>(pending->data,0x18) || skg::field<uint32_t>(pending->ui,0x48) ||
       skg::field<uint32_t>(pending->ui,0x54) || skg::field<uint32_t>(pending->ui,0x58))return;
    std::string error;
    if(!persisted(pending->after,error)){fail(error);return;}
    const auto saved=pending->after;pending.reset();if(C.saved)C.saved(saved);
}
bool begin(bool copy,std::string& error){
    Pending p;if(!ready(p,error))return false;
    uint32_t reason=0;
    if(!skg::thiscall<bool>(a_can_save,p.data,&reason)){
        error="Native adventure save is unavailable (reason "+std::to_string(reason)+")";return false;
    }
    p.copy=copy;pending=p;
    // Mode2 is the ordinary Save command that remains in the editor. Mode3 is
    // Save+Exit and would destroy the collaboration's active native context.
    skg::field<uint32_t>(p.ui,0x58)=2;
    { NativeScope ownership; skg::thiscall<void>(a_begin_save,p.ui,!copy); }
    if(!pending || !current(*pending) || skg::field<uint32_t>(p.data,0x18)==0){
        if(pending && current(*pending) && skg::field<uint32_t>(p.ui,0x58)==2){
            skg::field<uint32_t>(p.ui,0x58)=0;skg::thiscall<void>(a_hold,p.ui,true);
        }
        pending.reset();error="Native adventure save did not start";return false;
    }
    error.clear();return true;
}
} // namespace

bool init(const SkHost* host,SkMod* mod,Ctx context){
    shutdown();H=host;M=mod;C=std::move(context);
    if(!H || std::string(H->exe_sha256())!="dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37")return false;
    a_mode=H->sym("App::cScenarioMode::ptr");a_rm=H->sym("Resource::ResourceManager_ptr");
    a_vtable=H->sym("SporeCoop::Editor::DisplayVtable");a_can_save=H->sym("SporeCoop::Editor::CanSave");
    a_begin_save=H->sym("SporeCoop::Editor::BeginSaveCopy");a_pipeline=H->sym("SporeCoop::Editor::SaveResourcePipeline");
    a_hold=H->sym("SporeCoop::Editor::SaveInputHold");a_preview=H->sym("SporeCoop::Editor::NativePreview");
    a_return=H->sym("SporeCoop::Editor::NativeReturn");a_cleanup=H->sym("SporeCoop::Editor::PreviewCleanup");
    a_return_display=H->sym("SporeCoop::Editor::ReturnDisplayMode");a_display=H->sym("SporeCoop::Editor::SelectDisplayMode");
    a_captain=H->sym("SporeCoop::Editor::PreviewCaptain");a_validate_placement=H->sym("SporeCoop::Editor::ValidatePlacement");
    a_command=H->sym("SporeCoop::Editor::EditorCommand");
    supported=a_mode&&a_rm&&a_vtable&&a_can_save&&a_begin_save&&a_pipeline&&a_hold&&a_preview&&a_return&&
        a_cleanup&&a_return_display&&a_display&&a_captain&&a_validate_placement&&a_command;
    if(supported)supported=H->hook_create(M,reinterpret_cast<void*>(a_pipeline),reinterpret_cast<void*>(pipeline),reinterpret_cast<void**>(&original_pipeline))==0;
    if(supported)supported=H->hook_create(M,reinterpret_cast<void*>(a_return),reinterpret_cast<void*>(return_native),reinterpret_cast<void**>(&original_return))==0;
    if(supported)supported=H->hook_create(M,reinterpret_cast<void*>(a_command),reinterpret_cast<void*>(command_native),reinterpret_cast<void**>(&original_command))==0;
    if(supported)supported=H->hook_create(M,reinterpret_cast<void*>(a_display),reinterpret_cast<void*>(display_native),reinterpret_cast<void**>(&original_display))==0;
    return supported;
}
void shutdown(){++generation;pending.reset();queued=false;supported=false;return_notified=false;owned_native=false;C={};}
bool available(){return supported;}
bool busy(){return bool(pending);}
bool save_as(std::string& error){return begin(true,error);}
bool save(std::string& error){return begin(false,error);}
bool read_saved(const Key& key,std::vector<uint8_t>& bytes,std::string& error){
    if(!supported){error="Native adventure save readback is unavailable";return false;}
    return persisted(key,error,&bytes);
}
bool playtest(bool on,std::string& error){
    NativeScope ownership;
    if(on){
        Pending p;if(!ready(p,error))return false;
        return_notified=false;
        void* captain=skg::thiscall<void*>(a_captain,p.data);
        if(!captain || !skg::thiscall<bool>(a_validate_placement,p.data,captain,0,0,0)){
            error="Choose a valid native adventure captain before playtest";return false;
        }
        skg::thiscall<void>(a_display,p.ui,1);
        if(mode()!=p.mode || skg::field<int>(p.mode,0xCC)!=2){error="Native adventure playtest did not start";return false;}
    }else{
        void* m=mode();
        if(!supported || pending || !editor_native::state().playtesting || !skg::readable(m,0xDC) || skg::field<int>(m,0xCC)!=2 || skg::field<int>(m,0xD0)!=0){
            error="The native editor playtest is not active";return false;
        }
        void* play=skg::field<void*>(m,0x78);void* ui=skg::field<void*>(m,0x14);
        if(!skg::readable(play,0xA0) || !skg::readable(ui,0x98) || skg::field<uintptr_t>(ui,0)!=a_vtable){error="Native playtest return context is unavailable";return false;}
        // Exact native hammer-stop order F0EB09..F0EB25, not quickplay Done.
        skg::thiscall<void>(a_cleanup,play);skg::thiscall<void>(a_return_display,ui,0);original_return(m);
        if(mode()!=m || skg::field<int>(m,0xCC)!=1){error="Native playtest did not return to the editor";return false;}
    }
    error.clear();return true;
}
void tick(){
    if(!supported || !pending || queued)return;
    queued=true;auto* ticket=new uint64_t(generation);
    H->run_outside_frame([](void* opaque){std::unique_ptr<uint64_t> ticket(static_cast<uint64_t*>(opaque));
        if(*ticket!=generation)return;queued=false;poll();},ticket);
}
} // namespace editor_native_save
