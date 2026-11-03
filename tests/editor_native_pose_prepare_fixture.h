// Observer preparation through the public adapter, with controlled i686
// resources. The baseline is deliberately usable with the retained C30 source.
// These counters prove avoided work; they are not running-game frame timings.
namespace {
void pose_prepare_progress(const char* name,unsigned item=UINT32_MAX){
    if(item==UINT32_MAX)std::printf("pose prepare case=%s\n",name);
    else std::printf("pose prepare case=%s item=%u\n",name,item);
    std::fflush(stdout);
}
uint32_t pose_prepare_version=17;
std::function<void(void*)> pose_prepare_read_callback;
std::array<uint8_t,0x4E0> pose_prepare_class_act;
bool SK_THISCALL pose_prepare_read(void* resource,Stream* stream){
    if(!t_pose_read(resource,stream))return false;
    // Actual modern NativeRead publishes its resource version here. The small
    // pose codec does not serialize a native root header, so its reader owns
    // this exact fixture field instead of manufacturing a projected proof.
    skg::field<uint32_t>(resource,0x2CA8)=pose_prepare_version;
    if(pose_prepare_read_callback)pose_prepare_read_callback(resource);
    return true;
}
struct PosePrepareRoutes {
    RecentRoutes routes;
    ~PosePrepareRoutes(){pose_prepare_read_callback={};pose_prepare_version=17;}
};
Snapshot pose_prepare_setup(){
    pose_prepare_version=17;pose_prepare_read_callback={};pose_prepare_class_act.fill(0);
    auto base=pose_capture_extended_setup();a_read=uintptr_t(pose_prepare_read);
    skg::field<uint32_t>(t_live.data(),0x2CA8)=pose_prepare_version;
    base.document.project=t_scope.project;base.document.epoch=t_scope.epoch;
    std::string error;CHECK(validate(base,base.document,error));return base;
}
void pose_prepare_field(editor::Document& next,editor::Change& change,editor::ObjectId id,
                        editor::FieldKey field,std::optional<editor::Value> value){
    auto& placement=next.placements.at(id);const auto before=editor::value(placement,field);
    CHECK(before!=value);
    const auto expected=editor::field_revision(placement,field);
    if(field.field==editor::Field::Position)placement.position=std::get<editor::Vec3>(*value);
    else if(field.field==editor::Field::Rotation)placement.rotation=std::get<editor::Rotation>(*value);
    else if(field.field==editor::Field::Scale)placement.scale=std::get<float>(*value);
    else{CHECK(field.field==editor::Field::Property);if(value)placement.properties[field.property]=*value;else placement.properties.erase(field.property);}
    placement.revision=next.revision;placement.field_revisions[field]=next.revision;
    change.patch.fields.push_back({id,field,before,std::move(value),expected});
}
std::pair<editor::Document,editor::Change> pose_prepare_target(const Snapshot& base,unsigned field=0){
    auto next=base.document;next.revision=base.document.revision+1;editor::Change change;change.revision=next.revision;
    const auto id=next.placements.begin()->first;
    const auto property=[](uint32_t key){return editor::FieldKey{editor::Field::Property,key};};
    if(field==0)pose_prepare_field(next,change,id,{editor::Field::Position,0},editor::Vec3{4,5,6});
    if(field==1)pose_prepare_field(next,change,id,{editor::Field::Rotation,0},editor::Rotation{0,1,0,0});
    if(field==2)pose_prepare_field(next,change,id,{editor::Field::Scale,0},2.f);
    if(field==3)pose_prepare_field(next,change,id,property(kRelativeScaleProperty),.75f);
    if(field==4)pose_prepare_field(next,change,id,property(kElevationProperty),4.f);
    if(field==5)pose_prepare_field(next,change,id,property(kSoundPitchProperty),1.25f);
    if(field==6)pose_prepare_field(next,change,id,property(kSoundGainProperty),.25f);
    if(field==7)pose_prepare_field(next,change,id,property(kDestinationPositionProperty),editor::Vec3{7,8,9});
    if(field==8)pose_prepare_field(next,change,id,property(kDestinationRotationProperty),editor::Rotation{1,0,0,0});
    if(field==9)pose_prepare_field(next,change,id,property(kDestinationScaleProperty),3.f);
    return {std::move(next),std::move(change)};
}
void test_pose_prepare_modern_cached_base_avoids_projection(){
    for(unsigned field=0;field<10;++field){
        PosePrepareRoutes routes;const auto base=pose_prepare_setup();const auto [target,change]=pose_prepare_target(base,field);
        const auto live=t_rows[t_live.data()];const auto source=skg::field<editor::ResourceKey>(t_live.data(),8);
        const auto stats=work_stats();Snapshot prepared;std::string error;
        CHECK(prepare(base,target,change,nullptr,prepared,error));
        CHECK(prepared.document==target&&prepared.source==base.source&&prepared.bindings==base.bindings&&prepared.metadata==base.metadata);
        const auto finished=work_stats();
        if(finished.projections.calls!=stats.projections.calls || finished.reads.calls!=stats.reads.calls+2)
            std::printf("pose prepare work field=%u reads=%llu projections=%llu writes=%llu\n",field,
                static_cast<unsigned long long>(finished.reads.calls-stats.reads.calls),
                static_cast<unsigned long long>(finished.projections.calls-stats.projections.calls),
                static_cast<unsigned long long>(finished.writes.calls-stats.writes.calls));
        // Genuine C30 RED: it projects the after image once and reads it twice
        // in addition to its first independent mutable owner (3 Reads total).
        CHECK(finished.projections.calls==stats.projections.calls);
        CHECK(finished.reads.calls==stats.reads.calls+2);
        CHECK(finished.writes.calls==stats.writes.calls+3);
        CHECK(finished.snapshot_copies.calls==stats.snapshot_copies.calls+2);
        for(size_t i=0;i<size_t(WorkStep::Count);++i){
            const auto& step=finished.steps[i];const auto& prior=stats.steps[i];
            const auto calls=i<size_t(WorkStep::BaseRead)?0u:1u;
            CHECK(step.work.calls==prior.work.calls+calls);
            if(calls){CHECK(step.begin>0&&step.end>=step.begin&&step.frame==t_frame);
                CHECK(step.work.ticks-prior.work.ticks>=step.end-step.begin);}
        }
        CHECK(validate(prepared,target,error));
        CHECK(t_rows[t_live.data()]==live&&skg::field<editor::ResourceKey>(t_live.data(),8)==source&&!t_restores&&!t_transforms);
        CHECK(t_allocations.empty());
    }
    // Passive instrumentation must not turn a diagnostic SDK failure into a
    // proof/application error or make a single additional Native call.
    PosePrepareRoutes routes;pose_prepare_setup();const auto stats=work_stats();
    auto& controlled_host=*const_cast<SkHost*>(H);
    const auto callback=controlled_host.frame_index;controlled_host.frame_index=[]()->uint64_t{throw std::bad_alloc();};
    {StepTiming diagnostic(WorkStep::BaseRead);}
    controlled_host.frame_index=callback;const auto finished=work_stats();
    CHECK(finished.reads.calls==stats.reads.calls&&finished.writes.calls==stats.writes.calls&&finished.projections.calls==stats.projections.calls);
    CHECK(finished.steps[size_t(WorkStep::BaseRead)].work.calls==stats.steps[size_t(WorkStep::BaseRead)].work.calls+1);
    CHECK(finished.steps[size_t(WorkStep::BaseRead)].frame==0);
}
void test_pose_prepare_unsupported_fields_keep_full_semantics(){
    for(unsigned reason=0;reason<4;++reason){
        PosePrepareRoutes routes;const auto base=pose_prepare_setup();auto [target,change]=pose_prepare_target(base);
        const auto id=target.placements.begin()->first;
        if(reason==0)pose_prepare_field(target,change,id,{editor::Field::Property,0x12345678},editor::StructuredValue{0x50525031,{1,2,3},{}});
        if(reason==1)pose_prepare_field(target,change,id,{editor::Field::Property,kSoundPitchProperty},std::nullopt);
        if(reason==2)pose_prepare_field(target,change,id,{editor::Field::Property,kDestinationPositionProperty},std::nullopt);
        if(reason==3)pose_prepare_field(target,change,id,{editor::Field::Property,kElevationProperty},int64_t(4));
        const auto live=t_rows[t_live.data()];Snapshot refused;refused.bytes={0xCD};std::string error;
        CHECK(!prepare(base,target,change,nullptr,refused,error));
        CHECK(!error.empty()&&refused.bytes==std::vector<uint8_t>{0xCD});
        CHECK(t_rows[t_live.data()]==live&&!t_restores&&!t_transforms&&t_allocations.empty());
    }
}
void test_pose_prepare_read_normalization_cannot_acquire_provenance(){
    for(unsigned reason=0;reason<3;++reason){
        PosePrepareRoutes routes;const auto base=pose_prepare_setup();const auto [target,change]=pose_prepare_target(base);
        const auto live=t_rows[t_live.data()];
        pose_prepare_read_callback=[reason](void* resource){
            auto* marker=t_rows[resource].data()+4;
            if(skg::field<editor::Vec3>(marker,4)!=editor::Vec3{4,5,6})return;
            // Persistently normalize only AFTER parses, including any complete
            // fallback. A single Read ordinal would let a retry hide the delta.
            if(reason==0)skg::field<editor::Vec3>(marker,4).y+=1;
            if(reason==1)skg::field<uint32_t>(marker,0x100)^=1;
            if(reason==2)skg::field<uint32_t>(marker,0)=2048;
        };
        Snapshot refused;refused.bytes={0xCD};std::string error;
        CHECK(!prepare(base,target,change,nullptr,refused,error));
        CHECK(!error.empty()&&refused.bytes==std::vector<uint8_t>{0xCD});
        CHECK(t_rows[t_live.data()]==live&&!t_restores&&!t_transforms&&t_allocations.empty());
    }
}
void test_pose_prepare_legacy_payloads_keep_complete_projection(){
    for(unsigned legacy=0;legacy<4;++legacy){
        PosePrepareRoutes routes;pose_prepare_setup();
        if(legacy==0)pose_prepare_version=16;
        if(legacy==1)skg::field<uint32_t>(t_rows[t_live.data()].data()+4,0x20C)=1;
        if(legacy>=2){
            skg::field<void*>(t_pose_class.data()+8,0x70)=pose_prepare_class_act.data();
            skg::field<void*>(t_pose_class.data()+8,0x74)=pose_prepare_class_act.data()+pose_prepare_class_act.size();
            if(legacy==2){skg::field<uint32_t>(pose_prepare_class_act.data(),0x46C)=1;skg::field<uint32_t>(pose_prepare_class_act.data(),0x470)=1;}
            else skg::field<uint32_t>(pose_prepare_class_act.data(),0x460)=1;
        }
        Snapshot base;std::string error;CHECK(capture(base,error));base.document.project=t_scope.project;base.document.epoch=t_scope.epoch;
        CHECK(validate(base,base.document,error));const auto [target,change]=pose_prepare_target(base);
        const auto stats=work_stats();const auto live=t_rows[t_live.data()];Snapshot prepared;
        CHECK(prepare(base,target,change,nullptr,prepared,error));
        CHECK(work_stats().projections.calls>stats.projections.calls&&work_stats().reads.calls>=stats.reads.calls+3);
        CHECK(prepared.document==target&&t_rows[t_live.data()]==live&&!t_restores&&t_allocations.empty());
    }
}
std::function<void(void*)> pose_prepare_owning_read_callback;
std::function<void(void*)> pose_prepare_owning_write_callback,pose_prepare_owning_release_callback,pose_prepare_owning_before_release;
unsigned pose_prepare_owning_reads=0;
bool SK_THISCALL pose_prepare_owning_read(void* resource,Stream* stream){
    if(!tcg_read(resource,stream))return false;
    ++pose_prepare_owning_reads;
    skg::field<uint32_t>(resource,0x2CA8)=17;
    if(pose_prepare_owning_read_callback)pose_prepare_owning_read_callback(resource);
    return true;
}
bool SK_THISCALL pose_prepare_owning_write(void* resource,Stream* stream){
    if(pose_prepare_owning_write_callback)pose_prepare_owning_write_callback(resource);
    return tcg_write(resource,stream);
}
int SK_THISCALL pose_prepare_owning_release(void* resource){
    const bool last=skg::field<int>(resource,4)==1&&t_allocations.contains(resource);
    if(last&&pose_prepare_owning_before_release)pose_prepare_owning_before_release(resource);
    const auto released=tcg_release(resource);
    if(last&&pose_prepare_owning_release_callback)pose_prepare_owning_release_callback(resource);
    return released;
}
struct PosePrepareOwningRoutes {
    uintptr_t writer=a_write,reader=a_read,copier=a_copy,acts=a_copy_acts,crew=a_copy_crew,shown=a_layout_shown;
    uintptr_t initialization=a_scenario_initialization,modes=a_get_simulator_mode_manager;const SkHost* host=H;
    ~PosePrepareOwningRoutes(){
        pose_prepare_owning_read_callback={};pose_prepare_owning_write_callback={};pose_prepare_owning_release_callback={};pose_prepare_owning_before_release={};
        recent_resolve_callback={};CHECK(t_allocations.empty());
        tcg_rows.clear();tmc_properties.clear();act_cache_rows.clear();
        a_write=writer;a_read=reader;a_copy=copier;a_copy_acts=acts;a_copy_crew=crew;a_layout_shown=shown;
        a_scenario_initialization=initialization;a_get_simulator_mode_manager=modes;H=host;reset();
    }
};
Snapshot pose_prepare_owning_setup(bool resolver=false){
    pose_prepare_owning_read_callback={};pose_prepare_owning_write_callback={};pose_prepare_owning_release_callback={};pose_prepare_owning_before_release={};
    tcg_setup();terrain_grounding.clear();a_read=uintptr_t(pose_prepare_owning_read);a_write=uintptr_t(pose_prepare_owning_write);
    t_resource_vt[1]=reinterpret_cast<void*>(pose_prepare_owning_release);
    skg::field<uint32_t>(t_live.data(),0x2CA8)=17;
    if(resolver){
        static SkHost host;host=*H;host.sym=pose_capture_symbol;H=&host;recent_resolves=0;recent_resolved={17,19,23};recent_resolve_callback={};
        skg::field<uint64_t>(t_live.data(),0x140)=17;
    }
    Snapshot base;std::string error;CHECK(capture(base,error));base.document.project=t_scope.project;base.document.epoch=t_scope.epoch;
    // tcg_setup intentionally rekeys retained terrain rows for its native-only
    // identity tests. A detached canonical document must also advance its ID
    // allocator past those rows before public prepare can accept a new head.
    for(const auto& [id,row]:base.document.terrain){(void)row;base.document.next_id=std::max(base.document.next_id,id.id+1);}
    CHECK(validate(base,base.document,error));
    const bool canonical=editor::valid(base.document,editor::Limits{},error);
    if(!canonical)std::printf("owning pose base: %s next=%llu revision=%llu\n",error.c_str(),
        static_cast<unsigned long long>(base.document.next_id),static_cast<unsigned long long>(base.document.revision));
    CHECK(canonical);pose_prepare_owning_reads=0;return base;
}
void test_pose_prepare_owning_cached_base_avoids_projection(){
    PosePrepareOwningRoutes routes;const auto base=pose_prepare_owning_setup();const auto [target,change]=pose_prepare_target(base);
    std::string error;CHECK(editor::valid(target,editor::Limits{},error));
    const auto live=t_rows.at(t_live.data());const auto classes=tcg_rows.at(t_live.data()).classes;
    const auto acts=act_cache_rows.at(t_live.data()).acts;const auto stats=work_stats();Snapshot prepared;
    const bool okay=prepare(base,target,change,nullptr,prepared,error);const auto finished=work_stats();
    std::printf("owning pose prepare okay=%u reads=%llu projections=%llu writes=%llu error=%s\n",unsigned(okay),
        static_cast<unsigned long long>(finished.reads.calls-stats.reads.calls),
        static_cast<unsigned long long>(finished.projections.calls-stats.projections.calls),
        static_cast<unsigned long long>(finished.writes.calls-stats.writes.calls),error.c_str());
    CHECK(okay&&prepared.document==target&&prepared.source==base.source&&prepared.bindings==base.bindings&&prepared.metadata==base.metadata);
    CHECK(finished.projections.calls==stats.projections.calls&&finished.reads.calls==stats.reads.calls+2);
    CHECK(finished.writes.calls==stats.writes.calls+3);
    CHECK(finished.snapshot_copies.calls==stats.snapshot_copies.calls+2);
    CHECK(validate(prepared,target,error));
    CHECK(t_rows.at(t_live.data())==live&&tcg_rows.at(t_live.data()).classes==classes&&act_cache_rows.at(t_live.data()).acts==acts);
    CHECK(!t_restores&&!t_transforms&&t_allocations.empty());
}
void test_pose_prepare_owning_subtree_normalization_is_not_hidden(){
    for(unsigned reason=0;reason<4;++reason){
        pose_prepare_progress("owning-subtree",reason);
        PosePrepareOwningRoutes routes;const auto base=pose_prepare_owning_setup();const auto [target,change]=pose_prepare_target(base);
        const auto live=t_rows.at(t_live.data());const auto classes=tcg_rows.at(t_live.data()).classes;
        const auto acts=act_cache_rows.at(t_live.data()).acts;const auto crew_capacity=skg::field<uint32_t>(t_live.data(),0x238);bool fired=false;
        pose_prepare_owning_read_callback=[reason,crew_capacity,&fired](void* resource){
            // The old full guard also Reads class/act fingerprint fragments.
            // Those intentionally have no placement roster and cannot be the
            // AFTER image this callback is meant to taint.
            const auto found=t_rows.find(resource);if(found==t_rows.end()||found->second.size()<0x238)return;
            auto* marker=found->second.data()+4;
            if(skg::field<editor::Vec3>(marker,4)!=editor::Vec3{4,5,6})return;
            fired=true;
            // Unlike the minimal pose codec, tcg_write serializes all these
            // owned subtrees. Repeated AFTER parses receive the same taint.
            if(reason==0)tcg_rows.at(resource).classes.at(8+0x100)^=1;
            if(reason==1)act_cache_rows.at(resource).acts.at(0x500)^=1;
            if(reason==2)skg::field<uint32_t>(resource,0x238)=crew_capacity==1?2:1;
            if(reason==3)tcg_rows.at(resource).companions.at(0).at(0x31)^=1;
        };
        Snapshot refused;refused.bytes={0xCD};std::string error;
        CHECK(!prepare(base,target,change,nullptr,refused,error));
        if(!fired)std::printf("owning pose taint reason=%u not reached: %s\n",reason,error.c_str());
        CHECK(fired);
        CHECK(!error.empty()&&refused.bytes==std::vector<uint8_t>{0xCD});
        CHECK(t_rows.at(t_live.data())==live&&tcg_rows.at(t_live.data()).classes==classes&&act_cache_rows.at(t_live.data()).acts==acts);
        CHECK(skg::field<uint32_t>(t_live.data(),0x238)==crew_capacity&&!t_restores&&t_allocations.empty());
    }
}
void test_pose_prepare_callback_expiry_and_input_source_are_fenced(){
    for(unsigned reason=0;reason<3;++reason){
        pose_prepare_progress("owning-callback",reason);
        PosePrepareOwningRoutes routes;auto base=pose_prepare_owning_setup();const auto [target,change]=pose_prepare_target(base);
        const auto source=base.source;const auto live=t_rows.at(t_live.data());bool fired=false;
        pose_prepare_owning_read_callback=[&](void*){if(std::exchange(fired,true))return;
            if(reason==0)++generation;if(reason==1)++t_scope.epoch;if(reason==2)++base.source.instance;
        };
        Snapshot result;result.bytes={0xCD};std::string error;const auto stats=work_stats();const auto okay=prepare(base,target,change,nullptr,result,error);
        if(!fired)std::printf("owning pose callback reason=%u okay=%u reads=%llu projections=%llu writes=%llu error=%s\n",reason,unsigned(okay),
            static_cast<unsigned long long>(work_stats().reads.calls-stats.reads.calls),
            static_cast<unsigned long long>(work_stats().projections.calls-stats.projections.calls),
            static_cast<unsigned long long>(work_stats().writes.calls-stats.writes.calls),error.c_str());
        CHECK(fired);
        if(reason<2)CHECK(!okay&&!error.empty()&&result.bytes==std::vector<uint8_t>{0xCD});
        else{
            // The adapter may reject changed caller storage, or finish from an
            // independently owned immutable original. It cannot adopt the new
            // key supplied by a reentrant callback as fresh alias authority.
            CHECK(!okay || (result.source==source&&result.document==target));
            if(!okay)CHECK(result.bytes==std::vector<uint8_t>{0xCD}&&!error.empty());
        }
        CHECK(t_rows.at(t_live.data())==live&&!t_restores&&t_allocations.empty());
    }
    PosePrepareOwningRoutes routes;const auto base=pose_prepare_owning_setup(true);const auto [target,change]=pose_prepare_target(base);
    const auto live=t_rows.at(t_live.data());recent_resolves=0;
    recent_resolve_callback=[](unsigned call,editor::ResourceKey& key){if(call==1)++key.instance;};
    Snapshot refused;refused.bytes={0xCD};std::string error;
    CHECK(!prepare(base,target,change,nullptr,refused,error));
    CHECK(recent_resolves>0&&!error.empty()&&refused.bytes==std::vector<uint8_t>{0xCD});
    CHECK(t_rows.at(t_live.data())==live&&!t_restores&&t_allocations.empty());
}
void test_pose_prepare_producer_release_is_inside_the_publication_fence(){
    for(unsigned reason=0;reason<3;++reason){
        pose_prepare_progress("producer-release",reason);
        PosePrepareOwningRoutes routes;const auto base=pose_prepare_owning_setup(reason==2);const auto [target,change]=pose_prepare_target(base);
        const auto live=t_rows.at(t_live.data());void* producer=nullptr;bool released=false;
        pose_prepare_owning_read_callback=[&](void* resource){if(pose_prepare_owning_reads==1)producer=resource;};
        pose_prepare_owning_release_callback=[&](void* resource){if(resource!=producer)return;released=true;
            if(reason==0)++generation;if(reason==1)++t_scope.epoch;
            if(reason==2)++recent_resolved.instance;
        };
        Snapshot refused;refused.bytes={0xCD};std::string error;
        CHECK(!prepare(base,target,change,nullptr,refused,error));
        CHECK(released&&!error.empty()&&refused.bytes==std::vector<uint8_t>{0xCD});
        CHECK(t_rows.at(t_live.data())==live&&!t_restores&&t_allocations.empty());
    }
}
void test_pose_prepare_reverse_write_cannot_reuse_replaced_storage(){
    PosePrepareOwningRoutes routes;const auto base=pose_prepare_owning_setup();const auto [target,change]=pose_prepare_target(base);
    const auto live=t_rows.at(t_live.data());const auto before=base.document.placements.begin()->second.position;
    void* after=nullptr;bool replaced=false;std::vector<uint8_t> retired_storage;
    pose_prepare_owning_read_callback=[&](void* resource){if(pose_prepare_owning_reads==2)after=resource;};
    pose_prepare_owning_write_callback=[&](void* resource){
        if(replaced||resource!=after||skg::field<editor::Vec3>(t_rows.at(resource).data()+4,4)!=before)return;
        auto replacement=t_rows.at(resource);const auto original=t_rows.at(resource).data();
        t_rows.at(resource).swap(replacement);tcg_bind(resource);replaced=true;retired_storage=std::move(replacement);
        std::fill(retired_storage.begin(),retired_storage.end(),0xD3);
        CHECK(t_rows.at(resource).data()!=original);
    };
    Snapshot result;result.bytes={0xCD};std::string error;const auto stats=work_stats();
    const bool okay=prepare(base,target,change,nullptr,result,error);
    CHECK(replaced);
    if(okay)CHECK(result.document==target&&work_stats().projections.calls>stats.projections.calls);
    else CHECK(!error.empty()&&result.bytes==std::vector<uint8_t>{0xCD});
    CHECK(std::all_of(retired_storage.begin(),retired_storage.end(),[](uint8_t byte){return byte==0xD3;}));
    CHECK(t_rows.at(t_live.data())==live&&!t_restores&&t_allocations.empty());
}
void test_pose_prepare_reverse_write_exception_and_reentry_restore_private_after(){
    for(bool nested:{false,true}){
        pose_prepare_progress("reverse-exception-reentry",unsigned(nested));
        PosePrepareOwningRoutes routes;const auto base=pose_prepare_owning_setup();const auto [target,change]=pose_prepare_target(base);
        const auto live=t_rows.at(t_live.data());const auto before=base.document.placements.begin()->second.position;
        const auto base_bytes=base.bytes;const auto base_document=base.document;
        void* after=nullptr;bool fired=false,released=false;std::array<uint8_t,0x24> expected_transform{};
        std::array<uint8_t,0x28> expected_companions{};uint32_t expected_height=0,expected_flags=0;
        pose_prepare_owning_read_callback=[&](void* resource){
            if(pose_prepare_owning_reads!=2)return;after=resource;auto* marker=t_rows.at(resource).data()+4;
            std::memcpy(expected_transform.data(),marker+4,expected_transform.size());
            std::memcpy(expected_companions.data(),marker+0x1E0,expected_companions.size());
            expected_height=skg::field<uint32_t>(marker,0x220);expected_flags=skg::field<uint32_t>(marker,0x224);
        };
        pose_prepare_owning_before_release=[&](void* resource){
            if(resource!=after)return;after=nullptr;released=true;auto* marker=t_rows.at(resource).data()+4;
            CHECK(std::memcmp(marker+4,expected_transform.data(),expected_transform.size())==0);
            CHECK(std::memcmp(marker+0x1E0,expected_companions.data(),expected_companions.size())==0);
            CHECK(skg::field<uint32_t>(marker,0x220)==expected_height&&skg::field<uint32_t>(marker,0x224)==expected_flags);
        };
        pose_prepare_owning_write_callback=[&](void* resource){
            if(fired||resource!=after||skg::field<editor::Vec3>(t_rows.at(resource).data()+4,4)!=before)return;
            fired=true;if(!nested)throw std::runtime_error("injected reverse Write failure");
            const auto stats=work_stats();Snapshot inside;inside.bytes={0xCD};std::string error;
            const bool okay=prepare(base,target,change,nullptr,inside,error);
            if(okay)CHECK(inside.document==target&&work_stats().projections.calls>stats.projections.calls);
            else CHECK(!error.empty()&&inside.bytes==std::vector<uint8_t>{0xCD});
            CHECK(base.bytes==base_bytes&&base.document==base_document&&t_rows.at(t_live.data())==live);
        };
        Snapshot result;result.bytes={0xCD};std::string error;const auto stats=work_stats();
        const bool okay=prepare(base,target,change,nullptr,result,error);
        CHECK(fired&&released);
        if(okay)CHECK(result.document==target&&work_stats().projections.calls>stats.projections.calls);
        else CHECK(!error.empty()&&result.bytes==std::vector<uint8_t>{0xCD});
        CHECK(base.bytes==base_bytes&&base.document==base_document&&t_rows.at(t_live.data())==live);
        CHECK(!t_restores&&!t_transforms&&t_allocations.empty());
    }
}
std::weak_ptr<int> pose_prepare_scope_token;
bool pose_prepare_scope_returned=false;
void test_pose_prepare_scope_callback_owns_its_active_capture(){
    {
        PosePrepareRoutes routes;pose_prepare_setup();auto token=std::make_shared<int>(7);pose_prepare_scope_token=token;
        C.projection_scope=[keep=std::move(token)]{
            const auto observed=t_scope;C.projection_scope={};
            CHECK(!pose_prepare_scope_token.expired());return observed;
        };
        CHECK(value_scope()==t_scope&&pose_prepare_scope_token.expired());
    }
    for(unsigned point=0;point<2;++point)for(bool expire:{false,true}){
        pose_prepare_progress("scope-self-replacement",point*2+unsigned(expire));
        PosePrepareOwningRoutes routes;const auto base=pose_prepare_owning_setup();const auto [target,change]=pose_prepare_target(base);
        const auto live=t_rows.at(t_live.data());void* producer=nullptr;bool installed=false;pose_prepare_scope_returned=false;
        const auto install=[&]{
            installed=true;auto token=std::make_shared<int>(11);pose_prepare_scope_token=token;
            auto replacement=t_scope;if(expire)++replacement.epoch;
            C.projection_scope=[keep=std::move(token),replacement]{
                const auto observed=t_scope;C.projection_scope=[replacement]{return replacement;};
                CHECK(!pose_prepare_scope_token.expired());pose_prepare_scope_returned=true;return observed;
            };
        };
        pose_prepare_owning_read_callback=[&](void* resource){
            if(pose_prepare_owning_reads!=1)return;producer=resource;if(!point)install();
        };
        pose_prepare_owning_release_callback=[&](void* resource){if(point&&resource==producer&&!installed)install();};
        Snapshot result;result.bytes={0xCD};std::string error;const bool okay=prepare(base,target,change,nullptr,result,error);
        CHECK(installed&&pose_prepare_scope_returned&&pose_prepare_scope_token.expired());
        if(expire)CHECK(!okay&&!error.empty()&&result.bytes==std::vector<uint8_t>{0xCD});
        else CHECK(okay&&result.document==target&&result.source==base.source);
        CHECK(t_rows.at(t_live.data())==live&&!t_restores&&!t_transforms&&t_allocations.empty());
    }
}
void test_pose_prepare_bounded_inputs_and_native_allocation_failure(){
    for(unsigned reason=0;reason<4;++reason){
        PosePrepareRoutes routes;auto base=pose_prepare_setup();auto [target,change]=pose_prepare_target(base);
        if(reason==0)base.bytes.resize((32u<<20)+1);
        if(reason==1)base.metadata->text[0].assign(4097,u'x');
        if(reason==2)change.summary.assign(editor::Limits{}.summary_bytes+1,'x');
        const auto live=t_rows.at(t_live.data());const auto stats=work_stats();const auto reads=t_reads;
        if(reason==3)t_alloc_fail=true;
        Snapshot refused;refused.bytes={0xCD};std::string error;
        CHECK(!prepare(base,target,change,nullptr,refused,error));t_alloc_fail=false;
        CHECK(!error.empty()&&refused.bytes==std::vector<uint8_t>{0xCD});
        CHECK(work_stats().writes.calls==stats.writes.calls&&work_stats().projections.calls==stats.projections.calls&&t_reads==reads);
        if(reason<3)CHECK(work_stats().reads.calls==stats.reads.calls);
        CHECK(t_rows.at(t_live.data())==live&&!t_restores&&t_allocations.empty());
    }
}
void test_native_pose_prepare_projection(){
    pose_prepare_progress("modern");
    test_pose_prepare_modern_cached_base_avoids_projection();
    pose_prepare_progress("unsupported");
    test_pose_prepare_unsupported_fields_keep_full_semantics();
    pose_prepare_progress("read-normalization");
    test_pose_prepare_read_normalization_cannot_acquire_provenance();
    pose_prepare_progress("legacy");
    test_pose_prepare_legacy_payloads_keep_complete_projection();
    pose_prepare_progress("owning-positive");
    test_pose_prepare_owning_cached_base_avoids_projection();
    pose_prepare_progress("owning-subtrees");
    test_pose_prepare_owning_subtree_normalization_is_not_hidden();
    pose_prepare_progress("owning-callbacks");
    test_pose_prepare_callback_expiry_and_input_source_are_fenced();
    pose_prepare_progress("producer-release");
    test_pose_prepare_producer_release_is_inside_the_publication_fence();
    pose_prepare_progress("pool-replacement");
    test_pose_prepare_reverse_write_cannot_reuse_replaced_storage();
    pose_prepare_progress("reverse-exception-reentry");
    test_pose_prepare_reverse_write_exception_and_reentry_restore_private_after();
    pose_prepare_progress("scope-self-replacement");
    test_pose_prepare_scope_callback_owns_its_active_capture();
    pose_prepare_progress("bounds-allocation");
    test_pose_prepare_bounded_inputs_and_native_allocation_failure();
}
}
