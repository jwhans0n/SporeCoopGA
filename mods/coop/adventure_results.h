// Native result acceptance before a coordinated party exit. Runs outside frame.
#pragma once

#include <functional>
#include <string>
#include "sporekit.h"

namespace adventure_results {
struct Ctx {
    std::function<uint64_t()> run;
    std::function<bool(uint64_t, bool)> applied;
    std::function<void(const std::string&)> feed;
};
bool init(const SkHost*, SkMod*, Ctx);
bool prepare_exit(); // waits for native result/reward UI; never invents a reward
void accepted(void* play, int choice); // native Saved choice, before coordinator routing
void reset();
std::string status();
}
