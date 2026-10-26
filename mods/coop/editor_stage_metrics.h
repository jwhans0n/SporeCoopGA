// Opt-in diagnostic events. Numeric recording performs no allocations or I/O.
#pragma once
#include <array>
#include <algorithm>
#include <cstdint>
#include <limits>
#include <string>
#include <type_traits>
#include "net/editor_document.h"

namespace editor_stage_metrics {
enum class Stage : uint8_t {
    LocalSubmit, HostRequestReceived, ValidateBegin, ValidateEnd,
    ApplyWorkBegin, ApplyWorkEnd, NativeApplyBegin, NativeApplyEnd,
    PersistWorkBegin, PersistWorkEnd, DurableBegin, DurableEnd,
    HostDecisionQueued, DecisionDecoded, OwnConfirmed
};
inline const char* name(Stage stage) noexcept {
    switch(stage) {
    case Stage::LocalSubmit:return "local_submit";
    case Stage::HostRequestReceived:return "host_request_received";
    case Stage::ValidateBegin:return "validate_begin";
    case Stage::ValidateEnd:return "validate_end";
    case Stage::ApplyWorkBegin:return "apply_work_begin";
    case Stage::ApplyWorkEnd:return "apply_work_end";
    case Stage::NativeApplyBegin:return "native_apply_begin";
    case Stage::NativeApplyEnd:return "native_apply_end";
    case Stage::PersistWorkBegin:return "persist_work_begin";
    case Stage::PersistWorkEnd:return "persist_work_end";
    case Stage::DurableBegin:return "durable_begin";
    case Stage::DurableEnd:return "durable_end";
    case Stage::HostDecisionQueued:return "host_decision_queued";
    case Stage::DecisionDecoded:return "decision_decoded";
    case Stage::OwnConfirmed:return "own_confirmed";
    }
    return "invalid";
}
struct Event {
    editor::ProjectId project{};
    editor::OperationId operation;
    editor::Revision revision = 0;
    uint64_t qpc = 0;
    Stage stage{};
    uint8_t success = 0; // Begin/neutral0, successful end1, failed end2.
};
static_assert(std::is_trivially_copyable_v<Event>);

template<size_t Capacity=4096> class Buffer {
public:
    static_assert(Capacity>0 && Capacity<=4096);
    bool reset(uint32_t pid,uint64_t frequency,uint64_t now) noexcept {
        if(!pid || !frequency || !now)return false;
        count_=overflow_=0;pid_=pid;frequency_=frequency;started_=last_=now;
        recording_=true;clock_error_=false;++capture_;return true;
    }
    void stop() noexcept { recording_=false; }
    bool recording() const noexcept { return recording_; }
    size_t size() const noexcept { return count_; }
    size_t overflow() const noexcept { return overflow_; }
    bool clock_error() const noexcept { return clock_error_; }
    const Event* at(size_t index) const noexcept { return index<count_?&events_[index]:nullptr; }
    bool record(editor::ProjectId project,editor::OperationId operation,editor::Revision revision,
                Stage stage,uint64_t qpc,uint8_t success=0) noexcept {
        if(!recording_)return false;
        bool valid_project=false;for(auto b:project)valid_project|=b!=0;
        if(!valid_project || operation.author.slot>=4 || !operation.author.generation ||
           !operation.sequence || !operation.epoch || unsigned(stage)>unsigned(Stage::OwnConfirmed) || success>2)return false;
        if(qpc<last_){clock_error_=true;recording_=false;return false;}
        if(count_==Capacity){++overflow_;recording_=false;return false;}
        last_=qpc;events_[count_++]={project,operation,revision,qpc,stage,success};return true;
    }
    // Formatting belongs only to an outside-frame console command. Pagination
    // bounds the console reply even when a complete diagnostic buffer is full.
    std::string report(size_t offset=0,size_t requested=32) const {
        requested=std::min<size_t>(requested,32);
        const auto first=std::min(offset,count_);const auto end=first+std::min(requested,count_-first);
        std::string out="clock=win32_qpc pid="+std::to_string(pid_)+" frequency="+std::to_string(frequency_)+
            " capture="+std::to_string(capture_)+" started_qpc="+std::to_string(started_)+
            " recording="+std::to_string(recording_)+" count="+std::to_string(count_)+
            " capacity="+std::to_string(Capacity)+" overflow="+std::to_string(overflow_)+
            " clock_error="+std::to_string(clock_error_)+" offset="+std::to_string(first)+" end="+std::to_string(end)+"\n";
        static constexpr char digits[]="0123456789abcdef";
        for(size_t i=first;i<end;++i){
            const auto& e=events_[i];std::string project;project.reserve(32);
            for(auto b:e.project){project+=digits[b>>4];project+=digits[b&15];}
            out+="event="+std::to_string(i)+" stage="+name(e.stage)+" qpc="+std::to_string(e.qpc)+" project="+project+
                " author="+std::to_string(e.operation.author.slot)+":"+std::to_string(e.operation.author.generation)+
                " sequence="+std::to_string(e.operation.sequence)+" operation_epoch="+std::to_string(e.operation.epoch)+
                " revision="+std::to_string(e.revision)+" success="+std::to_string(e.success)+"\n";
        }
        return out;
    }
private:
    std::array<Event,Capacity> events_{};
    size_t count_=0,overflow_=0;
    uint32_t pid_=0;
    uint64_t frequency_=0,started_=0,last_=0,capture_=0;
    bool recording_=false,clock_error_=false;
};
static_assert(sizeof(Buffer<>)<=512u*1024);
} // namespace editor_stage_metrics
