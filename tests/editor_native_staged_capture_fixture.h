// Actual-i686 production adapter with controlled native owners, not a game
// performance result. Stages own only the original boundary refs and values.
namespace {
void* staged_release_witness=nullptr;
std::vector<uint8_t> staged_release_rows;
uintptr_t staged_original_release=0;
unsigned staged_restored_releases=0;
int SK_THISCALL staged_checked_release(void* resource){
    if(resource==staged_release_witness){CHECK(t_rows.at(resource)==staged_release_rows);++staged_restored_releases;}
    return skg::thiscall<int>(staged_original_release,resource);
}
struct StagedFixture {
    RecentRoutes routes;
    Snapshot canonical;
    CaptureTicket ticket;
    bool held=false,wait=false;
    unsigned begun=0,checked=0,delivered=0,failed=0;
    std::string failure;
    std::optional<Gesture> result;
    Owned after;
    explicit StagedFixture(bool captain=false):canonical(captain?pose_capture_captain_setup():pose_capture_setup()){
        C.gesture_scope=[this]{return held?GestureScope{}:t_scope;};
        C.projection_scope=[] {return t_scope;};
        C.capture_begin=[this](const CaptureTicket& input,std::string&){
            CHECK(!held&&!staged_pose&&input.generation==generation&&input.gesture);
            ticket=input;held=true;++begun;return true;
        };
        C.capture_current=[this](const CaptureTicket& input){
            return held&&input==ticket?(wait?CaptureState::Wait:CaptureState::Ready):CaptureState::Retired;
        };
        C.capture_before=[this](const CaptureTicket& input,const Snapshot& before,std::string& error){
            CHECK(input==ticket&&held);++checked;
            service_capture(); // Reentry cannot advance this or a later phase.
            return validate(before,canonical.document,error);
        };
        C.capture_failed=[this](const CaptureTicket& input,const std::string& error){
            CHECK(input==ticket&&!staged_pose&&!parsed_owner_count&&!validation_depth);
            ++failed;failure=error;held=false;
        };
        C.gesture=[this](Gesture gesture){
            CHECK(gesture.capture&&*gesture.capture==ticket&&held&&!staged_pose);
            CHECK(!serialized_before_witness&&!parsed_owner_count&&!parsed_image_bytes&&!validation_depth);
            CHECK(state().ready&&!state().gesture);
            ++delivered;result=std::move(gesture);
        };
    }
    ~StagedFixture(){
        staged_pose.reset();release(std::exchange(after.p,nullptr));
        C.capture_begin={};C.capture_current={};C.capture_before={};C.capture_failed={};C.gesture={};
        CHECK(!borrowed_projections&&!parsed_owner_count&&!validation_depth&&t_allocations.empty());
    }
    void begin(bool opaque=false){
        {ScopedPoseInput input;start(t_data.data());}pose_capture_move();
        if(opaque)skg::field<uint32_t>(t_rows[t_live.data()].data()+4,0x100)=78;
        after.p=construct();CHECK(after.p);t_copy(after.p,t_live.data());
        skg::field<void*>(t_entry.data(),0x14)=after.p;commit(t_data.data());
        CHECK(pending.size()==1&&pending.front().pose_candidate);
        ::drain();CHECK(staged_pose&&begun==1&&held&&!delivered&&!failed);
        CHECK(staged_pose->entry&&staged_pose->before&&!parsed_owner_count&&!validation_depth);
    }
    void step(){++t_frame;service_capture();CHECK(!parsed_owner_count&&!parsed_image_bytes&&!validation_depth&&!borrowed_projections);}
    void finish(){for(unsigned i=0;i<8&&staged_pose;++i)step();}
};
void test_staged_native_capture(){
    {
        StagedFixture fixture;fixture.begin();const auto raw=t_rows[t_live.data()];
        const auto initial=work_stats();service_capture();CHECK(work_stats().writes.calls==initial.writes.calls);
        fixture.wait=true;fixture.step();CHECK(staged_pose->phase==StagedPose::Phase::Before&&!fixture.checked);
        fixture.wait=false;
        for(unsigned phase=0;phase<5;++phase){
            fixture.step();CHECK(staged_pose&&!fixture.delivered);
            const auto count=work_stats();service_capture();
            CHECK(work_stats().writes.calls==count.writes.calls&&work_stats().reads.calls==count.reads.calls);
        }
        CHECK(staged_pose->phase==StagedPose::Phase::Delivery&&!staged_pose->entry&&!staged_pose->before);
        fixture.step();CHECK(!staged_pose&&fixture.delivered==1&&!fixture.failed&&fixture.checked==1);
        CHECK(fixture.result&&fixture.result->capture_proof&&t_rows[t_live.data()]==raw&&!t_restores);
        const auto before=work_stats();std::string error;
        CHECK(supported_changes(*fixture.result,error));
        CHECK(work_stats().writes.calls==before.writes.calls&&work_stats().reads.calls==before.reads.calls);
        auto forged=*fixture.result;forged.after.bytes.back()^=1;
        CHECK(!supported_changes(forged,error)&&!error.empty());
        forged=*fixture.result;forged.after.document.placements.begin()->second.position.x=99;
        CHECK(!supported_changes(forged,error)&&!error.empty());
        fixture.held=false;CHECK(!supported_changes(*fixture.result,error));fixture.held=true;
        // A seal describes captured immutable images, not the current scene.
        // The independent final live proof still catches an opaque mutation.
        skg::field<uint32_t>(t_rows[t_live.data()].data()+4,0x100)^=1;
        Snapshot matched;CHECK(recent_matches(fixture.result->after,fixture.result->after.document,generation,matched,error)==RecentMatches::Different);
        CHECK(!t_restores);
    }
    for(unsigned reason=0;reason<13;++reason){
        StagedFixture fixture;fixture.begin();fixture.step();
        if(reason==0)++generation;
        if(reason==1)++t_scope.epoch;
        if(reason==2)++t_scope.author.generation;
        if(reason==3)++t_scope.project[0];
        if(reason==4)++skg::field<editor::ResourceKey>(t_live.data(),8).instance;
        if(reason==5)skg::field<void*>(t_data.data(),0x10)=nullptr;
        if(reason==6)skg::field<void*>(t_mode.data(),0x74)=t_other_data.data();
        if(reason==7)t_context_pointer=t_other_data.data();
        if(reason==8)fixture.held=false;
        if(reason==9)C.projection_scope=[] {return GestureScope{};};
        if(reason==10)t_alloc_fail=true;
        if(reason==11){a_read=uintptr_t(pose_capture_read);pose_capture_read_callback=[](void* parsed){
            skg::field<editor::Vec3>(t_rows[parsed].data()+4,4).y+=1;
        };}
        if(reason==12)skg::field<uint32_t>(t_context.data(),0)=1;
        const auto live=t_rows[t_live.data()];fixture.finish();
        t_alloc_fail=false;pose_capture_read_callback={};
        CHECK(!staged_pose&&!fixture.delivered&&fixture.failed==1&&!fixture.failure.empty());
        CHECK(t_rows[t_live.data()]==live&&!t_restores);
    }
    {
        StagedFixture fixture;fixture.begin(true);fixture.finish();
        CHECK(fixture.failed==1&&!fixture.delivered&&!t_restores); // Unknown authored byte is not pose permission.
    }
    {
        StagedFixture fixture;fixture.begin();fixture.step();fixture.step();fixture.step();fixture.step();
        CHECK(staged_pose->phase==StagedPose::Phase::Opaque);
        skg::field<uint32_t>(t_rows[staged_pose->before].data()+4,0x100)^=1;
        fixture.step();CHECK(fixture.failed==1&&!fixture.delivered&&fixture.failure.find("BEFORE changed")!=std::string::npos);
    }
    {
        StagedFixture fixture;fixture.begin();fixture.step();fixture.step();fixture.step();
        C.capture_before=[](const auto&,const auto&,std::string&){return false;};
        // Callback ownership was copied at enrollment: replacing the global
        // target cannot skip the independently required original check.
        fixture.step();CHECK(fixture.checked==1&&staged_pose->phase==StagedPose::Phase::Opaque);
        fixture.finish();CHECK(fixture.delivered==1&&!fixture.failed);
    }
    {
        StagedFixture fixture;unsigned calls=0;
        C.capture_before=[&](const auto&,const Snapshot& before,std::string& error){
            if(++calls==1){error.clear();return false;}return validate(before,fixture.canonical.document,error);
        };
        fixture.begin();fixture.step();fixture.step();fixture.step();fixture.step();
        CHECK(calls==1&&staged_pose->phase==StagedPose::Phase::Canonical);
        fixture.step();CHECK(calls==2&&staged_pose->phase==StagedPose::Phase::Opaque);
        fixture.finish();CHECK(fixture.delivered==1&&!fixture.failed);
    }
    // The byte image belongs to the original release. A change between its
    // serialization and the later Read/projection cannot become a new gesture.
    for(bool known:{false,true}){
        StagedFixture fixture;fixture.begin();fixture.step();fixture.step();
        CHECK(staged_pose->phase==StagedPose::Phase::AfterProjection);
        auto* captured=skg::field<void*>(staged_pose->entry,0x14);
        if(known)skg::field<editor::Vec3>(t_rows.at(captured).data()+4,4).y+=1;
        else skg::field<uint32_t>(t_rows.at(captured).data()+4,0x100)^=1;
        const auto live=t_rows[t_live.data()];fixture.finish();
        CHECK(fixture.failed==1&&!fixture.delivered&&t_rows[t_live.data()]==live&&!t_restores);
    }
    {
        StagedFixture fixture;fixture.begin();fixture.step();fixture.step();
        CHECK(staged_pose->phase==StagedPose::Phase::AfterProjection);
        auto* original=staged_pose->after;
        CHECK(original==fixture.after.p&&skg::field<int>(original,4)==2);
        const auto live=t_rows[t_live.data()];unsigned entered=0;
        a_read=uintptr_t(pose_capture_read);
        pose_capture_read_callback=[&](void*){
            if(entered++)return;
            skg::field<void*>(staged_pose->entry,0x14)=nullptr;
            // The captured child remains independently owned even when the
            // original history entry stops retaining that exact child.
            CHECK(staged_pose->after==original&&t_allocations.contains(original));
        };
        fixture.step();pose_capture_read_callback={};
        CHECK(entered&&fixture.failed==1&&!fixture.delivered&&!staged_pose);
        CHECK(skg::field<int>(original,4)==1&&t_rows[t_live.data()]==live&&!t_restores);
    }
    {
        StagedFixture fixture;C.capture_current=[](const CaptureTicket&)->CaptureState{
            throw std::runtime_error("injected capture identity failure");
        };
        // Enrollment failure retires exclusive owners before Runtime sees the
        // failure and must never deliver an orphan gesture.
        {ScopedPoseInput input;start(t_data.data());}pose_capture_move();
        fixture.after.p=construct();CHECK(fixture.after.p);t_copy(fixture.after.p,t_live.data());
        skg::field<void*>(t_entry.data(),0x14)=fixture.after.p;commit(t_data.data());
        ::drain();CHECK(!staged_pose&&fixture.begun==1&&fixture.failed==1&&!fixture.delivered&&!t_restores);
        CHECK(skg::field<int>(fixture.after.p,4)==1);
    }
    {
        StagedFixture fixture;unsigned generic=0;
        C.capture_begin=[](const CaptureTicket&,std::string& error){error.clear();return false;};
        C.gesture=[&](Gesture gesture){++generic;CHECK(!gesture.capture&&!gesture.capture_proof);};
        {ScopedPoseInput input;start(t_data.data());}pose_capture_move();
        fixture.after.p=construct();CHECK(fixture.after.p);t_copy(fixture.after.p,t_live.data());
        skg::field<void*>(t_entry.data(),0x14)=fixture.after.p;commit(t_data.data());
        ::drain();CHECK(!staged_pose&&!fixture.held&&!fixture.failed&&generic==1&&!t_restores);
        CHECK(skg::field<int>(fixture.after.p,4)==1);
    }
    for(unsigned expired=0;expired<3;++expired){
        StagedFixture fixture;fixture.begin();unsigned writes=0;
        recent_write_callback=[&](void*,unsigned){if(++writes!=1)return;
            if(expired==0)++generation;
            if(expired==1)++t_scope.epoch;
            if(expired==2)++skg::field<editor::ResourceKey>(t_live.data(),8).instance;
        };
        fixture.step();recent_write_callback={};
        CHECK(fixture.failed==1&&!fixture.delivered&&!staged_pose&&!t_restores);
    }
    {
        StagedFixture fixture;fixture.begin();fixture.step();fixture.step();fixture.step();fixture.step();
        CHECK(staged_pose->phase==StagedPose::Phase::Opaque);
        staged_release_witness=staged_pose->before;staged_release_rows=t_rows.at(staged_release_witness);staged_restored_releases=0;
        staged_original_release=uintptr_t(t_resource_vt[1]);t_resource_vt[1]=reinterpret_cast<void*>(staged_checked_release);
        unsigned writes=0;recent_write_callback=[&](void* resource,unsigned){
            if(resource!=staged_release_witness)return;
            service_capture(); // Reentry cannot reuse the temporarily overlaid original.
            if(++writes==2)throw std::runtime_error("injected forward Write failure");
        };
        fixture.finish();recent_write_callback={};t_resource_vt[1]=reinterpret_cast<void*>(staged_original_release);
        CHECK(staged_restored_releases==1&&fixture.delivered==1&&!fixture.failed&&!fixture.result->capture_proof);
        CHECK(!t_restores);staged_release_witness=nullptr;staged_release_rows.clear();
    }
    {
        StagedFixture fixture(true);fixture.begin();fixture.finish();
        CHECK(fixture.delivered==1&&fixture.result->capture_proof);
        std::string error;const auto count=work_stats();recent_resolved={97,99,101};
        CHECK(!supported_changes(*fixture.result,error)&&!error.empty());
        CHECK(work_stats().writes.calls==count.writes.calls&&work_stats().reads.calls==count.reads.calls&&!t_restores);
    }
    {
        StagedFixture fixture(true);fixture.begin();fixture.finish();
        CHECK(fixture.delivered==1&&fixture.result->capture_proof);std::string error;unsigned nested=0;
        recent_resolve_callback=[&](unsigned,editor::ResourceKey&){if(nested++)return;
            CHECK(!supported_changes(*fixture.result,error));
        };
        CHECK(supported_changes(*fixture.result,error)&&nested);recent_resolve_callback={};
        CHECK(!fixture.result->capture_proof->busy&&!t_restores);
    }
    {
        StagedFixture fixture;fixture.begin();fixture.step();fixture.step();fixture.step();
        CHECK(staged_pose->phase==StagedPose::Phase::Canonical&&staged_pose->after_projection);
        projected_snapshots.clear();projected_snapshot_bytes=0;
        const auto before=work_stats();fixture.step();fixture.step();
        CHECK(staged_pose->phase==StagedPose::Phase::Delivery&&staged_pose->image.capture_proof);
        CHECK(work_stats().projections.calls==before.projections.calls);
        CHECK(work_stats().writes.calls==before.writes.calls+2); // Fresh BEFORE + exact forward image.
        fixture.step();projected_snapshots.clear();projected_snapshot_bytes=0;
        std::string error;const auto reuse=work_stats();
        CHECK(supported_changes(*fixture.result,error));
        CHECK(validate(fixture.result->after,fixture.result->after.document,error));
        CHECK(work_stats().reads.calls==reuse.reads.calls&&work_stats().writes.calls==reuse.writes.calls&&
            work_stats().projections.calls==reuse.projections.calls);
    }
    for(bool aba:{false,true}){
        StagedFixture fixture(true);fixture.begin();fixture.finish();
        CHECK(fixture.result&&fixture.result->capture_proof);std::string error;unsigned callbacks=0;
        recent_resolve_callback=[&](unsigned,editor::ResourceKey&){if(callbacks++)return;
            if(!aba){++skg::field<editor::ResourceKey>(t_live.data(),8).instance;return;}
            const auto original=recent_resolved;recent_resolved={91,93,95};editor::ResourceKey changed;
            CHECK(resolve_asset(17,changed)&&changed==recent_resolved);recent_resolved=original;
        };
        const auto before=work_stats();CHECK(!supported_changes(*fixture.result,error)&&!error.empty());
        CHECK(work_stats().reads.calls==before.reads.calls&&work_stats().writes.calls==before.writes.calls&&!t_restores);
        recent_resolve_callback={};
    }
}
} // namespace
