#include "../sporekit/task_pump_metrics.h"
#include "../mods/coop/editor_service_pulse.h"
#include <cstdio>
#include <cstdlib>
#include <thread>
static unsigned checks;
#define CHECK(x) do{++checks;if(!(x)){std::fprintf(stderr,"FAIL %d: %s\n",__LINE__,#x);std::exit(1);}}while(0)
int main(){
    using namespace sk::task_pump_metrics;
    Recorder recorder;
    CHECK(!recorder.begin(7,10).generation);
    recorder.defer();recorder.finish({1,7,10},20,256,true,3);
    CHECK(!recorder.snapshot().calls&&!recorder.snapshot().deferred);
    CHECK(recorder.start(1000).armed);
    const auto stale=recorder.begin(7,10);
    recorder.start(1000);recorder.finish(stale,50,256,true,2);CHECK(!recorder.snapshot().calls);
    auto token=recorder.begin(8,100);recorder.finish(token,99,1,false,0);CHECK(!recorder.snapshot().calls);
    recorder.finish(token,120,3,false,0);recorder.defer();
    auto value=recorder.snapshot();CHECK(value.calls==1&&value.processed==3&&!value.cap_hits&&value.deferred==1&&value.count==1);
    token=recorder.begin(9,200);recorder.finish(token,800,256,true,12);
    value=recorder.snapshot();CHECK(value.cap_hits==1&&value.peak.frame==9&&value.peak.queue_left==12&&value.peak.processed==256&&value.peak.end-value.peak.begin==600);
    // Ring eviction cannot hide the peak/cap totals that diagnose a long pump.
    for(unsigned i=0;i<30;++i){token=recorder.begin(10+i,1000+i*10);recorder.finish(token,1001+i*10,1,false,0);}
    value=recorder.snapshot();CHECK(value.calls==32&&value.count==16&&value.peak.frame==9&&value.cap_hits==1);
    CHECK(value.recent[value.next].frame==24&&value.recent[(value.next+15)%16].frame==39);
    token=recorder.begin(40,2000);const auto stopped=recorder.stop();recorder.finish(token,9000,256,true,1);
    CHECK(!recorder.armed()&&recorder.snapshot().calls==stopped.calls);
    recorder.start(1000);CHECK(!recorder.snapshot().calls&&!recorder.snapshot().count&&!recorder.snapshot().cap_hits);
    // Concurrent status/control touches only diagnostic state, never task state.
    std::thread reader([&]{for(unsigned i=0;i<1000;++i)(void)recorder.snapshot();});
    for(unsigned i=0;i<1000;++i){token=recorder.begin(i,i);recorder.finish(token,i+1,1,false,0);}
    reader.join();CHECK(recorder.snapshot().calls==1000);
    using namespace editor_service_pulse;
    Run pulse;Observation ready{1,true,Scope{}};editor_service_pulse::Ticket ticket;
    const auto at=[](int ms){return Time{}+std::chrono::milliseconds(ms);};
    pulse.publish(ready,at(100));CHECK(pulse.reserve(at(100),ticket));pulse.complete(ticket.serial);
    CHECK(!pulse.metrics().editor_reserves);
    pulse.metrics("start");CHECK(pulse.reserve(at(105),ticket)&&ticket.editor);
    pulse.metric(Metric::Dispatch,true);pulse.metric(Metric::Busy,true);
    // Diagnostic resets leave the existing reservation and minimum interval intact.
    pulse.metrics("start");CHECK(!pulse.reserve(at(200),ticket));pulse.complete(ticket.serial);
    CHECK(pulse.reserve(at(200),ticket)&&ticket.editor);pulse.metric(Metric::Dispatch,true);pulse.metric(Metric::Update,true);pulse.metric(Metric::Tick,true);
    pulse.complete(ticket.serial);CHECK(pulse.reserve(at(600),ticket)&&!ticket.editor);
    pulse.metric(Metric::Dispatch,false);pulse.metric(Metric::Stale,false);pulse.complete(ticket.serial);
    const auto metrics=pulse.metrics("stop");
    CHECK(!metrics.armed&&metrics.editor_reserves==1&&metrics.generic_reserves==1&&metrics.editor[0]==1&&metrics.editor[3]==1&&metrics.editor[4]==1&&metrics.generic[0]==1&&metrics.generic[1]==1);
    pulse.metric(Metric::Tick,false);CHECK(pulse.reserve(at(700),ticket));pulse.complete(ticket.serial);
    CHECK(pulse.metrics().generic_reserves==1&&!pulse.metrics().generic[4]);
    pulse.metrics("start");CHECK(!pulse.metrics().editor_reserves&&!pulse.metrics().generic_reserves);
    pulse.retire();CHECK(!pulse.reserve(at(800),ticket));
    std::printf("task pump/pulse metrics: %u checks passed\n",checks);
}
