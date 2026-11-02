// Private implementation included once inside editor_native, outside Present.
struct TerrainTransitionProof {
    TerrainTransitionKey key;uintptr_t tool=0;
    Snapshot entry;
    editor_native_terrain_materialization::Recipe rendered;
    editor_native_terrain_materialization::Materialization materialization;
    size_t owned_bytes=0;
    mutable bool retired=false;
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
bool make_transition_ticket(const TerrainTransitionKey& key,uintptr_t tool,const Snapshot& entry,
                            const editor_native_terrain_materialization::Recipe& raw,
                            const editor_native_terrain_materialization::Materialization& values,
                            TerrainTransitionTicket& out,std::string& error){
    constexpr size_t limit=64u<<20;size_t charged=sizeof(TerrainTransitionProof);
    const auto charge=[&](size_t bytes){if(bytes>limit-charged)return false;charged+=bytes;return true;};
    if(!charge(entry.bytes.size()) || !charge(editor::bytes(entry.document)) || !charge(entry.bindings.size()*sizeof(Binding)) ||
       !charge(values.owned_bytes())){error="The owned native terrain transition exceeds 64 MiB";return false;}
    if(entry.metadata)for(const auto& text:entry.metadata->text)if(!charge(text.size()*sizeof(char16_t))){
        error="The owned native terrain transition exceeds 64 MiB";return false;
    }
    for(const auto& kind:raw.rows)for(const auto& row:kind)if(!charge(sizeof(row)+row.bytes.capacity()+row.references.capacity()*sizeof(editor::ObjectId))){
        error="The owned native terrain transition exceeds 64 MiB";return false;
    }
    auto result=std::make_shared<TerrainTransitionProof>();result->key=key;result->tool=tool;result->entry=entry;
    result->rendered=raw;result->materialization=values;result->owned_bytes=charged;out=std::move(result);return true;
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
}
bool capture_terrain_transition(const TerrainTransitionKey& key,const Snapshot& entry,const TerrainTransitionCurrent& current,
                                TerrainTransitionTicket& out,std::string& error){
    try{
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
        TerrainTransitionTicket result;
        if(!make_transition_ticket(key,tool,entry,raw,terrain_materialization,result,error))return false;
        Snapshot verified;if(!transition_observe(key,tool,entry,raw,terrain_materialization,current,verified,error))return false;
        out=std::move(result);error.clear();return true;
    }catch(...){error="Cannot retain the owned native terrain transition";return false;}
}
bool refresh_terrain_transition(const TerrainTransitionTicket& ticket,const TerrainTransitionKey& key,const Snapshot& expected,
                                const TerrainTransitionCurrent& current,TerrainTransitionTicket& refreshed,Snapshot& observed,std::string& error){
    try{
        if(!ticket || ticket->retired || key!=ticket->key || terrain_read_identity){error="The native terrain refresh has no exact retained owner";return false;}
        if(!validate(expected,expected.document,error) || !transition_recipe_matches(ticket->rendered,ticket->materialization,expected,error))return false;
        Snapshot verified;if(!transition_observe(key,ticket->tool,expected,ticket->rendered,ticket->materialization,current,verified,error))return false;
        TerrainTransitionTicket result;
        if(!make_transition_ticket(key,ticket->tool,expected,ticket->rendered,ticket->materialization,result,error))return false;
        if(!transition_current(key,ticket->tool,current)){error="The native terrain refresh context expired";return false;}
        ticket->retired=true;refreshed=std::move(result);observed=std::move(verified);error.clear();return true;
    }catch(...){error="Cannot refresh the owned native terrain transition";return false;}
}
bool normalize_saved_terrain(const TerrainTransitionTicket& ticket,const TerrainTransitionReceipt& receipt,const Snapshot& expected,
                             Snapshot& saved,const TerrainTransitionCurrent& current,std::string& error){
    try{
        if(receipt.kind!=TerrainTransitionKind::Save || terrain_read_identity){error="The native terrain readback has no Save-only receipt";return false;}
        if(!transition_receipt(ticket,receipt,false,error))return false;
        if(!transition_current(receipt.after,ticket->tool,current)){error="The completed native Save no longer owns this terrain scene";return false;}
        editor_native_terrain_materialization::Recipe live,stored;
        if(!materialization_recipe(terraform_mode(),live,error) || !editor_native_terrain_materialization::from_bytes(saved.bytes,stored,error))return false;
        if(live!=ticket->rendered || stored!=ticket->rendered || saved.source!=receipt.after.owner.source){
            error="The native Save changed its retained terrain recipe or source";return false;
        }
        if(!transition_recipe_matches(live,ticket->materialization,expected,error))return false;
        Snapshot candidate=saved;
        if(!ticket->materialization.normalize(candidate.bytes,error) || !validate(candidate,expected.document,error))return false;
        candidate.document=expected.document;
        if(!transition_current(receipt.after,ticket->tool,current) || !materialization_recipe(terraform_mode(),live,error) || live!=ticket->rendered){
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
        TerrainTransitionTicket result;
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
