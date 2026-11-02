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
void test_terrain_materialization_hooks(){
    tmf_setup();const auto before=tmf_bytes();const auto resource_before=t_live;std::string error;
    terrain_materialize(t_terraform.data());CHECK(tmf_calls==1&&terrain_materialization_error.empty()&&!terrain_materialization.empty());
    const auto rendered=tmf_bytes();CHECK(rendered!=before);auto normalized=rendered;
    CHECK(normalize_materialized_terrain(normalized,error)&&normalized==before);CHECK(tmf_bytes()==rendered&&t_live==resource_before);
    terrain_materialize(t_terraform.data());auto twice=tmf_bytes();CHECK(normalize_materialized_terrain(twice,error)&&twice==before);
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
    reset();
}
} // namespace
