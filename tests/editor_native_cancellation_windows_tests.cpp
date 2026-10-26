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
std::map<void*,std::vector<uint8_t>> t_rows;
std::set<void*> t_allocations;
std::deque<std::pair<SkTaskFn,void*>> t_tasks;
void* t_mode_pointer=t_mode.data();
void* t_context_pointer=t_context.data();
void* t_loading_pointer=t_loading.data();
void* t_tool_pointer=&t_tool_pointer;
bool t_active=true,t_restore_fail=false,t_callback_fail=false,t_alloc_fail=false;
uint64_t t_frame=10;
unsigned t_restores=0,t_original_undo=0,t_own_undo=0,t_settled=0,t_failed=0;
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
    CHECK(owner==t_data.data());
    auto& rows=t_rows[t_live.data()];
    for(unsigned i=0;i<2;++i)if(marker==rows.data()+i*0x238+4)return t_actors[i].data();
    return nullptr;
}
void* SK_THISCALL t_marker(void* owner,void* actor){
    CHECK(owner==t_data.data());
    for(unsigned i=0;i<2;++i)if(actor==t_actors[i].data())return t_rows[t_live.data()].data()+i*0x238+4;
    return nullptr;
}
void SK_THISCALL t_select(void* tools,void* actor){CHECK(tools==t_tool_pointer);skg::field<void*>(t_context.data(),4)=actor;}
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
    if(gesture_before_resource){release(gesture_before_resource);gesture_before_resource=nullptr;}
    for(auto held:gesture_actors)release(held.object);
    gesture_actors.clear();gesture_before_bindings.clear();
    CHECK(t_allocations.empty());t_rows.clear();t_tasks.clear();
    t_mode.fill(0);t_data.fill(0);t_live.fill(0);t_history.fill(0);t_context.fill(0);
    t_display.fill(0);t_layout.fill(0);t_container.fill(0);t_loading.fill(0);
    t_entry.fill(0);t_other_entry.fill(0);t_other_data.fill(0);
    for(auto& actor:t_actors){actor.fill(0);skg::field<void*>(actor.data(),0)=t_actor_vt.data();skg::field<int>(actor.data(),4)=1;}
    t_resource_vt[0]=reinterpret_cast<void*>(t_addref);t_resource_vt[1]=reinterpret_cast<void*>(t_release);
    t_actor_vt=t_resource_vt;
    t_mode_pointer=t_mode.data();t_context_pointer=t_context.data();t_loading_pointer=t_loading.data();
    t_active=true;t_callback_fail=t_alloc_fail=t_restore_fail=false;
    t_frame=10;t_restores=t_original_undo=t_own_undo=t_settled=t_failed=0;t_failure.clear();t_seen={};
    t_scope={};t_scope.project[0]=0xAD;t_scope.epoch=9;t_scope.author={1,3};
    a_mode=uintptr_t(&t_mode_pointer);a_context=uintptr_t(&t_context_pointer);a_loading=uintptr_t(&t_loading_pointer);
    a_tools=uintptr_t(&t_tool_pointer);a_history=uintptr_t(t_history.data());a_entry_vtable=uintptr_t(t_entry_vt.data());
    a_object=uintptr_t(t_object);a_marker=uintptr_t(t_marker);a_copy=uintptr_t(t_copy);a_ctor=uintptr_t(t_ctor);
    a_alloc=uintptr_t(t_allocate);a_restore=uintptr_t(t_restore);a_display_vtable=0;
    original_start=t_start;original_undo=t_undo;original_select=t_select;
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
    bindings.clear();incarnations.clear();selection.clear();pending.clear();
    static SkHost host{};host.frame_index=t_get_frame;host.run_outside_frame=t_outside;H=&host;
    C={};C.active=[](){return t_active;};C.gesture_scope=[](){return t_scope;};
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
}
namespace game { uint32_t mode_id(){return modes::kScenario;} }
namespace sui {
void set_enabled(Win,bool){CHECK(false);} void release(Win){CHECK(false);} void add_ref(Win){CHECK(false);}
uint32_t control_id(Win){CHECK(false);return 0;} uint32_t flags(Win){CHECK(false);return 0;}
std::vector<Win> children(Win){CHECK(false);return {};}
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
int main(){
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
