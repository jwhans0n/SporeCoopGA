// Portable production admission core. No game, process, database or profile access.
#include "../mods/coop/package_admission.h"
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <limits>

namespace pa=package_admission;
static unsigned checks;
#define CHECK(...) do {++checks;if(!(__VA_ARGS__)){std::printf("FAIL %d: %s\n",__LINE__,#__VA_ARGS__);std::exit(1);}} while(0)
pa::Hash hash(unsigned n){pa::Hash h{};for(unsigned i=0;i<4;++i)h[i]=uint8_t(n>>(8*i));return h;}
struct Fixture {
    pa::Identity identity{123,0,456,hash(7)};
    pa::Ledger ledger{};
    pa::Core owner;
    const char* error=nullptr;
    unsigned native_calls=0;
    Fixture(){pa::Core::initialize(ledger,identity,10);CHECK(owner.attach(ledger,identity,error));}
    void boot(){CHECK(owner.event(pa::Event::PreInit,error));CHECK(owner.event(pa::Event::AppInit,error));CHECK(owner.event(pa::Event::PostInit,error));CHECK(owner.bind_profile(hash(8),error));}
    bool add(unsigned n,uint32_t bytes=1){pa::Ticket ticket;if(!owner.begin(hash(n),{n,2,3},bytes,false,ticket,error))return false;
        CHECK(ledger.count&&ledger.entries[ticket.index].state==pa::State::Reserved);++native_calls;return owner.finish(ticket,true,error);}
};
void count_bytes()
{
    Fixture f;f.boot();for(unsigned i=1;i<=pa::kMaxPackages;++i)CHECK(f.add(i));
    CHECK(!f.add(33));CHECK(f.native_calls==32);CHECK(f.ledger.count==32);CHECK(f.ledger.bytes==32);
    CHECK(f.error&&std::strstr(f.error,"limit"));
    Fixture large;large.boot();CHECK(large.add(1,128u<<20));
    for(unsigned i=2;i<=5;++i)CHECK(large.add(i,16u<<20));
    CHECK(!large.add(6,128u<<20));CHECK(large.native_calls==5);CHECK(large.ledger.bytes==192u<<20);
    Fixture exact;exact.boot();CHECK(exact.add(1,128u<<20));CHECK(exact.add(2,128u<<20));CHECK(!exact.add(3));
    CHECK(exact.native_calls==2);CHECK(exact.ledger.bytes==pa::kMaxBytes);
}
void reuse_and_promotion()
{
    Fixture f;f.boot();CHECK(f.add(1));
    for(unsigned i=0;i<1000;++i){pa::Ticket ticket;CHECK(f.owner.begin(hash(1),{1,2,3},1,true,ticket,f.error));CHECK(ticket.reused);CHECK(f.owner.finish(ticket,true,f.error));}
    CHECK(f.native_calls==1&&f.ledger.count==1&&f.ledger.bytes==1);
    pa::Ticket ticket;CHECK(!f.owner.begin(hash(1),{1,2,3},1,false,ticket,f.error));
    CHECK(!f.owner.begin(hash(1),{2,2,3},1,true,ticket,f.error));CHECK(!f.owner.begin(hash(1),{1,2,3},2,true,ticket,f.error));
    CHECK(!f.owner.begin(hash(2),{2,2,3},1,true,ticket,f.error));
    CHECK(f.owner.begin(hash(1),{1,2,3},1,true,ticket,f.error));CHECK(!f.owner.finish(ticket,false,f.error));
    CHECK(f.ledger.count==1&&f.ledger.bytes==1&&f.ledger.flags&pa::kUncertain);CHECK(!f.add(2));
}
void lifecycle_and_reload()
{
    Fixture late;pa::Ticket ticket;CHECK(!late.owner.begin(hash(1),{1,2,3},1,false,ticket,late.error));
    CHECK(!late.owner.event(pa::Event::PostInit,late.error));CHECK(!late.add(1));
    Fixture f;f.boot();f.owner.detach();pa::Core empty;CHECK(empty.attach(f.ledger,f.identity,f.error));
    CHECK(empty.begin(hash(1),{1,2,3},1,false,ticket,f.error));CHECK(empty.finish(ticket,true,f.error));empty.detach();
    pa::Core reloaded;CHECK(reloaded.attach(f.ledger,f.identity,f.error));
    CHECK(!reloaded.begin(hash(1),{1,2,3},1,true,ticket,f.error));CHECK(!reloaded.begin(hash(2),{2,2,3},1,false,ticket,f.error));
    CHECK(reloaded.accounting().reloaded);CHECK(reloaded.accounting().count==1);reloaded.detach();
    Fixture shutdown;shutdown.boot();CHECK(shutdown.owner.event(pa::Event::Shutdown,shutdown.error));CHECK(!shutdown.add(1));
    Fixture interrupted;interrupted.boot();CHECK(interrupted.owner.begin(hash(1),{1,2,3},1,false,ticket,interrupted.error));
    CHECK(interrupted.owner.event(pa::Event::Shutdown,interrupted.error));CHECK(!interrupted.owner.finish(ticket,true,interrupted.error));
    CHECK(interrupted.ledger.count==1&&interrupted.ledger.entries[0].state==pa::State::Uncertain);
}
void tickets_failures_and_reentry()
{
    Fixture f;f.boot();pa::Ticket ticket,other;
    CHECK(f.owner.begin(hash(1),{1,2,3},77,false,ticket,f.error));
    CHECK(!f.owner.begin(hash(2),{2,2,3},1,false,other,f.error));CHECK(f.ledger.count==1&&f.ledger.bytes==77);
    auto forged=ticket;++forged.serial;CHECK(!f.owner.finish(forged,true,f.error));CHECK(f.ledger.entries[0].state==pa::State::Reserved);
    CHECK(!f.owner.finish(ticket,false,f.error));CHECK(f.ledger.entries[0].state==pa::State::Uncertain);
    CHECK(!f.add(2));CHECK(f.native_calls==0);CHECK(f.owner.accounting().uncertain==1);
    Fixture abandoned;abandoned.boot();CHECK(abandoned.owner.begin(hash(1),{1,2,3},1,false,ticket,abandoned.error));abandoned.owner.detach();
    CHECK(abandoned.ledger.flags&pa::kUncertain);CHECK(abandoned.ledger.entries[0].state==pa::State::Uncertain);pa::Core next;CHECK(next.attach(abandoned.ledger,abandoned.identity,abandoned.error));
    CHECK(!next.begin(hash(2),{2,2,3},1,false,other,abandoned.error));CHECK(!next.finish(ticket,true,abandoned.error));
}
void invalid_receipts()
{
    Fixture f;f.boot();CHECK(f.add(1));f.owner.detach();
    const auto original=f.ledger;
    for(unsigned corrupt=0;corrupt<15;++corrupt){auto p=original;auto identity=f.identity;
        switch(corrupt){
        case 0:++p.version;break;case 1:--p.size;break;case 2:++identity.pid;break;case 3:++identity.creation;break;
        case 4:identity.executable=hash(9);break;case 5:p.count=33;break;case 6:++p.bytes;break;
        case 7:p.entries[0].bytes=0;break;case 8:p.entries[0].root.type=0;break;case 9:p.entries[0].module=0;break;
        case 10:p.entries[0].state=static_cast<pa::State>(99);break;case 11:p.flags=0x80000000;break;
        case 12:p.attached_module=1;break;case 13:p.module_counter=std::numeric_limits<uint32_t>::max();break;case 14:p.owner_handle=0;break;
        }
        pa::Core owner;const char* error=nullptr;CHECK(!owner.attach(p,identity,error));CHECK(error);
    }
    Fixture profile;profile.boot();CHECK(profile.owner.bind_profile(hash(8),profile.error));CHECK(!profile.owner.bind_profile(hash(9),profile.error));
    CHECK(profile.ledger.profile==hash(8));CHECK(profile.ledger.flags&pa::kUncertain);CHECK(!profile.add(1));
    Fixture invalid;invalid.boot();pa::Ticket ticket;
    for(auto key:{pa::Key{0,2,3},pa::Key{1,0,3}})CHECK(!invalid.owner.begin(hash(1),key,1,false,ticket,invalid.error));
    CHECK(!invalid.owner.begin(hash(1),{1,2,3},0,false,ticket,invalid.error));
    CHECK(!invalid.owner.begin(hash(1),{1,2,3},pa::kMaxBundleBytes+1,false,ticket,invalid.error));
    CHECK(invalid.ledger.count==0);invalid.ledger.ticket_counter=std::numeric_limits<uint64_t>::max();CHECK(!invalid.add(1));
    CHECK(invalid.ledger.flags&pa::kUncertain);
}
int main(){count_bytes();reuse_and_promotion();lifecycle_and_reload();tickets_failures_and_reentry();invalid_receipts();
    std::printf("package admission core: %u checks passed; packages=%u payload_limit=%u ledger_bytes=%u\n",checks,pa::kMaxPackages,pa::kMaxBytes,unsigned(sizeof(pa::Ledger)));}
