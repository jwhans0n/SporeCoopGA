#include "editor_native_scale.h"
#include "net/editor_protocol.h"
#include <bit>
#include <cstdlib>
#include <iostream>

using namespace editor;
using namespace editor_native;
static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)){std::cerr<<__LINE__<<": " #x "\n";std::exit(1);} } while(0)
static constexpr ObjectId object{3,1},other{4,1};
static constexpr Peer host{0,1},guest{1,1};
static constexpr FieldKey companion{Field::Property,kRelativeScaleProperty};
static State seed(bool tracked=true){
    State s;auto& d=s.document;d.project[0]=9;d.epoch=1;d.next_id=5;
    d.classes.insert({1,1});d.acts.insert({2,1});if(tracked)d.schema=relative_scale_schema();
    for(auto id:{object,other}){
        Placement p;p.object=id;p.class_id={1,1};p.act_id={2,1};p.scale=2;
        std::string e;CHECK(project_relative_scale(p,tracked,.5f,e));d.placements.emplace(id,p);
    }
    return s;
}
int main(){
    std::string error;float relative=0;bool tracked=false;
    auto initial=seed();CHECK(relative_scale_mode(initial.document,tracked,error));CHECK(tracked);
    for(float v:{0.f,.5f,.479587317f,1.f,-.25f,1.25f,std::numeric_limits<float>::lowest(),std::numeric_limits<float>::max()}){
        auto p=initial.document.placements.at(object);CHECK(project_relative_scale(p,true,v,error));
        CHECK(relative_scale_value(p,true,relative,error));CHECK(relative==v);CHECK(p.scale==2);
        auto d=initial.document;d.placements.at(object)=p;CHECK(valid(d,{},error));
    }
    for(float v:{std::numeric_limits<float>::infinity(),-std::numeric_limits<float>::infinity(),std::numeric_limits<float>::quiet_NaN()}){
        auto p=initial.document.placements.at(object);CHECK(!project_relative_scale(p,true,v,error));
        p.properties[kRelativeScaleProperty]=v;CHECK(!relative_scale_value(p,true,relative,error));
    }
    auto malformed=initial.document.placements.at(object);malformed.properties.clear();CHECK(!relative_scale_value(malformed,true,relative,error));
    malformed.properties[kRelativeScaleProperty]=true;CHECK(!relative_scale_value(malformed,true,relative,error));
    malformed.properties[kRelativeScaleProperty]=.5f;malformed.properties[17]=false;CHECK(!relative_scale_value(malformed,true,relative,error));
    auto schema=initial.document;schema.schema.begin()->second.optional=true;CHECK(!relative_scale_mode(schema,tracked,error));
    schema=initial.document;schema.schema[17]={ValueKind::Boolean};CHECK(!relative_scale_mode(schema,tracked,error));
    auto legacy=seed(false);CHECK(relative_scale_mode(legacy.document,tracked,error));CHECK(!tracked);
    CHECK(relative_scale_value(legacy.document.placements.at(object),false,relative,error));
    CHECK(!relative_scale_value(initial.document.placements.at(object),false,relative,error));
    CHECK(legacy_scale_blocked(6,false));CHECK(!legacy_scale_blocked(1,false));CHECK(!legacy_scale_blocked(11,false));CHECK(!legacy_scale_blocked(6,true));
    CHECK(clone_scale_blocked(6,true,true));CHECK(!clone_scale_blocked(6,false,true));
    CHECK(!clone_scale_blocked(6,true,false));CHECK(!clone_scale_blocked(1,true,true));CHECK(!clone_scale_blocked(11,true,true));

    Coordinator core;CHECK(core.reset(initial,1,error));CHECK(core.connect(host));CHECK(core.connect(guest));
    State replica=core.state();uint64_t seq=0;Time now=100;
    struct Native{float physical=2,relative=.5f;} native;
    Hooks hooks;
    hooks.validate=[](const Document&,const Document& after,const Change&,std::string& e){
        bool mode=false;if(!relative_scale_mode(after,mode,e))return false;
        for(const auto& [id,p]:after.placements){(void)id;float v=0;if(!relative_scale_value(p,mode,v,e))return false;}return true;
    };
    hooks.apply=[&](const Document&,const Document& after,const Change&,std::string& e){
        const auto& p=after.placements.at(object);native.physical=p.scale;return relative_scale_value(p,true,native.relative,e);
    };
    auto submit=[&](Intent intent,std::vector<Mutation> mutations,bool structure=false){
        auto lease=core.acquire(host,structure?std::vector<Scope>{{}}:std::vector<Scope>{{ScopeKind::Placement,object,{}}},now,1000);CHECK(lease.accepted());
        Request r;r.project=core.state().document.project;r.epoch=core.state().document.epoch;r.base_revision=core.state().document.revision;
        r.operation={host,++seq,r.epoch};r.intent=intent;r.summary="Native scale companion";r.mutations=std::move(mutations);
        for(const auto& l:lease.leases)r.leases.push_back({l.scope,l.token});
        auto decision=core.submit(r,now,hooks);CHECK(decision.accepted());CHECK(apply_committed(replica,decision,error));
        CHECK(replica==core.state());CHECK(core.release(host,r.leases));++now;return decision;
    };
    Mutation m;m.target=object;m.field=companion;m.value=.479587317f;
    auto decision=submit(Intent::Forward,{m});CHECK(core.state().document.revision==1);
    CHECK(native.physical==2);CHECK(native.relative==.479587317f);
    CHECK(std::bit_cast<uint32_t>(native.relative)==0x3EF58C78);
    CHECK(field_revision(core.state().document.placements.at(object),{Field::Scale,0})==0);
    CHECK(field_revision(core.state().document.placements.at(object),companion)==1);
    CHECK(decision.change->patch.fields.size()==1);CHECK(core.state().history.size()==1);
    submit(Intent::Undo,{});CHECK(native.relative==.5f);CHECK(native.physical==2);
    submit(Intent::Redo,{});CHECK(native.relative==.479587317f);CHECK(native.physical==2);
    std::vector<uint8_t> bytes;CHECK(wire::encode_state(core.state(),bytes,error));State loaded;CHECK(wire::decode_state(bytes,loaded,error));CHECK(loaded==core.state());
    CHECK(core.reset(loaded,2,error));CHECK(core.connect(host));CHECK(core.connect(guest));replica=core.state();
    submit(Intent::Undo,{});CHECK(native.relative==.5f);CHECK(native.physical==2);
    CHECK(core.state().document.schema==relative_scale_schema());
    m.expected=field_revision(core.state().document.placements.at(object),companion);m.value=.75f;
    Mutation physical;physical.target=object;physical.field={Field::Scale,0};physical.value=3.f;
    physical.expected=field_revision(core.state().document.placements.at(object),physical.field);
    submit(Intent::Forward,{physical,m});CHECK(native.physical==3);CHECK(native.relative==.75f);
    submit(Intent::Undo,{});CHECK(native.physical==2);CHECK(native.relative==.5f);
    m.expected=field_revision(core.state().document.placements.at(object),companion);m.value=.479587317f;
    submit(Intent::Forward,{m});CHECK(native.physical==2);CHECK(native.relative==.479587317f);
    Mutation clone;clone.kind=MutationKind::Clone;clone.target=object;clone.expected=core.state().document.placements.at(object).revision;
    auto cloned=submit(Intent::Forward,{clone},true);CHECK(cloned.change->allocated.size()==1);
    const auto clone_id=cloned.change->allocated.front();CHECK(core.state().document.placements.at(clone_id).properties.at(kRelativeScaleProperty)==Value{.479587317f});
    CHECK(core.state().document.placements.at(clone_id).scale==2);
    submit(Intent::Undo,{},true);CHECK(!core.state().document.placements.contains(clone_id));
    submit(Intent::Redo,{},true);CHECK(core.state().document.placements.at(clone_id).properties.at(kRelativeScaleProperty)==Value{.479587317f});
    Mutation erase;erase.kind=MutationKind::Delete;erase.target=clone_id;erase.expected=core.state().document.placements.at(clone_id).revision;
    submit(Intent::Forward,{erase},true);CHECK(!core.state().document.placements.contains(clone_id));
    submit(Intent::Undo,{},true);CHECK(core.state().document.placements.at(clone_id).properties.at(kRelativeScaleProperty)==Value{.479587317f});
    CHECK(wire::encode_state(legacy,bytes,error));CHECK(wire::decode_state(bytes,loaded,error));CHECK(loaded==legacy);
    CHECK(loaded.document.schema.empty());CHECK(loaded.document.placements.at(object).properties.empty());
    Coordinator old;CHECK(old.reset(legacy,1,error));CHECK(old.connect(host));
    auto old_lease=old.acquire(host,{{ScopeKind::Placement,object,{}}},now,1000);CHECK(old_lease.accepted());
    Request move;move.project=old.state().document.project;move.epoch=1;move.operation={host,1,1};move.summary="Legacy Move";
    Mutation position;position.target=object;position.field={Field::Position,0};position.value=Vec3{1,0,0};move.mutations={position};
    for(const auto& l:old_lease.leases)move.leases.push_back({l.scope,l.token});
    CHECK(old.submit(move,now).accepted());CHECK(old.state().history.size()==1);
    CHECK(wire::encode_state(old.state(),bytes,error));CHECK(wire::decode_state(bytes,loaded,error));CHECK(loaded==old.state());
    CHECK(old.reset(loaded,2,error));CHECK(old.state().history.size()==1);CHECK(old.state().document.schema.empty());
    CHECK(old.state().document.placements.at(object).properties.empty());
    std::cout<<checks<<" native scale projection/schema/transaction checks passed\n";
}
