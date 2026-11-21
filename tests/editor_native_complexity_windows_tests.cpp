#include "editor_native_complexity.h"
#include <cstdio>
#include <cstdlib>

static unsigned checks=0;
#define CHECK(x) do {++checks;if(!(x)){std::printf("FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main(){
    using namespace editor_native_complexity;
    std::array<uint8_t,11> create{0x3D,0,4,0,0,0x0F,0x83,0xD7,1,0,0};
    std::array<uint8_t,11> clone{0x3D,0,4,0,0,0x0F,0x83,0x9D,0,0,0};
    const auto original_create=create,original_clone=clone;
    Bounds bounds;CHECK(bounds.initialize(uintptr_t(create.data()),uintptr_t(clone.data())));CHECK(!bounds.active());
    std::string error;CHECK(bounds.set(false,error));CHECK(create==original_create&&clone==original_clone);
    CHECK(bounds.set(true,error));CHECK(bounds.active());
    CHECK(create[1]==0&&create[2]==16&&clone[1]==0&&clone[2]==16);
    for(size_t i=0;i<11;++i)if(i!=2)CHECK(create[i]==original_create[i]&&clone[i]==original_clone[i]);
    CHECK(bounds.set(true,error));CHECK(bounds.set(false,error));CHECK(!bounds.active());CHECK(create==original_create&&clone==original_clone);
    // A changed branch or unsupported immediate is never silently overwritten.
    clone[8]=1;CHECK(!bounds.set(true,error));CHECK(create==original_create);CHECK(!bounds.active());
    clone=original_clone;create[2]=8;CHECK(!bounds.set(true,error));CHECK(create[2]==8&&clone==original_clone);
    create=original_create;CHECK(bounds.set(true,error));CHECK(bounds.set(false,error));
    CHECK(create==original_create&&clone==original_clone);
    Bounds unsupported;CHECK(!unsupported.initialize(0,uintptr_t(clone.data())));CHECK(!unsupported.set(true,error));
    CHECK(clone==original_clone);
    std::printf("editor native complexity: %u checks passed\n",checks);
}
