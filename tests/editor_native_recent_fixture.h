// Actual-i686 adapter with controlled native owners. No running-game ABI claim.
namespace {
unsigned recent_live_writes=0,recent_all_writes=0,recent_fail_write=0,recent_resolves=0;
std::function<void(void*,unsigned)> recent_write_callback;
std::function<void(void*,unsigned)> recent_after_write_callback;
std::function<void(unsigned,editor::ResourceKey&)> recent_resolve_callback;
editor::ResourceKey recent_resolved{17,19,23};
bool SK_THISCALL recent_pose_write(void* resource,Stream* stream){
    ++recent_all_writes;
    if(recent_fail_write==recent_all_writes)return false;
    const auto live=resource==t_live.data()?++recent_live_writes:0;
    if(recent_write_callback)recent_write_callback(resource,live);
    const auto okay=t_pose_write(resource,stream);
    if(recent_after_write_callback)recent_after_write_callback(resource,live);
    return okay;
}
bool SK_CDECL recent_resolve(uint64_t server,editor::ResourceKey* key){
    CHECK(server==17);*key=recent_resolved;++recent_resolves;
    if(recent_resolve_callback)recent_resolve_callback(recent_resolves,*key);
    return true;
}
uintptr_t recent_symbol(const char* name){
    CHECK(std::string(name)=="Simulator::Scenario::ResolveLocalServerAsset");return uintptr_t(recent_resolve);
}
struct RecentRoutes {
    uintptr_t writer=a_write,reader=a_read,copier=a_copy_marker_rows,inserter=a_insert_marker,acts=a_copy_acts,crew=a_copy_crew;
    ~RecentRoutes(){
        recent_write_callback={};recent_after_write_callback={};recent_resolve_callback={};act_cache_rows.clear();
        a_write=writer;a_read=reader;a_copy_marker_rows=copier;a_insert_marker=inserter;a_copy_acts=acts;a_copy_crew=crew;
        reset();CHECK(t_allocations.empty());
    }
};
Snapshot recent_pose_setup(){
    reset();recent_write_callback={};recent_after_write_callback={};recent_resolve_callback={};recent_live_writes=recent_all_writes=recent_fail_write=0;
    t_pose_class.fill(0);t_pose_act.fill(0);skg::field<uint32_t>(t_pose_class.data(),0)=1024;
    skg::field<void*>(t_live.data(),0x2BF4)=t_pose_class.data();skg::field<void*>(t_live.data(),0x2BF8)=t_pose_class.data()+t_pose_class.size();
    skg::field<void*>(t_live.data(),0x23C)=t_pose_act.data();skg::field<void*>(t_live.data(),0x240)=t_pose_act.data()+t_pose_act.size();
    skg::field<editor::ResourceKey>(t_live.data(),8)={101,103,107};
    auto& raw=t_rows[t_live.data()];
    for(unsigned i=0;i<2;++i){auto* marker=raw.data()+i*0x238+4;skg::field<uint32_t>(marker,0)=1024;
        skg::field<editor::Vec3>(marker,4)={float(i),0,0};skg::field<editor::Rotation>(marker,0x10)={0,0,0,1};
        skg::field<float>(marker,0x20)=1;skg::field<float>(marker,0x24)=.5f;skg::field<uint32_t>(marker,0x100)=77+i;}
    a_write=uintptr_t(recent_pose_write);a_read=uintptr_t(t_pose_read);a_copy_marker_rows=uintptr_t(t_pose_copy_rows);a_insert_marker=uintptr_t(t_pose_insert);
    Snapshot base;std::string error;CHECK(capture(base,error));base.document.project=t_scope.project;base.document.epoch=t_scope.epoch;
    CHECK(validate(base,base.document,error));
    recent_live_writes=recent_all_writes=0;return base;
}
Snapshot recent_act_setup(bool resolve_captain){
    reset();recent_write_callback={};recent_resolve_callback={};recent_resolves=0;recent_resolved={17,19,23};
    a_write=uintptr_t(act_cache_write);a_read=uintptr_t(act_cache_read);a_copy_acts=uintptr_t(act_cache_copy_acts);a_copy_crew=uintptr_t(act_cache_copy_crew);
    t_resource_vt[1]=reinterpret_cast<void*>(act_cache_release);C.authoring_tracked=[]{return true;};
    t_rows[t_live.data()].clear();rows_for(t_live.data());
    auto& live=act_cache_rows[t_live.data()];live={};live.acts.resize(2*0x534);live.goals.resize(2);
    live.acts[0x500]=3;live.acts[0x534+0x500]=4;act_cache_bind(t_live.data(),live);
    skg::field<uint32_t>(t_live.data(),0x238)=1;
    skg::field<editor::ResourceKey>(t_live.data(),8)={101,103,107};
    skg::field<float>(t_live.data(),0x16C)=1;skg::field<editor::Rotation>(t_live.data(),0x15C)={0,0,0,1};
    skg::field<uint64_t>(t_live.data(),0x140)=resolve_captain?17:UINT64_MAX;
    if(resolve_captain){static SkHost host;host=*H;host.sym=recent_symbol;H=&host;}
    Snapshot base;std::string error;CHECK(capture(base,error));base.document.project=t_scope.project;base.document.epoch=t_scope.epoch;return base;
}
void test_recent_native_exact_and_fences(){
    RecentRoutes restore;std::string error;
    auto base=recent_pose_setup();const auto original=t_rows[t_live.data()];const auto reads=t_reads;
    const auto hit_before=projected_snapshot_hits;Snapshot matched;
    CHECK(recent_matches(base,base.document,generation,matched,error)==RecentMatches::Match);
    CHECK(matched.bytes==base.bytes&&matched.document==base.document&&matched.bindings==base.bindings);
    CHECK(recent_live_writes==2&&t_reads==reads&&projected_snapshot_hits>hit_before);
    CHECK(t_rows[t_live.data()]==original&&!t_restores&&pending.empty());
    // Own typed projection and cache entries cannot be rewritten by the caller.
    auto forged=base.document;forged.placements.begin()->second.position.x=99;
    CHECK(!validate(base,forged,error));CHECK(validate(base,base.document,error));
    CHECK(projected_snapshots.size()<=4&&projected_snapshot_bytes<=(32u<<20));
    for(unsigned reason=0;reason<9;++reason){
        base=recent_pose_setup();Snapshot untouched;untouched.bytes={0xCD};const auto raw=t_rows[t_live.data()];
        auto expected=base;auto wanted=base.document;auto expected_generation=generation;
        if(reason==0)++expected_generation;
        if(reason==1)++expected.source.instance;
        if(reason==2)std::swap(expected.bindings[0].marker,expected.bindings[1].marker);
        if(reason==3)expected.captain={1,2,3};
        if(reason==4)expected.captain_locked=true;
        if(reason==5)wanted.placements.begin()->second.position.x=99;
        if(reason==6)skg::field<int>(t_mode.data(),0xCC)=2;
        if(reason==7)skg::field<void*>(t_data.data(),0x10)=nullptr;
        if(reason==8)expected.act=1;
        const auto result=recent_matches(expected,wanted,expected_generation,untouched,error);
        CHECK(result==(reason==1?RecentMatches::Different:RecentMatches::Error)); // Save may retain a participant-local source alias.
        CHECK(untouched.bytes==std::vector<uint8_t>{0xCD}&&t_rows[t_live.data()]==raw&&!t_restores);
    }
    for(unsigned reason=0;reason<7;++reason){
        base=recent_pose_setup();const auto raw=t_rows[t_live.data()];
        if(reason==0)skg::field<float>(t_rows[t_live.data()].data()+4,4)+=1;
        if(reason==1)t_rows[t_live.data()][4+0x100]^=1; // Opaque native field, absent from the typed pose.
        if(reason==2)skg::field<uint32_t>(t_live.data(),0x2C4C)^=1;
        if(reason==3)skg::field<uint32_t>(t_data.data(),0x7C)=9; // Nonserialized metadata witness.
        if(reason==4)base.metadata.reset(); // Legacy unknown is never assumed equal to known live metadata.
        if(reason==5)terrain_grounding.emplace(base.bindings.front().object,Grounding{});
        if(reason==6)skg::field<uint32_t>(t_context.data(),0)=1; // state() reads the native editor context, not a_act.
        const auto changed=t_rows[t_live.data()];Snapshot untouched;untouched.bytes={0xCD};
        const auto result=recent_matches(base,base.document,generation,untouched,error);
        if(result!=RecentMatches::Different)std::printf("recent difference case %u: result=%u error=%s\n",reason,unsigned(result),error.c_str());
        CHECK(result==RecentMatches::Different);
        CHECK(untouched.bytes==std::vector<uint8_t>{0xCD}&&t_rows[t_live.data()]==changed&&!t_restores);
        if(reason!=0&&reason!=1)CHECK(changed==raw);
    }
    for(unsigned reason=0;reason<4;++reason){
        base=recent_pose_setup();recent_write_callback=[reason](void*,unsigned live){if(live!=1)return;
            if(reason==0)++t_scope.epoch;if(reason==1)++generation;
            if(reason==2)skg::field<void*>(t_mode.data(),0x74)=t_other_data.data();
            if(reason==3)skg::field<void*>(t_data.data(),0x10)=nullptr;};
        CHECK(recent_matches(base,base.document,generation,matched,error)==RecentMatches::Error);
        CHECK(recent_live_writes==1&&!t_restores);recent_write_callback={};
    }
    // Same-owner mutations during native serialization force ordinary capture;
    // no borrowed canonical projection may conceal pose or opaque edits.
    for(bool opaque:{false,true}){
        base=recent_pose_setup();recent_write_callback=[opaque](void*,unsigned live){if(live!=1)return;
            if(opaque)t_rows[t_live.data()][4+0x100]^=1;else skg::field<float>(t_rows[t_live.data()].data()+4,4)+=1;};
        CHECK(recent_matches(base,base.document,generation,matched,error)==RecentMatches::Different);
        CHECK(recent_live_writes==2&&!t_restores);recent_write_callback={};
    }
    base=recent_pose_setup();Snapshot refused;refused.bytes={0xCD};const auto raw=t_rows[t_live.data()];recent_fail_write=1;
    CHECK(recent_matches(base,base.document,generation,refused,error)==RecentMatches::Error);
    CHECK(refused.bytes==std::vector<uint8_t>{0xCD}&&t_rows[t_live.data()]==raw&&!t_restores);recent_fail_write=0;
    base=recent_pose_setup();recent_after_write_callback=[](void*,unsigned live){
        if(live==2)skg::field<float>(t_rows[t_live.data()].data()+4,4)+=1;};
    CHECK(recent_matches(base,base.document,generation,matched,error)==RecentMatches::Different);
    CHECK(recent_live_writes==2&&!t_restores);recent_after_write_callback={};
    base=recent_pose_setup();auto cache_reads=t_reads;
    ++t_scope.epoch;CHECK(validate(base,base.document,error)&&t_reads>cache_reads);cache_reads=t_reads;
    ++generation;CHECK(validate(base,base.document,error)&&t_reads>cache_reads);
    // More than four real projected byte images evict older owning proofs.
    for(unsigned i=0;i<7;++i){auto candidate=base;skg::field<uint32_t>(candidate.bytes.data(),8)=100+i;
        CHECK(validate(candidate,candidate.document,error));CHECK(projected_snapshots.size()<=4&&projected_snapshot_bytes<=(32u<<20));}
}
void test_recent_native_resolver_and_act_identity(){
    RecentRoutes restore;std::string error;Snapshot matched;
    auto base=recent_act_setup(false);CHECK(validate(base,base.document,error));
    auto reordered=base.document;std::reverse(reordered.act_order.begin(),reordered.act_order.end());const auto reads=act_cache_reads;
    CHECK(!validate(base,reordered,error)&&act_cache_reads>reads); // Old cache borrowed the wrong native-index mapping.
    auto rebound=base.document;auto row=rebound.settings.at({(7ull<<32),1});rebound.settings.erase(row.object);row.object={888,2};rebound.settings.emplace(row.object,row);
    bind_acts(rebound);const auto hits=projected_snapshot_hits;Snapshot captured;
    CHECK(capture(captured,error)&&captured.document.settings.contains(row.object)&&projected_snapshot_hits==hits);
    CHECK(!captured.document.settings.contains({(7ull<<32),1}));
    base=recent_act_setup(true);CHECK(validate(base,base.document,error));const auto old_reads=act_cache_reads;
    CHECK(validate(base,base.document,error)&&act_cache_reads==old_reads);
    recent_resolved.instance+=1;CHECK(!validate(base,base.document,error));
    for(unsigned changed=0;changed<2;++changed){
        base=recent_act_setup(true);CHECK(validate(base,base.document,error));bind_acts(base.document);
        const auto before_hits=projected_snapshot_hits;recent_resolves=0;
        recent_resolve_callback=[changed](unsigned call,editor::ResourceKey&){if(call!=2)return;
            if(changed)++generation;else ++t_scope.epoch;};
        CHECK(!capture(captured,error)&&projected_snapshot_hits==before_hits&&!error.empty());
        recent_resolve_callback={}; // A callback expired the live owner; do not fall through using its old pointer.
    }
    for(unsigned race:{3u,4u}){
        base=recent_act_setup(true);projected_snapshots.clear();projected_snapshot_bytes=0;recent_resolves=0;
        recent_resolve_callback=[race](unsigned call,editor::ResourceKey& key){if(call==race)++key.instance;};
        CHECK(!validate(base,base.document,error)&&projected_snapshots.empty());recent_resolve_callback={};
        CHECK(validate(base,base.document,error));
    }
    // The same-owner live scene changes inside final expected-image resolver
    // validation, after the first Write, must precede the final byte witness.
    base=recent_act_setup(true);CHECK(validate(base,base.document,error));recent_resolves=0;
    recent_resolve_callback=[](unsigned call,editor::ResourceKey&){if(call==3)act_cache_rows[t_live.data()].acts[0x500]^=1;};
    CHECK(recent_matches(base,base.document,generation,matched,error)==RecentMatches::Different);
    recent_resolve_callback={};CHECK(!t_restores&&pending.empty());
}
void test_recent_pose_reverse_fallback(){
    RecentRoutes restore;auto base=recent_pose_setup();std::string error;
    auto next=base.document;next.revision=1;const auto id=next.placements.begin()->first;
    auto& p=next.placements.at(id);p.position.y=3;p.revision=1;p.field_revisions[{editor::Field::Position,0}]=1;
    editor::Change change;change.revision=1;change.patch.fields.push_back({id,{editor::Field::Position,0},base.document.placements.at(id).position,p.position,0});
    const auto raw=t_rows[t_live.data()];Snapshot prepared;
    t_pose_row_copies=t_pose_inserts=0;const auto hits=pose_roundtrip_hits;
    CHECK(prepare(base,next,change,nullptr,prepared,error)&&pose_roundtrip_hits==hits+1&&!t_pose_row_copies&&!t_pose_inserts);
    CHECK(t_rows[t_live.data()]==raw&&prepared.document==next);
    projected_snapshots.clear();projected_snapshot_bytes=0;CHECK(validate(base,base.document,error));
    recent_all_writes=0;recent_fail_write=2;const auto fallback=pose_roundtrip_fallbacks;
    CHECK(prepare(base,next,change,nullptr,prepared,error)&&pose_roundtrip_fallbacks==fallback+1);
    CHECK(t_pose_row_copies==2&&t_pose_inserts==4&&t_rows[t_live.data()]==raw);
    auto opaque=prepared;opaque.bytes[12+4+0x100]^=1;Gesture check;check.before=base;check.after=opaque;
    CHECK(!supported_changes(check,error));recent_fail_write=0;
}
void test_recent_native_proofs(){
    test_recent_native_exact_and_fences();test_recent_native_resolver_and_act_identity();test_recent_pose_reverse_fallback();
}
}
