#include "frame_metrics.h"

#include <cstdio>
#include <limits>

int main() {
    int failures = 0, checks = 0;
    auto check = [&](bool okay) { ++checks; if (!okay) ++failures; };
    auto near = [](double a, double b) { return std::abs(a - b) < 1e-6; };
    coop_metrics::FrameSamples frames;
    frames.sample(1);
    check(frames.summary().samples == 0);
    frames.start(); frames.sample(1);
    check(frames.summary().samples == 0);
    double now = 1;
    for (size_t i = 1; i <= 100; ++i) { now += double(i) / 1000; frames.sample(now); }
    auto result = frames.summary();
    check(result.recording && result.samples == 100 && result.invalid == 0);
    check(near(result.median_ms, 50.5));
    check(near(result.p95_ms, 95));
    check(near(result.seconds, 5.05));
    check(near(result.mean_fps, 100 / 5.05));
    check(near(result.median_fps, 1000 / 50.5));
    frames.stop(); frames.sample(10);
    check(!frames.summary().recording && frames.summary().samples == 100);
    frames.start(); frames.sample(0); frames.sample(.020);
    check(frames.summary().samples == 1 && near(frames.summary().p95_ms, 20));
    frames.sample(.020); frames.sample(.010); frames.sample(std::numeric_limits<double>::infinity());
    frames.sample(1); frames.sample(1.020);
    result = frames.summary();
    check(result.samples == 2 && result.invalid == 3 && near(result.median_ms, 20));
    frames.start(); frames.sample(1);
    for (size_t i = 1; i <= coop_metrics::FrameSamples::capacity + 20; ++i) frames.sample(1 + double(i) / 100);
    result = frames.summary();
    check(result.samples == coop_metrics::FrameSamples::capacity && !result.recording);
    check(result.invalid == 0 && near(result.median_ms, 10) && near(result.p95_ms, 10));
    frames.start(); check(frames.summary().samples == 0 && frames.summary().invalid == 0);
    frames.sample(9); frames.sample(9.033);
    check(frames.summary().samples == 1 && near(frames.summary().median_ms, 33));
    std::printf("frame metrics: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
