// Portable cast transactions and placement companion values. Native blocks are
// controlled data here; actual game parsing is covered by the native lab.
#include "editor_native_cast.h"
#include "editor_native_scale.h"
#include "editor_native_authoring.h"
#include "net/editor_protocol.h"
#include <cstdlib>
#include <array>
#include <iostream>
using namespace editor;
static unsigned checks;
#define CHECK(...) do {++checks;if(!(__VA_ARGS__)){std::cerr<<__LINE__<<": " #__VA_ARGS__ "\n";std::exit(1);}}while(0)
static constexpr Peer host{0,1},guest{1,1};
static constexpr ObjectId act{3ull<<32,1},cast{2ull<<32,1},settings{5ull<<32,1};
static constexpr FieldKey block{Field::Property,editor_native::kCastBlockProperty};
static State seed(){State s;auto& d=s.document;d.project[0]=9;d.epoch=7;d.next_id=6ull<<32;d.acts.insert(act);d.class_schema=editor_native::cast_property_schema();return s;}
static ClassActSettings initial(){ClassActSettings c;c.object=settings;c.class_id=cast;c.act_id=act;c.properties[block.property]=StructuredValue{editor_native::kCastBlockFormat,{1,2,3},{}};c.properties[kClassAssetResourceProperty]=ResourceKey{1,2,3};return c;}
static void unrelated_asset_ownership(){
    auto s=seed();auto& d=s.document;d.next_class=2;
    const ObjectId b{cast.id|1,1},bs{settings.id|1,1};
    auto a=initial(),other=initial();other.class_id=b;other.object=bs;
    d.classes={cast,b};d.class_settings={{settings,a},{bs,other}};
    Placement p;p.object={30,1};p.class_id=b;p.act_id=act;d.placements[p.object]=p;
    Coordinator core;std::string error;CHECK(core.reset(s,7,error));CHECK(core.connect(host));CHECK(core.connect(guest));
    const auto held=core.acquire(host,{{ScopeKind::Class,cast,{}}},0,30000);CHECK(held.accepted());
    const auto guest_owned=core.acquire(guest,{{ScopeKind::Structure,{},{}},{ScopeKind::Class,b,{}},{ScopeKind::Content,b,{}}},0,30000);CHECK(guest_owned.accepted());
    CHECK(core.acquire(guest,{{ScopeKind::Class,cast,{}}},0,30000).code==Code::Conflict);
    CHECK(core.acquire(guest,{{ScopeKind::Document,{},{}}},0,30000).code==Code::Conflict);
    uint64_t sequence=0;
    auto submit=[&](std::vector<Mutation> mutations,Intent intent=Intent::Forward){
        Request r;r.project=d.project;r.epoch=7;r.operation={guest,++sequence,7};r.base_revision=core.state().document.revision;r.intent=intent;r.mutations=std::move(mutations);r.summary="Unrelated assets";
        for(const auto& l:guest_owned.leases)r.leases.push_back({l.scope,l.token});return core.submit(r,0);
    };
    auto third=initial();third.class_id.id|=2;third.object.id|=2;
    Mutation add;add.kind=MutationKind::CreateClass;add.class_initial=third;
    CHECK(submit({add}).accepted()); // Host keeps A's inspector open during import.
    Mutation edit;edit.target=bs;edit.field=block;edit.value=StructuredValue{editor_native::kCastBlockFormat,{9,8,7},{}};
    CHECK(submit({edit}).accepted());
    Mutation remove;remove.kind=MutationKind::DeleteClass;remove.target=b;remove.expected=2;
    Mutation erase;erase.kind=MutationKind::Delete;erase.target=p.object;
    CHECK(submit({remove,erase}).accepted());
    CHECK(!core.state().document.classes.contains(b)&&core.state().document.classes.contains(cast));
    CHECK(core.acquire(guest,{{ScopeKind::Placement,p.object,{}}},0,30000).accepted()); // Retained B does not overlap A.
    CHECK(submit({},Intent::Undo).accepted());
    CHECK(core.state().document.classes.contains(b)&&core.state().document.placements.contains(p.object));
    CHECK(submit({},Intent::Redo).accepted());
    CHECK(core.state().document.class_settings.at(settings)==a);
    auto forged=add;forged.class_initial=initial();CHECK(!submit({forged}).accepted());
}
static void referenced_import_undo(){
    std::string error;
    for(bool objective:{false,true}){
        auto state=seed();Coordinator core;CHECK(core.reset(state,7,error));CHECK(core.connect(host));CHECK(core.connect(guest));
        auto request=[&](Peer author,uint64_t sequence,Intent intent,std::vector<Mutation> mutations){
            auto held=core.acquire(author,{{}},0,30000);CHECK(held.accepted());
            Request r;r.project=state.document.project;r.epoch=core.state().document.epoch;r.operation={author,sequence,r.epoch};r.base_revision=core.state().document.revision;r.intent=intent;r.summary="Reference safety";r.mutations=std::move(mutations);
            for(const auto& l:held.leases)r.leases.push_back({l.scope,l.token});
            auto result=core.submit(r,0);CHECK(core.release(author,r.leases));return result;
        };
        Mutation create;create.kind=MutationKind::CreateClass;create.class_initial=initial();
        CHECK(request(host,1,Intent::Forward,{create}).accepted());
        auto linked=core.state();
        if(objective)linked.document.external_references.push_back({{(3ull<<32)|0x1000,1},cast});
        else{
            auto other=initial();other.class_id.id|=1;other.object.id|=1;
            auto& data=std::get<StructuredValue>(other.properties.at(block.property));data.references={cast};
            create.class_initial=other;CHECK(request(guest,1,Intent::Forward,{create}).accepted());linked=core.state();
        }
        linked.document.epoch=8;
        CHECK(core.reset(linked,8,error));CHECK(core.connect(host));CHECK(core.connect(guest));
        CHECK(request(host,2,Intent::Undo,{}).code==Code::Referenced);
        CHECK(core.state().document==linked.document&&core.state().history==linked.history);
    }
    std::set<ObjectId> refs;
    for(auto [type,value]:{std::pair{0,4},std::pair{1,4},std::pair{3,4},std::pair{2,-1},std::pair{2,1024},std::pair{2,1024}})
        editor_native::behavior_class_reference(refs,type,value);
    CHECK(refs==std::set<ObjectId>{{(2ull<<32)|1024,1}});
}
static void active_behavior_reference_metadata(){
    // Adventure Town class14 keeps target1109 in all five standard pickup
    // slots while custom behavior is enabled. Preserve those authored bytes;
    // only references from the active native behavior may constrain editing.
    auto state=seed();auto& d=state.document;d.classes.insert(cast);d.class_settings.emplace(settings,initial());
    auto& subtree=std::get<StructuredValue>(d.class_settings.at(settings).properties.at(block.property));
    subtree.bytes={0x55,0x04,0,0,1,3,1,3};const auto authored=subtree.bytes;
    std::set<ObjectId> refs;std::string error;
    editor_native::standard_behavior_class_references(refs,true,3,3,5,1109,1109,1109);
    subtree.references.assign(refs.begin(),refs.end());CHECK(refs.empty());CHECK(valid(d,{},error));CHECK(subtree.bytes==authored);
    editor_native::standard_behavior_class_references(refs,false,1,1,3,1109,1109,1109);
    CHECK(refs.empty());
    for(unsigned selected=0;selected<3;++selected){
        refs.clear();editor_native::standard_behavior_class_references(refs,false,selected==0?3:1,selected==1?3:1,selected==2?5:3,1109,1109,1109);
        CHECK(refs==std::set<ObjectId>{{(2ull<<32)|1109,1}});
        subtree.references.assign(refs.begin(),refs.end());CHECK(!valid(d,{},error));CHECK(subtree.bytes==authored);
    }
    refs.clear();editor_native::standard_behavior_class_references(refs,true,3,3,5,1109,1109,1109);
    editor_native::behavior_class_reference(refs,2,1109);subtree.references.assign(refs.begin(),refs.end());
    CHECK(!valid(d,{},error)); // An active custom rule still cannot dangle.
    d.classes.insert({(2ull<<32)|1109,1});CHECK(valid(d,{},error));CHECK(subtree.bytes==authored);
}
static void independent_act_and_captain_settings(){
    using namespace editor_native;
    auto baseline=seed();auto& d=baseline.document;d.next_id=9ull<<32;d.classes.insert(cast);
    const ObjectId second_act{(3ull<<32)|1,1};d.acts.insert(second_act);d.settings_schema=authoring_property_schema();
    const FieldKey act_field{Field::Property,kActBlockProperty},locked{Field::Property,kCaptainLockedProperty};
    for(unsigned index=0;index<2;++index){
        AuthoringSettings s;s.object=act_settings_id(index);s.act_id={(3ull<<32)|index,1};
        s.properties[kActBlockProperty]=StructuredValue{kActBlockFormat,{uint8_t(index+1)}, {}};d.settings.emplace(s.object,s);
    }
    AuthoringSettings captain;captain.object=kCaptainObject;captain.kind=SettingsKind::Captain;
    captain.properties={{kCaptainLockedProperty,false},{kCaptainPositionProperty,Vec3{}},{kCaptainRotationProperty,Rotation{}},
        {kCaptainAssetProperty,ResourceKey{1,2,3}}};d.settings.emplace(captain.object,captain);
    std::string error;CHECK(valid(d,{},error));Coordinator core;CHECK(core.reset(baseline,7,error));
    CHECK(core.connect(host));CHECK(core.connect(guest));State replica=baseline;
    auto a=core.acquire(host,{{ScopeKind::Settings,act_settings_id(0),{}}},100,30000);CHECK(a.accepted());
    auto b=core.acquire(guest,{{ScopeKind::Settings,act_settings_id(1),{}}},100,30000);CHECK(b.accepted());
    CHECK(core.acquire(guest,{{ScopeKind::Settings,act_settings_id(0),{}}},100,30000).code==Code::Conflict);
    auto c=core.acquire(guest,{{ScopeKind::Settings,kCaptainObject,{}}},100,30000);CHECK(c.accepted());
    CHECK(core.acquire(host,{{ScopeKind::Settings,kCaptainObject,{}}},100,30000).code==Code::Conflict);
    CHECK(core.acquire(host,{{ScopeKind::Class,cast,{}}},100,30000).accepted());
    uint64_t sequence=0;
    auto submit=[&](Peer who,Intent intent,std::vector<Mutation> mutations){
        Request r;r.project=d.project;r.epoch=7;r.base_revision=core.state().document.revision;
        r.operation={who,++sequence,7};r.intent=intent;r.summary="Authoring settings";r.mutations=std::move(mutations);
        for(const auto& lease:core.leases())if(lease.owner==who)r.leases.push_back({lease.scope,lease.token});
        auto decision=core.submit(r,100);
        if(decision.accepted()){
            CHECK(apply_committed(replica,decision,error));CHECK(replica==core.state());
            std::vector<uint8_t> bytes;State decoded;CHECK(wire::encode_state(core.state(),bytes,error));
            CHECK(bytes.size()>28 && bytes[24]==3);CHECK(wire::decode_state(bytes,decoded,error));CHECK(decoded==core.state());
        }
        return decision;
    };
    Mutation first;first.target=act_settings_id(0);first.field=act_field;first.value=StructuredValue{kActBlockFormat,{9},{cast}};
    CHECK(submit(host,Intent::Forward,{first}).accepted());
    Mutation second;second.target=act_settings_id(1);second.field=act_field;second.value=StructuredValue{kActBlockFormat,{8},{}};
    CHECK(submit(guest,Intent::Forward,{second}).accepted());
    Mutation change_captain;change_captain.target=kCaptainObject;change_captain.field=locked;change_captain.value=true;
    Mutation move=change_captain;move.field={Field::Property,kCaptainPositionProperty};move.value=Vec3{3,4,5};
    auto moved=submit(guest,Intent::Forward,{change_captain,move});CHECK(moved.accepted() && moved.change->patch.fields.size()==2);
    auto shared=core.state();CHECK(submit(host,Intent::Undo,{}).accepted());
    CHECK(value(core.state().document,act_settings_id(1),act_field)==second.value);
    CHECK(value(core.state().document,kCaptainObject,locked)==Value{true});
    CHECK(submit(host,Intent::Redo,{}).accepted());
    CHECK(submit(guest,Intent::Undo,{}).accepted());
    CHECK(value(core.state().document,kCaptainObject,locked)==Value{false});
    CHECK(value(core.state().document,act_settings_id(0),act_field)==first.value);
    change_captain.expected=field_revision(core.state().document,kCaptainObject,locked);change_captain.value=1.f;
    CHECK(submit(guest,Intent::Forward,{change_captain}).code==Code::Unsupported);
    // A goal edited on another act must still protect the referenced class.
    auto referenced=core.state();referenced.document.epoch=8;
    referenced.document.class_settings[settings]=initial();referenced.document.next_class=1;
    CHECK(core.reset(referenced,8,error));CHECK(core.connect(host));
    auto structure=core.acquire(host,{{ScopeKind::Structure,{},{}},{ScopeKind::Class,cast,{}},{ScopeKind::Content,cast,{}}},100,30000);CHECK(structure.accepted());
    Request removal;removal.project=d.project;removal.epoch=8;removal.operation={host,1,8};removal.base_revision=core.state().document.revision;removal.summary="Remove referenced cast";
    Mutation remove;remove.kind=MutationKind::DeleteClass;remove.target=cast;removal.mutations={remove};
    for(const auto& lease:structure.leases)removal.leases.push_back({lease.scope,lease.token});
    CHECK(core.submit(removal,100).code==Code::Referenced);
    CHECK(core.state().document.classes.contains(cast));
    (void)shared;
}
static void ordered_act_roster(){
    using namespace editor_native;
    auto baseline=seed();auto& d=baseline.document;d.next_id=9ull<<32;d.classes.insert(cast);d.class_settings[settings]=initial();
    d.settings_schema=authoring_property_schema();const ObjectId b{act.id|1,1};d.acts.insert(b);d.act_order={act,b};
    for(unsigned i=0;i<2;++i){AuthoringSettings row;row.object=act_settings_id(i);row.act_id=d.act_order[i];row.properties[kActBlockProperty]=StructuredValue{kActBlockFormat,{uint8_t(i+1)},{}};d.settings[row.object]=row;}
    Placement p;p.object={20,1};p.class_id=cast;p.act_id=act;d.placements[p.object]=p;
    Coordinator core;std::string error;CHECK(core.reset(baseline,7,error));CHECK(core.connect(host));CHECK(core.connect(guest));State replica=baseline;
    uint64_t sequence=0;
    auto submit=[&](Peer author,Intent intent,std::vector<Mutation> mutations){
        auto grant=core.acquire(author,{{}},100,30000);CHECK(grant.accepted());
        Request r;r.project=d.project;r.epoch=7;r.operation={author,++sequence,7};r.base_revision=core.state().document.revision;r.intent=intent;r.summary="Act roster";r.mutations=std::move(mutations);
        for(const auto& l:grant.leases)r.leases.push_back({l.scope,l.token});
        auto result=core.submit(r,100);CHECK(core.release(author,r.leases));
        if(result.accepted()){CHECK(apply_committed(replica,result,error));CHECK(replica==core.state());std::vector<uint8_t> bytes;State roundtrip;const bool encoded=wire::encode_state(replica,bytes,error);if(!encoded)std::cerr<<"roster revision "<<replica.document.revision<<": "<<error<<'\n';CHECK(encoded);CHECK(bytes[24]==4);CHECK(wire::decode_state(bytes,roundtrip,error));CHECK(roundtrip==replica);}
        return result;
    };
    Mutation add;add.kind=MutationKind::SetActs;add.act_roster=act_roster(d);
    AuthoringSettings row;row.act_id={d.next_id,1};row.object={d.next_id+1,1};row.properties[kActBlockProperty]=StructuredValue{kActBlockFormat,{3},{}};
    add.act_roster->insert(add.act_roster->begin()+1,row);CHECK(submit(guest,Intent::Forward,{add}).accepted());
    CHECK(core.state().document.act_order==(std::vector<ObjectId>{act,row.act_id,b}));
    Mutation reorder;reorder.kind=MutationKind::SetActs;reorder.expected=1;reorder.act_roster=act_roster(core.state().document);
    std::swap((*reorder.act_roster)[0],(*reorder.act_roster)[2]);CHECK(submit(host,Intent::Forward,{reorder}).accepted());
    CHECK(core.state().document.act_order==(std::vector<ObjectId>{b,row.act_id,act}));
    Mutation erase;erase.kind=MutationKind::SetActs;erase.expected=2;erase.act_roster=act_roster(core.state().document);erase.act_roster->pop_back();
    CHECK(submit(host,Intent::Forward,{erase}).accepted());
    CHECK(core.state().document.placements.at(p.object).act_id==b && core.state().document.class_settings.at(settings).act_id==b);
    CHECK(core.state().document.tombstones.contains(act));
    Mutation move;move.target=p.object;move.field={Field::Position,0};move.value=Vec3{4,5,6};
    CHECK(submit(guest,Intent::Forward,{move}).accepted());
    Mutation text;text.target=act_settings_id(1);text.field={Field::Property,kActBlockProperty};text.value=StructuredValue{kActBlockFormat,{9},{}};
    CHECK(submit(guest,Intent::Forward,{text}).accepted());
    CHECK(submit(host,Intent::Undo,{}).accepted());
    CHECK(core.state().document.act_order==(std::vector<ObjectId>{b,row.act_id,act}));
    CHECK(core.state().document.placements.at(p.object).position==(Vec3{4,5,6}));
    CHECK(core.state().document.placements.at(p.object).act_id==act);
    CHECK(value(core.state().document,text.target,text.field)==text.value);
    CHECK(submit(host,Intent::Undo,{}).accepted());
    CHECK(core.state().document.act_order==(std::vector<ObjectId>{act,row.act_id,b}));
    CHECK(value(core.state().document,text.target,text.field)==text.value);
    CHECK(submit(host,Intent::Redo,{}).accepted());
    CHECK(submit(host,Intent::Redo,{}).accepted());
    auto bad=erase;bad.expected=core.state().document.act_revision;bad.act_roster=std::vector<AuthoringSettings>{};CHECK(!submit(host,Intent::Forward,{bad}).accepted());
    bad.act_roster=act_roster(core.state().document);bad.act_roster->push_back(bad.act_roster->front());CHECK(!submit(host,Intent::Forward,{bad}).accepted());
}
static void placement_history_after_act_deletion(){
    using namespace editor_native;
    auto baseline=seed();auto& d=baseline.document;d.next_id=9ull<<32;d.classes.insert(cast);d.class_settings[settings]=initial();
    const ObjectId second{act.id|1,1};d.acts.insert(second);d.act_order={act,second};d.settings_schema=authoring_property_schema();
    for(unsigned i=0;i<2;++i){AuthoringSettings row;row.object=act_settings_id(i);row.act_id=d.act_order[i];row.properties[kActBlockProperty]=StructuredValue{kActBlockFormat,{uint8_t(i+1)},{}};d.settings[row.object]=row;}
    Placement p;p.object={20,1};p.class_id=cast;p.act_id=act;p.position={3,4,5};d.placements[p.object]=p;
    Coordinator core;std::string error;CHECK(core.reset(baseline,7,error));CHECK(core.connect(host));CHECK(core.connect(guest));State replica=baseline;uint64_t sequence=0;
    auto submit=[&](Peer author,Intent intent,std::vector<Mutation> changes){
        auto grant=core.acquire(author,{{}},0,30000);CHECK(grant.accepted());
        Request r;r.project=d.project;r.epoch=7;r.operation={author,++sequence,7};r.base_revision=core.state().document.revision;r.intent=intent;r.summary="Placement history across acts";r.mutations=std::move(changes);
        for(const auto& lease:grant.leases)r.leases.push_back({lease.scope,lease.token});
        auto result=core.submit(r,0);CHECK(result.accepted());CHECK(core.release(author,r.leases));CHECK(apply_committed(replica,result,error));CHECK(replica==core.state());
    };
    Mutation erase;erase.kind=MutationKind::Delete;erase.target=p.object;submit(guest,Intent::Forward,{erase});
    Mutation roster;roster.kind=MutationKind::SetActs;roster.act_roster=act_roster(core.state().document);roster.act_roster->erase(roster.act_roster->begin());
    submit(host,Intent::Forward,{roster});submit(guest,Intent::Undo,{});
    const auto& restored=core.state().document.placements.at(p.object);CHECK(restored.act_id==second && restored.position==p.position);
    std::vector<uint8_t> bytes;State recovered;CHECK(wire::encode_state(core.state(),bytes,error));CHECK(wire::decode_state(bytes,recovered,error));CHECK(recovered==core.state());
}
static void exact_class_boundary(bool relaxed_mutant=false){
    // Exercise real canonical allocation/validation/history. The dependency
    // registry below is a transactional hook double, not Native admission.
    constexpr uint32_t limit=1024;
    auto baseline=seed();auto& doc=baseline.document;doc.next_id=9ull<<32;doc.next_class=limit-1;
    auto row=[](uint32_t index){
        auto result=initial();result.class_id.id|=index;result.object.id|=index;
        auto& body=std::get<StructuredValue>(result.properties.at(block.property));
        body.bytes={0x71,uint8_t(index),uint8_t(index>>8),0xA9};
        if(index)body.references={cast};
        return result;
    };
    for(uint32_t index=0;index<limit-1;++index){auto c=row(index);doc.classes.insert(c.class_id);doc.class_settings.emplace(c.object,std::move(c));}
    const ObjectId untouched{(1ull<<32)|77,1};
    Placement placement;placement.object=untouched;placement.class_id=cast;placement.act_id=act;placement.position={19,23,29};
    doc.placements.emplace(untouched,placement);
    doc.settings_schema=editor_native::authoring_property_schema();
    AuthoringSettings authoring;authoring.object=editor_native::act_settings_id(0);authoring.act_id=act;
    authoring.properties[editor_native::kActBlockProperty]=StructuredValue{editor_native::kActBlockFormat,{0x19,0x23,0x29},{cast}};
    doc.settings.emplace(authoring.object,authoring);
    std::string error;CHECK(valid(doc,{},error));
    Limits limits;if(relaxed_mutant)limits.classes=limit+1; // Test sensitivity only.
    Coordinator core(limits);CHECK(core.reset(baseline,7,error));CHECK(core.connect(host));CHECK(core.connect(guest));
    const ObjectId last{cast.id|(limit-1),1},last_settings{settings.id|(limit-1),1};
    const auto foreign=core.acquire(guest,{{ScopeKind::Content,cast,{}}},100,30000);CHECK(foreign.accepted());
    auto granted=core.acquire(host,{{ScopeKind::Structure,{},{}}},100,30000);CHECK(granted.accepted());
    const auto resources=[](const Document& d){
        std::set<ResourceKey> result;
        for(const auto& [id,c]:d.class_settings){(void)id;result.insert(std::get<ResourceKey>(c.properties.at(kClassAssetResourceProperty)));}
        return result;
    };
    auto admitted=resources(doc);State durable=baseline,replica=baseline;
    unsigned validations=0,applications=0,persists=0,rollbacks=0;
    Hooks hooks;
    hooks.validate=[&](const Document&,const Document&,const Change& change,std::string&){
        ++validations;CHECK(change.patch.dependencies.size()==2);return true;
    };
    hooks.apply=[&](const Document&,const Document& after,const Change&,std::string&){++applications;admitted=resources(after);return true;};
    hooks.persist=[&](const State& next,const Change&,std::string&){++persists;durable=next;return true;};
    hooks.rollback=[&](const Document&,const Document&,const Change&,std::string&){++rollbacks;return true;};
    uint64_t sequence=0;
    const auto request=[&](Intent intent,std::vector<Mutation> mutations={}){
        Request r;r.project=doc.project;r.epoch=7;r.operation={host,++sequence,7};r.base_revision=core.state().document.revision;
        r.intent=intent;r.summary="Exact canonical class boundary";r.mutations=std::move(mutations);
        if(intent==Intent::Forward)r.dependencies={{untouched,CheckKind::Object,{},0},{settings,CheckKind::Field,block,0}};
        for(const auto& lease:granted.leases)r.leases.push_back({lease.scope,lease.token});return r;
    };
    const auto unrelated=[&]{
        const auto& after=core.state().document;
        CHECK(after.placements==doc.placements && after.settings==doc.settings && after.external_references==doc.external_references);
        CHECK(after.schema==doc.schema && after.class_schema==doc.class_schema && after.settings_schema==doc.settings_schema);
        CHECK(after.acts==doc.acts && after.act_order==doc.act_order && after.terrain==doc.terrain);
        for(const auto& [id,c]:doc.class_settings)CHECK(after.class_settings.at(id)==c);
    };
    const auto accepted=[&](Request r,size_t count,Revision revision){
        const auto decision=core.submit(r,100,hooks);
        if(!decision.accepted())std::cerr<<"class boundary revision="<<revision<<" code="<<unsigned(decision.code)<<" reason="<<decision.reason<<'\n';
        CHECK(decision.accepted() && decision.change);
        CHECK(core.state().document.classes.size()==count && core.state().document.class_settings.size()==count);
        CHECK(core.state().document.revision==revision && core.state().history.size()==1);
        CHECK(core.state().document.next_class==limit);CHECK(admitted==resources(core.state().document));
        CHECK(durable==core.state());CHECK(apply_committed(replica,decision,error));
        // A host's rejected-decision cache need not be copied to the replica.
        CHECK(replica.document==core.state().document && replica.history==core.state().history);
        std::vector<uint8_t> bytes;State reopened;CHECK(wire::encode_state(durable,bytes,error));
        CHECK(wire::decode_state(bytes,reopened,error) && reopened==durable);unrelated();return decision;
    };
    auto candidate=row(limit-1);candidate.properties[kClassAssetResourceProperty]=ResourceKey{0x710003FF,2,3};
    std::get<StructuredValue>(candidate.properties.at(block.property)).references={cast,{cast.id|1,1}};
    Mutation add;add.kind=MutationKind::CreateClass;add.class_initial=candidate;
    const auto created=accepted(request(Intent::Forward,{add}),limit,1);
    CHECK(created.change->patch.classes.size()==1 && created.change->patch.classes.front().object==last);
    CHECK(core.state().document.classes.contains(last) && core.state().document.class_settings.contains(last_settings));
    CHECK(core.state().document.class_settings.at(last_settings).properties==candidate.properties);
    CHECK(validations==1 && applications==1 && persists==1 && rollbacks==0);
    // Class/Content capabilities are meaningful only after the new canonical
    // identity exists. Retain them across the subsequent Undo tombstone.
    const auto history_grant=core.acquire(host,{{ScopeKind::Class,last,{}},{ScopeKind::Content,last,{}}},100,30000);CHECK(history_grant.accepted());
    granted.leases.insert(granted.leases.end(),history_grant.leases.begin(),history_grant.leases.end());
    auto overflow=row(limit);overflow.properties[kClassAssetResourceProperty]=ResourceKey{0x71000400,2,3};
    add.class_initial=overflow;const auto rejected_request=request(Intent::Forward,{add});
    const auto before=core.state(),before_durable=durable;const auto before_admitted=admitted;
    const auto decision=core.submit(rejected_request,100,hooks);
    CHECK(!decision.accepted() && !decision.change && !decision.reason.empty());
    CHECK(core.state().document==before.document && core.state().history==before.history);
    CHECK(core.state().retired_sequences==before.retired_sequences && core.state().history_truncated==before.history_truncated);
    CHECK(admitted==before_admitted && durable==before_durable);
    CHECK(validations==1 && applications==1 && persists==1 && rollbacks==0);
    // Rejection is remembered, but it cannot touch a class, dependency, durable
    // revision or undo stack, and replay cannot invoke any transactional hook.
    CHECK(core.state().decisions.at(rejected_request.operation)==decision);
    const auto rejected_state=core.state();CHECK(core.submit(rejected_request,100,hooks)==decision && core.state()==rejected_state);
    CHECK(!core.state().document.classes.contains(overflow.class_id) && !core.state().document.class_settings.contains(overflow.object));
    CHECK(!admitted.contains(std::get<ResourceKey>(overflow.properties.at(kClassAssetResourceProperty))));
    const auto undone=accepted(request(Intent::Undo),limit-1,2);CHECK(undone.change->patch.classes.size()==1);
    CHECK(!core.state().document.classes.contains(last) && admitted==resources(doc));
    CHECK(core.state().history.front().redo_available && !core.state().history.front().applied);
    accepted(request(Intent::Redo),limit,3);
    CHECK(core.state().document.class_settings.at(last_settings).properties==candidate.properties);
    CHECK(core.state().history.front().applied && !core.state().history.front().redo_available);
    CHECK(validations==3 && applications==3 && persists==3 && rollbacks==0);
    CHECK(core.release(host,rejected_request.leases));
    std::vector<LeaseToken> foreign_tokens;for(const auto& lease:foreign.leases)foreign_tokens.push_back({lease.scope,lease.token});
    CHECK(core.release(guest,foreign_tokens));
}
int main(int argc,char** argv){
    if(argc==2 && std::string(argv[1])=="--class-boundary-relaxed-mutant"){
        exact_class_boundary(true);return 0;
    }
    if(argc>2 || (argc==2 && std::string(argv[1])!="--class-boundary-only"))return 2;
    exact_class_boundary();
    if(argc==2){std::cout<<checks<<" exact class-boundary checks passed\n";return 0;}
    active_behavior_reference_metadata();
    placement_history_after_act_deletion();
    ordered_act_roster();
    independent_act_and_captain_settings();
    unrelated_asset_ownership();
    std::array<uint8_t,0x90> asset_data;asset_data.fill(0xA5);
    auto expected=asset_data;
    for(size_t offset:{0xCu,0x34u,0x5Cu})std::fill_n(expected.begin()+offset,4,0);
    editor_native::canonical_cast_asset_hints(asset_data.data());
    CHECK(asset_data==expected); // All keys, server IDs and other fields retained.
    editor_native::canonical_cast_asset_hints(asset_data.data());CHECK(asset_data==expected);
    referenced_import_undo();
    std::string error;Coordinator core;auto original=seed();CHECK(core.reset(original,7,error));CHECK(core.connect(host));CHECK(core.connect(guest));
    uint64_t sequence=0;State replica=original;
    auto submit=[&](Intent intent,std::vector<Mutation> mutations={},Peer author=host){
        auto grant=core.acquire(author,{{}},100,30000);CHECK(grant.accepted());
        Request r;r.project=core.state().document.project;r.epoch=7;r.operation={author,++sequence,7};r.base_revision=core.state().document.revision;r.intent=intent;r.summary="Cast regression";r.mutations=std::move(mutations);
        for(const auto& l:grant.leases)r.leases.push_back({l.scope,l.token});
        auto result=core.submit(r,100);CHECK(result.accepted());CHECK(apply_committed(replica,result,error));CHECK(replica==core.state());
        CHECK(core.release(author,r.leases));
        std::vector<uint8_t> bytes;CHECK(wire::encode_state(core.state(),bytes,error));State decoded;CHECK(wire::decode_state(bytes,decoded,error));CHECK(decoded==core.state());
        return result;
    };
    Mutation create;create.kind=MutationKind::CreateClass;create.class_initial=initial();
    submit(Intent::Forward,{create});CHECK(core.state().document.classes.contains(cast));CHECK(core.state().document.next_class==1);
    auto changed=std::get<StructuredValue>(core.state().document.class_settings.at(settings).properties.at(block.property));changed.bytes={4,5,6};
    Mutation edit;edit.target=settings;edit.field=block;edit.value=changed;edit.expected=1;
    submit(Intent::Forward,{edit});CHECK(value(core.state().document,settings,block)==Value{changed});
    submit(Intent::Undo);CHECK(value(core.state().document,settings,block)==initial().properties.at(block.property));
    submit(Intent::Undo);CHECK(core.state().document.classes.empty());CHECK(core.state().document.class_settings.empty());CHECK(core.state().document.next_class==1);
    submit(Intent::Redo);CHECK(core.state().document.classes.contains(cast));
    submit(Intent::Redo);CHECK(value(core.state().document,settings,block)==Value{changed});
    auto second=initial();second.object.id|=1;second.class_id.id|=1;
    create.class_initial=second;submit(Intent::Forward,{create},guest);
    submit(Intent::Undo);submit(Intent::Undo);
    CHECK(core.state().document.class_settings.at(second.object).properties==second.properties);
    CHECK(core.state().document.classes.contains(second.class_id)&&!core.state().document.classes.contains(cast));
    submit(Intent::Redo);submit(Intent::Redo);
    CHECK(core.state().document.class_settings.at(second.object).properties==second.properties);
    auto invalid_recovery=core.state();
    auto& unsafe=std::get<StructuredValue>(invalid_recovery.document.class_settings.at(settings).properties.at(block.property));
    unsafe.references={{1234567,9}};Coordinator recovery;CHECK(!recovery.reset(invalid_recovery,8,error));
    const auto before=core.state();
    auto rejected=[&](Mutation mutation,Peer peer=host){
        auto grant=core.acquire(peer,{{ScopeKind::Class,cast,{}}},100,30000);CHECK(grant.accepted());
        Request r;r.project=before.document.project;r.epoch=7;r.operation={peer,++sequence,7};r.base_revision=before.document.revision;r.summary="Rejected cast";r.mutations={mutation};
        for(const auto& l:grant.leases)r.leases.push_back({l.scope,l.token});
        CHECK(!core.submit(r,100).accepted());CHECK(core.state().document==before.document);CHECK(core.state().history==before.history);CHECK(core.release(peer,r.leases));
    };
    edit.expected=field_revision(core.state().document,settings,block);
    auto oversized=changed;oversized.bytes.resize(Limits{}.structured_bytes+1);edit.value=oversized;rejected(edit);
    auto dangling=changed;dangling.references={{999,1}};edit.value=dangling;rejected(edit);
    edit.value=changed;edit.expected=0;rejected(edit,guest);
    Mutation remove;remove.kind=MutationKind::DeleteClass;remove.target=cast;remove.expected=core.state().document.class_settings.at(settings).revision;
    rejected(remove); // A class lease cannot change the cast roster.
    auto movement=seed();auto& d=movement.document;d.class_schema.clear();d.classes.insert(cast);d.schema=editor_native::placement_property_schema();
    Placement p;p.object={20,1};p.class_id=cast;p.act_id=act;p.properties[editor_native::kRelativeScaleProperty]=.5f;
    p.properties[editor_native::kSoundPitchProperty]=1.f;p.properties[editor_native::kSoundGainProperty]=.375f;
    p.properties[editor_native::kElevationProperty]=2.f;p.properties[editor_native::kDestinationPositionProperty]=Vec3{10,20,30};
    p.properties[editor_native::kDestinationRotationProperty]=Rotation{};p.properties[editor_native::kDestinationScaleProperty]=1.f;d.placements[p.object]=p;
    CHECK(valid(d,{},error));auto invalid=d;invalid.placements.at(p.object).properties[editor_native::kDestinationRotationProperty]=Rotation{0,0,0,0};CHECK(!valid(invalid,{},error));
    invalid=d;invalid.placements.at(p.object).properties[editor_native::kDestinationPositionProperty]=Vec3{1e8f,0,0};CHECK(!valid(invalid,{},error));
    Coordinator poses;CHECK(poses.reset(movement,7,error));CHECK(poses.connect(host));auto grant=poses.acquire(host,{{ScopeKind::Placement,p.object,{}}},0,1000);CHECK(grant.accepted());
    Request r;r.project=d.project;r.epoch=7;r.operation={host,1,7};r.summary="Move destination";
    Mutation destination;destination.target=p.object;destination.field={Field::Property,editor_native::kDestinationPositionProperty};destination.value=Vec3{30,40,50};r.mutations={destination};
    for(const auto& l:grant.leases)r.leases.push_back({l.scope,l.token});
    CHECK(poses.submit(r,0).accepted());CHECK(poses.state().document.placements.at(p.object).position==p.position);CHECK(poses.state().document.placements.at(p.object).rotation==p.rotation);
    r.operation.sequence=2;r.base_revision=1;r.intent=Intent::Undo;r.mutations.clear();CHECK(poses.submit(r,0).accepted());CHECK(poses.state().document.placements.at(p.object).properties==p.properties);
    auto jitter=p.properties;jitter[editor_native::kSoundPitchProperty]=std::bit_cast<float>(uint32_t{0x3F80000A});
    CHECK(editor_native::same_native_properties(jitter,p.properties));
    jitter[editor_native::kSoundPitchProperty]=std::bit_cast<float>(uint32_t{0x3F800020});
    CHECK(!editor_native::same_native_properties(jitter,p.properties));
    jitter=p.properties;jitter[editor_native::kSoundGainProperty]=.5f;
    CHECK(!editor_native::same_native_properties(jitter,p.properties));
    r.operation.sequence=3;r.base_revision=2;r.intent=Intent::Forward;
    Mutation position;position.target=p.object;position.field={Field::Position,0};position.value=Vec3{4,5,6};
    Mutation pitch;pitch.target=p.object;pitch.field={Field::Property,editor_native::kSoundPitchProperty};pitch.value=std::bit_cast<float>(uint32_t{0x3F80000A});
    r.mutations={position,pitch};CHECK(poses.submit(r,0).accepted());
    CHECK(poses.state().history.size()==2);CHECK(value(poses.state().document,p.object,pitch.field)==pitch.value);
    CHECK(poses.state().document.placements.at(p.object).properties.at(editor_native::kSoundGainProperty)==p.properties.at(editor_native::kSoundGainProperty));
    r.operation.sequence=4;r.base_revision=3;r.intent=Intent::Undo;r.mutations.clear();CHECK(poses.submit(r,0).accepted());
    CHECK(poses.state().document.placements.at(p.object).position==p.position&&poses.state().document.placements.at(p.object).properties==p.properties);
    std::cout<<checks<<" cast/companion transaction checks passed\n";
}
