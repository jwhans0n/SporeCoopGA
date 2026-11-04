// Portable crew/project settings, ordered terrain and staged durability.
#include "net/editor_protocol.h"
#include "net/editor_terrain.h"
#include "editor_terrain_edit.h"
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
static void wire_roundtrip(const State& state,uint8_t version=5){
    std::string error;std::vector<uint8_t> bytes;State decoded;
    CHECK(wire::encode_state(state,bytes,error));CHECK(bytes.size()>28 && bytes[24]==version);
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
static void native_terrain_revisions(){
    std::string error;Coordinator c;CHECK(c.reset(initial(),9,error));CHECK(c.connect(host));CHECK(c.connect(guest));
    CHECK(c.submit(request(c.state(),host,1,{append()}),100).accepted());
    const auto prior=terrain_recipe(c.state().document).front();CHECK(prior.revision==1);
    auto before=prior;before.revision=0;auto after=before;number(after.payload.bytes,16,20);
    Mutation legacy;legacy.kind=MutationKind::SetTerrain;legacy.target=prior.object;legacy.expected=prior.revision;legacy.terrain=after;
    auto bad=request(c.state(),host,2,{legacy});owned(c,bad,{{ScopeKind::Terrain,prior.object,{}}});
    CHECK(c.submit(bad,100).code==Code::Invalid);CHECK(c.state().document.revision==1);
    Mutation fixed;CHECK(editor_terrain_edit::replacement(prior,before,after,fixed,error));
    CHECK(fixed.expected==1&&fixed.terrain->revision==1&&fixed.terrain->object==prior.object&&fixed.terrain->order==prior.order);
    auto edit=request(c.state(),host,3,{fixed});owned(c,edit,{{ScopeKind::Terrain,prior.object,{}}});CHECK(c.submit(edit,100).accepted());
    CHECK(c.submit(request(c.state(),guest,1,{append(30)}),100).accepted());
    auto undo=request(c.state(),host,4,{},Intent::Undo);owned(c,undo,{{ScopeKind::Terrain,prior.object,{}}});CHECK(c.submit(undo,100).accepted());
    CHECK(c.state().document.terrain.size()==2&&c.state().document.terrain.at(prior.object).payload==prior.payload);
    auto redo=request(c.state(),host,5,{},Intent::Redo);owned(c,redo,{{ScopeKind::Terrain,prior.object,{}}});CHECK(c.submit(redo,100).accepted());
    CHECK(c.state().document.terrain.size()==2&&c.state().document.terrain.at(prior.object).payload==after.payload);
    for(unsigned variant=0;variant<6;++variant){
        auto first=before,next=after;
        if(variant==0)++first.object.generation;if(variant==1)++next.order;
        if(variant==2)next.kind=TerrainKind::Model;if(variant==3)number(first.payload.bytes,16,99);
        if(variant==4)next.payload.references={{7,1}};if(variant==5)number(next.payload.bytes,16,std::numeric_limits<float>::infinity());
        auto unchanged=fixed;CHECK(!editor_terrain_edit::replacement(prior,first,next,unchanged,error));CHECK(unchanged==fixed);
    }
    auto untrusted=after;untrusted.revision=UINT64_MAX;Mutation bounded;
    CHECK(editor_terrain_edit::replacement(prior,before,untrusted,bounded,error)&&bounded==fixed);
    wire_roundtrip(c.state());
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
static void authoring_hold_keeps_content_independent(){
    auto s=initial();auto& d=s.document;d.next_id=1000;d.acts.insert({2,1});d.act_order={{1,1},{2,1}};d.classes={{20,1}};
    const FieldKey source{Field::Property,kClassAssetResourceProperty},behavior{Field::Property,5},position{Field::Position,0};
    d.class_schema={{kClassAssetResourceProperty,{ValueKind::Resource,0,0,0,true,false}},{5,{ValueKind::Boolean,0,0,0,true,false}}};
    d.settings_schema={{42,{ValueKind::Boolean,0,0,0,true,false}}};
    for(unsigned i=1;i<=2;++i){AuthoringSettings row;row.object={10+i,1};row.act_id={i,1};row.properties[42]=true;d.settings[row.object]=row;}
    ClassActSettings cast;cast.object={30,1};cast.class_id={20,1};cast.act_id={1,1};cast.properties={{kClassAssetResourceProperty,ResourceKey{7,8,9}},{5,true}};d.class_settings[cast.object]=cast;
    Placement placed;placed.object={40,1};placed.class_id=cast.class_id;placed.act_id=cast.act_id;d.placements[placed.object]=placed;
    std::string error;Coordinator c;CHECK(c.reset(s,9,error));CHECK(c.connect(host));CHECK(c.connect(guest));const Peer observer{2,1};CHECK(c.connect(observer));
    const Scope content{ScopeKind::Content,cast.class_id,{}},authoring{ScopeKind::Authoring,{},{}};
    auto own_content=c.acquire(host,{content},100,30000);CHECK(own_content.accepted());
    auto hold=c.acquire(guest,{authoring},100,30000);CHECK(hold.accepted());
    CHECK(c.acquire(observer,{{ScopeKind::Placement,placed.object,{}}},100,30000).code==Code::Conflict);
    CHECK(c.acquire(observer,{{ScopeKind::Field,placed.object,position}},100,30000).code==Code::Conflict);
    CHECK(c.acquire(observer,{{ScopeKind::Field,cast.object,behavior}},100,30000).code==Code::Conflict);
    CHECK(c.acquire(observer,{{ScopeKind::Document,{},{}}},100,30000).code==Code::Conflict);
    CHECK(c.acquire(observer,{{ScopeKind::Authoring,{1,1},{}}},100,30000).code==Code::Invalid);
    auto fenced=c.submit(request(c.state(),observer,1,{append()}),100);CHECK(fenced.code==Code::Conflict);
    Mutation roster;roster.kind=MutationKind::SetActs;roster.act_roster=std::vector<AuthoringSettings>{d.settings.at({12,1}),d.settings.at({11,1})};
    auto q=request(c.state(),guest,1,{roster});q.leases={{authoring,hold.leases.front().token}};auto changed=c.submit(q,100);CHECK(changed.accepted());CHECK(apply_committed(s,changed,error));
    CHECK(s.document.act_order==std::vector<ObjectId>({{2,1},{1,1}}));
    Mutation replace;replace.target=cast.object;replace.field=source;replace.value=ResourceKey{70,8,9};
    q=request(c.state(),guest,2,{replace});q.leases={{authoring,hold.leases.front().token}};CHECK(!c.submit(q,100).accepted());
    CHECK(value(c.state().document,cast.object,source)==Value{ResourceKey{7,8,9}});
    q=request(c.state(),host,1,{replace});q.leases={{content,own_content.leases.front().token}};changed=c.submit(q,100);CHECK(changed.accepted());CHECK(apply_committed(s,changed,error));
    for(auto [intent,sequence]:{std::pair{Intent::Undo,3ull},std::pair{Intent::Redo,4ull}}){
        q=request(c.state(),guest,sequence,{},intent);q.leases={{authoring,hold.leases.front().token}};changed=c.submit(q,100);CHECK(changed.accepted());CHECK(apply_committed(s,changed,error));
        CHECK(value(c.state().document,cast.object,source)==Value{ResourceKey{70,8,9}});
    }
    replace.field=behavior;replace.value=false;q=request(c.state(),guest,5,{replace});q.leases={{authoring,hold.leases.front().token}};changed=c.submit(q,100);CHECK(changed.accepted());CHECK(apply_committed(s,changed,error));CHECK(s.document==c.state().document && s.history==c.state().history);
    CHECK(is_authoring_scope({ScopeKind::Placement,placed.object,{}}));CHECK(!is_authoring_scope(content));CHECK(!is_authoring_scope({ScopeKind::Document,{},{}}));CHECK(!is_authoring_scope({ScopeKind::Field,cast.object,source}));
    wire::LeaseRequest lease;lease.action=wire::LeaseAction::Acquire;lease.serial=1;lease.ttl=30000;lease.scopes={authoring};
    wire::Context ctx{s.document.project,s.document.epoch,s.document.revision};net::Writer encoded;CHECK(wire::write({ctx,lease},encoded,error));
    net::Reader input(encoded.data.data(),encoded.data.size());wire::Message decoded;CHECK(wire::read(input,decoded,guest,false,error));
    const auto& decoded_lease=std::get<wire::LeaseRequest>(decoded.payload);CHECK(decoded_lease.action==lease.action && decoded_lease.serial==lease.serial && decoded_lease.ttl==lease.ttl && decoded_lease.scopes==lease.scopes && decoded_lease.tokens==lease.tokens);
    CHECK(c.release(guest,{{authoring,hold.leases.front().token}}));
    CHECK(c.submit(request(c.state(),observer,2,{append()}),100).accepted());
}
static void class_references_authoring_settings(){
    auto s=initial();auto& d=s.document;d.next_id=1000;d.classes={{20,1}};
    const ObjectId captain{80,1};d.settings_schema={{42,{ValueKind::Boolean,0,0,0,true,false}}};
    AuthoringSettings row;row.object=captain;row.kind=SettingsKind::Captain;row.properties[42]=true;d.settings[captain]=row;
    d.class_schema={{50,{ValueKind::Structured,0,0,0,true,false}},{51,{ValueKind::Reference,0,0,0,true,false}}};
    ClassActSettings cast;cast.object={30,1};cast.class_id={20,1};cast.act_id={1,1};cast.properties={{50,StructuredValue{1,{1,2},{captain}}},{51,captain}};d.class_settings[cast.object]=cast;
    std::string error;CHECK(valid(d,Limits{},error));wire_roundtrip(s,3);
    auto missing=d;missing.settings.clear();CHECK(!valid(missing,Limits{},error));
    CHECK(error.size()<256 && error.find("owner=30:1 property=50 reference=80:1")!=std::string::npos);
    auto stale=d;std::get<StructuredValue>(stale.class_settings.at(cast.object).properties.at(50)).references={{captain.id,2}};CHECK(!valid(stale,Limits{},error));
    Coordinator c;CHECK(c.reset(s,9,error));CHECK(c.connect(host));
    Mutation m;m.target=cast.object;m.field={Field::Property,50};m.value=StructuredValue{1,{3,4},{captain}};
    auto q=request(c.state(),host,1,{m});owned(c,q,{{ScopeKind::Class,cast.class_id,{}}});auto changed=c.submit(q,100);CHECK(changed.accepted());CHECK(apply_committed(s,changed,error));CHECK(s==c.state());
    m.expected=1;m.value=StructuredValue{1,{3,4},{{999,1}}};q=request(c.state(),host,2,{m});owned(c,q,{{ScopeKind::Class,cast.class_id,{}}});CHECK(!c.submit(q,100).accepted());CHECK(c.state().document==s.document);
}
static Decision terrain_commit(Coordinator& coordinator,State& replica,Request operation){
    std::string error;std::vector<uint8_t> encoded;Request decoded;
    CHECK(wire::encode_request(operation,encoded,error));CHECK(encoded.size()<wire::kOperationBytes);
    CHECK(wire::decode_request(encoded,decoded,error));CHECK(decoded==operation);
    auto accepted=coordinator.submit(decoded,100);if(!accepted.accepted())std::cerr<<accepted.reason<<"\n";CHECK(accepted.accepted());
    Decision transferred;CHECK(wire::encode_decision(accepted,encoded,error));CHECK(encoded.size()<wire::kOperationBytes);
    CHECK(wire::decode_decision(encoded,transferred,error));CHECK(transferred==accepted);
    CHECK(apply_committed(replica,transferred,error));CHECK(replica==coordinator.state());return accepted;
}
static void large_terrain_stroke_and_recipe_replacement(){
    auto s=initial();const ObjectId planet{10,1};const FieldKey water{Field::Property,7};
    s.document.settings_schema={{7,{ValueKind::Number,0,1,0,true,false}}};
    AuthoringSettings settings;settings.kind=SettingsKind::Planet;settings.object=planet;settings.properties[7]=.1f;s.document.settings[planet]=settings;
    std::string error;Coordinator coordinator;CHECK(coordinator.reset(s,9,error));CHECK(coordinator.connect(host));CHECK(coordinator.connect(guest));
    std::vector<Mutation> stroke(100);for(size_t i=0;i<stroke.size();++i)stroke[i]=append(float(i));
    auto first=terrain_commit(coordinator,s,request(s,host,1,stroke));CHECK(first.change->allocated.size()==100 && s.history.size()==1);
    auto foreign=terrain_commit(coordinator,s,request(s,guest,1,{append(700),append(701)}));CHECK(foreign.change->allocated.size()==2);
    const auto original=terrain_recipe(s.document);std::vector<Mutation> replacement;
    for(const auto& row:original){Mutation remove;remove.kind=MutationKind::DeleteTerrain;remove.target=row.object;remove.expected=row.revision;replacement.push_back(remove);}
    for(unsigned i=0;i<300;++i)replacement.push_back(append(float(i+1000)));
    Mutation companion;companion.target=planet;companion.field=water;companion.value=.6f;replacement.push_back(companion);
    auto q=request(s,host,2,replacement);owned(coordinator,q,{{ScopeKind::Authoring,{},{}}});
    const auto tokens=q.leases;auto changed=terrain_commit(coordinator,s,q);CHECK(changed.change->allocated.size()==300 && s.document.terrain.size()==300 && s.history.size()==3);
    CHECK(coordinator.release(host,tokens));
    auto later=terrain_commit(coordinator,s,request(s,guest,2,{append(9000),append(9001)}));
    for(auto [intent,sequence,count]:{std::tuple{Intent::Undo,3ull,size_t(104)},std::tuple{Intent::Redo,4ull,size_t(302)}}){
        q=request(s,host,sequence,{},intent);owned(coordinator,q,{{ScopeKind::Authoring,{},{}}});terrain_commit(coordinator,s,q);
        CHECK(s.document.terrain.size()==count);for(auto id:later.change->allocated)CHECK(s.document.terrain.contains(id));
        CHECK(value(s.document,planet,water)==Value{intent==Intent::Undo?.1f:.6f});CHECK(coordinator.release(host,q.leases));
    }
    wire_roundtrip(s);Coordinator recovered;CHECK(recovered.reset(s,10,error));CHECK(recovered.state().history==s.history);
    // The larger envelope is reserved for native terrain recipes. Ordinary
    // placement/field operations and mixed structural gestures retain 64.
    std::vector<Mutation> fields(65,companion);q=request(s,host,5,fields);std::vector<uint8_t> bytes;
    CHECK(!wire::encode_request(q,bytes,error));CHECK(!coordinator.submit(q,100).accepted());
    std::vector<Mutation> mixed(65,append());mixed.back().kind=MutationKind::Create;CHECK(!mutation_count_valid(mixed,Limits{}));
    fields.assign(65,companion);fields.push_back(append());CHECK(!mutation_count_valid(fields,Limits{}));
    Limits small;small.terrain_records=3;small.mutations=2;std::vector<Mutation> overflow(7,append());CHECK(!mutation_count_valid(overflow,small));
    auto forged=*changed.change;forged.allocated.resize(301);CHECK(!allocation_count_valid(forged,Limits{.terrain_records=300}));
    forged=*changed.change;forged.patch.objects.push_back({});CHECK(!allocation_count_valid(forged,Limits{}));CHECK(!patch_count_valid(forged.patch,Limits{}));
    Change mixed_allocations;mixed_allocations.patch.terrain.resize(32);mixed_allocations.patch.classes.resize(32);mixed_allocations.allocated.resize(96);
    for(auto& row:mixed_allocations.patch.terrain)row.after=stamp();
    CHECK(patch_count_valid(mixed_allocations.patch,Limits{}) && allocation_count_valid(mixed_allocations,Limits{}));
    mixed_allocations.allocated.push_back({});CHECK(!allocation_count_valid(mixed_allocations,Limits{}));
    overflow.assign(2*Limits{}.terrain_records+1,append());q=request(s,host,6,std::move(overflow));CHECK(!wire::encode_request(q,bytes,error));
    auto huge=append();huge.terrain->payload.bytes.resize(Limits{}.structured_bytes,1);q=request(s,host,7,std::vector<Mutation>(33,huge));CHECK(!wire::encode_request(q,bytes,error));
}
int main(){native_terrain_revisions();terrain_concurrency();terrain_negatives();mixed_allocation_replay();singleton_and_assets();staged_durability();presence();authoring_hold_keeps_content_independent();class_references_authoring_settings();large_terrain_stroke_and_recipe_replacement();std::cout<<checks<<" authoring extension checks passed\n";}
