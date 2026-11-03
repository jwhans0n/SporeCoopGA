#include "../mods/coop/sui_tree_root.h"

#include <cstdio>
#include <map>
#include <stdexcept>
#include <string>

namespace {
unsigned checks=0;
void check(bool value,const char* text) { ++checks; if (!value) throw std::runtime_error(text); }
using Graph=std::map<uintptr_t,std::vector<uintptr_t>>;
using sui_tree_root::Result;
void parsing()
{
    for (const auto* text:{"1234ABCD","1234abcd","0x1234ABCD","0X1234abcd","FFFFFFFF","00000001"}) {
        uint32_t value=0;
        check(sui_tree_root::parse(text,value),"strict valid pointer");
        check(value!=0,"nonzero parsed pointer");
    }
    for (const auto* text:{"","0","00000000","0x00000000","1234ABC","01234ABCD","1234ABCDx"," 1234ABCD","1234ABCD ",
            "+1234ABC","-1234ABC","1234GBCD","0x123456789","0x1234ABC","0x1234ABCD\n","4294967296"}) {
        uint32_t value=0xA5A5A5A5;
        check(!sui_tree_root::parse(text,value),"malformed/null pointer rejected");
        check(value==0xA5A5A5A5,"parse failure atomic");
    }
}
void membership()
{
    Graph graph{{0x1000,{0x2000,0x3000}},{0x2000,{0x4000}},{0x3000,{}},{0x4000,{}}};
    std::vector<uintptr_t> inspected;
    unsigned getters=0;
    auto current=[&] { ++getters; return uintptr_t(0x1000); };
    auto read=[&](uintptr_t node,std::vector<uintptr_t>& children) {
        inspected.push_back(node);
        const auto it=graph.find(node);
        check(it!=graph.end(),"only owning graph references inspected");
        children=it->second; return true;
    };
    uintptr_t accepted=0xDEADBEEF;
    check(sui_tree_root::resolve(0x4000,current,read,accepted)==Result::Found && accepted==0x4000,"valid nested member");
    check(getters==2 && inspected.size()==8,"second fresh main and full traversal");
    inspected.clear(); accepted=0xDEADBEEF;
    check(sui_tree_root::resolve(0x05D17546,current,read,accepted)==Result::Missing,"actual control ID is not a window pointer");
    check(accepted==0xDEADBEEF,"missing member output atomic");
    for (auto node:inspected) check(node!=0x05D17546,"invalid candidate never inspected");
    inspected.clear();
    check(sui_tree_root::resolve(0,current,read,accepted)==Result::Missing && inspected.empty(),"null candidate not inspected");
    check(sui_tree_root::resolve(0x1000,current,read,accepted)==Result::Found && accepted==0x1000,"explicit main member");
    for (const auto limits:{sui_tree_root::Limits{3,9},sui_tree_root::Limits{9,2},sui_tree_root::Limits{0,9}}) {
        accepted=0xDEADBEEF;
        check(sui_tree_root::resolve(0x1000,current,read,accepted,limits)==Result::Limit,"incomplete traversal rejects even found root");
        check(accepted==0xDEADBEEF,"limit output atomic");
    }
    accepted=0xDEADBEEF;
    check(sui_tree_root::resolve(0x4000,current,read,accepted,{4,3})==Result::Found,"exact node and edge bounds");
    graph[0x4000]={0x1000}; accepted=0xDEADBEEF;
    check(sui_tree_root::resolve(0x2000,current,read,accepted)==Result::InvalidGraph && accepted==0xDEADBEEF,"cycle refuses dump");
    graph[0x4000]={}; graph[0x3000]={0x4000};
    check(sui_tree_root::resolve(0x2000,current,read,accepted)==Result::InvalidGraph,"duplicate child refuses dump");
    graph[0x3000]={0};
    check(sui_tree_root::resolve(0x2000,current,read,accepted)==Result::InvalidGraph,"null child refuses dump");
    graph[0x3000]={};
    auto failed=[](uintptr_t,std::vector<uintptr_t>&) { return false; };
    check(sui_tree_root::resolve(0x1000,current,failed,accepted)==Result::Unavailable,"unreadable native list refuses dump");
    auto throwing=[](uintptr_t,std::vector<uintptr_t>&)->bool { throw std::bad_alloc(); };
    check(sui_tree_root::resolve(0x1000,current,throwing,accepted)==Result::Unavailable,"allocation/read exception refuses dump");
    auto absent=[] { return uintptr_t(0); };
    inspected.clear();
    check(sui_tree_root::resolve(0x4000,absent,read,accepted)==Result::Unavailable && inspected.empty(),"missing main never inspects candidate");
    getters=0;
    auto changed=[&] { return ++getters==1?uintptr_t(0x1000):uintptr_t(0x5000); };
    check(sui_tree_root::resolve(0x4000,changed,read,accepted)==Result::Changed,"changed main refuses stale root");
    getters=0;
    auto removed=[&] { if (++getters==2) graph[0x2000].clear(); return uintptr_t(0x1000); };
    accepted=0xDEADBEEF; inspected.clear();
    check(sui_tree_root::resolve(0x4000,removed,read,accepted)==Result::Missing && accepted==0xDEADBEEF,"same main retiring child during getter refuses stale root");
    check(inspected.back()!=0x4000,"retired root not inspected after getter");
    graph[0x2000]={0x4000}; getters=0;
    auto broken=[&] { if (++getters==2) graph[0x4000]={0x1000}; return uintptr_t(0x1000); };
    check(sui_tree_root::resolve(0x2000,broken,read,accepted)==Result::InvalidGraph,"second snapshot graph failure refuses dump");
}
}
int main()
{
    try { parsing(); membership(); std::printf("Sui tree root: %u checks passed\n",checks); return 0; }
    catch (const std::exception& error) { std::printf("FAIL: %s\n",error.what()); return 1; }
}
