// Portable crew/project settings, ordered terrain and staged durability.
#include "net/editor_protocol.h"
#include "net/editor_terrain.h"
#include <cstdlib>
#include <cstring>
#include <iostream>
#include <limits>
using namespace editor;
static unsigned checks;
#define CHECK(...) do{++checks;if(!(__VA_ARGS__)){std::cerr<<__LINE__<<": " #__VA_ARGS__ "\n";std::exit(1);}}while(0)
static constexpr Peer host{0,1},guest{1,1};
static State initial(){State s;s.document.project[0]=41;s.document.epoch=9;s.document.next_id=100;s.document.acts.insert({1,1});return s;}
static void word(std::vector<uint8_t>& bytes,size_t at,uint32_t value){for(unsigned i=0;i<4;++i)bytes[at+i]=uint8_t(value>>(8*i));}
static void number(std::vector<uint8_t>& bytes,size_t at,float value){uint32_t bits;std::memcpy(&bits,&value,4);word(bytes,at,bits);}
static TerrainRecord stamp(float x=0,TerrainKind kind=TerrainKind::Effect){
    TerrainRecord row;row.kind=kind;row.payload.format=kTerrainRecordFormat;
    auto& b=row.payload.bytes;b.resize(kind==TerrainKind::Effect?kTerrainEffectBytes:kTerrainModelBytes);
    word(b,0,123);word(b,4,456);b[12]=7;number(b,16,x);number(b,28,1);
    for(size_t at:{size_t(32),size_t(48),size_t(64)})number(b,at,1);
    if(kind==TerrainKind::Effect){number(b,68,1);word(b,72,0xFFFFFFFF);number(b,76,.5f);number(b,80,0);}
    return row;
}
static Request request(const State& state,Peer author,uint64_t sequence,std::vector<Mutation> mutations={},Intent intent=Intent::Forward){
    Request q;q.project=state.document.project;q.epoch=state.document.epoch;q.base_revision=state.document.revision;
    q.operation={author,sequence,q.epoch};q.summary="Checked authoring";q.mutations=std::move(mutations);q.intent=intent;return q;
}
static Mutation append(float x=0){Mutation m;m.kind=MutationKind::AppendTerrain;m.terrain=stamp(x);return m;}
static void owned(Coordinator& c,Request& q,const std::vector<Scope>& scopes){
    auto l=c.acquire(q.operation.author,scopes,100,30000);CHECK(l.accepted());
    for(const auto& lease:l.leases)q.leases.push_back({lease.scope,lease.token});
}
static void wire_roundtrip(const State& state){
    std::string error;std::vector<uint8_t> bytes;State decoded;
    CHECK(wire::encode_state(state,bytes,error));CHECK(bytes.size()>28 && bytes[24]==5);
    CHECK(wire::decode_state(bytes,decoded,error));CHECK(decoded==state);
    auto prior=decoded;bytes.pop_back();CHECK(!wire::decode_state(bytes,decoded,error));CHECK(decoded==prior);
}
static void terrain_concurrency(){
    std::string error;State replica=initial();Coordinator c;CHECK(c.reset(replica,9,error));CHECK(c.connect(host));CHECK(c.connect(guest));
    auto h=request(replica,host,1,{append(1),append(2),append(3)});
    auto g=request(replica,guest,1,{append(4),append(5)}); // Same base revision, no global lease.
    auto first=c.submit(h,100);CHECK(first.accepted());CHECK(first.change->allocated.size()==3);CHECK(apply_committed(replica,first,error));
    auto second=c.submit(g,100);CHECK(second.accepted());CHECK(apply_committed(replica,second,error));CHECK(replica==c.state());
    auto recipe=terrain_recipe(c.state().document);CHECK(recipe.size()==5);for(size_t i=0;i<5;++i)CHECK(recipe[i].order==i+1);
    CHECK(c.state().history.size()==2);wire_roundtrip(c.state());
    auto undo=request(c.state(),host,2,{},Intent::Undo);std::vector<Scope> scopes;
    for(auto id:first.change->allocated)scopes.push_back({ScopeKind::Terrain,id,{}});owned(c,undo,scopes);
    auto removed=c.submit(undo,100);CHECK(removed.accepted());CHECK(apply_committed(replica,removed,error));CHECK(replica==c.state());
    recipe=terrain_recipe(c.state().document);CHECK(recipe.size()==2 && recipe[0].order==4 && recipe[1].order==5);
    CHECK(c.acquire(guest,{{ScopeKind::Placement,first.change->allocated.front(),{}}},100,30000).code==Code::Invalid);
    auto redo=request(c.state(),host,3,{},Intent::Redo);owned(c,redo,scopes);
    auto restored=c.submit(redo,100);CHECK(restored.accepted());CHECK(apply_committed(replica,restored,error));CHECK(replica==c.state());
    recipe=terrain_recipe(c.state().document);CHECK(recipe.size()==5);for(size_t i=0;i<5;++i)CHECK(recipe[i].order==i+1);
    auto prior=recipe.front();Mutation set;set.kind=MutationKind::SetTerrain;set.target=prior.object;set.expected=prior.revision;set.terrain=prior;number(set.terrain->payload.bytes,16,20);
    auto edit=request(c.state(),host,4,{set});owned(c,edit,{{ScopeKind::Terrain,prior.object,{}}});auto changed=c.submit(edit,100);CHECK(changed.accepted());CHECK(apply_committed(replica,changed,error));
    auto stale=request(c.state(),host,5,{set});owned(c,stale,{{ScopeKind::Terrain,prior.object,{}}});CHECK(c.submit(stale,100).code==Code::Conflict);
    auto inverse=request(c.state(),host,6,{},Intent::Undo);owned(c,inverse,{{ScopeKind::Terrain,prior.object,{}}});CHECK(c.submit(inverse,100).accepted());
    auto original=request(c.state(),host,7,{},Intent::Undo);owned(c,original,scopes);CHECK(c.submit(original,100).accepted()); // Refreshes own causal terrain preconditions.
    CHECK(c.state().document.terrain.size()==2);wire_roundtrip(c.state());
}
static void terrain_negatives(){
    std::string error;Coordinator c;CHECK(c.reset(initial(),9,error));CHECK(c.connect(host));CHECK(c.connect(guest));
    uint64_t sequence=0;
    for(unsigned variant=0;variant<7;++variant){
        auto m=append();
        if(variant==0)m.terrain->object={99,1};
        if(variant==1)m.terrain->order=3;
        if(variant==2)m.terrain->payload.references={{1,1}};
        if(variant==3)m.terrain->payload.bytes.pop_back();
        if(variant==4)number(m.terrain->payload.bytes,16,std::numeric_limits<float>::infinity());
        if(variant==5)m.terrain->payload.bytes[12]=8;
        if(variant==6)m.terrain->payload.format=9;
        CHECK(!c.submit(request(c.state(),host,++sequence,{m}),100).accepted());CHECK(c.state().document.revision==0);
    }
    auto l=c.acquire(host,{{}},100,30000);CHECK(l.accepted());
    CHECK(c.submit(request(c.state(),guest,1,{append()}),100).code==Code::Conflict);
    CHECK(c.release(host,{{l.leases[0].scope,l.leases[0].token}}));
    Limits bound;bound.terrain_records=1;Coordinator limited(bound);CHECK(limited.reset(initial(),9,error));CHECK(limited.connect(host));
    CHECK(!limited.submit(request(limited.state(),host,1,{append(),append()}),100).accepted());CHECK(limited.state().document.terrain.empty());
    auto accepted=c.submit(request(c.state(),host,++sequence,{append()}),100);CHECK(accepted.accepted());
    std::vector<uint8_t> bytes;Request decoded;auto q=request(c.state(),guest,2,{append(8)});CHECK(wire::encode_request(q,bytes,error));CHECK(wire::decode_request(bytes,decoded,error));CHECK(decoded==q);
    auto forged=*accepted.change;forged.patch.terrain[0].after->order=999;Decision bad=accepted;bad.change=forged;auto replica=initial();CHECK(!apply_committed(replica,bad,error));CHECK(replica.document.revision==0);
}
static void mixed_allocation_replay(){
    std::string error;auto s=initial();s.document.classes={{2,1}};Coordinator c;CHECK(c.reset(s,9,error));CHECK(c.connect(host));
    Mutation placed;placed.kind=MutationKind::Create;placed.initial.class_id={2,1};placed.initial.act_id={1,1};
    auto q=request(c.state(),host,1,{append(1),placed,append(2),placed});owned(c,q,{{}});
    auto decision=c.submit(q,100);CHECK(decision.accepted());CHECK(decision.change->allocated==std::vector<ObjectId>({{100,1},{101,1},{102,1},{103,1}}));
    CHECK(apply_committed(s,decision,error));CHECK(s==c.state());wire_roundtrip(s);
}
static void singleton_and_assets(){
    auto s=initial();auto& d=s.document;d.next_id=1000;d.classes={{2,1}};d.next_class=1;
    const FieldKey crew{Field::Property,1},complexity{Field::Property,2},water{Field::Property,3},source{Field::Property,kClassAssetResourceProperty},behavior{Field::Property,5};
    d.settings_schema={{1,{ValueKind::Integer,0,3,0,true,false}},{2,{ValueKind::Boolean,0,0,0,true,false}},{3,{ValueKind::Number,0,1,0,true,false}}};
    for(auto [kind,id,key,v]:{std::tuple{SettingsKind::Crew,ObjectId{10,1},1,Value{int64_t(3)}},std::tuple{SettingsKind::Project,ObjectId{11,1},2,Value{false}},std::tuple{SettingsKind::Planet,ObjectId{12,1},3,Value{.5f}}}){
        AuthoringSettings row;row.object=id;row.kind=kind;row.properties[key]=v;d.settings[id]=row;
    }
    d.class_schema={{kClassAssetResourceProperty,{ValueKind::Resource,0,0,0,true,false}},{5,{ValueKind::Boolean,0,0,0,true,false}}};
    ClassActSettings cast;cast.object={20,1};cast.class_id={2,1};cast.act_id={1,1};cast.properties={{kClassAssetResourceProperty,ResourceKey{7,8,9}},{5,true}};d.class_settings[cast.object]=cast;
    std::string error;Coordinator c;CHECK(c.reset(s,9,error));CHECK(c.connect(host));CHECK(c.connect(guest));State replica=s;
    Mutation m;m.target={10,1};m.field=crew;m.value=int64_t(2);auto q=request(c.state(),guest,1,{m});owned(c,q,{{ScopeKind::Settings,m.target,{}}});auto a=c.submit(q,100);CHECK(a.accepted());CHECK(apply_committed(replica,a,error));
    m.target={11,1};m.field=complexity;m.value=true;q=request(c.state(),host,1,{m});owned(c,q,{{ScopeKind::Field,m.target,complexity}});a=c.submit(q,100);CHECK(a.accepted());CHECK(apply_committed(replica,a,error));
    m.target={12,1};m.field=water;m.value=.7f;q=request(c.state(),guest,2,{m});owned(c,q,{{ScopeKind::Field,m.target,water}});a=c.submit(q,100);CHECK(a.accepted());CHECK(apply_committed(replica,a,error));
    m.target=cast.object;m.field=source;m.value=ResourceKey{70,8,9};q=request(c.state(),host,2,{m});owned(c,q,{{ScopeKind::Content,cast.class_id,{}}});a=c.submit(q,100);CHECK(a.accepted());CHECK(apply_committed(replica,a,error));
    CHECK(value(c.state().document,cast.object,behavior)==Value{true});
    m.field=behavior;m.value=false;q=request(c.state(),guest,3,{m});owned(c,q,{{ScopeKind::Class,cast.class_id,{}}});a=c.submit(q,100);CHECK(a.accepted());CHECK(apply_committed(replica,a,error));
    CHECK(c.acquire(guest,{{ScopeKind::Content,cast.class_id,{}}},100,30000).code==Code::Conflict);
    auto undo=request(c.state(),host,3,{},Intent::Undo);owned(c,undo,{{ScopeKind::Content,cast.class_id,{}}});a=c.submit(undo,100);CHECK(a.accepted());CHECK(apply_committed(replica,a,error));
    CHECK(value(c.state().document,cast.object,source)==Value{ResourceKey{7,8,9}});CHECK(value(c.state().document,cast.object,behavior)==Value{false});CHECK(replica==c.state());wire_roundtrip(c.state());
}
static void staged_durability(){
    std::string error;Coordinator c;auto s=initial();CHECK(c.reset(s,9,error));CHECK(c.connect(host));CHECK(c.connect(guest));
    unsigned applies=0,rollbacks=0,persists=0;Hooks hooks;
    hooks.apply=[&](const auto&,const auto&,const auto&,auto&){++applies;return true;};
    hooks.rollback=[&](const auto&,const auto&,const auto&,auto&){++rollbacks;return true;};
    hooks.persist=[&](const auto&,const auto&,auto&){++persists;return true;};
    auto q=request(c.state(),host,1,{append()});Decision candidate;
    CHECK(c.prepare(q,100,hooks,candidate,error));CHECK(candidate.accepted());CHECK(c.prepared_state() && c.prepared_change());
    CHECK(c.state()==s && c.applying() && applies==1 && persists==0);CHECK(c.prepared_state()->document.revision==1);
    CHECK(c.acquire(guest,{{}},100,30000).code==Code::Busy);CHECK(!c.release(host,{}));CHECK(!c.reset(s,10,error));
    CHECK(c.submit(request(c.state(),guest,1,{append()}),100).code==Code::Busy);
    auto accepted=c.finish_prepared(true);CHECK(accepted==candidate);CHECK(c.state().document.revision==1 && !c.applying());CHECK(rollbacks==0 && persists==0);
    CHECK(apply_committed(s,accepted,error));CHECK(s==c.state());
    CHECK(c.prepare(q,100,hooks,candidate,error));CHECK(!c.prepared_state() && applies==1); // Retry has no native or worker work.
    q=request(c.state(),host,2,{append()});CHECK(c.prepare(q,100,hooks,candidate,error));auto rejected=c.finish_prepared(false,false,"disk refused");
    CHECK(rejected.code==Code::PersistenceFailed && rollbacks==1 && c.state().document.revision==1 && !c.frozen());
    q=request(c.state(),host,3,{append()});CHECK(c.prepare(q,100,hooks,candidate,error));rejected=c.finish_prepared(false,true,"uncertain flush");
    CHECK(rejected.code==Code::Frozen && rollbacks==2 && c.frozen() && c.state().document.revision==1);
    Coordinator failed;CHECK(failed.reset(initial(),9,error));CHECK(failed.connect(host));
    hooks.rollback=[](const auto&,const auto&,const auto&,auto&){return false;};
    q=request(failed.state(),host,1,{append()});CHECK(failed.prepare(q,100,hooks,candidate,error));
    CHECK(failed.finish_prepared(false).code==Code::Frozen && failed.frozen());CHECK(!failed.prepared_state());
    Coordinator invalid;CHECK(invalid.reset(initial(),9,error));CHECK(invalid.connect(host));
    hooks.validate=[](const auto&,const auto&,const auto&,auto& why){why="native rejected";return false;};
    q=request(invalid.state(),host,1,{append()});CHECK(!invalid.prepare(q,100,hooks,candidate,error));
    CHECK(candidate.code==Code::NativeRejected && !invalid.prepared_state() && !invalid.frozen());
}
static void presence(){
    wire::Presence p;p.author=guest;p.creation_serial=45;p.creating_asset=true;p.creation_target={20,1};p.scene_revision=3;p.scene_ready=false;
    wire::Context ctx;ctx.project[0]=9;ctx.epoch=7;ctx.revision=4;net::Writer encoded;std::string error;
    CHECK(wire::write({ctx,p},encoded,error));net::Reader r(encoded.data.data(),encoded.data.size());wire::Message decoded;
    CHECK(wire::read(r,decoded,guest,false,error));CHECK(std::get<wire::Presence>(decoded.payload)==p);
    p.scene_revision=5;CHECK(!wire::write({ctx,p},encoded,error));p.scene_revision=3;p.creation_serial=0;CHECK(!wire::write({ctx,p},encoded,error));
}
int main(){terrain_concurrency();terrain_negatives();mixed_allocation_replay();singleton_and_assets();staged_durability();presence();std::cout<<checks<<" authoring extension checks passed\n";}
