#include "editor_stage_metrics.h"
#include <cstdlib>
#include <cstdio>
#include <new>
static unsigned checks;
static bool allocation_forbidden;
void* operator new(std::size_t size){if(allocation_forbidden)std::abort();if(auto p=std::malloc(size))return p;throw std::bad_alloc();}
void operator delete(void* p) noexcept {std::free(p);}
void* operator new[](std::size_t size){return ::operator new(size);}
void operator delete[](void* p) noexcept {std::free(p);}
#define CHECK(...) do{++checks;if(!(__VA_ARGS__)){std::printf("FAIL %d: %s\n",__LINE__,#__VA_ARGS__);std::exit(1);}}while(0)
int main(){
    using namespace editor_stage_metrics;
    Buffer<8> buffer;editor::ProjectId project{};project[0]=0xab;
    const editor::OperationId op{{1,2},3,4};
    CHECK(!buffer.record(project,op,1,Stage::LocalSubmit,10));
    CHECK(!buffer.reset(0,10000000,1));CHECK(!buffer.reset(10,0,1));CHECK(!buffer.reset(10,10000000,0));
    CHECK(buffer.reset(10,10000000,100));
    allocation_forbidden=true;
    CHECK(buffer.record(project,op,1,Stage::LocalSubmit,100));
    CHECK(buffer.record(project,op,2,Stage::NativeApplyBegin,110));
    CHECK(buffer.record(project,op,2,Stage::NativeApplyEnd,120,1));
    auto replacement=op;replacement.epoch=5;
    CHECK(buffer.record(project,replacement,3,Stage::LocalSubmit,120));
    CHECK(!buffer.record({},op,2,Stage::LocalSubmit,120));
    CHECK(!buffer.record(project,{{4,2},3,4},2,Stage::LocalSubmit,120));
    CHECK(!buffer.record(project,{{1,0},3,4},2,Stage::LocalSubmit,120));
    CHECK(!buffer.record(project,{{1,2},0,4},2,Stage::LocalSubmit,120));
    CHECK(!buffer.record(project,{{1,2},3,0},2,Stage::LocalSubmit,120));
    CHECK(!buffer.record(project,op,2,Stage(255),120));
    CHECK(!buffer.record(project,op,2,Stage::LocalSubmit,120,3));
    allocation_forbidden=false;
    CHECK(buffer.size()==4);CHECK(buffer.at(0)->operation==op);CHECK(buffer.at(3)->operation==replacement);
    CHECK(buffer.at(2)->qpc==120&&buffer.at(2)->success==1);CHECK(!buffer.at(4));
    const auto page=buffer.report(1,2);
    CHECK(page.find("clock=win32_qpc pid=10 frequency=10000000")!=std::string::npos);
    CHECK(page.find("offset=1 end=3")!=std::string::npos);
    CHECK(page.find("event=0 ")==std::string::npos);CHECK(page.find("event=3 ")==std::string::npos);
    CHECK(page.find("stage=native_apply_end qpc=120")!=std::string::npos);
    CHECK(buffer.report(99,128).find("offset=4 end=4")!=std::string::npos);
    buffer.stop();CHECK(!buffer.record(project,op,9,Stage::OwnConfirmed,150));CHECK(buffer.size()==4);
    CHECK(buffer.reset(11,10000000,200));CHECK(buffer.size()==0&&buffer.recording());
    CHECK(!buffer.record(project,op,1,Stage::LocalSubmit,199));CHECK(buffer.clock_error()&&!buffer.recording());
    CHECK(buffer.reset(11,10000000,200));
    allocation_forbidden=true;
    for(unsigned i=0;i<8;++i)CHECK(buffer.record(project,op,i,Stage::LocalSubmit,200+i));
    CHECK(!buffer.record(project,op,9,Stage::LocalSubmit,208));
    CHECK(buffer.overflow()==1&&!buffer.recording()&&buffer.size()==8);
    allocation_forbidden=false;
    CHECK(buffer.report(0,99999).find("end=8")!=std::string::npos);
    Buffer<64> worst;editor::ProjectId full;full.fill(255);
    CHECK(worst.reset(UINT32_MAX,UINT64_MAX,1));
    const editor::OperationId largest{{3,UINT32_MAX},UINT64_MAX,UINT64_MAX};
    for(unsigned i=0;i<64;++i)CHECK(worst.record(full,largest,UINT64_MAX,Stage::HostRequestReceived,UINT64_MAX));
    const auto maximum_page=worst.report(0,99999);
    CHECK(maximum_page.size()<12u*1024);CHECK(maximum_page.find("end=32")!=std::string::npos);
    CHECK(maximum_page.find("event=31 ")!=std::string::npos);CHECK(maximum_page.find("event=32 ")==std::string::npos);
    std::printf("%u bounded editor stage checks passed\n",checks);
}
