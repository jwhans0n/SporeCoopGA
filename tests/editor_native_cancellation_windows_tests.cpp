// Production native adapter, actual i686 ABI, controlled exact-layout objects.
// This proves hook routing and ownership; it is not a running-game result.
#include "../mods/coop/editor_native.cpp"
#include <cstdlib>
#include <cstdio>

static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}} while(0)
namespace {
using namespace editor_native;
std::array<uint8_t,0xE0> t_mode;
std::array<uint8_t,0x128> t_data,t_other_data;
std::array<uint8_t,0x2CB0> t_live;
std::array<uint8_t,0x28> t_history,t_context,t_display;
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
editor::Revision t_revision=0;
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
    held_tool.reset();held_import.reset();held_creation.reset();held_class_removal.reset();held_captain_asset.reset();held_crew_action.reset();
    for(auto item:pending){release(item.entry);release(item.before);}pending.clear();
    if(gesture_before_resource){release(gesture_before_resource);gesture_before_resource=nullptr;}
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
    t_frame=10;t_time=1;t_presses=t_finishes=t_ticks=t_steps=0;t_granted=false;t_revision=0;
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
        speech_apply();CHECK(pending.size()==1&&t_allocations.size()==1&&state().gesture);
        CHECK(value(pending.front().before,0)==1&&pending.front().speech==ui.data());
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
namespace game { uint32_t mode_id(){return modes::kScenario;} void destroy_creature(void* actor){CHECK(actor==t_ghost_actor.data()||actor==t_ghost_exit.data());++t_ghosts_destroyed;skg::field<bool>(actor,0x20)=true;t_release(actor);} }
namespace sui {
void set_enabled(Win,bool){CHECK(false);} void release(Win){CHECK(false);} void add_ref(Win){CHECK(false);}
uint32_t control_id(Win w){CHECK(w==&t_card_id||w==&t_text);return w==&t_text?t_text_id:t_card_id;} uint32_t flags(Win){CHECK(false);return 0;}
std::vector<Win> children(Win){CHECK(false);return {};}
Win parent(Win){CHECK(false);return nullptr;}bool visible(Win){CHECK(false);return false;}
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
void* SK_CDECL v_get_manager(){++v_gets;return &v_manager;}
void* SK_THISCALL v_get_cameras(void* self){CHECK(self==&v_manager);return &v_cameras;}
void* SK_THISCALL v_get_active(void* self){CHECK(self==&v_cameras);return v_active;}
void* SK_THISCALL v_get_named(void* self,uint32_t id){CHECK(self==&v_cameras);CHECK(id==0x00EBB801);return v_named;}
void* SK_THISCALL v_get_cast(void* self,uint32_t id){CHECK(self==v_active);CHECK(id==0x011966ED);return v_cast?self:nullptr;}
const float* SK_THISCALL v_target(void* self){
    static std::array<float,3> target;
    for(unsigned i=0;i<3;++i)target[i]=skg::field<float>(self,0x54+4*i)*skg::field<float>(self,0x28);
    return target.data();
}
void SK_THISCALL v_orbit(void* self,float* yaw,float* pitch,float* distance){
    *yaw=skg::field<float>(self,0x154);*pitch=skg::field<float>(self,0x130);*distance=skg::field<float>(self,0x10C);
}
void SK_THISCALL v_look(void* self,const float* point,const float* quaternion,bool instant){
    CHECK(self==v_active);CHECK(instant);++v_looks;
    if(v_refuse)return;
    const float radius=std::sqrt(point[0]*point[0]+point[1]*point[1]+point[2]*point[2]);
    for(unsigned i=0;i<3;++i)skg::field<float>(self,0x54+4*i)=point[i]/radius;
    skg::field<float>(self,0x28)=radius;
    std::memcpy(skg::ptr_add<void>(self,0xAC),quaternion,sizeof(float)*4);
}
void SK_THISCALL v_set(void* self,float yaw,float pitch,float distance){
    CHECK(self==v_active);++v_sets;if(v_refuse)return;
    skg::field<float>(self,0x154)=skg::field<float>(self,0x15C)=yaw;
    skg::field<float>(self,0x130)=skg::field<float>(self,0x138)=pitch;
    skg::field<float>(self,0x10C)=skg::field<float>(self,0x114)=distance;
}
void view_fixture(){
    reset();v_camera.fill(0);v_replacement.fill(0);v_manager_vt.fill(nullptr);v_cameras_vt.fill(nullptr);v_camera_vt.fill(nullptr);
    v_manager=v_manager_vt.data();v_cameras=v_cameras_vt.data();v_active=v_named=v_camera.data();
    v_cast=true;v_refuse=false;v_gets=v_looks=v_sets=0;
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
        CHECK(raw==live_before && t_pose_row_copies==2 && t_pose_inserts==4); // Only opaque comparison rebuilds rows.
        auto unsupported=prepared;unsupported.bytes[12+4+0x100]^=1;Gesture invalid;invalid.before=base;invalid.after=unsupported;
        CHECK(!supported_changes(invalid,error)); // Pose optimization cannot admit an unrelated opaque marker mutation.
        CHECK(prepare(base,next,change,nullptr,prepared,error));CHECK(raw==live_before);
        auto forged=base;forged.bytes[0]^=1;CHECK(!validate(forged,base.document,error));CHECK(!prepare(forged,next,change,nullptr,prepared,error));
        forged=base;forged.bindings[0].marker=9999;CHECK(!validate(forged,base.document,error));
        forged=base;forged.act=1;CHECK(!validate(forged,base.document,error));
        auto wrong=base.document;wrong.placements.at(id).position.x=99;CHECK(!validate(base,wrong,error));
        const auto reads=t_reads;CHECK(validate(base,base.document,error));CHECK(t_reads==reads+1);
        ++generation;CHECK(validate(base,base.document,error));CHECK(t_reads==reads+2);
        {ValidationScope nested;CHECK(validate(base,base.document,error));CHECK(t_reads==reads+2);}
    }
    CHECK(!validated_snapshot && !validation_depth);const auto reads=t_reads;CHECK(validate(base,base.document,error));CHECK(t_reads==reads+1);
    a_write=old_write;a_read=old_read;a_copy_marker_rows=old_copy;a_insert_marker=old_insert;
    reset();CHECK(t_allocations.empty());
}
int main(){
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
    test_local_view();
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
