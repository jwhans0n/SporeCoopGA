// Captain XYZ from the native terrain grounding call is derived scene state.
namespace captain_grounding_fixture {
std::array<uint8_t,0x238> marker;
std::array<uint8_t,0x238> replacement;
std::function<void()> ground_callback;
unsigned ground_calls=0;
void SK_THISCALL ground(void* owner,bool flag){
    CHECK(owner==t_data.data()&&!flag);++ground_calls;
    skg::field<editor::Vec3>(marker.data(),4)={-268.155426f,489.257782f,88.628014f};
    if(ground_callback)ground_callback();
}
Snapshot setup(){
    const auto before=pose_capture_captain_setup();ground_callback={};ground_calls=0;
    marker.fill(0);skg::field<uint32_t>(marker.data(),0)=UINT32_MAX-1;
    std::memcpy(marker.data()+4,t_live.data()+0x150,32);
    skg::field<void*>(t_data.data(),0x8C)=marker.data();original_ground_scene=ground;
    return before;
}
void exported(){
    ground_scene(t_data.data(),false);
    // F3D930 exports that special marker to resource+150 after grounding.
    auto* current=skg::field<void*>(t_data.data(),0x8C);
    skg::field<editor::Vec3>(t_live.data(),0x150)=skg::field<editor::Vec3>(current,4);
}
void* SK_THISCALL captain_marker(void* owner,void* actor){
    return actor==t_actors[0].data()?skg::field<void*>(owner,0x8C):t_marker(owner,actor);
}
void test(){
    RecentRoutes routes;const auto old_ground=original_ground_scene;std::string error;
    auto before=setup();exported();
    Snapshot after;CHECK(capture_resource(t_live.data(),before.act,after,error));
    CHECK(after.document.settings.at(kCaptainObject).properties==before.document.settings.at(kCaptainObject).properties);
    CHECK(skg::field<editor::Vec3>(marker.data(),4)!=std::get<editor::Vec3>(before.document.settings.at(kCaptainObject).properties.at(kCaptainPositionProperty)));
    CHECK(ground_calls==1&&captain_grounding&&captain_has_grounding());
    // Repeated native grounding keeps the original authored value, even after
    // Export copied the previous rendered value into the live resource.
    exported();CHECK(capture_resource(t_live.data(),before.act,after,error));
    CHECK(after.document.settings.at(kCaptainObject).properties==before.document.settings.at(kCaptainObject).properties&&ground_calls==2);
    // The owned BEFORE of a real Captain gesture is canonicalized before the
    // marker can move. A pending/denied grab keeps its unchanged witness;
    // an actual authored pose change invalidates it by exact native identity.
    a_marker=uintptr_t(captain_marker);t_granted=true;
    CHECK(tool_native(t_tool_pointer,1,0,0)==1&&gesture_domain==GestureDomain::Captain);
    CHECK(gesture_before_resource&&current_captain_grounding());
    CHECK(skg::field<editor::Vec3>(gesture_before_resource,0x150)==std::get<editor::Vec3>(before.document.settings.at(kCaptainObject).properties.at(kCaptainPositionProperty)));
    skg::field<editor::Vec3>(marker.data(),4).x+=2;CHECK(!current_captain_grounding());
    before=setup();exported();a_marker=uintptr_t(captain_marker);t_granted=false;
    CHECK(tool_native(t_tool_pointer,1,0,0)==1&&held_tool&&!t_steps);
    CHECK(tool_native(t_tool_pointer,0,0,0)==1&&held_tool->released);
    deny_input({{editor::ScopeKind::Settings,kCaptainObject,{}}});
    CHECK(!held_tool&&!t_steps&&current_captain_grounding());
    CHECK(capture_resource(t_live.data(),before.act,after,error));
    CHECK(after.document.settings.at(kCaptainObject).properties==before.document.settings.at(kCaptainObject).properties);
    reset();
    for(unsigned changed=0;changed<12;++changed){
        before=setup();
        ground_callback=[changed]{
            if(changed==0)skg::field<float>(t_live.data(),0x170)+=1;
            if(changed==1)skg::field<bool>(t_live.data(),0x174)=true;
            if(changed==2)skg::field<bool>(t_live.data(),0x175)=true;
            if(changed==3)++skg::field<editor::ResourceKey>(t_live.data(),0x130).instance;
            if(changed==4)skg::field<editor::Rotation>(t_live.data(),0x15C).x=.5f;
            if(changed==5)skg::field<float>(t_live.data(),0x16C)=2;
            if(changed==6)skg::field<editor::Rotation>(marker.data(),0x10).x=.5f;
            if(changed==7)skg::field<float>(marker.data(),0x20)=2;
            if(changed==8)++t_scope.epoch;
            if(changed==9)++generation;
            if(changed==10){replacement=marker;skg::field<void*>(t_data.data(),0x8C)=replacement.data();}
            if(changed==11)++t_revision;
        };
        exported();CHECK(ground_calls==1&&!captain_grounding);
        CHECK(capture_resource(t_live.data(),before.act,after,error));
        CHECK(after.document.settings.at(kCaptainObject).properties!=before.document.settings.at(kCaptainObject).properties);
    }
    // Later property edits retain the position-only witness. Their own fields
    // remain visible to the ordinary settings lease and unsupported checks.
    before=setup();exported();skg::field<float>(t_live.data(),0x170)+=1;
    skg::field<bool>(t_live.data(),0x175)=true;
    CHECK(capture_resource(t_live.data(),before.act,after,error));
    const auto& properties=after.document.settings.at(kCaptainObject).properties;
    CHECK(properties.at(kCaptainPositionProperty)==before.document.settings.at(kCaptainObject).properties.at(kCaptainPositionProperty));
    CHECK(properties.at(kCaptainHealthProperty)!=before.document.settings.at(kCaptainObject).properties.at(kCaptainHealthProperty));
    CHECK(properties.at(kCaptainLockedProperty)==editor::Value{true});
    // A later unwitnessed pose and an unrelated opaque edit remain visible.
    before=setup();exported();skg::field<editor::Vec3>(t_live.data(),0x150).x+=7;
    CHECK(capture_resource(t_live.data(),before.act,after,error));
    CHECK(after.document.settings.at(kCaptainObject).properties!=before.document.settings.at(kCaptainObject).properties);
    before=setup();exported();t_rows[t_live.data()][4+0x100]^=1;
    CHECK(capture_resource(t_live.data(),before.act,after,error));after.metadata=before.metadata;
    Gesture checked;checked.before=before;checked.after=after;CHECK(!supported_changes(checked,error));
    // No input-authority callback may change the observed XYZ and then have
    // that write attributed to the native function which already returned.
    before=setup();unsigned queries=0;
    C.object_revision=[&](editor::ObjectId)->std::optional<editor::Revision>{
        if(++queries==3)skg::field<editor::Vec3>(marker.data(),4).x+=2;
        return t_revision;
    };
    exported();CHECK(queries==3&&!captain_grounding);
    ground_callback={};original_ground_scene=old_ground;reset();
}
void saved(){
    RecentRoutes routes;const auto old_ground=original_ground_scene;
    const auto before=setup();exported();std::string error;
    TerrainTransitionProof proof;proof.entry=before;proof.captain=current_captain_grounding();
    CHECK(proof.captain);proof.placements.emplace();
    CHECK(creation_terrain_placements(before,*proof.placements,error));
    {
        Owned resource(construct());CHECK(resource.p);t_copy(resource.p,t_live.data());
        // The real reflected stream omits live native keys. Fresh Read keeps
        // FFFFFFFF in marker+228; this raw-row codec must model that domain.
        for(auto* row=t_rows[resource.p].data();row<t_rows[resource.p].data()+t_rows[resource.p].size();row+=0x238)
            skg::field<uint32_t>(row+4,0x228)=UINT32_MAX;
        Snapshot persisted;CHECK(capture_resource(resource.p,before.act,persisted,error));
        CHECK(persisted.document.settings.at(kCaptainObject).properties!=before.document.settings.at(kCaptainObject).properties);
        const bool normalized=normalize_transition_grounding(proof,persisted,[]{return true;},error);
        if(!normalized)std::printf("saved captain normalization: %s\n",error.c_str());
        CHECK(normalized);
        CHECK(validate(persisted,before.document,error));
    }
    for(unsigned changed=0;changed<5;++changed){
        const auto entry=setup();exported();TerrainTransitionProof guarded;guarded.entry=entry;
        guarded.captain=current_captain_grounding();guarded.placements.emplace();
        CHECK(creation_terrain_placements(entry,*guarded.placements,error));
        Owned resource(construct());CHECK(resource.p);t_copy(resource.p,t_live.data());
        for(auto* row=t_rows[resource.p].data();row<t_rows[resource.p].data()+t_rows[resource.p].size();row+=0x238)
            skg::field<uint32_t>(row+4,0x228)=UINT32_MAX;
        if(changed==0)skg::field<editor::Vec3>(resource.p,0x150).x+=1;
        if(changed==1)skg::field<editor::Rotation>(resource.p,0x15C).x=.5f;
        if(changed==2)skg::field<float>(resource.p,0x16C)=2;
        if(changed==3)++skg::field<editor::ResourceKey>(resource.p,0x130).instance;
        if(changed==4)skg::field<float>(resource.p,0x170)+=1;
        Snapshot image;CHECK(capture_resource(resource.p,entry.act,image,error));const auto bytes=image.bytes;
        const auto normalized=normalize_transition_grounding(guarded,image,[]{return true;},error);
        if(changed<4)CHECK(!normalized&&image.bytes==bytes);
        else CHECK(normalized&&!validate(image,entry.document,error));
    }
    original_ground_scene=old_ground;reset();
}
void held(){
    RecentRoutes routes;const auto old_ground=original_ground_scene;
    const auto entry=setup();exported();const auto before=*captain_grounding;
    TerrainTransitionKey key;key.scope=t_scope;key.generation=generation;key.serial=77;key.owner=before.owner;
    C.projection_scope=[]{return GestureScope{};};C.gesture_scope=[]{return GestureScope{};};
    CHECK(!current_captain_grounding());
    const TerrainTransitionCurrent current=[]{return true;};
    {
        TransitionRead read(key,0,current,nullptr);
        CHECK(current_captain_grounding());
        exported();CHECK(current_captain_grounding());
        CHECK(captain_exact(captain_grounding->authored,before.authored));
        CHECK(C.gesture_scope()==GestureScope{});
    }
    CHECK(!current_captain_grounding());
    original_ground_scene=old_ground;reset();
}
}
