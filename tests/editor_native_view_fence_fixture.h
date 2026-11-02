#include <cstdarg>
namespace view_fence_fixture {
DeferredLocalView pin(const LocalView& view){
    void* m=mode();void* d=data(m);void* r=resource(d);
    return {view,value_scope(),generation,m,d,r,skg::field<editor::ResourceKey>(r,8)};
}
std::vector<std::string> rotation_logs;
void log_rotation(SkMod*,int level,const char* format,...){
    CHECK(level==SK_DEBUG);char line[1024];va_list args;va_start(args,format);
    std::vsnprintf(line,sizeof line,format,args);va_end(args);rotation_logs.emplace_back(line);
}
void test_quaternion_diagnostics(){
    reset();Snapshot snapshot;
    auto& marker_rows=t_rows[t_live.data()];marker_rows.resize(12*0x238);rows_for(t_live.data());
    for(uint32_t i=0;i<12;++i){
        const editor::ObjectId id{uint64_t(i)+1,1};editor::Placement placement;
        placement.class_id={(2ull<<32)|7,1};placement.rotation={-.588576674f,-.170447186f,.266058296f,.744144559f};
        snapshot.document.placements[id]=placement;snapshot.bindings.push_back({id,i});
        auto* marker=marker_rows.data()+i*0x238+4;skg::field<uint32_t>(marker,0)=7;
        skg::field<editor::Rotation>(marker,0x10)=placement.rotation;
    }
    const auto* previous=H;SkHost host=*H;host.log=log_rotation;H=&host;rotation_logs.clear();
    const auto reads=t_reads,writes=t_writes;
    log_restore_quaternions(snapshot,t_live.data(),"parsed-before-restore");CHECK(rotation_logs.empty());
    for(uint32_t i=0;i<12;++i)skg::field<uint32_t>(marker_rows.data()+i*0x238+4,0x10)^=1;
    const auto changed=marker_rows;
    log_restore_quaternions(snapshot,t_live.data(),"after-restore");
    CHECK(rotation_logs.size()==8&&rotation_logs.front().find("stage=after-restore object=1:1 marker=0 actual=")!=std::string::npos);
    CHECK(rotation_logs.front().find(" canonical=")!=std::string::npos);
    CHECK(t_reads==reads&&t_writes==writes&&marker_rows==changed);
    H=previous;reset();
}
void test_fenced_view(){
    std::string error;LocalView pinned;SceneOwner owner;
    auto setup=[&]{
        view_fixture();CHECK(capture_view(pinned,error)&&capture_scene_owner(owner,error));
        C.projection_scope=[] {return t_scope;};C.gesture_scope=[] {return GestureScope{};};
        v_gets=v_looks=v_sets=0;skg::field<float>(v_camera.data(),0x130)=0;
    };
    setup();CHECK(restore_view_fenced(pinned,generation,owner,t_scope,[]{return true;},error));
    CHECK(v_looks==1&&v_sets==1&&C.gesture_scope()==GestureScope{});
    setup();CHECK(!restore_view_fenced(pinned,generation,owner,t_scope,{},error)&&!v_gets&&!v_looks&&!v_sets);
    setup();CHECK(!restore_view_fenced(pinned,generation,owner,t_scope,[]{return false;},error)&&!v_gets&&!v_looks&&!v_sets);
    setup();CHECK(!restore_view_fenced(pinned,generation,owner,{},[]{return true;},error)&&!v_gets&&!v_looks&&!v_sets);
    // The caller's optional hold and its view can be destroyed inside the first
    // native getter. Only the value copy may be consumed by later callbacks.
    setup();std::optional<LocalView> holder=pinned;bool retired=false;
    v_callback=[&](unsigned stage){if(stage==0){holder->target={};holder.reset();retired=true;}};
    CHECK(restore_view_fenced(*holder,generation,owner,t_scope,[]{return true;},error)&&retired&&!holder);
    v_callback={};LocalView restored;CHECK(capture_view(restored,error)&&same_view(pinned,restored));
    for(unsigned stage=0;stage<9;++stage)for(unsigned reason=0;reason<12;++reason){
        setup();const auto expected=generation;const auto scope=t_scope;bool fired=false,allowed=true;
        v_callback=[&](unsigned at){if(at!=stage || fired)return;fired=true;
            if(reason==0)++generation;if(reason==1)++t_scope.project[0];if(reason==2)++t_scope.epoch;
            if(reason==3)++t_scope.author.slot;if(reason==4)++t_scope.author.generation;
            if(reason==5)C.projection_scope=[] {return GestureScope{};};
            if(reason==6)skg::field<void*>(t_mode.data(),0x74)=t_other_data.data();
            if(reason==7)t_mode_pointer=nullptr;
            if(reason==8)skg::field<void*>(t_data.data(),0x10)=t_other_data.data();
            if(reason==9)++skg::field<editor::ResourceKey>(t_live.data(),8).instance;
            if(reason==10)allowed=false;
            if(reason==11)C.projection_scope=[] {throw 7;return GestureScope{};};
        };
        CHECK(!restore_view_fenced(pinned,expected,owner,scope,[&]{return allowed;},error)&&fired&&!error.empty());
        CHECK(v_looks==(stage>=7?1u:0u)&&v_sets==(stage>=8?1u:0u));
    }
    setup();CHECK(!restore_view_fenced(pinned,generation,owner,t_scope,[]{throw 7;return true;},error)&&!v_looks&&!v_sets);
    // Deferred settlement uses the same read identity while editing authority
    // is empty, and refuses replacement projects/owners before camera writes.
    for(unsigned reason=0;reason<12;++reason){
        setup();deferred_local_view=pin(pinned);
        if(reason==1)++t_scope.project[0];if(reason==2)++t_scope.epoch;
        if(reason==3)++t_scope.author.slot;if(reason==4)++t_scope.author.generation;
        if(reason==5)C.projection_scope=[] {return GestureScope{};};
        if(reason==6)++generation;if(reason==7)t_mode_pointer=nullptr;
        if(reason==8)skg::field<void*>(t_mode.data(),0x74)=t_other_data.data();
        if(reason==9)skg::field<void*>(t_data.data(),0x10)=t_other_data.data();
        if(reason==10)++skg::field<editor::ResourceKey>(t_live.data(),8).instance;
        if(reason==11)C.projection_scope=[] {throw 7;return GestureScope{};};
        drain_local_view(nullptr);CHECK(!deferred_local_view&&v_looks==(reason==0?1u:0u)&&v_sets==(reason==0?1u:0u));
    }
    setup();deferred_local_view=pin(pinned);t_terraform.fill(0);v_terrain.fill(0);
    skg::field<void*>(t_mode.data(),0x18)=t_terraform.data();skg::field<void*>(t_terraform.data(),0x190)=v_terrain.data();
    a_terrain_vtable=0x12345678;skg::field<uintptr_t>(v_terrain.data(),0)=a_terrain_vtable;
    a_terrain_loaded=uintptr_t(v_terrain_loaded);terrain_rebuilding=true;terrain_rebuild_frame=t_frame++;
    v_callback=[](unsigned stage){if(stage==9)++t_scope.epoch;};
    drain_local_view(nullptr);CHECK(!deferred_local_view&&!v_looks&&!v_sets);
    v_callback={};reset();
}
}
static void test_native_view_fences(){view_fence_fixture::test_fenced_view();view_fence_fixture::test_quaternion_diagnostics();}
