// Production adapter regression with owning serialized doubles. This is not
// running-game acceptance. Sparse live slots compact on Write exactly as the
// native marker pool does; no pointer is transported through the test stream.
struct CreationGroundingRows {
    std::vector<uint8_t> classes;
    std::vector<std::vector<uint8_t>> companions;
};
std::map<void*,CreationGroundingRows> tcg_rows;
void tcg_bind(void* resource){
    auto& storage=tcg_rows[resource];
    skg::field<void*>(resource,0x2BF4)=storage.classes.data();
    skg::field<void*>(resource,0x2BF8)=storage.classes.data()+storage.classes.size();
    skg::field<void*>(resource,0x2BFC)=storage.classes.data()+storage.classes.size();
    rows_for(resource);auto& raw=t_rows[resource];
    for(size_t slot=0;slot<raw.size()/0x238;++slot){
        auto* marker=raw.data()+slot*0x238+4;auto& acts=storage.companions.at(slot);
        skg::field<void*>(marker,0x28)=acts.data();skg::field<void*>(marker,0x2C)=acts.data()+acts.size();
        skg::field<void*>(marker,0x30)=acts.data()+acts.size();
    }
}
int SK_THISCALL tcg_release(void* resource){
    if(skg::field<int>(resource,4)==1&&t_allocations.contains(resource))tcg_rows.erase(resource);
    return tmc_release(resource);
}
void* SK_THISCALL tcg_copy(void* destination,const void* source){
    t_copy(destination,source);auto* from=const_cast<void*>(source);
    tcg_rows[destination]=tcg_rows.at(from);act_cache_rows[destination]=act_cache_rows.at(from);
    tmc_properties[destination]=tmc_properties.at(from);
    act_cache_bind(destination,act_cache_rows.at(destination));tcg_bind(destination);return destination;
}
bool SK_THISCALL tcg_write(void* resource,Stream* stream){
    Stream acts;if(!act_cache_write(resource,&acts))return false;
    Rows classes,markers;
    if(!rows(resource,0x2BF4,0x27E8,8,classes)||!rows(resource,0x2C10,0x238,8,markers))return false;
    uint32_t count=0;for(auto* row=markers.begin;row!=markers.end;row+=0x238)if(live_slot(row))++count;
    const std::array<uint32_t,4> header{0x43475231,uint32_t(acts.bytes.size()),uint32_t((classes.end-classes.begin)/0x27E8),count};
    Stream body;
    if(!write(&body,header.data(),sizeof header)||!write(&body,acts.bytes.data(),uint32_t(acts.bytes.size()))||
       !write(&body,classes.begin,uint32_t(classes.end-classes.begin)))return false;
    for(auto* row=markers.begin;row!=markers.end;row+=0x238)if(live_slot(row)){
        Rows companions;if(!rows(row+4,0x28,0x34,8,companions))return false;
        std::array<uint8_t,0x238> stable;std::memcpy(stable.data(),row,stable.size());
        skg::field<uint32_t>(stable.data(),0)=0;std::memset(stable.data()+4+0x28,0,12);
        const auto count=uint32_t((companions.end-companions.begin)/0x34);
        if(!write(&body,stable.data(),uint32_t(stable.size()))||!write(&body,&count,4)||
           !write(&body,companions.begin,count*0x34))return false;
    }
    auto properties=tmc_properties.contains(resource)?tmc_properties.at(resource):editor_native_terrain::Properties{};
    properties.header={0xF9,0xD4,0x23,0xB5,17,0,0,0};properties.tail=std::move(body.bytes);
    std::vector<uint8_t> encoded;std::string error;
    if(!editor_native_terrain::encode(properties,encoded,error))return false;
    return write(stream,encoded.data(),uint32_t(encoded.size()));
}
bool SK_THISCALL tcg_read(void* resource,Stream* stream){
    editor_native_terrain::Properties properties;std::string error;
    if(!editor_native_terrain::decode(stream->bytes,properties,error))return false;
    Stream body;body.bytes=std::move(properties.tail);std::array<uint32_t,4> header{};
    if(read(&body,header.data(),sizeof header)!=sizeof header||header[0]!=0x43475231||header[1]>body.bytes.size()||header[2]>8||header[3]>8)return false;
    Stream acts;acts.bytes.resize(header[1]);
    if(read(&body,acts.bytes.data(),header[1])!=int(header[1])||!act_cache_read(resource,&acts))return false;
    auto& storage=tcg_rows[resource];storage.classes.resize(header[2]*0x27E8);
    if(read(&body,storage.classes.data(),uint32_t(storage.classes.size()))!=int(storage.classes.size()))return false;
    auto& markers=t_rows[resource];markers.resize(header[3]*0x238);storage.companions.resize(header[3]);
    for(size_t slot=0;slot<header[3];++slot){
        uint32_t count=0;if(read(&body,markers.data()+slot*0x238,0x238)!=0x238||read(&body,&count,4)!=4||count>8)return false;
        auto& companion=storage.companions[slot];companion.resize(count*0x34);
        if(read(&body,companion.data(),uint32_t(companion.size()))!=int(companion.size()))return false;
    }
    if(body.position!=body.bytes.size())return false;
    tcg_bind(resource);properties.tail.clear();tmc_properties[resource]=std::move(properties);
    stream->position=uint32_t(stream->bytes.size());return true;
}
Snapshot tcg_setup(){
    const auto prior=tmc_setup();tcg_rows.clear();
    a_write=uintptr_t(tcg_write);a_read=uintptr_t(tcg_read);a_copy=uintptr_t(tcg_copy);
    t_resource_vt[1]=reinterpret_cast<void*>(tcg_release);
    auto& storage=tcg_rows[t_live.data()];storage.classes.resize(0x27E8);
    skg::field<uint32_t>(storage.classes.data(),0)=7;
    auto& raw=t_rows[t_live.data()];raw.assign(4*0x238,0);storage.companions.resize(4);
    for(size_t slot=0;slot<4;++slot){
        auto* row=raw.data()+slot*0x238;skg::field<uint32_t>(row,0)=0x80000000;
        if(slot!=1&&slot!=3)continue;
        skg::field<uint32_t>(row,0)=0;auto* marker=row+4;
        skg::field<uint32_t>(marker,0)=7;skg::field<editor::Vec3>(marker,4)={float(10+slot),float(20+slot),float(30+slot)};
        skg::field<editor::Rotation>(marker,0x10)={0,0,0,1};skg::field<float>(marker,0x20)=1;
        skg::field<float>(marker,0x24)=1;skg::field<uint32_t>(marker,0x228)=uint32_t(100+slot);
        storage.companions[slot].resize(2*0x34);storage.companions[slot][0x31]=uint8_t(slot);
    }
    tcg_bind(t_live.data());bind({{{9001,4},1},{{9002,7},3}});
    auto identity=prior.document;identity.act_order={{(3ull<<32)|101,2},{(3ull<<32)|103,5}};
    for(auto it=identity.settings.begin();it!=identity.settings.end();)if(it->second.kind==editor::SettingsKind::Act)it=identity.settings.erase(it);else ++it;
    for(size_t i=0;i<2;++i){editor::AuthoringSettings setting;setting.object={(4ull<<32)|uint64_t(201+i),uint32_t(3+i)};
        setting.act_id=identity.act_order[i];identity.settings.emplace(setting.object,setting);}
    std::map<editor::ObjectId,editor::TerrainRecord> records;
    for(auto [id,row]:identity.terrain){row.object={id.id+1000,4};records.emplace(row.object,std::move(row));}
    identity.terrain=std::move(records);bind_acts(identity);
    Snapshot entry;std::string error;CHECK(capture(entry,error));entry.document.project=t_scope.project;entry.document.epoch=t_scope.epoch;
    CHECK(validate(entry,entry.document,error));bind_acts(entry.document);
    // The first row has an existing exact grounding witness. The second row is
    // deliberately unchanged, so a ticket must retain the complete roster.
    auto* marker=raw.data()+0x238+4;const auto authored=skg::field<editor::Vec3>(marker,4);
    auto rendered=authored;rendered.z=std::nextafter(rendered.z,100.f);skg::field<editor::Vec3>(marker,4)=rendered;
    terrain_grounding[editor::ObjectId{9001,4}]={rendered,authored};
    Snapshot grounded;CHECK(capture_draft(grounded,error));grounded.document.project=t_scope.project;grounded.document.epoch=t_scope.epoch;
    CHECK(validate(grounded,entry.document,error));
    return grounded;
}
void test_creation_grounding_return(){
    struct Routes {
        uintptr_t writer=a_write,reader=a_read,copier=a_copy,acts=a_copy_acts,crew=a_copy_crew,shown=a_layout_shown;
        uintptr_t initialization=a_scenario_initialization,modes=a_get_simulator_mode_manager;const SkHost* host=H;
        ~Routes(){CHECK(t_allocations.empty());tcg_rows.clear();tmc_properties.clear();act_cache_rows.clear();
            a_write=writer;a_read=reader;a_copy=copier;a_copy_acts=acts;a_copy_crew=crew;a_layout_shown=shown;
            a_scenario_initialization=initialization;a_get_simulator_mode_manager=modes;H=host;reset();}
    } routes;
    std::string error;auto entry=tcg_setup();SceneOwner scene;CHECK(capture_scene_owner(scene,error));
    CHECK((entry.bindings==std::vector<Binding>{{{9001,4},0},{{9002,7},1}}));
    CHECK(bindings.at(1)==editor::ObjectId(9001,4)&&bindings.at(3)==editor::ObjectId(9002,7));
    const auto raw=t_rows.at(t_live.data());const auto columns=tmf_columns;
    const auto live=t_live;const auto owner=t_data;const auto history=t_history;
    CreationTerrainTicket ticket;CHECK(capture_creation_terrain(t_scope,7,scene,entry,ticket,error));
    CHECK(ticket&&!ticket->retired&&ticket->owned_bytes<=(64u<<20));
    tmc_excursion();CHECK(bindings.empty()&&act_identity.act_order.empty()&&terrain_grounding.empty());
    CHECK(resume_creation_terrain(ticket,t_scope,7,scene,entry,generation,error)&&ticket->retired);
    CHECK((bindings==std::map<uint32_t,editor::ObjectId>{{1,{9001,4}},{3,{9002,7}}}));
    CHECK(act_identity.act_order==entry.document.act_order&&act_identity.terrain==entry.document.terrain&&terrain_grounding.size()==1);
    Snapshot returned;CHECK(capture_draft(returned,error));CHECK(validate(returned,entry.document,error));
    CHECK(returned.bindings==entry.bindings&&returned.document.placements==entry.document.placements);
    CHECK(t_rows.at(t_live.data())==raw&&tmf_columns==columns&&t_live==live&&t_data==owner&&t_history==history&&!t_restores&&t_allocations.empty());
    CHECK(!resume_creation_terrain(ticket,t_scope,7,scene,entry,generation,error));
    // A changed raw row, entry identity, or caller context cannot acquire the
    // old grounding/identity maps, even when the replacement is one ULP away.
    for(unsigned reason=0;reason<20;++reason){
        entry=tcg_setup();CHECK(capture_scene_owner(scene,error));ticket.reset();CHECK(capture_creation_terrain(t_scope,7,scene,entry,ticket,error));tmc_excursion();
        auto key=entry;auto scope=t_scope;auto expected=scene;auto returned_generation=generation;
        auto* marker=t_rows.at(t_live.data()).data()+0x238+4;
        if(reason==0)skg::field<uint32_t>(marker-4,0)=0x80000000;
        if(reason==1)skg::field<uint32_t>(t_rows.at(t_live.data()).data(),0)=0;
        if(reason==2)skg::field<uint32_t>(marker,0)=8;
        if(reason==3)++skg::field<uint32_t>(marker,0x228);
        if(reason==4)skg::field<float>(marker,4)=std::nextafter(skg::field<float>(marker,4),100.f);
        if(reason==5)std::swap_ranges(t_rows.at(t_live.data()).begin()+0x238,t_rows.at(t_live.data()).begin()+2*0x238,t_rows.at(t_live.data()).begin()+3*0x238);
        if(reason==6)++key.document.placements.begin()->second.revision;
        if(reason==7)key.document.settings_schema.begin()->second.optional^=true;
        if(reason==8)std::reverse(key.document.act_order.begin(),key.document.act_order.end());
        if(reason==9)key.bytes.back()^=1;
        if(reason==10)++scope.author.generation;
        if(reason==11)++scope.epoch;
        if(reason==12)++expected.source.instance;
        if(reason==13)++returned_generation;
        if(reason==14)C.creation_return_current=[](const auto&,uint64_t){return false;};
        if(reason==15)C.creation_return_current=[](const auto&,uint64_t)->bool{throw 1;};
        if(reason==16){auto calls=std::make_shared<unsigned>(0);C.creation_return_current=[calls](const auto&,uint64_t){return ++*calls==1;};}
        if(reason==17){C.projection_scope=[](){auto changed=t_scope;++changed.epoch;return changed;};}
        if(reason==18){C.creation_return_current=[](const auto&,uint64_t){auto* row=t_rows.at(t_live.data()).data()+3*0x238+4;++skg::field<uint32_t>(row,0x228);return true;};}
        if(reason==19){skg::field<void*>(t_mode.data(),0x74)=t_other_data.data();}
        const auto untouched=t_rows.at(t_live.data());const auto properties=tmc_properties.at(t_live.data());
        CHECK(!resume_creation_terrain(ticket,scope,7,expected,key,returned_generation,error)&&ticket->retired&&!error.empty());
        CHECK(bindings.empty()&&terrain_grounding.empty()&&act_identity.act_order.empty()&&terrain_materialization.empty());
        if(reason!=18)CHECK(t_rows.at(t_live.data())==untouched);
        const auto& current_properties=tmc_properties.at(t_live.data());
        CHECK(current_properties.header==properties.header&&current_properties.fields==properties.fields&&current_properties.tail==properties.tail&&!t_restores&&t_allocations.empty());
    }
    // Capture never invents grounding when raw XYZ differs from the snapshot.
    for(unsigned reason=0;reason<4;++reason){
        entry=tcg_setup();CHECK(capture_scene_owner(scene,error));ticket.reset();
        if(reason==0)terrain_grounding.clear();
        if(reason==1)terrain_grounding.begin()->second.authored.x=99;
        if(reason==2)terrain_grounding.begin()->second.native.x=99;
        if(reason==3)bindings[1]={9999,1};
        CHECK(!capture_creation_terrain(t_scope,7,scene,entry,ticket,error)&&!ticket&&!error.empty());
        CHECK(!t_restores&&t_allocations.empty());
    }
    // Reentrant caller retirement releases its own input storage. The adapter
    // must keep private value copies alive and must not publish after expiry.
    for(bool returning:{false,true}){
        auto holder=std::make_shared<Snapshot>(tcg_setup());CHECK(capture_scene_owner(scene,error));
        ticket.reset();if(returning){CHECK(capture_creation_terrain(t_scope,7,scene,*holder,ticket,error));tmc_excursion();}
        const auto retained=ticket;bool current=true;
        C.creation_return_current=[&](const auto&,uint64_t){holder.reset();ticket.reset();current=false;return current;};
        if(returning)CHECK(!resume_creation_terrain(ticket,t_scope,7,scene,*holder,generation,error));
        else CHECK(!capture_creation_terrain(t_scope,7,scene,*holder,ticket,error));
        CHECK(!holder&&!ticket&&!error.empty()&&!t_restores&&t_allocations.empty());
        if(returning)CHECK(retained->retired&&bindings.empty()&&terrain_grounding.empty()&&act_identity.act_order.empty());
    }
}
