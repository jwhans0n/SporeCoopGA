#include "editor_reopen_identity.h"
#include <cstdlib>
#include <iostream>
using namespace editor_reopen_probe;
using editor::ObjectId;
using editor_native::Snapshot;
static unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { std::cerr << __LINE__ << ": " #x "\n"; std::exit(1); } } while (0)
static Snapshot fixture(bool saved, unsigned count=3) {
    Snapshot s; s.bytes={0,1,255,7}; s.source={9,10,11}; s.act=0;
    s.captain={12,13,14}; s.captain_locked=true;
    s.document.classes.insert({2ull<<32,1}); s.document.acts.insert({3ull<<32,1});
    for (unsigned i=0;i<count;++i) {
        ObjectId id{(saved?4ull:1ull)<<32 | i,saved?7u:1u};
        editor::Placement p; p.object=id; p.class_id={2ull<<32,1}; p.act_id={3ull<<32,1};
        p.position={float(i),float(i+1),float(i+2)}; p.scale=float(i+1);
        s.document.placements.emplace(id,p); s.bindings.push_back({id,i});
        s.document.external_references.push_back({{(3ull<<32)|0x1000,1},id});
    }
    return s;
}
static void identical_observations(const Snapshot& observed,const Snapshot& mapped) {
    CHECK(mapped.bytes==observed.bytes); CHECK(mapped.source==observed.source);
    CHECK(mapped.act==observed.act); CHECK(mapped.captain==observed.captain);
    CHECK(mapped.captain_locked==observed.captain_locked);
    CHECK(mapped.document.project==observed.document.project);
    CHECK(mapped.document.epoch==observed.document.epoch);
    CHECK(mapped.document.revision==observed.document.revision);
}
int main() {
    auto saved=fixture(true), observed=fixture(false); Snapshot out; std::string error;
    CHECK(remap_reopened(saved,observed,out,error)); CHECK(error.empty());
    CHECK(out.document==saved.document); CHECK(out.bindings==saved.bindings);
    identical_observations(observed,out);
    std::reverse(saved.bindings.begin(),saved.bindings.end());
    std::reverse(observed.bindings.begin(),observed.bindings.end());
    CHECK(remap_reopened(saved,observed,out,error)); CHECK(error.empty());
    CHECK(out.document==saved.document);
    CHECK(out.bindings==(std::vector<editor_native::Binding>{{{4ull<<32,7},0},{{(4ull<<32)|1,7},1},{{(4ull<<32)|2,7},2}}));
    // A changed native pose must survive identity mapping, so independent native
    // validate rejects it rather than our helper hiding a broken disk save.
    auto changed=observed;
    changed.document.placements.begin()->second.position.x=123;
    changed.bytes.push_back(66);
    CHECK(remap_reopened(saved,changed,out,error));
    CHECK(out.document.placements.begin()->second.position.x==123);
    CHECK(out.document!=saved.document); identical_observations(changed,out);
    // References in properties and external goal/avatar relations are renamed;
    // non-placement owners and resource identities are preserved.
    auto references=observed;
    references.document.placements.begin()->second.properties[11]=ObjectId{(1ull<<32)|1,1};
    references.document.placements.begin()->second.properties[12]=editor::ResourceKey{77,88,99};
    CHECK(remap_reopened(saved,references,out,error));
    CHECK(std::get<ObjectId>(out.document.placements.begin()->second.properties.at(11))==(ObjectId{(4ull<<32)|1,7}));
    CHECK(std::get<editor::ResourceKey>(out.document.placements.begin()->second.properties.at(12))==(editor::ResourceKey{77,88,99}));
    CHECK(out.document.external_references==saved.document.external_references);
    const auto untouched=out;
    auto fails=[&](const Snapshot& a,const Snapshot& b) {
        CHECK(!remap_reopened(a,b,out,error)); CHECK(!error.empty());
        CHECK(out.bytes==untouched.bytes); CHECK(out.document==untouched.document);
        CHECK(out.bindings==untouched.bindings); CHECK(out.source==untouched.source);
    };
    auto bad=observed; bad.source.instance++; fails(saved,bad);
    bad=observed; bad.act++; fails(saved,bad);
    bad=observed; bad.captain.type++; fails(saved,bad);
    bad=observed; bad.captain_locked=false; fails(saved,bad);
    bad=observed; bad.bytes.clear(); fails(saved,bad);
    bad=observed; bad.bytes.resize((16u<<20)+1); fails(saved,bad);
    bad=observed; bad.document.classes.clear(); fails(saved,bad);
    bad=observed; bad.document.acts.clear(); fails(saved,bad);
    bad=observed; bad.document.placements.erase(bad.document.placements.begin()); fails(saved,bad);
    bad=observed; bad.bindings.pop_back(); fails(saved,bad);
    bad=observed; bad.bindings[0].marker=bad.bindings[1].marker; fails(saved,bad);
    bad=observed; bad.bindings[0].object=bad.bindings[1].object; fails(saved,bad);
    bad=observed; bad.bindings[0].object.generation=0; fails(saved,bad);
    bad=observed; bad.bindings[0].object.id=0; fails(saved,bad);
    bad=observed; bad.bindings[0].marker=10; fails(saved,bad); // No compaction repair.
    bad=observed; bad.document.placements.begin()->second.object={99,1}; fails(saved,bad);
    bad=observed; bad.document.tombstones[{88,1}]=1; fails(saved,bad);
    bad=saved; bad.bindings[0].marker=7; fails(bad,observed);
    bad=saved; bad.source={}; fails(bad,observed);
    auto maximum=fixture(false,1024), max_saved=fixture(true,1024);
    CHECK(remap_reopened(max_saved,maximum,out,error)); CHECK(out.document==max_saved.document);
    auto too_many=fixture(false,1025), too_saved=fixture(true,1025);
    CHECK(!remap_reopened(too_saved,too_many,out,error));
    auto empty=fixture(false,0), empty_saved=fixture(true,0);
    CHECK(remap_reopened(empty_saved,empty,out,error)); CHECK(out.bindings.empty());
    std::cout << checks << " read-only reopen identity checks passed\n";
}
