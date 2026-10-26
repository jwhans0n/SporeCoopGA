#include "editor_native_bindings.h"
#include <cstdlib>
#include <iostream>
using namespace editor_native;
static unsigned checks;
#define CHECK(x) do { ++checks; if(!(x)){std::cerr<<__LINE__<<": " #x "\n";std::exit(1);} } while(0)
int main(){
    const editor::ObjectId a{0x100000000ull,1},b{0x400000000ull,1},c{0x400000001ull,3};
    std::vector<Binding> out;std::string error;
    CHECK(serialized_bindings({},out,error));CHECK(out.empty());
    CHECK(serialized_bindings({{a,0},{b,1},{c,2}},out,error));
    CHECK(out==(std::vector<Binding>{{a,0},{b,1},{c,2}}));
    CHECK(serialized_bindings({{b,1},{c,2}},out,error));
    CHECK(out==(std::vector<Binding>{{b,0},{c,1}})); // Delete first row.
    CHECK(serialized_bindings({{a,0},{c,2}},out,error));
    CHECK(out==(std::vector<Binding>{{a,0},{c,1}})); // Delete middle row.
    CHECK(serialized_bindings({{c,2},{a,0},{b,17}},out,error));
    CHECK(out==(std::vector<Binding>{{a,0},{c,1},{b,2}})); // Input order is irrelevant.
    CHECK(serialized_bindings({{c,0},{b,17}},out,error));
    CHECK(out==(std::vector<Binding>{{c,0},{b,1}})); // Reused row retains its new incarnation.
    std::vector<Binding> maximum;
    for(uint32_t i=0;i<kPlacementLimit;++i)maximum.push_back({{0x500000000ull+i,1},i});
    CHECK(serialized_bindings(maximum,out,error));CHECK(out.size()==kPlacementLimit);
    const auto largest=out;
    maximum.push_back({{0x600000000ull,1},kPlacementLimit});
    CHECK(!serialized_bindings(maximum,out,error));CHECK(out==largest);CHECK(!error.empty());
    const auto previous=out;
    for(const auto& invalid:std::vector<std::vector<Binding>>{{{a,0},{b,0}},{{a,0},{a,1}},{{a,kPlacementLimit}},{{{0,1},0}},{{{1,0},0}}}){
        CHECK(!serialized_bindings(invalid,out,error));CHECK(out==previous);CHECK(!error.empty());
    }
    for(uint32_t hole=0;hole<64;++hole){
        std::vector<Binding> live;
        for(uint32_t i=0;i<64;++i)if(i!=hole)live.push_back({{0x400000000ull+i,2},i});
        CHECK(serialized_bindings(live,out,error));CHECK(out.size()==63);
        for(uint32_t i=0;i<out.size();++i){CHECK(out[i].marker==i);CHECK(out[i].object==live[i].object);}
    }
    std::cout<<checks<<" editor native serialized-binding checks passed\n";
}
