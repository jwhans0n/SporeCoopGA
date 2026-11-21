// Included by the actual-i686 production adapter fixture, not a live-game test.
namespace {
struct ActCacheRows {
    std::vector<uint8_t> acts,crew;
    std::vector<std::vector<uint8_t>> goals;
};
std::map<void*,ActCacheRows> act_cache_rows;
unsigned act_cache_reads=0,act_cache_copies=0,act_cache_alloc_failures=0;
bool act_cache_fail_fingerprint_write=false,act_cache_resolve_failure=false;
bool act_cache_omit_external_source=false;
editor::ResourceKey act_cache_resolved{17,19,23};
void act_cache_bind(void* resource,ActCacheRows& storage){
    skg::field<void*>(resource,0x23C)=storage.acts.data();
    skg::field<void*>(resource,0x240)=storage.acts.data()+storage.acts.size();
    skg::field<void*>(resource,0x244)=storage.acts.data()+storage.acts.size();
    for(size_t i=0;i<storage.goals.size();++i){
        auto* act=storage.acts.data()+i*0x534;auto& goals=storage.goals[i];
        skg::field<void*>(act,0x84)=goals.data();skg::field<void*>(act,0x88)=goals.data()+goals.size();
        skg::field<void*>(act,0x8C)=goals.data()+goals.size();
    }
    skg::field<void*>(resource,0x178)=storage.crew.data();
    skg::field<void*>(resource,0x17C)=storage.crew.data()+storage.crew.size();
    skg::field<void*>(resource,0x180)=storage.crew.data()+storage.crew.size();
}
int SK_THISCALL act_cache_release(void* resource){
    if(skg::field<int>(resource,4)==1&&t_allocations.contains(resource))act_cache_rows.erase(resource);
    return t_release(resource);
}
void* SK_CDECL act_cache_allocate(size_t size,const char* name,int first,int second,int third,int fourth){
    if(act_cache_alloc_failures){--act_cache_alloc_failures;return nullptr;}
    return t_allocate(size,name,first,second,third,fourth);
}
void* SK_THISCALL act_cache_copy_acts(void* destination,const uintptr_t* source){
    ++act_cache_copies;CHECK(source[2]==source[1]&&source[1]>=source[0]&&(source[1]-source[0])%0x534==0);
    const auto count=(source[1]-source[0])/0x534;CHECK(count<=8);
    auto* owner=static_cast<uint8_t*>(destination)-0x23C;
    auto& storage=act_cache_rows[owner];
    if(count)storage.acts.assign(reinterpret_cast<const uint8_t*>(source[0]),reinterpret_cast<const uint8_t*>(source[1]));
    else storage.acts.clear();
    storage.goals.resize(count);
    for(size_t i=0;i<count;++i){
        auto* act=storage.acts.data()+i*0x534;Rows goals;CHECK(rows(act,0x84,0x188,3,goals));
        if(goals.begin!=goals.end)storage.goals[i].assign(goals.begin,goals.end);else storage.goals[i].clear();
    }
    act_cache_bind(owner,storage);return destination;
}
void* SK_THISCALL act_cache_copy_crew(void* destination,const uintptr_t* source){
    CHECK(source[1]>=source[0]&&(source[1]-source[0])%0x38==0&&source[1]-source[0]<=3*0x38);
    auto* owner=static_cast<uint8_t*>(destination)-0x178;auto& storage=act_cache_rows[owner];
    if(source[0]!=source[1])storage.crew.assign(reinterpret_cast<const uint8_t*>(source[0]),reinterpret_cast<const uint8_t*>(source[1]));
    else storage.crew.clear();
    act_cache_bind(owner,storage);return destination;
}
bool SK_THISCALL act_cache_write(void* resource,Stream* stream){
    Rows acts,crew;CHECK(rows(resource,0x23C,0x534,8,acts)&&rows(resource,0x178,0x38,3,crew));
    const uint32_t count=uint32_t((acts.end-acts.begin)/0x534),crew_count=uint32_t((crew.end-crew.begin)/0x38);
    if(act_cache_fail_fingerprint_write&&count==2)return false;
    const std::array<uint32_t,3> prefix{0xB523D4F9,17,0};
    const std::array<uint32_t,4> header{0x41435431,count,crew_count,skg::field<uint32_t>(resource,0x238)};
    const auto source=act_cache_omit_external_source?editor::ResourceKey{}:skg::field<editor::ResourceKey>(resource,8);
    if(!write(stream,prefix.data(),sizeof prefix)||!write(stream,header.data(),sizeof header)||
       !write(stream,&source,sizeof source)||
       !write(stream,skg::ptr_add<void>(resource,0x130),0x46)||!write(stream,crew.begin,crew_count*0x38))return false;
    for(uint32_t i=0;i<count;++i){
        auto* act=acts.begin+i*0x534;Rows goals;CHECK(rows(act,0x84,0x188,3,goals));
        std::array<uint8_t,0x534> stable;std::memcpy(stable.data(),act,stable.size());std::memset(stable.data()+0x84,0,12);
        const auto goal_count=uint32_t((goals.end-goals.begin)/0x188);
        if(!write(stream,stable.data(),uint32_t(stable.size()))||!write(stream,&goal_count,4)||!write(stream,goals.begin,goal_count*0x188))return false;
    }
    return true;
}
bool SK_THISCALL act_cache_read(void* resource,Stream* stream){
    ++act_cache_reads;std::array<uint32_t,3> prefix{};std::array<uint32_t,4> header{};
    if(read(stream,prefix.data(),sizeof prefix)!=sizeof prefix||prefix!=std::array<uint32_t,3>{0xB523D4F9,17,0}||
       read(stream,header.data(),sizeof header)!=sizeof header||header[0]!=0x41435431||header[1]>8||header[2]>3||
       read(stream,skg::ptr_add<void>(resource,8),sizeof(editor::ResourceKey))!=sizeof(editor::ResourceKey)||
       read(stream,skg::ptr_add<void>(resource,0x130),0x46)!=0x46)return false;
    if(act_cache_omit_external_source)skg::field<editor::ResourceKey>(resource,8)={};
    auto& storage=act_cache_rows[resource];storage.crew.resize(header[2]*0x38);
    if(read(stream,storage.crew.data(),uint32_t(storage.crew.size()))!=int(storage.crew.size()))return false;
    storage.acts.resize(header[1]*0x534);storage.goals.resize(header[1]);
    for(uint32_t i=0;i<header[1];++i){
        uint32_t count=0;if(read(stream,storage.acts.data()+i*0x534,0x534)!=0x534||read(stream,&count,4)!=4||count>3)return false;
        auto& goals=storage.goals[i];goals.resize(count*0x188);
        if(read(stream,goals.data(),uint32_t(goals.size()))!=int(goals.size()))return false;
    }
    skg::field<uint32_t>(resource,0x238)=header[3];act_cache_bind(resource,storage);
    return stream->position==stream->bytes.size();
}
bool SK_CDECL act_cache_resolve(uint64_t server,editor::ResourceKey* key){
    CHECK(server==17);if(act_cache_resolve_failure)return false;*key=act_cache_resolved;return true;
}
uintptr_t act_cache_symbol(const char* name){
    CHECK(std::string(name)=="Simulator::Scenario::ResolveLocalServerAsset");return uintptr_t(act_cache_resolve);
}
bool act_cache_project(void* resource,editor::Document& document,std::string& error,const editor::Document& identity){
    // projection() supplies a fresh document to the authoring subtree helper.
    document={};return project_authoring(resource,document,error,identity);
}
void test_act_projection_production_cache(){
    reset();const auto old_alloc=a_alloc,old_copy=a_copy_acts,old_crew=a_copy_crew,old_write=a_write,old_read=a_read;
    const auto* old_host=H;SkHost host=*H;host.sym=act_cache_symbol;H=&host;
    a_alloc=uintptr_t(act_cache_allocate);a_copy_acts=uintptr_t(act_cache_copy_acts);a_copy_crew=uintptr_t(act_cache_copy_crew);
    a_write=uintptr_t(act_cache_write);a_read=uintptr_t(act_cache_read);t_resource_vt[1]=reinterpret_cast<void*>(act_cache_release);
    act_projection_cache.clear();act_cache_reads=act_cache_copies=0;act_cache_alloc_failures=0;
    act_cache_fail_fingerprint_write=act_cache_resolve_failure=false;act_cache_resolved={17,19,23};
    auto& live=act_cache_rows[t_live.data()];live.acts.resize(2*0x534);live.goals.resize(2);live.crew.resize(0x38);
    for(size_t i=0;i<2;++i){
        live.acts[i*0x534+0x500]=uint8_t(3+i);live.goals[i].resize(0x188);
        skg::field<uint32_t>(live.goals[i].data(),0)=1;skg::field<int32_t>(live.goals[i].data(),4)=int32_t(7+i);
        skg::field<int32_t>(live.goals[i].data(),8)=-1;skg::field<uint32_t>(live.goals[i].data(),0x14)=3;
        skg::field<uint32_t>(live.goals[i].data(),0x184)=0x1234;
    }
    skg::field<uint64_t>(live.crew.data(),0x10)=17;act_cache_bind(t_live.data(),live);
    skg::field<uint32_t>(t_live.data(),0x238)=1;skg::field<uint64_t>(t_live.data(),0x140)=17;
    skg::field<editor::ResourceKey>(t_live.data(),8)={101,103,107};
    editor::Document identity;identity.project[0]=7;identity.epoch=11;identity.act_order={{100,2},{101,3}};
    for(size_t i=0;i<2;++i){editor::AuthoringSettings row;row.object={200+i,1};row.act_id=identity.act_order[i];identity.settings.emplace(row.object,row);}
    std::string error;editor::Document baseline,hit;auto before_reads=act_cache_reads;
    CHECK(act_cache_project(t_live.data(),baseline,error,identity)&&act_cache_reads==before_reads+2);
    const auto baseline_acts=live.acts;const auto baseline_goals=live.goals;const auto initial_copies=act_cache_copies;
    const auto initial_hits=act_projection_cache.stats().hits;before_reads=act_cache_reads;
    CHECK(act_cache_project(t_live.data(),hit,error,identity)&&hit==baseline&&act_cache_reads==before_reads);
    CHECK(act_projection_cache.stats().hits==initial_hits+1&&act_cache_copies==initial_copies+1);
    CHECK(hit.next_id>201&&hit.act_order==identity.act_order);
    const auto& first=std::get<editor::StructuredValue>(hit.settings.at({200,1}).properties.at(kActBlockProperty));
    CHECK((first.references==std::vector<editor::ObjectId>{{(2ull<<32)|7,1}}));
    // Opaque act bytes, nested goal fields and stable identities all invalidate.
    for(unsigned change=0;change<12;++change){
        act_projection_cache.clear();editor::Document original;CHECK(act_cache_project(t_live.data(),original,error,identity));
        auto changed_identity=identity;const auto source=skg::field<editor::ResourceKey>(t_live.data(),8);
        if(change==0)live.acts[0x500]^=1;
        if(change==1)skg::field<int32_t>(live.goals[0].data(),4)=99;
        if(change==2)skg::field<uint32_t>(live.goals[0].data(),0)=0;
        if(change==3){std::swap_ranges(live.acts.begin(),live.acts.begin()+0x534,live.acts.begin()+0x534);std::swap(live.goals[0],live.goals[1]);act_cache_bind(t_live.data(),live);}
        if(change==4)std::reverse(changed_identity.act_order.begin(),changed_identity.act_order.end());
        if(change==5){auto row=changed_identity.settings.extract({200,1});row.key()={900,1};row.mapped().object=row.key();changed_identity.settings.insert(std::move(row));}
        if(change==6)++changed_identity.project[0];if(change==7)++changed_identity.epoch;
        if(change==8)++skg::field<editor::ResourceKey>(t_live.data(),8).instance;
        if(change==9){const auto context=act_projection_cache.context();observed_data=nullptr;CHECK(state().ready&&act_projection_cache.context()!=context);}
        if(change==10){
            live.acts.resize(3*0x534);live.goals.resize(3);act_cache_bind(t_live.data(),live);
            changed_identity.act_order.push_back({102,4});editor::AuthoringSettings row;row.object={202,1};row.act_id=changed_identity.act_order.back();
            changed_identity.settings.emplace(row.object,row);
        }
        if(change==11){live.acts.resize(0x534);live.goals.resize(1);act_cache_bind(t_live.data(),live);changed_identity.act_order.resize(1);changed_identity.settings.erase({201,1});}
        editor::Document changed;before_reads=act_cache_reads;const auto hits=act_projection_cache.stats().hits;
        CHECK(act_cache_project(t_live.data(),changed,error,changed_identity)&&act_cache_reads==before_reads+changed_identity.act_order.size()&&act_projection_cache.stats().hits==hits);
        if(change==1){const auto& block=std::get<editor::StructuredValue>(changed.settings.at({200,1}).properties.at(kActBlockProperty));CHECK((block.references==std::vector<editor::ObjectId>{{(2ull<<32)|99,1}}));}
        if(change==2)CHECK(std::get<editor::StructuredValue>(changed.settings.at({200,1}).properties.at(kActBlockProperty)).references.empty());
        live.acts=baseline_acts;live.goals=baseline_goals;act_cache_bind(t_live.data(),live);skg::field<editor::ResourceKey>(t_live.data(),8)=source;
    }
    act_projection_cache.clear();CHECK(act_cache_project(t_live.data(),baseline,error,identity));
    value(t_live.data(),0)+=5;before_reads=act_cache_reads;const auto hits=act_projection_cache.stats().hits;
    CHECK(act_cache_project(t_live.data(),hit,error,identity)&&hit==baseline&&act_cache_reads==before_reads&&act_projection_cache.stats().hits==hits+1);
    // Captain and crew never come from the act cache, including server remaps.
    act_cache_resolved.instance+=1;before_reads=act_cache_reads;
    CHECK(act_cache_project(t_live.data(),hit,error,identity)&&act_cache_reads==before_reads);
    const auto current_captain=std::get<editor::ResourceKey>(hit.settings.at(kCaptainObject).properties.at(kCaptainAssetProperty));
    CHECK(current_captain==act_cache_resolved);
    CHECK(hit.settings.at(kCrewObject).properties.at(kCrewRosterProperty)!=baseline.settings.at(kCrewObject).properties.at(kCrewRosterProperty));
    skg::field<bool>(t_live.data(),0x175)=true;skg::field<uint32_t>(t_live.data(),0x238)=2;
    CHECK(act_cache_project(t_live.data(),hit,error,identity)&&std::get<bool>(hit.settings.at(kCaptainObject).properties.at(kCaptainLockedProperty)));
    CHECK(std::get<int64_t>(hit.settings.at(kCrewObject).properties.at(kCrewCapacityProperty))==2);
    act_cache_resolve_failure=true;CHECK(!act_cache_project(t_live.data(),hit,error,identity));act_cache_resolve_failure=false;
    skg::field<bool>(t_live.data(),0x175)=false;skg::field<uint32_t>(t_live.data(),0x238)=1;act_cache_resolved={17,19,23};
    for(unsigned fail=0;fail<2;++fail){
        if(!fail)act_cache_alloc_failures=1;else act_cache_fail_fingerprint_write=true;
        before_reads=act_cache_reads;const auto prior_hits=act_projection_cache.stats().hits;
        CHECK(act_cache_project(t_live.data(),hit,error,identity)&&hit==baseline&&act_cache_reads==before_reads+2&&act_projection_cache.stats().hits==prior_hits);
        act_cache_fail_fingerprint_write=false;
    }
    auto invalid=identity;invalid.settings.erase({200,1});before_reads=act_cache_reads;
    CHECK(!act_cache_project(t_live.data(),hit,error,invalid)&&act_cache_reads==before_reads);
    CHECK(t_allocations.empty()&&act_cache_rows.size()==1&&!t_restores&&pending.empty());
    act_projection_cache.clear();act_cache_rows.clear();a_alloc=old_alloc;a_copy_acts=old_copy;a_copy_crew=old_crew;a_write=old_write;a_read=old_read;H=old_host;reset();
}
void test_act_projection_cache_bounds(){
    editor_native_act_cache::Cache cache;editor_native_act_cache::Key key;key.context=cache.context();key.project[0]=5;key.epoch=7;
    key.source={11,13,17};key.identities={{{100,1},{200,1}},{{101,1},{201,1}}};key.schema=authoring_property_schema();key.bytes={1,2,3};
    editor_native_act_cache::Acts blocks(2);blocks[0].format=blocks[1].format=kActBlockFormat;blocks[0].bytes={4,5};blocks[1].bytes={6};blocks[0].references={{9,1}};
    cache.put(key,blocks);auto found=cache.get(key);CHECK(found&&*found==blocks&&cache.stats().entries==1);
    blocks[0].bytes[0]=99;CHECK((*found)[0].bytes[0]==4);
    for(unsigned change=0;change<8;++change){auto changed=key;
        if(change==0)++changed.context;if(change==1)++changed.project[0];if(change==2)++changed.epoch;if(change==3)++changed.source.instance;
        if(change==4)std::reverse(changed.identities.begin(),changed.identities.end());if(change==5)++changed.identities[0].settings.id;
        if(change==6)changed.schema.clear();if(change==7)changed.bytes[0]^=1;CHECK(!cache.get(changed));}
    const auto context=key.context;cache.clear();CHECK(cache.context()!=context&&!cache.get(key)&&!cache.stats().entries&&!cache.stats().bytes);
    cache.put(key,*found);CHECK(!cache.stats().entries);key.context=cache.context();
    for(unsigned i=0;i<12;++i){auto version=key;version.bytes.push_back(uint8_t(i));cache.put(std::move(version),*found);}
    CHECK(cache.stats().entries==8&&cache.stats().bytes<=32u<<20);
    auto oversized=key;oversized.bytes.resize((32u<<20)+1);const auto entries=cache.stats().entries;cache.put(std::move(oversized),*found);CHECK(cache.stats().entries==entries);
    CHECK((*found)[0].bytes[0]==4); // Eviction never invalidates an owned value.
}
} // namespace
