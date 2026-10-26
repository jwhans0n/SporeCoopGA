#pragma once

#include <array>
#include <cmath>
#include <optional>
#include <string>

#include "net/editor_document.h"

namespace editor_metrics {

// Opt-in, bounded diagnostic measurements. Called only outside the frame.
// Start at Controller::submit, after native inverse settlement and lease gating;
// finish at this author's accepted canonical decision after native/durable apply.
class Confirmations {
public:
    static constexpr size_t capacity = 256;
    void reset() { *this = {}; recording_ = true; }
    void stop() { recording_ = false; pending_.reset(); }
    void begin(editor::OperationId operation, double seconds) {
        if (recording_ && count_ < capacity && std::isfinite(seconds)) pending_ = Pending{operation,seconds};
    }
    void cancel(editor::OperationId operation) { if (pending_ && pending_->operation == operation) pending_.reset(); }
    void complete(const editor::Decision& decision, double seconds) {
        if (!pending_ || !decision.change || pending_->operation != decision.change->operation) return;
        const auto elapsed = (seconds-pending_->started)*1000;
        pending_.reset();
        if (recording_ && decision.accepted() && std::isfinite(elapsed) && elapsed >= 0 && count_ < capacity) {
            milliseconds_[count_++] = elapsed;
            if (count_ == capacity) recording_ = false;
        }
    }
    std::string report() const {
        std::string result = "samples=" + std::to_string(count_) + " recording=" + std::to_string(recording_) + " capacity=" + std::to_string(capacity) + "\nconfirmation_ms=";
        for (size_t i=0;i<count_;++i) { if (i) result += ","; result += std::to_string(milliseconds_[i]); }
        return result;
    }
private:
    struct Pending { editor::OperationId operation; double started; };
    std::optional<Pending> pending_;
    std::array<double,capacity> milliseconds_{};
    size_t count_ = 0;
    bool recording_ = false;
};

} // namespace editor_metrics
