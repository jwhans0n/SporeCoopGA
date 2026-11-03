// Controlled F335B0 completion: native grounding happens before F34400's
// later F37A00 rebuild. No running game or rendering call is made here.
namespace terrain_water_fixture {
unsigned completions=0;
std::function<void()> callback;
void SK_THISCALL complete(void* self){
    CHECK(self==t_terraform.data());++completions;
    if(!applying){if(callback)callback();return;}
    Rows markers;CHECK(rows(t_live.data(),0x2C10,0x238,kPlacementLimit,markers));
    for(auto* row=markers.begin;row!=markers.end;row+=0x238)if(live_slot(row)){
        skg::field<uint32_t>(row,0x14)^=1;
        skg::field<editor::Vec3>(row,8).z+=5;
    }
    if(callback)callback();
}
struct Routes {
    terrain_rotation_capture_fixture::Routes rotation;
    HistoryFn water=original_terrain_complete_water;
    ~Routes(){callback={};original_terrain_complete_water=water;}
};
Snapshot setup(bool hole=false,bool pin=true){
    callback={};completions=0;original_terrain_complete_water=complete;
    return terrain_rotation_capture_fixture::setup(hole,pin);
}
void test(){
    Routes routes;std::string error;
    for(bool hole:{false,true}){
        auto before=setup(hole);const auto raw=t_rows.at(t_live.data());
        terrain_complete_water(t_terraform.data());
        CHECK(completions==1&&!applying&&terrain_capture_rotation_error.empty());
        CHECK(terrain_grounding.size()==before.document.placements.size());
        const auto rendered=t_rows.at(t_live.data());Snapshot after;
        CHECK(capture_resource(t_live.data(),before.act,after,error));after.metadata=before.metadata;
        CHECK(after.document.placements==before.document.placements&&after.bindings==before.bindings);
        Gesture known;known.before=before;known.after=after;CHECK(supported_changes(known,error));
        CHECK(t_rows.at(t_live.data())==rendered&&rendered!=raw&&!t_restores&&t_allocations.empty());
        // A second completion chains exact prior rendered q/XYZ to the same
        // authored source; a later rebuild must accept that recorded result.
        terrain_complete_water(t_terraform.data());CHECK(completions==2&&terrain_capture_rotation_error.empty());
        terrain_rebuild(t_terraform.data());CHECK(terrain_rotation_capture_fixture::rebuilds==1&&terrain_capture_rotation_error.empty());
        CHECK(capture_resource(t_live.data(),before.act,after,error)&&after.document.placements==before.document.placements);
    }
    // The unhooked native sequence reproduces the old ticket failure. This
    // remains a negative assertion against the actual old ordering.
    auto before=setup();{const auto previous=applying;applying=true;complete(t_terraform.data());applying=previous;}
    terrain_rebuild(t_terraform.data());
    CHECK(completions==1&&!terrain_capture_rotation_error.empty()&&!terrain_rotation_capture_fixture::rebuilds);
    // A completed Water boundary does not hide an unknown companion, or a
    // Copy implementation that changes the unmodified serialized image.
    before=setup();terrain_complete_water(t_terraform.data());t_rows.at(t_live.data())[4+0x100]^=1;
    Snapshot after;CHECK(capture_resource(t_live.data(),before.act,after,error));after.metadata=before.metadata;
    Gesture unknown;unknown.before=before;unknown.after=after;CHECK(!supported_changes(unknown,error));
    before=setup();terrain_complete_water(t_terraform.data());const auto rendered=t_rows.at(t_live.data());
    a_copy=uintptr_t(pose_proof_normalizing_copy);CHECK(!capture_resource(t_live.data(),before.act,after,error));
    CHECK(t_rows.at(t_live.data())==rendered&&t_allocations.empty());a_copy=routes.rotation.copy;
    // The original input must be canonical or have a prior exact grounding
    // map. A third XYZ may not be laundered by running Water completion.
    before=setup();skg::field<editor::Vec3>(marker_for_key(t_live.data(),0),4).z+=1;
    const auto unowned=t_rows.at(t_live.data());terrain_complete_water(t_terraform.data());
    CHECK(!completions&&!terrain_capture_rotation_error.empty()&&t_rows.at(t_live.data())==unowned);
    for(unsigned stage=0;stage<2;++stage)for(unsigned reason=0;reason<17;++reason){
        before=setup();bool allowed=true,fired=false;
        const auto expire=[&]{
            if(fired)return;fired=true;
            if(reason<12)restore_fence_fixture::expire(reason,allowed);
            if(reason==12)++t_revision;
            if(reason==13)++bindings.at(0).generation;
            if(reason==14)++skg::field<uint32_t>(marker_for_key(t_live.data(),0),0);
            if(reason==15)++skg::field<uint32_t>(marker_for_key(t_live.data(),0),0x228);
            if(reason==16)throw 17;
        };
        C.gesture_scope=[&]{return allowed?t_scope:GestureScope{};};
        if(stage)callback=expire;else C.active=[&]{expire();return true;};
        terrain_complete_water(t_terraform.data());
        if(!fired||completions!=stage||terrain_capture_rotation_error.empty()||applying)
            std::fprintf(stderr,"Water expiry stage=%u reason=%u fired=%u completions=%u error=%s applying=%u\n",stage,reason,unsigned(fired),completions,terrain_capture_rotation_error.c_str(),unsigned(applying));
        CHECK(fired&&completions==stage&&!terrain_capture_rotation_error.empty()&&!applying);
        CHECK(terrain_grounding.empty()&&!t_restores&&t_allocations.empty());
    }
    // A final read-identity callback cannot replace a sampled XYZ after the
    // original returns and before its value-only grounding map is published.
    before=setup();bool returned=false,fired=false;callback=[&]{returned=true;};
    C.gesture_scope=[&]{if(returned&&!fired){fired=true;skg::field<editor::Vec3>(marker_for_key(t_live.data(),0),4).z+=1;}return t_scope;};
    terrain_complete_water(t_terraform.data());
    CHECK(completions==1&&fired&&!terrain_capture_rotation_error.empty()&&terrain_grounding.empty());
    // A deferred real Start/Water/Commit sequence produces one normalized
    // gesture and restores the independently canonical raw image on inverse.
    before=setup(false,false);unsigned delivered=0;LocalView view;CHECK(capture_view(view,error));
    C.gesture=[&](Gesture gesture){
        ++delivered;CHECK(gesture.before.document.placements==gesture.after.document.placements);
        CHECK(supported_changes(gesture,error));
        CHECK(editor_native::apply(before.document,editor::Change{},&before,error));
        Snapshot restored;CHECK(capture(restored,error)&&validate(restored,before.document,error));
        LocalView actual;CHECK(capture_view(actual,error)&&same_view(view,actual));
    };
    terrain_start(t_terraform.data());terrain_complete_water(t_terraform.data());terrain_commit(t_terraform.data());
    CHECK(terrain_pending&&completions==1&&terrain_capture_rotation_error.empty());
    ++t_frame;drain_terrain(nullptr);
    CHECK(delivered==1&&!terrain_capture_rotations&&!terrain_before&&!terrain_pending&&!t_failed&&t_allocations.empty());
    // Native-default and already-applying calls still forward exactly once.
    setup();clear_terrain_capture_rotations();terrain_before.reset();C.active=[]{return false;};
    terrain_complete_water(t_terraform.data());CHECK(completions==1&&terrain_capture_rotation_error.empty());
    setup();applying=true;terrain_complete_water(t_terraform.data());CHECK(completions==1&&applying);applying=false;
}
}
static void test_native_water_completion(){terrain_water_fixture::test();}
