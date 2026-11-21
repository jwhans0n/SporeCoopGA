// Experimental private preparation. Only this adapter retains the unique AFTER
// owner; tickets and all Runtime/worker inputs contain values only.
struct StagedPosePrepare {
    struct Row {
        uint32_t slot=0,kind=0,key=0,elevation=0,flags=0;
        std::array<uint8_t,0x24> transform{};
        std::array<uint8_t,0x28> companions{};
    };
    PosePrepareTicket ticket;
    DWORD thread=0;uint64_t frame=0;
    decltype(SkHost::frame_index) frame_index=nullptr;
    bool retired=false;
    SceneOwner owner;
    Snapshot base,prepared,native;
    editor::Change change;
    std::optional<PreparedPoseContext> context;
    std::function<bool()> authority;
    std::shared_ptr<const ProjectedSnapshot> proof;
    std::vector<PreparedPoseOriginal> originals;
    std::vector<ResolvedAssetWitness> assets;
    std::map<uint64_t,editor::ResourceKey> resolutions;
    Owned after;
    uintptr_t first=0,last=0;
    std::vector<Row> rows;
};
std::shared_ptr<StagedPosePrepare> staged_pose_prepare;
uint64_t staged_pose_prepare_serial=0;
bool staged_pose_preparing=false;
enum class StagedPoseFallback {None,Unavailable,NoContinuation,Grounding,RotationTicket,Shape,Destination,Projection,LegacyBase,LegacyAfter,ReadNormalization};
StagedPoseFallback staged_pose_last_fallback=StagedPoseFallback::None;
uint64_t staged_pose_starts=0,staged_pose_ready=0,staged_pose_errors=0,staged_pose_cancelled=0,staged_pose_fallbacks=0;
bool staged_pose_raw_owner(const SceneOwner& pin){
    skg::ReadOnlyMemoryScope memory;
    auto* m=mode();auto* d=data(m);auto* r=resource(d);
    return uintptr_t(m)==pin.mode && uintptr_t(d)==pin.data && uintptr_t(r)==pin.resource &&
        skg::readable(m,0xDC) && skg::field<int>(m,0xCC)==1 && !skg::field<int>(m,0xD0) &&
        skg::readable(r,kResourceSize) && skg::field<editor::ResourceKey>(r,8)==pin.source;
}
bool staged_pose_current(const std::shared_ptr<StagedPosePrepare>& work){
    const auto raw=[&]{return work && !work->retired && staged_pose_prepare==work && supported && H &&
        work->thread==GetCurrentThreadId() && work->thread==native_owner_thread && work->context && work->context->native_generation==generation &&
        work->context->host==H && work->frame_index && H->frame_index==work->frame_index &&
        work->context->reader==a_read && work->context->writer==a_write &&
        work->context->constructor==a_ctor && staged_pose_raw_owner(work->owner);};
    if(!raw())return false;
    try{const auto authority=work->authority;
        return work->context->current() && raw() && authority && authority() && raw();
    }catch(...){return false;}
}
bool staged_pose_rows_current(const StagedPosePrepare& work){
    Rows markers;if(!work.after.p || !rows(work.after.p,0x2C10,0x238,kPlacementLimit,markers) ||
       uintptr_t(markers.begin)!=work.first || uintptr_t(markers.end)!=work.last)return false;
    size_t live=0;for(auto row=markers.begin;row!=markers.end;row+=0x238)live+=live_slot(row);
    if(live!=work.rows.size())return false;
    for(const auto& row:work.rows){auto* marker=marker_for_key(work.after.p,row.slot);
        if(!marker || skg::field<uint32_t>(marker,0)!=row.kind || skg::field<uint32_t>(marker,0x228)!=row.key ||
           skg::field<uint32_t>(marker,0x220)!=row.elevation || skg::field<uint32_t>(marker,0x224)!=row.flags ||
           std::memcmp(skg::ptr_add<void>(marker,4),row.transform.data(),row.transform.size()) ||
           std::memcmp(skg::ptr_add<void>(marker,0x1E0),row.companions.data(),row.companions.size()))return false;
    }
    return true;
}
void clear_staged_pose_prepare(){
    struct Cleaning {bool previous=staged_pose_preparing;Cleaning(){staged_pose_preparing=true;}~Cleaning(){staged_pose_preparing=previous;}} cleaning;
    if(staged_pose_prepare){if(!staged_pose_prepare->retired)++staged_pose_cancelled;staged_pose_prepare->retired=true;}
    staged_pose_prepare.reset();
}
bool staged_pose_values(StagedPosePrepare& work,std::string& error){
    std::map<uint32_t,editor::ObjectId> supplied;
    for(const auto& binding:work.base.bindings)if(!supplied.emplace(binding.marker,binding.object).second)return false;
    {WorkTimer copy(native_work.snapshot_copies);work.native=snapshot_values::with_image(work.proof->image,work.prepared);}
    Rows markers;if(!rows(work.after.p,0x2C10,0x238,kPlacementLimit,markers))return false;
    work.first=uintptr_t(markers.begin);work.last=uintptr_t(markers.end);
    for(auto row=markers.begin;row!=markers.end;row+=0x238)if(live_slot(row)){
        const auto slot=slot_index(markers,row);const auto binding=supplied.find(slot);
        if(binding==supplied.end())return false;
        auto found=work.native.document.placements.find(binding->second);
        if(found==work.native.document.placements.end())return false;
        auto& placement=found->second;auto* marker=row+4;
        if(skg::field<uint32_t>(marker,0)!=uint32_t(placement.class_id.id))return false;
        placement.position=skg::field<editor::Vec3>(marker,4);placement.rotation=skg::field<editor::Rotation>(marker,0x10);
        placement.scale=skg::field<float>(marker,0x20);
        const auto assign=[&]<class T>(uint32_t key,uint32_t offset){
            const auto found=placement.properties.find(key);if(found==placement.properties.end())return true;
            if(!std::get_if<T>(&found->second))return false;found->second=skg::field<T>(marker,offset);return true;
        };
        if(!assign.template operator()<float>(kRelativeScaleProperty,0x24) || !assign.template operator()<float>(kElevationProperty,0x220) ||
           !assign.template operator()<float>(kSoundPitchProperty,0x1E0) || !assign.template operator()<float>(kSoundGainProperty,0x1E4) ||
           !assign.template operator()<editor::Vec3>(kDestinationPositionProperty,0x1E8) ||
           !assign.template operator()<editor::Rotation>(kDestinationRotationProperty,0x1F4) ||
           !assign.template operator()<float>(kDestinationScaleProperty,0x204))return false;
        StagedPosePrepare::Row retained;retained.slot=slot;retained.kind=skg::field<uint32_t>(marker,0);
        retained.key=skg::field<uint32_t>(marker,0x228);retained.elevation=skg::field<uint32_t>(marker,0x220);
        retained.flags=skg::field<uint32_t>(marker,0x224);
        std::memcpy(retained.transform.data(),skg::ptr_add<void>(marker,4),retained.transform.size());
        std::memcpy(retained.companions.data(),skg::ptr_add<void>(marker,0x1E0),retained.companions.size());work.rows.push_back(retained);
    }
    return work.rows.size()==work.native.document.placements.size() &&
        validate_projection(work.native,work.prepared,work.prepared.document,error);
}
PosePrepareResult staged_pose_begin(const Snapshot& base,const editor::Document& document,const editor::Change& change,
                                   const std::function<bool()>& authority,PosePrepareTicket& ticket,std::string& error){
    using Result=PosePrepareResult;
    if(staged_pose_preparing || staged_pose_prepare){++staged_pose_errors;error="Another private Native preparation is pending";return Result::Error;}
    const auto fallback=[&](StagedPoseFallback reason){++staged_pose_fallbacks;staged_pose_last_fallback=reason;error.clear();return Result::Fallback;};
    std::set<editor::ObjectId> targets;
    if(!supported || !H || !H->frame_index || !native_owner_thread || GetCurrentThreadId()!=native_owner_thread)return fallback(StagedPoseFallback::Unavailable);
    if(!authority)return fallback(StagedPoseFallback::NoContinuation);
    if(!terrain_grounding.empty())return fallback(StagedPoseFallback::Grounding);
    if(terrain_capture_rotations)return fallback(StagedPoseFallback::RotationTicket);
    if(!exact_pose_change(base.document,document,change,targets) || !prepared_pose_fields(base.document,document,change))return fallback(StagedPoseFallback::Shape);
    for(const auto& field:change.patch.fields)if(field.field.field!=editor::Field::Position && field.field.field!=editor::Field::Rotation)return fallback(StagedPoseFallback::Shape);
    // Destination rendering has an additional actor graph. Keep that existing
    // synchronous route until its complete callback fence is staged separately.
    for(auto id:targets)if(document.placements.at(id).properties.contains(kDestinationPositionProperty))return fallback(StagedPoseFallback::Destination);
    struct Busy {Busy(){staged_pose_preparing=true;}~Busy(){staged_pose_preparing=false;}} busy;
    std::shared_ptr<StagedPosePrepare> work;
    Owned producer;
    const auto stop=[&](Result result,const char* message,StagedPoseFallback reason=StagedPoseFallback::Projection){
        // A conservative fallback still has to finish every private Release
        // before deciding that the original context may use its old producer.
        if(work)release(std::exchange(work->after.p,nullptr));
        release(std::exchange(producer.p,nullptr));
        if(result==Result::Fallback && work && work->context && !staged_pose_current(work)){
            result=Result::Error;message="The staged Native fallback cleanup expired its original context";
        }
        if(work)work->retired=true;
        if(staged_pose_prepare==work)staged_pose_prepare.reset();
        if(result==Result::Fallback){++staged_pose_fallbacks;staged_pose_last_fallback=reason;error.clear();}
        else{++staged_pose_errors;if(error.empty())error=message;}
        return result;
    };
    try{
        size_t charge=0;editor::Limits limits;limits.placements=kPlacementLimit;
        if(!transition_entry_charge(base,charge) || charge>(64u<<20)/3 ||
           editor::bytes(document)>(64u<<20)/2 ||
           editor::bytes(change.patch)>limits.document_bytes || editor::bytes(change.inverse)>limits.document_bytes ||
           change.summary.size()>limits.summary_bytes || change.allocated.size()>kPlacementLimit || change.clone_sources.size()>kPlacementLimit ||
           !editor::valid(document,limits,error))return stop(Result::Error,"The staged Native inputs exceed their owned bounds");
        const auto cost=uint64_t(charge)*3+uint64_t(editor::bytes(document))*2+editor::bytes(change.patch)+editor::bytes(change.inverse)+
            change.summary.size()+uint64_t(base.bindings.size())*(sizeof(StagedPosePrepare::Row)+sizeof(PreparedPoseOriginal)+64)*4;
        if(cost>(64u<<20))return stop(Result::Error,"The staged Native owned values exceed 64 MiB");
        if(staged_pose_prepare_serial==UINT64_MAX)return stop(Result::Error,"The staged Native serial is exhausted");
        work=std::make_shared<StagedPosePrepare>();work->base=base;
        {WorkTimer copy(native_work.snapshot_copies);work->prepared=snapshot_values::with_document(base,document);}
        work->change=change;work->authority=authority;work->thread=GetCurrentThreadId();work->frame_index=H->frame_index;
        auto* m=mode();auto* d=data(m);auto* r=resource(d);
        if(!skg::readable(r,kResourceSize))return stop(Result::Fallback,"",StagedPoseFallback::Unavailable);
        work->owner={uintptr_t(m),uintptr_t(d),uintptr_t(r),skg::field<editor::ResourceKey>(r,8)};
        work->ticket.serial=++staged_pose_prepare_serial;staged_pose_prepare=work;
        work->frame=work->frame_index();work->context.emplace();
        ScopedResolutionAudit resolution;
        if(!staged_pose_current(work))return stop(Result::Error,"The staged Native owner expired before preparation");
        work->proof=projected_snapshot(work->base,error);
        if(!staged_pose_current(work))return stop(Result::Error,"The staged Native proof context expired");
        if(!work->proof)return stop(Result::Fallback,"");
        if(work->proof->generation!=work->context->native_generation || work->proof->scope!=work->context->scope ||
           work->base.document.project!=work->context->scope.project || work->base.document.epoch!=work->context->scope.epoch ||
           work->prepared.document.project!=work->context->scope.project || work->prepared.document.epoch!=work->context->scope.epoch)
            return stop(Result::Error,"The staged Native projection belongs to another value context");
        if(!validate(work->base,work->base.document,error) || !staged_pose_current(work))return stop(Result::Error,"The staged Native base is invalid");
        bool tracked=false;if(!relative_scale_mode(work->prepared.document,tracked,error))return stop(Result::Error,"The staged pose scale mode is invalid");
        producer.p=step_parse(WorkStep::BaseRead,work->base.bytes,error);
        if(!producer.p || !staged_pose_current(work))return stop(Result::Error,"The staged Native BASE read failed");
        if(!prepared_pose_modern(producer.p,work->base))return stop(Result::Fallback,"",StagedPoseFallback::LegacyBase);
        for(auto id:targets){
            const auto binding=std::find_if(work->base.bindings.begin(),work->base.bindings.end(),[&](const auto& b){return b.object==id;});
            auto* marker=binding==work->base.bindings.end()?nullptr:marker_for_key(producer.p,binding->marker);
            const auto& placement=work->prepared.document.placements.at(id);float relative=0;
            if(!marker || skg::field<uint32_t>(marker,0)!=uint32_t(placement.class_id.id) || !relative_scale_value(placement,tracked,relative,error))
                return stop(Result::Error,"The staged Native target is not bound");
            PreparedPoseOriginal original;original.marker=binding->marker;
            std::memcpy(original.transform.data(),skg::ptr_add<void>(marker,4),original.transform.size());
            std::memcpy(original.companions.data(),skg::ptr_add<void>(marker,0x1E0),original.companions.size());
            original.elevation=skg::field<uint32_t>(marker,0x220);original.flags=skg::field<uint32_t>(marker,0x224);
            original.relative=tracked || work->base.document.placements.at(id).scale!=placement.scale;
            original.pitch=placement.properties.contains(kSoundPitchProperty);original.gain=placement.properties.contains(kSoundGainProperty);
            original.destination=placement.properties.contains(kDestinationPositionProperty);original.height=placement.properties.contains(kElevationProperty);
            work->originals.push_back(original);
            const auto old_scale=skg::field<float>(marker,0x20);
            if(tracked)skg::field<float>(marker,0x24)=relative;else if(old_scale>0)skg::field<float>(marker,0x24)*=placement.scale/old_scale;
            skg::field<editor::Vec3>(marker,4)=placement.position;skg::field<editor::Rotation>(marker,0x10)=placement.rotation;
            skg::field<float>(marker,0x20)=placement.scale;
            const auto assign=[&]<class T>(uint32_t property,uint32_t offset){const auto field=placement.properties.find(property);
                if(field==placement.properties.end())return true;const auto* value=std::get_if<T>(&field->second);
                if(!value)return false;skg::field<T>(marker,offset)=*value;return true;};
            if(!assign.template operator()<float>(kElevationProperty,0x220) || !assign.template operator()<float>(kSoundPitchProperty,0x1E0) ||
               !assign.template operator()<float>(kSoundGainProperty,0x1E4) || !assign.template operator()<editor::Vec3>(kDestinationPositionProperty,0x1E8) ||
               !assign.template operator()<editor::Rotation>(kDestinationRotationProperty,0x1F4) || !assign.template operator()<float>(kDestinationScaleProperty,0x204))
                return stop(Result::Error,"The staged Native companion has the wrong type");
        }
        std::vector<editor::ResourceKey> before_assets,after_assets;
        if(!step_serialize(WorkStep::ProducerWrite,producer.p,work->prepared.bytes,error) || !staged_pose_current(work) ||
           !resolved_asset_values(producer.p,work->base.document,before_assets,error) || !staged_pose_current(work))return stop(Result::Error,"The staged Native producer expired");
        work->after.p=step_parse(WorkStep::AfterRead,work->prepared.bytes,error);
        if(!work->after.p || !staged_pose_current(work))return stop(Result::Error,"The staged Native AFTER read failed");
        if(!prepared_pose_modern(work->after.p,work->prepared))return stop(Result::Fallback,"",StagedPoseFallback::LegacyAfter);
        std::vector<uint8_t> serialized;
        if(!step_serialize(WorkStep::FixedpointWrite,work->after.p,serialized,error) || !staged_pose_current(work))return stop(Result::Error,"The staged Native fixed-point write failed");
        if(serialized!=work->prepared.bytes)return stop(Result::Fallback,"",StagedPoseFallback::ReadNormalization);
        if(!captain_sidecar_matches(work->after.p,work->prepared,work->prepared.document,error) || !staged_pose_current(work) ||
           !resolved_asset_values(work->after.p,work->prepared.document,after_assets,error) || !staged_pose_current(work) || before_assets!=after_assets ||
           !asset_witnesses(work->after.p,work->prepared.document,work->assets,error) || !staged_pose_current(work) || work->assets!=work->proof->assets ||
           !staged_pose_values(*work,error) || resolution.audit.failed)return stop(Result::Error,"The staged Native AFTER projection changed");
        release(std::exchange(producer.p,nullptr));
        if(!staged_pose_current(work) || !staged_pose_rows_current(*work) || !current_asset_witnesses(work->assets) ||
           !staged_pose_current(work) || !staged_pose_rows_current(*work) || resolution.audit.failed)return stop(Result::Error,"The staged Native producer cleanup expired");
        work->resolutions=std::move(resolution.audit.keys);ticket=work->ticket;++staged_pose_starts;
        staged_pose_last_fallback=StagedPoseFallback::None;error.clear();return Result::Pending;
    }catch(...){return stop(Result::Error,"The staged Native preparation failed");}
}
PosePrepareResult staged_pose_finish(PosePrepareTicket ticket,Snapshot& out,std::string& error){
    using Result=PosePrepareResult;
    if(staged_pose_preparing){++staged_pose_errors;error="The staged Native ticket is already being serviced";return Result::Error;}
    struct Busy {Busy(){staged_pose_preparing=true;}~Busy(){staged_pose_preparing=false;}} busy;
    const auto work=staged_pose_prepare;
    if(!work || work->ticket!=ticket || work->thread!=GetCurrentThreadId()){++staged_pose_errors;error="The staged Native ticket is unavailable";return Result::Error;}
    const auto fail=[&](const char* message){++staged_pose_errors;work->retired=true;if(staged_pose_prepare==work)staged_pose_prepare.reset();
        if(error.empty())error=message;return Result::Error;};
    try{
        if(!staged_pose_current(work))return fail("The staged Native continuation expired");
        const auto frame=work->frame_index();
        if(!staged_pose_current(work))return fail("The staged Native frame source expired");
        if(frame<=work->frame){error.clear();return Result::Pending;}
        ScopedResolutionAudit resolution;resolution.audit.keys=work->resolutions;
        if(!staged_pose_rows_current(*work) || !prepared_pose_modern(work->after.p,work->prepared) ||
           !current_asset_witnesses(work->assets) || !staged_pose_current(work) || !staged_pose_rows_current(*work))return fail("The retained Native AFTER owner changed between frames");
        struct Restore {
            StagedPosePrepare& work;bool reversed=false;
            bool current()const{Rows rows;return work.after.p && editor_native::rows(work.after.p,0x2C10,0x238,kPlacementLimit,rows) &&
                uintptr_t(rows.begin)==work.first && uintptr_t(rows.end)==work.last;}
            bool apply(){if(!reversed)return true;if(!current())return false;
                for(const auto& row:work.rows){auto* marker=marker_for_key(work.after.p,row.slot);
                    if(!marker || skg::field<uint32_t>(marker,0)!=row.kind || skg::field<uint32_t>(marker,0x228)!=row.key)return false;}
                for(const auto& row:work.rows){auto* marker=marker_for_key(work.after.p,row.slot);
                    std::memcpy(skg::ptr_add<void>(marker,4),row.transform.data(),row.transform.size());
                    std::memcpy(skg::ptr_add<void>(marker,0x1E0),row.companions.data(),row.companions.size());
                    skg::field<uint32_t>(marker,0x220)=row.elevation;skg::field<uint32_t>(marker,0x224)=row.flags;}
                reversed=false;return true;}
            ~Restore(){if(reversed)apply();}
        } restore{*work};restore.reversed=true;
        for(const auto& original:work->originals){auto* marker=marker_for_key(work->after.p,original.marker);
            if(!marker || ((skg::field<uint32_t>(marker,0x224)^original.flags)&~0x16u))return fail("The staged Native inverse target changed");
            std::memcpy(skg::ptr_add<void>(marker,4),original.transform.data(),0x20);
            if(original.relative)std::memcpy(skg::ptr_add<void>(marker,0x24),original.transform.data()+0x20,4);
            if(original.pitch)std::memcpy(skg::ptr_add<void>(marker,0x1E0),original.companions.data(),4);
            if(original.gain)std::memcpy(skg::ptr_add<void>(marker,0x1E4),original.companions.data()+4,4);
            if(original.destination)std::memcpy(skg::ptr_add<void>(marker,0x1E8),original.companions.data()+8,0x20);
            if(original.height)skg::field<uint32_t>(marker,0x220)=original.elevation;
            auto& flags=skg::field<uint32_t>(marker,0x224);flags=(flags&~0x16u)|(original.flags&0x16u);
        }
        std::vector<uint8_t> serialized;
        if(!step_serialize(WorkStep::InverseWrite,work->after.p,serialized,error) || !staged_pose_current(work) ||
           !restore.current() || serialized!=work->base.bytes || !current_asset_witnesses(work->assets) ||
           !staged_pose_current(work) || resolution.audit.failed)return fail("The staged Native inverse did not recover its exact BASE");
        if(!restore.apply())return fail("The staged Native AFTER cleanup lost its private rows");
        release(std::exchange(work->after.p,nullptr));
        if(!staged_pose_current(work) || !current_asset_witnesses(work->assets) || !staged_pose_current(work) || resolution.audit.failed)
            return fail("The staged Native AFTER Release expired its proof");
        remember_projection(work->native,work->assets,work->context->scope,work->context->native_generation);
        if(!staged_pose_current(work) || !current_asset_witnesses(work->assets) || !staged_pose_current(work) || resolution.audit.failed)
            return fail("The staged Native publication context expired");
        work->retired=true;staged_pose_prepare.reset();++pose_roundtrip_hits;++staged_pose_ready;
        out=std::move(work->prepared);error.clear();return Result::Ready;
    }catch(...){return fail("The staged Native inverse proof failed");}
}
