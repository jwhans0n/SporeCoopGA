// Shared native HP tuning rule, separated from engine calls for regression testing.
#pragma once

#include <algorithm>
#include <cmath>

namespace ga {

struct HealthScale {
    int act = -1;
    float original = 0, applied = 0, multiplier = 0;

    template<class Maximum>
    bool apply(int current_act, float& tuning, float& health, float factor, Maximum maximum)
    {
        if (act == current_act && factor == multiplier) return false;
        float before = maximum(), previous = tuning;
        if (!std::isfinite(tuning) || tuning <= 0 || !std::isfinite(before) || before <= 0 ||
            !std::isfinite(health) || !std::isfinite(factor) || factor <= 0) return false;
        // An act may preserve our tuning or reload an authored value.
        float baseline = tuning;
        if (act >= 0 && std::fabs(tuning - applied) <= std::max(1e-5f, std::fabs(tuning) * 1e-5f)) baseline = original;
        float scaled = baseline * factor;
        if (!std::isfinite(scaled) || scaled <= 0) return false;
        tuning = scaled;
        float after = maximum();
        if (!std::isfinite(after) || after <= 0) { tuning = previous; return false; }
        health = std::clamp(health / before, 0.f, 1.f) * after;
        act = current_act; original = baseline; applied = scaled; multiplier = factor;
        return true;
    }
};

} // namespace ga
