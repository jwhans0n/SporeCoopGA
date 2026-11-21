// Private synchronous preparation proof. No native owner enters a value cache.
struct PreparedPoseOriginal {
    uint32_t marker=0;
    std::array<uint8_t,0x24> transform{};
    std::array<uint8_t,0x28> companions{};
    uint32_t elevation=0,flags=0;
    bool relative=false,pitch=false,gain=false,destination=false,height=false;
};
bool prepared_pose_fields(const editor::Document& before,const editor::Document& after,const editor::Change& change){
    const auto known=[](uint32_t key){return key==kRelativeScaleProperty || key==kElevationProperty ||
        key==kSoundPitchProperty || key==kSoundGainProperty || key==kDestinationPositionProperty ||
        key==kDestinationRotationProperty || key==kDestinationScaleProperty;};
    for(const auto& edit:change.patch.fields)if(edit.field.field==editor::Field::Property &&
        (!known(edit.field.property) || !edit.before || !edit.after || edit.before->index()!=edit.after->index()))return false;
    for(const auto& [id,placement]:before.placements){
        const auto next=after.placements.find(id);if(next==after.placements.end() || placement.properties.size()!=next->second.properties.size())return false;
        for(const auto& [key,value]:placement.properties){
            const auto found=next->second.properties.find(key);
            if(!known(key) || found==next->second.properties.end() || value.index()!=found->second.index())return false;
        }
        const auto destination=placement.properties.contains(kDestinationPositionProperty);
        if(destination!=placement.properties.contains(kDestinationRotationProperty) ||
           destination!=placement.properties.contains(kDestinationScaleProperty))return false;
    }
    return true;
}
bool prepared_pose_modern(void* resource,const Snapshot& image){
    if(!skg::readable(resource,kResourceSize) || skg::field<uint32_t>(resource,0x2CA8)!=17)return false;
    Rows classes,acts,markers;
    if(!rows(resource,0x2BF4,0x27E8,1024,classes) || !rows(resource,0x23C,0x534,8,acts) ||
       !rows(resource,0x2C10,0x238,kPlacementLimit,markers))return false;
    const auto count=uint32_t((acts.end-acts.begin)/0x534);
    if(count!=image.document.acts.size())return false;
    std::set<editor::ObjectId> class_ids;
    for(auto row=classes.begin;row!=classes.end;row+=0x27E8){
        if(!class_ids.insert({(2ull<<32)|skg::field<uint32_t>(row,0),1}).second)return false;
        Rows settings;if(!rows(row+8,0x70,0x4E0,8,settings))return false;
        if(!image.document.class_schema.empty() && size_t(settings.end-settings.begin)!=count*0x4E0)return false;
        for(auto act=settings.begin;act!=settings.end;act+=0x4E0){
            // F2CF60..F2CFA5 can resolve external localized text even in v17.
            // Empty inline storage need not be a null/static-pointer value.
            if((skg::field<uint32_t>(act,0x470)&&skg::field<uint32_t>(act,0x46C)) ||
               skg::field<uintptr_t>(act,0x45C)!=skg::field<uintptr_t>(act,0x460))return false;
        }
    }
    if(class_ids!=image.document.classes)return false;
    size_t live=0;
    for(auto row=markers.begin;row!=markers.end;row+=0x238)if(live_slot(row)){
        auto* marker=row+4;++live;
        if(skg::field<uintptr_t>(marker,0x20C)!=skg::field<uintptr_t>(marker,0x210))return false;
        if(!image.document.settings_schema.empty()){
            Rows companions;if(!rows(marker,0x28,0x34,8,companions) || size_t(companions.end-companions.begin)!=count*0x34)return false;
        }
    }
    return live==image.bindings.size();
}
struct PreparedPoseContext {
    uint64_t native_generation=generation;GestureScope scope;
    uintptr_t reader=a_read,writer=a_write,constructor=a_ctor;const SkHost* host=H;
    PreparedPoseContext(){const auto callback=C.projection_scope?C.projection_scope:C.gesture_scope;scope=callback?callback():GestureScope{};}
    bool current()const{
        try{
        if(generation!=native_generation || reader!=a_read || writer!=a_write || constructor!=a_ctor || host!=H)return false;
        const auto callback=C.projection_scope?C.projection_scope:C.gesture_scope;
        const auto observed=callback?callback():GestureScope{};
        return observed==scope && generation==native_generation && reader==a_read && writer==a_write && constructor==a_ctor && host==H;
        }catch(...){return false;}
    }
};
bool prepared_pose_proving=false;
RecentMatches prepared_pose_projection(const PreparedPoseContext& context,const std::shared_ptr<const ProjectedSnapshot>& proof,
                                       const Snapshot& base,Snapshot& prepared,Owned& producer,
                                       const std::vector<PreparedPoseOriginal>& originals,std::string& error){
    const auto expired=[&]{error="The prepared pose proof belongs to an expired native value context";return RecentMatches::Error;};
    ScopedResolutionAudit resolution;
    const auto different=[&]{
        if(resolution.audit.failed){error="The prepared pose asset resolution changed during its proof";return RecentMatches::Error;}
        return context.current()?RecentMatches::Different:expired();
    };
    if(prepared_pose_proving || !proof)return different();
    struct Busy {Busy(){prepared_pose_proving=true;}~Busy(){prepared_pose_proving=false;}} busy;
    try{
        if(!context.current() || proof->generation!=context.native_generation || proof->scope!=context.scope)return expired();
        if(!same_projection_image(proof->image,base) || !validate_projection(proof->image,base,base.document,error) ||
           !prepared_pose_modern(producer.p,base))return different();
        std::map<uint32_t,editor::ObjectId> supplied;
        for(const auto& binding:base.bindings)if(!supplied.emplace(binding.marker,binding.object).second)return different();
        std::vector<editor::ResourceKey> before_assets,after_assets;
        if(!resolved_asset_values(producer.p,base.document,before_assets,error) || !context.current())return different();
        Owned after(step_parse(WorkStep::AfterRead,prepared.bytes,error));if(!after.p)return different();
        if(!context.current())return expired();
        if(!prepared_pose_modern(after.p,prepared))return different();
        std::vector<uint8_t> serialized;
        if(!step_serialize(WorkStep::FixedpointWrite,after.p,serialized,error) || !context.current())return different();
        if(serialized!=prepared.bytes)return different(); // No Read normalization can seed a projection.
        if(!captain_sidecar_matches(after.p,prepared,prepared.document,error) || !context.current() ||
           !resolved_asset_values(after.p,prepared.document,after_assets,error) || !context.current() || before_assets!=after_assets)return different();
        std::vector<ResolvedAssetWitness> assets;
        if(!asset_witnesses(after.p,prepared.document,assets,error) || !context.current() || assets!=proof->assets || resolution.audit.failed)return different();
        Snapshot native;
        {WorkTimer copy(native_work.snapshot_copies);native=snapshot_values::with_image(proof->image,prepared);}
        Rows markers;if(!rows(after.p,0x2C10,0x238,kPlacementLimit,markers))return different();
        size_t live=0;
        for(auto row=markers.begin;row!=markers.end;row+=0x238)if(live_slot(row)){
            const auto binding=supplied.find(slot_index(markers,row));if(binding==supplied.end())return different();
            auto found=native.document.placements.find(binding->second);if(found==native.document.placements.end())return different();
            auto& placement=found->second;auto* marker=row+4;++live;
            if(skg::field<uint32_t>(marker,0)!=uint32_t(placement.class_id.id))return different();
            placement.position=skg::field<editor::Vec3>(marker,4);placement.rotation=skg::field<editor::Rotation>(marker,0x10);
            placement.scale=skg::field<float>(marker,0x20);
            const auto assign=[&]<class T>(uint32_t key,uint32_t offset){
                auto value=placement.properties.find(key);if(value==placement.properties.end())return true;
                if(!std::get_if<T>(&value->second))return false;value->second=skg::field<T>(marker,offset);return true;
            };
            if(!assign.template operator()<float>(kRelativeScaleProperty,0x24) || !assign.template operator()<float>(kElevationProperty,0x220) ||
               !assign.template operator()<float>(kSoundPitchProperty,0x1E0) || !assign.template operator()<float>(kSoundGainProperty,0x1E4) ||
               !assign.template operator()<editor::Vec3>(kDestinationPositionProperty,0x1E8) ||
               !assign.template operator()<editor::Rotation>(kDestinationRotationProperty,0x1F4) ||
               !assign.template operator()<float>(kDestinationScaleProperty,0x204))return different();
        }
        if(live!=native.document.placements.size() || !validate_projection(native,prepared,prepared.document,error))return different();
        // This owner is unique and never indexed in parsed_images. Preserve its
        // actual AFTER primitives even when reversal/Write throws or reenters.
        struct Restore {
            struct Row {uint32_t slot,kind,key;std::array<uint8_t,0x24> transform;std::array<uint8_t,0x28> companions;uint32_t elevation,flags;};
            void* owner;uintptr_t first,last;std::vector<Row> retained;
            bool current()const{
                Rows map;if(!rows(owner,0x2C10,0x238,kPlacementLimit,map) || uintptr_t(map.begin)!=first || uintptr_t(map.end)!=last)return false;
                for(const auto& row:retained){auto* marker=marker_for_key(owner,row.slot);
                    if(!marker || skg::field<uint32_t>(marker,0)!=row.kind || skg::field<uint32_t>(marker,0x228)!=row.key)return false;}
                return true;
            }
            bool apply(){
                // A Write/current callback can replace a private row buffer.
                // Never keep its borrowed marker addresses across that call.
                if(!current())return false;
                for(const auto& row:retained){auto* marker=marker_for_key(owner,row.slot);
                    std::memcpy(skg::ptr_add<void>(marker,4),row.transform.data(),row.transform.size());
                    std::memcpy(skg::ptr_add<void>(marker,0x1E0),row.companions.data(),row.companions.size());
                    skg::field<uint32_t>(marker,0x220)=row.elevation;skg::field<uint32_t>(marker,0x224)=row.flags;}
                retained.clear();return true;
            }
            ~Restore(){if(!retained.empty())apply();}
        } restore{after.p,uintptr_t(markers.begin),uintptr_t(markers.end),{}};
        restore.retained.reserve(originals.size());
        for(const auto& original:originals){
            auto* marker=marker_for_key(after.p,original.marker);if(!marker)return different();
            const auto flags=skg::field<uint32_t>(marker,0x224);if((flags^original.flags)&~0x16u)return different();
            Restore::Row retained{original.marker,skg::field<uint32_t>(marker,0),skg::field<uint32_t>(marker,0x228),{},{},skg::field<uint32_t>(marker,0x220),flags};
            std::memcpy(retained.transform.data(),skg::ptr_add<void>(marker,4),retained.transform.size());
            std::memcpy(retained.companions.data(),skg::ptr_add<void>(marker,0x1E0),retained.companions.size());restore.retained.push_back(retained);
            std::memcpy(skg::ptr_add<void>(marker,4),original.transform.data(),0x20);
            if(original.relative)std::memcpy(skg::ptr_add<void>(marker,0x24),original.transform.data()+0x20,4);
            if(original.pitch)std::memcpy(skg::ptr_add<void>(marker,0x1E0),original.companions.data(),4);
            if(original.gain)std::memcpy(skg::ptr_add<void>(marker,0x1E4),original.companions.data()+4,4);
            if(original.destination)std::memcpy(skg::ptr_add<void>(marker,0x1E8),original.companions.data()+8,0x20);
            if(original.height)skg::field<uint32_t>(marker,0x220)=original.elevation;
            skg::field<uint32_t>(marker,0x224)=(flags&~0x16u)|(original.flags&0x16u);
        }
        if(!step_serialize(WorkStep::InverseWrite,after.p,serialized,error) || !context.current() || !restore.current())return different();
        if(serialized!=base.bytes)return different();
        if(!current_asset_witnesses(assets) || !context.current() || !restore.current() || resolution.audit.failed)return different();
        // The restoration guard must run before Release callbacks, and Release
        // must finish before any value-only proof is made reusable.
        if(!restore.apply())return different();
        release(std::exchange(after.p,nullptr));release(std::exchange(producer.p,nullptr));
        if(!context.current() || !current_asset_witnesses(assets) || !context.current() || resolution.audit.failed)return different();
        remember_projection(native,assets,context.scope,context.native_generation);
        if(!context.current() || !current_asset_witnesses(assets) || !context.current() || resolution.audit.failed)return different();
        error.clear();return RecentMatches::Match;
    }catch(...){return different();}
}
