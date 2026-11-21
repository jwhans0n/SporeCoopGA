// Actual-i686 production adapter with controlled native owners. These are
// callback/lifetime proofs, not running-game performance or ABI acceptance.
namespace {
std::function<void(void*)> pose_capture_read_callback;
bool pose_capture_resolve_failure=false;
unsigned pose_proof_copy_calls=0;
void* SK_THISCALL pose_proof_normalizing_copy(void* destination,const void* source){
    ++pose_proof_copy_calls;t_copy(destination,source);
    skg::field<uint32_t>(t_rows[destination].data()+4,0x100)=78;return destination;
}
bool SK_CDECL pose_capture_resolve(uint64_t server,editor::ResourceKey* key){
    CHECK(server==17);*key=recent_resolved;++recent_resolves;
    if(recent_resolve_callback)recent_resolve_callback(recent_resolves,*key);
    return !pose_capture_resolve_failure;
}
uintptr_t pose_capture_symbol(const char* name){
    CHECK(std::string(name)=="Simulator::Scenario::ResolveLocalServerAsset");return uintptr_t(pose_capture_resolve);
}
bool SK_THISCALL pose_capture_read(void* resource,Stream* stream){
    if(!t_pose_read(resource,stream))return false;
    if(pose_capture_read_callback)pose_capture_read_callback(resource);
    return true;
}
bool SK_THISCALL pose_capture_entry_read(void* resource,Stream* stream){
    if(!t_entry_read(resource,stream))return false;
    if(pose_capture_read_callback)pose_capture_read_callback(resource);
    return true;
}
Snapshot pose_capture_setup(){
    auto before=recent_pose_setup();before.document.project={};before.document.epoch=0;
    pose_capture_read_callback={};pose_capture_resolve_failure=false;return before;
}
std::map<uint32_t,editor::ObjectId> pose_capture_bindings(const Snapshot& before){
    std::map<uint32_t,editor::ObjectId> supplied;
    for(const auto& row:before.bindings)supplied.emplace(row.marker,row.object);
    return supplied;
}
void pose_capture_move(unsigned marker=0){
    skg::field<editor::Vec3>(t_rows[t_live.data()].data()+marker*0x238+4,4).y+=3;
}
bool SK_THISCALL pose_capture_compact_write(void* resource,Stream* stream){
    ++t_writes;const auto& rows=t_rows[resource];uint32_t count=0;
    for(auto* row=rows.data();row<rows.data()+rows.size();row+=0x238)if(live_slot(const_cast<uint8_t*>(row)))++count;
    const std::array<uint32_t,3> header{0x50534531,count,skg::field<uint32_t>(resource,0x2C4C)};
    if(!write(stream,header.data(),sizeof header))return false;
    for(auto* row=rows.data();row<rows.data()+rows.size();row+=0x238)
        if(live_slot(const_cast<uint8_t*>(row))&&!write(stream,row,0x238))return false;
    return true;
}
Snapshot pose_capture_extended_setup(){
    pose_capture_setup();C.extended_poses_tracked=[] {return true;};C.relative_scale_tracked=[] {return true;};
    skg::field<uint32_t>(t_pose_class.data(),8)=0x90C48D09;
    for(auto* row=t_rows[t_live.data()].data();row<t_rows[t_live.data()].data()+t_rows[t_live.data()].size();row+=0x238){
        skg::field<editor::Rotation>(row+4,0x1F4)={0,0,0,1};skg::field<float>(row+4,0x204)=1;
    }
    Snapshot before;std::string error;CHECK(capture(before,error));return before;
}
Snapshot pose_capture_captain_setup(){
    pose_capture_setup();C.authoring_tracked=[] {return true;};a_copy_acts=uintptr_t(t_entry_copy_acts);a_copy_crew=0;
    a_read=uintptr_t(t_entry_read);a_write=uintptr_t(t_entry_write);t_entry_marker_act.fill(0);
    for(auto* row=t_rows[t_live.data()].data();row<t_rows[t_live.data()].data()+t_rows[t_live.data()].size();row+=0x238){
        skg::field<void*>(row+4,0x28)=t_entry_marker_act.data();skg::field<void*>(row+4,0x2C)=t_entry_marker_act.data()+t_entry_marker_act.size();
    }
    skg::field<editor::ResourceKey>(t_live.data(),0x130)={1,2,3};skg::field<uint64_t>(t_live.data(),0x140)=17;
    skg::field<editor::Rotation>(t_live.data(),0x15C)={0,0,0,1};skg::field<float>(t_live.data(),0x16C)=1;
    skg::field<float>(t_live.data(),0x170)=.8f;
    static SkHost host;host=*H;host.sym=pose_capture_symbol;H=&host;recent_resolves=0;recent_resolved={17,19,23};
    Snapshot before;std::string error;CHECK(capture(before,error));return before;
}
void test_pose_capture_owned_read_lifetime(){
    RecentRoutes routes;const auto before=pose_capture_setup();std::string error;
    CHECK(parsed_images.empty()&&!parsed_owner_count&&!parsed_owner_bytes&&!validation_depth);
    const auto reads=t_reads;const auto allocations=t_allocations.size();
    {
        ValidationScope outer;ReadImage first,same,second,uncached;
        CHECK(read_image(before,first,error));CHECK(first.retained&&t_reads==reads+1);
        {ValidationScope nested;CHECK(read_image(before,same,error));CHECK(same.p==first.p&&t_reads==reads+1);}
        auto other=before;skg::field<uint32_t>(other.bytes.data(),8)^=1;
        CHECK(read_image(other,second,error));CHECK(second.retained&&second.p!=first.p&&t_reads==reads+2);
        other.bytes[8]^=2;
        CHECK(read_image(other,uncached,error));CHECK(!uncached.retained&&uncached.owned.p&&t_reads==reads+3);
        const auto stats=work_stats();
        CHECK(stats.parsed_entries==2&&stats.parsed_owners==2&&stats.parsed_peak_owners<=2);
        CHECK(stats.parsed_bytes<=16u<<20&&stats.parsed_owner_bytes<=16u<<20);
        CHECK(t_allocations.size()==allocations+3); // Existing local miss plus two retained owners.
        CHECK(first.p&&second.p&&first.p!=second.p&&uncached.p!=first.p&&uncached.p!=second.p);
    }
    CHECK(parsed_images.empty()&&!parsed_image_bytes&&!parsed_owner_count&&!parsed_owner_bytes&&!validation_depth);
    CHECK(t_allocations.size()==allocations);
    // Native Read may reenter validation before its outer owner is indexed.
    // All retained owners remain bounded and the old caller cannot publish an
    // owner under the replacement generation or scope.
    for(unsigned changed=0;changed<2;++changed){
        auto fresh=pose_capture_setup();a_read=uintptr_t(pose_capture_read);unsigned entered=0;
        {
            ValidationScope outer;ReadImage refused;
            pose_capture_read_callback=[&](void*){
                if(++entered!=1)return;
                ReadImage nested;CHECK(read_image(fresh,nested,error));CHECK(nested.p);
                if(changed)++generation;else ++t_scope.epoch;
            };
            CHECK(!read_image(fresh,refused,error)&&!refused.p&&!error.empty());
            CHECK(parsed_owner_count<=2&&parsed_owner_bytes<=16u<<20);
            pose_capture_read_callback={};
        }
        CHECK(t_allocations.empty()&&parsed_images.empty()&&!parsed_owner_count&&!parsed_owner_bytes);
    }
}
void test_pose_capture_serialized_projection_and_reuse(){
    RecentRoutes routes;const auto before=pose_capture_setup();const auto supplied=pose_capture_bindings(before);
    pose_capture_move();const auto actual=t_rows[t_live.data()];std::string error;
    Snapshot after;PoseCaptureProof proof;
    {
        ValidationScope outer;const auto count=work_stats();
        CHECK(capture_pose_resource(t_live.data(),before.act,before,supplied,*before.metadata,after,proof,error)==RecentMatches::Match);
        CHECK(work_stats().projections.calls==count.projections.calls+1&&work_stats().reads.calls==count.reads.calls+1);
        CHECK(after.source==before.source&&after.metadata==before.metadata&&after.bindings==before.bindings);
        CHECK(after.document.placements.at(before.bindings[0].object).position.y==3);
        CHECK(after.document.placements.at(before.bindings[1].object)==before.document.placements.at(before.bindings[1].object));
        CHECK(only_existing_pose_fields(before.document,after.document));
        remember_projection(after,proof.assets,proof.scope,proof.generation);
        const auto reads=t_reads;const auto projections=work_stats().projections.calls;
        CHECK(validate(after,after.document,error)&&t_reads==reads&&work_stats().projections.calls==projections);
        Gesture checked;checked.before=before;checked.after=after;
        CHECK(supported_changes(checked,error));CHECK(t_reads==reads+1); // Before is read; after is borrowed.
        const auto borrowed_reads=t_reads;
        CHECK(supported_changes(checked,error)&&t_reads==borrowed_reads);
        auto opaque=after;opaque.bytes[12+4+0x100]^=1;checked.after=opaque;
        CHECK(!supported_changes(checked,error));
        CHECK(t_rows[t_live.data()]==actual&&!t_restores);
    }
    CHECK(t_allocations.empty()&&parsed_images.empty()&&!parsed_owner_count&&!parsed_owner_bytes);
    // The value proof survives, while every native owner has gone away.
    const auto reads=t_reads;CHECK(validate(after,after.document,error)&&t_reads==reads);
    auto forged=after.document;forged.placements.begin()->second.position.x=99;
    CHECK(!validate(after,forged,error)&&t_rows[t_live.data()]==actual&&!t_restores);
}
void test_pose_capture_normalization_and_identity_rejection(){
    RecentRoutes routes;std::string error;
    for(unsigned changed=0;changed<8;++changed){
        const auto before=pose_capture_setup();const auto supplied=pose_capture_bindings(before);pose_capture_move();
        Snapshot untouched;untouched.bytes={0xCD};PoseCaptureProof proof;const auto count=projected_snapshots.size();
        if(changed<=2){
            a_read=uintptr_t(pose_capture_read);
            pose_capture_read_callback=[changed](void* parsed){
                auto* marker=t_rows[parsed].data()+4;
                if(changed==0)skg::field<editor::Vec3>(marker,4).y+=1;
                if(changed==1)skg::field<editor::Rotation>(marker,0x10).w=.5f;
                if(changed==2)skg::field<float>(marker,0x20)+=1;
            };
        }
        if(changed==3)skg::field<editor::ResourceKey>(t_live.data(),8).instance^=1;
        if(changed==4)skg::field<uint32_t>(t_rows[t_live.data()].data()+4,0)=2048;
        if(changed==5)skg::field<bool>(t_live.data(),0x175)=true;
        if(changed==6)t_alloc_fail=true;
        if(changed==7)recent_after_write_callback=[](void*,unsigned live){if(live==2)pose_capture_move();};
        {
            ValidationScope outer;
            CHECK(capture_pose_resource(t_live.data(),before.act,before,supplied,*before.metadata,untouched,proof,error)==RecentMatches::Different);
            CHECK(untouched.bytes==std::vector<uint8_t>{0xCD}&&projected_snapshots.size()==count&&!t_restores);
        }
        pose_capture_read_callback={};recent_after_write_callback={};t_alloc_fail=false;
        CHECK(t_allocations.empty()&&parsed_images.empty()&&!parsed_owner_count);
    }
    for(unsigned changed=0;changed<2;++changed){
        const auto before=pose_capture_setup();const auto supplied=pose_capture_bindings(before);pose_capture_move();
        a_read=uintptr_t(pose_capture_read);pose_capture_read_callback=[changed](void*){if(changed)++generation;else ++t_scope.epoch;};
        Snapshot untouched;untouched.bytes={0xCD};PoseCaptureProof proof;
        {ValidationScope outer;
            CHECK(capture_pose_resource(t_live.data(),before.act,before,supplied,*before.metadata,untouched,proof,error)==RecentMatches::Error);
            CHECK(untouched.bytes==std::vector<uint8_t>{0xCD}&&!error.empty());
        }
        pose_capture_read_callback={};CHECK(t_allocations.empty()&&!parsed_owner_count);
    }
    // Typed non-placement settings, a roster change, or an act mapping change
    // never become an existing-placement pose boundary.
    const auto before=pose_capture_setup();auto after=before.document;
    after.placements.begin()->second.position.y=3;CHECK(only_existing_pose_fields(before.document,after));
    after.classes.insert({999,1});CHECK(!only_existing_pose_fields(before.document,after));
    after=before.document;after.placements.begin()->second.position.y=3;after.terrain.emplace(editor::ObjectId{99,1},editor::TerrainRecord{});
    CHECK(!only_existing_pose_fields(before.document,after));
    after=before.document;after.placements.erase(after.placements.begin());CHECK(!only_existing_pose_fields(before.document,after));
    // New structured placement settings must not inherit pose authorization.
    constexpr uint32_t unrelated=0xF00D0001;
    auto structured_before=before.document;
    structured_before.placements.begin()->second.properties[unrelated]=editor::StructuredValue{9,{1,2,3},{}};
    after=structured_before;after.placements.begin()->second.position.y=3;
    CHECK(only_existing_pose_fields(structured_before,after));
    std::get<editor::StructuredValue>(after.placements.begin()->second.properties.at(unrelated)).bytes[0]^=1;
    CHECK(!only_existing_pose_fields(structured_before,after));
    after=structured_before;after.placements.begin()->second.position.y=3;after.placements.begin()->second.properties.erase(unrelated);
    CHECK(!only_existing_pose_fields(structured_before,after));
    after=before.document;after.placements.begin()->second.position.y=3;after.placements.begin()->second.properties[kSoundGainProperty]=1.f;
    CHECK(!only_existing_pose_fields(before.document,after)); // A companion cannot appear in an old schema.
}
void test_pose_capture_dense_bindings_and_companions(){
    RecentRoutes routes;std::string error;
    auto before=pose_capture_setup();const auto original=t_rows[t_live.data()];
    auto& sparse=t_rows[t_live.data()];sparse.resize(4*0x238);
    std::memcpy(sparse.data(),original.data(),0x238);
    std::memcpy(sparse.data()+2*0x238,original.data()+0x238,0x238);
    skg::field<uint32_t>(sparse.data()+0x238,0)=0x80000000;
    skg::field<uint32_t>(sparse.data()+3*0x238,0)=0x80000000;
    rows_for(t_live.data());bindings.clear();incarnations.clear();a_write=uintptr_t(pose_capture_compact_write);
    CHECK(capture(before,error));CHECK(before.bindings.size()==2&&before.bindings[0].marker==0&&before.bindings[1].marker==1);
    const std::map<uint32_t,editor::ObjectId> supplied{{0,before.bindings[0].object},{2,before.bindings[1].object}};
    Owned sparse_before(construct());CHECK(sparse_before.p);t_copy(sparse_before.p,t_live.data());
    CHECK(before.bindings[1].object.id==((1ull<<32)|2));pose_capture_move(2);
    {
        ValidationScope outer;Snapshot after;PoseCaptureProof proof;
        CHECK(capture_pose_resource(t_live.data(),before.act,before,supplied,*before.metadata,after,proof,error)==RecentMatches::Match);
        CHECK(after.bindings==before.bindings&&after.document.placements.at(before.bindings[1].object).position.y==3);
        CHECK(after.document.placements.at(before.bindings[0].object)==before.document.placements.at(before.bindings[0].object));
        remember_projection(after,proof.assets,proof.scope,proof.generation);
        Gesture checked;checked.generation=generation;checked.before=before;checked.after=after;
        SerializedBeforeScope serialized(sparse_before.p,supplied,before);
        const auto hits=opaque_pose_hits;CHECK(supported_changes(checked,error)&&opaque_pose_hits==hits+1);
    }
    release(std::exchange(sparse_before.p,nullptr));
    CHECK(t_allocations.empty()&&!parsed_owner_count);
    before=pose_capture_extended_setup();const auto all_before=t_rows[t_live.data()];
    const auto extended_bindings=pose_capture_bindings(before);
    Owned extended_before(construct());CHECK(extended_before.p);t_copy(extended_before.p,t_live.data());
    auto* first=t_rows[t_live.data()].data()+4;auto* second=t_rows[t_live.data()].data()+0x238+4;
    skg::field<editor::Vec3>(first,4)={1,2,3};skg::field<editor::Rotation>(first,0x10)={.5f,.5f,.5f,.5f};
    skg::field<float>(first,0x20)=2;skg::field<float>(first,0x24)=.75f;skg::field<float>(first,0x220)=4;
    skg::field<float>(first,0x1E0)=1.25f;skg::field<float>(first,0x1E4)=.25f;
    skg::field<editor::Vec3>(second,0x1E8)={4,5,6};skg::field<editor::Rotation>(second,0x1F4)={0,.6f,0,.8f};
    skg::field<float>(second,0x204)=3;
    {
        ValidationScope outer;Snapshot after;PoseCaptureProof proof;
        CHECK(capture_pose_resource(t_live.data(),before.act,before,pose_capture_bindings(before),*before.metadata,after,proof,error)==RecentMatches::Match);
        const auto& p=after.document.placements.at(before.bindings[0].object);
        CHECK((p.position==editor::Vec3{1,2,3}&&p.rotation==editor::Rotation{.5f,.5f,.5f,.5f}&&p.scale==2));
        CHECK(std::get<float>(p.properties.at(kRelativeScaleProperty))==.75f&&std::get<float>(p.properties.at(kElevationProperty))==4);
        CHECK(std::get<float>(p.properties.at(kSoundPitchProperty))==1.25f&&std::get<float>(p.properties.at(kSoundGainProperty))==.25f);
        const auto& destination=after.document.placements.at(before.bindings[1].object).properties;
        CHECK((std::get<editor::Vec3>(destination.at(kDestinationPositionProperty))==editor::Vec3{4,5,6}));
        CHECK((std::get<editor::Rotation>(destination.at(kDestinationRotationProperty))==editor::Rotation{0,.6f,0,.8f}));
        CHECK(std::get<float>(destination.at(kDestinationScaleProperty))==3);
        remember_projection(after,proof.assets,proof.scope,proof.generation);
        Gesture checked;checked.generation=generation;checked.before=before;checked.after=after;
        SerializedBeforeScope serialized(extended_before.p,extended_bindings,before);
        const auto hits=opaque_pose_hits,writes=work_stats().writes.calls;
        CHECK(supported_changes(checked,error)&&opaque_pose_hits==hits+1&&work_stats().writes.calls==writes+1);
    }
    release(std::exchange(extended_before.p,nullptr));
    CHECK(t_allocations.empty()&&!parsed_owner_count&&t_rows[t_live.data()]!=all_before&&!t_restores);
    for(auto offset:{0x24u,0x220u,0x1E0u,0x1E4u,0x1E8u,0x1F4u,0x204u}){
        before=pose_capture_extended_setup();pose_capture_move();a_read=uintptr_t(pose_capture_read);
        pose_capture_read_callback=[offset](void* resource){skg::field<float>(t_rows[resource].data()+4,offset)+=.25f;};
        {
            ValidationScope outer;Snapshot after;PoseCaptureProof proof;
            CHECK(capture_pose_resource(t_live.data(),before.act,before,pose_capture_bindings(before),*before.metadata,after,proof,error)==RecentMatches::Different);
        }
        pose_capture_read_callback={};CHECK(t_allocations.empty()&&!parsed_owner_count&&!t_restores);
    }
}
void test_pose_capture_captain_and_resolution_closure(){
    RecentRoutes routes;std::string error;
    auto before=pose_capture_captain_setup();CHECK((before.captain==editor::ResourceKey{17,19,23}));pose_capture_move();
    {
        ValidationScope outer;Snapshot after;PoseCaptureProof proof;
        CHECK(capture_pose_resource(t_live.data(),before.act,before,pose_capture_bindings(before),*before.metadata,after,proof,error)==RecentMatches::Match);
        CHECK(after.captain==before.captain&&after.document.settings.at(kCaptainObject).properties==before.document.settings.at(kCaptainObject).properties);
        CHECK(!proof.assets.empty()&&proof.assets.front().server==17&&proof.assets.front().key==before.captain);
    }
    for(unsigned race=1;race<=5;++race){
        before=pose_capture_captain_setup();pose_capture_move();recent_resolves=0;
        recent_resolve_callback=[race](unsigned call,editor::ResourceKey& key){if(call==race)++key.instance;};
        {
            ValidationScope outer;Snapshot after;after.bytes={0xCD};PoseCaptureProof proof;
            CHECK(capture_pose_resource(t_live.data(),before.act,before,pose_capture_bindings(before),*before.metadata,after,proof,error)!=RecentMatches::Match);
            CHECK(after.bytes==std::vector<uint8_t>{0xCD}&&!t_restores);
        }
        recent_resolve_callback={};CHECK(t_allocations.empty()&&!parsed_owner_count);
    }
    for(auto offset:{0x150u,0x15Cu,0x16Cu,0x170u}){
        before=pose_capture_captain_setup();pose_capture_move();a_read=uintptr_t(pose_capture_entry_read);
        pose_capture_read_callback=[offset](void* resource){skg::field<float>(resource,offset)+=.25f;};
        {
            ValidationScope outer;Snapshot after;PoseCaptureProof proof;
            CHECK(capture_pose_resource(t_live.data(),before.act,before,pose_capture_bindings(before),*before.metadata,after,proof,error)==RecentMatches::Different);
        }
        pose_capture_read_callback={};CHECK(t_allocations.empty()&&!parsed_owner_count&&!t_restores);
    }
    // Marker-act data is outside the pose allowlist. A parsed after resource
    // can never use its typed pose proof to bypass the opaque-change guard.
    before=pose_capture_captain_setup();pose_capture_move();a_read=uintptr_t(pose_capture_entry_read);
    std::array<uint8_t,0x34> different_act{};different_act[0x10]=7;
    pose_capture_read_callback=[&](void* resource){
        auto* marker=t_rows[resource].data()+4;
        if(skg::field<editor::Vec3>(marker,4).y!=3)return;
        skg::field<void*>(marker,0x28)=different_act.data();skg::field<void*>(marker,0x2C)=different_act.data()+different_act.size();
    };
    {
        ValidationScope outer;Snapshot after;PoseCaptureProof proof;
        CHECK(capture_pose_resource(t_live.data(),before.act,before,pose_capture_bindings(before),*before.metadata,after,proof,error)==RecentMatches::Match);
        remember_projection(after,proof.assets,proof.scope,proof.generation);
        Gesture gesture;gesture.before=before;gesture.after=after;CHECK(!supported_changes(gesture,error));
        CHECK(!t_restores&&skg::field<void*>(t_rows[t_live.data()].data()+4,0x28)==t_entry_marker_act.data());
    }
    pose_capture_read_callback={};CHECK(t_allocations.empty()&&!parsed_owner_count);
}
void test_pose_capture_history_scope(){
    RecentRoutes routes;std::string error;
    for(unsigned scenario=0;scenario<4;++scenario){
        auto before=pose_capture_setup();const auto original=t_rows[t_live.data()];const auto count=work_stats();
        unsigned seen=0;Gesture captured;
        C.gesture=[&](Gesture gesture){
            ++seen;captured=gesture;
            if(scenario==0){
                const auto reads=t_reads;CHECK(validate(gesture.after,gesture.after.document,error)&&t_reads==reads);
                CHECK(work_stats().parsed_owners>=1&&work_stats().parsed_owners<=2);
                CHECK(supported_changes(gesture,error));
            }
        };
        {ScopedPoseInput pose;const auto previous=input_cast;input_cast=scenario==1;start(t_data.data());input_cast=previous;}
        CHECK(gesture_before_resource&&gesture_pose==(scenario!=1));pose_capture_move();
        Owned after(construct());CHECK(after.p);t_copy(after.p,t_live.data());skg::field<void*>(t_entry.data(),0x14)=after.p;
        if(scenario==2)gesture_clone=before.bindings.front().object;
        if(scenario==3)skg::field<uint32_t>(t_data.data(),0x7C)=3;
        commit(t_data.data());CHECK(pending.size()==1);
        CHECK(pending.front().pose_candidate==(scenario==0||scenario==3));
        ::drain();CHECK(seen==1&&!t_failed&&pending.empty());
        CHECK(work_stats().pose_captures==count.pose_captures+(scenario==0?1:0));
        CHECK(parsed_images.empty()&&!parsed_owner_count&&!parsed_owner_bytes&&!validation_depth);
        CHECK(captured.before.metadata->genre==0&&captured.after.metadata->genre==(scenario==3?3u:0u));
        auto changed=original;skg::field<editor::Vec3>(changed.data()+4,4).y+=3;
        CHECK(t_rows[t_live.data()]==changed&&!t_restores);
        release(std::exchange(after.p,nullptr));
    }
}
void test_pose_capture_failed_callback_cannot_fallback_into_replacement(){
    RecentRoutes routes;std::string error;
    auto expire=[](unsigned changed){
        if(changed==0)++t_scope.epoch;
        if(changed==1)++generation;
        if(changed==2)skg::field<void*>(t_mode.data(),0x74)=t_other_data.data();
        if(changed==3)skg::field<void*>(t_data.data(),0x10)=nullptr;
        if(changed==4)skg::field<editor::ResourceKey>(t_live.data(),8).instance^=1;
    };
    for(unsigned changed=0;changed<6;++changed)for(unsigned call:{1u,2u,4u,5u}){
        const auto before=pose_capture_captain_setup();pose_capture_move();recent_resolves=0;
        recent_resolve_callback=[&,changed,call](unsigned current,editor::ResourceKey&){
            if(current!=call)return;pose_capture_resolve_failure=true;expire(changed);
        };
        {
            ValidationScope outer;Snapshot after;after.bytes={0xCD};PoseCaptureProof proof;
            CHECK(capture_pose_resource(t_live.data(),before.act,before,pose_capture_bindings(before),*before.metadata,after,proof,error)==
                (changed==5?RecentMatches::Different:RecentMatches::Error));
            CHECK(after.bytes==std::vector<uint8_t>{0xCD}&&proof.assets.empty()&&!proof.generation&&!t_restores);
        }
        recent_resolve_callback={};pose_capture_resolve_failure=false;
        CHECK(t_allocations.empty()&&!parsed_owner_count&&!parsed_owner_bytes);
    }
    // Exercise the actual deferred history route. A failing resolver on the
    // candidate's decoded after-image must report failure, not invoke Runtime
    // with an old gesture after replacing the native owner/scope.
    for(unsigned changed=0;changed<5;++changed){
        pose_capture_captain_setup();a_read=uintptr_t(pose_capture_entry_read);unsigned seen=0;
        C.gesture=[&](Gesture){++seen;};
        {ScopedPoseInput pose;start(t_data.data());}pose_capture_move();
        Owned after(construct());CHECK(after.p);t_copy(after.p,t_live.data());skg::field<void*>(t_entry.data(),0x14)=after.p;
        commit(t_data.data());CHECK(pending.size()==1&&pending.front().pose_candidate);
        pose_capture_read_callback=[&,changed](void* parsed){
            if(t_rows[parsed].empty()||skg::field<editor::Vec3>(t_rows[parsed].data()+4,4).y!=3)return;
            recent_resolve_callback=[&,changed](unsigned,editor::ResourceKey&){pose_capture_resolve_failure=true;expire(changed);};
        };
        ::drain();CHECK(!seen&&t_failed==1&&!t_failure.empty()&&pending.empty()&&!t_restores);
        CHECK(parsed_images.empty()&&!parsed_owner_count&&!parsed_owner_bytes&&!validation_depth);
        pose_capture_read_callback={};recent_resolve_callback={};pose_capture_resolve_failure=false;
        release(std::exchange(after.p,nullptr));
    }
}
void test_pose_capture_projection_identity_survives_input_hold(){
    RecentRoutes routes;std::string error;
    // This is the real Runtime callback shape: submission clears the
    // authorizing gesture identity while the read-only project identity stays
    // valid. A value proof must hit during adoption without granting a grab.
    auto before=pose_capture_setup();bool awaiting=false;
    C.gesture_scope=[&]{return awaiting?GestureScope{}:t_scope;};
    C.projection_scope=[] {return t_scope;};pose_capture_move();Snapshot after;PoseCaptureProof proof;
    {
        ValidationScope outer;
        CHECK(capture_pose_resource(t_live.data(),before.act,before,pose_capture_bindings(before),*before.metadata,after,proof,error)==RecentMatches::Match);
        CHECK(proof.scope==t_scope);remember_projection(after,proof.assets,proof.scope,proof.generation);
    }
    CHECK(!parsed_owner_count);awaiting=true;const auto reads=t_reads;
    CHECK(!C.gesture_scope().epoch&&C.projection_scope()==t_scope);
    CHECK(validate(after,after.document,error)&&t_reads==reads);
    Snapshot observed;CHECK(recent_matches(after,after.document,generation,observed,error)==RecentMatches::Match&&t_reads==reads);
    CHECK(observed.bytes==after.bytes&&observed.document==after.document);
    C.gate=[](const auto&){return false;};const auto presses=t_presses;
    CHECK(tool_native(t_tool_pointer,1,0,0)==1);
    CHECK(t_presses==presses&&!held_tool&&!gesture_before_resource&&pending.empty());
    CHECK(skg::field<uint8_t>(t_history.data(),0x20)==1&&!t_restores); // Native Start was never reached.
    for(unsigned changed=0;changed<3;++changed){
        before=pose_capture_setup();C.projection_scope=[] {return t_scope;};pose_capture_move();
        {
            ValidationScope outer;
            CHECK(capture_pose_resource(t_live.data(),before.act,before,pose_capture_bindings(before),*before.metadata,after,proof,error)==RecentMatches::Match);
            remember_projection(after,proof.assets,proof.scope,proof.generation);
        }
        C.gesture_scope=[] {return GestureScope{};};const auto previous_reads=t_reads;
        if(changed==0)++t_scope.project[0];if(changed==1)++t_scope.epoch;if(changed==2)++t_scope.author.generation;
        CHECK(validate(after,after.document,error)&&t_reads>previous_reads); // Old project/epoch/peer proof cannot hit.
        CHECK(!t_restores&&!parsed_owner_count);
    }
    // Projection identity can expire independently in a callback; closing only
    // the input identity would miss this failure while the producer is sealed.
    before=pose_capture_setup();auto projection=t_scope;C.projection_scope=[&]{return projection;};
    pose_capture_move();a_read=uintptr_t(pose_capture_read);
    pose_capture_read_callback=[&](void*){++projection.epoch;};
    {
        ValidationScope outer;Snapshot refused;refused.bytes={0xCD};PoseCaptureProof unsealed;
        CHECK(capture_pose_resource(t_live.data(),before.act,before,pose_capture_bindings(before),*before.metadata,refused,unsealed,error)==RecentMatches::Error);
        CHECK(refused.bytes==std::vector<uint8_t>{0xCD}&&unsealed.assets.empty());
    }
    pose_capture_read_callback={};C.projection_scope={};CHECK(t_allocations.empty()&&!parsed_owner_count);
}
void test_supported_pose_serialized_before_owner(){
    RecentRoutes routes;std::string error;
    for(unsigned scenario=0;scenario<6;++scenario){
        if(std::getenv("SPORECOOP_FIXTURE_TRACE")){std::printf("serialized_before_owner scenario=%u\n",scenario);std::fflush(stdout);}
        const auto before=pose_capture_setup();const auto bindings=pose_capture_bindings(before);
        Owned original(construct());CHECK(original.p);t_copy(original.p,t_live.data());
        pose_capture_move();if(scenario==1)skg::field<uint32_t>(t_rows[t_live.data()].data()+4,0x100)=78;
        if(scenario==5)skg::field<uint32_t>(t_live.data(),0x2C4C)=42; // Native local-camera normalization remains supported.
        Snapshot after;PoseCaptureProof proof;Gesture gesture;gesture.generation=generation;gesture.before=before;
        {
            ValidationScope images;
            CHECK(capture_pose_resource(t_live.data(),before.act,before,bindings,*before.metadata,after,proof,error)==RecentMatches::Match);
            remember_projection(after,proof.assets,proof.scope,proof.generation);gesture.after=after;
            if(scenario==2)gesture.after.metadata->genre^=1;
            if(scenario==3)gesture.after.bindings.front().object.generation^=1;
            if(scenario==4)gesture.after.document.placements.begin()->second.properties[0xC0012345]=editor::StructuredValue{7,{8},{}};
            CHECK(supported_pose_image(gesture,error)==RecentMatches::Different); // Typed images alone grant no opaque proof.
            const auto hits=opaque_pose_hits,fallbacks=opaque_pose_fallbacks;
            const auto writes=work_stats().writes.calls;const auto copies=t_pose_row_copies,inserts=t_pose_inserts;
            {
                SerializedBeforeScope serialized(original.p,bindings,before);
                if(scenario==0){
                    CHECK(supported_changes(gesture,error));
                    CHECK(opaque_pose_hits==hits+1&&opaque_pose_fallbacks==fallbacks);
                    CHECK(work_stats().writes.calls==writes+1&&t_pose_row_copies==copies&&t_pose_inserts==inserts);
                }else{
                    CHECK(supported_pose_image(gesture,error)==RecentMatches::Different&&opaque_pose_hits==hits);
                    if(scenario==1)CHECK(!supported_changes(gesture,error)&&opaque_pose_fallbacks==fallbacks+1);
                    if(scenario==5)CHECK(supported_changes(gesture,error)&&opaque_pose_fallbacks==fallbacks+1);
                }
            }
            CHECK(!serialized_before_witness&&!t_restores);
        }
        release(std::exchange(original.p,nullptr));CHECK(t_allocations.empty()&&!parsed_owner_count);
    }
}
void test_supported_pose_history_normalization_and_expiration(){
    RecentRoutes routes;std::string error;
    // A fresh NativeRead of BEFORE can normalize an unknown field to the very
    // unauthorized delta in AFTER. Only the original serialized native owner
    // may seed the forward comparison. Test the real Start/Commit/drain route.
    for(unsigned scenario=0;scenario<14;++scenario){
        const auto before=pose_capture_setup();a_read=uintptr_t(pose_capture_read);
        unsigned seen=0;const auto hits=opaque_pose_hits;
        C.gesture=[&](Gesture gesture){
            ++seen;CHECK(serialized_before_witness&&serialized_before_witness->resource);
            const auto original_rows=t_rows[serialized_before_witness->resource];
            const auto writes=work_stats().writes.calls;
            if(scenario==0){
                const auto copier=a_copy;pose_proof_copy_calls=0;a_copy=uintptr_t(pose_proof_normalizing_copy);t_alloc_fail=true;
                CHECK(supported_changes(gesture,error)&&opaque_pose_hits==hits+1);
                CHECK(work_stats().writes.calls==writes+1&&!pose_proof_copy_calls);a_copy=copier;t_alloc_fail=false;
            }else if(scenario<=2){
                // Force the new Read boundary after the original native Write
                // witness, instead of accidentally borrowing an older parse.
                parsed_images.clear();parsed_image_bytes=0;
                pose_capture_read_callback=[&](void* resource){skg::field<uint32_t>(t_rows[resource].data()+4,0x100)=scenario==1?78u:77u;};
                ReadImage normalized_before;
                if(scenario==1){
                    CHECK(read_image(gesture.before,normalized_before,error));
                    CHECK(skg::field<uint32_t>(t_rows[normalized_before.p].data()+4,0x100)==78&&validate(gesture.before,gesture.before.document,error));
                }
                CHECK(supported_pose_image(gesture,error)==RecentMatches::Different&&opaque_pose_hits==hits);
                CHECK(skg::field<uint32_t>(t_rows[serialized_before_witness->resource].data()+4,0x100)==77);
            }else if(scenario<=8){
                recent_write_callback=[&](void*,unsigned){
                    if(scenario==3)++generation;
                    if(scenario==4)++t_scope.epoch;
                    if(scenario==5)skg::field<void*>(t_mode.data(),0x74)=t_other_data.data();
                    if(scenario==6)skg::field<void*>(t_data.data(),0x10)=nullptr;
                    if(scenario==7)++skg::field<editor::ResourceKey>(t_live.data(),8).instance;
                    if(scenario==8)++t_scope.author.generation;
                };
                CHECK(supported_pose_image(gesture,error)==RecentMatches::Error&&!error.empty()&&opaque_pose_hits==hits);
                recent_write_callback={};
            }else if(scenario==9){
                unsigned nested=0;recent_write_callback=[&](void*,unsigned){
                    if(nested++)return;
                    CHECK(serialized_before_witness->busy);
                    CHECK(supported_pose_image(gesture,error)==RecentMatches::Different);
                    CHECK(supported_changes(gesture,error)); // Separate immutable parses, never the temporarily posed owner.
                };
                CHECK(supported_changes(gesture,error)&&opaque_pose_hits==hits+1&&nested>=3);recent_write_callback={};
            }else if(scenario==13){
                recent_write_callback=[&](void*,unsigned){editor_native::shutdown();};
                CHECK(supported_pose_image(gesture,error)==RecentMatches::Error&&opaque_pose_hits==hits);recent_write_callback={};
            }else{
                if(scenario==11)recent_fail_write=recent_all_writes+1;
                else recent_write_callback=[&](void*,unsigned){if(scenario==12)++generation;throw std::runtime_error("injected native Write failure");};
                CHECK(supported_pose_image(gesture,error)==(scenario==12?RecentMatches::Error:RecentMatches::Different));
                CHECK(opaque_pose_hits==hits);recent_write_callback={};recent_fail_write=0;
            }
            CHECK(!serialized_before_witness->busy&&t_rows[serialized_before_witness->resource]==original_rows);
            std::vector<uint8_t> restored;CHECK(serialize(serialized_before_witness->resource,restored,error)&&restored==gesture.before.bytes);
        };
        {ScopedPoseInput input;start(t_data.data());}pose_capture_move();
        if(scenario==1||scenario==2)skg::field<uint32_t>(t_rows[t_live.data()].data()+4,0x100)=78;
        Owned after(construct());CHECK(after.p);t_copy(after.p,t_live.data());skg::field<void*>(t_entry.data(),0x14)=after.p;
        commit(t_data.data());CHECK(pending.size()==1&&pending.front().pose_candidate);
        ::drain();CHECK(seen==1&&!serialized_before_witness&&!t_restores);
        pose_capture_read_callback={};recent_write_callback={};release(std::exchange(after.p,nullptr));
        CHECK(t_allocations.empty()&&!parsed_owner_count&&!parsed_owner_bytes&&!validation_depth);
    }
}
void test_pose_capture_proofs(){
    if(std::getenv("SPORECOOP_FIXTURE_TRACE"))std::setvbuf(stdout,nullptr,_IONBF,0);
    auto run=[](const char* name,void(*test)()){
        if(std::getenv("SPORECOOP_FIXTURE_TRACE")){std::printf("CASE %s\n",name);std::fflush(stdout);}test();
    };
    run("owned_read_lifetime",test_pose_capture_owned_read_lifetime);
    run("serialized_projection_and_reuse",test_pose_capture_serialized_projection_and_reuse);
    run("normalization_and_identity_rejection",test_pose_capture_normalization_and_identity_rejection);
    run("dense_bindings_and_companions",test_pose_capture_dense_bindings_and_companions);
    run("captain_and_resolution_closure",test_pose_capture_captain_and_resolution_closure);
    run("history_scope",test_pose_capture_history_scope);
    run("failed_callback_cannot_fallback",test_pose_capture_failed_callback_cannot_fallback_into_replacement);
    run("projection_identity_input_hold",test_pose_capture_projection_identity_survives_input_hold);
    run("serialized_before_owner",test_supported_pose_serialized_before_owner);
    run("history_normalization_and_expiration",test_supported_pose_history_normalization_and_expiration);
}
}
