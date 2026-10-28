// Portable cast transactions and placement companion values. Native blocks are
// controlled data here; actual game parsing is covered by the native lab.
#include "editor_native_cast.h"
#include "editor_native_scale.h"
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
static ClassActSettings initial(){ClassActSettings c;c.object=settings;c.class_id=cast;c.act_id=act;c.properties[block.property]=StructuredValue{editor_native::kCastBlockFormat,{1,2,3},{}};return c;}
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
int main(){
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
