#include "editor_creation.h"
#include "editor_creation_native.h"
#include "editor_creation_ui.h"
#include "game.h"
#include "modes.h"
#include "sporekit_game.hpp"
#include "fixes.h"
#include <array>
#include <cstdio>
#include <utility>

namespace editor_creation {
namespace {
const SkHost* H;
SkMod* M;
Ctx C;
bool supported=false,queued=false;
bool g_creation_chain_fix=true;
COOP_FIX("editor.creation_chain", "BUG-1028", g_creation_chain_fix, "retain ownership of the native creature-to-outfitter result");
uint64_t next_serial=0;
Submit original_submit=nullptr;
using Notify=void(SK_THISCALL*)(void*,void*);
Notify original_notify=nullptr;
enum class Phase { Preparing,Submitted,Returning };
struct Session {
    Scope scope;
    Request request;
    void* native_request=nullptr; // Our intrusive reference, never adventure data.
    Phase phase=Phase::Preparing;
    double started=0;
    bool entered=false,stale=false;
    std::optional<Result> result;
    uint32_t observed_mode=UINT32_MAX;
    unsigned observations=0;
};
std::optional<Session> session;
std::optional<Result> retained_saved;
std::optional<editor::ObjectId> opening_target;
std::optional<editor_native::GestureScope> opening_owner;
void* opening_request=nullptr;
std::string status;
void observe(const char* boundary,void* request=nullptr){
    if(!H||!H->log||!M||!session||session->observations++>=32)return;
    auto* value=request?request:session->native_request;
    const bool valid=skg::readable(value,0x9C);
    H->log(M,SK_INFO,"Creation transition: serial=%llu phase=%u boundary=%s mode=%08X owned=%p request=%p editor=%08X return=%08X",
        (unsigned long long)session->scope.serial,unsigned(session->phase),boundary,game::mode_id(),session->native_request,value,
        valid?skg::field<uint32_t>(value,0xC):0,valid?skg::field<uint32_t>(value,0x1C):0);
}

void addref(void* object){if(object)skg::vcall<int>(object,1);}
void release(void* object){if(object)skg::vcall<int>(object,2);}
bool family(uint32_t type){
    switch(type){
    case 0x2B978C46:case 0x2399BE55:case 0x24682294:
    case 0x476A98C7:return true;
    default:return false;
    }
}
uint32_t default_editor(uint32_t type){
    const auto variants=new_variants(type);return variants.empty()?0:variants.front().editor_id;
}
uint32_t editor_family(uint32_t editor_id){
    for(auto type:{0x2B978C46u,0x2399BE55u,0x24682294u,0x476A98C7u})
        for(const auto& variant:new_variants(type))if(variant.editor_id==editor_id)return type;
    return 0;
}
void tell(const std::string& message){status=message;if(C.notice)C.notice(message);}
bool current(){return session && C.current && C.current(session->scope);}
bool owns_request(void* request){
    if(!session || !request)return false;
    if(request==session->native_request)return true;
    if(!g_creation_chain_fix || session->phase==Phase::Preparing)return false;
    // Native57C14F..57C160 follows the intrusive request+98 link without Submit
    // or an intermediate NotifyResult. Our retained root owns the entire chain.
    // Accept only verified adventure-compatible descendants with the same
    // resource family and return mode, never an arbitrary active editor request.
    auto* parent=session->native_request;
    if(!skg::readable(parent,0x9C))return false;
    const auto return_mode=skg::field<uint32_t>(parent,0x1C);
    std::array<void*,8> seen{parent};
    for(size_t depth=1;depth<seen.size();++depth){
        auto* child=skg::field<void*>(parent,0x98);
        if(!child || !skg::readable(child,0x9C))return false;
        for(size_t prior=0;prior<depth;++prior)if(seen[prior]==child)return false;
        if(skg::field<void*>(child,0)!=skg::field<void*>(parent,0) ||
           skg::field<int>(child,8)<=0 || skg::field<uint32_t>(child,0x1C)!=return_mode)return false;
        const auto id=skg::field<uint32_t>(child,0xC);
        const auto type=skg::field<editor::ResourceKey>(child,0x10).type;
        const auto allowed_family=editor_family(id);
        if(type!=session->request.source.type ||
           (allowed_family!=type && !(type==0x2B978C46 && id==0xA56567F7)))return false;
        if(child==request)return true;
        seen[depth]=child;parent=child;
    }
    return false;
}
void finish(bool success,const std::string& reason){
    if(!session)return;
    auto item=std::move(*session);session.reset();
    release(item.native_request);
    status=reason;
    if(C.finished)C.finished(item.scope,success,reason);
}
void SK_THISCALL notify(void* editor,void* result){
    if(session)observe("notify",skg::readable(editor,0x1D0)?skg::field<void*>(editor,0x1CC):nullptr);
    // 57A710 consumes result and releases editor+1CC. Match the owned chain
    // before reading anything; unrelated editors/results stay entirely native.
    if(!session || !skg::readable(editor,0x1D0) ||
       !owns_request(skg::field<void*>(editor,0x1CC)) || !skg::readable(result,0x48)){
        original_notify(editor,result);return;
    }
    if(!session->result){
        Result completed{session->request,skg::field<editor::ResourceKey>(result,0x18),skg::field<bool>(result,0x44)};
        if(!completed.cancelled && (!completed.saved.instance || !family(completed.saved.type) ||
           completed.saved.type!=session->request.source.type)){
            completed.cancelled=true;status="The native editor returned an unsupported creation";
        }
        session->result=completed;session->phase=Phase::Returning;
        if(!completed.cancelled)retained_saved=completed;
    }
    // NotifyResult posts 030C11C7 through IMessageManager+18 (MessagePost), so
    // ED4289..ED42C1 reads +44 after this hook returns. Keep this exact owned
    // native result cancelled for its entire posted lifetime. The copied Result
    // above retains the real completion for coordinated admission and return.
    // Pin across NotifyResult's release; the posted message owns its own ref.
    addref(result);
    skg::field<bool>(result,0x44)=true;
    original_notify(editor,result);
    release(result);
}
void service(){
    if(!session)return;
    if(!current()){
        if(session->phase==Phase::Preparing){finish(false,"The creation request belongs to an expired shared editor");return;}
        session->stale=true;
    }
    if(session->phase!=Phase::Preparing && session->request.target &&
       (!C.lease_valid || !C.lease_valid(session->scope,*session->request.target)))session->stale=true;
    if(session->phase==Phase::Preparing){
        if(GetKeyState(VK_ESCAPE)&0x8000){finish(false,"Creation editor entry cancelled");return;}
        if(H->time_seconds()-session->started>30){finish(false,"Creation editor ownership timed out");return;}
        std::string error;
        auto decision=C.prepare?C.prepare(session->scope,session->request,error):PrepareResult::Reject;
        if(decision==PrepareResult::Wait)return;
        if(decision==PrepareResult::Reject){finish(false,error.empty()?"Creation editor entry was rejected":error);return;}
        if(!current()){finish(false,"The shared editor changed before creation entry");return;}
        if(session->request.target && (!C.lease_valid || !C.lease_valid(session->scope,*session->request.target))){
            finish(false,"Creation content ownership was lost before entry");return;
        }
        session->phase=Phase::Submitted;session->started=H->time_seconds();
        auto* request=session->native_request;
        // The supplied trampoline avoids recursively submitting or acquiring
        // ownership twice. The native engine retains its own request reference.
        if(!original_submit || !original_submit(request))finish(false,"The native creation editor declined the request");
        return;
    }
    const auto mode=game::mode_id();
    if(session->observed_mode!=mode){session->observed_mode=mode;observe("mode");}
    if(session->phase==Phase::Submitted){
        if(mode==modes::kEditor || mode==modes::kEdit)session->entered=true;
        else if(session->entered && (mode==modes::kScenario || mode==modes::kMenu))
            finish(false,"The native creation editor returned without its owned result");
        // Loading a large asset can take minutes. Do not expire a live editor
        // merely because the user is designing or native baking is slow.
        return;
    }
    if(!session->result || mode==modes::kEditor || mode==modes::kEdit || mode==modes::kLoad)return;
    if(session->stale){finish(false,"The local creation was saved, but its shared edit session expired");return;}
    std::string error;
    if(C.returned && C.returned(session->scope,*session->result,error)){
        finish(true,session->result->cancelled?"Returned from the creation editor":"Creation saved and returned to the adventure");
    }else if(!error.empty())finish(false,error);
}
void SK_CDECL drain(void*){queued=false;service();}
}

bool init(const SkHost* host,SkMod* mod,Ctx context){
    H=host;M=mod;C=std::move(context);
    auto address=H?H->sym("SporeCoop::CreationEditor::NotifyResult"):0;
    supported=H && skg::supported_gog2024(H->exe_sha256()) && address &&
        H->sym("SporeCoop::CreationEditor::RequestCtor") && H->sym("SporeCoop::CreationEditor::Allocate") &&
        H->hook_create(M,reinterpret_cast<void*>(address),reinterpret_cast<void*>(notify),reinterpret_cast<void**>(&original_notify))==0;
    if(supported)supported=editor_creation_native::init(H,M);
    if(supported)editor_creation_ui::init(H);
    return supported;
}
void shutdown(){editor_creation_ui::shutdown();if(session){release(session->native_request);session.reset();}editor_creation_native::shutdown();C={};H=nullptr;M=nullptr;supported=queued=false;original_submit=nullptr;}
void tick(){if(H)editor_creation_ui::tick();if(H && session && !queued){queued=true;H->run_outside_frame(drain,nullptr);}}
bool intercept(void* request,Submit original){
    if(!supported || !original || !skg::readable(request,0x9C)){tell("Native creation editor support is unavailable");return false;}
    if(session){observe("nested-submit",request);tell("Finish or cancel your current creation before opening another editor");return false;}
    auto owner=C.scope?C.scope():std::nullopt;
    if(!owner || owner->project==editor::ProjectId{} || !owner->epoch || !owner->author.generation || next_serial==UINT64_MAX){
        tell("Creation entry requires a current shared adventure");return false;
    }
    if(opening_owner && (request!=opening_request || *owner!=*opening_owner)){
        tell("The creation request changed its shared adventure before entry");return false;
    }
    Request input{skg::field<uint32_t>(request,0xC),skg::field<editor::ResourceKey>(request,0x10),opening_target};
    const auto type=family(input.source.type)?input.source.type:
        input.source.instance?0:editor_family(input.editor_id);
    if(!family(type) || (!input.source.instance && editor_family(input.editor_id)!=type)){
        tell("This editor cannot produce the selected adventure creation variant");return false;
    }
    if(!input.source.instance)input.source.type=type;
    original_submit=original;
    addref(request);
    session=Session{Scope{owner->project,owner->epoch,owner->author,++next_serial},input,request,Phase::Preparing,H->time_seconds(),false,false,std::nullopt};
    status="Preparing your local creation editor";tick();
    // Native picker entry is asynchronous. Its request remains alive while
    // ownership arrives even when the native caller releases its reference.
    return false;
}
bool open(uint32_t resource_type,const editor::ResourceKey& supplied_source,std::optional<editor::ObjectId> target,std::string& error,uint32_t editor_id){
    const auto source=supplied_source;
    if(!source.instance && !editor_id)editor_id=default_editor(resource_type);
    const auto scope_callback=C.scope;const auto* host=H;
    const auto owner=scope_callback?scope_callback():std::nullopt;
    if(!supported || session || opening_owner || !owner || !owner->epoch || !owner->author.generation ||
       !family(resource_type) || (source.instance && (source.type!=resource_type || !source.group || editor_id)) ||
       (!source.instance && editor_family(editor_id)!=resource_type)){
        error="This creation editor is unavailable during the current transition";return false;
    }
    const auto allocation=host->sym("SporeCoop::CreationEditor::Allocate"),ctor=host->sym("SporeCoop::CreationEditor::RequestCtor"),submit=host->sym("Editors::EditorRequest::Submit");
    if(!allocation || !ctor || !submit){error="Verified native creation entry is unavailable";return false;}
    void* request=skg::cdecl_call<void*>(allocation,size_t(0x9C),"CoopCreationRequest",0,0,0,0);
    if(!request){error="Native creation request allocation failed";return false;}
    request=skg::thiscall<void*>(ctor,request);addref(request);
    struct Reference {void* value;~Reference(){release(value);}} reference{request};
    if(!supported || H!=host || session || !scope_callback || scope_callback()!=owner){
        error="The creation request changed its shared adventure before entry";return false;
    }
    // Existing content must keep RequestCtor's FFFFFFFF sentinel. Native
    // Submit 5A930F..5A931D resolves its installed metadata subtype through
    // 5A9040 -> GetEditorForAssetType, preserving air/water and other variants.
    // New uses the specialized GA category choice, just as AssetBrowser's
    // 645C30 -> 64AF5F route does, including its native part-list constraints.
    if(!source.instance)skg::field<uint32_t>(request,0xC)=editor_id;
    skg::field<editor::ResourceKey>(request,0x10)=source;
    skg::field<bool>(request,0x20)=!source.instance;
    struct Opening {~Opening(){opening_target.reset();opening_owner.reset();opening_request=nullptr;}} opening;
    opening_target=target;opening_owner=owner;opening_request=request;
    skg::cdecl_call<bool>(submit,request); // Existing hook supplies its trampoline.
    const bool accepted=session && session->native_request==request && session->scope.project==owner->project &&
        session->scope.epoch==owner->epoch && session->scope.author==owner->author;
    if(!accepted){error=status.empty()?"The creation request was rejected":status;return false;}
    error.clear();return true;
}
bool active(){return session.has_value();}
bool designing(){return session && session->phase!=Phase::Preparing;}
bool available(){return supported;}
std::optional<Scope> scope(){return session?std::optional<Scope>{session->scope}:std::nullopt;}
std::optional<Result> last_saved(){return retained_saved;}
void cancel_pending(){if(session && session->phase==Phase::Preparing)finish(false,"Creation editor entry cancelled");}
std::string diagnostic(){
    char identity[64];
    if(retained_saved)std::snprintf(identity,sizeof(identity)," last_saved=%08X!%08X.%08X",retained_saved->saved.group,retained_saved->saved.instance,retained_saved->saved.type);
    else std::snprintf(identity,sizeof(identity)," last_saved=none");
    if(!session)return std::string("creation_editor active=0 available=")+(supported?"1":"0")+identity+" status="+status;
    return "creation_editor active=1 phase="+std::to_string(int(session->phase))+" serial="+std::to_string(session->scope.serial)+
        " stale="+std::to_string(session->stale)+" entered="+std::to_string(session->entered)+identity+" status="+status;
}
} // namespace editor_creation
