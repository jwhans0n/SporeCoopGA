// Actual i686 hook ABI and owned-byte provenance, not a live-game result.
namespace {
namespace tmf=editor_native_terrain_materialization;
std::array<tmf::Columns,2> tmf_columns;
unsigned tmf_calls=0;bool tmf_bad=false,tmf_stale=false;
void tmf_bind(){
    for(size_t k=0;k<2;++k){const std::array<unsigned,6> offsets=k?std::array<unsigned,6>{0xA0,0xB4,0,0,0,0}:std::array<unsigned,6>{0x14,0x28,0x50,0x64,0x3C,0x8C};
        for(size_t c=0;c<(k?2u:6u);++c){auto& bytes=tmf_columns[k][c];
            auto* first=bytes.empty()?nullptr:bytes.data();auto* last=bytes.empty()?nullptr:bytes.data()+bytes.size();
            skg::field<void*>(t_terraform.data(),offsets[c])=first;skg::field<void*>(t_terraform.data(),offsets[c]+4)=last;skg::field<void*>(t_terraform.data(),offsets[c]+8)=last;
        }
    }
}
void SK_THISCALL tmf_native(void* self){
    CHECK(self==t_terraform.data());++tmf_calls;
    for(auto& columns:tmf_columns){auto& bytes=columns[1];for(size_t n=0;n<bytes.size();n+=56){
        auto* transform=bytes.data()+n;skg::field<uint16_t>(transform,0)|=4;skg::field<uint16_t>(transform,2)++;
        skg::field<float>(transform,4)=std::nextafter(skg::field<float>(transform,4),1000.f);
        if(tmf_bad)skg::field<float>(transform,0x10)=2;
    }}
    if(tmf_stale)++t_scope.epoch;
}
std::vector<uint8_t> tmf_bytes(){
    tmf::Recipe recipe;std::string error;CHECK(materialization_recipe(t_terraform.data(),recipe,error));
    editor_native_terrain::Properties properties;properties.header={0xF9,0xD4,0x23,0xB5,17,0,0,0};properties.tail={'D','B','P','F',1,2,3};
    properties.fields[0xFFFFFFFF]={0xA,0,{1,2,3,4}};std::vector<uint8_t> base;CHECK(editor_native_terrain::encode(properties,base,error));
    editor::Document d;CHECK(editor_native_terrain::project(base,{},d,error));uint64_t serial=d.next_id;
    for(size_t k=0;k<2;++k)for(const auto& payload:recipe.rows[k]){
        editor::TerrainRecord row;row.object={serial++,1};row.kind=editor::TerrainKind(k);row.payload=payload;row.order=d.next_terrain_order++;
        d.terrain.emplace(row.object,std::move(row));
    }
    std::vector<uint8_t> result;CHECK(editor_native_terrain::merge(base,d,result,error));return result;
}
void tmf_setup(){
    setup_terrain_retry();clear_terrain_materialization();tmf_calls=0;tmf_bad=tmf_stale=false;
    original_terrain_materialize=tmf_native;tmf_columns={};
    for(size_t k=0;k<2;++k){auto& c=tmf_columns[k];c[0].resize(12);skg::field<uint32_t>(c[0].data(),0)=uint32_t(123+k);skg::field<uint32_t>(c[0].data(),4)=k?19:0;
        c[1].resize(56);auto* t=c[1].data();skg::field<uint16_t>(t,0)=6;skg::field<uint16_t>(t,2)=1;
        skg::field<float>(t,4)=13+float(k);skg::field<float>(t,0x10)=1;
        skg::field<float>(t,0x14)=skg::field<float>(t,0x24)=skg::field<float>(t,0x34)=1;
        if(!k)for(size_t n=2;n<6;++n){c[n].resize(4);skg::field<float>(c[n].data(),0)=1;}
    }
    tmf_bind();CHECK(state().ready);
}
unsigned tmf_updates=0,tmf_appends=0,tmf_removes=0;
void tmf_append(editor::TerrainKind kind,const editor::ResourceKey& key,const void* transform){
    auto& columns=tmf_columns[size_t(kind)];std::array<uint8_t,56> owned_transform;
    std::memcpy(owned_transform.data(),transform,owned_transform.size());
    const auto* key_bytes=reinterpret_cast<const uint8_t*>(&key);
    columns[0].insert(columns[0].end(),key_bytes,key_bytes+12);
    columns[1].insert(columns[1].end(),owned_transform.begin(),owned_transform.end());
    if(kind==editor::TerrainKind::Effect)for(size_t c=2;c<6;++c){
        const std::array<uint8_t,4> value{0,0,0x80,0x3F};columns[c].insert(columns[c].end(),value.begin(),value.end());
    }
    ++tmf_appends;tmf_bind();
}
void SK_THISCALL tmf_append_model(void* self,const editor::ResourceKey* key,const void* transform){
    CHECK(self==t_terraform.data());tmf_append(editor::TerrainKind::Model,*key,transform);
}
void SK_THISCALL tmf_append_effect(void* self,uint32_t,uint32_t,const void* transform,uint32_t,uint32_t,uint32_t){
    CHECK(self==t_terraform.data());tmf_append(editor::TerrainKind::Effect,{456,0,789},transform);
}
void SK_THISCALL tmf_update(void* self,bool){
    CHECK(self==t_terraform.data());++tmf_updates;
    if(!skg::field<bool>(self,0x1B8))return;
    const auto kind=t_terrain_definition[0x68]?editor::TerrainKind::Model:editor::TerrainKind::Effect;
    const editor::ResourceKey key{456,19,789};const auto* transform=tmf_columns[size_t(kind)][1].data();
    if(kind==editor::TerrainKind::Model)terrain_append_model(self,&key,transform);
    else terrain_append_effect(self,456,0,transform,0,0,0);
    skg::field<uint32_t>(t_terrain_descriptor.data(),8)=1;
    skg::field<bool>(self,0x1B8)=false;
}
void SK_THISCALL tmf_remove(void* self,void*){CHECK(self==t_terraform.data());++tmf_removes;}
void tmf_preview_setup(editor::TerrainKind kind,uint32_t index){
    tmf_setup();tmf_updates=tmf_appends=tmf_removes=0;t_granted=true;
    t_terrain_descriptor.fill(0);t_terrain_definition.fill(0);t_terrain_definition[0x68]=kind==editor::TerrainKind::Model;
    skg::field<void*>(t_terrain_descriptor.data(),0)=t_actors[0].data();
    skg::field<void*>(t_terrain_descriptor.data(),4)=t_terrain_definition.data();
    skg::field<uint32_t>(t_terrain_descriptor.data(),8)=index;
    skg::field<void*>(t_terraform.data(),0x1B0)=t_terrain_descriptor.data();skg::field<bool>(t_terraform.data(),0x1B8)=true;
    a_terrain_selection=uintptr_t(t_terrain_selected);original_terrain_update=tmf_update;
    original_terrain_append_model=tmf_append_model;original_terrain_append_effect=tmf_append_effect;original_terrain_remove=tmf_remove;
    pin_terrain_before();
}
void test_terrain_preview_update(){
    for(auto kind:{editor::TerrainKind::Effect,editor::TerrainKind::Model})for(auto index:{0u,1u,UINT32_MAX}){
        tmf_preview_setup(kind,index);const auto before=tmf_columns;const auto draft=t_live;
        terrain_start(t_terraform.data());CHECK(terrain_starts==1&&!terrain_commits);
        terrain_update(t_terraform.data(),false);
        CHECK(tmf_updates==1&&tmf_appends==1&&tmf_columns[size_t(kind)][0].size()==24);
        CHECK(terrain_authored_rows[size_t(kind)]==std::set<size_t>{1});
        CHECK(terrain_authored_rows[1-size_t(kind)].empty());
        CHECK(std::equal(before[size_t(kind)][0].begin(),before[size_t(kind)][0].end(),tmf_columns[size_t(kind)][0].begin()));
        CHECK(tmf_columns[1-size_t(kind)]==before[1-size_t(kind)]&&t_live==draft&&pending.empty());
        // A subsequent native Update acts on the allocated row, without a
        // second append or a second history item.
        C.terrain_gate=[](const auto&,const auto& fields){CHECK(fields.empty());return true;};
        editor::TerrainRecord first;first.object={55,1};first.kind=kind;first.order=1;
        editor::TerrainRecord added=first;added.object={56,1};added.order=2;
        terrain_before->document.terrain.emplace(first.object,first);terrain_before->document.terrain.emplace(added.object,added);
        terrain_update(t_terraform.data(),true);CHECK(tmf_updates==2&&tmf_appends==1&&terrain_starts==1&&!terrain_commits);
        terrain_commit(t_terraform.data());CHECK(terrain_commits==1&&terrain_pending&&tmf_appends==1);
    }
    for(unsigned reason=0;reason<15;++reason){
        tmf_preview_setup(editor::TerrainKind::Effect,UINT32_MAX);const auto before=tmf_columns;
        void* self=t_terraform.data();std::array<uint8_t,0x1CC> wrong_mode{};
        if(reason==0)t_granted=false;
        if(reason==1)self=wrong_mode.data();
        if(reason==2)skg::field<void*>(t_terrain_descriptor.data(),0)=nullptr;
        if(reason==3)skg::field<void*>(t_terrain_descriptor.data(),0)=t_actors[1].data();
        if(reason==4)skg::field<void*>(t_terrain_descriptor.data(),4)=nullptr;
        if(reason==5)skg::field<bool>(t_terraform.data(),0x1B8)=false;
        if(reason==6)++generation;
        if(reason==7)++t_scope.epoch;
        if(reason==8){skg::field<void*>(t_other_data.data(),0x10)=t_live.data();skg::field<void*>(t_mode.data(),0x74)=t_other_data.data();}
        if(reason==9)C.terrain_gate=[](const auto&,const auto&){skg::field<void*>(t_terraform.data(),0x1B0)=nullptr;return true;};
        if(reason==10)a_terrain_selection=0;
        if(reason==11){skg::field<void*>(wrong_mode.data(),0)=t_actors[0].data();skg::field<void*>(wrong_mode.data(),4)=t_terrain_definition.data();
            skg::field<void*>(t_terraform.data(),0x1B0)=wrong_mode.data();}
        if(reason==12)C.terrain_gate=[](const auto&,const auto&){skg::field<bool>(t_terraform.data(),0x1B8)=false;return true;};
        if(reason==13)C.terrain_gate=[](const auto&,const auto&){skg::field<uint32_t>(t_terrain_descriptor.data(),8)=0;return true;};
        if(reason==14)C.terrain_gate=[](const auto&,const auto&){skg::field<void*>(t_terrain_descriptor.data(),4)=nullptr;return true;};
        terrain_update(self,true);
        if(tmf_updates||tmf_appends||tmf_columns!=before)std::fprintf(stderr,"terrain preview rejection case %u mutated\n",reason);
        CHECK(!tmf_updates&&!tmf_appends&&tmf_columns==before&&terrain_authored_rows[0].empty()&&terrain_authored_rows[1].empty());
        CHECK(!terrain_starts&&!terrain_commits&&!terrain_pending&&!t_failed);
    }
    // A preview flag never relaxes Remove's requirement for a real row.
    tmf_preview_setup(editor::TerrainKind::Effect,UINT32_MAX);
    terrain_remove(t_terraform.data(),t_actors[0].data());CHECK(!tmf_removes&&terrain_authored_rows[0].empty());
    reset();
}
std::map<void*,editor_native_terrain::Properties> tmc_properties;
int SK_THISCALL tmc_release(void* resource){
    if(skg::field<int>(resource,4)==1&&t_allocations.contains(resource))tmc_properties.erase(resource);
    return act_cache_release(resource);
}
bool SK_THISCALL tmc_write(void* resource,Stream* stream){
    Stream body;if(!act_cache_write(resource,&body))return false;
    auto p=tmc_properties.contains(resource)?tmc_properties.at(resource):editor_native_terrain::Properties{};
    p.header={0xF9,0xD4,0x23,0xB5,17,0,0,0};p.tail=std::move(body.bytes);
    std::vector<uint8_t> encoded;std::string error;
    CHECK(editor_native_terrain::encode(p,encoded,error));return write(stream,encoded.data(),uint32_t(encoded.size()));
}
bool SK_THISCALL tmc_read(void* resource,Stream* stream){
    editor_native_terrain::Properties p;std::string error;
    if(!editor_native_terrain::decode(stream->bytes,p,error))return false;
    Stream body;body.bytes=std::move(p.tail);
    if(!act_cache_read(resource,&body))return false;
    p.tail.clear();tmc_properties[resource]=std::move(p);stream->position=uint32_t(stream->bytes.size());return true;
}
void SK_THISCALL tmc_export(void* resource){
    editor_native_terrain::Properties p;std::string error;
    CHECK(editor_native_terrain::decode(tmf_bytes(),p,error));p.tail.clear();tmc_properties[resource]=std::move(p);
}
uintptr_t tmc_symbol(const char* name){
    CHECK(std::string(name)=="SporeCoop::Editor::CaptureTerrain");return uintptr_t(tmc_export);
}
Snapshot tmc_setup(){
    tmf_setup();tmc_properties.clear();act_cache_rows.clear();
    t_rows[t_live.data()].clear();rows_for(t_live.data());
    auto& live=act_cache_rows[t_live.data()];live.acts.resize(2*0x534);live.goals.resize(2);act_cache_bind(t_live.data(),live);
    skg::field<float>(t_live.data(),0x16C)=1;skg::field<editor::Rotation>(t_live.data(),0x15C)={0,0,0,1};
    skg::field<editor::ResourceKey>(t_live.data(),8)={101,103,107};
    a_copy_acts=uintptr_t(act_cache_copy_acts);a_copy_crew=uintptr_t(act_cache_copy_crew);
    a_write=uintptr_t(tmc_write);a_read=uintptr_t(tmc_read);t_resource_vt[1]=reinterpret_cast<void*>(tmc_release);
    C.authoring_tracked=[]{return true;};C.creation_return_current=[](const auto& scope,uint64_t serial){return scope==t_scope&&serial==7;};
    static SkHost host;host=*H;host.sym=tmc_symbol;H=&host;
    v_simulator_modes.fill(0);v_initialization=4;v_world_visible=true;v_modes_missing=false;
    a_get_simulator_mode_manager=uintptr_t(v_get_simulator_modes);a_scenario_initialization=uintptr_t(&v_initialization);a_layout_shown=uintptr_t(v_world_shown);
    skg::field<void*>(t_terraform.data(),0x0C)=v_terrain.data();
    tmc_export(t_live.data());Snapshot entry;std::string error;CHECK(capture(entry,error));
    entry.document.project=t_scope.project;entry.document.epoch=t_scope.epoch;
    CHECK(validate(entry,entry.document,error));bind_acts(entry.document);
    terrain_materialize(t_terraform.data());CHECK(!terrain_materialization.empty()&&terrain_materialization_error.empty());
    return entry;
}
void tmc_excursion(){
    t_game_mode_id=modes::kEditor;CHECK(!state().editing&&terrain_materialization.empty());
    t_game_mode_id=modes::kScenario;CHECK(state().ready&&terrain_materialization.empty());
}
void test_creation_terrain_ticket(){
    const auto old_write=a_write,old_read=a_read,old_acts=a_copy_acts,old_crew=a_copy_crew,old_shown=a_layout_shown;
    const auto old_init=a_scenario_initialization,old_modes=a_get_simulator_mode_manager;const auto* old_host=H;
    std::string error;auto entry=tmc_setup();SceneOwner scene;CHECK(capture_scene_owner(scene,error));
    const auto raw=tmf_columns;const auto live=t_live;const auto data_before=t_data;const auto history=t_history;
    CreationTerrainTicket ticket;CHECK(capture_creation_terrain(t_scope,7,scene,entry,ticket,error));
    CHECK(ticket&&!ticket->retired&&ticket->owned_bytes<=(64u<<20));
    CHECK(tmf_columns==raw&&t_live==live&&t_data==data_before&&t_history==history&&t_allocations.empty());
    const auto original_generation=generation;tmc_excursion();CHECK(generation>original_generation);
    auto rendered=tmf_bytes();CHECK(normalize_materialized_terrain(rendered,error)&&rendered==tmf_bytes()); // No inference after retirement.
    CHECK(resume_creation_terrain(ticket,t_scope,7,scene,entry,generation,error)&&ticket->retired);
    auto normalized=tmf_bytes();CHECK(normalize_materialized_terrain(normalized,error));tmf::Recipe expected,actual;
    CHECK(tmf::from_bytes(entry.bytes,expected,error)&&tmf::from_bytes(normalized,actual,error)&&actual==expected);
    CHECK(!resume_creation_terrain(ticket,t_scope,7,scene,entry,generation,error));
    bind_acts(entry.document);Snapshot returned;const bool captured_return=capture_draft(returned,error);
    if(!captured_return)std::printf("creation terrain return capture: %s\n",error.c_str());CHECK(captured_return);
    const bool valid_return=validate(returned,entry.document,error);
    if(!valid_return)std::printf("creation terrain return validation: %s\n",error.c_str());CHECK(valid_return);
    CHECK(tmf_columns==raw&&t_live==live&&t_data==data_before&&t_history==history&&!t_restores&&t_allocations.empty());
    for(unsigned reason=0;reason<23;++reason){
        entry=tmc_setup();CHECK(capture_scene_owner(scene,error));ticket.reset();CHECK(capture_creation_terrain(t_scope,7,scene,entry,ticket,error));tmc_excursion();
        auto scope=t_scope;auto owner=scene;auto key=entry;uint64_t serial=7,native_generation=generation;
        if(reason==0)++scope.epoch;if(reason==1)++scope.author.generation;if(reason==2)++serial;if(reason==3)++native_generation;
        if(reason==4)++owner.source.instance;if(reason==5)owner.resource=1;if(reason==6)owner.mode=1;if(reason==7)owner.data=1;
        if(reason==8)key.bytes.back()^=1;if(reason==9)std::reverse(key.document.act_order.begin(),key.document.act_order.end());
        if(reason==10)key.document.settings_schema.begin()->second.optional^=true;
        if(reason==11)++key.source.instance;if(reason==12)key.captain_locked=!key.captain_locked;
        if(reason==13)key.metadata->text[0]=u"another entry";
        if(reason==14)key.document.terrain.begin()->second.order++;
        if(reason==15)tmf_columns[0][1][4]^=1;if(reason==16)tmf_columns[1][1][2]++;
        if(reason==17)v_initialization=3;if(reason==18)v_world_visible=false;
        if(reason==19)skg::field<bool>(t_mode.data(),0xDC)=true;
        if(reason==20)skg::field<bool>(v_simulator_modes.data(),0x28)=true;
        if(reason==21)C.creation_return_current={};
        if(reason==22)skg::field<void*>(t_mode.data(),0x18)=nullptr;
        const auto untouched=tmf_columns;const auto resource=t_live;
        CHECK(!resume_creation_terrain(ticket,scope,serial,owner,key,native_generation,error)&&ticket->retired&&!error.empty());
        CHECK(tmf_columns==untouched&&t_live==resource&&!t_restores&&t_allocations.empty());
        CHECK(!resume_creation_terrain(ticket,t_scope,7,scene,entry,generation,error));
    }
    for(unsigned reason=0;reason<5;++reason){
        entry=tmc_setup();CHECK(capture_scene_owner(scene,error));CreationTerrainTicket unchanged;
        if(reason==0)C.creation_return_current={};if(reason==1)++scene.source.instance;
        if(reason==2)entry.bytes[0]^=1;
        if(reason==3)tmf_columns[0][1][4]^=1;
        if(reason==4){projected_snapshots.clear();parsed_images.clear();projected_snapshot_bytes=parsed_image_bytes=0;t_alloc_fail=true;}
        const auto untouched=tmf_columns;const auto resource=t_live;
        CHECK(!capture_creation_terrain(t_scope,7,scene,entry,unchanged,error)&&!unchanged);
        CHECK(tmf_columns==untouched&&t_live==resource&&!t_restores&&t_allocations.empty());
    }
    // Native validation does not account for portable historical identities.
    // An oversized owned sidecar must still be rejected before copying the
    // ticket, preserving the scene and its existing exact materialization.
    entry=tmc_setup();
    for(size_t n=0;n<(64u<<20)/48+1;++n)entry.document.tombstones.emplace(editor::ObjectId{500000000+n,1},1);
    CHECK(editor::bytes(entry.document)>(64u<<20)&&validate(entry,entry.document,error));
    CHECK(capture_scene_owner(scene,error));ticket.reset();
    const auto before_resource=t_live;const auto witness_bytes=terrain_materialization.owned_bytes();
    CHECK(!capture_creation_terrain(t_scope,7,scene,entry,ticket,error)&&!ticket&&error.find("64 MiB")!=std::string::npos);
    CHECK(t_live==before_resource&&terrain_materialization.owned_bytes()==witness_bytes&&!t_restores&&t_allocations.empty());
    tmc_properties.clear();act_cache_rows.clear();a_write=old_write;a_read=old_read;a_copy_acts=old_acts;a_copy_crew=old_crew;
    a_layout_shown=old_shown;a_scenario_initialization=old_init;a_get_simulator_mode_manager=old_modes;H=old_host;reset();
}
#include "editor_native_terrain_transition_fixture.h"
void* SK_THISCALL tmc_inverse_copy(void* destination,const void* source){
    t_copy(destination,source);act_cache_rows[destination]=act_cache_rows.at(const_cast<void*>(source));
    act_cache_bind(destination,act_cache_rows.at(destination));tmc_properties[destination]=tmc_properties.at(const_cast<void*>(source));
    return destination;
}
std::function<void()> tmc_inverse_read_callback;
bool SK_THISCALL tmc_inverse_read(void* resource,Stream* stream){
    const bool okay=tmc_read(resource,stream);if(tmc_inverse_read_callback)tmc_inverse_read_callback();return okay;
}
void test_empty_placement_terrain_inverse(){
    struct Routes {
        uintptr_t writer=a_write,reader=a_read,copy=a_copy,acts=a_copy_acts,crew=a_copy_crew,marker_rows=a_copy_marker_rows,insert=a_insert_marker;
        uintptr_t allocate=a_alloc,entry=a_entry,restore=a_restore,loaded=a_terrain_loaded,vtable=a_terrain_vtable;
        uintptr_t shown=a_layout_shown,init=a_scenario_initialization,modes=a_get_simulator_mode_manager;
        TerrainLoadFn load=original_terrain_load;HistoryFn rebuild=original_terrain_rebuild;
        TerrainAppendEffectFn append=original_terrain_append_effect;SelectActFn select=original_select_act;
        const SkHost* host=H;
        ~Routes(){
            tmc_inverse_read_callback={};CHECK(t_allocations.empty());tmc_properties.clear();act_cache_rows.clear();reset();
            a_write=writer;a_read=reader;a_copy=copy;a_copy_acts=acts;a_copy_crew=crew;a_copy_marker_rows=marker_rows;a_insert_marker=insert;
            a_alloc=allocate;a_entry=entry;a_restore=restore;a_terrain_loaded=loaded;a_terrain_vtable=vtable;
            a_layout_shown=shown;a_scenario_initialization=init;a_get_simulator_mode_manager=modes;
            original_terrain_load=load;original_terrain_rebuild=rebuild;original_terrain_append_effect=append;original_select_act=select;H=host;
        }
    } routes;
    for(unsigned rejection=0;rejection<10;++rejection){
        view_fixture();auto canonical=tmc_setup();std::string error;canonical.metadata.reset();
        tmc_inverse_read_callback={};canonical.document.revision=1;t_canonical_revision=1;
        for(auto& [id,row]:canonical.document.terrain){(void)id;row.revision=1;}
        bind_acts(canonical.document);
        LocalView camera;CHECK(capture_view(camera,error));
        CHECK(canonical.document.placements.empty()&&canonical.bindings.empty()&&canonical.document.terrain.size()==2);
        const auto baseline=tmf_columns;const auto canonical_document=canonical.document;
        C.projection_scope=[] {return t_scope;};C.terrain_gate=[](const auto&,const auto&){return true;};
        Snapshot untouched_a,untouched_b;CHECK(capture_draft(untouched_a,error)&&capture_draft(untouched_b,error));
        CHECK(untouched_a.document.revision==0&&untouched_a.document.terrain==untouched_b.document.terrain);
        for(const auto& [id,row]:untouched_a.document.terrain){(void)id;CHECK(row.revision==0);}
        // Runtime adds the authenticated project/epoch before wire encoding.
        // The native projection must already satisfy its revision constraints.
        auto encoded_input=untouched_a.document;encoded_input.project=t_scope.project;encoded_input.epoch=t_scope.epoch;
        CHECK(editor::valid(encoded_input,editor::Limits{},error));
        CHECK(canonical.document.revision==1&&canonical.document.terrain==canonical_document.terrain);
        unsigned dependency_calls=0;
        C.validate_dependencies=[&,rejection](const Snapshot&,std::string& e){
            ++dependency_calls;
            if(rejection==1){e="controlled empty-roster dependency rejection";return false;}
            if(rejection==2)++skg::field<editor::ResourceKey>(t_live.data(),8).instance;
            if(rejection==3)C.projection_scope=[] {return GestureScope{};};
            if(rejection==5)++t_canonical_revision;
            return true;
        };
        a_copy=uintptr_t(tmc_inverse_copy);a_copy_marker_rows=uintptr_t(t_pose_copy_rows);a_insert_marker=uintptr_t(t_pose_insert);
        ttt_restore_bind();original_terrain_load=ttt_load;original_terrain_rebuild=ttt_rebuild;
        original_terrain_append_effect=tmf_append_effect;
        v_terrain.fill(0);a_terrain_vtable=0x24681357;skg::field<uintptr_t>(v_terrain.data(),0)=a_terrain_vtable;
        skg::field<void*>(t_terraform.data(),0x190)=v_terrain.data();a_terrain_loaded=uintptr_t(v_terrain_loaded);
        skg::field<void*>(t_context.data(),4)=nullptr;t_granted=true;unsigned deliveries=0;
        C.gesture=[&](Gesture gesture){
            ++deliveries;CHECK(terrain_capture_rotations&&terrain_capture_rotations->canonical.empty());
            CHECK(gesture.before.document.placements.empty()&&gesture.after.document.placements.empty());
            CHECK(gesture.before.document.terrain.size()==2&&gesture.after.document.terrain.size()==3);
            for(const auto& [id,row]:gesture.before.document.terrain){
                CHECK(row.revision==0&&gesture.after.document.terrain.at(id)==row);
            }
            const bool supported=supported_changes(gesture,error);
            if(!supported)std::fprintf(stderr,"empty placement terrain support: %s\n",error.c_str());CHECK(supported);
            const auto changed=tmf_columns;
            const auto camera_before=v_camera;const auto selected=skg::field<void*>(t_context.data(),4);
            if(rejection==4)t_alloc_fail=true;
            if(rejection==6)++t_canonical_revision;
            if(rejection==7)C.canonical_revision={};
            if(rejection==8)C.canonical_revision=[]()->std::optional<editor::Revision>{throw 7;};
            bool read_expired=false;
            if(rejection==9){
                projected_snapshots.clear();parsed_images.clear();projected_snapshot_bytes=parsed_image_bytes=0;
                a_read=uintptr_t(tmc_inverse_read);tmc_inverse_read_callback=[&]{if(!read_expired){read_expired=true;++t_canonical_revision;}};
            }
            const auto inverse=editor_native::apply(canonical_document,editor::Change{},&canonical,error);
            tmc_inverse_read_callback={};
            if(rejection){
                CHECK(!inverse&&!error.empty());
                if(rejection==1)CHECK(error.find("dependency rejection")!=std::string::npos);
                if(rejection==5)CHECK(dependency_calls==1&&t_canonical_revision==2);
                if(rejection>=6&&rejection<=8)CHECK(!dependency_calls);
                if(rejection==9)CHECK(read_expired&&!dependency_calls);
                CHECK(!t_restores&&tmf_columns==changed&&v_camera==camera_before&&skg::field<void*>(t_context.data(),4)==selected);return;
            }
            if(!inverse)std::fprintf(stderr,"empty placement terrain inverse: %s\n",error.c_str());CHECK(inverse);
            CHECK(t_restores==1&&!terrain_capture_rotations&&tmf_columns[0][0]==baseline[0][0]);
            CHECK(tmf_columns[1][0]==baseline[1][0]&&tmf_columns[0][0].size()==12);
        };
        terrain_start(t_terraform.data());CHECK(terrain_starts==1&&terrain_before&&terrain_capture_rotations);
        CHECK(terrain_capture_rotations->canonical.empty());
        const auto transform=tmf_columns[0][1];terrain_append_effect(t_terraform.data(),456,0,transform.data(),0,0,0);
        CHECK(tmf_columns[0][0].size()==24&&terrain_authored_rows[0]==std::set<size_t>{1});
        terrain_commit(t_terraform.data());CHECK(terrain_commits==1&&terrain_pending);
        ++t_frame;drain_terrain(nullptr);CHECK(deliveries==1&&!terrain_pending&&!terrain_before&&!t_failed&&t_allocations.empty());
        if(!rejection){
            ++t_frame;drain_local_view(nullptr);Snapshot verified;CHECK(capture_draft(verified,error));
            const bool valid=validate(verified,canonical_document,error);
            if(!valid)std::fprintf(stderr,"empty placement restored draft: %s\n",error.c_str());CHECK(valid);
            auto native_terrain=canonical_document.terrain;for(auto& [id,row]:native_terrain){(void)id;row.revision=0;}
            CHECK(verified.document.placements.empty()&&verified.document.terrain==native_terrain);
            CHECK(verified.bindings.empty()&&terrain_starts==1&&terrain_commits==1&&t_restores==1);
            LocalView current_camera;CHECK(capture_view(current_camera,error)&&same_view(camera,current_camera));
            CHECK(skg::field<void*>(t_context.data(),4)==nullptr);
        }
        CHECK(canonical.document.revision==1&&canonical.document.terrain==canonical_document.terrain);
    }
}
void test_terrain_materialization_read_scope(){
    std::string error;
    for(unsigned reason=0;reason<12;++reason){
        tmf_setup();C.projection_scope=[] {return t_scope;};
        const auto authored=tmf_bytes();terrain_materialize(t_terraform.data());
        const auto rendered=tmf_bytes();CHECK(rendered!=authored&&!terrain_materialization.empty());
        const auto witness_bytes=terrain_materialization.owned_bytes();
        // Awaiting a normal commit withdraws mutation authorization while the
        // exact project/scene read identity remains current.
        C.gesture_scope=[] {return GestureScope{};};t_granted=true;
        auto normalized=rendered;
        CHECK(normalize_materialized_terrain(normalized,error)&&normalized==authored);
        CHECK(terrain_materialization.owned_bytes()==witness_bytes&&!terrain_materialization.empty());
        CHECK(!terrain_begin(t_terraform.data())&&!terrain_before&&!terrain_starts);
        CHECK(tmf_bytes()==rendered); // Read proofs never write the native recipe.
        auto alternate_mode=t_mode;auto alternate_data=t_data;auto alternate_resource=t_live;auto alternate_tool=t_terraform;
        if(reason==0)++t_scope.project[0];if(reason==1)++t_scope.epoch;
        if(reason==2)++t_scope.author.slot;if(reason==3)++t_scope.author.generation;
        if(reason==4)++generation;
        if(reason==5)t_mode_pointer=alternate_mode.data();
        if(reason==6)skg::field<void*>(t_mode.data(),0x74)=alternate_data.data();
        if(reason==7)skg::field<void*>(t_data.data(),0x10)=alternate_resource.data();
        if(reason==8)skg::field<void*>(t_mode.data(),0x18)=alternate_tool.data();
        if(reason==9)++skg::field<editor::ResourceKey>(t_live.data(),8).instance;
        if(reason==10)C.projection_scope=[] {return GestureScope{};};
        if(reason==11)t_game_mode_id=modes::kEditor;
        normalized=rendered;
        CHECK(normalize_materialized_terrain(normalized,error)&&normalized==rendered&&terrain_materialization.empty());
        CHECK(!terrain_materialization_owner);
    }
    reset();
}
void test_terrain_materialization_hooks(){
    test_empty_placement_terrain_inverse();
    test_terrain_materialization_read_scope();
    tmf_setup();const auto before=tmf_bytes();const auto resource_before=t_live;std::string error;
    terrain_materialize(t_terraform.data());CHECK(tmf_calls==1&&terrain_materialization_error.empty()&&!terrain_materialization.empty());
    const auto rendered=tmf_bytes();CHECK(rendered!=before);auto normalized=rendered;
    CHECK(normalize_materialized_terrain(normalized,error)&&normalized==before);CHECK(tmf_bytes()==rendered&&t_live==resource_before);
    terrain_materialize(t_terraform.data());auto twice=tmf_bytes();CHECK(normalize_materialized_terrain(twice,error)&&twice==before);
    // Native F36750 imports exact geometry using destination-owned Transform
    // counters. The real F35E10 wrapper must chain its original authored XYZ
    // through that reset for effects and models without rewriting live bytes.
    for(auto& columns:tmf_columns)for(size_t n=0;n<columns[1].size();n+=56)
        skg::field<uint16_t>(columns[1].data()+n,2)=0;
    tmf_bind();const auto imported=tmf_bytes();normalized=imported;
    CHECK(normalize_materialized_terrain(normalized,error)&&normalized==imported);
    terrain_materialize(t_terraform.data());const auto import_output=tmf_bytes();normalized=import_output;
    CHECK(normalize_materialized_terrain(normalized,error)&&normalized==before&&tmf_bytes()==import_output);
    CHECK(authored_terrain_append(t_terraform.data(),editor::TerrainKind::Effect));CHECK(terrain_authored_rows[0]==std::set<size_t>{1});
    // The selected descriptor marks its exact native kind/index as genuinely
    // authored before a move, independently of other proxy bookkeeping.
    std::array<uint8_t,16> descriptor{};std::array<uint8_t,0x69> definition{};definition[0x68]=1;
    skg::field<void*>(descriptor.data(),4)=definition.data();
    CHECK(authored_terrain_record(descriptor.data()));CHECK(terrain_authored_rows[1]==std::set<size_t>{0});
    // Every owner/generation/project/participant/source replacement retires the
    // exact proof before it can normalize bytes for another native context.
    for(unsigned reason=0;reason<9;++reason){
        tmf_setup();terrain_materialize(t_terraform.data());const auto after=tmf_bytes();
        if(reason==0)++generation;if(reason==1)++t_scope.epoch;if(reason==2)++t_scope.project[0];if(reason==3)++t_scope.author.generation;
        if(reason==4)skg::field<uint32_t>(t_live.data(),8)++;
        if(reason==5)skg::field<void*>(t_data.data(),0x10)=t_other_data.data();
        if(reason==6)skg::field<void*>(t_mode.data(),0x18)=nullptr;
        if(reason==7)t_game_mode_id=0;if(reason==8)t_active=false;
        auto other=after;CHECK(normalize_materialized_terrain(other,error)&&other==after&&terrain_materialization.empty());
    }
    tmf_setup();tmf_stale=true;terrain_materialize(t_terraform.data());CHECK(terrain_materialization.empty()&&terrain_materialization_error.empty());
    // Unaudited output freezes via one deferred failure, never inside the
    // native hook. A clean context can later establish a fresh proof.
    tmf_setup();tmf_bad=true;terrain_materialize(t_terraform.data());CHECK(!terrain_materialization_error.empty()&&!state().ready&&!t_failed);
    auto bad=tmf_bytes(),retained=bad;CHECK(!normalize_materialized_terrain(bad,error)&&bad==retained);
    // This isolated recipe fixture has no fake native UI tree. Keep unrelated
    // periodic UI fencing out of its deferred-failure queue.
    const auto prior_fence=fence_queued;fence_queued=true;
    editor_native::tick();drain();CHECK(t_failed==1);editor_native::tick();drain();CHECK(t_failed==1);fence_queued=prior_fence;
    tmf_setup();terrain_materialize(t_terraform.data());CHECK(terrain_materialization_error.empty()&&state().ready);
    reset();test_terrain_preview_update();test_creation_terrain_ticket();test_terrain_transition_receipts();
}
} // namespace
