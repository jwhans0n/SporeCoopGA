#include "editor_native_pose.h"
#include <cstdlib>
#include <iostream>
#include <limits>
using namespace editor;
static unsigned checks;
#define CHECK(...) do{++checks;if(!(__VA_ARGS__)){std::cerr<<__LINE__<<": " #__VA_ARGS__ "\n";std::exit(1);}}while(0)
static State initial(){
    State s;auto& d=s.document;d.project[0]=7;d.epoch=9;d.next_id=100;d.classes={{1,1}};d.acts={{2,1}};
    Placement p;p.object={3,1};p.class_id={1,1};p.act_id={2,1};d.placements[p.object]=p;p.object={4,1};d.placements[p.object]=p;return s;
}
int main(){
    auto before=initial();Coordinator c;std::string error;CHECK(c.reset(before,9,error));CHECK(c.connect({0,1}));auto lease=c.acquire({0,1},{{}},0,30000);CHECK(lease.accepted());
    Mutation a;a.target={3,1};a.field={Field::Position,0};a.value=Vec3{1,2,3};
    Mutation b=a;b.target={4,1};b.field={Field::Scale,0};b.value=2.f;
    Request q;q.project=before.document.project;q.epoch=9;q.operation={{0,1},1,9};q.summary="Actual pose patch";q.mutations={a,b};q.leases={{lease.leases[0].scope,lease.leases[0].token}};
    auto decision=c.submit(q,0);CHECK(decision.accepted());auto after=c.state().document;auto change=*decision.change;std::set<ObjectId> targets;
    CHECK(editor_native::exact_pose_change(before.document,after,change,targets));CHECK(targets==std::set<ObjectId>({{3,1},{4,1}}));
    auto reject=[&](auto edit){auto candidate=after;edit(candidate);CHECK(!editor_native::exact_pose_change(before.document,candidate,change,targets));};
    reject([](auto& d){d.next_id++;});reject([](auto& d){d.epoch++;});reject([](auto& d){d.placements.at({3,1}).rotation={0,0,1,0};});
    reject([](auto& d){d.placements.at({3,1}).class_id={9,1};});reject([](auto& d){d.placements.at({3,1}).field_revisions[{Field::Rotation,0}]=1;});
    reject([](auto& d){d.external_references.push_back({{8,1},{3,1}});});reject([](auto& d){d.next_terrain_order++;});
    auto forged=change;forged.patch.fields[0].expected=9;CHECK(!editor_native::exact_pose_change(before.document,after,forged,targets));
    forged=change;forged.patch.fields.push_back(forged.patch.fields[0]);CHECK(!editor_native::exact_pose_change(before.document,after,forged,targets));
    forged=change;forged.patch.terrain.push_back({});CHECK(!editor_native::exact_pose_change(before.document,after,forged,targets));
    forged=change;forged.patch.fields[0].after=Rotation{};CHECK(!editor_native::exact_pose_change(before.document,after,forged,targets));
    // The exact recognizer does not normalize or overwrite any unrelated value.
    CHECK(before==initial());CHECK(c.state().document==after);
    std::cout<<checks<<" native pose recognition checks passed\n";
}
