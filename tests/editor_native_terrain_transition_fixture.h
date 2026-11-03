// Included inside the controlled native materialization fixture namespace.
bool ttt_allowed=true,ttt_held=false,ttt_release_change=false,ttt_change_after_load=false;
int SK_THISCALL ttt_release(void* resource){
    const bool final=skg::field<int>(resource,4)==1&&t_allocations.contains(resource);
    const auto result=tmc_release(resource);
    if(final&&ttt_release_change){ttt_release_change=false;++generation;}
    return result;
}
void ttt_columns(const Snapshot& image){
    tmf::Recipe recipe;std::string error;CHECK(tmf::from_bytes(image.bytes,recipe,error));tmf_columns={};
    for(size_t kind=0;kind<2;++kind)for(const auto& row:recipe.rows[kind]){
        auto& c=tmf_columns[kind];c[0].insert(c[0].end(),row.bytes.begin(),row.bytes.begin()+12);
        c[1].insert(c[1].end(),row.bytes.begin()+12,row.bytes.begin()+68);
        if(!kind)for(size_t n=2;n<6;++n)c[n].insert(c[n].end(),row.bytes.begin()+68+(n-2)*4,row.bytes.begin()+72+(n-2)*4);
    }
    tmf_bind();
}
void SK_THISCALL ttt_load(void* self,void* properties){
    CHECK(self==t_terraform.data()&&applying);auto* r=static_cast<uint8_t*>(properties)-0x18;
    CHECK(tmc_properties.contains(r));Snapshot image;std::string error;
    CHECK(editor_native_terrain::encode(tmc_properties.at(r),image.bytes,error));ttt_columns(image);
    if(ttt_change_after_load)ttt_release_change=true;
}
void SK_THISCALL ttt_rebuild(void* self){CHECK(applying);terrain_materialize(self);}
void* SK_CDECL ttt_allocate(size_t size,const char* name,int a,int b,int c,int d){
    if(size!=0x18)return t_allocate(size,name,a,b,c,d);
    if(t_alloc_fail)return nullptr;void* value=std::calloc(1,size);t_allocations.insert(value);return value;
}
void* SK_THISCALL ttt_entry(void* memory,void* owner,void*,void*){
    CHECK(owner==t_data.data());skg::field<void*>(memory,0)=t_entry_vt.data();skg::field<int>(memory,4)=1;
    skg::field<void*>(memory,0xC)=owner;return memory;
}
void SK_THISCALL ttt_restore(void* entry,void* before){
    CHECK(skg::field<void*>(entry,0xC)==t_data.data());++t_restores;t_copy(t_live.data(),before);
    act_cache_rows[t_live.data()]=act_cache_rows.at(before);act_cache_bind(t_live.data(),act_cache_rows.at(t_live.data()));
    tmc_properties[t_live.data()]=tmc_properties.at(before);release(entry);
}
void SK_CDECL ttt_select(uint32_t act){CHECK(act==0);}
void ttt_restore_bind(){
    a_alloc=uintptr_t(ttt_allocate);a_entry=uintptr_t(ttt_entry);a_restore=uintptr_t(ttt_restore);original_select_act=ttt_select;
}
std::vector<uint8_t> ttt_saved_bytes(const Snapshot& image){
    editor_native_terrain::Properties body,raw;std::string error;
    CHECK(editor_native_terrain::decode(image.bytes,body,error)&&editor_native_terrain::decode(tmf_bytes(),raw,error));
    raw.tail=std::move(body.tail);std::vector<uint8_t> bytes;CHECK(editor_native_terrain::encode(raw,bytes,error));return bytes;
}
Snapshot ttt_setup(TerrainTransitionKey& key){
    auto entry=tmc_setup();ttt_allowed=true;ttt_held=false;ttt_release_change=ttt_change_after_load=false;
    const editor::ResourceKey source{101,0x366A930D,0x408A0000};skg::field<editor::ResourceKey>(t_live.data(),8)=source;entry.source=source;
    {std::string error;Owned canonical(parse(entry.bytes,error));CHECK(canonical.p);
        skg::field<editor::ResourceKey>(canonical.p,8)=source;CHECK(serialize(canonical.p,entry.bytes,error));}
    ttt_columns(entry);clear_terrain_materialization();terrain_materialize(t_terraform.data());CHECK(!terrain_materialization.empty());
    SceneOwner owner;std::string error;CHECK(capture_scene_owner(owner,error));key={t_scope,11,state().generation,owner};
    C.projection_scope=[] {return ttt_held?GestureScope{}:t_scope;};
    return entry;
}
void test_terrain_transition_receipts(){
    const auto old_write=a_write,old_read=a_read,old_acts=a_copy_acts,old_crew=a_copy_crew,old_shown=a_layout_shown;
    const auto old_init=a_scenario_initialization,old_modes=a_get_simulator_mode_manager;const auto* old_host=H;
    const auto old_load=original_terrain_load;const auto old_rebuild=original_terrain_rebuild;
    const auto old_alloc=a_alloc,old_entry=a_entry,old_restore=a_restore;const auto old_select=original_select_act;
    std::string error;TerrainTransitionKey key;auto entry=ttt_setup(key);
    const auto current=[] {return ttt_allowed;};TerrainTransitionTicket ticket;
    const auto live=t_live;const auto raw=tmf_columns;const auto history=t_history;
    CHECK(capture_terrain_transition(key,entry,current,ticket,error)&&ticket&&!ticket->retired&&ticket->owned_bytes<=(64u<<20));
    CHECK(t_live==live&&tmf_columns==raw&&t_history==history&&t_allocations.empty());
    ttt_held=true;C.gesture_scope=[] {return GestureScope{};};
    TerrainTransitionTicket refreshed;Snapshot observed;
    CHECK(refresh_terrain_transition(ticket,key,entry,current,refreshed,observed,error)&&ticket->retired&&!refreshed->retired);
    CHECK(observed.document==entry.document&&tmf_columns==raw&&!terrain_read_identity);
    ticket=refreshed;entry=observed; // Same exact strict handoff retained by Runtime.
    auto receipt=TerrainTransitionReceipt{TerrainTransitionKind::Save,key,key,3,5};
    receipt.after.owner.source.instance=202;skg::field<editor::ResourceKey>(t_live.data(),8)=receipt.after.owner.source;
    Snapshot saved=entry;saved.bytes=ttt_saved_bytes(entry);saved.source=receipt.after.owner.source;
    CHECK(normalize_saved_terrain(ticket,receipt,entry,saved,current,error));
    CHECK(saved.bytes==entry.bytes || validate(saved,entry.document,error));
    CHECK(saved.source==receipt.after.owner.source&&saved.document==entry.document&&tmf_columns==raw);
    Snapshot resumed;
    CHECK(resume_terrain_transition(ticket,receipt,entry,current,error,&resumed)&&ticket->retired&&!terrain_read_identity);
    CHECK(resumed.document==entry.document&&resumed.source==receipt.after.owner.source&&validate(resumed,entry.document,error));
    CHECK(!resume_terrain_transition(ticket,receipt,entry,current,error));
    ttt_held=false;auto normalized=tmf_bytes();CHECK(normalize_materialized_terrain(normalized,error));
    tmf::Recipe a,b;CHECK(tmf::from_bytes(normalized,a,error)&&tmf::from_bytes(entry.bytes,b,error)&&a==b);
    for(unsigned reason=0;reason<20;++reason){
        entry=ttt_setup(key);ticket.reset();CHECK(capture_terrain_transition(key,entry,current,ticket,error));ttt_held=true;
        CHECK(refresh_terrain_transition(ticket,key,entry,current,refreshed,observed,error));ticket=refreshed;entry=observed;
        receipt={TerrainTransitionKind::Save,key,key,3,5};receipt.after.owner.source.instance=202;
        skg::field<editor::ResourceKey>(t_live.data(),8)=receipt.after.owner.source;
        saved=entry;saved.bytes=ttt_saved_bytes(entry);saved.source=receipt.after.owner.source;
        if(reason==0)++receipt.before.serial;if(reason==1)++receipt.after.serial;
        if(reason==2)++receipt.after.scope.epoch;if(reason==3)++receipt.after.scope.project[0];
        if(reason==4)++receipt.after.scope.author.generation;if(reason==5)++receipt.after.generation;
        if(reason==6)receipt.save_serial=0;if(reason==7)receipt.after.owner.mode=1;
        if(reason==8)receipt.after.owner.data=1;if(reason==9)receipt.after.owner.resource=1;
        if(reason==10)receipt.after.owner.source.type=1;if(reason==11)++saved.source.instance;
        if(reason==12){tmf_columns[0][1][4]^=1;tmf_bind();}
        if(reason==13)saved.bytes[0]^=1;
        if(reason==14)ttt_allowed=false;
        if(reason==15)++generation;
        if(reason==16)skg::field<void*>(t_mode.data(),0x18)=nullptr;
        if(reason==17){auto p=tmc_properties.at(t_live.data());p.fields[editor_native_terrain::kTransforms].value[12]^=1;
            CHECK(editor_native_terrain::encode(p,saved.bytes,error));}
        if(reason==18)receipt.kind=TerrainTransitionKind::RecoveryBootstrap;
        if(reason==19)receipt.save_generation=0;
        const auto original=saved;const auto native=t_live;const auto columns=tmf_columns;
        CHECK(!normalize_saved_terrain(ticket,receipt,entry,saved,current,error));
        CHECK(same_projection_image(saved,original)&&saved.document==original.document&&t_live==native&&tmf_columns==columns&&!terrain_read_identity);
        CHECK(t_allocations.empty());
    }
    // Non-saver export restores the exact old source/owner without a Save
    // receipt, and one-use transfer remains independent of mutation scopes.
    entry=ttt_setup(key);ticket.reset();CHECK(capture_terrain_transition(key,entry,current,ticket,error));ttt_held=true;
    receipt={TerrainTransitionKind::Export,key,key,0,0};
    CHECK(resume_terrain_transition(ticket,receipt,entry,current,error)&&ticket->retired);
    // A failed callback cannot publish a new value proof or leave its borrowed
    // read identity installed for unrelated native work.
    entry=ttt_setup(key);ticket.reset();CHECK(capture_terrain_transition(key,entry,current,ticket,error));
    receipt={TerrainTransitionKind::Export,key,key,0,0};ttt_allowed=false;
    CHECK(!resume_terrain_transition(ticket,receipt,entry,current,error)&&ticket->retired&&!terrain_read_identity);
    // A final export barrier may explicitly rebuild a newer verified canonical
    // recipe; stale raw values are not normalized into a guessed old witness.
    entry=ttt_setup(key);ticket.reset();CHECK(capture_terrain_transition(key,entry,current,ticket,error));ttt_held=true;
    tmf_columns[0][1][4]^=1;tmf_bind();TerrainTransitionTicket declined;Snapshot untouched=entry;
    CHECK(!refresh_terrain_transition(ticket,key,entry,current,declined,untouched,error)&&!ticket->retired&&!declined);
    CHECK(untouched.document==entry.document&&untouched.bytes==entry.bytes);
    entry.metadata.reset();original_terrain_load=ttt_load;original_terrain_rebuild=ttt_rebuild;ttt_restore_bind();
    receipt={TerrainTransitionKind::ExportRebuild,key,key,0,0};TerrainTransitionTicket rebuilt;
    CHECK(restore_terrain_transition(ticket,receipt,entry,current,rebuilt,error)&&ticket->retired&&rebuilt&&!rebuilt->retired);
    CHECK(terrain_materialization.empty()&&!terrain_read_identity);terrain_rebuilding=false;
    receipt={TerrainTransitionKind::Export,key,key,0,0};
    CHECK(resume_terrain_transition(rebuilt,receipt,entry,current,error)&&rebuilt->retired&&!terrain_materialization.empty());
    for(unsigned reason=0;reason<6;++reason){
        entry=ttt_setup(key);ticket.reset();CHECK(capture_terrain_transition(key,entry,current,ticket,error));ttt_held=true;
        receipt={TerrainTransitionKind::ExportRebuild,key,key,0,0};
        if(reason==0)++receipt.after.serial;if(reason==1)++receipt.after.generation;if(reason==2)++receipt.after.scope.epoch;
        if(reason==3)++receipt.after.owner.source.instance;if(reason==4)receipt.save_serial=1;
        TerrainTransitionTicket target=rebuilt;
        CHECK(!restore_terrain_transition(reason==5?TerrainTransitionTicket{}:ticket,receipt,entry,current,target,error)&&target==rebuilt&&!terrain_read_identity);
    }
    // Canonical recovery bootstrap is a distinct path with no inferred old
    // rendered witness. Load plus F35E10 creates a new privately retained map.
    entry=ttt_setup(key);entry.metadata.reset();ttt_held=true;clear_terrain_materialization();
    original_terrain_load=ttt_load;original_terrain_rebuild=ttt_rebuild;ttt_restore_bind();
    receipt={TerrainTransitionKind::RecoveryBootstrap,key,key,0,0};TerrainTransitionTicket prepared;
    CHECK(restore_terrain_transition({},receipt,entry,current,prepared,error)&&prepared&&!prepared->retired);
    CHECK(terrain_materialization.empty()&&!terrain_read_identity); // Not published before attachment.
    terrain_rebuilding=false; // Controlled native loader has finished its readiness boundary.
    auto attached=receipt.after;++attached.scope.epoch;++attached.scope.author.generation;++generation;attached.generation=generation;
    receipt={TerrainTransitionKind::Recovery,key,attached,0,0};t_scope=attached.scope;
    CHECK(resume_terrain_transition(prepared,receipt,entry,current,error)&&prepared->retired);
    CHECK(!terrain_materialization.empty()&&terrain_materialization_owner->scope==attached.scope);
    for(unsigned reason=0;reason<5;++reason){
        entry=ttt_setup(key);ttt_held=true;clear_terrain_materialization();original_terrain_load=ttt_load;original_terrain_rebuild=ttt_rebuild;
        receipt={TerrainTransitionKind::RecoveryBootstrap,key,key,0,0};
        if(reason==0)++receipt.after.serial;if(reason==1)receipt.kind=TerrainTransitionKind::Recovery;
        if(reason==2)receipt.save_serial=1;if(reason==3)++receipt.after.owner.source.instance;if(reason==4)ttt_allowed=false;
        TerrainTransitionTicket untouched=prepared;
        CHECK(!restore_terrain_transition({},receipt,entry,current,untouched,error)&&untouched==prepared&&!terrain_read_identity);
    }
    entry=ttt_setup(key);projected_snapshots.clear();parsed_images.clear();projected_snapshot_bytes=parsed_image_bytes=0;
    ticket.reset();t_alloc_fail=true;const auto before_allocation=t_live;const auto columns_before=tmf_columns;
    CHECK(!capture_terrain_transition(key,entry,current,ticket,error)&&!ticket&&t_live==before_allocation&&tmf_columns==columns_before&&!terrain_read_identity);
    entry=ttt_setup(key);TerrainTransitionTicket unchanged=prepared;
    const TerrainTransitionCurrent throwing=[]()->bool {throw 17;};
    CHECK(!capture_terrain_transition(key,entry,throwing,unchanged,error)&&unchanged==prepared&&!terrain_read_identity);
    unsigned fence_calls=0;
    const TerrainTransitionCurrent changes_owner=[&]{if(++fence_calls==2)++skg::field<editor::ResourceKey>(t_live.data(),8).instance;return true;};
    CHECK(!capture_terrain_transition(key,entry,changes_owner,unchanged,error)&&unchanged==prepared&&fence_calls==2&&!terrain_read_identity);
    entry=ttt_setup(key);entry.metadata.reset();ttt_held=true;clear_terrain_materialization();
    original_terrain_load=ttt_load;original_terrain_rebuild=ttt_rebuild;ttt_restore_bind();
    t_resource_vt[1]=reinterpret_cast<void*>(ttt_release);ttt_change_after_load=true;
    receipt={TerrainTransitionKind::RecoveryBootstrap,key,key,0,0};unchanged=prepared;
    CHECK(!restore_terrain_transition({},receipt,entry,current,unchanged,error)&&unchanged==prepared);
    CHECK(!terrain_read_identity&&terrain_materialization.empty()&&t_allocations.empty());
    entry=ttt_setup(key);
    // The combined value budget includes portable identity history before any
    // ticket copy, even when native projection itself accepts those sidecars.
    for(size_t n=0;n<(64u<<20)/48+1;++n)entry.document.tombstones.emplace(editor::ObjectId{500000000+n,1},1);
    CHECK(editor::bytes(entry.document)>(64u<<20));const auto retained=t_live;const auto proof_bytes=terrain_materialization.owned_bytes();
    CHECK(!capture_terrain_transition(key,entry,current,unchanged,error)&&unchanged==prepared&&error.find("64 MiB")!=std::string::npos);
    CHECK(t_live==retained&&terrain_materialization.owned_bytes()==proof_bytes&&!terrain_read_identity&&t_allocations.empty());
    tmc_properties.clear();act_cache_rows.clear();a_write=old_write;a_read=old_read;a_copy_acts=old_acts;a_copy_crew=old_crew;
    a_layout_shown=old_shown;a_scenario_initialization=old_init;a_get_simulator_mode_manager=old_modes;H=old_host;
    original_terrain_load=old_load;original_terrain_rebuild=old_rebuild;
    a_alloc=old_alloc;a_entry=old_entry;a_restore=old_restore;original_select_act=old_select;reset();
}
