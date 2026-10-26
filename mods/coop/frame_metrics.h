#pragma once

#include <algorithm>
#include <array>
#include <cmath>
#include <cstddef>

namespace coop_metrics {

// Test instrumentation only. Recording performs no allocation, UI or resource
// work in the frame callback. The outside-frame command computes percentiles.
class FrameSamples {
public:
    static constexpr size_t capacity = 8192;
    struct Summary {
        size_t samples = 0;
        size_t invalid = 0;
        bool recording = false;
        double seconds = 0;
        double mean_fps = 0;
        double median_fps = 0;
        double median_ms = 0;
        double p95_ms = 0;
    };

    void start() { count_ = invalid_ = 0; previous_ = 0; primed_ = false; recording_ = true; }
    void stop() { recording_ = false; }
    void sample(double now) {
        if (!recording_) return;
        if (!std::isfinite(now)) { ++invalid_; primed_ = false; return; }
        if (!primed_) { previous_ = now; primed_ = true; return; }
        const double delta = now - previous_;
        previous_ = now;
        if (!(delta > 0) || !std::isfinite(delta)) { ++invalid_; return; }
        values_[count_++] = delta * 1000;
        if (count_ == capacity) recording_ = false;
    }
    Summary summary() const {
        Summary result;
        result.samples = count_; result.invalid = invalid_; result.recording = recording_;
        if (!count_) return result;
        auto sorted = values_;
        std::sort(sorted.begin(), sorted.begin() + count_);
        for (size_t i = 0; i < count_; ++i) result.seconds += sorted[i] / 1000;
        result.median_ms = count_ % 2 ? sorted[count_ / 2] : (sorted[count_ / 2 - 1] + sorted[count_ / 2]) / 2;
        result.p95_ms = sorted[(count_ * 95 + 99) / 100 - 1];
        result.median_fps = 1000 / result.median_ms;
        result.mean_fps = count_ / result.seconds;
        return result;
    }
private:
    std::array<double, capacity> values_{};
    size_t count_ = 0, invalid_ = 0;
    double previous_ = 0;
    bool recording_ = false, primed_ = false;
};

} // namespace coop_metrics
