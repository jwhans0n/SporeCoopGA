// Production native adapter, actual i686 ABI, controlled exact-layout objects.
// This proves hook routing and ownership; it is not a running-game result.
#include <windows.h>
static SHORT native_fixture_escape_state=0;
static SHORT WINAPI native_fixture_key_state(int key){return key==VK_ESCAPE?native_fixture_escape_state:0;}
// Native input policy is tested with exact controlled keyboard values. A real
// user's Escape press must not nondeterministically cancel a headless double.
#define GetKeyState native_fixture_key_state
#include "../mods/coop/editor_native.cpp"
#undef GetKeyState
#include <cstdlib>
#include <cstdio>

static unsigned checks;
// A failed check may leave scoped native doubles alive. Do not run unrelated
// static destructors (whose double registry can already be destroyed) on that
// failure path; flush the assertion and let the OS retire this isolated process.
#define CHECK(x) do {++checks;if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::fflush(stdout);std::_Exit(1);}} while(0)
namespace {
using namespace editor_native;
std::array<uint8_t,0xE0> t_mode;
std::array<uint8_t,0x128> t_data,t_other_data;
std::array<uint8_t,0x2CB0> t_live;
std::array<uint8_t,0x28> t_history,t_context;
std::array<uint8_t,0x2C> t_display;
std::array<uint8_t,0x18> t_layout,t_entry,t_other_entry;
std::array<uint8_t,0x8C> t_container;
std::array<uint8_t,0x371> t_loading;
std::array<void*,8> t_entry_vt{},t_resource_vt{},t_actor_vt{};
std::array<std::array<uint8_t,8>,2> t_actors;
std::array<std::array<uint8_t,0x50>,4> t_models;
std::array<void*,17> t_model_vt;
std::array<uint8_t,0x58> t_teleporter;
std::array<uint8_t,0x28> t_ghost_actor;
std::array<uint8_t,0x28> t_ghost_exit;
std::array<uint8_t,0x58> t_ghost_parent_interface,t_ghost_exit_interface;
bool t_ghost_linked=false,t_ghost_fail=false;
unsigned t_marker_constructions=0,t_marker_destructions=0;
unsigned t_ghosts_created=0,t_ghosts_destroyed=0;
bool t_inspector_visible=false;
unsigned t_inspector_opens=0;
bool SK_THISCALL t_layout_visible(void*){return t_inspector_visible;}
void SK_THISCALL t_inspector_open(void* settings,uint32_t index){CHECK(index==7);skg::field<uint32_t>(settings,0x20)=index;t_inspector_visible=true;++t_inspector_opens;}
std::map<void*,std::vector<uint8_t>> t_rows;
std::set<void*> t_allocations;
std::deque<std::pair<SkTaskFn,void*>> t_tasks;
void* t_mode_pointer=t_mode.data();
void* t_context_pointer=t_context.data();
void* t_loading_pointer=t_loading.data();
std::array<uint8_t,16> t_tool;
void* t_tool_pointer=t_tool.data();
bool t_active=true,t_restore_fail=false,t_callback_fail=false,t_alloc_fail=false;
uint64_t t_frame=10;
uint32_t t_game_mode_id=modes::kScenario;
double t_time=1;
unsigned t_presses=0,t_finishes=0,t_ticks=0,t_steps=0;
bool t_granted=false;
bool t_audio_drift=false;
bool t_retired_actor=false;
uint32_t t_card_id=0;
std::array<void*,8> t_text_vt{};
void* t_text=t_text_vt.data();
uint32_t t_text_id=0;
unsigned t_card_calls=0;
unsigned t_palette_calls=0;
editor::Revision t_revision=0,t_canonical_revision=0;
unsigned t_restores=0,t_original_undo=0,t_own_undo=0,t_settled=0,t_failed=0;
unsigned t_object_lookups=0,t_writes=0,t_reads=0,t_transforms=0;
GestureScope t_scope;
CancellationTicket t_seen;
std::string t_failure;

void rows_for(void* r){
    auto& rows=t_rows[r];
    skg::field<void*>(r,0x2C10)=rows.data();
    skg::field<void*>(r,0x2C14)=rows.data()+rows.size();
}
float& value(void* r,unsigned index){return skg::field<float>(t_rows[r].data()+index*0x238+4,0x20);}
int SK_THISCALL t_addref(void* r){return ++skg::field<int>(r,4);}
int SK_THISCALL t_release(void* r){
    int count=--skg::field<int>(r,4);
    CHECK(count>=0);
    if(!count&&t_allocations.erase(r)){t_rows.erase(r);std::free(r);}
    return count;
}
void* SK_THISCALL t_cast(void* actor,uint32_t type){
    if(type==0x0175CDC9 && t_ghost_linked){
        if(actor==t_ghost_actor.data())return t_ghost_parent_interface.data();
        if(actor==t_ghost_exit.data())return t_ghost_exit_interface.data();
    }
    if(type==0x0175CDC9 && actor==t_actors[0].data())return t_teleporter.data();
    if(type!=0x01186577)return nullptr;
    for(size_t i=0;i<2;++i)if(actor==t_actors[i].data())return t_models[i].data();
    return actor==t_ghost_actor.data()?t_models[2].data():actor==t_ghost_exit.data()?t_models[3].data():nullptr;
}
void* SK_THISCALL t_marker_ctor(void* marker){++t_marker_constructions;skg::field<void*>(marker,0x28)=static_cast<uint8_t*>(marker)+0x40;return marker;}
void SK_THISCALL t_marker_dtor(void* marker){
    CHECK(skg::field<void*>(t_ghost_actor.data(),0x18)!=marker && skg::field<void*>(t_ghost_exit.data(),0x18)!=marker);++t_marker_destructions;
}
void SK_THISCALL t_marker_resize(void* acts,uint32_t count){CHECK(count>0&&count<=8);skg::field<uint8_t*>(acts,4)=skg::field<uint8_t*>(acts,0)+count*0x34;}
void SK_THISCALL t_assign(void** slot,void* value){if(value)t_addref(value);auto* old=*slot;*slot=value;if(old)t_release(old);}
void SK_THISCALL t_model_position(void* model,const editor::Vec3* p){skg::field<editor::Vec3>(model,4)=*p;}
void SK_THISCALL t_model_rotation(void* model,const editor::Rotation* q){skg::field<editor::Rotation>(model,0x10)=*q;}
void SK_THISCALL t_model_scale(void* model,float scale){skg::field<float>(model,0x20)=scale;}
void* SK_CDECL t_preview_create(const editor::ResourceKey*,const editor::Vec3* p,const editor::Rotation* q,float scale,bool avatar,uint32_t asset,void* marker,void* acts,uint32_t act){
    CHECK(!avatar&&asset==7&&marker&&acts&&act==0);++t_ghosts_created;
    CHECK(skg::field<uint32_t>(marker,0)==asset&&skg::field<void*>(marker,0x28)==acts);
    if(t_ghost_fail)return nullptr;
    t_ghost_actor.fill(0);skg::field<void*>(t_ghost_actor.data(),0)=t_actor_vt.data();skg::field<int>(t_ghost_actor.data(),4)=1;
    skg::field<void*>(t_ghost_actor.data(),0x18)=marker;
    if(t_ghost_linked){
        t_ghost_exit.fill(0);skg::field<void*>(t_ghost_exit.data(),0)=t_actor_vt.data();
        skg::field<int>(t_ghost_exit.data(),4)=2;skg::field<int>(t_ghost_actor.data(),4)=2;
        skg::field<void*>(t_ghost_exit.data(),0x18)=marker;
        skg::field<void*>(t_ghost_parent_interface.data(),0x54)=t_ghost_exit.data();
        skg::field<void*>(t_ghost_exit_interface.data(),0x50)=t_ghost_actor.data();
    }
    t_model_position(t_models[2].data(),p);t_model_rotation(t_models[2].data(),q);t_model_scale(t_models[2].data(),scale);return t_ghost_actor.data();
}
void* SK_CDECL t_allocate(size_t size,const char*,int,int,int,int){
    CHECK(size==0x2CB0);if(t_alloc_fail)return nullptr;
    void* out=std::calloc(1,size);t_allocations.insert(out);return out;
}
void* SK_THISCALL t_ctor(void* r){skg::field<void*>(r,0)=t_resource_vt.data();return r;}
void* SK_THISCALL t_copy(void* dst,const void* src){
    void* vt=skg::field<void*>(dst,0);int refs=skg::field<int>(dst,4);
    std::memcpy(dst,src,0x2CB0);skg::field<void*>(dst,0)=vt;skg::field<int>(dst,4)=refs;
    const auto begin=skg::field<const uint8_t*>(const_cast<void*>(src),0x2C10),end=skg::field<const uint8_t*>(const_cast<void*>(src),0x2C14);
    t_rows[dst].assign(begin,end);rows_for(dst);return dst;
}
void* SK_THISCALL t_object(void* owner,void* marker){
    ++t_object_lookups;
    CHECK(owner==t_data.data());
    auto& rows=t_rows[t_live.data()];
    for(unsigned i=0;i<2;++i)if(marker==rows.data()+i*0x238+4)return t_actors[i].data();
    return nullptr;
}
bool SK_THISCALL t_write(void*,Stream* stream){
    ++t_writes;const uint8_t byte=0xA5;return write(stream,&byte,1);
}
bool SK_THISCALL t_no_read(void*,Stream*){++t_reads;return false;}
void SK_THISCALL t_transform(void* owner,void* marker,bool shared){
    CHECK(owner==t_data.data()&&!shared&&applying);
    CHECK(marker==t_rows[t_live.data()].data()+(kPlacementLimit-1)*0x238+4);
    ++t_transforms;
}
void* SK_THISCALL t_marker(void* owner,void* actor){
    CHECK(owner==t_data.data());
    if(t_retired_actor)return nullptr;
    for(unsigned i=0;i<2;++i)if(actor==t_actors[i].data())return t_rows[t_live.data()].data()+i*0x238+4;
    return nullptr;
}
void noisy_audio(void* marker){if(t_audio_drift){skg::field<float>(marker,0x1E0)+=.00000238f;skg::field<float>(marker,0x1E4)+=.00000119f;}}
void SK_THISCALL t_select(void* tools,void* actor){CHECK(tools==t_tool_pointer);skg::field<void*>(t_context.data(),4)=actor;if(actor)noisy_audio(t_marker(t_data.data(),actor));}
void SK_THISCALL t_start(void* owner){
    CHECK(owner==t_data.data());skg::field<void*>(t_entry.data(),0)=t_entry_vt.data();
    skg::field<void*>(t_entry.data(),0x0C)=owner;
    skg::field<void*>(t_history.data(),0x24)=t_entry.data();skg::field<uint8_t>(t_history.data(),0x20)=0;
}
void SK_THISCALL t_undo(void*){++t_original_undo;}
void SK_THISCALL t_restore(void* entry,void* before){
    CHECK(entry==t_entry.data());CHECK(skg::field<void*>(entry,0x0C)==t_data.data());
    ++t_restores;t_copy(t_live.data(),before);
    // Native restoration clears the editor selection; the adapter must retain it.
    skg::field<void*>(t_context.data(),4)=nullptr;
}
uint64_t t_get_frame(){return t_frame;}
double t_get_time(){return t_time;}
void t_log(SkMod*,int,const char*,...){}
void SK_THISCALL t_step(void*){++t_steps;value(t_live.data(),0)=99;noisy_audio(t_rows[t_live.data()].data()+4);}
void SK_THISCALL t_apply_audio(void* owner,void* marker,uint32_t){CHECK(owner==t_data.data());noisy_audio(marker);skg::field<float>(marker,0x20)=3.f;}
void SK_THISCALL t_tick(void*){++t_ticks;ground_step(nullptr);}
uint32_t SK_THISCALL t_tool_action(void* manager,uint32_t action,uint32_t,uint32_t){
    CHECK(manager==t_tool_pointer);
    if(action==1){++t_presses;start(t_data.data());}
    else if(!action){++t_finishes;skg::field<uint8_t>(t_history.data(),0x20)=1;}
    return 1;
}
bool SK_THISCALL t_create(void* manager,uint32_t index){CHECK(index==7);t_tool_action(manager,1,0,0);return true;}
void SK_THISCALL t_remove_class(void* owner,uint32_t index){CHECK(owner==t_data.data() && index==7);++t_steps;value(t_live.data(),0)=77;}
bool SK_THISCALL t_card_action(void* self,uint32_t first,void* window,uint32_t third){
    CHECK(self==t_tool_pointer && first==17 && window==&t_card_id && third==23);++t_card_calls;return false;
}
bool SK_THISCALL t_palette_message(void* self,uint32_t message,uint32_t payload){
    CHECK(self==t_layout.data()+8 && message==0x07326686 && payload==7);++t_palette_calls;
    start(t_data.data());remove_class_native(t_data.data(),payload);return false;
}
bool SK_THISCALL t_script_input(void*,void*,void*){CHECK(input_cast);start(t_data.data());value(t_live.data(),0)+=1;commit(t_data.data());return true;}
sui::Win SK_THISCALL t_inspector_root(void* self,uint32_t id,bool recursive){CHECK(self==t_layout.data() && id==0x073E6BA0 && recursive);return &t_card_id;}
uint32_t SK_CDECL t_act(){return 0;}
void SK_THISCALL t_commit(void*){skg::field<uint8_t>(t_history.data(),0x20)=1;}
void SK_CDECL t_speech(){start(t_data.data());value(t_live.data(),0)+=1;commit(t_data.data());}
void t_outside(SkTaskFn task,void* user){t_tasks.emplace_back(task,user);}
void drain(){auto work=std::move(t_tasks);t_tasks.clear();for(auto [task,user]:work)task(user);}
void next_drain(){++t_frame;editor_native::cancellation_queued=true;drain_cancellation(nullptr);}
void native_cancel(void* entry=nullptr){
    undo_native(entry?entry:t_entry.data());
    // Exact F3BF00 caller clears/releases the current entry after hooked Undo.
    skg::field<void*>(t_history.data(),0x24)=nullptr;skg::field<uint8_t>(t_history.data(),0x20)=1;
}
void begin(){start(t_data.data());CHECK(!t_allocations.empty());CHECK(gesture_serial!=0);}
void reset(){
    clear_environment_refresh();environment_refresh_queued=environment_refresh_servicing=false;
    native_fixture_escape_state=0;
    clear_terrain_capture_rotations();
    act_projection_cache.clear();
    clear_terrain_materialization();
    held_terrain_preset.reset();a_copy_terrain_properties=0;
    held_terrain_controls.clear();terrain_before.reset();terrain_pending=terrain_rebuilding=terrain_capturing=terrain_recipe_replacement=false;terrain_available=false;
    deferred_local_view.reset();local_view_queued=local_view_settling=false;
    held_tool.reset();held_import.reset();held_creation.reset();held_class_removal.reset();held_captain_asset.reset();held_crew_action.reset();
    for(auto item:pending){release(item.entry);release(item.before);}pending.clear();
    if(gesture_before_resource){release(gesture_before_resource);gesture_before_resource=nullptr;}
    gesture_before_metadata.reset();gesture_metadata_error.clear();
    for(auto held:gesture_actors)release(held.object);
    gesture_actors.clear();gesture_before_bindings.clear();
    CHECK(t_allocations.empty());t_rows.clear();t_tasks.clear();
    t_mode.fill(0);t_data.fill(0);t_live.fill(0);t_history.fill(0);t_context.fill(0);
    t_display.fill(0);t_layout.fill(0);t_container.fill(0);t_loading.fill(0);
    t_entry.fill(0);t_other_entry.fill(0);t_other_data.fill(0);
    for(auto& actor:t_actors){actor.fill(0);skg::field<void*>(actor.data(),0)=t_actor_vt.data();skg::field<int>(actor.data(),4)=1;}
    t_resource_vt[0]=reinterpret_cast<void*>(t_addref);t_resource_vt[1]=reinterpret_cast<void*>(t_release);
    t_entry_vt=t_resource_vt;skg::field<int>(t_entry.data(),4)=1;
    t_actor_vt=t_resource_vt;
    t_mode_pointer=t_mode.data();t_context_pointer=t_context.data();t_loading_pointer=t_loading.data();
    t_active=true;t_callback_fail=t_alloc_fail=t_restore_fail=false;
    t_audio_drift=false;
    t_retired_actor=false;
    t_frame=10;t_game_mode_id=modes::kScenario;t_time=1;t_presses=t_finishes=t_ticks=t_steps=0;t_granted=false;t_revision=t_canonical_revision=0;
    t_restores=t_original_undo=t_own_undo=t_settled=t_failed=0;t_failure.clear();t_seen={};
    t_scope={};t_scope.project[0]=0xAD;t_scope.epoch=9;t_scope.author={1,3};
    a_mode=uintptr_t(&t_mode_pointer);a_context=uintptr_t(&t_context_pointer);a_loading=uintptr_t(&t_loading_pointer);
    a_tools=uintptr_t(&t_tool_pointer);a_history=uintptr_t(t_history.data());a_entry_vtable=uintptr_t(t_entry_vt.data());
    a_object=uintptr_t(t_object);a_marker=uintptr_t(t_marker);a_copy=uintptr_t(t_copy);a_ctor=uintptr_t(t_ctor);
    a_alloc=uintptr_t(t_allocate);a_restore=uintptr_t(t_restore);a_display_vtable=0;
    a_speech_ui=0;a_act=uintptr_t(t_act);input_speech=false;
    original_start=t_start;original_undo=t_undo;original_select=t_select;
    original_create=t_create;original_commit=t_commit;original_speech_apply=t_speech;original_apply_marker=t_apply_audio;
    original_remove_class=t_remove_class;
    a_tool_tick=uintptr_t(t_tick);original_tool=t_tool_action;original_tick_tools=t_tick;
    original_ground_step=original_vertical_step=original_radial_step=original_auxiliary_step=t_step;
    skg::field<int>(t_mode.data(),0xCC)=1;skg::field<void*>(t_mode.data(),0x74)=t_data.data();
    skg::field<void*>(t_mode.data(),0x14)=t_display.data();skg::field<void*>(t_data.data(),0x10)=t_live.data();
    skg::field<void*>(t_display.data(),0x24)=t_layout.data();skg::field<void*>(t_layout.data(),0x14)=t_container.data();
    skg::field<uint8_t>(t_history.data(),0x20)=1;
    skg::field<void*>(t_live.data(),0)=t_resource_vt.data();skg::field<int>(t_live.data(),4)=1;
    t_rows[t_live.data()].resize(2*0x238);rows_for(t_live.data());value(t_live.data(),0)=1;value(t_live.data(),1)=2;
    skg::field<void*>(t_context.data(),4)=t_actors[0].data();
    supported=true;applying=queued=false;generation=1;observed_mode=observed_data=nullptr;observed_scenario=false;
    cancellation.reset();cancellation_queued=cancellation_settling=false;
    gesture_owner=gesture_entry=nullptr;gesture_generation=gesture_serial=gesture_counter=cancellation_counter=0;gesture_scope_pin={};
    ordinary_cancellations=0;
    bindings.clear();incarnations.clear();selection.clear();pending.clear();act_identity={};
    static SkHost host{};host.frame_index=t_get_frame;host.run_outside_frame=t_outside;
    host.time_seconds=t_get_time;host.log=t_log;H=&host;
    C={};C.active=[](){return t_active;};C.gesture_scope=[](){return t_scope;};
    C.extended_poses_tracked=[] {return false;};C.cast_tracked=[] {return false;};C.authoring_tracked=[]{return false;};
    C.gate=[](const auto&){return t_granted;};
    C.object_revision=[](editor::ObjectId)->std::optional<editor::Revision>{return t_revision;};
    C.canonical_revision=[]()->std::optional<editor::Revision>{return t_canonical_revision;};
    C.history=[](bool redo){CHECK(!redo);++t_own_undo;};C.failure=[](const std::string& error){++t_failed;t_failure=error;};
    C.cancelled=[](const CancellationTicket& ticket,std::string& error){
        CHECK(state().ready);CHECK(!state().gesture);CHECK(ticket.scope==t_scope);CHECK(ticket.generation==state().generation);
        ++t_settled;t_seen=ticket;
        if(t_callback_fail){error="controlled latest canonical restoration failure";return false;}
        // Runtime callback supplies CURRENT canonical state, including a peer's
        // disjoint commit since Start. This value is not taken from before.
        value(t_live.data(),1)=42;
        return true;
    };
    CHECK(state().ready);refresh_selection();CHECK(selection.size()==1);
}
void test_pending_press(){
    reset();
    CHECK(tool_native(t_tool_pointer,1,17,23)==1);
    CHECK(held_tool&&t_presses==1&&t_ticks==1&&!t_steps&&state().gesture);
    CHECK(value(t_live.data(),0)==1);
    tick_tools(t_tool_pointer);ground_step(nullptr);vertical_step(nullptr);radial_step(nullptr);auxiliary_step(nullptr);
    CHECK(t_ticks==1&&!t_steps);
    // The complete quick drag, including release, survives its lease latency.
    CHECK(tool_native(t_tool_pointer,0,0,0)==1&&held_tool->released&&!t_finishes);
    continue_input(nullptr);CHECK(held_tool&&!t_steps&&!t_finishes);
    t_granted=true;continue_input(nullptr);
    CHECK(!held_tool&&t_presses==1&&t_ticks==2&&t_steps==1&&t_finishes==1&&!state().gesture);
    CHECK(value(t_live.data(),0)==99);
    for(unsigned kind=0;kind<9;++kind){
        reset();tool_native(t_tool_pointer,1,0,0);CHECK(held_tool&&!t_steps);
        if(kind==0)++t_revision;
        if(kind==1)++t_scope.epoch;
        if(kind==2)++t_scope.author.generation;
        if(kind==3)++generation;
        if(kind==4)t_time=4;
        if(kind==5)held_tool->focused=true; // No owning game window in this fixture.
        if(kind==6)skg::field<uintptr_t>(t_context.data(),0x10)=1;
        if(kind==7){deny_input({{editor::ScopeKind::Placement,held_tool->object,{}}});}
        if(kind==8){select_native(t_tool_pointer,t_actors[1].data());}
        t_granted=true;continue_input(nullptr);
        CHECK(!held_tool&&!t_steps&&value(t_live.data(),0)==1);
        CHECK(t_finishes==(kind==3?0u:1u));
    }
    reset();
}
unsigned crew_calls=0;editor::ResourceKey crew_key;int32_t crew_index;bool crew_value;
int32_t SK_THISCALL t_crew_add(void*,const editor::ResourceKey* key){++crew_calls;crew_key=*key;return 0;}
void SK_THISCALL t_crew_replace(void*,int32_t index,const editor::ResourceKey* key){++crew_calls;crew_index=index;crew_key=*key;}
void SK_THISCALL t_crew_index(void*,int32_t index){++crew_calls;crew_index=index;}
void SK_THISCALL t_crew_lock(void*,int32_t index,bool value){++crew_calls;crew_index=index;crew_value=value;}
void test_crew_input_ownership(){
    for(unsigned action=0;action<5;++action){
        reset();crew_calls=0;crew_key={};crew_index=-1;crew_value=false;
        original_crew_add=t_crew_add;original_crew_replace=t_crew_replace;original_crew_remove=original_crew_capacity=t_crew_index;original_crew_lock=t_crew_lock;
        C.authoring_tracked=[]{return true;};C.settings_gate=[](editor::ObjectId id){CHECK(id==kCrewObject);return t_granted;};
        editor::ResourceKey key{17,19,23};
        switch(action){
        case 0:CHECK(crew_add(t_data.data(),&key)==-1);break;
        case 1:crew_replace(t_data.data(),1,&key);break;
        case 2:crew_remove(t_data.data(),1);break;
        case 3:crew_lock(t_data.data(),1,true);break;
        case 4:crew_capacity(t_data.data(),2);break;
        }
        CHECK(!crew_calls&&held_crew_action);key={};continue_input(nullptr);CHECK(!crew_calls&&held_crew_action);
        t_granted=true;continue_input(nullptr);CHECK(crew_calls==1&&!held_crew_action);
        if(action<2)CHECK((crew_key==editor::ResourceKey{17,19,23}));
        if(action>0)CHECK(crew_index==(action==4?2:1));
        if(action==3)CHECK(crew_value);
    }
    for(unsigned reason=0;reason<6;++reason){
        reset();crew_calls=0;C.authoring_tracked=[]{return true;};C.settings_gate=[](editor::ObjectId){return t_granted;};
        original_crew_add=t_crew_add;const editor::ResourceKey key{17,19,23};crew_add(t_data.data(),&key);CHECK(held_crew_action);
        if(reason==0)++t_revision;if(reason==1)++t_scope.epoch;if(reason==2)++generation;
        if(reason==3)t_time+=3;if(reason==4)++t_scope.author.generation;
        if(reason==5)deny_input({{editor::ScopeKind::Settings,kCrewObject,{}}});
        t_granted=true;continue_input(nullptr);CHECK(!crew_calls&&!held_crew_action);
    }
    reset();
}
std::array<uint8_t,0x1CC> t_terraform;
unsigned terrain_values_called=0,terrain_starts=0,terrain_commits=0,terrain_rebuilds=0,terrain_gradients=0;
unsigned terrain_builds=0;
uint32_t terrain_raw=0;std::array<uint32_t,3> terrain_pointed{};bool terrain_expect_pointer=false;
std::vector<float> terrain_heights;std::vector<editor::Vec3> terrain_colors;
void SK_THISCALL t_terrain_start(void* self){CHECK(self==t_terraform.data());++terrain_starts;skg::field<uint8_t>(t_history.data(),0x20)=0;}
void SK_THISCALL t_terrain_commit(void* self){CHECK(self==t_terraform.data());++terrain_commits;skg::field<uint8_t>(t_history.data(),0x20)=1;}
void SK_THISCALL t_terrain_rebuild(void* self){CHECK(self==t_terraform.data()&&applying);++terrain_rebuilds;}
void SK_THISCALL t_terrain_build(void* self,void*){CHECK(self==t_terraform.data()&&applying);++terrain_builds;}
void SK_THISCALL t_terrain_value(void* self,uint32_t raw){
    CHECK(self==t_terraform.data()&&t_granted);++terrain_values_called;terrain_raw=raw;
    if(terrain_expect_pointer)std::memcpy(terrain_pointed.data(),reinterpret_cast<void*>(uintptr_t(raw)),12);
}
void SK_THISCALL t_terrain_gradient(void* self,void* heights,void* colors){
    CHECK(self==t_terraform.data()&&t_granted);++terrain_gradients;
    CHECK(copy_terrain_vector(heights,terrain_heights)&&copy_terrain_vector(colors,terrain_colors));
}
void setup_terrain_retry(){
    reset();t_terraform.fill(0);skg::field<void*>(t_mode.data(),0x18)=t_terraform.data();terrain_available=true;
    C.terrain_gate=[](const auto& objects,const auto& fields){CHECK(objects.empty());CHECK(fields.empty()||fields.size()<=7);return t_granted;};
    C.notice=[](const std::string& value){t_failure=value;};
    original_terrain_start=t_terrain_start;original_terrain_commit=t_terrain_commit;original_terrain_rebuild=t_terrain_rebuild;original_terrain_build=t_terrain_build;
    original_terrain_values.fill(t_terrain_value);original_terrain_gradient=t_terrain_gradient;
    terrain_values_called=terrain_starts=terrain_commits=terrain_rebuilds=terrain_gradients=0;
    terrain_builds=0;
    terrain_pointed={};terrain_raw=0;terrain_expect_pointer=false;terrain_heights.clear();terrain_colors.clear();
    auto& row=act_identity.settings[editor_native_terrain::kPlanetObject];row.object=editor_native_terrain::kPlanetObject;row.kind=editor::SettingsKind::Planet;
    row.properties[0x968C496A]=false;
}
void pin_terrain_before(){
    terrain_before.emplace();terrain_before->document=act_identity;terrain_scope=t_scope;terrain_generation=generation;terrain_owner=t_data.data();
}
void test_terrain_control_retry(){
    // All eighteen setter ABIs retain values, including pointer arguments, and
    // execute one native history item only after their field grant arrives.
    for(unsigned index=0;index<18;++index){
        setup_terrain_retry();std::array<uint32_t,3> pointed{0x11,0x22,0x33};
        terrain_expect_pointer=index<4||index==17;
        hold_terrain_value(index,terrain_expect_pointer?uint32_t(uintptr_t(pointed.data())):index+101);
        pointed={};CHECK(held_terrain_controls.size()==1);
        continue_terrain_control();CHECK(!terrain_values_called&&!terrain_starts);
        t_granted=true;pin_terrain_before();continue_terrain_control();
        CHECK(held_terrain_controls.empty()&&terrain_values_called==1&&terrain_starts==1&&terrain_commits==1&&terrain_rebuilds==1&&terrain_pending);
        if(terrain_expect_pointer)CHECK((terrain_pointed==std::array<uint32_t,3>{0x11,0x22,0x33}));else CHECK(terrain_raw==index+101);
    }
    // A quick native setter, coalesced slider values, and an unrelated accepted
    // field do not require another click or invalidate the desired value.
    setup_terrain_retry();terrain_value<4>(t_terraform.data(),123);terrain_value<4>(t_terraform.data(),456);
    CHECK(held_terrain_controls.size()==1&&!terrain_values_called);
    act_identity.settings.at(editor_native_terrain::kPlanetObject).properties[0x8E390196]=true;
    t_granted=true;pin_terrain_before();continue_terrain_control();CHECK(terrain_raw==456&&terrain_values_called==1);
    setup_terrain_retry();terrain_value<4>(t_terraform.data(),123);CHECK(held_terrain_controls.size()==1);
    t_granted=true;pin_terrain_before();original_terrain_start(t_terraform.data());terrain_value<4>(t_terraform.data(),789);
    CHECK(held_terrain_controls.empty()&&terrain_values_called==1&&terrain_raw==789);
    terrain_commit(t_terraform.data());CHECK(terrain_starts==1&&terrain_commits==1);
    // Per-field conflict, epoch, generation, act, focus, timeout and explicit
    // denial all retire the desired value without touching native state.
    for(unsigned reason=0;reason<10;++reason){
        setup_terrain_retry();hold_terrain_value(0,0); // Null pointer is rejected.
        CHECK(held_terrain_controls.empty());std::array<uint32_t,3> pointed{1,2,3};hold_terrain_value(0,uint32_t(uintptr_t(pointed.data())));
        if(reason==0)act_identity.settings.at(editor_native_terrain::kPlanetObject).properties[0x968C496A]=true;
        if(reason==1)++t_scope.epoch;if(reason==2)++generation;
        if(reason==3)skg::field<uint32_t>(t_context.data(),0)=1;
        if(reason==4)held_terrain_controls.front().focused=true;
        if(reason==5)t_time+=3;
        if(reason==6)deny_input({{editor::ScopeKind::Field,editor_native_terrain::kPlanetObject,{editor::Field::Property,0x968C496A}}});
        if(reason==7)++act_identity.settings.at(editor_native_terrain::kPlanetObject).field_revisions[{editor::Field::Property,0x968C496A}];
        if(reason==8)deny_input({{editor::ScopeKind::Authoring,{},{}}});
        if(reason==9)skg::field<void*>(t_mode.data(),0x74)=t_other_data.data();
        t_granted=true;continue_terrain_control();CHECK(held_terrain_controls.empty()&&!terrain_values_called&&!terrain_starts);
        if(reason==0||reason==6||reason==7||reason==8)CHECK(!t_failure.empty());
    }
    setup_terrain_retry();
    std::array<float,2> heights{.2f,.8f};std::array<editor::Vec3,2> colors{{{1,0,0},{0,1,0}}};
    struct Source{void* begin;void* end;void* capacity;};Source h{heights.data(),heights.data()+2,heights.data()+2},c{colors.data(),colors.data()+2,colors.data()+2};
    terrain_gradient(t_terraform.data(),&h,&c);CHECK(held_terrain_controls.size()==1&&!terrain_gradients);
    heights={};colors={};t_granted=true;pin_terrain_before();continue_terrain_control();
    CHECK(terrain_gradients==1&&terrain_starts==1&&terrain_commits==1&&terrain_heights==std::vector<float>({.2f,.8f}));
    CHECK((terrain_colors==std::vector<editor::Vec3>{{1,0,0},{0,1,0}}));
    setup_terrain_retry();h.end=reinterpret_cast<void*>(uintptr_t(h.begin)+3);terrain_gradient(t_terraform.data(),&h,&c);
    CHECK(held_terrain_controls.empty()&&!terrain_gradients);reset();
}
void test_preset_history_boundary(){
    for(bool existing:{false,true}){
        setup_terrain_retry();C.terrain_gate=[](const auto& records,const auto& fields){CHECK(records.empty());CHECK(fields.empty()||fields.size()==editor_native_terrain::planet_properties().size());return t_granted;};
        int properties=0;terrain_build(t_terraform.data(),&properties);
        CHECK(!terrain_recipe_replacement&&!terrain_builds&&!terrain_starts&&!terrain_commits);
        t_granted=true;pin_terrain_before();if(existing)original_terrain_start(t_terraform.data());
        terrain_build(t_terraform.data(),&properties);
        CHECK(terrain_recipe_replacement&&terrain_builds==1&&terrain_starts==1);
        CHECK(terrain_commits==(existing?0u:1u));
        if(existing)terrain_commit(t_terraform.data());
        CHECK(terrain_commits==1&&terrain_pending);
    }
    setup_terrain_retry();int properties=0;applying=true;terrain_build(t_terraform.data(),&properties);applying=false;
    CHECK(terrain_builds==1&&!terrain_starts&&!terrain_commits&&!terrain_recipe_replacement);reset();
}
unsigned terrain_initializations=0,terrain_initial_loads=0,terrain_initial_models=0,terrain_initial_effects=0;
bool terrain_initial_default=false;
void SK_THISCALL t_initial_terrain_load(void* self,void*){
    CHECK(self==t_terraform.data()&&applying);++terrain_initial_loads;
}
void SK_THISCALL t_initial_terrain_value(void* self,uint32_t){
    CHECK(self==t_terraform.data()&&applying&&!t_granted);++terrain_values_called;
}
void SK_THISCALL t_initial_terrain_model(void* self,const editor::ResourceKey*,const void*){
    CHECK(self==t_terraform.data()&&applying&&!t_granted);++terrain_initial_models;
}
void SK_THISCALL t_initial_terrain_effect(void* self,uint32_t,uint32_t,const void*,uint32_t,uint32_t,uint32_t){
    CHECK(self==t_terraform.data()&&applying&&!t_granted);++terrain_initial_effects;
}
void SK_THISCALL t_initial_terrain_build(void* self,void* properties){
    CHECK(applying);++terrain_builds;terrain_load(self,properties);
    // Native initialization reaches the same setters and append paths as UI
    // authoring. None may request a lease or queue a deferred user action.
    terrain_value<0>(self,0);terrain_value<1>(self,0);terrain_value<2>(self,0);terrain_value<3>(self,0);
    terrain_value<4>(self,0);terrain_value<5>(self,0);terrain_value<6>(self,0);terrain_value<7>(self,0);
    terrain_value<8>(self,0);terrain_value<9>(self,0);terrain_value<10>(self,0);terrain_value<11>(self,0);
    terrain_value<12>(self,0);terrain_value<13>(self,0);terrain_value<14>(self,0);
    terrain_value<15>(self,0);terrain_value<16>(self,0);terrain_value<17>(self,0);
    terrain_append_model(self,nullptr,nullptr);terrain_append_effect(self,0,0,nullptr,0,0,0);
}
void SK_THISCALL t_initial_terrain_resource(void* resource){
    CHECK(resource==t_live.data()&&applying);++terrain_initializations;
    terrain_build(t_terraform.data(),static_cast<uint8_t*>(resource)+0x18);
}
void SK_THISCALL t_initial_terrain_activate(void* self,const editor::ResourceKey*){
    CHECK(self==t_terraform.data()&&applying);++terrain_initializations;
    if(terrain_initial_default){int properties=0;terrain_build(self,&properties);}
    else terrain_resource_init(t_live.data());
    CHECK(applying); // A nested resource boundary retains the outer scope.
}
void test_cold_native_terrain_initialization(){
    for(unsigned route=0;route<3;++route)for(bool already_applying:{false,true}){
        setup_terrain_retry();CHECK(active()&&editing()&&!t_granted);
        // Joining has a live editor resource but cannot issue authoring leases.
        // The old Build guard rejected this exact state before native grounding.
        C.terrain_gate=[](const auto&,const auto&){CHECK(false);return false;};
        C.gesture_scope=[]{CHECK(false);return GestureScope{};};
        original_terrain_load=t_initial_terrain_load;original_terrain_build=t_initial_terrain_build;
        original_terrain_values.fill(t_initial_terrain_value);
        original_terrain_append_model=t_initial_terrain_model;original_terrain_append_effect=t_initial_terrain_effect;
        original_terrain_resource_init=t_initial_terrain_resource;original_terrain_activate=t_initial_terrain_activate;
        terrain_initializations=terrain_initial_loads=terrain_initial_models=terrain_initial_effects=0;
        terrain_initial_default=route==2;applying=already_applying;
        if(route==0)terrain_resource_init(t_live.data());else terrain_activate(t_terraform.data(),nullptr);
        CHECK(applying==already_applying&&terrain_initial_loads==1&&terrain_builds==1&&terrain_values_called==18);
        CHECK(terrain_initial_models==1&&terrain_initial_effects==1&&terrain_initializations==(route==1?2u:1u));
        CHECK(!terrain_before&&!terrain_pending&&!terrain_recipe_replacement&&held_terrain_controls.empty());
        CHECK(pending.empty()&&!gesture_before_resource&&!terrain_starts&&!terrain_commits);
        CHECK(!t_restores&&!t_failed&&t_allocations.empty());applying=false;
    }
    // Ordinary external presets still pass the existing ownership/history gate.
    test_preset_history_boundary();reset();
}
std::array<uint8_t,16> t_terrain_descriptor;
std::array<uint8_t,0x69> t_terrain_definition;
void* SK_THISCALL t_terrain_selected(void* self,void* actor){
    CHECK(self==t_terraform.data());return actor==t_actors[0].data()?t_terrain_descriptor.data():nullptr;
}
uint32_t SK_THISCALL t_terrain_grab(void* manager,uint32_t action,uint32_t,uint32_t){
    CHECK(manager==t_tool_pointer);
    if(action==1){++t_presses;terrain_start(t_terraform.data());}
    else if(!action){++t_finishes;terrain_commit(t_terraform.data());}
    return 1;
}
void setup_terrain_grab(){
    setup_terrain_retry();t_terrain_descriptor.fill(0);t_terrain_definition.fill(0);
    skg::field<uint32_t>(t_display.data(),0x1C)=2;
    skg::field<void*>(t_terrain_descriptor.data(),0)=t_actors[0].data();
    skg::field<void*>(t_terrain_descriptor.data(),4)=t_terrain_definition.data();
    editor::TerrainRecord row;row.object={55,1};row.kind=editor::TerrainKind::Effect;row.order=1;row.revision=t_revision;
    act_identity.terrain.emplace(row.object,row);pin_terrain_before();
    a_terrain_selection=uintptr_t(t_terrain_selected);original_tool=t_terrain_grab;
    C.terrain_gate=[](const auto& records,const auto& fields){CHECK(fields.empty());CHECK(records.empty()||records==std::vector<editor::ObjectId>({{55,1}}));return t_granted;};
}
void test_terrain_pending_grab(){
    for(bool quick_release:{false,true}){
        setup_terrain_grab();CHECK(state().terraforming&&state().ready&&!state().gesture);
        CHECK(tool_native(t_tool_pointer,1,17,23)==1&&held_tool&&held_tool->terrain);
        CHECK(t_presses==1&&t_ticks==1&&!t_steps&&terrain_starts==1&&!terrain_commits);
        tick_tools(t_tool_pointer);ground_step(nullptr);vertical_step(nullptr);radial_step(nullptr);auxiliary_step(nullptr);
        CHECK(t_ticks==1&&!t_steps&&value(t_live.data(),0)==1);
        if(quick_release)CHECK(tool_native(t_tool_pointer,0,0,0)==1&&held_tool->released);
        continue_input(nullptr);CHECK(held_tool&&!t_steps);
        t_granted=true;continue_input(nullptr);CHECK(!held_tool&&t_presses==1&&t_steps==1&&t_ticks==2);
        if(!quick_release)tool_native(t_tool_pointer,0,0,0);
        CHECK(t_finishes==1&&terrain_starts==1&&terrain_commits==1&&terrain_pending);
    }
    for(unsigned reason=0;reason<9;++reason){
        setup_terrain_grab();tool_native(t_tool_pointer,1,0,0);CHECK(held_tool);
        if(reason==0)++t_revision;if(reason==1)++t_scope.epoch;if(reason==2)++generation;
        if(reason==3)++skg::field<uint32_t>(t_context.data(),0);if(reason==4)skg::field<void*>(t_context.data(),4)=t_actors[1].data();
        if(reason==5)skg::field<uintptr_t>(t_context.data(),0x10)=123;
        if(reason==6)t_time+=3;if(reason==7)held_tool->focused=true;
        if(reason==8)deny_input({{editor::ScopeKind::Authoring,{},{}}});
        continue_input(nullptr);CHECK(!held_tool&&!t_steps&&value(t_live.data(),0)==1);
    }
    reset();
}
std::map<void*,std::vector<uint8_t>> t_preset_payloads;
unsigned t_preset_copies=0;
void* SK_THISCALL t_copy_preset_properties(void* destination,const void* source){
    ++t_preset_copies;editor_native_terrain::Properties p;p.header={0xF9,0xD4,0x23,0xB5,17,0,0,0};
    p.fields[0x8E390196]={1,0,{*static_cast<const uint8_t*>(source)}};std::string error;
    CHECK(editor_native_terrain::encode(p,t_preset_payloads[static_cast<uint8_t*>(destination)-0x18],error));return destination;
}
bool SK_THISCALL t_write_preset_resource(void* self,Stream* stream){
    const auto& bytes=t_preset_payloads.at(self);return write(stream,bytes.data(),uint32_t(bytes.size()));
}
uint32_t SK_THISCALL t_read_preset_resource(void* self,Stream* stream){
    t_preset_payloads[self]=stream->bytes;stream->position=uint32_t(stream->bytes.size());return stream->position;
}
void SK_THISCALL t_build_preset_resource(void* self,void* properties){
    CHECK(self==t_terraform.data()&&applying&&t_granted);++terrain_builds;
    editor_native_terrain::Properties p;std::string error;
    CHECK(editor_native_terrain::decode(t_preset_payloads.at(static_cast<uint8_t*>(properties)-0x18),p,error));
    CHECK(p.fields.at(0x8E390196).value==std::vector<uint8_t>{1});
}
void setup_preset_retry(){
    setup_terrain_retry();t_preset_payloads.clear();t_preset_copies=0;
    a_copy_terrain_properties=uintptr_t(t_copy_preset_properties);a_write=uintptr_t(t_write_preset_resource);a_read=uintptr_t(t_read_preset_resource);
    original_terrain_build=t_build_preset_resource;
    C.terrain_gate=[](const auto& records,const auto& fields){CHECK(records.empty());CHECK(fields.empty()||fields.size()==editor_native_terrain::planet_properties().size());return t_granted;};
}
void test_terrain_preset_retry(){
    setup_preset_retry();std::array<uint8_t,0x38> source{};source[0]=1;
    terrain_build(t_terraform.data(),source.data());CHECK(held_terrain_preset&&t_preset_copies==1&&!terrain_builds&&!terrain_starts&&!terrain_commits);
    source[0]=0;continue_input(nullptr);CHECK(held_terrain_preset&&!terrain_builds);
    t_granted=true;pin_terrain_before();continue_input(nullptr);
    CHECK(!held_terrain_preset&&terrain_builds==1&&terrain_starts==1&&terrain_commits==1&&terrain_pending&&t_allocations.empty());
    for(unsigned reason=0;reason<8;++reason){
        setup_preset_retry();source[0]=1;terrain_build(t_terraform.data(),source.data());CHECK(held_terrain_preset);
        if(reason==0)++t_scope.epoch;if(reason==1)++generation;if(reason==2)t_time+=3;
        if(reason==3)held_terrain_preset->focused=true;
        if(reason==4)++act_identity.settings.at(editor_native_terrain::kPlanetObject).revision;
        if(reason==5)act_identity.terrain.emplace(editor::ObjectId{55,1},editor::TerrainRecord{});
        if(reason==6)deny_input({{editor::ScopeKind::Authoring,{},{}}});
        if(reason==7)++held_terrain_preset->act;
        t_granted=true;continue_input(nullptr);CHECK(!held_terrain_preset&&!terrain_builds&&!terrain_starts&&t_allocations.empty());
    }
    setup_preset_retry();source[0]=2;terrain_build(t_terraform.data(),source.data());CHECK(!held_terrain_preset&&!terrain_builds&&!t_failure.empty());
    setup_preset_retry();source[0]=1;t_alloc_fail=true;terrain_build(t_terraform.data(),source.data());CHECK(!held_terrain_preset&&!terrain_builds&&t_allocations.empty());
    t_alloc_fail=false;t_preset_payloads.clear();reset();
}
void test_pending_capture_fences(){
    reset();pending.emplace_back();CHECK(state().gesture&&!state().ready);
    C.gate=[](const auto&){return true;};
    CHECK(tool_native(t_tool_pointer,1,0,0)==1&&!t_presses&&!held_tool);
    start(t_data.data());CHECK(t_allocations.empty()&&skg::field<uint8_t>(t_history.data(),0x20));
    std::string error;Snapshot empty;LocalView view;
    CHECK(!restore(empty,error)&&!t_restores);CHECK(!rebaseline(error));CHECK(!capture_view(view,error));
    editor_native::drain(nullptr);CHECK(pending.empty()&&state().ready&&!state().gesture);
    pending.emplace_back();pending.back().speech=t_context.data();
    CHECK(state().ready&&state().gesture); // The same native speech dialog coalesces.
    editor_native::drain(nullptr);CHECK(pending.empty()&&state().ready&&!state().gesture);
    reset();
}
std::vector<uint8_t> t_fingerprint_rows,t_fingerprint_acts;
unsigned t_fingerprint_copies=0,t_fingerprint_writes=0;
void* SK_THISCALL t_copy_fingerprint_rows(void* destination,const uintptr_t* source){
    CHECK(source[2]==source[1]&&source[1]-source[0]==0x27E8);
    ++t_fingerprint_copies;
    t_fingerprint_rows.assign(reinterpret_cast<const uint8_t*>(source[0]),reinterpret_cast<const uint8_t*>(source[1]));
    const auto* acts=skg::field<const uint8_t*>(t_fingerprint_rows.data(),0x78);
    const auto* end=skg::field<const uint8_t*>(t_fingerprint_rows.data(),0x7C);
    CHECK(end-acts==0x4E0);t_fingerprint_acts.assign(acts,end);
    skg::field<void*>(t_fingerprint_rows.data(),0x78)=t_fingerprint_acts.data();
    skg::field<void*>(t_fingerprint_rows.data(),0x7C)=t_fingerprint_acts.data()+t_fingerprint_acts.size();
    skg::field<void*>(t_fingerprint_rows.data(),0x80)=t_fingerprint_acts.data()+t_fingerprint_acts.size();
    skg::field<void*>(destination,0)=t_fingerprint_rows.data();
    skg::field<void*>(destination,4)=t_fingerprint_rows.data()+t_fingerprint_rows.size();
    return destination;
}
bool SK_THISCALL t_write_fingerprint(void* self,Stream* stream){
    ++t_fingerprint_writes;
    CHECK(skg::field<void*>(self,0x2BF4)==t_fingerprint_rows.data());
    CHECK(skg::field<float>(self,0x2C48)==1.f&&skg::field<uint64_t>(self,0x68)==0);
    auto stable=t_fingerprint_rows;std::memset(stable.data()+0x78,0,12);
    return write(stream,&skg::field<uint32_t>(self,0x2C0C),4)&&
        write(stream,stable.data(),uint32_t(stable.size()))&&
        write(stream,t_fingerprint_acts.data(),uint32_t(t_fingerprint_acts.size()));
}
void test_full_roster_fingerprint(){
    reset();const auto prior_copy=a_copy_class_rows,prior_write=a_write;
    a_copy_class_rows=uintptr_t(t_copy_fingerprint_rows);a_write=uintptr_t(t_write_fingerprint);
    std::array<uint8_t,0x27E8> row{};std::array<uint8_t,0x4E0> acts{};
    skg::field<uint32_t>(row.data(),0)=7;skg::field<editor::ResourceKey>(row.data(),8)={1,2,3};
    skg::field<void*>(row.data(),0x78)=acts.data();skg::field<void*>(row.data(),0x7C)=acts.data()+acts.size();
    skg::field<void*>(row.data(),0x80)=acts.data()+acts.size();
    skg::field<uint32_t>(t_live.data(),0x2C0C)=8;
    Rows rows{row.data(),row.data()+row.size()};std::string error;
    t_fingerprint_copies=t_fingerprint_writes=0;
    editor_native_cache::Key first,again,changed;
    CHECK(class_roster_fingerprint(t_live.data(),rows,first,error));
    CHECK(t_fingerprint_copies==1&&t_fingerprint_writes==1&&t_allocations.empty());
    CHECK((first.assets.size()==3&&first.assets.front()==editor::ResourceKey{1,2,3}));
    CHECK(class_roster_fingerprint(t_live.data(),rows,again,error)&&again==first);
    // A nested in-place edit has the same source addresses and native IDs.
    acts[0x4B0]=1;CHECK(class_roster_fingerprint(t_live.data(),rows,changed,error)&&changed.bytes!=first.bytes);
    acts[0x4B0]=0;changed={};row[0x600]=0x5A;
    CHECK(class_roster_fingerprint(t_live.data(),rows,changed,error)&&changed.bytes!=first.bytes);
    row[0x600]=0;changed={};++skg::field<uint32_t>(t_live.data(),0x2C0C);
    CHECK(class_roster_fingerprint(t_live.data(),rows,changed,error)&&changed.bytes!=first.bytes);
    t_alloc_fail=true;changed={};CHECK(!class_roster_fingerprint(t_live.data(),rows,changed,error)&&t_allocations.empty());
    t_alloc_fail=false;t_fingerprint_rows.clear();t_fingerprint_acts.clear();a_copy_class_rows=prior_copy;a_write=prior_write;reset();
}
void test_exact_class_projection_cache(){
    editor_native_cache::ClassesCache cache;
    editor_native_cache::Key key;key.context=cache.context();key.source={1,2,3};key.project[0]=7;key.epoch=4;
    key.anchor={12,1};key.acts=2;key.next_class=8;key.authoring=true;key.act_order={{12,1},{13,1}};
    key.assets={{1,2,3}};
    key.schema=cast_property_schema();key.bytes={1,2,3,4};
    editor_native_cache::Classes classes;editor::ClassActSettings row;row.object={55,1};row.class_id={7,1};row.act_id=key.anchor;
    editor::StructuredValue block;block.format=kCastBlockFormat;block.bytes={0xAA,0xBB};block.references={{8,1}};
    row.properties[kCastBlockProperty]=block;row.properties[editor::kClassAssetResourceProperty]=editor::ResourceKey{1,2,3};
    classes.emplace(row.object,row);CHECK(!cache.get(key));cache.put(key,classes);
    auto found=cache.get(key);CHECK(found&&*found==classes&&cache.stats().entries==1&&cache.stats().bytes>key.bytes.size());
    classes.begin()->second.properties[kCastBlockProperty]=editor::StructuredValue{};
    CHECK(std::get<editor::StructuredValue>(found->at(row.object).properties.at(kCastBlockProperty))==block);
    for(unsigned change=0;change<13;++change){
        auto different=key;
        if(change==0)++different.context;if(change==1)++different.source.instance;if(change==2)++different.project[0];
        if(change==3)++different.epoch;if(change==4)++different.anchor.id;if(change==5)++different.acts;
        if(change==6)++different.next_class;if(change==7)different.authoring=false;
        if(change==8)std::reverse(different.act_order.begin(),different.act_order.end());
        if(change==9)different.schema.clear();if(change==10)different.bytes.back()^=1;if(change==11)different.bytes.push_back(0);
        if(change==12)++different.assets.front().instance;
        CHECK(!cache.get(different));
    }
    CHECK(cache.get(key)==found);const auto old_context=key.context;cache.clear();
    CHECK(cache.context()!=old_context&&!cache.get(key)&&!cache.stats().entries&&!cache.stats().bytes);
    cache.put(key,*found);CHECK(!cache.stats().entries); // A worker with the retired context cannot publish a cache entry.
    key.context=cache.context();cache.put(key,*found);CHECK(cache.get(key)&&cache.stats().entries==1);
    // Keep at most eight exact versions, evicting the least recently used.
    for(unsigned i=0;i<12;++i){auto version=key;version.bytes.push_back(uint8_t(i));cache.put(std::move(version),*found);}
    CHECK(cache.stats().entries==8&&cache.stats().bytes<=32u<<20);CHECK(!cache.get(key));
    auto oversized=key;oversized.bytes.resize((32u<<20)+1);const auto entries=cache.stats().entries;
    cache.put(std::move(oversized),*found);CHECK(cache.stats().entries==entries);
    CHECK(std::get<editor::StructuredValue>(found->at(row.object).properties.at(kCastBlockProperty))==block);
}
std::array<uint8_t,0x174> t_flora_ui;
std::array<uint8_t,0x1C> t_terraform_ui;
std::array<void*,13> t_flora_slider_vtable;
void* t_flora_slider=t_flora_slider_vtable.data();
void* t_flora_found=nullptr;void* t_flora_root=nullptr;
unsigned t_flora_calls=0;int32_t t_flora_value=0,t_flora_maximum=100;
unsigned t_flora_set_calls=0;bool t_flora_notify=true;
editor::ResourceKey t_flora_key;
void* SK_THISCALL t_flora_cast(void* self,uint32_t type){CHECK(self==&t_flora_slider&&type==0x0F00A8A0);return self;}
int32_t SK_THISCALL t_flora_get(void*){return t_flora_value;}
int32_t SK_THISCALL t_flora_max(void*){return t_flora_maximum;}
// Verified native ISlider::SetValue consumes TWO stack arguments (ret8).
// A queued replay must suppress its automatic event before the explicit one.
void SK_THISCALL t_flora_set(void* self,int32_t value,bool notify){CHECK(self==&t_flora_slider);++t_flora_set_calls;t_flora_value=value;t_flora_notify=notify;}
void SK_THISCALL t_flora_remove(void* self,void* card){CHECK(self==t_flora_ui.data()&&card==&t_card_id&&t_granted);++t_flora_calls;}
void SK_THISCALL t_flora_add(void* self,void* card,const editor::ResourceKey* key){t_flora_remove(self,card);t_flora_key=*key;}
bool SK_THISCALL t_flora_input(void* self,void* window,void* raw){
    CHECK(self==t_flora_ui.data()+8&&window==&t_flora_slider&&t_granted&&applying);
    const auto* message=static_cast<sui::Msg*>(raw);CHECK(message->source==window&&uint32_t(message->type)==0xEF00A884&&message->a==0x072BFDFD);
    ++t_flora_calls;return true;
}
void setup_flora_retry(){
    setup_terrain_retry();t_flora_ui.fill(0);t_terraform_ui.fill(0);t_flora_calls=0;t_flora_key={};t_flora_set_calls=0;t_flora_notify=true;
    a_terraform_ui_vtable=0x11223344;a_flora_ui_vtable=0x55667788;
    skg::field<uintptr_t>(t_terraform_ui.data(),0)=a_terraform_ui_vtable;
    skg::field<void*>(t_terraform_ui.data(),0x18)=t_flora_ui.data();
    skg::field<void*>(t_display.data(),0x28)=t_terraform_ui.data();
    skg::field<uintptr_t>(t_flora_ui.data(),0)=a_flora_ui_vtable;
    t_flora_root=&t_flora_key;skg::field<void*>(t_flora_ui.data(),0x168)=t_flora_root;
    t_flora_found=&t_card_id;t_card_id=0x072C2823;
    original_flora_remove=t_flora_remove;original_flora_add=t_flora_add;original_flora_input=t_flora_input;
    t_flora_slider_vtable.fill(nullptr);t_flora_slider_vtable[3]=(void*)t_flora_cast;
    t_flora_slider_vtable[0x1C/4]=(void*)t_flora_set;t_flora_slider_vtable[0x20/4]=(void*)t_flora_get;t_flora_slider_vtable[0x30/4]=(void*)t_flora_max;
}
void test_flora_control_retry(){
    setup_flora_retry();editor::ResourceKey key{7,11,13};flora_add(t_flora_ui.data(),&t_card_id,&key);key={};
    CHECK(held_terrain_controls.size()==1&&!t_flora_calls);t_granted=true;continue_terrain_control();
    CHECK(held_terrain_controls.empty()&&t_flora_calls==1&&(t_flora_key==editor::ResourceKey{7,11,13}));
    setup_flora_retry();flora_remove(t_flora_ui.data(),&t_card_id);CHECK(held_terrain_controls.size()==1&&!t_flora_calls);
    t_granted=true;continue_terrain_control();CHECK(t_flora_calls==1&&held_terrain_controls.empty());
    for(unsigned reason=0;reason<4;++reason){
        setup_flora_retry();flora_remove(t_flora_ui.data(),&t_card_id);CHECK(held_terrain_controls.size()==1);
        if(reason==0)skg::field<void*>(t_flora_ui.data(),0x168)=&t_card_id;
        if(reason==1)t_flora_found=nullptr;
        if(reason==2)skg::field<void*>(t_terraform_ui.data(),0x18)=nullptr;
        if(reason==3)skg::field<uintptr_t>(t_flora_ui.data(),0)=0;
        t_granted=true;continue_terrain_control();CHECK(held_terrain_controls.empty()&&!t_flora_calls&&!t_failure.empty());
    }
    setup_flora_retry();t_flora_found=&t_flora_slider;t_flora_value=72;t_flora_maximum=100;
    sui::Msg message{&t_flora_slider,0,int32_t(0xEF00A884),0x072BFDFD,0,0,0};
    CHECK(!flora_input(t_flora_ui.data()+8,&t_flora_slider,&message));CHECK(held_terrain_controls.size()==1&&!t_flora_calls);
    message={};t_flora_value=0;t_granted=true;pin_terrain_before();continue_terrain_control();
    CHECK(held_terrain_controls.empty()&&t_flora_calls==1&&t_flora_value==72&&terrain_starts==1&&terrain_commits==1);
    CHECK(t_flora_set_calls==1&&!t_flora_notify);
    for(const int32_t density:{0,100}){
        setup_flora_retry();t_flora_found=&t_flora_slider;t_flora_value=density;t_flora_maximum=100;
        sui::Msg changed{&t_flora_slider,0,int32_t(0xEF00A884),0x072BFDFD,0,0,0};
        CHECK(!flora_input(t_flora_ui.data()+8,&t_flora_slider,&changed));
        CHECK(held_terrain_controls.size()==1&&!t_flora_set_calls&&!t_flora_calls);
        t_flora_value=50;t_granted=true;pin_terrain_before();continue_terrain_control();
        CHECK(t_flora_set_calls==1&&!t_flora_notify&&t_flora_value==density);
        CHECK(t_flora_calls==1&&terrain_starts==1&&terrain_commits==1);
    }
    for(unsigned reason=0;reason<4;++reason){
        setup_flora_retry();t_flora_found=&t_flora_slider;t_flora_value=50;t_flora_maximum=100;
        sui::Msg changed{&t_flora_slider,0,int32_t(0xEF00A884),0x072BFDFD,0,0,0};
        CHECK(!flora_input(t_flora_ui.data()+8,&t_flora_slider,&changed));
        if(reason==0)skg::field<void*>(t_flora_ui.data(),0x168)=&t_card_id;
        if(reason==1)t_flora_found=nullptr;
        if(reason==2)skg::field<void*>(t_terraform_ui.data(),0x18)=nullptr;
        if(reason==3)t_flora_maximum=0;
        t_granted=true;continue_terrain_control();
        CHECK(held_terrain_controls.empty()&&!t_flora_set_calls&&!t_flora_calls&&!terrain_starts&&!terrain_commits);
    }
    t_flora_found=t_flora_root=nullptr;reset();
}
void test_render_only_collaboration(){
    reset();a_preview_factory=0;
    t_actor_vt[3]=(void*)t_cast;
    t_model_vt.fill(nullptr);t_model_vt[0x38/4]=(void*)t_model_position;t_model_vt[0x3C/4]=(void*)t_model_rotation;t_model_vt[0x40/4]=(void*)t_model_scale;
    for(auto& model:t_models){model.fill(0);skg::field<void*>(model.data(),0)=t_model_vt.data();}
    t_teleporter.fill(0);skg::field<void*>(t_teleporter.data(),0x54)=t_actors[1].data();
    auto* marker=t_rows[t_live.data()].data()+4;
    skg::field<editor::Rotation>(marker,0x10)={};skg::field<editor::Rotation>(marker,0x1F4)={};
    skg::field<editor::Vec3>(marker,0x1E8)={3,4,5};skg::field<float>(marker,0x204)=2;
    bindings[0]={42,1};
    const auto authored=t_rows[t_live.data()];
    LivePreview hint;hint.author={1,9};hint.object={42,1};hint.act={3ull<<32,1};hint.class_id={(2ull<<32)|7,1};
    hint.pose={{10,20,30},{0,0,.6f,.8f},3};hint.destination=editor::Pose{{40,50,60},{.6f,0,0,.8f},4};
    update_previews({hint});
    CHECK(skg::field<editor::Vec3>(t_models[0].data(),4)==hint.pose.position);
    CHECK(skg::field<editor::Vec3>(t_models[1].data(),4)==hint.destination->position);
    CHECK(t_rows[t_live.data()]==authored && !skg::field<bool>(t_data.data(),0x84));
    CHECK(visual_status().find("exit=40,50,60")!=std::string::npos);
    hint.pose.position.x=15;update_previews({hint});CHECK(skg::field<float>(t_models[0].data(),4)==15);
    update_previews({});
    CHECK(skg::field<editor::Vec3>(t_models[0].data(),4)==skg::field<editor::Vec3>(marker,4));
    CHECK(skg::field<editor::Vec3>(t_models[1].data(),4)==skg::field<editor::Vec3>(marker,0x1E8));
    CHECK(t_rows[t_live.data()]==authored);
    std::vector<uint8_t> classes(0x27E8);skg::field<uint32_t>(classes.data(),0)=7;
    skg::field<editor::ResourceKey>(classes.data(),8)={9,7,3};
    skg::field<void*>(t_live.data(),0x2BF4)=classes.data();skg::field<void*>(t_live.data(),0x2BF8)=classes.data()+classes.size();
    t_ghosts_created=t_ghosts_destroyed=0;a_preview_factory=uintptr_t(t_preview_create);
    a_marker_ctor=uintptr_t(t_marker_ctor);a_marker_dtor=uintptr_t(t_marker_dtor);
    a_resize_marker_acts=uintptr_t(t_marker_resize);a_object_reference=uintptr_t(t_assign);
    std::array<uint8_t,0x534> acts{};skg::field<void*>(t_live.data(),0x23C)=acts.data();skg::field<void*>(t_live.data(),0x240)=acts.data()+acts.size();
    hint.creating=true;hint.object={999,1};update_previews({hint});
    CHECK(t_ghosts_created==1 && preview_actors.size()==1 && t_rows[t_live.data()]==authored);
    hint.pose.position.x=19;update_previews({hint});CHECK(t_ghosts_created==1 && skg::field<float>(t_models[2].data(),4)==19);
    CHECK(visual_status().find("ghost=1:9")!=std::string::npos);
    update_previews({});CHECK(preview_actors.empty()&&t_ghosts_destroyed==1&&t_rows[t_live.data()]==authored);
    hint.author.generation=10;update_previews({hint});CHECK(t_ghosts_created==2);
    t_active=false;update_previews({});CHECK(preview_actors.empty()&&t_ghosts_destroyed==2);
    t_active=true;t_ghost_linked=true;update_previews({hint});
    CHECK(preview_actors.at(hint.author).secondary==t_ghost_exit.data());
    CHECK(skg::field<editor::Vec3>(t_models[3].data(),4)==hint.destination->position);
    update_previews({});CHECK(t_ghosts_destroyed==4 && !skg::field<int>(t_ghost_actor.data(),4) && !skg::field<int>(t_ghost_exit.data(),4));
    CHECK(!skg::field<void*>(t_ghost_parent_interface.data(),0x54) && !skg::field<void*>(t_ghost_exit_interface.data(),0x50));
    t_ghost_linked=false;t_ghost_fail=true;update_previews({hint});CHECK(!preview_actors.at(hint.author).object&&!preview_actors.at(hint.author).marker);
    t_ghost_fail=false;update_previews({hint});CHECK(preview_actors.at(hint.author).object);update_previews({});
    CHECK(t_marker_constructions==t_marker_destructions && t_rows[t_live.data()]==authored);
    a_preview_factory=0;t_actor_vt[3]=nullptr;reset();
}
void test_independent_inspector_survives_scene_refresh(){
    reset();
    std::array<uint8_t,0x1C> script{};
    std::array<uint8_t,0x30> settings{};
    std::vector<uint8_t> classes(0x27E8);
    skg::field<void*>(t_display.data(),0x18)=script.data();skg::field<void*>(script.data(),0x18)=settings.data();
    skg::field<void*>(settings.data(),0x10)=t_layout.data();skg::field<uint32_t>(settings.data(),0x20)=7;
    skg::field<uint32_t>(classes.data(),0)=7;skg::field<void*>(t_live.data(),0x2BF4)=classes.data();skg::field<void*>(t_live.data(),0x2BF8)=classes.data()+classes.size();
    a_inspector_open=uintptr_t(t_inspector_open);a_layout_shown=uintptr_t(t_layout_visible);
    t_inspector_visible=true;t_inspector_opens=0;
    {InspectorView browsing;t_inspector_visible=false;}
    CHECK(t_inspector_visible&&t_inspector_opens==1);
    {InspectorView browsing;}
    CHECK(t_inspector_visible&&t_inspector_opens==1); // No toggling an already visible independent inspector.
    {InspectorView browsing;t_inspector_visible=false;skg::field<uint32_t>(classes.data(),0)=8;}
    CHECK(!t_inspector_visible&&t_inspector_opens==1); // The inspected asset was removed.
    a_inspector_open=a_layout_shown=0;reset();
}
void test_pending_palette_drag(){
    reset();C.structure_gate=[](uint32_t index){CHECK(index==7);return t_granted;};
    CHECK(!create_native(t_tool_pointer,7));CHECK(held_creation&&!state().gesture&&!t_presses&&!t_steps);
    tool_native(t_tool_pointer,0,0,0);CHECK(held_creation->released);
    continue_input(nullptr);CHECK(held_creation&&!t_presses&&!t_steps);
    t_granted=true;continue_input(nullptr);
    CHECK(!held_creation&&t_presses==1&&t_ticks==1&&t_steps==1&&t_finishes==1&&!state().gesture);
    for(unsigned kind=0;kind<6;++kind){
        reset();C.structure_gate=[](uint32_t){return t_granted;};CHECK(!create_native(t_tool_pointer,7)&&held_creation);
        if(kind==0)++t_scope.epoch;
        if(kind==1)++t_revision;
        if(kind==2)t_active=false;
        if(kind==3)t_time=4;
        if(kind==4)held_creation->focused=true;
        if(kind==5)deny_input({{editor::ScopeKind::Document,{},{}}});
        t_granted=true;continue_input(nullptr);CHECK(!held_creation&&!t_presses&&!t_steps&&!state().gesture);
    }
}
void test_pending_class_removal(){
    reset();C.cast_tracked=[]{return true;};C.remove_class_gate=[](uint32_t index){CHECK(index==7);return t_granted;};
    remove_class_native(t_data.data(),7);CHECK(held_class_removal && !t_steps && value(t_live.data(),0)==1);
    continue_input(nullptr);CHECK(held_class_removal && !t_steps);
    t_granted=true;continue_input(nullptr);
    CHECK(!held_class_removal && t_steps==1 && value(t_live.data(),0)==77 && pending.size()==1);
    for(unsigned kind=0;kind<6;++kind){
        reset();C.cast_tracked=[]{return true;};C.remove_class_gate=[](uint32_t){return t_granted;};
        remove_class_native(t_data.data(),7);CHECK(held_class_removal && !t_steps);
        if(kind==0)++t_scope.epoch;
        if(kind==1)++t_scope.author.generation;
        if(kind==2)++t_revision;
        if(kind==3)++generation;
        if(kind==4)t_time=4;
        if(kind==5)deny_input({{editor::ScopeKind::Document,{},{}}});
        t_granted=true;continue_input(nullptr);
        CHECK(!held_class_removal && !t_steps && value(t_live.data(),0)==1);
    }
}
void test_palette_removal_waits_before_ui_and_history(){
    reset();skg::field<void*>(t_display.data(),0x14)=t_layout.data();
    original_palette_message=t_palette_message;t_palette_calls=0;
    C.cast_tracked=[]{return true;};C.remove_class_gate=[](uint32_t index){CHECK(index==7);return t_granted;};
    CHECK(!palette_message(t_layout.data()+8,0x07326686,7));
    CHECK(held_class_removal && held_class_removal->listener==t_layout.data()+8 && !t_palette_calls && !gesture_entry && pending.empty());
    continue_input(nullptr);CHECK(!t_palette_calls && !t_steps);
    t_granted=true;continue_input(nullptr);
    CHECK(!held_class_removal && t_palette_calls==1 && t_steps==1 && pending.size()==1);
    reset();skg::field<void*>(t_display.data(),0x14)=t_layout.data();
    C.cast_tracked=[]{return true;};C.remove_class_gate=[](uint32_t){return false;};
    t_palette_calls=0;palette_message(t_layout.data()+8,0x07326686,7);CHECK(held_class_removal);
    // A rebuilt palette cannot receive the old deferred notification.
    skg::field<void*>(t_display.data(),0x14)=t_context.data();t_granted=true;continue_input(nullptr);
    CHECK(!held_class_removal && !t_palette_calls && !t_steps && pending.empty());
}
void test_coalesced_speech(){
    reset();std::array<uint8_t,0x27E8> classes{};std::array<uint8_t,8> ui{};void* ui_pointer=ui.data();
    skg::field<uint32_t>(classes.data(),0)=7;
    skg::field<void*>(t_live.data(),0x2BF4)=classes.data();skg::field<void*>(t_live.data(),0x2BF8)=classes.data()+classes.size();
    skg::field<void*>(ui.data(),4)=classes.data()+8;a_speech_ui=uintptr_t(&ui_pointer);
    C.cast_tracked=[]{return true;};C.class_gate=[](uint32_t index){CHECK(index==7);return t_granted;};
    speech_apply();CHECK(pending.empty()&&value(t_live.data(),0)==1&&!state().gesture);
    t_granted=true;
    for(unsigned i=0;i<128;++i){
        skg::field<uint32_t>(t_data.data(),0x7C)=i+1;
        speech_apply();CHECK(pending.size()==1&&t_allocations.size()==1&&state().gesture);
        CHECK(value(pending.front().before,0)==1&&pending.front().speech==ui.data());
        CHECK(pending.front().before_metadata&&pending.front().before_metadata->genre==1);
        CHECK(pending.front().after_metadata&&pending.front().after_metadata->genre==i+1);
    }
    CHECK(value(t_live.data(),0)==129&&!t_failed);
    editor_native::drain(nullptr);CHECK(pending.size()==1&&state().gesture);
    // A delayed text finalizer may never apply to a replaced logical epoch.
    ++t_scope.epoch;editor_native::drain(nullptr);
    CHECK(pending.empty()&&t_allocations.empty()&&!state().gesture&&!t_failed);
}
void test_pose_refresh_preserves_authored_audio(){
    reset();C.extended_poses_tracked=[]{return true;};t_audio_drift=true;
    auto* marker=t_rows[t_live.data()].data()+4;
    skg::field<float>(marker,0x1E0)=1.f;skg::field<float>(marker,0x1E4)=.375f;
    for(unsigned i=0;i<64;++i){
        apply_marker_state(t_data.data(),marker,0);
        select_native(t_tool_pointer,t_actors[0].data());ground_step(nullptr);
        CHECK(skg::field<float>(marker,0x1E0)==1.f&&skg::field<float>(marker,0x1E4)==.375f);
        CHECK(value(t_live.data(),0)==99.f); // The pose still applied.
    }
    // A real parameter setter supplies its new value before transform refresh.
    skg::field<float>(marker,0x1E0)=1.25f;apply_marker_state(t_data.data(),marker,0);
    CHECK(skg::field<float>(marker,0x1E0)==1.25f&&skg::field<float>(marker,0x1E4)==.375f);
    t_active=false;apply_marker_state(t_data.data(),marker,0);
    CHECK(skg::field<float>(marker,0x1E0)!=1.25f);
}
void test_card_action_scope(){
    reset();original_card_action=t_card_action;t_card_calls=0;
    for(auto id:{0x073272B0u}){
        t_card_id=id;CHECK(card_action(t_tool_pointer,17,&t_card_id,23));
        CHECK(!t_card_calls && !t_failed && !state().gesture);
    }
    t_card_id=0x07464588;CHECK(!card_action(t_tool_pointer,17,&t_card_id,23));CHECK(t_card_calls==1);
    t_card_id=0x073D54A0;CHECK(!card_action(t_tool_pointer,17,&t_card_id,23));CHECK(t_card_calls==2);
    t_active=false;t_card_id=0x073272B0;CHECK(!card_action(t_tool_pointer,17,&t_card_id,23));CHECK(t_card_calls==3);
}
void test_cast_actor_rebuild_keeps_marker_identity(){
    for(unsigned changed=0;changed<2;++changed){
        reset();begin();const auto before=bindings;
        t_retired_actor=true;
        if(changed)skg::field<uint32_t>(t_rows[t_live.data()].data()+4,0)=7;
        commit(t_data.data());CHECK(pending.size()==1);
        if(changed)CHECK(pending.front().after_bindings.at(0)!=before.at(0));
        else CHECK(pending.front().after_bindings==before);
    }
}
void test_script_cast_callback_keeps_bindings(){
    reset();std::array<uint8_t,0x24> script{},settings{};std::array<uint8_t,0x10> message{};
    skg::field<void*>(t_display.data(),0x18)=script.data();skg::field<void*>(script.data(),0x18)=settings.data();
    skg::field<void*>(settings.data(),0x10)=t_layout.data();skg::field<uint32_t>(settings.data(),0x20)=7;
    a_layout_find=uintptr_t(t_inspector_root);original_script_input=t_script_input;
    C.cast_tracked=[]{return true;};C.class_gate=[](uint32_t index){CHECK(index==7);return t_granted;};
    C.authoring_tracked=[]{return true;};C.settings_gate=[](editor::ObjectId){CHECK(false);return true;};
    skg::field<uint32_t>(message.data(),8)=sui::kMsgComponentActivated;
    CHECK(!script_input(script.data()+8,&t_card_id,message.data()));CHECK(pending.empty() && !input_cast);
    t_granted=true;CHECK(script_input(script.data()+8,&t_card_id,message.data()));
    CHECK(gesture_cast && !input_cast && pending.size()==1 && value(t_live.data(),0)==2);
}
unsigned t_authoring_inputs=0,t_settings_checks=0;
bool SK_THISCALL t_authoring_input(void*,void*,void*){++t_authoring_inputs;return true;}
void test_captain_and_act_input_ownership(){
    reset();t_authoring_inputs=t_settings_checks=0;
    C.authoring_tracked=[]{return true;};
    original_captain_input=original_script_input=t_authoring_input;
    C.settings_gate=[](editor::ObjectId object){++t_settings_checks;CHECK(object==kCaptainObject);return t_granted;};
    std::array<uint8_t,0x1C> message{};
    skg::field<uint32_t>(message.data(),8)=sui::kMsgComponentActivated;
    skg::field<uint32_t>(message.data(),0xC)=0x0770B540;
    CHECK(!captain_input(t_tool_pointer,&t_card_id,message.data()));
    CHECK(t_settings_checks==1&&!t_authoring_inputs);
    t_granted=true;CHECK(captain_input(t_tool_pointer,&t_card_id,message.data()));
    CHECK(t_settings_checks==2&&t_authoring_inputs==1);
    C.authoring_tracked=[]{return false;};
    CHECK(!captain_input(t_tool_pointer,&t_card_id,message.data())&&t_authoring_inputs==1);
    C.authoring_tracked=[]{return true;};
    std::array<uint8_t,0x24> script{};skg::field<void*>(t_display.data(),0x18)=script.data();
    C.settings_gate=[](editor::ObjectId object){++t_settings_checks;CHECK(object==act_settings_id(0));return t_granted;};
    skg::field<uint32_t>(message.data(),8)=3; // No authored EE4290 branch.
    CHECK(script_input(script.data()+8,&t_card_id,message.data()));
    CHECK(t_settings_checks==2&&t_authoring_inputs==2);
    skg::field<uint32_t>(message.data(),8)=sui::kMsgComponentActivated;
    skg::field<uint32_t>(message.data(),0xC)=0x0743B8E3;
    CHECK(script_input(script.data()+8,&t_card_id,message.data()));
    CHECK(t_settings_checks==2&&t_authoring_inputs==3);
    skg::field<uint32_t>(message.data(),0xC)=0x07462210;
    t_granted=false;CHECK(!script_input(script.data()+8,&t_card_id,message.data()));
    CHECK(t_settings_checks==3&&t_authoring_inputs==3);
    t_granted=true;CHECK(script_input(script.data()+8,&t_card_id,message.data()));
    CHECK(t_settings_checks==4&&t_authoring_inputs==4);
    skg::field<uint32_t>(message.data(),0xC)=0x07BD2DD0;
    CHECK(!script_input(script.data()+8,&t_card_id,message.data())&&t_authoring_inputs==4);
    int roster_action=-1;C.act_action=[&](int value){roster_action=value;};
    CHECK(!script_input(script.data()+8,&t_card_id,message.data())&&roster_action==0);
    skg::field<uint32_t>(message.data(),0xC)=0x0743BC38;
    CHECK(!script_input(script.data()+8,&t_card_id,message.data())&&roster_action==1);
    editor::Document reordered;const editor::ObjectId stable{91,2},settings{92,3};
    reordered.act_order={stable};editor::AuthoringSettings row;row.object=settings;row.act_id=stable;reordered.settings[settings]=row;
    bind_acts(reordered);C.settings_gate=[&](editor::ObjectId value){CHECK(value==settings);return true;};
    skg::field<uint32_t>(message.data(),0xC)=0x07462210;
    CHECK(script_input(script.data()+8,&t_card_id,message.data())&&t_authoring_inputs==5);
    C.settings_gate=[](editor::ObjectId){CHECK(false);return false;};
    for(auto type:{3u,8u,0x1Bu}){
        skg::field<uint32_t>(message.data(),8)=type;
        CHECK(script_input(script.data()+8,&t_card_id,message.data()));
        CHECK(captain_input(t_tool_pointer,&t_card_id,message.data()));
    }
    unsigned text_checks=0;C.settings_gate=[&](editor::ObjectId value){CHECK(value==settings);++text_checks;return t_granted;};
    t_text_vt[7]=reinterpret_cast<void*>(t_authoring_input);
    for(auto id:{0x0743B978u,0x0710A140u,0x05CA82B7u,0x07CCBD48u,0x07DF4C98u}){
        t_text_id=id;skg::field<void*>(message.data(),0x18)=&t_text;
        skg::field<void*>(message.data(),0)=&t_text;
        skg::field<uint32_t>(message.data(),0xC)=1;
        for(auto type:{0x1Bu,0x1Cu}){
            skg::field<uint32_t>(message.data(),8)=type;const auto calls=text_checks;
            CHECK(script_input(script.data()+8,&t_card_id,message.data())&&text_checks==calls);
        }
        skg::field<uint32_t>(message.data(),0xC)=0;
        t_granted=false;skg::field<uint32_t>(message.data(),8)=0x1B;
        const auto calls=text_checks;CHECK(script_input(script.data()+8,&t_card_id,message.data()));
        CHECK(text_checks==calls+1); // Focus reserves without writing authored data.
        skg::field<uint32_t>(message.data(),8)=3;
        CHECK(script_input(script.data()+8,&t_card_id,message.data())&&text_checks==calls+2);
        skg::field<uint32_t>(message.data(),8)=0x1C;
        const auto inputs=t_authoring_inputs;
        CHECK(!script_input(script.data()+8,&t_card_id,message.data())&&t_authoring_inputs==inputs);
        t_granted=true;CHECK(script_input(script.data()+8,&t_card_id,message.data()));
        CHECK(t_authoring_inputs==inputs+1);
    }
}
unsigned t_captain_assets=0;
void SK_THISCALL t_set_captain(void* owner,const editor::ResourceKey* key){
    CHECK(owner==t_data.data());CHECK(key && key->instance==17 && key->type==19);
    CHECK(t_granted);++t_captain_assets;skg::field<editor::ResourceKey>(t_live.data(),0x130)=*key;
}
void test_async_captain_asset_keeps_value_and_rechecks_identity(){
    reset();t_captain_assets=0;C.authoring_tracked=[]{return true;};
    original_captain_asset=t_set_captain;C.settings_gate=[](editor::ObjectId id){CHECK(id==kCaptainObject);return t_granted;};
    editor::ResourceKey key{17,19,23};captain_asset(t_data.data(),&key);
    CHECK(!t_captain_assets&&held_captain_asset&&held_captain_asset->key==key);
    key.instance=99; // The picker callback's storage has gone away.
    t_granted=true;continue_input(nullptr);
    CHECK(t_captain_assets==1&&!held_captain_asset&&pending.size()==1);
    CHECK(skg::field<editor::ResourceKey>(t_live.data(),0x130)==(editor::ResourceKey{17,19,23}));
    for(unsigned retire=0;retire<4;++retire){
        reset();C.authoring_tracked=[]{return true;};original_captain_asset=t_set_captain;
        C.settings_gate=[](editor::ObjectId){return t_granted;};key={17,19,23};captain_asset(t_data.data(),&key);CHECK(held_captain_asset);
        if(retire==0)++t_scope.author.generation;
        if(retire==1)++generation;
        if(retire==2)++t_revision;
        if(retire==3)deny_held_input({{editor::ScopeKind::Settings,kCaptainObject,{}}});
        t_granted=true;continue_input(nullptr);CHECK(!held_captain_asset&&t_captain_assets==1);
    }
}
}
namespace game { uint32_t mode_id(){return t_game_mode_id;} void destroy_creature(void* actor){CHECK(actor==t_ghost_actor.data()||actor==t_ghost_exit.data());++t_ghosts_destroyed;skg::field<bool>(actor,0x20)=true;t_release(actor);} }
#include "editor_native_history_toolbar_fixture.h"
namespace sui {
Win find(Win root,uint32_t){return root==t_flora_root?t_flora_found:nullptr;}
void set_enabled(Win w,bool value){if(history_ui_fixture::active){history_ui_fixture::enabled(w,value);return;}CHECK(false);} void release(Win){CHECK(false);} void add_ref(Win){CHECK(false);}
uint32_t control_id(Win w){if(history_ui_fixture::active)return history_ui_fixture::id(w);if(w==&t_flora_slider)return 0x072BFDFD;CHECK(w==&t_card_id||w==&t_text);return w==&t_text?t_text_id:t_card_id;} uint32_t flags(Win w){if(history_ui_fixture::active)return history_ui_fixture::flags(w);CHECK(false);return 0;}
std::vector<Win> children(Win w){if(history_ui_fixture::active)return history_ui_fixture::children(w);CHECK(false);return {};}
Win parent(Win w){if(history_ui_fixture::active)return history_ui_fixture::parent(w);CHECK(false);return nullptr;}bool visible(Win w){if(history_ui_fixture::active)return history_ui_fixture::visible(w);CHECK(false);return false;}
}
namespace {
std::array<uint8_t,0x318> v_camera,v_replacement;
std::array<void*,21> v_manager_vt{};
std::array<void*,17> v_cameras_vt{};
std::array<void*,4> v_camera_vt{};
void** v_manager=v_manager_vt.data();
void** v_cameras=v_cameras_vt.data();
void* v_active=v_camera.data();
void* v_named=v_camera.data();
bool v_cast=true,v_refuse=false;
unsigned v_gets=0,v_looks=0,v_sets=0;
std::function<void(unsigned)> v_callback;
void* SK_CDECL v_get_manager(){++v_gets;if(v_callback)v_callback(0);return &v_manager;}
void* SK_THISCALL v_get_cameras(void* self){CHECK(self==&v_manager);if(v_callback)v_callback(1);return &v_cameras;}
void* SK_THISCALL v_get_active(void* self){CHECK(self==&v_cameras);if(v_callback)v_callback(2);return v_active;}
void* SK_THISCALL v_get_named(void* self,uint32_t id){CHECK(self==&v_cameras);CHECK(id==0x00EBB801);if(v_callback)v_callback(3);return v_named;}
void* SK_THISCALL v_get_cast(void* self,uint32_t id){CHECK(self==v_active||self==v_named);CHECK(id==0x011966ED);if(v_callback)v_callback(4);return v_cast?self:nullptr;}
const float* SK_THISCALL v_target(void* self){
    if(v_callback)v_callback(5);
    static std::array<float,3> target;
    for(unsigned i=0;i<3;++i)target[i]=skg::field<float>(self,0x54+4*i)*skg::field<float>(self,0x28);
    return target.data();
}
void SK_THISCALL v_orbit(void* self,float* yaw,float* pitch,float* distance){
    if(v_callback)v_callback(6);
    *yaw=skg::field<float>(self,0x154);*pitch=skg::field<float>(self,0x130);*distance=skg::field<float>(self,0x10C);
}
void SK_THISCALL v_look(void* self,const float* point,const float* quaternion,bool instant){
    CHECK(self==v_active);CHECK(instant);++v_looks;
    if(v_callback)v_callback(7);
    if(v_refuse)return;
    const float radius=std::sqrt(point[0]*point[0]+point[1]*point[1]+point[2]*point[2]);
    for(unsigned i=0;i<3;++i)skg::field<float>(self,0x54+4*i)=point[i]/radius;
    skg::field<float>(self,0x28)=radius;
    std::memcpy(skg::ptr_add<void>(self,0xAC),quaternion,sizeof(float)*4);
}
void SK_THISCALL v_set(void* self,float yaw,float pitch,float distance){
    CHECK(self==v_active);++v_sets;if(v_callback)v_callback(8);if(v_refuse)return;
    skg::field<float>(self,0x154)=skg::field<float>(self,0x15C)=yaw;
    skg::field<float>(self,0x130)=skg::field<float>(self,0x138)=pitch;
    skg::field<float>(self,0x10C)=skg::field<float>(self,0x114)=distance;
}
void view_fixture(){
    reset();v_camera.fill(0);v_replacement.fill(0);v_manager_vt.fill(nullptr);v_cameras_vt.fill(nullptr);v_camera_vt.fill(nullptr);
    v_manager=v_manager_vt.data();v_cameras=v_cameras_vt.data();v_active=v_named=v_camera.data();
    v_cast=true;v_refuse=false;v_gets=v_looks=v_sets=0;v_callback={};
    v_manager_vt[0x50/4]=reinterpret_cast<void*>(v_get_cameras);
    v_cameras_vt[0x38/4]=reinterpret_cast<void*>(v_get_active);
    v_cameras_vt[0x40/4]=reinterpret_cast<void*>(v_get_named);
    v_camera_vt[0x0C/4]=reinterpret_cast<void*>(v_get_cast);
    skg::field<void*>(v_camera.data(),0)=v_camera_vt.data();
    skg::field<float>(v_camera.data(),0x54)=1;skg::field<float>(v_camera.data(),0x28)=500;
    skg::field<float>(v_camera.data(),0xB8)=1;
    skg::field<float>(v_camera.data(),0x154)=.3f;skg::field<float>(v_camera.data(),0x130)=.6f;skg::field<float>(v_camera.data(),0x10C)=123;
    skg::field<float>(v_camera.data(),0x15C)=8;skg::field<float>(v_camera.data(),0x138)=9;skg::field<float>(v_camera.data(),0x114)=999;
    skg::field<float>(v_camera.data(),0x2AC)=-2;skg::field<float>(v_camera.data(),0x2B0)=2;
    skg::field<float>(v_camera.data(),0x26C)=.01f;skg::field<float>(v_camera.data(),0x270)=.02f;
    skg::field<float>(v_camera.data(),0x310)=.1f;skg::field<float>(v_camera.data(),0x314)=10000;
    a_view_manager=uintptr_t(v_get_manager);a_view_target=uintptr_t(v_target);a_view_orbit=uintptr_t(v_orbit);
    a_view_set_orbit=uintptr_t(v_set);a_view_lookat=uintptr_t(v_look);
}
std::array<uint8_t,0x10> v_simulator,v_simulator_camera_state;
std::array<uint8_t,0x30> v_simulator_modes;
uint32_t v_initialization=4;
bool v_world_visible=true,v_modes_missing=false;
void* SK_CDECL v_get_simulator_modes(){if(v_callback)v_callback(10);return v_modes_missing?nullptr:v_simulator_modes.data();}
bool SK_THISCALL v_world_shown(void* self){CHECK(self==t_layout.data());if(v_callback)v_callback(11);return v_world_visible;}
unsigned v_activations=0;
bool v_activation_refuse=false,v_activation_replace=false,v_activation_expire=false,v_activation_stale_cache=false,v_simulator_missing=false;
void* SK_CDECL v_get_simulator(){return v_simulator_missing?nullptr:v_simulator.data();}
void* SK_CDECL v_get_simulator_camera_state(){return v_simulator_camera_state.data();}
bool SK_THISCALL v_activate_id(void* self,uint32_t id){
    CHECK(self==&v_cameras&&id==0x00EBB801);if(!v_activation_refuse)v_active=v_named;return true;
}
bool SK_THISCALL v_activate_simulator(void* self,uint32_t id){
    CHECK(self==v_simulator.data()&&id==0x00EBB801);++v_activations;
    skg::vcall<bool>(&v_cameras,0x34/4,id);
    if(v_active!=v_named)return false;
    if(!v_activation_stale_cache)skg::field<void*>(v_simulator_camera_state.data(),0xC)=skg::vcall<void*>(v_active,0xC/4,uint32_t(0x011966ED));
    if(v_activation_replace)v_named=v_replacement.data();
    if(v_activation_expire)++generation;
    return v_activation_stale_cache||skg::field<void*>(v_simulator_camera_state.data(),0xC)!=nullptr;
}
void creation_camera_fixture(){
    view_fixture();v_simulator.fill(0);v_simulator_camera_state.fill(0);v_activations=0;
    v_simulator_modes.fill(0);v_initialization=4;v_world_visible=true;v_modes_missing=false;
    a_get_simulator_mode_manager=uintptr_t(v_get_simulator_modes);a_scenario_initialization=uintptr_t(&v_initialization);a_layout_shown=uintptr_t(v_world_shown);
    skg::field<void*>(v_simulator_camera_state.data(),0)=v_simulator.data();
    v_activation_refuse=v_activation_replace=v_activation_expire=v_activation_stale_cache=v_simulator_missing=false;
    v_active=v_replacement.data(); // The departed creation editor's camera.
    a_get_simulator_system=uintptr_t(v_get_simulator);a_activate_simulator_camera=uintptr_t(v_activate_simulator);
    a_get_simulator_camera_state=uintptr_t(v_get_simulator_camera_state);
    v_cameras_vt[0x34/4]=reinterpret_cast<void*>(v_activate_id);
    C.creation_return_current=[](const GestureScope& scope,uint64_t serial){return scope==t_scope&&serial==7;};
}
void test_creation_scene_camera_resume(){
    creation_camera_fixture();std::string error;
    SceneOwner scene;CHECK(capture_scene_owner(scene,error));
    CHECK(scene.mode==uintptr_t(t_mode.data())&&scene.data==uintptr_t(t_data.data())&&scene.resource==uintptr_t(t_live.data()));
    CHECK(creation_scene_state(scene).settled());
    const auto owner=t_data;const auto resource=t_live;const auto history=t_history;const auto camera=v_camera;
    const auto selected=state().selected;const auto expected=state().generation;
    CHECK(resume_creation_scene_camera(t_scope,7,expected,scene,error));
    CHECK(v_activations==1&&v_active==v_camera.data()&&skg::field<void*>(v_simulator_camera_state.data(),0xC)==v_camera.data());
    CHECK(!v_looks&&!v_sets&&v_camera==camera&&t_data==owner&&t_live==resource&&t_history==history&&state().selected==selected);
    // An already-active App camera still refreshes Simulator's cached owner.
    skg::field<void*>(v_simulator_camera_state.data(),0xC)=nullptr;
    CHECK(resume_creation_scene_camera(t_scope,7,expected,scene,error)&&v_activations==2&&skg::field<void*>(v_simulator_camera_state.data(),0xC)==v_camera.data());
    for(unsigned kind=0;kind<30;++kind){
        creation_camera_fixture();auto scope=t_scope;uint64_t serial=7,native=state().generation;
        CHECK(capture_scene_owner(scene,error));
        if(kind==0)scope.project[0]^=1;if(kind==1)++scope.epoch;if(kind==2)++scope.author.generation;
        if(kind==3)++serial;if(kind==4)++native;if(kind==5)scope={};
        if(kind==6)t_game_mode_id=modes::kEditor;if(kind==7)skg::field<int>(t_mode.data(),0xCC)=2;
        if(kind==8)skg::field<uint8_t>(t_history.data(),0x20)=0;
        if(kind==9)C.creation_return_current={};if(kind==10)t_active=false;
        if(kind==11)a_activate_simulator_camera=0;if(kind==12)a_get_simulator_system=0;
        if(kind==13)v_named=nullptr;if(kind==14)v_cast=false;if(kind==15)v_simulator_missing=true;
        if(kind==16)a_get_simulator_camera_state=0;if(kind==17)skg::field<void*>(v_simulator_camera_state.data(),0)=nullptr;
        if(kind==18)v_initialization=3;if(kind==19)skg::field<bool>(t_mode.data(),0xDC)=true;
        if(kind==20)skg::field<bool>(v_simulator_modes.data(),0x28)=true;if(kind==21)v_world_visible=false;
        if(kind==22)a_get_simulator_mode_manager=0;if(kind==23)a_scenario_initialization=0;
        if(kind==24)v_modes_missing=true;if(kind==25)scene.mode=1;if(kind==26)scene.data=1;
        if(kind==27)scene.resource=1;if(kind==28)++scene.source.instance;if(kind==29)scene={};
        const auto untouched=v_active;
        CHECK(!resume_creation_scene_camera(scope,serial,native,scene,error)&&!error.empty());
        CHECK(!v_activations&&v_active==untouched&&!v_looks&&!v_sets);
    }
    for(unsigned kind=0;kind<4;++kind){
        creation_camera_fixture();const auto native=state().generation;
        CHECK(capture_scene_owner(scene,error));
        if(kind==0)v_activation_refuse=true;if(kind==1)v_activation_replace=true;if(kind==2)v_activation_expire=true;
        if(kind==3)v_activation_stale_cache=true;
        CHECK(!resume_creation_scene_camera(t_scope,7,native,scene,error)&&!error.empty());
        CHECK(v_activations==1&&!v_looks&&!v_sets);
    }
    // A deferred native OnEnter remains distinct from generic editor readiness.
    // No flag, source, owner or view is changed by the read-only resume probe.
    creation_camera_fixture();CHECK(capture_scene_owner(scene,error));
    auto resume=creation_scene_state(scene);CHECK(resume.available&&resume.owner&&resume.ready&&resume.world_visible&&resume.settled());
    for(unsigned kind=0;kind<5;++kind){
        creation_camera_fixture();CHECK(capture_scene_owner(scene,error));
        if(kind==0)v_initialization=0;if(kind==1)skg::field<bool>(t_mode.data(),0xDC)=true;
        if(kind==2)skg::field<bool>(v_simulator_modes.data(),0x28)=true;if(kind==3)v_world_visible=false;
        if(kind==4)skg::field<editor::ResourceKey>(t_live.data(),8).instance=7;
        const auto before_mode=t_mode;const auto before_data=t_data;const auto before_resource=t_live;
        resume=creation_scene_state(scene);CHECK(!resume.settled());
        CHECK(t_mode==before_mode&&t_data==before_data&&t_live==before_resource&&!v_activations);
        CHECK(resume.ready); // These native resume fences were missing from ready.
    }
    creation_camera_fixture();CHECK(capture_scene_owner(scene,error));
    t_game_mode_id=modes::kEditor;SceneOwner untouched=scene;
    CHECK(!capture_scene_owner(untouched,error)&&untouched==scene);
    CHECK(!creation_scene_state(scene).available);
    a_get_simulator_mode_manager=a_scenario_initialization=a_layout_shown=0;
    reset();
}
std::array<void*,6> v_message_vt;
void** v_messages=v_message_vt.data();
unsigned v_name_notices=0;
unsigned v_name_mutation=0;
std::u16string v_caption;
void* SK_CDECL v_get_messages(){return &v_messages;}
void SK_THISCALL v_name_notice(void* self,uint32_t id,void* packet,void* recipient){
    CHECK(self==&v_messages&&id==0x7AA519DC&&!recipient);++v_name_notices;
    CHECK(skg::field<void*>(packet,0)==t_data.data()+4);
    CHECK(!skg::field<uint32_t>(packet,4)&&!skg::field<uint32_t>(packet,0xC)&&!skg::field<uint32_t>(packet,0x10));
    const auto* title=skg::field<const char16_t*>(packet,8);CHECK(title);v_caption=title;
    // A notification is display-only. Preserve these independently owned dirty
    // flags even if another synchronous listener marks the native owner.
    skg::field<uint32_t>(t_data.data(),0x80)=999;skg::field<bool>(t_data.data(),0x84)=false;skg::field<bool>(t_data.data(),0x85)=true;
    if(v_name_mutation==1)++generation;
    if(v_name_mutation==2)++t_scope.epoch;
    if(v_name_mutation==3)skg::field<void*>(t_mode.data(),0x74)=t_other_data.data();
    if(v_name_mutation==4)++skg::field<editor::ResourceKey>(t_live.data(),8).instance;
    if(v_name_mutation==5)++skg::field<uint32_t>(t_data.data(),0x7C);
}
void test_metadata_caption_notification(){
    std::u16string title=u"Private native adventure";std::string error;
    auto setup=[&]{
        view_fixture();v_message_vt.fill(nullptr);v_messages=v_message_vt.data();v_message_vt[0x14/4]=reinterpret_cast<void*>(v_name_notice);
        a_metadata_message_manager=uintptr_t(v_get_messages);a_metadata_message_send=uintptr_t(v_name_notice);
        skg::field<void*>(t_data.data(),0x4C)=title.data();skg::field<void*>(t_data.data(),0x50)=title.data()+title.size();
        skg::field<uint32_t>(t_data.data(),0x80)=42;skg::field<bool>(t_data.data(),0x84)=true;skg::field<bool>(t_data.data(),0x85)=false;
        v_name_notices=v_name_mutation=0;v_caption.clear();
    };
    setup();SceneOwner owner;Snapshot::Metadata metadata;
    CHECK(capture_scene_owner(owner,error)&&capture_metadata(t_data.data(),metadata,error));const auto expected=state().generation;
    const auto data_before=t_data;const auto resource_before=t_live;const auto history_before=t_history;const auto rows_before=t_rows[t_live.data()];
    CHECK(notify_metadata_caption(owner,expected,t_scope,metadata,error));
    CHECK(v_name_notices==1&&v_caption==title&&t_data==data_before&&t_live==resource_before&&t_history==history_before&&t_rows[t_live.data()]==rows_before);
    CHECK(!pending.size()&&!t_steps&&!t_restores&&!v_looks&&!v_sets);
    for(unsigned reason=0;reason<10;++reason){
        setup();CHECK(capture_scene_owner(owner,error)&&capture_metadata(t_data.data(),metadata,error));auto native=state().generation;auto scope=t_scope;
        if(reason==0)owner.data=1;if(reason==1)owner.resource=1;if(reason==2)owner.mode=1;
        if(reason==3)++native;if(reason==4)++scope.epoch;if(reason==5)t_game_mode_id=modes::kEditor;
        if(reason==6)a_metadata_message_manager=0;if(reason==7)a_metadata_message_send=0;
        if(reason==8)v_message_vt[0x14/4]=nullptr;if(reason==9)metadata.text[0]=u"uncommitted title";
        CHECK(!notify_metadata_caption(owner,native,scope,metadata,error)&&!error.empty()&&!v_name_notices);
    }
    for(unsigned reason=1;reason<=5;++reason){
        setup();CHECK(capture_scene_owner(owner,error)&&capture_metadata(t_data.data(),metadata,error));const auto native=state().generation;v_name_mutation=reason;
        CHECK(!notify_metadata_caption(owner,native,t_scope,metadata,error)&&!error.empty()&&v_name_notices==1);
    }
    a_metadata_message_manager=a_metadata_message_send=0;reset();
}
std::array<uint8_t,0x100> v_terrain;
bool SK_THISCALL v_terrain_loaded(void* self){CHECK(self==v_terrain.data());if(v_callback)v_callback(9);return skg::field<void*>(self,0xFC)==nullptr;}
void test_local_view(){
    std::string error;
    view_fixture();select_native(t_tool_pointer,nullptr);
    const auto live=t_live;const auto owner=t_data;const auto history=t_history;
    LocalView pinned;CHECK(capture_view(pinned,error));CHECK(state().selected.empty());CHECK(v_gets==1);
    CHECK((pinned.target==std::array<float,3>({500,0,0})));
    CHECK((pinned.rotation==std::array<float,4>({0,0,0,1})));
    CHECK((pinned.orbit==std::array<float,3>({.3f,.6f,123})));CHECK(!v_looks&&!v_sets);
    // Camera replacement during native Test is legitimate. No borrowed camera
    // pointer survives capture; return resolves the current owned editor view.
    v_replacement=v_camera;v_active=v_named=v_replacement.data();
    skg::field<float>(v_replacement.data(),0x130)=0;
    skg::field<float>(v_replacement.data(),0x10C)=999;
    CHECK(restore_view(pinned,state().generation,error));CHECK(v_looks==1&&v_sets==1);
    LocalView after;CHECK(capture_view(after,error)&&same_view(pinned,after));
    CHECK(skg::field<float>(v_camera.data(),0x10C)==123);
    CHECK(t_live==live&&t_data==owner&&t_history==history&&state().selected.empty());
    // Exact native owner, camera identity, finite values and settings fail
    // before any setter, with no focus-on-selected fallback.
    for(unsigned kind=0;kind<9;++kind){
        view_fixture();CHECK(capture_view(pinned,error));const auto before=v_looks;
        uint64_t expected=state().generation;
        if(kind==0)++expected;
        if(kind==1)v_named=v_replacement.data();
        if(kind==2)v_cast=false;
        if(kind==3)pinned.orbit[0]=std::numeric_limits<float>::quiet_NaN();
        if(kind==4)pinned.rotation={0,0,0,0};
        if(kind==5)skg::field<float>(v_camera.data(),0x26C)=5;
        if(kind==6)pinned.orbit[1]=3;
        if(kind==7)a_view_orbit=0;
        if(kind==8)skg::field<int>(t_mode.data(),0xCC)=2;
        CHECK(!restore_view(pinned,expected,error));CHECK(v_looks==before&&!error.empty());
    }
    view_fixture();CHECK(capture_view(pinned,error));
    skg::field<float>(v_camera.data(),0x130)=0;v_refuse=true;
    CHECK(!restore_view(pinned,state().generation,error));CHECK(v_looks==1&&v_sets==1);
    // A nonfinite native view cannot replace a previously valid pin.
    view_fixture();CHECK(capture_view(pinned,error));const auto old=pinned;
    skg::field<float>(v_camera.data(),0x10C)=std::numeric_limits<float>::infinity();
    CHECK(!capture_view(pinned,error)&&pinned==old);
    // Accepted terrain materialization retains a copied camera view until the
    // native loader and actual frame complete. Scene readiness cannot precede
    // the view acknowledgement, and no borrowed camera survives the rebuild.
    view_fixture();CHECK(capture_view(pinned,error));t_terraform.fill(0);v_terrain.fill(0);
    skg::field<void*>(t_mode.data(),0x18)=t_terraform.data();
    skg::field<void*>(t_terraform.data(),0x190)=v_terrain.data();
    a_terrain_vtable=0x12345678;skg::field<uintptr_t>(v_terrain.data(),0)=a_terrain_vtable;a_terrain_loaded=uintptr_t(v_terrain_loaded);
    skg::field<float>(v_camera.data(),0x130)=0;
    deferred_local_view=DeferredLocalView{pinned,t_scope,generation,mode(),data(mode()),resource(data(mode())),skg::field<editor::ResourceKey>(resource(data(mode())),8)};
    terrain_rebuilding=true;terrain_rebuild_frame=t_frame;
    CHECK(!state().ready);drain_local_view(nullptr);CHECK(deferred_local_view&&!v_looks);
    ++t_frame;skg::field<void*>(v_terrain.data(),0xFC)=v_terrain.data();drain_local_view(nullptr);CHECK(deferred_local_view&&!v_looks&&!state().ready);
    skg::field<void*>(v_terrain.data(),0xFC)=nullptr;drain_local_view(nullptr);
    CHECK(!deferred_local_view&&v_looks==1&&v_sets==1&&state().ready);
    CHECK(capture_view(after,error)&&same_view(pinned,after));
    for(unsigned reason=0;reason<5;++reason){
        view_fixture();CHECK(capture_view(pinned,error));
        deferred_local_view=DeferredLocalView{pinned,t_scope,generation,mode(),data(mode()),resource(data(mode())),skg::field<editor::ResourceKey>(resource(data(mode())),8)};
        if(reason==0)++generation;if(reason==1)++t_scope.epoch;
        if(reason==2)++t_scope.author.generation;if(reason==3)deferred_local_view->owner=t_other_data.data();
        if(reason==4)deferred_local_view->mode=nullptr;
        drain_local_view(nullptr);CHECK(!deferred_local_view&&!v_looks&&!v_sets&&!t_failed);
    }
}
}
static void test_large_transform_without_roster_work(){
    using namespace editor_native;
    reset();skg::field<void*>(t_context.data(),4)=nullptr;
    std::array<uint8_t,0x27E8> classes{};std::array<uint8_t,0x534> acts{};
    skg::field<uint32_t>(classes.data(),0)=1024;
    skg::field<void*>(t_live.data(),0x2BF4)=classes.data();
    skg::field<void*>(t_live.data(),0x2BF8)=classes.data()+classes.size();
    skg::field<void*>(t_live.data(),0x23C)=acts.data();
    skg::field<void*>(t_live.data(),0x240)=acts.data()+acts.size();
    auto& rows=t_rows[t_live.data()];rows.assign(kPlacementLimit*0x238,0);rows_for(t_live.data());
    for(unsigned i=0;i<kPlacementLimit;++i){
        auto* marker=rows.data()+i*0x238+4;
        skg::field<uint32_t>(marker,0)=1024;
        skg::field<editor::Vec3>(marker,4)={float(i),0,0};
        skg::field<editor::Rotation>(marker,0x10)={0,0,0,1};
        skg::field<float>(marker,0x20)=1;skg::field<float>(marker,0x24)=.5f;
    }
    a_write=uintptr_t(t_write);a_read=uintptr_t(t_no_read);a_apply_marker=uintptr_t(t_transform);
    t_object_lookups=t_writes=t_reads=t_transforms=0;
    Snapshot before;std::string error;CHECK(capture(before,error));
    auto wanted=before.document;wanted.placements.rbegin()->second.position.y=3;
    const auto raw=rows;
    CHECK(editor_native::apply(wanted,error));
    CHECK(t_transforms==1&&t_object_lookups==0&&t_reads==0&&t_writes==2);
    CHECK(t_allocations.empty()&&skg::field<bool>(t_data.data(),0x84));
    for(unsigned i=0;i<kPlacementLimit;++i){
        auto* marker=rows.data()+i*0x238+4;
        CHECK(skg::field<float>(marker,8)==(i==kPlacementLimit-1?3.f:0.f));
    }
    auto expected=raw;skg::field<float>(expected.data()+(kPlacementLimit-1)*0x238+4,8)=3;
    CHECK(rows==expected);
    CHECK(editor_native::apply(wanted,error));CHECK(t_transforms==1&&t_object_lookups==0&&t_reads==0);
    wanted.placements.rbegin()->second.position.x=std::numeric_limits<float>::infinity();
    CHECK(!editor_native::apply(wanted,error));CHECK(rows==expected&&t_transforms==1&&t_reads==0);
    reset();CHECK(t_allocations.empty());
}
static std::array<uint8_t,0x27E8> t_pose_class;
static std::array<uint8_t,0x534> t_pose_act;
static unsigned t_pose_row_copies=0,t_pose_inserts=0;
static bool SK_THISCALL t_pose_write(void* r,editor_native::Stream* stream){
    ++t_writes;const auto& rows=t_rows[r];
    const std::array<uint32_t,3> header{0x50534531,uint32_t(rows.size()/0x238),skg::field<uint32_t>(r,0x2C4C)};
    return editor_native::write(stream,header.data(),sizeof header) && editor_native::write(stream,rows.data(),uint32_t(rows.size()));
}
static bool SK_THISCALL t_pose_read(void* r,editor_native::Stream* stream){
    ++t_reads;std::array<uint32_t,3> header{};
    if(editor_native::read(stream,header.data(),sizeof header)!=sizeof header || header[0]!=0x50534531 ||
       header[1]>editor_native::kPlacementLimit || stream->bytes.size()-stream->position!=header[1]*0x238)return false;
    auto& rows=t_rows[r];rows.resize(header[1]*0x238);
    if(editor_native::read(stream,rows.data(),uint32_t(rows.size()))!=int(rows.size()))return false;
    rows_for(r);skg::field<uint32_t>(r,0x2C4C)=header[2];
    skg::field<void*>(r,0x2BF4)=t_pose_class.data();skg::field<void*>(r,0x2BF8)=t_pose_class.data()+t_pose_class.size();
    skg::field<void*>(r,0x23C)=t_pose_act.data();skg::field<void*>(r,0x240)=t_pose_act.data()+t_pose_act.size();return true;
}
static void* SK_THISCALL t_pose_copy_rows(void* destination,void* source){
    ++t_pose_row_copies;auto* owner=static_cast<uint8_t*>(destination)-0x2C10;
    const auto first=skg::field<uint8_t*>(source,0),last=skg::field<uint8_t*>(source,4);
    t_rows[owner].assign(first,last);rows_for(owner);return destination;
}
static uint32_t SK_THISCALL t_pose_insert(void* destination,void* marker){
    ++t_pose_inserts;auto* owner=static_cast<uint8_t*>(destination)-0x2C10;auto& rows=t_rows[owner];
    const auto index=uint32_t(rows.size()/0x238);const auto* first=static_cast<uint8_t*>(marker)-4;
    rows.insert(rows.end(),first,first+0x238);rows_for(owner);return index;
}
std::array<uint8_t,0x34> t_entry_marker_act;
void* SK_THISCALL t_entry_copy_acts(void* destination,const uintptr_t* source){
    CHECK(source[1]-source[0]==0x534);std::memcpy(destination,source,12);return destination;
}
bool SK_THISCALL t_entry_write(void* r,editor_native::Stream* stream){
    const std::array<uint32_t,3> prefix{0xB523D4F9,17,0};
    const auto& rows=t_rows[r];const std::array<uint32_t,3> header{0x454E5431,uint32_t(rows.size()/0x238),skg::field<uint32_t>(r,0x2C4C)};
    return editor_native::write(stream,prefix.data(),sizeof prefix) && editor_native::write(stream,header.data(),sizeof header) &&
        editor_native::write(stream,skg::ptr_add<void>(r,0x130),0x46) && editor_native::write(stream,rows.data(),uint32_t(rows.size()));
}
bool SK_THISCALL t_entry_read(void* r,editor_native::Stream* stream){
    std::array<uint32_t,3> prefix{},header{};
    if(editor_native::read(stream,prefix.data(),sizeof prefix)!=sizeof prefix || prefix!=std::array<uint32_t,3>{0xB523D4F9,17,0} ||
       editor_native::read(stream,header.data(),sizeof header)!=sizeof header || header[0]!=0x454E5431 || header[1]>editor_native::kPlacementLimit ||
       editor_native::read(stream,skg::ptr_add<void>(r,0x130),0x46)!=0x46 || stream->bytes.size()-stream->position!=header[1]*0x238)return false;
    auto& rows=t_rows[r];rows.resize(header[1]*0x238);
    if(editor_native::read(stream,rows.data(),uint32_t(rows.size()))!=int(rows.size()))return false;
    rows_for(r);skg::field<uint32_t>(r,0x2C4C)=header[2];
    skg::field<void*>(r,0x2BF4)=t_pose_class.data();skg::field<void*>(r,0x2BF8)=t_pose_class.data()+t_pose_class.size();
    skg::field<void*>(r,0x23C)=t_pose_act.data();skg::field<void*>(r,0x240)=t_pose_act.data()+t_pose_act.size();return true;
}
void test_entry_hydrates_nonzero_captain_before_scene_entry(){
    reset();const auto old_read=a_read,old_write=a_write,old_acts=a_copy_acts,old_crew=a_copy_crew;
    a_read=uintptr_t(t_entry_read);a_write=uintptr_t(t_entry_write);a_copy_acts=uintptr_t(t_entry_copy_acts);a_copy_crew=0;
    C.authoring_tracked=[]{return true;};t_pose_class.fill(0);t_pose_act.fill(0);t_entry_marker_act.fill(0);
    skg::field<uint32_t>(t_pose_class.data(),0)=1024;
    skg::field<void*>(t_live.data(),0x2BF4)=t_pose_class.data();skg::field<void*>(t_live.data(),0x2BF8)=t_pose_class.data()+t_pose_class.size();
    skg::field<void*>(t_live.data(),0x23C)=t_pose_act.data();skg::field<void*>(t_live.data(),0x240)=t_pose_act.data()+t_pose_act.size();
    // Exact nonzero Adventure Town identity from the retained c8 rev0
    // checkpoint. The controlled stream exercises the production adapter's
    // typed projection/recursive validation before any Scenario scene exists.
    const editor::ResourceKey town{132681735,731352134,1080189440};
    skg::field<editor::ResourceKey>(t_live.data(),0x130)=town;skg::field<uint64_t>(t_live.data(),0x140)=UINT64_MAX;
    skg::field<float>(t_live.data(),0x16C)=3;skg::field<float>(t_live.data(),0x170)=.9f;
    for(auto* p=t_rows[t_live.data()].data();p<t_rows[t_live.data()].data()+t_rows[t_live.data()].size();p+=0x238){
        auto* marker=p+4;skg::field<uint32_t>(marker,0)=1024;skg::field<editor::Rotation>(marker,0x10)={0,0,0,1};
        skg::field<float>(marker,0x20)=1;skg::field<float>(marker,0x24)=.5f;
        skg::field<void*>(marker,0x28)=t_entry_marker_act.data();skg::field<void*>(marker,0x2C)=t_entry_marker_act.data()+t_entry_marker_act.size();
    }
    std::vector<uint8_t> bytes;std::string error;CHECK(serialize(t_live.data(),bytes,error));
    const auto original=t_live;const auto original_rows=t_rows[t_live.data()];t_game_mode_id=modes::kMenu;
    Owned parsed(parse(bytes,error));CHECK(parsed.p);Snapshot fresh;fresh.act=0;editor::Document identity;
    CHECK(projection(parsed.p,fresh,error,nullptr,std::nullopt,std::nullopt,std::nullopt,std::nullopt,&identity));fresh.bytes=bytes;
    CHECK(fresh.document.settings.contains(kCaptainObject)&&fresh.captain==editor::ResourceKey{});
    CHECK(!validate(fresh,fresh.document,error)&&error.find("authored sidecar")!=std::string::npos); // Original c8 regression.
    CHECK(validate_entry(bytes,0,error)); // Entry itself now hydrates its own proof.
    CHECK(hydrate_native_captain(parsed.p,fresh,error)&&fresh.captain==town&&!fresh.captain_locked);
    CHECK(validate(fresh,fresh.document,error));
    auto malformed=fresh;malformed.captain.instance^=1;CHECK(!validate(malformed,fresh.document,error));
    malformed=fresh;malformed.captain_locked=true;CHECK(!validate(malformed,fresh.document,error));
    CHECK(t_game_mode_id==modes::kMenu&&!t_restores&&pending.empty()&&t_live==original&&t_rows[t_live.data()]==original_rows);
    release(std::exchange(parsed.p,nullptr)); // Release explicitly before fixture reset.
    a_read=old_read;a_write=old_write;a_copy_acts=old_acts;a_copy_crew=old_crew;reset();
}
bool t_captain_resolve_failure=false;
bool SK_CDECL t_resolve_captain(uint64_t server,editor::ResourceKey* key){
    CHECK(server==17);if(t_captain_resolve_failure)return false;*key={7,9,11};return true;
}
uintptr_t t_captain_symbol(const char* name){
    CHECK(std::string(name)=="Simulator::Scenario::ResolveLocalServerAsset");return uintptr_t(t_resolve_captain);
}
void test_resolved_captain_sidecars(){
    reset();const auto* previous=H;SkHost host=*H;host.sym=t_captain_symbol;H=&host;t_captain_resolve_failure=false;
    skg::field<editor::ResourceKey>(t_live.data(),0x130)={1,2,3};skg::field<uint64_t>(t_live.data(),0x140)=17;
    skg::field<bool>(t_live.data(),0x175)=true;
    Snapshot snapshot;snapshot.captain={7,9,11};snapshot.captain_locked=true;
    editor::AuthoringSettings captain;captain.object=kCaptainObject;captain.kind=editor::SettingsKind::Captain;
    captain.properties={{kCaptainAssetProperty,snapshot.captain},{kCaptainLockedProperty,true}};
    snapshot.document.settings.emplace(kCaptainObject,captain);std::string error;
    const auto untouched=t_live;
    CHECK(captain_sidecar_matches(t_live.data(),snapshot,snapshot.document,error));
    snapshot.captain={1,2,3};CHECK(!captain_sidecar_matches(t_live.data(),snapshot,snapshot.document,error));
    snapshot.captain={7,9,11};snapshot.captain_locked=false;CHECK(!captain_sidecar_matches(t_live.data(),snapshot,snapshot.document,error));
    snapshot.captain_locked=true;t_captain_resolve_failure=true;CHECK(!captain_sidecar_matches(t_live.data(),snapshot,snapshot.document,error));
    t_captain_resolve_failure=false;host.sym=nullptr;CHECK(!captain_sidecar_matches(t_live.data(),snapshot,snapshot.document,error));
    // Older non-Captain projections retain the raw sidecar and never invent a
    // dependency resolution that their schema could not have asserted.
    snapshot.document={};snapshot.captain={1,2,3};CHECK(captain_sidecar_matches(t_live.data(),snapshot,snapshot.document,error));
    snapshot.captain={};snapshot.captain_locked=false;CHECK(captain_sidecar_matches(t_live.data(),snapshot,snapshot.document,error));
    CHECK(t_live==untouched);H=previous;reset();
}
static void test_exact_validation_cache_and_owned_pose_prepare(){
    using namespace editor_native;
    reset();const auto old_write=a_write,old_read=a_read,old_copy=a_copy_marker_rows,old_insert=a_insert_marker;
    t_pose_class.fill(0);t_pose_act.fill(0);skg::field<uint32_t>(t_pose_class.data(),0)=1024;
    skg::field<void*>(t_live.data(),0x2BF4)=t_pose_class.data();skg::field<void*>(t_live.data(),0x2BF8)=t_pose_class.data()+t_pose_class.size();
    skg::field<void*>(t_live.data(),0x23C)=t_pose_act.data();skg::field<void*>(t_live.data(),0x240)=t_pose_act.data()+t_pose_act.size();
    skg::field<uint32_t>(t_live.data(),0x2C4C)=0xDEADBEEF;
    auto& raw=t_rows[t_live.data()];
    for(unsigned i=0;i<2;++i){auto* marker=raw.data()+i*0x238+4;skg::field<uint32_t>(marker,0)=1024;
        skg::field<editor::Vec3>(marker,4)={float(i),0,0};skg::field<editor::Rotation>(marker,0x10)={0,0,0,1};
        skg::field<float>(marker,0x20)=1;skg::field<float>(marker,0x24)=.5f;skg::field<uint32_t>(marker,0x100)=77+i;}
    a_write=uintptr_t(t_pose_write);a_read=uintptr_t(t_pose_read);a_copy_marker_rows=uintptr_t(t_pose_copy_rows);a_insert_marker=uintptr_t(t_pose_insert);
    Snapshot base;std::string error;CHECK(capture(base,error));base.document.project[0]=1;base.document.epoch=9;
    const auto live_before=raw;const auto hits=validation_hits;t_reads=0;t_pose_row_copies=t_pose_inserts=0;
    {
        ValidationScope scope;CHECK(validate(base,base.document,error));CHECK(t_reads==1);
        CHECK(validate(base,base.document,error));CHECK(t_reads==1 && validation_hits==hits+1);
        const auto id=base.document.placements.begin()->first;auto next=base.document;next.revision=1;
        auto& p=next.placements.at(id);p.position={4,5,6};p.revision=1;p.field_revisions[{editor::Field::Position,0}]=1;
        editor::Change change;change.revision=1;change.patch.fields.push_back({id,{editor::Field::Position,0},base.document.placements.at(id).position,p.position,0});
        Snapshot prepared;CHECK(prepare(base,next,change,nullptr,prepared,error));CHECK(prepared.document==next);
        CHECK(raw==live_before && !t_pose_row_copies && !t_pose_inserts); // Exact reverse proof needs no roster rebuild.
        auto unsupported=prepared;unsupported.bytes[12+4+0x100]^=1;Gesture invalid;invalid.before=base;invalid.after=unsupported;
        CHECK(!supported_changes(invalid,error)); // Pose optimization cannot admit an unrelated opaque marker mutation.
        CHECK(prepare(base,next,change,nullptr,prepared,error));CHECK(raw==live_before);
        auto forged=base;forged.bytes[0]^=1;CHECK(!validate(forged,base.document,error));CHECK(!prepare(forged,next,change,nullptr,prepared,error));
        forged=base;forged.captain={7,9,11};CHECK(!validate(forged,base.document,error));
        forged=base;forged.captain_locked=true;CHECK(!validate(forged,base.document,error));
        forged=base;forged.metadata->text[0]=std::u16string(1,u'\0');CHECK(!validate(forged,base.document,error));
        forged=base;forged.bindings[0].marker=9999;CHECK(!validate(forged,base.document,error));
        forged=base;forged.act=1;CHECK(!validate(forged,base.document,error));
        auto wrong=base.document;wrong.placements.at(id).position.x=99;CHECK(!validate(base,wrong,error));
        const auto reads=t_reads;CHECK(validate(base,base.document,error));CHECK(t_reads==reads);
        ++generation;CHECK(validate(base,base.document,error));CHECK(t_reads==reads+1);
        {ValidationScope nested;CHECK(validate(base,base.document,error));CHECK(t_reads==reads+1);}
    }
    CHECK(parsed_images.empty() && !parsed_image_bytes && !validation_depth);const auto reads=t_reads;CHECK(validate(base,base.document,error));CHECK(t_reads==reads);
    // A copied Snapshot cannot smuggle an unsupported metadata edit behind an
    // otherwise equal native byte stream or a successful validation cache hit.
    Gesture metadata_change;metadata_change.before=base;metadata_change.after=base;
    metadata_change.after.metadata->genre=7;CHECK(!supported_changes(metadata_change,error));
    metadata_change.after.metadata=base.metadata;metadata_change.after.metadata->text[1]=u"unowned description";
    CHECK(!supported_changes(metadata_change,error));
    metadata_change.after.metadata=base.metadata;metadata_change.before.metadata.reset();
    CHECK(supported_changes(metadata_change,error)); // Explicit unknown legacy metadata.
    metadata_change.before=base;metadata_change.after=base;metadata_change.after.captain={1,2,3};
    CHECK(!supported_changes(metadata_change,error));
    metadata_change.after=base;metadata_change.after.captain_locked=true;CHECK(!supported_changes(metadata_change,error));
    // Native Start/Commit keep immutable metadata values from their own actual
    // boundaries. Later owner strings, text edits or callbacks cannot rewrite
    // either witness, and the drain never fills both from the current owner.
    std::u16string before_name=u"before",after_name=u"after";
    auto point_name=[&](std::u16string& name){skg::field<void*>(t_data.data(),0x4C)=name.data();skg::field<void*>(t_data.data(),0x50)=name.data()+name.size();};
    point_name(before_name);skg::field<uint32_t>(t_data.data(),0x7C)=3;
    start(t_data.data());CHECK(gesture_before_metadata&&gesture_before_metadata->text[0]==u"before");
    before_name[0]=u'!';point_name(after_name);skg::field<uint32_t>(t_data.data(),0x7C)=4;
    skg::field<void*>(t_entry.data(),0x14)=t_live.data();commit(t_data.data());
    CHECK(pending.size()==1&&pending.front().before_metadata->genre==3&&pending.front().after_metadata->genre==4);
    after_name[0]=u'!';skg::field<uint32_t>(t_data.data(),0x7C)=9;
    std::optional<Gesture> captured;C.gesture=[&](Gesture gesture){captured=std::move(gesture);};editor_native::drain(nullptr);
    CHECK(captured&&captured->before.metadata->text[0]==u"before"&&captured->after.metadata->text[0]==u"after");
    CHECK(captured->before.metadata->genre==3&&captured->after.metadata->genre==4&&!supported_changes(*captured,error));
    CHECK(!pending.size()&&!gesture_before_metadata&&gesture_metadata_error.empty());
    for(unsigned kind=0;kind<4;++kind){
        std::array<char16_t,2> invalid{u'a',u'\0'};
        skg::field<void*>(t_data.data(),0x4C)=invalid.data();skg::field<void*>(t_data.data(),0x50)=invalid.data()+invalid.size();
        if(kind==1)skg::field<uintptr_t>(t_data.data(),0x50)=uintptr_t(invalid.data())-2;
        if(kind==2)skg::field<uintptr_t>(t_data.data(),0x50)=uintptr_t(invalid.data())+1;
        if(kind==3)skg::field<uintptr_t>(t_data.data(),0x50)=uintptr_t(invalid.data())+8194;
        Snapshot::Metadata unchanged;unchanged.genre=77;CHECK(!capture_metadata(t_data.data(),unchanged,error)&&unchanged.genre==77);
    }
    a_write=old_write;a_read=old_read;a_copy_marker_rows=old_copy;a_insert_marker=old_insert;
    reset();CHECK(t_allocations.empty());
}
#include "editor_native_act_cache_fixture.h"
#include "editor_native_crew_assets_fixture.h"
#include "editor_native_recent_fixture.h"
#include "editor_native_pose_capture_fixture.h"
#include "editor_native_staged_capture_fixture.h"
#include "editor_native_terrain_materialization_fixture.h"
#include "editor_native_diagnostic_fixture.h"
#include "editor_native_view_fence_fixture.h"
#include "editor_native_water_fixture.h"
#include "editor_native_environment_fixture.h"
#include "editor_native_complexity_fixture.h"
#include "editor_native_pose_prepare_fixture.h"
#include "editor_native_pose_prepare_staged_fixture.h"
#include "editor_native_service_pulse_fixture.h"
int main(int argc,char** argv){
    if(argc==2&&std::string(argv[1])=="--pose-prepare-staged-only"){
        test_two_frame_native_pose_prepare();test_staged_pose_checked_application();std::printf("native staged pose prepare: %u checks passed\n",checks);return 0;
    }
    if(argc==1){test_two_frame_native_pose_prepare();test_staged_pose_checked_application();}
    if(argc==2&&std::string(argv[1])=="--service-pulse-only"){service_pulse_fixture::test();std::printf("native service pulse: %u checks passed\n",checks);return 0;}
    if(argc==1)service_pulse_fixture::test();
    if(argc==2&&std::string(argv[1])=="--pose-prepare-only"){
        test_native_pose_prepare_projection();std::printf("native pose prepare: %u checks passed\n",checks);return 0;
    }
    if(argc==2&&std::string(argv[1])=="--environment-only"){
        test_native_environment_refresh();std::printf("native Environment refresh: %u checks passed\n",checks);return 0;
    }
    if(argc==2&&std::string(argv[1])=="--water-only"){
        test_native_water_completion();std::printf("native Water completion: %u checks passed\n",checks);return 0;
    }
    if(argc==2&&std::string(argv[1])=="--staged-capture-only"){
        test_staged_native_capture();std::printf("native staged capture: %u checks passed\n",checks);return 0;
    }
    if(argc==2&&std::string(argv[1])=="--creation-grounding-only"){
        test_creation_grounding_return();std::printf("native creation grounding return: %u checks passed\n",checks);return 0;
    }
    if(argc==2&&std::string(argv[1])=="--complexity-only"){
        test_native_complexity_capability();std::printf("native complexity capability: %u checks passed\n",checks);return 0;
    }
    if(argc==2&&std::string(argv[1])=="--terrain-rotation-capture-only"){
        test_terrain_rotation_capture();std::printf("native terrain rotation capture: %u checks passed\n",checks);return 0;
    }
    if(argc==2&&std::string(argv[1])=="--terrain-parent-only"){
        test_terrain_parent_load();std::printf("native terrain parents: %u checks passed\n",checks);return 0;
    }
    if(argc==2&&std::string(argv[1])=="--terrain-rotation-only"){
        test_terrain_rotation_restore();std::printf("native terrain rotations: %u checks passed\n",checks);return 0;
    }
    if(argc==2&&std::string(argv[1])=="--restore-fence-only"){
        test_native_restore_fences();std::printf("native restore fences: %u checks passed\n",checks);return 0;
    }
    if(argc==2&&std::string(argv[1])=="--view-fence-only"){
        test_native_view_fences();std::printf("native view fences: %u checks passed\n",checks);return 0;
    }
    if(argc==2&&std::string(argv[1])=="--diagnostic-only"){
        test_native_payload_diagnostics();std::printf("native payload diagnostics: %u checks passed\n",checks);return 0;
    }
    if(argc==2&&std::string(argv[1])=="--history-toolbar-only"){
        test_history_toolbar_current_layout();std::printf("native history toolbar: %u checks passed\n",checks);return 0;
    }
    if(argc==2&&std::string(argv[1])=="--pose-capture-only"){
        test_pose_capture_proofs();std::printf("native pose capture proofs: %u checks passed\n",checks);return 0;
    }
    if(argc==2&&std::string(argv[1])=="--save-grounding-only"){
        test_saved_grounding_transition();std::printf("native saved grounding: %u checks passed\n",checks);return 0;
    }
    if(argc==2&&std::string(argv[1])=="--terrain-materialization-only"){
        test_terrain_materialization_hooks();std::printf("native terrain materialization: %u checks passed\n",checks);return 0;
    }
    if(argc==2&&std::string(argv[1])=="--recent-only"){
        test_recent_native_proofs();std::printf("native recent proofs: %u checks passed\n",checks);return 0;
    }
    test_native_complexity_capability();
    test_native_view_fences();
    test_native_restore_fences();
    test_terrain_rotation_restore();
    test_terrain_rotation_capture();
    test_native_water_completion();
    test_native_environment_refresh();
    test_terrain_parent_load();
    test_native_payload_diagnostics();
    test_history_toolbar_current_layout();
    test_recent_native_proofs();
    test_pose_capture_proofs();
    test_native_pose_prepare_projection();
    test_staged_native_capture();
    test_terrain_materialization_hooks();
    test_owned_crew_asset_versions();
    test_act_projection_production_cache();test_act_projection_cache_bounds();
    test_entry_hydrates_nonzero_captain_before_scene_entry();
    test_metadata_caption_notification();
    test_resolved_captain_sidecars();
    test_pending_capture_fences();
    test_preset_history_boundary();
    test_flora_control_retry();
    test_terrain_control_retry();
    test_cold_native_terrain_initialization();
    test_terrain_pending_grab();
    test_terrain_preset_retry();
    test_full_roster_fingerprint();test_exact_class_projection_cache();
    test_crew_input_ownership();
    test_captain_and_act_input_ownership();
    test_async_captain_asset_keeps_value_and_rechecks_identity();
    test_independent_inspector_survives_scene_refresh();
    test_render_only_collaboration();
    // The native multi-select picker invokes import once for each selected key.
    // None may overwrite an earlier selection while document ownership waits.
    reset();C.cast_tracked=[] {return true;};C.structure_gate=[](uint32_t index){CHECK(index==UINT32_MAX);return false;};
    for(uint32_t key=1;key<=33;++key){editor::ResourceKey asset{key,7,9};CHECK(import_native(t_data.data(),&asset)==UINT32_MAX);}
    CHECK(held_import && held_import->size()==32);
    for(size_t i=0;i<held_import->size();++i)CHECK((*held_import)[i].instance==i+1);
    editor::ResourceKey duplicate{1,7,9};CHECK(import_native(t_data.data(),&duplicate)==UINT32_MAX);CHECK(held_import->size()==32);
    continue_input(nullptr);CHECK(held_import->size()==32 && t_allocations.empty());
    ++t_scope.epoch;continue_input(nullptr);CHECK(!held_import && t_allocations.empty());
    reset();begin();commit(t_data.data());CHECK(!pending.empty());
    held_import.emplace(std::deque<editor::ResourceKey>{{1,7,9}});
    import_scope=t_scope;import_generation=generation;import_started=t_time;
    C.structure_gate=[](uint32_t){CHECK(false);return true;};
    continue_input(nullptr);CHECK(held_import && held_import->size()==1 && !pending.empty());
    reset();held_import.emplace(std::deque<editor::ResourceKey>{{1,7,9}});
    import_scope=t_scope;import_generation=generation;import_started=t_time;t_time+=3;
    C.structure_gate=[](uint32_t){CHECK(false);return true;};C.transaction_pending=[] {return true;};
    continue_input(nullptr);CHECK(held_import && held_import->size()==1);
    C.cast_tracked=[] {return true;};editor::ResourceKey next_asset{2,7,9};
    CHECK(import_native(t_data.data(),&next_asset)==UINT32_MAX);CHECK(held_import->size()==2);
    C.transaction_pending=[] {return false;};t_time+=3;continue_input(nullptr);CHECK(!held_import);
    reset();held_import.emplace(std::deque<editor::ResourceKey>{{1,7,9}});
    import_scope=t_scope;import_generation=generation;import_started=t_time;
    C.import_scope=[] {return t_scope;};C.gesture_scope=[] {return GestureScope{};};
    C.transaction_pending=[] {return true;};C.structure_gate=[](uint32_t){CHECK(false);return true;};
    continue_input(nullptr);CHECK(held_import && held_import->size()==1);
    ++t_scope.author.generation;continue_input(nullptr);CHECK(!held_import);
    test_pending_press();
    test_card_action_scope();
    test_cast_actor_rebuild_keeps_marker_identity();
    test_script_cast_callback_keeps_bindings();
    test_pending_class_removal();
    test_palette_removal_waits_before_ui_and_history();
    test_pending_palette_drag();
    test_coalesced_speech();
    test_pose_refresh_preserves_authored_audio();
    test_large_transform_without_roster_work();
    test_exact_validation_cache_and_owned_pose_prepare();
    test_creation_scene_camera_resume();test_local_view();
    using namespace editor_native;
    // Ordinary editing keeps the native entry-level Undo behavior intact.
    reset();t_active=false;undo_native(t_entry.data());CHECK(t_original_undo==1&&!t_own_undo&&!cancellation);
    // Read-only ordinary diagnostics require exact native owned-entry identity;
    // they neither count committed Undo nor alter original call forwarding.
    t_start(t_data.data());undo_native(t_entry.data());CHECK(t_original_undo==2&&ordinary_cancellations==1&&!cancellation);
    skg::field<void*>(t_entry.data(),0)=t_resource_vt.data();undo_native(t_entry.data());CHECK(t_original_undo==3&&ordinary_cancellations==1);
    skg::field<void*>(t_entry.data(),0)=t_entry_vt.data();applying=true;undo_native(t_entry.data());CHECK(t_original_undo==4&&ordinary_cancellations==1);applying=false;
    skg::field<uint8_t>(t_history.data(),0x20)=1;undo_native(t_entry.data());CHECK(t_original_undo==5&&ordinary_cancellations==1);
    skg::field<uint8_t>(t_history.data(),0x20)=0;ordinary_cancellations=UINT64_MAX;undo_native(t_entry.data());CHECK(t_original_undo==6&&ordinary_cancellations==UINT64_MAX);
    // A user Undo outside a current edit is chronological and never cancelled.
    reset();undo_native(t_entry.data());CHECK(t_own_undo==1&&!t_original_undo&&!cancellation);
    // Invalid native Scale cancels only its owned current entry, then settles
    // latest canonical after the native caller finalizes and an actual frame.
    reset();begin();value(t_live.data(),0)=99;value(t_live.data(),1)=42;
    skg::field<float>(t_live.data(),0x2C30)=9;
    undo_native(t_entry.data());CHECK(t_restores==1&&!t_original_undo&&!t_own_undo);
    CHECK(value(t_live.data(),0)==1&&value(t_live.data(),1)==2);
    CHECK(skg::field<float>(t_live.data(),0x2C30)==9);CHECK(!state().ready);
    const auto ticket=cancellation->ticket;undo_native(t_entry.data());CHECK(t_restores==1&&cancellation->ticket==ticket);
    C.history_available=[](bool){return true;};C.gate=[](const auto&){return true;};C.structure_gate=[](uint32_t){return true;};
    CHECK(!has_undo(t_history.data())&&!has_redo(t_history.data()));
    CHECK(tool_native(t_tool_pointer,1,0,0)==1&&tool_native(t_tool_pointer,6,0,0)==1&&tool_native(t_tool_pointer,11,0,0)==1);
    CHECK(!create_native(t_tool_pointer,7)&&!clone_selection_native(t_tool_pointer,t_actors[0].data()));
    delete_native(t_tool_pointer);history_undo(t_history.data());history_redo(t_history.data());redo_native(t_entry.data());CHECK(!t_own_undo);
    skg::field<void*>(t_history.data(),0x24)=nullptr;skg::field<uint8_t>(t_history.data(),0x20)=1;
    drain();CHECK(!t_settled&&cancellation);next_drain();
    CHECK(t_settled==1&&!t_failed&&!cancellation&&state().ready);CHECK(t_seen==ticket);
    CHECK(value(t_live.data(),0)==1&&value(t_live.data(),1)==42);CHECK(selection.size()==1&&selection.front().id==(1ull<<32));
    CHECK(skg::field<float>(t_live.data(),0x2C30)==9&&t_allocations.empty());
    // Unknown active entry/type, missing before and serial exhaustion fail
    // outside the finalizer, without own Undo.
    for(unsigned kind=0;kind<5;++kind){
        reset();begin();
        if(kind==0)skg::field<void*>(t_entry.data(),0)=t_resource_vt.data();
        if(kind==1){release(gesture_before_resource);gesture_before_resource=nullptr;}
        if(kind==2)cancellation_counter=UINT64_MAX;
        if(kind==3)skg::field<void*>(t_other_entry.data(),0)=t_entry_vt.data();
        if(kind==4)gesture_entry=t_other_entry.data();
        native_cancel(kind==3?t_other_entry.data():nullptr);
        CHECK(!t_failed&&!t_settled&&!t_own_undo&&!t_restores&&cancellation);drain();CHECK(!t_failed);
        next_drain();CHECK(t_failed==1&&!t_settled&&!cancellation);CHECK(!t_failure.empty());
    }
    // Logical replacement can happen before the finalizer enters the hook,
    // while the native entry still exists. Preserve its old captured scope.
    for(unsigned kind=0;kind<4;++kind){
        reset();begin();const auto old_scope=gesture_scope_pin;
        if(kind==0)++t_scope.epoch;
        if(kind==1)++t_scope.author.generation;
        if(kind==2)t_scope={};
        if(kind==3)C.gesture_scope={};
        value(t_live.data(),0)=77;native_cancel();
        CHECK(cancellation&&cancellation->ticket.scope==old_scope&&!t_restores);
        next_drain();CHECK(!cancellation&&!t_settled&&!t_failed&&value(t_live.data(),0)==77);
    }
    // A replacement capability/project/epoch/native context retires only the
    // old hold. It cannot call Runtime or write the replacement's resource.
    for(unsigned kind=0;kind<6;++kind){
        reset();begin();native_cancel();
        if(kind==0)++t_scope.author.generation;
        if(kind==1)++t_scope.epoch;
        if(kind==2)++t_scope.project[0];
        if(kind==3)skg::field<void*>(t_mode.data(),0x74)=t_other_data.data();
        if(kind==4)t_active=false;
        if(kind==5)++generation;
        value(t_live.data(),1)=77;next_drain();
        CHECK(!cancellation&&!t_settled&&!t_failed&&!t_own_undo&&value(t_live.data(),1)==77);
    }
    // A current canonical failure is explicit; the next native gesture cannot
    // overtake the pending finalizer or clear its ticket.
    reset();begin();native_cancel();const auto first=cancellation->ticket;start(t_data.data());CHECK(cancellation->ticket==first);
    skg::field<void*>(t_history.data(),0x24)=nullptr;skg::field<uint8_t>(t_history.data(),0x20)=1;
    t_callback_fail=true;next_drain();CHECK(t_settled==1&&t_failed==1&&!cancellation&&t_failure.find("canonical")!=std::string::npos);
    reset();begin();t_alloc_fail=true;native_cancel();next_drain();CHECK(t_failed==1&&!t_settled&&!t_restores);
    // Monotonic gesture and cancellation tickets differ even in the same native
    // object and logical epoch. An old deferred ticket cannot match a new one.
    reset();begin();native_cancel();auto old=cancellation->ticket;next_drain();begin();native_cancel();
    CHECK(cancellation->ticket.gesture>old.gesture&&cancellation->ticket.serial>old.serial);next_drain();CHECK(t_settled==2);
    reset();CHECK(t_allocations.empty());
    std::printf("editor_native_cancellation_windows_tests: %u checks passed\n",checks);
}
