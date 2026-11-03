#pragma once
#include <array>
#include <atomic>
#include <cstdint>
#include <mutex>

namespace sk::task_pump_metrics {
struct Ticket { uint64_t generation=0,frame=0,begin=0; };
struct Record {
    uint64_t frame=0,begin=0,end=0,queue_left=0;
    uint32_t processed=0;
    bool cap_reached=false;
};
struct Snapshot {
    bool armed=false;
    uint64_t generation=0,frequency=0,calls=0,processed=0,cap_hits=0,deferred=0;
    Record peak;
    std::array<Record,16> recent{};
    uint32_t count=0,next=0;
};
// Diagnostic values only. No task ownership or queue/scheduling decisions.
class Recorder {
    std::atomic<bool> armed_{false};
    mutable std::mutex mutex_;
    Snapshot value_;
public:
    bool armed()const noexcept{return armed_.load(std::memory_order_relaxed);}
    Snapshot start(uint64_t frequency){
        std::lock_guard lock(mutex_);
        const auto generation=value_.generation+1;
        value_={};value_.generation=generation;value_.frequency=frequency;value_.armed=true;
        armed_.store(true,std::memory_order_relaxed);return value_;
    }
    Snapshot stop(){
        std::lock_guard lock(mutex_);value_.armed=false;armed_.store(false,std::memory_order_relaxed);return value_;
    }
    Snapshot snapshot()const{std::lock_guard lock(mutex_);return value_;}
    Ticket begin(uint64_t frame,uint64_t clock)const{
        if(!armed())return {};
        std::lock_guard lock(mutex_);return value_.armed?Ticket{value_.generation,frame,clock}:Ticket{};
    }
    void defer(){
        if(!armed())return;
        std::lock_guard lock(mutex_);if(value_.armed)++value_.deferred;
    }
    void finish(Ticket ticket,uint64_t end,uint32_t processed,bool cap,uint64_t left){
        if(!ticket.generation || !armed())return;
        std::lock_guard lock(mutex_);
        if(!value_.armed || ticket.generation!=value_.generation || end<ticket.begin)return;
        const Record record{ticket.frame,ticket.begin,end,left,processed,cap};
        ++value_.calls;value_.processed+=processed;value_.cap_hits+=cap;
        if(value_.calls==1 || end-ticket.begin>value_.peak.end-value_.peak.begin)value_.peak=record;
        value_.recent[value_.next]=record;value_.next=(value_.next+1)%value_.recent.size();
        if(value_.count<value_.recent.size())++value_.count;
    }
};
}
