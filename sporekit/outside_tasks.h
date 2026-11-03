#pragma once
#include <cstdint>
#include <deque>
#include <functional>
#include <mutex>
#include <optional>
#include <stdexcept>

namespace sk::outside_tasks {
// Values stay in FIFO ownership until invocation. A batch's sequence boundary
// also survives a nested pump consuming some of its original entries.
class Queue {
    struct Entry {uint64_t sequence;std::function<void()> task;};
    mutable std::mutex mutex_;
    std::deque<Entry> tasks_;
    uint64_t sequence_=0;
public:
    void push(std::function<void()> task){
        std::lock_guard lock(mutex_);
        if(sequence_==UINT64_MAX)throw std::overflow_error("Outside task sequence exhausted");
        const auto next=sequence_+1;tasks_.push_back({next,std::move(task)});sequence_=next;
    }
    uint64_t boundary()const{std::lock_guard lock(mutex_);return tasks_.empty()?0:tasks_.back().sequence;}
    size_t size()const{std::lock_guard lock(mutex_);return tasks_.size();}
    std::optional<std::function<void()>> take(uint64_t boundary){
        std::lock_guard lock(mutex_);
        if(tasks_.empty() || tasks_.front().sequence>boundary)return {};
        auto task=std::move(tasks_.front().task);tasks_.pop_front();return task;
    }
};
struct Result {uint32_t processed=0;bool cap_reached=false,budget_reached=false;};
// Never interrupts a callback. New work waits for another pump, even if it is
// posted faster than old work completes. The original count cap is retained.
template<class Clock> void pump(Queue& queue,Result& result,Clock clock,uint64_t budget_ticks){
    const auto boundary=queue.boundary(),begin=clock();
    while(result.processed<256){
        auto task=queue.take(boundary);if(!task)return;
        ++result.processed;(*task)();task.reset();
        const auto end=clock();
        if(result.processed==256){result.cap_reached=true;return;}
        if(budget_ticks && end>=begin && end-begin>=budget_ticks){result.budget_reached=true;return;}
    }
}
}
