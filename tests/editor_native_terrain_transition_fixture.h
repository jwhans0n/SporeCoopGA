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
unsigned tss_action=0;bool tss_restored=false;
void* SK_THISCALL tss_assign_text(void* header,const char16_t* first,const char16_t* last){
    CHECK(first==last);skg::field<void*>(header,0)=nullptr;skg::field<void*>(header,4)=nullptr;return header;
}
uintptr_t tss_symbol(const char* name){
    return std::string(name)=="SporeCoop::Editor::AssignMetadataText"?uintptr_t(tss_assign_text):tmc_symbol(name);
}
void SK_THISCALL tss_restore(void* entry,void* before){
    ttt_restore(entry,before);
    // With unchanged authored terrain, RestoreHistorySnapshot materializes
    // the existing live vectors again; it does not reload canonical columns.
    terrain_materialize(t_terraform.data());tss_restored=true;
    if(tss_action==12){tmf_columns[0][1][16]^=1;tmf_bind();}
    if(tss_action==13)++generation;
}
void test_saved_terrain_restore(){
    const auto old_write=a_write,old_read=a_read,old_acts=a_copy_acts,old_crew=a_copy_crew,old_shown=a_layout_shown;
    const auto old_init=a_scenario_initialization,old_modes=a_get_simulator_mode_manager;const auto* old_host=H;
    const auto old_alloc=a_alloc,old_entry=a_entry,old_restore=a_restore;const auto old_select=original_select_act;
    const auto old_message_manager=a_metadata_message_manager,old_message_send=a_metadata_message_send;
    for(unsigned action=0;action<14;++action){
        tss_action=action;tss_restored=false;TerrainTransitionKey key;auto entry=ttt_setup(key);
        remember_terrain(entry.document);std::string error;const auto current=[] {return ttt_allowed;};
        TerrainTransitionTicket ticket;CHECK(capture_terrain_transition(key,entry,current,ticket,error));
        ttt_held=true;C.gesture_scope=[] {return GestureScope{};};
        static SkHost host;host=*H;host.sym=tss_symbol;H=&host;
        v_message_vt.fill(nullptr);v_messages=v_message_vt.data();v_message_vt[0x14/4]=reinterpret_cast<void*>(v_name_notice);
        a_metadata_message_manager=uintptr_t(v_get_messages);a_metadata_message_send=uintptr_t(v_name_notice);v_name_mutation=0;
        TerrainTransitionReceipt receipt{TerrainTransitionKind::Save,key,key,3,5};
        receipt.after.owner.source.instance=202;skg::field<editor::ResourceKey>(t_live.data(),8)=receipt.after.owner.source;
        Snapshot saved=entry;saved.source=receipt.after.owner.source;saved.bytes=ttt_saved_bytes(entry);
        {Owned image(parse(saved.bytes,error));CHECK(image.p);
            skg::field<editor::ResourceKey>(image.p,8)=saved.source;CHECK(serialize(image.p,saved.bytes,error));}
        CHECK(normalize_saved_terrain(ticket,receipt,entry,saved,current,error));
        // Ordinary capture observes the rotated Save source while publication
        // scope is empty; only the retained completed-Save ticket survives.
        clear_terrain_materialization();
        const auto raw=tmf_columns;ttt_restore_bind();a_restore=uintptr_t(tss_restore);
        if(action==1)ticket->saved.reset();
        if(action==2)++receipt.save_serial;
        if(action==3)++receipt.after.scope.epoch;
        if(action==4)++receipt.after.generation;
        if(action==5)++receipt.after.owner.resource;
        if(action==6)++saved.source.instance;
        if(action==7)++saved.document.revision;
        if(action==8){tmf_columns[0][1][16]^=1;tmf_bind();}
        if(action==9)ttt_allowed=false;
        if(action==10)ticket->retired=true;
        if(action==11)materialized_terrain->terrain.clear();
        const auto calls=t_restores;TerrainTransitionTicket prepared;
        const bool accepted=restore_saved_terrain(ticket,receipt,saved,current,prepared,error);
        if(!accepted&&action==0)std::printf("saved restore diagnostic: %s\n",error.c_str());
        std::printf("saved restore action=%u accepted=%u\n",action,unsigned(accepted));
        CHECK(!terrain_read_identity&&terrain_materialization.empty()&&t_allocations.empty());
        if(action){
            CHECK(!accepted&&!prepared&&!error.empty());
            CHECK((action>=12)==tss_restored);
            CHECK(t_restores==calls+(action>=12?1:0));continue;
        }
        CHECK(accepted&&prepared&&ticket->retired&&!prepared->retired&&tss_restored&&tmf_columns!=raw);
        auto resume=receipt;resume.kind=TerrainTransitionKind::Export;resume.before=resume.after;
        Snapshot observed;CHECK(resume_terrain_transition(prepared,resume,saved,current,error,&observed));
        CHECK(prepared->retired&&observed.document==saved.document&&validate(observed,saved.document,error));
        CHECK(!resume_terrain_transition(prepared,resume,saved,current,error));
    }
    tmc_properties.clear();act_cache_rows.clear();a_write=old_write;a_read=old_read;a_copy_acts=old_acts;a_copy_crew=old_crew;
    a_layout_shown=old_shown;a_scenario_initialization=old_init;a_get_simulator_mode_manager=old_modes;H=old_host;
    a_alloc=old_alloc;a_entry=old_entry;a_restore=old_restore;original_select_act=old_select;reset();
    a_metadata_message_manager=old_message_manager;a_metadata_message_send=old_message_send;
}
unsigned tcr_action=0;
unsigned tcr_rotation_refreshes=0;
bool tcr_explicit_load=false,tcr_sample_ready=false,tcr_tainted=false;
uint64_t tcr_restore_count=0;
editor::Rotation tcr_rotation(bool rendered){
    const std::array<uint32_t,4> bits=rendered?std::array<uint32_t,4>{0xBF16ACF8,0xBE2E89B6,0x3E8838CB,0x3F3E8043}:
        std::array<uint32_t,4>{0xBF16ACF6,0xBE2E89B5,0x3E8838CB,0x3F3E8042};
    editor::Rotation value;std::memcpy(&value,bits.data(),sizeof(value));return value;
}
void SK_THISCALL tcr_apply(void* owner,void* marker,uint32_t flags){
    CHECK(owner==t_data.data()&&!flags&&applying);++tcr_rotation_refreshes;
    const auto first=t_rows.at(t_live.data()).data()+4;const unsigned index=marker==first?0:1;
    CHECK(marker==first+index*0x238);
    t_model_rotation(t_models[index].data(),&skg::field<editor::Rotation>(marker,0x10));
    if(tcr_sample_ready&&!tcr_tainted&&tcr_action>=18){
        tcr_tainted=true;
        if(tcr_action==18)skg::field<uint32_t>(marker,0x10)^=1;
        if(tcr_action==19)skg::field<float>(first,4)=std::nextafter(skg::field<float>(first,4),0.f);
        if(tcr_action==20)++generation;
    }
}
void SK_THISCALL tcr_restore(void* entry,void* before){
    CHECK(skg::field<void*>(entry,0xC)==t_data.data());++t_restores;tcg_copy(t_live.data(),before);
    auto* marker=t_rows.at(t_live.data()).data()+4;
    skg::field<float>(marker,4)=std::nextafter(skg::field<float>(marker,4),0.f);
    release(entry);
}
void SK_THISCALL tcr_load(void* self,void* properties){
    ttt_load(self,properties);tcr_explicit_load=restores>tcr_restore_count;
    if(tcr_explicit_load&&tcr_action==21){tcr_tainted=true;++generation;}
}
void SK_THISCALL tcr_rebuild(void* self){
    ttt_rebuild(self);if(!tcr_explicit_load)return;
    auto& raw=t_rows.at(t_live.data());auto* marker=raw.data()+4;
    // Exact real C40 pair: the extra rebuild changes the already grounded XYZ
    // after restore() retained its first derived sample.
    skg::field<editor::Vec3>(marker,4)={-459.84436f,116.277344f,187.374207f};
    // Exact object253 C42 q drift from the second canonical Load/Rebuild.
    skg::field<editor::Rotation>(raw.data()+0x238+4,0x10)=tcr_rotation(true);
    if(tcr_action==1)++skg::field<uint32_t>(marker,0x100);
    if(tcr_action==3)skg::field<float>(marker,0x20)=2;
    if(tcr_action==4)skg::field<float>(marker,0x1E0)=.75f;
    if(tcr_action==5)++skg::field<uint32_t>(marker,0x228);
    if(tcr_action==6)++skg::field<editor::ResourceKey>(t_live.data(),8).instance;
    if(tcr_action==7)++generation;
    if(tcr_action==8)skg::field<float>(marker,4)=std::numeric_limits<float>::quiet_NaN();
    if(tcr_action==10)std::swap(bindings.begin()->second,std::next(bindings.begin())->second);
    if(tcr_action==11)skg::field<uint32_t>(raw.data()+0x238,0)=0x80000000;
    if(tcr_action==12)++skg::field<uint32_t>(marker,0);
    tcr_sample_ready=true;
}
void SK_THISCALL tcr_export(void* resource){
    tmc_export(resource);
    if(tcr_sample_ready&&tcr_action==14)tmc_properties.at(resource).fields[0x12345678]={0xA,0,{1,2,3,4}};
}
uintptr_t tcr_symbol(const char* name){
    return std::string(name)=="SporeCoop::Editor::CaptureTerrain"?uintptr_t(tcr_export):tmc_symbol(name);
}
int SK_THISCALL tcr_release(void* resource){
    const bool final=t_allocations.contains(resource)&&skg::field<int>(resource,4)==1;
    const auto result=tcg_release(resource);
    if(final&&tcr_sample_ready&&tcr_action==13&&!tcr_tainted){
        tcr_tainted=true;auto* marker=t_rows.at(t_live.data()).data()+4;
        skg::field<float>(marker,4)=std::nextafter(skg::field<float>(marker,4),0.f);
    }
    return result;
}
Snapshot tcr_setup(TerrainTransitionKey& key){
    tcr_explicit_load=tcr_sample_ready=tcr_tainted=false;tcr_rotation_refreshes=0;tcr_restore_count=restores;
    auto entry=tcg_setup();ttt_allowed=true;ttt_held=true;
    terrain_grounding.clear();auto* marker=t_rows.at(t_live.data()).data()+0x238+4;
    skg::field<editor::Vec3>(marker,4)={-459.844482f,116.277374f,187.374252f};
    skg::field<editor::Rotation>(t_rows.at(t_live.data()).data()+3*0x238+4,0x10)=tcr_rotation(false);
    const editor::ResourceKey local{0xBF9B39CD,0x366A930D,0x408A0000};
    skg::field<editor::ResourceKey>(t_live.data(),8)=local;entry.source=local;
    std::string error;CHECK(capture_draft(entry,error));entry.document.project=t_scope.project;entry.document.epoch=t_scope.epoch;
    for(const auto& [id,row]:entry.document.terrain){(void)row;entry.document.next_id=std::max(entry.document.next_id,id.id+1);}
    entry.metadata.reset();CHECK(validate(entry,entry.document,error));CHECK(editor::valid(entry.document,editor::Limits{},error));ttt_columns(entry);clear_terrain_materialization();
    SceneOwner owner;CHECK(capture_scene_owner(owner,error));key={t_scope,77,generation,owner};
    C.projection_scope=[] {return ttt_held?GestureScope{}:t_scope;};
    static SkHost host;host=*H;host.sym=tcr_symbol;H=&host;
    original_terrain_load=tcr_load;original_terrain_rebuild=tcr_rebuild;ttt_restore_bind();a_restore=uintptr_t(tcr_restore);
    a_object=uintptr_t(t_object);t_actor_vt[3]=reinterpret_cast<void*>(t_cast);original_apply_marker=tcr_apply;
    t_resource_vt[1]=reinterpret_cast<void*>(tcr_release);return entry;
}
void test_cold_recovery_grounding_bootstrap(){
    struct Routes {
        uintptr_t writer=a_write,reader=a_read,copier=a_copy,acts=a_copy_acts,crew=a_copy_crew,shown=a_layout_shown;
        uintptr_t initialization=a_scenario_initialization,modes=a_get_simulator_mode_manager,allocator=a_alloc,entry=a_entry,restore=a_restore,object=a_object;
        const SkHost* host=H;decltype(original_terrain_load) load=original_terrain_load;decltype(original_terrain_rebuild) rebuild=original_terrain_rebuild;
        decltype(original_select_act) select=original_select_act;
        ApplyMarkerFn apply_route=original_apply_marker;void* cast_route=t_actor_vt[3];
        ~Routes(){CHECK(t_allocations.empty());tcg_rows.clear();tmc_properties.clear();act_cache_rows.clear();
            a_write=writer;a_read=reader;a_copy=copier;a_copy_acts=acts;a_copy_crew=crew;a_layout_shown=shown;
            a_scenario_initialization=initialization;a_get_simulator_mode_manager=modes;a_alloc=allocator;a_entry=entry;a_restore=restore;
            a_object=object;original_apply_marker=apply_route;t_actor_vt[3]=cast_route;
            original_terrain_load=load;original_terrain_rebuild=rebuild;original_select_act=select;H=host;reset();}
    } routes;
    for(unsigned action=0;action<22;++action){
        std::printf("cold recovery grounding action=%u\n",action);std::fflush(stdout);
        tcr_action=action;TerrainTransitionKey key;auto entry=tcr_setup(key);const auto camera=v_camera;
        const auto current=[&]{
            if(tcr_sample_ready&&!tcr_tainted&&(action==2||action==9||action==15)){
                tcr_tainted=true;if(action==15)++t_scope.epoch;
                else if(action==2)skg::field<uint32_t>(t_rows.at(t_live.data()).data()+0x238+4,0x10)^=2;
                else{auto* marker=t_rows.at(t_live.data()).data()+4;skg::field<float>(marker,4)=std::nextafter(skg::field<float>(marker,4),0.f);}
            }
            return ttt_allowed&&t_scope==key.scope;
        };
        TerrainTransitionTicket prepared;std::string error;
        TerrainTransitionReceipt receipt{TerrainTransitionKind::RecoveryBootstrap,key,key,0,0};
        const bool restored=restore_terrain_transition({},receipt,entry,current,prepared,error);
        if(action==21){CHECK(!restored&&!prepared&&tcr_explicit_load&&tcr_tainted&&!tcr_sample_ready&&!error.empty());continue;}
        CHECK(tcr_explicit_load&&tcr_sample_ready);
        if(action==2||action==9||action==13||action==15||action>=18)CHECK(tcr_tainted);
        if(!restored){CHECK(action!=0&&!prepared&&!error.empty());continue;}
        CHECK(prepared&&prepared->owned_bytes<=(64u<<20)&&tcr_explicit_load&&tcr_sample_ready);
        terrain_rebuilding=false;receipt.kind=TerrainTransitionKind::Recovery;
        if(action==16)entry.bytes.back()^=1;
        if(action==17)++entry.document.placements.begin()->second.revision;
        const auto transforms=t_transforms;Snapshot observed;
        const bool resumed=resume_terrain_transition(prepared,receipt,entry,current,error,&observed);
        if(action){if(resumed||error.empty())std::printf("cold recovery unexpected result action=%u resumed=%u error=%s\n",action,unsigned(resumed),error.c_str());CHECK(!resumed&&prepared->retired&&!error.empty());continue;}
        if(!resumed)std::printf("cold recovery grounding failed: %s\n",error.c_str());
        CHECK(resumed&&prepared->retired&&observed.document==entry.document);
        CHECK(tcr_rotation_refreshes==1&&editor_native_terrain_rotations::exact(skg::field<editor::Rotation>(t_rows.at(t_live.data()).data()+0x238+4,0x10),tcr_rotation(false)));
        CHECK(editor_native_terrain_rotations::exact(skg::field<editor::Rotation>(t_models[1].data(),0x10),tcr_rotation(false)));
        ttt_held=false; // Runtime releases its recovery hold after publication.
        Snapshot strict;CHECK(capture_draft(strict,error)&&validate(strict,entry.document,error));
        CHECK(strict.bindings==entry.bindings&&terrain_grounding.size()==1&&t_transforms==transforms&&v_camera==camera);
        CHECK((skg::field<editor::Vec3>(t_rows.at(t_live.data()).data()+4,4)==editor::Vec3{-459.84436f,116.277344f,187.374207f}));
        CHECK(!resume_terrain_transition(prepared,receipt,entry,current,error));
    }
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
    const bool rebuilt_resumed=resume_terrain_transition(rebuilt,receipt,entry,current,error);
    if(!rebuilt_resumed)std::printf("rebuilt transition diagnostic: %s\n",error.c_str());
    CHECK(rebuilt_resumed&&rebuilt->retired&&!terrain_materialization.empty());
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
    test_cold_recovery_grounding_bootstrap();
    test_saved_terrain_restore();
}
