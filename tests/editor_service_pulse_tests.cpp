#include "editor_service_pulse.h"
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <vector>
using namespace editor_service_pulse;
static unsigned checks;
#define CHECK(x) do {++checks;if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
static Time at(int ms){return Time{}+std::chrono::milliseconds(ms);}
int main(){
    Observation ready{7,true,Scope{}};ready.editor->words[0]=1;
    Run run;Ticket ticket;
    CHECK(!run.reserve(at(100),ticket));
    run.publish(ready,at(100));CHECK(run.reserve(at(100),ticket)&&ticket.editor);
    CHECK(run.current(ticket,ready,at(100)));
    for(int i=0;i<100;++i)CHECK(!run.reserve(at(100+i),ticket));
    const auto old=ticket;run.complete(ticket.serial);
    CHECK(!run.reserve(at(104),ticket));CHECK(run.reserve(at(105),ticket));
    run.complete(old.serial);CHECK(!run.reserve(at(120),ticket));
    for(size_t word=0;word<ready.editor->words.size();++word){
        auto changed=ready;++changed.editor->words[word];CHECK(!run.current(ticket,changed,at(110)));
    }
    auto changed=ready;++changed.transport;CHECK(!run.current(ticket,changed,at(110)));
    changed=ready;changed.editor.reset();CHECK(!run.current(ticket,changed,at(110)));
    run.complete(ticket.serial);CHECK(run.reserve(at(110),ticket)); // failed enqueue retry
    run.complete(ticket.serial);
    CHECK(run.reserve(at(600),ticket)&&!ticket.editor); // original no-frame fallback
    CHECK(run.current(ticket,ready,at(600)));run.complete(ticket.serial);
    CHECK(!run.reserve(at(699),ticket));CHECK(run.reserve(at(700),ticket)&&!ticket.editor);
    run.publish(ready,at(701));CHECK(!run.current(ticket,ready,at(701)));run.complete(ticket.serial);
    CHECK(run.reserve(at(705),ticket)&&ticket.editor);
    run.retire();CHECK(!run.current(ticket,ready,at(705)));run.complete(ticket.serial);CHECK(!run.reserve(at(900),ticket));
    Run fallback;fallback.publish({},at(100));CHECK(!fallback.reserve(at(599),ticket));
    CHECK(fallback.reserve(at(600),ticket)&&!ticket.editor);fallback.complete(ticket.serial);
    Run concurrent;concurrent.publish(ready,at(100));std::atomic<unsigned> winners=0;
    std::vector<std::thread> threads;
    for(unsigned i=0;i<16;++i)threads.emplace_back([&]{Ticket value;if(concurrent.reserve(at(100),value))++winners;});
    for(auto& thread:threads)thread.join();CHECK(winners==1);
    bool busy=false;
    {ServiceGuard outer(busy);CHECK(outer.entered&&busy);{ServiceGuard nested(busy);CHECK(!nested.entered&&busy);}CHECK(busy);}
    CHECK(!busy);
    Run waiting;waiting.publish(ready,Clock::now());
    std::thread waiter([&]{waiting.wait();});waiting.retire();waiter.join();CHECK(!waiting.alive.load());
    std::printf("editor service pulse: %u checks passed\n",checks);
}
