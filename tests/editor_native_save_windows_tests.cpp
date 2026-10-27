// Real i686 calling conventions, native ownership and asynchronous save fencing.
#include "../mods/coop/editor_native_save.cpp"
#include <array>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <deque>
#include <map>

static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}} while(0)
namespace {
using Key=editor::ResourceKey;
struct Object{void** vt;};
std::array<uint8_t,0xE0> t_mode;
std::array<uint8_t,0x128> t_data;
std::array<uint8_t,0x98> t_ui;
std::array<uint8_t,0x2CB0> t_resource,t_parsed;
std::array<uint8_t,0x100> t_play;
void* t_mode_pointer=t_mode.data();
void* t_rm_pointer;
std::array<void*,24> t_ui_vtable{},t_manager_vt{},t_database_vt{},t_record_vt{},t_stream_vt{},t_private_vt{};
Object t_manager{t_manager_vt.data()},t_database{t_database_vt.data()},t_record{t_record_vt.data()},t_stream{t_stream_vt.data()},t_private{t_private_vt.data()};
std::deque<std::pair<SkTaskFn,void*>> t_tasks;
std::vector<int> t_order;
Key t_before{41,0x366A930D,0x12345678},t_after{42,0x366A930D,0x87654321};
bool t_gate=true,t_native_success=true,t_readback=true,t_truncate=false,t_gesture=false,t_no_start=false,t_unchanged=false;
int t_saved=0,t_cancelled=0,t_errors=0,t_holds=0,t_closed=0,t_released=0,t_native_calls=0;
uint32_t t_offset=0;
bool t_replaced=false;
bool t_owned_preview=false;
bool t_shared=false,t_editor_active=true;
unsigned t_command_calls=0,t_display_calls=0;
unsigned t_set_captain_calls=0;
unsigned t_return_requests=0;
unsigned t_exit_calls=0,t_exit_confirmed=0,t_exit_cancelled=0;
std::vector<editor_native_save::Request> t_requests;
std::string t_error;
Key t_saved_key;
using Fn=bool(SK_THISCALL*)(void*);
Fn t_detour;
using ReturnFn=void(SK_THISCALL*)(void*);
ReturnFn t_return_detour;
ReturnFn t_continue_detour;
using CommandFn=void(SK_THISCALL*)(void*,int);
CommandFn t_command_detour,t_display_detour;
bool SK_THISCALL t_can_save(void* data,uint32_t* reason){CHECK(data==t_data.data());const bool allowed=t_gate && (!t_unchanged || skg::field<bool>(data,0x85));*reason=allowed?0:5;return allowed;}
void SK_THISCALL t_begin(void* ui,bool replace){CHECK(ui==t_ui.data());CHECK(skg::field<uint32_t>(ui,0x58)==2);t_replaced=replace;
    if(!t_no_start){skg::field<uint32_t>(t_data.data(),0x18)=1;skg::field<uint8_t>(t_data.data(),0x1C)=!replace;}}
bool SK_THISCALL t_pipeline(void* data){++t_native_calls;if(data!=t_data.data())return t_native_success;
    skg::field<uint32_t>(data,0x18)=0;if(t_native_success)skg::field<Key>(t_resource.data(),8)=t_after;return t_native_success;}
void SK_THISCALL t_hold(void* ui,bool release){CHECK(ui==t_ui.data());CHECK(release);++t_holds;}
void* SK_THISCALL t_find(void* self,const dbpf::Key* key,dbpf::Key* real,void* zero){CHECK(self==&t_manager);CHECK(!zero);*real=*key;return t_readback?&t_database:nullptr;}
bool SK_THISCALL t_open(void* self,const dbpf::Key*,void** out,int a,int b,bool c,void* d){CHECK(self==&t_database);CHECK(a==1&&b==6&&c&&!d);*out=&t_record;return true;}
void* SK_THISCALL t_get_stream(void* self){CHECK(self==&t_record);return &t_stream;}
bool SK_THISCALL t_close(void* self){CHECK(self==&t_record);++t_closed;return true;}
int SK_THISCALL t_release_record(void* self){CHECK(self==&t_record);++t_released;return 0;}
uint32_t SK_THISCALL t_size(void* self){CHECK(self==&t_stream);return 512;}
bool SK_THISCALL t_seek(void* self,int a,int b){CHECK(self==&t_stream);CHECK(!a&&!b);t_offset=0;return true;}
int SK_THISCALL t_read(void* self,void* out,uint32_t size){CHECK(self==&t_stream);if(t_truncate&&t_offset)return 0;size=std::min<uint32_t>(size,256);std::memset(out,0xA5,size);t_offset+=size;return int(size);}
bool SK_THISCALL t_get_private(void* self,const dbpf::Key* key,void** out,void* a,void* b,void* c,void* d){CHECK(self==&t_manager);CHECK(!a&&!b&&!c&&!d);CHECK(t_closed==t_released&&t_closed>0);*out=&t_private;skg::field<Key>(t_parsed.data(),8)={key->instance,key->type,key->group};return true;}
void* SK_THISCALL t_query(void* self,uint32_t type){CHECK(self==&t_private);CHECK(type==0xE742574A);return t_parsed.data();}
int SK_THISCALL t_release_private(void* self){CHECK(self==&t_private);return 0;}
void SK_THISCALL t_preview(void*,int){}
void SK_THISCALL t_return(void* self){CHECK(self==t_mode.data());t_order.push_back(3);skg::field<int>(self,0xCC)=1;}
void SK_THISCALL t_cleanup(void* self){CHECK(self==t_play.data());t_order.push_back(1);}
void SK_THISCALL t_return_display(void* self,int mode){CHECK(self==t_ui.data());CHECK(mode==0);t_order.push_back(2);}
void SK_THISCALL t_display(void* self,int mode){CHECK(self==t_ui.data());CHECK(mode==0||mode==1);CHECK(!t_shared||mode==0||editor_native_save::owned_native);++t_display_calls;if(mode==1)skg::field<int>(t_mode.data(),0xCC)=2;}
void SK_THISCALL t_command(void* self,int action){CHECK(self==t_ui.data());++t_command_calls;
    if(editor_native_save::exiting)skg::field<uint32_t>(self,0x58)=uint32_t(action);}
void SK_THISCALL t_continue(void* self){CHECK(self==t_ui.data());++t_exit_calls;skg::field<uint32_t>(self,0x58)=0;}
void* SK_THISCALL t_captain(void* self){CHECK(self==t_data.data());return t_resource.data();}
bool SK_THISCALL t_set_captain(void* self,void* captain,int a,int b,int c){CHECK(self==t_data.data());CHECK(captain==t_resource.data());CHECK(!a&&!b&&!c);++t_set_captain_calls;return true;}
uintptr_t t_sym(const char* name){
    const std::map<std::string,uintptr_t> names{
        {"App::cScenarioMode::ptr",uintptr_t(&t_mode_pointer)},{"Resource::ResourceManager_ptr",uintptr_t(&t_rm_pointer)},
        {"SporeCoop::Editor::DisplayVtable",uintptr_t(t_ui_vtable.data())},{"SporeCoop::Editor::CanSave",uintptr_t(t_can_save)},
        {"SporeCoop::Editor::BeginSaveCopy",uintptr_t(t_begin)},{"SporeCoop::Editor::SaveResourcePipeline",uintptr_t(t_pipeline)},
        {"SporeCoop::Editor::SaveInputHold",uintptr_t(t_hold)},{"SporeCoop::Editor::NativePreview",uintptr_t(t_preview)},
        {"SporeCoop::Editor::NativeReturn",uintptr_t(t_return)},{"SporeCoop::Editor::PreviewCleanup",uintptr_t(t_cleanup)},
        {"SporeCoop::Editor::ReturnDisplayMode",uintptr_t(t_return_display)},{"SporeCoop::Editor::SelectDisplayMode",uintptr_t(t_display)},
        {"SporeCoop::Editor::PreviewCaptain",uintptr_t(t_captain)},{"SporeCoop::Editor::ValidatePlacement",uintptr_t(t_set_captain)},
        {"SporeCoop::Editor::EditorCommand",uintptr_t(t_command)},
        {"SporeCoop::Editor::ContinueCommand",uintptr_t(t_continue)}};
    auto it=names.find(name);return it==names.end()?0:it->second;
}
const char* t_sha(){return "dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37";}
int t_hook(SkMod*,void* target,void* detour,void** original){*original=target;
    if(target==reinterpret_cast<void*>(t_pipeline))t_detour=reinterpret_cast<Fn>(detour);
    else if(target==reinterpret_cast<void*>(t_return))t_return_detour=reinterpret_cast<ReturnFn>(detour);
    else if(target==reinterpret_cast<void*>(t_command))t_command_detour=reinterpret_cast<CommandFn>(detour);
    else if(target==reinterpret_cast<void*>(t_continue))t_continue_detour=reinterpret_cast<ReturnFn>(detour);
    else{CHECK(target==reinterpret_cast<void*>(t_display));t_display_detour=reinterpret_cast<CommandFn>(detour);}return 0;}
void t_outside(SkTaskFn fn,void* user){t_tasks.emplace_back(fn,user);}
void drain(){auto tasks=std::move(t_tasks);t_tasks.clear();for(auto [fn,user]:tasks)fn(user);}
void poll(){editor_native_save::tick();drain();}
void reset(){
    editor_native_save::shutdown();drain();t_mode.fill(0);t_data.fill(0);t_ui.fill(0);t_resource.fill(0);t_parsed.fill(0);t_play.fill(0);t_order.clear();
    t_mode_pointer=t_mode.data();t_rm_pointer=&t_manager;t_gate=t_native_success=t_readback=true;t_truncate=t_gesture=t_no_start=t_unchanged=false;
    t_saved=t_cancelled=t_errors=t_holds=t_closed=t_released=t_native_calls=0;t_replaced=false;t_error.clear();t_saved_key={};
    t_owned_preview=false;t_return_requests=0;t_shared=false;t_editor_active=true;t_command_calls=t_display_calls=t_set_captain_calls=0;
    t_exit_calls=t_exit_confirmed=t_exit_cancelled=0;t_requests.clear();
    skg::field<int>(t_mode.data(),0xCC)=1;skg::field<void*>(t_mode.data(),0x14)=t_ui.data();skg::field<void*>(t_mode.data(),0x74)=t_data.data();skg::field<void*>(t_mode.data(),0x78)=t_play.data();
    skg::field<void*>(t_data.data(),0x10)=t_resource.data();skg::field<uintptr_t>(t_ui.data(),0)=uintptr_t(t_ui_vtable.data());skg::field<Key>(t_resource.data(),8)=t_before;
    t_manager_vt[0x30/4]=reinterpret_cast<void*>(t_find);t_manager_vt[0x1C/4]=reinterpret_cast<void*>(t_get_private);t_database_vt[0x34/4]=reinterpret_cast<void*>(t_open);
    t_record_vt[0x18/4]=reinterpret_cast<void*>(t_get_stream);t_record_vt[0x24/4]=reinterpret_cast<void*>(t_close);t_record_vt[0x08/4]=reinterpret_cast<void*>(t_release_record);
    t_stream_vt[0x1C/4]=reinterpret_cast<void*>(t_size);t_stream_vt[0x28/4]=reinterpret_cast<void*>(t_seek);t_stream_vt[0x30/4]=reinterpret_cast<void*>(t_read);
    t_private_vt[0x0C/4]=reinterpret_cast<void*>(t_query);t_private_vt[0x04/4]=reinterpret_cast<void*>(t_release_private);
    static SkHost host{};host.sym=t_sym;host.exe_sha256=t_sha;host.hook_create=t_hook;host.run_outside_frame=t_outside;
    editor_native_save::Ctx ctx;ctx.saved=[](Key key){++t_saved;t_saved_key=key;};ctx.cancelled=[](){++t_cancelled;};ctx.error=[](const std::string& e){++t_errors;t_error=e;};
    ctx.owned_preview=[](){return t_owned_preview;};ctx.return_requested=[](){++t_return_requests;};
    ctx.shared_active=[](){return t_shared;};
    ctx.request=[](editor_native_save::Request r){t_requests.push_back(r);};
    ctx.exit_confirmed=[](){++t_exit_confirmed;};ctx.exit_cancelled=[](){++t_exit_cancelled;};
    CHECK(editor_native_save::init(&host,nullptr,std::move(ctx)));CHECK(editor_native_save::available());
}
}
namespace editor_native {State state(){State s;s.gesture=t_gesture;
    s.editing=t_editor_active&&skg::field<int>(t_mode.data(),0xCC)==1&&skg::field<int>(t_mode.data(),0xD0)==0;
    s.ready=s.editing;s.playtesting=t_editor_active&&skg::field<int>(t_mode.data(),0xCC)==2&&skg::field<int>(t_mode.data(),0xD0)==0;return s;}}
int main(){
    std::string error;
    reset();t_shared=true;
    t_command_detour(t_ui.data(),1);t_command_detour(t_ui.data(),2);t_command_detour(t_ui.data(),3);
    t_display_detour(t_ui.data(),1);
    CHECK(t_requests==std::vector<editor_native_save::Request>({editor_native_save::Request::Exit,
        editor_native_save::Request::Save,editor_native_save::Request::SaveExit,editor_native_save::Request::Play}));
    CHECK(!t_command_calls&&!t_display_calls);
    CHECK(editor_native_save::request_exit(error));CHECK(t_command_calls==1&&!t_exit_confirmed&&!t_exit_calls);
    t_continue_detour(t_ui.data());t_continue_detour(t_ui.data());CHECK(t_exit_confirmed==1&&!t_exit_calls);
    CHECK(editor_native_save::complete_exit(error));CHECK(t_exit_calls==1&&!t_exit_cancelled);
    reset();CHECK(editor_native_save::request_exit(error));skg::field<uint32_t>(t_ui.data(),0x58)=0;poll();
    CHECK(t_exit_cancelled==1&&!t_exit_confirmed&&!t_exit_calls);
    reset();CHECK(editor_native_save::request_exit(error));t_mode_pointer=nullptr;poll();CHECK(t_exit_cancelled==1&&!t_exit_calls);
    reset();t_shared=true;CHECK(editor_native_save::request_exit(error));t_shared=false;
    t_continue_detour(t_ui.data());CHECK(t_exit_calls==1&&!t_exit_confirmed);
    reset();CHECK(editor_native_save::restore_identity({}, {9,true,true},error));
    CHECK(skg::field<Key>(t_resource.data(),8)==Key{}&&skg::field<uint32_t>(t_data.data(),0x80)==9&&skg::field<bool>(t_data.data(),0x84));
    reset();CHECK(editor_native_save::save_as(error));CHECK(editor_native_save::busy());CHECK(!t_replaced);CHECK(skg::field<uint8_t>(t_data.data(),0x1C)==1);CHECK(!editor_native_save::save_as(error));
    CHECK(t_detour(t_data.data()));for(int i=0;i<2000;++i)poll();CHECK(!t_saved&&editor_native_save::busy());CHECK(t_offset==0);
    skg::field<uint32_t>(t_ui.data(),0x58)=0;poll();CHECK(t_saved==1&&t_saved_key==t_after);CHECK(t_offset==512);CHECK(t_closed==1&&t_released==1);CHECK(!editor_native_save::busy());poll();CHECK(t_saved==1);
    reset();CHECK(editor_native_save::save(error));CHECK(t_replaced);CHECK(skg::field<uint8_t>(t_data.data(),0x1C)==0);
    reset();t_gate=false;CHECK(!editor_native_save::save_as(error));CHECK(!editor_native_save::busy());CHECK(skg::field<Key>(t_resource.data(),8)==t_before);CHECK(!t_saved&&!t_errors);
    reset();t_unchanged=true;CHECK(!editor_native_save::save(error));CHECK(!skg::field<bool>(t_data.data(),0x85));
    CHECK(editor_native_save::save_as(error));CHECK(skg::field<bool>(t_data.data(),0x85));
    skg::field<uint32_t>(t_data.data(),0x18)=0;skg::field<uint32_t>(t_ui.data(),0x58)=0;poll();
    CHECK(!editor_native_save::busy()&&t_cancelled==1&&!skg::field<bool>(t_data.data(),0x85));
    reset();t_gesture=true;CHECK(!editor_native_save::save_as(error));CHECK(!editor_native_save::busy());
    reset();t_no_start=true;CHECK(!editor_native_save::save_as(error));CHECK(!editor_native_save::busy());CHECK(t_holds==1&&skg::field<uint32_t>(t_ui.data(),0x58)==0);CHECK(!t_errors);
    reset();CHECK(editor_native_save::save_as(error));t_native_success=false;CHECK(!t_detour(t_data.data()));poll();CHECK(t_errors==1&&!t_saved&&!editor_native_save::busy());CHECK(t_holds==1&&skg::field<uint32_t>(t_ui.data(),0x58)==0);
    reset();CHECK(editor_native_save::save_as(error));CHECK(t_detour(t_data.data()));skg::field<uint32_t>(t_ui.data(),0x58)=0;t_readback=false;poll();CHECK(t_errors==1&&!t_saved);
    reset();CHECK(editor_native_save::save_as(error));CHECK(t_detour(t_data.data()));skg::field<uint32_t>(t_ui.data(),0x58)=0;t_truncate=true;poll();CHECK(t_errors==1&&!t_saved);CHECK(t_closed==1&&t_released==1);
    reset();CHECK(editor_native_save::save_as(error));CHECK(t_detour(t_play.data()));poll();CHECK(!t_saved&&editor_native_save::busy());t_mode_pointer=nullptr;poll();CHECK(t_cancelled==1&&!t_errors&&!editor_native_save::busy());
    reset();CHECK(editor_native_save::save_as(error));editor_native_save::tick();editor_native_save::shutdown();drain();CHECK(!t_saved&&!t_cancelled&&!t_errors);
    reset();skg::field<int>(t_mode.data(),0xD0)=1;CHECK(!editor_native_save::playtest(true,error));skg::field<int>(t_mode.data(),0xD0)=0;CHECK(editor_native_save::playtest(true,error));CHECK(skg::field<int>(t_mode.data(),0xCC)==2);CHECK(editor_native_save::playtest(false,error));CHECK(t_order==std::vector<int>({1,2,3}));CHECK(skg::field<int>(t_mode.data(),0xCC)==1);
    reset();t_owned_preview=true;CHECK(editor_native_save::playtest(true,error));t_return_detour(t_mode.data());t_return_detour(t_mode.data());CHECK(t_return_requests==1);CHECK(skg::field<int>(t_mode.data(),0xCC)==2);CHECK(editor_native_save::playtest(false,error));CHECK(skg::field<int>(t_mode.data(),0xCC)==1);CHECK(t_order==std::vector<int>({1,2,3}));
    // Ordinary commands pass through; shared hotkeys cannot exit or replace the
    // draft, or enter raw preview outside the coordinator's readiness barrier.
    reset();for(int mode=0;mode<5;++mode)t_command_detour(t_ui.data(),mode);CHECK(t_command_calls==5);
    t_shared=true;for(int mode=0;mode<5;++mode)t_command_detour(t_ui.data(),mode);CHECK(t_command_calls==5);
    t_display_detour(t_ui.data(),1);CHECK(t_display_calls==0&&skg::field<int>(t_mode.data(),0xCC)==1);
    CHECK(editor_native_save::save_as(error));CHECK(!t_replaced);CHECK(t_detour(t_data.data()));
    skg::field<uint32_t>(t_ui.data(),0x58)=0;poll();CHECK(t_saved==1&&!editor_native_save::busy());
    CHECK(editor_native_save::playtest(true,error));CHECK(t_display_calls==1);CHECK(editor_native_save::playtest(false,error));
    reset();t_shared=true;skg::field<int>(t_ui.data(),0x1C)=-1;t_display_detour(t_ui.data(),0);CHECK(t_display_calls==1);
    skg::field<int>(t_ui.data(),0x1C)=0;t_display_detour(t_ui.data(),0);CHECK(t_display_calls==2);
    // +20 is the return display, which can legitimately remain -1 or name a
    // previous display. Active Build/Play/Terraform is +1C, not that return slot.
    skg::field<int>(t_ui.data(),0x20)=1;t_display_detour(t_ui.data(),0);CHECK(t_display_calls==3);
    skg::field<int>(t_ui.data(),0x1C)=1;skg::field<int>(t_ui.data(),0x20)=0;t_display_detour(t_ui.data(),0);CHECK(t_display_calls==3);
    skg::field<int>(t_ui.data(),0x1C)=2;t_display_detour(t_ui.data(),0);CHECK(t_display_calls==3);
    reset();std::vector<uint8_t> saved_bytes{0x37};CHECK(editor_native_save::read_saved(t_before,saved_bytes,error));
    CHECK(saved_bytes==std::vector<uint8_t>(512,0xA5));CHECK(t_closed==1&&t_released==1);
    saved_bytes={0x37};t_readback=false;CHECK(!editor_native_save::read_saved(t_before,saved_bytes,error));CHECK(saved_bytes==std::vector<uint8_t>{0x37});
    reset();t_editor_active=false;CHECK(!editor_native_save::save_as(error));CHECK(!editor_native_save::playtest(true,error));
    CHECK(!t_native_calls&&!t_saved&&skg::field<Key>(t_resource.data(),8)==t_before);
    reset();CHECK(editor_native_save::save_as(error));t_editor_active=false;poll();CHECK(t_cancelled==1&&!t_errors&&!editor_native_save::busy());
    reset();CHECK(editor_native_save::playtest(true,error));t_editor_active=false;CHECK(!editor_native_save::playtest(false,error));CHECK(t_order.empty());
    std::printf("editor native save: %u checks passed\n",checks);
}
