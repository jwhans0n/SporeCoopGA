#include "editor_native_terrain_rotations.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <limits>

static unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
using namespace editor_native_terrain_rotations;
static Row row(uint32_t n){return {{uint64_t(n)+1,1},n,n+17,9,{0,0,0,1}};}
static editor::Rotation changed(editor::Rotation q){q.x=std::bit_cast<float>(uint32_t{0x34000001});return q;}
static void equal_output(editor::Rotation a,editor::Rotation b){CHECK(exact(a,b));}
int main(){
    std::string error;Witness proof;
    std::vector<Row> canonical{row(0),row(1)},before=canonical,after=canonical;
    after[0].q=changed(after[0].q);
    CHECK(proof.observe(before,after,canonical,error));CHECK(error.empty());CHECK(proof.size()==1);
    editor::Rotation sentinel{3,4,5,6},out=sentinel;
    CHECK(proof.normalize(after[0],canonical[0],out)==Result::Match);equal_output(out,canonical[0].q);
    out=sentinel;CHECK(proof.normalize(after[1],canonical[1],out)==Result::None);equal_output(out,sentinel);
    auto stranger=after[0];stranger.q.y=0.25f;out=sentinel;
    CHECK(proof.normalize(stranger,canonical[0],out)==Result::None);equal_output(out,sentinel);
    // Only an exactly witnessed previous output can chain another rebuild.
    auto second=after;second[0].q.y=std::bit_cast<float>(uint32_t{0x35000001});
    CHECK(proof.observe(after,second,canonical,error));CHECK(proof.size()==1);
    CHECK(proof.normalize(second[0],canonical[0],out)==Result::Match);equal_output(out,canonical[0].q);
    out=sentinel;CHECK(proof.normalize(after[0],canonical[0],out)==Result::None);equal_output(out,sentinel);
    // Canonical identity/revision/rotation changes cannot borrow old provenance.
    for(unsigned which=0;which<6;++which){
        auto target=canonical[0];
        if(which==0)++target.object.generation;
        if(which==1)++target.slot;
        if(which==2)++target.class_index;
        if(which==3)++target.revision;
        if(which==4)target.q.z=-0.0f;
        if(which==5)target.q.w=std::numeric_limits<float>::infinity();
        out=sentinel;CHECK(proof.normalize(second[0],target,out)==Result::Stale);equal_output(out,sentinel);
    }
    // Clearing a genuinely edited row prevents coincident old rendered bits
    // from being mistaken for a terrain round trip.
    proof.erase(canonical[0].object);CHECK(proof.size()==0);out=sentinel;
    CHECK(proof.normalize(second[0],canonical[0],out)==Result::None);equal_output(out,sentinel);
    CHECK(!proof.observe(second,second,canonical,error));CHECK(proof.size()==0);
    CHECK(proof.observe(canonical,after,canonical,error));
    auto collision=after;collision[0].object={99,1};auto collision_canonical=canonical;collision_canonical[0].object={99,1};
    CHECK(!proof.observe(collision,collision,collision_canonical,error));CHECK(proof.size()==0);
    // Exact roster validation refuses duplication, deletion, appending, slot,
    // class, revision and invalid values; failures retire previously valid data.
    for(unsigned which=0;which<11;++which){
        CHECK(proof.observe(canonical,after,canonical,error));
        auto bad=after;
        if(which==0)bad.push_back(row(2));
        if(which==1)bad.pop_back();
        if(which==2)bad[1].object=bad[0].object;
        if(which==3)bad[1].slot=bad[0].slot;
        if(which==4)++bad[0].class_index;
        if(which==5)++bad[0].revision;
        if(which==6)bad[0].object.id=0;
        if(which==7)bad[0].object.generation=0;
        if(which==8)bad[0].slot=kLimit;
        if(which==9)bad[0].q.x=std::numeric_limits<float>::quiet_NaN();
        if(which==10)bad[0].q.y=-std::numeric_limits<float>::infinity();
        CHECK(!proof.observe(canonical,bad,canonical,error));CHECK(!error.empty());CHECK(proof.size()==0);
    }
    // Iteration order is not identity: correctly bound rows may be enumerated
    // in another order without changing their authored association.
    auto reverse=after;std::reverse(reverse.begin(),reverse.end());
    CHECK(proof.observe(canonical,reverse,canonical,error));
    CHECK(proof.normalize(after[0],canonical[0],out)==Result::Match);equal_output(out,canonical[0].q);
    CHECK(proof.observe(after,canonical,canonical,error));CHECK(proof.size()==0);
    std::vector<Row> full;for(uint32_t n=0;n<kLimit;++n)full.push_back(row(n));
    auto rendered=full;for(auto& item:rendered)item.q=changed(item.q);
    CHECK(proof.observe(full,rendered,full,error));CHECK(proof.size()==kLimit);
    for(size_t n=0;n<full.size();++n){
        CHECK(proof.normalize(rendered[n],full[n],out)==Result::Match);equal_output(out,full[n].q);
    }
    full.push_back(row(kLimit));rendered.push_back(row(kLimit));
    CHECK(!proof.observe(full,rendered,full,error));CHECK(proof.size()==0);
    CHECK(proof.observe({}, {}, {},error));CHECK(proof.size()==0);
    std::printf("editor native terrain rotations: %u checks passed\n",checks);
}
