#include <cstdarg>
namespace view_fence_fixture {
void test_settled_scene_owner(){
    std::string error;SceneOwner owner;
    creation_camera_fixture();CHECK(capture_scene_owner(owner,error));
    auto scene=creation_scene_state(owner);CHECK(scene.settled()&&!v_looks&&!v_sets);
    for(unsigned reason=0;reason<5;++reason){
        creation_camera_fixture();CHECK(capture_scene_owner(owner,error));
        const auto generation_before=generation;bool fired=false;
        v_callback=[&](unsigned stage){if(stage!=10)return;fired=true;
            if(reason==0)++generation;
            if(reason==1)t_mode_pointer=nullptr;
            if(reason==2)skg::field<void*>(t_mode.data(),0x74)=t_other_data.data();
            if(reason==3)skg::field<void*>(t_data.data(),0x10)=t_other_data.data();
            if(reason==4)++skg::field<editor::ResourceKey>(t_live.data(),8).instance;
        };
        scene=creation_scene_state(owner);
        CHECK(fired&&!scene.owner&&!scene.ready&&!scene.settled()&&scene.generation==generation_before);
        CHECK(!v_looks&&!v_sets);
    }
    // capture_scene_owner must leave its caller's pin unchanged when its final
    // state getter replaces the resource without changing native generation.
    for(unsigned reason=0;reason<5;++reason){
        creation_camera_fixture();unsigned callbacks=0;
        SceneOwner untouched{1,2,3,{4,5,6}},captured=untouched;
        v_callback=[&](unsigned stage){if(stage!=11 || ++callbacks!=2)return;
            if(reason==0)++generation;
            if(reason==1)t_mode_pointer=nullptr;
            if(reason==2)skg::field<void*>(t_mode.data(),0x74)=t_other_data.data();
            if(reason==3)skg::field<void*>(t_data.data(),0x10)=t_other_data.data();
            if(reason==4)++skg::field<editor::ResourceKey>(t_live.data(),8).instance;
        };
        CHECK(!capture_scene_owner(captured,error)&&callbacks==2&&captured==untouched&&!error.empty());
        CHECK(!v_looks&&!v_sets);
    }
    v_callback={};reset();
}
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
void test_settings_diagnostics(){
    reset();Snapshot snapshot;editor::AuthoringSettings captain;
    captain.object=kCaptainObject;captain.kind=editor::SettingsKind::Captain;
    captain.properties[kCaptainPositionProperty]=editor::Vec3{0,1,0};
    captain.properties[kCaptainRotationProperty]=editor::Rotation{0,0,0,1};
    captain.properties[kCaptainScaleProperty]=1.f;
    snapshot.document.settings.emplace(captain.object,captain);
    const auto* previous=H;SkHost host=*H;host.log=log_rotation;H=&host;rotation_logs.clear();
    const auto raw=t_live;const auto before=t_rows[t_live.data()];const auto reads=t_reads,writes=t_writes;
    log_restore_quaternions(snapshot,t_live.data(),"after-terrain-rebuild");
    CHECK(rotation_logs.size()==3);
    CHECK(rotation_logs[0].find("stage=after-terrain-rebuild object=")!=std::string::npos);
    CHECK(rotation_logs[1].find("canonical=00000000,00000000,00000000,3F800000")!=std::string::npos);
    Snapshot projected=snapshot;projected.document.settings.at(kCaptainObject).properties[kCaptainRotationProperty]=editor::Rotation{0,0,0,0};
    rotation_logs.clear();std::string error;
    CHECK(!validate_projection(projected,snapshot,snapshot.document,error)&&rotation_logs.size()==1);
    CHECK(rotation_logs[0].find("stage=validation object=")!=std::string::npos&&rotation_logs[0].find("3F800000")!=std::string::npos);
    projected.document.settings.at(kCaptainObject).properties.clear();snapshot.document.settings.at(kCaptainObject).properties.clear();
    for(uint32_t i=0;i<20;++i){projected.document.settings.at(kCaptainObject).properties[i]=float(i);
        snapshot.document.settings.at(kCaptainObject).properties[i]=float(i+1);}
    rotation_logs.clear();CHECK(!validate_projection(projected,snapshot,snapshot.document,error)&&rotation_logs.size()==8);
    CHECK(t_live==raw&&t_rows[t_live.data()]==before&&t_reads==reads&&t_writes==writes);
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
static void test_native_view_fences(){
    view_fence_fixture::test_settled_scene_owner();view_fence_fixture::test_fenced_view();view_fence_fixture::test_quaternion_diagnostics();
    view_fence_fixture::test_settings_diagnostics();
}

namespace restore_fence_fixture {
std::function<void(unsigned)> callback;
std::set<void*> entries;
std::array<void*,8> entry_vtable;
std::array<uint8_t,0x1C> script;
std::array<uint8_t,0x24> settings;
std::array<uint8_t,0x18> inspector_layout;
unsigned writes=0,acts=0,inspector_opens=0,selections=0,entry_destroys=0;
unsigned text_writes=0,caption_writes=0;
bool inspector_visible=false,fail_allocation=false;
void event(unsigned stage){if(callback)callback(stage);}
int SK_THISCALL addref_entry(void* entry){return ++skg::field<int>(entry,8);}
int SK_THISCALL release_entry(void* entry){
    const auto refs=--skg::field<int>(entry,8);CHECK(refs>=0);
    if(!refs){CHECK(entries.erase(entry)==1);t_release(skg::field<void*>(entry,0x10));
        t_release(skg::field<void*>(entry,0x14));std::free(entry);++entry_destroys;}
    return refs;
}
void* SK_CDECL allocate(size_t size,const char* name,int a,int b,int c,int d){
    if(size!=0x18)return t_allocate(size,name,a,b,c,d);
    event(22);if(fail_allocation)return nullptr;
    auto* entry=std::calloc(1,size);CHECK(entry);entries.insert(entry);return entry;
}
void* SK_THISCALL construct_entry(void* entry,void* owner,void* before,void* after){
    CHECK(entries.contains(entry)&&owner==t_data.data());
    skg::field<void*>(entry,0)=entry_vtable.data();skg::field<void*>(entry,0xC)=owner;
    skg::field<void*>(entry,0x10)=before;skg::field<void*>(entry,0x14)=after;t_addref(before);t_addref(after);
    event(23);return entry;
}
void SK_THISCALL restore_entry(void* entry,void* image){
    CHECK(entries.contains(entry)&&skg::field<void*>(entry,0xC)==t_data.data());
    ++writes;t_copy(t_live.data(),image);skg::field<void*>(t_context.data(),4)=nullptr;
    inspector_visible=false;event(24);
}
void SK_CDECL select_act(uint32_t act){++acts;skg::field<uint32_t>(t_context.data(),0)=act;event(25);}
void SK_THISCALL select(void* tools,void* actor){++selections;t_select(tools,actor);event(26);}
bool SK_THISCALL shown(void* layout){
    CHECK(layout==t_layout.data()||layout==inspector_layout.data());
    event(layout==inspector_layout.data()?20:11);
    return layout==t_layout.data()||inspector_visible;
}
void SK_THISCALL open_inspector(void* self,uint32_t asset){
    CHECK(self==settings.data()&&asset==1024);++inspector_opens;inspector_visible=true;
}
bool SK_THISCALL read_image(void* image,Stream* stream){const bool okay=t_pose_read(image,stream);event(21);return okay;}
void* SK_THISCALL assign_text(void* header,const char16_t* first,const char16_t* last){
    CHECK(first==last);skg::field<void*>(header,0)=nullptr;skg::field<void*>(header,4)=nullptr;
    ++text_writes;event(27);return header;
}
uintptr_t symbol(const char* name){
    CHECK(std::string(name)=="SporeCoop::Editor::AssignMetadataText");event(30);return uintptr_t(assign_text);
}
void* SK_CDECL get_messages(){event(28);return &v_messages;}
void SK_THISCALL send_caption(void* self,uint32_t message,void* packet,void*){
    CHECK(self==&v_messages&&message==0x7AA519DC&&skg::field<void*>(packet,0)==t_data.data()+4);
    ++caption_writes;event(29);
}
struct Routes {
    RecentRoutes recent;
    uintptr_t allocator=a_alloc,entry=a_entry,restore=a_restore,shown_route=a_layout_shown,inspector=a_inspector_open;
    uintptr_t message_manager=a_metadata_message_manager,message_send=a_metadata_message_send;
    SelectActFn act=original_select_act;SelectFn select_route=original_select;
    ~Routes(){
        callback={};v_callback={};CHECK(entries.empty());reset();
        a_alloc=allocator;a_entry=entry;a_restore=restore;a_layout_shown=shown_route;a_inspector_open=inspector;
        a_metadata_message_manager=message_manager;a_metadata_message_send=message_send;
        original_select_act=act;original_select=select_route;
    }
};
Snapshot setup(SceneOwner& owner){
    CHECK(entries.empty());callback={};view_fixture();auto snapshot=recent_pose_setup();snapshot.metadata.reset();
    C.projection_scope=[] {return t_scope;};C.gesture_scope=[] {return GestureScope{};};
    script.fill(0);settings.fill(0);inspector_layout.fill(0);
    skg::field<void*>(t_display.data(),0x18)=script.data();skg::field<void*>(script.data(),0x18)=settings.data();
    skg::field<void*>(settings.data(),0x10)=inspector_layout.data();skg::field<uint32_t>(settings.data(),0x20)=1024;
    inspector_visible=true;writes=acts=inspector_opens=selections=entry_destroys=text_writes=caption_writes=0;fail_allocation=false;
    entry_vtable.fill(nullptr);entry_vtable[0]=reinterpret_cast<void*>(addref_entry);entry_vtable[1]=reinterpret_cast<void*>(release_entry);
    a_alloc=uintptr_t(allocate);a_entry=uintptr_t(construct_entry);a_restore=uintptr_t(restore_entry);
    a_layout_shown=uintptr_t(shown);a_inspector_open=uintptr_t(open_inspector);a_read=uintptr_t(read_image);
    original_select_act=select_act;original_select=select;
    v_callback=[](unsigned stage){event(stage);};
    std::string error;CHECK(capture_scene_owner(owner,error));v_gets=v_looks=v_sets=0;
    return snapshot;
}
void metadata(Snapshot& snapshot){
    snapshot.metadata=Snapshot::Metadata{};
    static SkHost host;host=*H;host.sym=symbol;H=&host;
    v_message_vt.fill(nullptr);v_messages=v_message_vt.data();v_message_vt[0x14/4]=reinterpret_cast<void*>(send_caption);
    a_metadata_message_manager=uintptr_t(get_messages);a_metadata_message_send=uintptr_t(send_caption);
}
void expire(unsigned reason,bool& allowed){
    if(reason==0)++generation;
    if(reason==1)++t_scope.project[0];
    if(reason==2)++t_scope.epoch;
    if(reason==3)++t_scope.author.slot;
    if(reason==4)++t_scope.author.generation;
    if(reason==5)C.projection_scope=[] {return GestureScope{};};
    if(reason==6)t_mode_pointer=nullptr;
    if(reason==7)skg::field<void*>(t_mode.data(),0x74)=t_other_data.data();
    if(reason==8)skg::field<void*>(t_data.data(),0x10)=t_other_data.data();
    if(reason==9)++skg::field<editor::ResourceKey>(t_live.data(),8).instance;
    if(reason==10)allowed=false;
    if(reason==11)C.projection_scope=[] {throw 7;return GestureScope{};};
}
void test(){
    Routes routes;std::string error;SceneOwner owner;
    auto snapshot=setup(owner);const auto unchanged=t_rows[t_live.data()];
    CHECK(restore_fenced(snapshot,generation,owner,t_scope,[]{return true;},error));
    CHECK(writes==1&&acts==1&&selections==1&&inspector_opens==1&&entry_destroys==1);
    CHECK(v_looks==1&&v_sets==1&&entries.empty()&&t_allocations.empty()&&t_rows[t_live.data()]==unchanged&&!applying);
    // Internal getter and private-allocation callbacks must never re-pin a new
    // source/owner, even when its native generation and data address survive.
    for(unsigned stage:{0u,1u,2u,3u,4u,5u,6u,11u,20u,21u,22u,23u})for(unsigned reason=0;reason<12;++reason){
        snapshot=setup(owner);const auto before=t_rows[t_live.data()];const auto expected=generation;const auto scope=t_scope;
        bool fired=false,allowed=true;callback=[&](unsigned at){if(at==stage&&!fired){fired=true;expire(reason,allowed);}};
        bool accepted=false;
        try{accepted=restore_fenced(snapshot,expected,owner,scope,[&]{return allowed;},error);}
        catch(...){std::printf("restore callback escaped: stage=%u reason=%u\n",stage,reason);std::fflush(stdout);throw;}
        CHECK(!accepted&&fired&&!error.empty());
        CHECK(!writes&&!acts&&!selections&&!inspector_opens&&!v_looks&&!v_sets);
        CHECK(entries.empty()&&t_allocations.empty()&&t_rows[t_live.data()]==before&&!applying);
    }
    // A mutation that expires the pin may finish its own original Native call;
    // no later act, selection, inspector, or camera mutation may follow it.
    for(unsigned stage:{24u,25u,26u})for(unsigned reason=0;reason<12;++reason){
        snapshot=setup(owner);const auto expected=generation;const auto scope=t_scope;bool fired=false,allowed=true;
        callback=[&](unsigned at){if(at==stage&&!fired){fired=true;expire(reason,allowed);}};
        CHECK(!restore_fenced(snapshot,expected,owner,scope,[&]{return allowed;},error)&&fired&&!error.empty());
        CHECK(writes==1&&acts==(stage>=25?1u:0u)&&selections==(stage>=26?1u:0u));
        CHECK(!inspector_opens&&!v_looks&&!v_sets&&entries.empty()&&t_allocations.empty()&&!applying);
    }
    snapshot=setup(owner);fail_allocation=true;
    CHECK(!restore_fenced(snapshot,generation,owner,t_scope,[]{return true;},error));
    CHECK(!writes&&entries.empty()&&t_allocations.empty()&&!applying);
    snapshot=setup(owner);CHECK(!restore_fenced(snapshot,generation,owner,t_scope,{},error)&&!v_gets&&!writes);
    snapshot=setup(owner);CHECK(!restore_fenced(snapshot,generation,owner,{},[]{return true;},error)&&!v_gets&&!writes);
    snapshot=setup(owner);CHECK(!restore_fenced(snapshot,generation,owner,t_scope,[]{throw 7;return true;},error)&&!v_gets&&!writes);
    // A callback can also throw after NativeRead has allocated its private
    // image. The overload must release it and keep its explicit error boundary.
    snapshot=setup(owner);bool threw=false;
    callback=[&](unsigned stage){if(stage==21){threw=true;throw 7;}};
    CHECK(!restore_fenced(snapshot,generation,owner,t_scope,[]{return true;},error)&&threw);
    CHECK(!writes&&entries.empty()&&t_allocations.empty()&&!applying&&error.find("callback")!=std::string::npos);
    snapshot=setup(owner);metadata(snapshot);
    CHECK(restore_fenced(snapshot,generation,owner,t_scope,[]{return true;},error)&&text_writes==3&&caption_writes==1&&writes==1);
    for(unsigned stage:{27u,28u,29u,30u})for(unsigned reason=0;reason<11;++reason){
        snapshot=setup(owner);metadata(snapshot);const auto expected=generation;const auto scope=t_scope;bool fired=false,allowed=true;
        callback=[&](unsigned at){if(at==stage&&!fired){fired=true;expire(reason,allowed);}};
        CHECK(!restore_fenced(snapshot,expected,owner,scope,[&]{return allowed;},error)&&fired&&!error.empty());
        CHECK(text_writes==(stage==30?0u:stage==27?1u:3u)&&writes==(stage>=28&&stage!=30?1u:0u));
        CHECK(caption_writes==(stage==29?1u:0u)&&entries.empty()&&t_allocations.empty()&&!applying);
        CHECK(!inspector_opens&&!v_looks&&!v_sets);
    }
    // The caller may destroy its optional snapshot inside a Native getter.
    snapshot=setup(owner);std::optional<Snapshot> held=snapshot;bool retired=false;
    callback=[&](unsigned stage){if(stage==0&&!retired){held->bytes.clear();held->document={};held.reset();retired=true;}};
    CHECK(restore_fenced(*held,generation,owner,t_scope,[]{return true;},error)&&retired&&!held&&writes==1);
    CHECK(entries.empty()&&t_allocations.empty()&&!applying);
    callback={};v_callback={};
}
}
static void test_native_restore_fences(){restore_fence_fixture::test();}

namespace terrain_rotation_fixture {
unsigned refreshes=0;bool missing_object=false,missing_spatial=false,repeat_drift=false;
std::function<void(unsigned)> callback;
void event(unsigned stage){if(callback)callback(stage);}
void* SK_THISCALL object(void* owner,void* marker){auto* result=t_object(owner,marker);event(0);return missing_object?nullptr:result;}
void* SK_THISCALL cast(void* actor,uint32_t type){auto* result=t_cast(actor,type);event(1);return missing_spatial?nullptr:result;}
void SK_THISCALL apply(void* owner,void* marker,uint32_t flags){
    CHECK(owner==t_data.data()&&!flags&&applying);++refreshes;
    const auto first=t_rows[t_live.data()].data()+4;const unsigned index=marker==first?0:1;
    CHECK(marker==first+index*0x238);
    t_model_rotation(t_models[index].data(),&skg::field<editor::Rotation>(marker,0x10));
    skg::field<float>(marker,0x1E0)+=1;skg::field<float>(marker,0x1E4)+=2;
    if(repeat_drift)skg::field<uint32_t>(marker,0x10)^=1;
    event(2);
}
struct Routes {
    restore_fence_fixture::Routes restore;
    uintptr_t getter=a_object;ApplyMarkerFn apply_route=original_apply_marker;
    ~Routes(){callback={};a_object=getter;original_apply_marker=apply_route;applying=false;}
};
Snapshot setup(SceneOwner& scene){
    auto snapshot=restore_fence_fixture::setup(scene);
    t_actor_vt[3]=reinterpret_cast<void*>(cast);a_object=uintptr_t(object);original_apply_marker=apply;
    C.extended_poses_tracked=[] {return true;};
    refreshes=0;missing_object=missing_spatial=repeat_drift=false;callback={};
    for(unsigned i=0;i<2;++i){auto* marker=t_rows[t_live.data()].data()+i*0x238+4;
        skg::field<float>(marker,0x1E0)=.8f;skg::field<float>(marker,0x1E4)=.4f;}
    return snapshot;
}
void drift(){for(unsigned i=0;i<2;++i)skg::field<uint32_t>(t_rows[t_live.data()].data()+i*0x238+4,0x10)^=1;}
void test(){
    Routes routes;SceneOwner scene;std::string error;TerrainRotationOwner pin;std::vector<TerrainRotation> rotations;
    auto snapshot=setup(scene);RestoreFence fence{generation,scene,t_scope,[]{return true;}};
    CHECK(pin_terrain_rotations(snapshot,&fence,pin,rotations,error)&&rotations.size()==2);
    const auto before=t_rows[t_live.data()];drift();applying=true;
    CHECK(restore_terrain_rotations(pin,rotations,error)&&refreshes==2&&t_rows[t_live.data()]==before);
    for(unsigned i=0;i<2;++i)CHECK(skg::field<editor::Rotation>(t_models[i].data(),0x10)==snapshot.document.placements.at(snapshot.bindings[i].object).rotation);
    applying=false;
    // Never adopt a rotation that already differed before the rebuild.
    snapshot=setup(scene);rotations.clear();drift();const auto mismatched=t_rows[t_live.data()];
    CHECK(pin_terrain_rotations(snapshot,nullptr,pin,rotations,error)&&rotations.empty());
    applying=true;CHECK(restore_terrain_rotations(pin,rotations,error)&&!refreshes&&t_rows[t_live.data()]==mismatched);applying=false;
    for(unsigned stage=0;stage<3;++stage)for(unsigned reason=0;reason<14;++reason){
        snapshot=setup(scene);rotations.clear();bool allowed=true,fired=false;
        fence={generation,scene,t_scope,[&]{return allowed;}};
        CHECK(pin_terrain_rotations(snapshot,&fence,pin,rotations,error));drift();const auto changed=t_rows[t_live.data()];
        callback=[&](unsigned at){if(at!=stage||fired)return;fired=true;
            if(reason<12)restore_fence_fixture::expire(reason,allowed);
            if(reason==12)skg::field<uint32_t>(t_rows[t_live.data()].data()+4,0)=1025;
            if(reason==13){auto replacement=t_rows[t_live.data()];t_rows[t_live.data()].swap(replacement);rows_for(t_live.data());}
        };
        applying=true;CHECK(!restore_terrain_rotations(pin,rotations,error)&&fired&&!error.empty());applying=false;
        CHECK(refreshes==(stage==2?1u:0u));
        if(stage<2)CHECK(skg::field<uint32_t>(t_rows[t_live.data()].data()+4,0x10)==skg::field<uint32_t>(const_cast<uint8_t*>(changed.data())+4,0x10));
        // A sound object's Native refresh may dirty its source audio, but the
        // adapter must not write it back after source/read identity expired.
        if(stage==2&&reason<12){CHECK(skg::field<float>(t_rows[t_live.data()].data()+4,0x1E0)==1.8f);
            CHECK(skg::field<float>(t_rows[t_live.data()].data()+4,0x1E4)==2.4f);}
    }
    for(unsigned reason=0;reason<3;++reason){
        snapshot=setup(scene);rotations.clear();CHECK(pin_terrain_rotations(snapshot,nullptr,pin,rotations,error));drift();
        const auto changed=t_rows[t_live.data()];missing_object=reason==0;missing_spatial=reason==1;repeat_drift=reason==2;
        applying=true;CHECK(!restore_terrain_rotations(pin,rotations,error)&&!error.empty());applying=false;
        CHECK(refreshes==(reason==2?1u:0u));
        if(reason<2)CHECK(t_rows[t_live.data()]==changed);
    }
    snapshot=setup(scene);rotations.clear();CHECK(pin_terrain_rotations(snapshot,nullptr,pin,rotations,error));drift();
    CHECK(!restore_terrain_rotations(pin,rotations,error)&&!refreshes&&!error.empty());
    callback={};
}
}
static void test_terrain_rotation_restore(){terrain_rotation_fixture::test();}

namespace terrain_parent_fixture {
constexpr uint32_t property_id=0x00B2CCCB;
const editor::ResourceKey key{0x73CC30A0,0x00B1B104,0x4284A100};
std::array<uint8_t,0x34> parent,wrong_parent;
std::array<uint8_t,8> manager;
unsigned gets=0,resolves=0,writes=0;bool fail_manager=false,fail_resolve=false,wrong=false,unknown=false,reserved=false,changed_key=false,removed_key=false;
std::function<void(unsigned)> callback;
void event(unsigned stage){if(callback)callback(stage);}
void* SK_CDECL get(){++gets;event(0);return fail_manager?nullptr:manager.data();}
void SK_THISCALL resolve(void* self,void* properties){
    CHECK(self==manager.data());++resolves;
    auto* resource=static_cast<uint8_t*>(properties)-0x18;
    if(!fail_resolve){auto* chosen=wrong?wrong_parent.data():parent.data();
        skg::field<void*>(properties,0x30)=chosen;++skg::field<int>(chosen,4);}
    if(unknown)tmc_properties.at(resource).fields[0xFFFFFFFF].value[0]^=1;
    if(reserved)tmc_properties.at(resource).fields[property_id].value[12]^=1;
    if(changed_key)tmc_properties.at(resource).fields[property_id].value[0]^=1;
    if(removed_key)tmc_properties.at(resource).fields.erase(property_id);
    event(1);
}
bool SK_THISCALL write_resource(void* resource,Stream* stream){const auto count=++writes;const bool okay=tmc_write(resource,stream);event(count==1?2:3);return okay;}
int SK_THISCALL release_resource(void* resource){
    if(skg::field<int>(resource,4)==1 && t_allocations.contains(resource)){
        auto* p=skg::field<void*>(resource,0x48);if(p==parent.data()||p==wrong_parent.data())CHECK(--skg::field<int>(p,4)>=1);
    }
    return tmc_release(resource);
}
struct Routes {
    uintptr_t reader=a_read,writer=a_write,acts=a_copy_acts,crew=a_copy_crew,manager_route=a_property_manager_get,resolver=a_resolve_property_parents;
    uintptr_t property_copy=a_copy_terrain_properties;TerrainLoadFn build_route=original_terrain_build;
    uintptr_t shown=a_layout_shown,init=a_scenario_initialization,modes=a_get_simulator_mode_manager;
    ~Routes(){callback={};CHECK(t_allocations.empty());tmc_properties.clear();act_cache_rows.clear();reset();
        a_read=reader;a_write=writer;a_copy_acts=acts;a_copy_crew=crew;a_property_manager_get=manager_route;a_resolve_property_parents=resolver;
        a_copy_terrain_properties=property_copy;original_terrain_build=build_route;
        a_layout_shown=shown;a_scenario_initialization=init;a_get_simulator_mode_manager=modes;}
};
Snapshot setup(){
    CHECK(t_allocations.empty());callback={};auto snapshot=tmc_setup();
    parent.fill(0);wrong_parent.fill(0);manager.fill(0);
    skg::field<editor::ResourceKey>(parent.data(),8)=key;skg::field<int>(parent.data(),4)=1;
    skg::field<editor::ResourceKey>(wrong_parent.data(),8)=key;skg::field<editor::ResourceKey>(wrong_parent.data(),8).instance++;
    skg::field<int>(wrong_parent.data(),4)=1;
    editor_native_terrain::Property p{0x20,0,std::vector<uint8_t>(16)};std::memcpy(p.value.data(),&key,sizeof key);
    tmc_properties[t_live.data()].fields[property_id]=p;
    std::string error;CHECK(serialize(t_live.data(),snapshot.bytes,error));
    C.projection_scope=[] {return t_scope;};C.gesture_scope=[] {return GestureScope{};};
    a_property_manager_get=uintptr_t(get);a_resolve_property_parents=uintptr_t(resolve);a_write=uintptr_t(write_resource);
    t_resource_vt[1]=reinterpret_cast<void*>(release_resource);
    gets=resolves=writes=0;fail_manager=fail_resolve=wrong=unknown=reserved=changed_key=removed_key=false;
    return snapshot;
}
void test(){
    Routes routes;std::string error;auto snapshot=setup();
    {
        Owned parsed(parse(snapshot.bytes,error));CHECK(parsed.p&&!skg::field<void*>(parsed.p,0x48));
        // Model the verified F32A90 CopyFrom+SetParent and F33A50 export:
        // a null runtime parent removes the saved key despite its root presence.
        auto tool=tmc_properties.at(parsed.p);if(!skg::field<void*>(parsed.p,0x48))tool.fields.erase(property_id);
        CHECK(!tool.fields.contains(property_id)&&tmc_properties.at(parsed.p).fields.contains(property_id));
        CHECK(prepare_terrain_load_input(parsed.p,snapshot.bytes,[]{return true;},error));
        CHECK(gets==1&&resolves==1&&skg::field<void*>(parsed.p,0x48)==parent.data()&&skg::field<int>(parent.data(),4)==2);
        tool=tmc_properties.at(parsed.p);if(!skg::field<void*>(parsed.p,0x48))tool.fields.erase(property_id);
        CHECK(tool.fields.contains(property_id));const auto before_gets=gets,before_resolves=resolves;
        CHECK(prepare_terrain_load_input(parsed.p,snapshot.bytes,[]{return true;},error));
        CHECK(gets==before_gets&&resolves==before_resolves);
    }
    CHECK(skg::field<int>(parent.data(),4)==1&&t_allocations.empty());
    // Even a previously resolved child must close over its CURRENT parent
    // pointer after the serializer callback, rather than using a cached value.
    for(unsigned reason=0;reason<2;++reason){
        snapshot=setup();{
            Owned parsed(parse(snapshot.bytes,error));CHECK(prepare_terrain_load_input(parsed.p,snapshot.bytes,[]{return true;},error));
            writes=0;bool fired=false;
            callback=[&](unsigned stage){if(stage!=2||fired)return;fired=true;
                CHECK(skg::field<void*>(parsed.p,0x48)==parent.data());--skg::field<int>(parent.data(),4);
                skg::field<void*>(parsed.p,0x48)=reason?wrong_parent.data():nullptr;
                if(reason)++skg::field<int>(wrong_parent.data(),4);
            };
            CHECK(!prepare_terrain_load_input(parsed.p,snapshot.bytes,[]{return true;},error)&&fired&&!error.empty());
        }
        CHECK(skg::field<int>(parent.data(),4)==1&&skg::field<int>(wrong_parent.data(),4)==1&&t_allocations.empty());
    }
    for(unsigned reason=0;reason<10;++reason){
        snapshot=setup();{
            Owned parsed(parse(snapshot.bytes,error));CHECK(parsed.p);
            if(reason==0)fail_manager=true;if(reason==1)fail_resolve=true;if(reason==2)wrong=true;if(reason==3)unknown=true;
            if(reason==4)a_property_manager_get=0;if(reason==5)a_resolve_property_parents=0;
            if(reason==6){skg::field<void*>(parsed.p,0x48)=wrong_parent.data();++skg::field<int>(wrong_parent.data(),4);}
            if(reason==7)tmc_properties.at(parsed.p).fields.erase(property_id);
            if(reason==8)changed_key=true;if(reason==9)removed_key=true;
            CHECK(!prepare_terrain_load_input(parsed.p,snapshot.bytes,[]{return true;},error)&&!error.empty());
        }
        CHECK(skg::field<int>(parent.data(),4)==1&&skg::field<int>(wrong_parent.data(),4)==1&&t_allocations.empty());
    }
    for(unsigned stage=0;stage<4;++stage)for(unsigned reason=0;reason<12;++reason){
        snapshot=setup();{
            Owned parsed(parse(snapshot.bytes,error));CHECK(parsed.p);bool allowed=true,fired=false;
            callback=[&](unsigned at){if(at==stage&&!fired){fired=true;restore_fence_fixture::expire(reason,allowed);}};
            CHECK(!prepare_terrain_load_input(parsed.p,snapshot.bytes,[&]{return allowed;},error)&&fired&&!error.empty());
            CHECK(resolves==(stage==1||stage==3?1u:0u));
        }
        CHECK(skg::field<int>(parent.data(),4)==1&&t_allocations.empty());
    }
    snapshot=setup();{
        Owned parsed(parse(snapshot.bytes,error));reserved=true;
        CHECK(prepare_terrain_load_input(parsed.p,snapshot.bytes,[]{return true;},error));
        CHECK(tmc_properties.at(parsed.p).fields.at(property_id).value[12]==1);
    }
    snapshot=setup();{
        auto fields=tmc_properties.at(t_live.data());fields.fields.erase(property_id);tmc_properties[t_live.data()]=fields;
        CHECK(serialize(t_live.data(),snapshot.bytes,error));Owned parsed(parse(snapshot.bytes,error));gets=resolves=0;
        CHECK(prepare_terrain_load_input(parsed.p,snapshot.bytes,[]{return true;},error)&&!gets&&!resolves&&!skg::field<void*>(parsed.p,0x48));
        skg::field<void*>(parsed.p,0x48)=parent.data();++skg::field<int>(parent.data(),4);
        CHECK(!prepare_terrain_load_input(parsed.p,snapshot.bytes,[]{return true;},error)&&!gets&&!resolves);
    }
    for(unsigned reason=0;reason<3;++reason){
        snapshot=setup();auto& property=tmc_properties.at(t_live.data()).fields[property_id];
        if(reason==0)property.flags=1;if(reason==1)property.type=0x31;
        if(reason==2)std::fill_n(property.value.begin(),4,0);
        CHECK(serialize(t_live.data(),snapshot.bytes,error));Owned parsed(parse(snapshot.bytes,error));CHECK(parsed.p);gets=resolves=0;
        CHECK(!prepare_terrain_load_input(parsed.p,snapshot.bytes,[]{return true;},error)&&!gets&&!resolves&&!error.empty());
    }
    callback={};CHECK(t_allocations.empty()&&skg::field<int>(parent.data(),4)==1);
}
void* SK_THISCALL preset_copy(void* destination,const void* source){
    t_copy_preset_properties(destination,source);
    auto& bytes=t_preset_payloads.at(static_cast<uint8_t*>(destination)-0x18);
    editor_native_terrain::Properties properties;std::string error;CHECK(editor_native_terrain::decode(bytes,properties,error));
    editor_native_terrain::Property p{0x20,0,std::vector<uint8_t>(16)};std::memcpy(p.value.data(),&key,sizeof key);
    properties.fields[property_id]=std::move(p);CHECK(editor_native_terrain::encode(properties,bytes,error));return destination;
}
int SK_THISCALL preset_release(void* resource){
    if(skg::field<int>(resource,4)==1&&t_allocations.contains(resource)){
        auto* p=skg::field<void*>(resource,0x48);if(p==parent.data())CHECK(--skg::field<int>(p,4)>=1);
        t_preset_payloads.erase(resource);
    }
    return t_release(resource);
}
void SK_THISCALL preset_build(void* tool,void* properties){
    CHECK(skg::field<void*>(properties,0x30)==parent.data());t_build_preset_resource(tool,properties);
}
void test_presets(){
    Routes routes;std::array<uint8_t,0x38> source{};source[0]=1;
    for(unsigned reason=0;reason<5;++reason){
        callback={};setup_preset_retry();parent.fill(0);skg::field<editor::ResourceKey>(parent.data(),8)=key;skg::field<int>(parent.data(),4)=1;
        a_copy_terrain_properties=uintptr_t(preset_copy);original_terrain_build=preset_build;
        a_property_manager_get=uintptr_t(get);a_resolve_property_parents=uintptr_t(resolve);t_resource_vt[1]=reinterpret_cast<void*>(preset_release);
        fail_manager=fail_resolve=wrong=unknown=reserved=changed_key=removed_key=false;gets=resolves=0;
        terrain_build(t_terraform.data(),source.data());CHECK(held_terrain_preset&&!terrain_starts&&!terrain_builds);
        t_granted=true;pin_terrain_before();
        callback=[&](unsigned stage){
            CHECK(!terrain_starts&&!terrain_builds);
            if(stage==1&&reason==1)++t_scope.epoch;
            if(stage==0&&reason==2)++skg::field<editor::ResourceKey>(t_live.data(),8).instance;
            if(stage==1&&reason==3)++act_identity.settings.at(editor_native_terrain::kPlanetObject).revision;
            if(stage==1&&reason==4)++skg::field<uint32_t>(t_context.data(),0);
        };
        continue_input(nullptr);CHECK(!held_terrain_preset&&gets==1);
        if(!reason)CHECK(terrain_builds==1&&terrain_starts==1&&terrain_commits==1&&terrain_pending);
        else CHECK(!terrain_starts&&!terrain_builds&&!terrain_commits&&!t_failure.empty());
        CHECK(t_allocations.empty()&&skg::field<int>(parent.data(),4)==1);
    }
    callback={};t_preset_payloads.clear();
}
}
static void test_terrain_parent_load(){terrain_parent_fixture::test();terrain_parent_fixture::test_presets();}
