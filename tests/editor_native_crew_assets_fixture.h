// Private canonical Crew ref rewrites, exercising the production native ABI.
namespace {
void* SK_THISCALL crew_asset_copy_resource(void* destination,const void* source){
    t_copy(destination,source);
    act_cache_copy_acts(skg::ptr_add<void>(destination,0x23C),reinterpret_cast<const uintptr_t*>(static_cast<const uint8_t*>(source)+0x23C));
    act_cache_copy_crew(skg::ptr_add<void>(destination,0x178),reinterpret_cast<const uintptr_t*>(static_cast<const uint8_t*>(source)+0x178));
    return destination;
}
void test_owned_crew_asset_versions(){
    reset();const auto old_alloc=a_alloc,old_copy=a_copy,old_acts=a_copy_acts,old_crew=a_copy_crew,old_rows=a_copy_marker_rows,old_insert=a_insert_marker,old_write=a_write,old_read=a_read;
    const auto* old_host=H;SkHost host=*H;host.sym=act_cache_symbol;H=&host;
    a_alloc=uintptr_t(act_cache_allocate);a_copy=uintptr_t(crew_asset_copy_resource);a_copy_acts=uintptr_t(act_cache_copy_acts);
    a_copy_crew=uintptr_t(act_cache_copy_crew);a_copy_marker_rows=uintptr_t(t_pose_copy_rows);a_insert_marker=uintptr_t(t_pose_insert);a_write=uintptr_t(act_cache_write);a_read=uintptr_t(act_cache_read);
    t_resource_vt[1]=reinterpret_cast<void*>(act_cache_release);act_cache_alloc_failures=0;act_cache_resolve_failure=act_cache_fail_fingerprint_write=false;
    act_cache_omit_external_source=true;
    C.authoring_tracked=[] {return true;};t_rows[t_live.data()].clear();rows_for(t_live.data());
    auto& live=act_cache_rows[t_live.data()];live.acts.resize(0x534);live.goals.resize(1);live.crew.resize(3*0x38);
    const editor::ResourceKey original{0x29058782,0x2B978C46,0x40626200},other{0x12345678,0x2B978C46,0x40626200};
    const editor::ResourceKey version{0x7D123456,0x2B978C46,0x40626200};
    for(size_t i=0;i<3;++i){auto* row=live.crew.data()+i*0x38;
        skg::field<editor::ResourceKey>(row,0)=i==1?other:original;skg::field<uint64_t>(row,0x10)=UINT64_MAX;
        for(size_t offset=0x18;offset<0x38;++offset)row[offset]=uint8_t(11+i*31+offset);
    }
    act_cache_bind(t_live.data(),live);skg::field<int32_t>(t_live.data(),0x238)=3;
    skg::field<editor::ResourceKey>(t_live.data(),8)={0x7D999999,0x366A930D,0x408A0000};
    skg::field<editor::Rotation>(t_live.data(),0x15C)={0,0,0,1};skg::field<float>(t_live.data(),0x16C)=1;skg::field<float>(t_live.data(),0x170)=1;
    editor::Document identity;identity.project[0]=4;identity.epoch=9;identity.act_order={{71,3}};
    editor::AuthoringSettings act;act.object={81,2};act.act_id=identity.act_order.front();identity.settings.emplace(act.object,act);bind_acts(identity);
    Snapshot base;std::string error;CHECK(capture(base,error));base.document.project=identity.project;base.document.epoch=identity.epoch;
    base.document.revision=42;base.document.act_revision=11;base.document.next_id+=19;base.document.next_terrain_order=27;
    for(auto& [id,row]:base.document.settings){(void)id;row.revision=37;row.field_revisions[{editor::Field::Property,kCrewRosterProperty}]=29;}
    CHECK(validate(base,base.document,error));
    {Owned parsed(parse(base.bytes,error));CHECK(parsed.p&&skg::field<editor::ResourceKey>(parsed.p,8)==editor::ResourceKey{}&&base.source.instance);}
    const auto scene=t_live;const auto owner=t_data;const auto history=t_history;const auto context=t_context;const auto selection_before=selection;
    const auto live_rows=live.crew;const auto before_restores=t_restores;const auto generation_before=generation;
    auto unchanged_scene=[&]{CHECK(t_live==scene&&t_data==owner&&t_history==history&&t_context==context&&selection==selection_before&&live.crew==live_rows&&t_restores==before_restores&&generation==generation_before&&pending.empty()&&t_allocations.empty());};
    std::vector<editor::ResourceKey> keys;CHECK(crew_assets(base.document,keys,error)&&keys==std::vector<editor::ResourceKey>({original,other,original}));
    Snapshot output;const bool remapped=remap_crew_assets(base,{{0,original,version}},output,error);
    if(!remapped)std::printf("Crew asset remap: %s\n",error.c_str());CHECK(remapped);
    auto expected=base.document;expected.settings.at(kCrewObject).properties.at(kCrewRosterProperty)=output.document.settings.at(kCrewObject).properties.at(kCrewRosterProperty);
    CHECK(output.document==expected&&output.source==base.source&&output.bindings==base.bindings&&output.metadata==base.metadata&&output.act==base.act);
    CHECK(crew_assets(output.document,keys,error)&&keys==std::vector<editor::ResourceKey>({version,other,original}));
    {Owned parsed(parse(output.bytes,error));CHECK(parsed.p&&skg::field<editor::ResourceKey>(parsed.p,8)==editor::ResourceKey{});Rows rows;CHECK(editor_native::rows(parsed.p,0x178,0x38,3,rows));
     CHECK(skg::field<int32_t>(parsed.p,0x238)==3&&rows.end-rows.begin==3*0x38);
     CHECK(std::memcmp(rows.begin+0x18,live_rows.data()+0x18,0x20)==0);
     CHECK(std::memcmp(rows.begin+0x38,live_rows.data()+0x38,2*0x38)==0);
     CHECK(skg::field<uint32_t>(rows.begin,0xC)==0&&skg::field<uint64_t>(rows.begin,0x10)==UINT64_MAX);}
    unchanged_scene();
    // Same asset in a different slot is independent, including opaque flags.
    CHECK(remap_crew_assets(base,{{2,original,version}},output,error));
    CHECK(crew_assets(output.document,keys,error)&&keys==std::vector<editor::ResourceKey>({original,other,version}));unchanged_scene();
    t_game_mode_id=modes::kMenu;CHECK(remap_crew_assets(base,{{0,original,version}},output,error));
    CHECK(t_game_mode_id==modes::kMenu);t_game_mode_id=modes::kScenario;unchanged_scene();
    CHECK(remap_crew_assets(base,{},output,error)&&output.bytes==base.bytes&&output.document==base.document);unchanged_scene();
    // A native server-backed row may retain a raw placeholder key. Check its
    // resolved canonical identity, then remove only that selected row's hints.
    const auto old_resolved=act_cache_resolved;act_cache_resolved=original;Snapshot hinted;
    CHECK(edit_owned_snapshot(base,[&](void* owned,std::string&){
        Rows rows;CHECK(editor_native::rows(owned,0x178,0x38,3,rows));
        skg::field<editor::ResourceKey>(rows.begin,0)={1,2,3};skg::field<uint32_t>(rows.begin,0xC)=77;skg::field<uint64_t>(rows.begin,0x10)=17;return true;
    },hinted,error));
    CHECK(remap_crew_assets(hinted,{{0,original,version}},output,error));
    CHECK(crew_assets(output.document,keys,error)&&keys==std::vector<editor::ResourceKey>({version,other,original}));unchanged_scene();
    act_cache_resolve_failure=true;CHECK(!remap_crew_assets(hinted,{{0,original,version}},output,error));act_cache_resolve_failure=false;
    act_cache_resolved=old_resolved;unchanged_scene();
    for(unsigned fail=0;fail<8;++fail){
        std::vector<CrewAssetRemap> edits{{0,original,version}};
        if(fail==0)edits[0].slot=3;if(fail==1)edits[0].expected=other;if(fail==2)edits.push_back(edits.front());
        if(fail==3)edits[0].replacement={};if(fail==4)++edits[0].replacement.type;if(fail==5)++edits[0].replacement.group;
        if(fail==6)edits.resize(4,edits.front());if(fail==7)act_cache_alloc_failures=1;
        Snapshot untouched;untouched.document.revision=999;untouched.bytes={0x77};
        CHECK(!remap_crew_assets(base,edits,untouched,error)&&untouched.document.revision==999&&untouched.bytes==std::vector<uint8_t>{0x77});
        unchanged_scene();
    }
    Snapshot reordered;CHECK(edit_owned_snapshot(base,[&](void* owned,std::string&){
        Rows rows;CHECK(editor_native::rows(owned,0x178,0x38,3,rows));std::swap_ranges(rows.begin,rows.begin+0x38,rows.begin+0x38);return true;
    },reordered,error));
    CHECK(!remap_crew_assets(reordered,{{0,original,version}},output,error));unchanged_scene();
    Snapshot removed;CHECK(edit_owned_snapshot(base,[&](void* owned,std::string&){
        Rows rows;CHECK(editor_native::rows(owned,0x178,0x38,3,rows));const std::array<uintptr_t,3> remaining{uintptr_t(rows.begin+0x38),uintptr_t(rows.end),uintptr_t(rows.end)};
        // Borrow from an independent owning copy: native fixed-vector assign
        // must not read a subrange of the vector it is replacing.
        std::vector<uint8_t> copied(rows.begin+0x38,rows.end);const std::array<uintptr_t,3> source{uintptr_t(copied.data()),uintptr_t(copied.data()+copied.size()),uintptr_t(copied.data()+copied.size())};
        CHECK(remaining[1]-remaining[0]==2*0x38);act_cache_copy_crew(skg::ptr_add<void>(owned,0x178),source.data());return true;
    },removed,error));
    CHECK(!remap_crew_assets(removed,{{2,original,version}},output,error));unchanged_scene();
    for(unsigned bad=0;bad<11;++bad){
        auto document=base.document;auto& row=document.settings.at(kCrewObject);
        if(bad==0)document.settings.erase(kCrewObject);
        if(bad==1)row.kind=editor::SettingsKind::Act;if(bad==2)row.act_id={1,1};
        if(bad==3)row.properties[kCrewCapacityProperty]=int64_t(2);
        if(bad>=4&&bad<=7){auto& block=std::get<editor::StructuredValue>(row.properties.at(kCrewRosterProperty));
            if(bad==4)++block.format;if(bad==5)block.references.push_back({1,1});if(bad==6)block.bytes={0};
            if(bad==7)skg::field<uint32_t>(block.bytes.data(),20)=4;
        }
        if(bad==8)row.properties[kCrewRosterProperty]=true;
        if(bad==9)document.settings_schema.erase(kCrewRosterProperty);
        if(bad==10)document.settings_schema.at(kCrewCapacityProperty).kind=editor::ValueKind::Boolean;
        keys={version};CHECK(!crew_assets(document,keys,error)&&keys==std::vector<editor::ResourceKey>{version});unchanged_scene();
    }
    // Legacy drafts advertise no Crew support; they are not malformed modern
    // rosters and must not acquire an invented entry through this helper.
    editor::Document legacy;keys={version};CHECK(crew_assets(legacy,keys,error)&&keys.empty());
    legacy.settings_schema[kCrewRosterProperty]=authoring_property_schema().at(kCrewRosterProperty);
    CHECK(!crew_assets(legacy,keys,error));unchanged_scene();
    act_cache_omit_external_source=false;act_cache_rows.clear();a_alloc=old_alloc;a_copy=old_copy;a_copy_acts=old_acts;a_copy_crew=old_crew;a_copy_marker_rows=old_rows;a_insert_marker=old_insert;a_write=old_write;a_read=old_read;H=old_host;reset();
}
} // namespace
