// Private implementation included once inside editor_native, outside Present.
struct TerrainTransitionProof {
    TerrainTransitionKey key;uintptr_t tool=0;
    Snapshot entry;
    editor_native_terrain_materialization::Recipe rendered;
    editor_native_terrain_materialization::Materialization materialization;
    // Exact pre-transition live slots and rendered/authored XYZ. A recovery
    // preparation has no such readback until its settled refresh captures it.
    std::optional<std::vector<CreationTerrainProof::Placement>> placements;
    size_t owned_bytes=0;
    mutable bool retired=false,normalizing=false;
    mutable std::optional<TerrainTransitionReceipt> saved;
};
namespace {
bool transition_current(const TerrainTransitionKey& key,uintptr_t tool,const TerrainTransitionCurrent& current,bool ready=true){
    if(!current || !current() || !valid_scope(key.scope) || !key.serial || !key.generation)return false;
    const auto native=state();void* m=mode();void* d=data(m);void* r=resource(d);
    if(native.generation!=key.generation || !native.editing || native.playtesting || native.gesture || applying ||
       (ready&&!native.ready) || uintptr_t(terraform_mode())!=tool || !skg::readable(r,kResourceSize))return false;
    const SceneOwner actual{uintptr_t(m),uintptr_t(d),uintptr_t(r),skg::field<editor::ResourceKey>(r,8)};
    if(actual!=key.owner || !current())return false;
    // The caller fence may itself inspect native state. Re-resolve identities
    // after that callback before making any further native call.
    m=mode();d=data(m);r=resource(d);
    return generation==key.generation && editing() && uintptr_t(terraform_mode())==tool && skg::readable(r,kResourceSize) &&
        SceneOwner{uintptr_t(m),uintptr_t(d),uintptr_t(r),skg::field<editor::ResourceKey>(r,8)}==key.owner;
}
struct TransitionRead {
    TerrainReadIdentity identity;TerrainReadIdentity* previous=nullptr;
    TransitionRead(const TerrainTransitionKey& key,uintptr_t tool,const TerrainTransitionCurrent& current,
                   const editor_native_terrain_materialization::Materialization* values):identity{&key,tool,&current,values,false},previous(terrain_read_identity){
        terrain_read_identity=&identity;
    }
    ~TransitionRead(){terrain_read_identity=previous;}
};
bool transition_receipt(const TerrainTransitionTicket& ticket,const TerrainTransitionReceipt& receipt,bool restore,std::string& error){
    const auto& a=receipt.before;const auto& b=receipt.after;
    const bool bootstrap=restore && receipt.kind==TerrainTransitionKind::RecoveryBootstrap && !ticket && a==b;
    if((!bootstrap&&(!ticket || ticket->retired || a!=ticket->key)) || !valid_scope(b.scope) || !b.serial || !b.generation ||
       a.serial!=b.serial || a.scope.project!=b.scope.project || b.generation<a.generation ||
       !b.owner.mode || !b.owner.data || !b.owner.resource ||
       (b.owner.source!=editor::ResourceKey{} && (!b.owner.source.instance || b.owner.source.type!=0x366A930D || !b.owner.source.group)) ||
       bool(receipt.save_generation)!=bool(receipt.save_serial)){
        error="The native terrain transition receipt is invalid or expired";return false;
    }
    const bool same_owner=a.owner.mode==b.owner.mode && a.owner.data==b.owner.data && a.owner.resource==b.owner.resource;
    switch(receipt.kind){
    case TerrainTransitionKind::Save:
        if(restore || a.scope!=b.scope || a.generation!=b.generation || !same_owner || !b.owner.source.instance ||
           !receipt.save_generation || !receipt.save_serial)break;
        return true;
    case TerrainTransitionKind::Export:
        if(restore || a.scope!=b.scope || !same_owner || b.owner.source!=a.owner.source)break;
        if(receipt.save_generation && (!ticket->saved || ticket->saved->save_generation!=receipt.save_generation ||
           ticket->saved->save_serial!=receipt.save_serial))break;
        return true;
    case TerrainTransitionKind::Recovery:
        if(receipt.save_generation || receipt.save_serial || a.scope.author.slot!=b.scope.author.slot || (!restore&&!same_owner))break;
        return true;
    case TerrainTransitionKind::RecoveryBootstrap:
        if(!bootstrap || receipt.save_generation || receipt.save_serial)break;
        return true;
    case TerrainTransitionKind::ExportRebuild:
        if(!restore || !ticket || a!=b || receipt.save_generation || receipt.save_serial)break;
        return true;
    }
    error="The native terrain transition changed an unauthorized identity field";return false;
}
bool transition_recipe_matches(const editor_native_terrain_materialization::Recipe& raw,
                              const editor_native_terrain_materialization::Materialization& values,
                              const Snapshot& expected,std::string& error){
    editor_native_terrain_materialization::Recipe canonical;
    if(!editor_native_terrain_materialization::from_bytes(expected.bytes,canonical,error))return false;
    auto normalized=raw;values.normalize_recipe(normalized);
    if(normalized!=canonical){error="The retained native terrain recipe does not match the expected authored recipe";return false;}
    return true;
}
bool transition_placements_current(const TerrainTransitionProof& proof,std::string& error){
    if(!proof.placements){error="The native terrain transition has no retained placement roster";return false;}
    skg::ReadOnlyMemoryScope memory;
    auto* r=resource(data(mode()));Rows markers;
    if(!rows(r,0x2C10,0x238,kPlacementLimit,markers)){
        error="The native terrain transition placement roster is unavailable";return false;
    }
    size_t count=0;for(auto* raw=markers.begin;raw!=markers.end;raw+=0x238)if(live_slot(raw))++count;
    if(count!=proof.placements->size()){error="The native terrain transition placement roster changed";return false;}
    for(const auto& row:*proof.placements){
        const auto found=proof.entry.document.placements.find(row.live.object);auto* marker=marker_for_key(r,row.live.marker);
        if(found==proof.entry.document.placements.end() || found->second.revision!=row.revision ||
           uint32_t(found->second.class_id.id)!=row.class_index || !exact_grounding_position(found->second.position,row.authored) ||
           marker_id(row.live.marker)!=row.live.object || !marker || skg::field<uint32_t>(marker,0)!=row.class_index ||
           skg::field<uint32_t>(marker,0x228)!=row.native_key || !exact_grounding_position(skg::field<editor::Vec3>(marker,4),row.rendered)){
            error="The native terrain transition lost an exact placement grounding witness";return false;
        }
    }
    return true;
}
bool transition_entry_charge(const Snapshot& entry,size_t& charged){
    constexpr size_t limit=64u<<20;charged=0;
    const auto charge=[&](size_t bytes){if(bytes>limit-charged)return false;charged+=bytes;return true;};
    if(!charge(entry.bytes.size()) || !charge(editor::bytes(entry.document)) || !charge(entry.bindings.size()*sizeof(Binding)))return false;
    if(entry.metadata)for(const auto& text:entry.metadata->text)if(!charge(text.size()*sizeof(char16_t)))return false;
    return true;
}
bool make_transition_ticket(const TerrainTransitionKey& key,uintptr_t tool,const Snapshot& entry,
                            const editor_native_terrain_materialization::Recipe& raw,
                            const editor_native_terrain_materialization::Materialization& values,
                            std::shared_ptr<TerrainTransitionProof>& out,std::string& error,bool retain_placements=false){
    constexpr size_t limit=64u<<20;size_t charged=sizeof(TerrainTransitionProof);
    const auto charge=[&](size_t bytes){if(bytes>limit-charged)return false;charged+=bytes;return true;};
    size_t entry_bytes=0;
    if(!transition_entry_charge(entry,entry_bytes) || !charge(entry_bytes) || !charge(values.owned_bytes())){
        error="The owned native terrain transition exceeds 64 MiB";return false;
    }
    for(const auto& kind:raw.rows)for(const auto& row:kind)if(!charge(sizeof(row)+row.bytes.capacity()+row.references.capacity()*sizeof(editor::ObjectId))){
        error="The owned native terrain transition exceeds 64 MiB";return false;
    }
    std::optional<std::vector<CreationTerrainProof::Placement>> placements;
    if(retain_placements){
        placements.emplace();if(!creation_terrain_placements(entry,*placements,error))return false;
        if(!charge(placements->capacity()*sizeof(CreationTerrainProof::Placement))){
            error="The owned native terrain transition exceeds 64 MiB";return false;
        }
    }
    auto result=std::make_shared<TerrainTransitionProof>();result->key=key;result->tool=tool;result->entry=entry;
    result->rendered=raw;result->materialization=values;result->placements=std::move(placements);
    result->owned_bytes=charged;out=std::move(result);return true;
}
bool transition_observation_matches(const Snapshot& expected,const Snapshot& observed,const TerrainTransitionCurrent& current,std::string& error){
    const bool same_domain=expected.source.type==observed.source.type && expected.source.group==observed.source.group;
    // An unsaved live Scenario has no external key; Runtime may already own a
    // transport root. The observed key is independently pinned by SceneOwner.
    const bool unsaved_alias=observed.source==editor::ResourceKey{} && expected.source.instance &&
        expected.source.type==0x366A930D && expected.source.group;
    if(expected.document!=observed.document || expected.bindings!=observed.bindings || expected.captain!=observed.captain ||
       expected.captain_locked!=observed.captain_locked || expected.metadata!=observed.metadata ||
       (!same_domain&&!unsaved_alias)){
        error="The native terrain observation changed its asserted entry domain";return false;
    }
    Owned original(parse(expected.bytes,error));if(!original.p || !current())return false;
    Owned actual(parse(observed.bytes,error));if(!actual.p || !current())return false;
    std::vector<uint8_t> roundtrip;
    if(!serialize(original.p,roundtrip,error) || !current() || roundtrip!=expected.bytes){
        error="The native terrain entry Read changed its exact image";return false;
    }
    if(!serialize(actual.p,roundtrip,error) || !current() || roundtrip!=observed.bytes){
        error="The native terrain observation Read changed its exact image";return false;
    }
    const auto source=skg::field<editor::ResourceKey>(original.p,8);
    const auto actual_source=skg::field<editor::ResourceKey>(actual.p,8);
    if((source!=editor::ResourceKey{} && source!=expected.source) ||
       (actual_source!=editor::ResourceKey{} && actual_source!=observed.source)){
        error="The native terrain observation has an unrelated embedded source";return false;
    }
    // Only the external root key and existing local camera bytes can differ
    // from the independently supplied entry. No Native Copy can launder an
    // unknown change into the new observed-image ticket.
    struct Restore {
        void* resource;editor::ResourceKey source;std::array<uint8_t,0x20> camera;bool active=true;
        explicit Restore(void* value):resource(value),source(skg::field<editor::ResourceKey>(value,8)){
            std::memcpy(camera.data(),skg::ptr_add<void>(value,0x2C30),camera.size());
        }
        void restore()noexcept{if(!active)return;active=false;skg::field<editor::ResourceKey>(resource,8)=source;
            std::memcpy(skg::ptr_add<void>(resource,0x2C30),camera.data(),camera.size());}
        ~Restore(){restore();}
    } restore{original.p};
    skg::field<editor::ResourceKey>(original.p,8)=actual_source;
    std::memcpy(skg::ptr_add<void>(original.p,0x2C30),skg::ptr_add<void>(actual.p,0x2C30),0x20);
    std::vector<uint8_t> forward;
    if(!serialize(original.p,forward,error) || !current())return false;
    auto compared=observed.bytes;
    if(observed.document.settings.contains(editor_native_terrain::kPlanetObject) &&
       !editor_native_terrain::normalize_supported(compared,forward,error))return false;
    if(compared!=forward){error="The native terrain observation changed an opaque entry field";return false;}
    restore.restore();
    if(!serialize(original.p,roundtrip,error) || !current() || roundtrip!=expected.bytes){
        error="The native terrain observation Write changed its original image";return false;
    }
    if(!serialize(actual.p,roundtrip,error) || !current() || roundtrip!=observed.bytes){
        error="The native terrain observation Write changed its observed image";return false;
    }
    release(std::exchange(original.p,nullptr));release(std::exchange(actual.p,nullptr));
    if(!current()){error="The native terrain observation owner expired during Release";return false;}
    return true;
}
bool retain_transition_observation(TerrainTransitionProof& proof,const Snapshot& observed,std::string& error){
    // Runtime retains this exact checked readback, including its independently
    // pinned live source. A canonical transport envelope may have another key.
    // Do not replace the pre-callback roster with a fresh, unproved roster.
    size_t old_bytes=0,new_bytes=0;constexpr size_t limit=64u<<20;
    if(observed.source!=proof.key.owner.source || observed.document!=proof.entry.document){
        error="The native terrain readback changed its retained source or document";return false;
    }
    if(!transition_entry_charge(proof.entry,old_bytes) || !transition_entry_charge(observed,new_bytes) ||
       old_bytes>proof.owned_bytes || new_bytes>limit-(proof.owned_bytes-old_bytes)){
        error="The owned native terrain transition exceeds 64 MiB";return false;
    }
    proof.entry=observed;proof.owned_bytes=proof.owned_bytes-old_bytes+new_bytes;return true;
}
bool transition_observe(const TerrainTransitionKey& key,uintptr_t tool,const Snapshot& expected,
                        const editor_native_terrain_materialization::Recipe& raw,
                        const editor_native_terrain_materialization::Materialization& values,
                        const TerrainTransitionCurrent& current,Snapshot& observed,std::string& error){
    if(!transition_current(key,tool,current)){error="The native terrain transition owner is unavailable";return false;}
    TransitionRead read(key,tool,current,&values);
    editor_native_terrain_materialization::Recipe actual;
    if(!materialization_recipe(terraform_mode(),actual,error) || actual!=raw){error="The native terrain recipe changed during its owned transition";return false;}
    Snapshot candidate;
    if(!capture_draft(candidate,error) || !validate(candidate,expected.document,error))return false;
    candidate.document=expected.document;
    if(read.identity.invalid || !transition_current(key,tool,current) || !materialization_recipe(terraform_mode(),actual,error) || actual!=raw){
        error="The native terrain transition changed during its strict readback";return false;
    }
    observed=std::move(candidate);return true;
}
bool normalize_transition_grounding(const TerrainTransitionProof& proof,Snapshot& image,
                                    const std::function<bool()>& current,std::string& error){
    if(!proof.placements){error="The native Save has no retained placement grounding roster";return false;}
    if(image.bindings!=proof.entry.bindings){error="The native Save placement bindings changed";return false;}
    if(std::none_of(proof.placements->begin(),proof.placements->end(),[](const auto& row){return row.grounded;}))return true;
    Owned owned(parse(image.bytes,error));if(!owned.p)return false;
    if(!current()){error="The native Save grounding owner expired during Read";return false;}
    std::vector<uint8_t> roundtrip;
    if(!serialize(owned.p,roundtrip,error))return false;
    if(!current() || roundtrip!=image.bytes){error="The native Save grounding Read changed its exact serialized image";return false;}
    struct Position {uint32_t dense=0,class_index=0,native_key=0;editor::Vec3 original,authored;};
    std::vector<Position> positions;positions.reserve(proof.placements->size());
    std::map<editor::ObjectId,uint32_t> dense;
    for(const auto& binding:image.bindings)if(!dense.emplace(binding.object,binding.marker).second){
        error="The native Save has duplicate placement bindings";return false;
    }
    Rows markers;if(!rows(owned.p,0x2C10,0x238,kPlacementLimit,markers)){
        error="The persisted native Save placement roster is unavailable";return false;
    }
    size_t count=0;for(auto* raw=markers.begin;raw!=markers.end;raw+=0x238)if(live_slot(raw))++count;
    if(count!=proof.placements->size()){error="The persisted native Save placement roster changed";return false;}
    for(const auto& row:*proof.placements){
        const auto binding=dense.find(row.live.object);auto* marker=binding==dense.end()?nullptr:marker_for_key(owned.p,binding->second);
        // Marker+228 is an actor-lookup key in the original live editor. The
        // reflected stream omits it; ctor F2A960 leaves FFFFFFFF in a fresh
        // Read, and pool insertion preserves that sentinel. Keep the live key
        // fence above, but require this separate exact private-owner domain.
        if(!marker || skg::field<uint32_t>(marker,0)!=row.class_index || skg::field<uint32_t>(marker,0x228)!=UINT32_MAX){
            error="The persisted native Save placement identity changed";return false;
        }
        const auto actual=skg::field<editor::Vec3>(marker,4);
        if(!exact_grounding_position(actual,row.authored) && (!row.grounded || !exact_grounding_position(actual,row.rendered))){
            error="The persisted native Save position has no exact retained grounding witness";return false;
        }
        positions.push_back({binding->second,row.class_index,UINT32_MAX,actual,row.authored});
    }
    const auto unchanged=[&]{
        Rows actual;if(!rows(owned.p,0x2C10,0x238,kPlacementLimit,actual))return false;
        size_t count=0;for(auto* raw=actual.begin;raw!=actual.end;raw+=0x238)if(live_slot(raw))++count;
        if(count!=positions.size())return false;
        for(const auto& row:positions){auto* marker=marker_for_key(owned.p,row.dense);
            if(!marker || skg::field<uint32_t>(marker,0)!=row.class_index || skg::field<uint32_t>(marker,0x228)!=row.native_key ||
               !exact_grounding_position(skg::field<editor::Vec3>(marker,4),row.original))return false;
        }
        return true;
    };
    // Only this private parsed owner is touched. Construct cleanup before the
    // first primitive write, including exceptional NativeWrite exits.
    struct RestorePositions {
        void* resource;const std::vector<Position>& positions;bool active=true;
        void restore() noexcept {if(!active)return;active=false;for(const auto& row:positions)
            if(!exact_grounding_position(row.original,row.authored))
                if(auto* marker=marker_for_key(resource,row.dense))skg::field<editor::Vec3>(marker,4)=row.original;}
        ~RestorePositions(){restore();}
    } restore{owned.p,positions};
    for(const auto& row:positions)if(!exact_grounding_position(row.original,row.authored))
        skg::field<editor::Vec3>(marker_for_key(owned.p,row.dense),4)=row.authored;
    std::vector<uint8_t> normalized;
    if(!serialize(owned.p,normalized,error))return false;
    if(!current()){error="The native Save grounding owner expired during Write";return false;}
    restore.restore();
    if(!unchanged()){error="The native Save grounding Write changed its placement roster";return false;}
    if(!serialize(owned.p,roundtrip,error))return false;
    if(!current() || roundtrip!=image.bytes || !unchanged()){
        error="The native Save grounding Write changed an unwitnessed field";return false;
    }
    // Release is another native callback. Retire the private owner before the
    // final live/source/context closure and before publishing any new bytes.
    release(std::exchange(owned.p,nullptr));
    if(!current()){error="The native Save grounding owner expired during Release";return false;}
    image.bytes=std::move(normalized);return true;
}
}
bool capture_terrain_transition(const TerrainTransitionKey& supplied_key,const Snapshot& supplied_entry,const TerrainTransitionCurrent& supplied_current,
                                TerrainTransitionTicket& out,std::string& error){
    try{
        const auto key=supplied_key;const auto entry=supplied_entry;const auto current=supplied_current;
        const auto tool=uintptr_t(terraform_mode());
        if(terrain_read_identity || !transition_current(key,tool,current) || value_scope()!=key.scope){
            error="The native terrain transition cannot retain this current owner";return false;
        }
        if(!validate(entry,entry.document,error))return false;
        const auto owner=materialization_owner(terraform_mode());
        if(!owner || !terrain_materialization_error.empty() || (!terrain_materialization.empty()&&terrain_materialization_owner!=owner)){
            error="The native terrain transition has no current materialization witness";return false;
        }
        editor_native_terrain_materialization::Recipe raw;
        if(!materialization_recipe(terraform_mode(),raw,error) || !transition_recipe_matches(raw,terrain_materialization,entry,error))return false;
        std::shared_ptr<TerrainTransitionProof> result;
        if(!make_transition_ticket(key,tool,entry,raw,terrain_materialization,result,error,true))return false;
        Snapshot verified;if(!transition_observe(key,tool,entry,result->rendered,result->materialization,current,verified,error))return false;
        const auto closure=[&]{return transition_current(key,tool,current)&&transition_placements_current(*result,error);};
        if(!transition_observation_matches(entry,verified,closure,error))return false;
        if(!transition_current(key,tool,current)){error="The native terrain capture context expired";return false;}
        // Capture has no observed-image output. Retain the caller's asserted
        // image; refresh alone replaces it with its paired observed output.
        if(!transition_placements_current(*result,error))return false;
        out=std::move(result);error.clear();return true;
    }catch(...){error="Cannot retain the owned native terrain transition";return false;}
}
bool refresh_terrain_transition(const TerrainTransitionTicket& supplied_ticket,const TerrainTransitionKey& supplied_key,const Snapshot& supplied_expected,
                                const TerrainTransitionCurrent& supplied_current,TerrainTransitionTicket& refreshed,Snapshot& observed,std::string& error){
    try{
        const auto ticket=supplied_ticket;const auto key=supplied_key;const auto expected=supplied_expected;const auto current=supplied_current;
        if(!ticket || ticket->retired || key!=ticket->key || terrain_read_identity){error="The native terrain refresh has no exact retained owner";return false;}
        if(!validate(expected,expected.document,error) || !transition_recipe_matches(ticket->rendered,ticket->materialization,expected,error))return false;
        std::shared_ptr<TerrainTransitionProof> result;
        if(!make_transition_ticket(key,ticket->tool,expected,ticket->rendered,ticket->materialization,result,error,true))return false;
        Snapshot verified;if(!transition_observe(key,ticket->tool,expected,result->rendered,result->materialization,current,verified,error))return false;
        const auto closure=[&]{return !ticket->retired && transition_current(key,ticket->tool,current)&&
            !ticket->retired && transition_placements_current(*result,error);};
        if(!transition_observation_matches(expected,verified,closure,error))return false;
        if(!transition_current(key,ticket->tool,current) || ticket->retired){error="The native terrain refresh context expired";return false;}
        if(!transition_placements_current(*result,error) || !retain_transition_observation(*result,verified,error))return false;
        ticket->retired=true;refreshed=std::move(result);observed=std::move(verified);error.clear();return true;
    }catch(...){error="Cannot refresh the owned native terrain transition";return false;}
}
bool normalize_saved_terrain(const TerrainTransitionTicket& supplied_ticket,const TerrainTransitionReceipt& supplied_receipt,const Snapshot& expected_snapshot,
                             Snapshot& saved,const TerrainTransitionCurrent& supplied_current,std::string& error){
    try{
        // Runtime's Save hold may be retired by any callback below. Keep all
        // value inputs alive independently and publish only through its fence.
        const auto ticket=supplied_ticket;const auto receipt=supplied_receipt;const auto expected=expected_snapshot;
        const auto current=supplied_current;Snapshot candidate=saved;
        if(receipt.kind!=TerrainTransitionKind::Save || terrain_read_identity){error="The native terrain readback has no Save-only receipt";return false;}
        if(!transition_receipt(ticket,receipt,false,error))return false;
        if(ticket->normalizing){error="The native Save terrain readback is already in progress";return false;}
        struct Busy {const TerrainTransitionProof& proof;~Busy(){proof.normalizing=false;}} busy{*ticket};ticket->normalizing=true;
        const auto retained=[&]{return !ticket->retired && transition_current(receipt.after,ticket->tool,current) &&
            !ticket->retired && transition_placements_current(*ticket,error);};
        if(!retained()){error="The completed native Save no longer owns this terrain scene";return false;}
        if(ticket->entry.document!=expected.document || !same_projection_image(ticket->entry,expected)){
            const auto& retained=ticket->entry;
            const unsigned fields=unsigned(retained.document!=expected.document) |
                (unsigned(retained.source!=expected.source)<<1) | (unsigned(retained.bytes!=expected.bytes)<<2) |
                (unsigned(retained.bindings!=expected.bindings)<<3) | (unsigned(retained.act!=expected.act)<<4) |
                (unsigned(retained.captain!=expected.captain || retained.captain_locked!=expected.captain_locked)<<5) |
                (unsigned(retained.metadata!=expected.metadata)<<6);
            error="The native Save grounding proof no longer matches its exact committed entry (fields="+std::to_string(fields)+
                ", revision="+std::to_string(retained.document.revision)+"/"+std::to_string(expected.document.revision)+
                ", source="+std::to_string(retained.source.instance)+"/"+std::to_string(expected.source.instance)+")";return false;
        }
        editor_native_terrain_materialization::Recipe live,stored;
        if(!materialization_recipe(terraform_mode(),live,error) || !editor_native_terrain_materialization::from_bytes(candidate.bytes,stored,error))return false;
        if(live!=ticket->rendered || stored!=ticket->rendered || candidate.source!=receipt.after.owner.source){
            error="The native Save changed its retained terrain recipe or source";return false;
        }
        if(!transition_recipe_matches(live,ticket->materialization,expected,error))return false;
        if(!ticket->materialization.normalize(candidate.bytes,error) || !normalize_transition_grounding(*ticket,candidate,retained,error) ||
           !validate(candidate,expected.document,error))return false;
        candidate.document=expected.document;
        if(!retained() || !materialization_recipe(terraform_mode(),live,error) || live!=ticket->rendered){
            error="The native Save terrain owner changed during readback";return false;
        }
        ticket->saved=receipt;saved=std::move(candidate);error.clear();return true;
    }catch(...){error="Cannot normalize the owned native Save terrain record";return false;}
}
bool resume_terrain_transition(const TerrainTransitionTicket& ticket,const TerrainTransitionReceipt& receipt,const Snapshot& expected,
                               const TerrainTransitionCurrent& current,std::string& error,Snapshot* observed){
    const bool accepted=transition_receipt(ticket,receipt,false,error);
    if(ticket)ticket->retired=true;
    if(!accepted)return false;
    try{
        if(terrain_read_identity || (receipt.kind==TerrainTransitionKind::Save && (!ticket->saved ||
           ticket->saved->save_generation!=receipt.save_generation || ticket->saved->save_serial!=receipt.save_serial ||
           ticket->saved->after!=receipt.after))){error="The native terrain transfer has no completed Save receipt";return false;}
        if(!validate(expected,expected.document,error) || !transition_recipe_matches(ticket->rendered,ticket->materialization,expected,error))return false;
        Snapshot verified;if(!transition_observe(receipt.after,ticket->tool,expected,ticket->rendered,ticket->materialization,current,verified,error))return false;
        auto values=ticket->materialization;
        if(!transition_current(receipt.after,ticket->tool,current)){error="The native terrain transfer context expired";return false;}
        TerrainMaterializationOwner owner{receipt.after.scope,receipt.after.generation,receipt.after.owner.mode,receipt.after.owner.data,
            receipt.after.owner.resource,ticket->tool,receipt.after.owner.source};
        terrain_materialization=std::move(values);terrain_materialization_owner=owner;
        terrain_materialization_error.clear();terrain_materialization_error_reported=false;
        for(auto& rows:terrain_authored_rows)rows.clear();if(observed)*observed=std::move(verified);error.clear();return true;
    }catch(...){error="Cannot resume the owned native terrain transition";return false;}
}
bool restore_terrain_transition(const TerrainTransitionTicket& ticket,const TerrainTransitionReceipt& receipt,const Snapshot& expected,
                                const TerrainTransitionCurrent& current,TerrainTransitionTicket& prepared,std::string& error){
    const bool accepted=transition_receipt(ticket,receipt,true,error);
    if(ticket)ticket->retired=true;
    if(!accepted || (receipt.kind!=TerrainTransitionKind::Recovery && receipt.kind!=TerrainTransitionKind::RecoveryBootstrap &&
       receipt.kind!=TerrainTransitionKind::ExportRebuild))return false;
    try{
        const auto tool=uintptr_t(terraform_mode());
        if(terrain_read_identity || !transition_current(receipt.after,tool,current) || !terrain_available ||
           !original_terrain_load || !original_terrain_rebuild || !original_terrain_materialize){error="The canonical terrain recovery boundary is unavailable";return false;}
        if(!validate(expected,expected.document,error))return false;
        Owned native(parse(expected.bytes,error));if(!native.p)return false;
        const auto parent_current=[after=receipt.after,tool,current]{return transition_current(after,tool,current,false);};
        if(!prepare_terrain_load_input(native.p,expected.bytes,parent_current,error))return false;
        TransitionRead read(receipt.after,tool,current,nullptr);
        struct PrivateValues {~PrivateValues(){clear_terrain_materialization();}} private_values;
        if(!restore(expected,error))return false;
        if(read.identity.invalid || !transition_current(receipt.after,tool,current,false)){
            error="The canonical terrain owner changed before its explicit Load boundary";return false;
        }
        clear_terrain_materialization();
        struct Applying {bool previous=applying;Applying(){applying=true;}~Applying(){applying=previous;}} guard;
        original_terrain_load(terraform_mode(),skg::ptr_add<void>(native.p,0x18));terrain_rebuild(terraform_mode());
        applying=guard.previous;
        if(read.identity.invalid || !transition_current(receipt.after,tool,current,false)){
            error="The canonical terrain recovery owner changed during materialization";return false;
        }
        const auto owner=materialization_owner(terraform_mode());
        if(!owner || terrain_materialization_owner!=owner || !terrain_materialization_error.empty()){
            error="The canonical terrain recovery produced no exact materialization proof";return false;
        }
        editor_native_terrain_materialization::Recipe raw;
        if(!materialization_recipe(terraform_mode(),raw,error) || !transition_recipe_matches(raw,terrain_materialization,expected,error))return false;
        std::shared_ptr<TerrainTransitionProof> result;
        if(!make_transition_ticket(receipt.after,tool,expected,raw,terrain_materialization,result,error))return false;
        // Retire the last native allocation before publishing a value token;
        // its Release callback is part of this synchronous ownership fence.
        release(std::exchange(native.p,nullptr));
        editor_native_terrain_materialization::Recipe settled;
        if(read.identity.invalid || !transition_current(receipt.after,tool,current,false) ||
           !materialization_recipe(terraform_mode(),settled,error) || settled!=raw){error="The canonical terrain recovery receipt expired";return false;}
        // The fresh values become readable only through the prepared token.
        // Runtime waits for actual readiness and accepted controller attachment
        // before the strict resume readback publishes their new owner.
        prepared=std::move(result);error.clear();return true;
    }catch(...){error="Cannot materialize the owned canonical terrain recovery";return false;}
}
