#include "editor_creation.h"
#include "editor_creation_native.h"
#include "editor_creation_ui.h"
#include "game.h"
#include "modes.h"
#include "sporekit_game.hpp"
#include <cstdio>
#include <utility>

namespace editor_creation {
namespace {
const SkHost* H;
SkMod* M;
Ctx C;
bool supported=false,queued=false;
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
};
std::optional<Session> session;
std::optional<Result> retained_saved;
std::optional<editor::ObjectId> opening_target;
std::string status;

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
    // Native default selection at 5A909C..5A90F3. Configured specialized
    // editors retain their original native IDs when entering through a picker.
    switch(type){
    case 0x2B978C46:return 0x465C50BA;
    case 0x2399BE55:return 0xD817CD63;
    case 0x24682294:return 0x99F87089;
    case 0x476A98C7:return 0x96B24187;
    default:return 0;
    }
}
uint32_t editor_family(uint32_t editor_id){
    for(auto type:{0x2B978C46u,0x2399BE55u,0x24682294u,0x476A98C7u})
        if(default_editor(type)==editor_id)return type;
    return 0;
}
void tell(const std::string& message){status=message;if(C.notice)C.notice(message);}
bool current(){return session && C.current && C.current(session->scope);}
void finish(bool success,const std::string& reason){
    if(!session)return;
    auto item=std::move(*session);session.reset();
    release(item.native_request);
    status=reason;
    if(C.finished)C.finished(item.scope,success,reason);
}
void SK_THISCALL notify(void* editor,void* result){
    // 57A710 consumes result and releases editor+1CC. Match the owned request
    // before reading anything; unrelated editors/results stay entirely native.
    if(!session || !skg::readable(editor,0x1D0) ||
       skg::field<void*>(editor,0x1CC)!=session->native_request || !skg::readable(result,0x48)){
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
    if(session){tell("Finish or cancel your current creation before opening another editor");return false;}
    auto owner=C.scope?C.scope():std::nullopt;
    if(!owner || owner->project==editor::ProjectId{} || !owner->epoch || !owner->author.generation || next_serial==UINT64_MAX){
        tell("Creation entry requires a current shared adventure");return false;
    }
    Request input{skg::field<uint32_t>(request,0xC),skg::field<editor::ResourceKey>(request,0x10),opening_target};
    const auto type=family(input.source.type)?input.source.type:
        input.source.instance?0:editor_family(input.editor_id);
    if(!family(type)){tell("This editor cannot produce a supported adventure creation");return false;}
    if(!input.source.instance)input.source.type=type;
    original_submit=original;
    addref(request);
    session=Session{Scope{owner->project,owner->epoch,owner->author,++next_serial},input,request,Phase::Preparing,H->time_seconds(),false,false,std::nullopt};
    status="Preparing your local creation editor";tick();
    // Native picker entry is asynchronous. Its request remains alive while
    // ownership arrives even when the native caller releases its reference.
    return false;
}
bool open(uint32_t resource_type,const editor::ResourceKey& source,std::optional<editor::ObjectId> target,std::string& error){
    const auto owner=C.scope?C.scope():std::nullopt;
    if(!supported || session || !owner || !owner->epoch || !owner->author.generation ||
       !family(resource_type) || (source.instance && (source.type!=resource_type || !source.group))){
        error="This creation editor is unavailable during the current transition";return false;
    }
    const auto allocation=H->sym("SporeCoop::CreationEditor::Allocate"),ctor=H->sym("SporeCoop::CreationEditor::RequestCtor"),submit=H->sym("Editors::EditorRequest::Submit");
    if(!allocation || !ctor || !submit){error="Verified native creation entry is unavailable";return false;}
    void* request=skg::cdecl_call<void*>(allocation,size_t(0x9C),"CoopCreationRequest",0,0,0,0);
    if(!request){error="Native creation request allocation failed";return false;}
    request=skg::thiscall<void*>(ctor,request);addref(request);
    // Existing content must keep RequestCtor's FFFFFFFF sentinel. Native
    // Submit 5A930F..5A931D resolves its installed metadata subtype through
    // 5A9040 -> GetEditorForAssetType, preserving air/water and other variants.
    // New content has no subtype yet and uses the verified family default.
    if(!source.instance)skg::field<uint32_t>(request,0xC)=default_editor(resource_type);
    skg::field<editor::ResourceKey>(request,0x10)=source;
    skg::field<bool>(request,0x20)=!source.instance;
    opening_target=target;
    skg::cdecl_call<bool>(submit,request); // Existing hook supplies its trampoline.
    opening_target.reset();
    const bool accepted=session && session->native_request==request;
    release(request);
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
