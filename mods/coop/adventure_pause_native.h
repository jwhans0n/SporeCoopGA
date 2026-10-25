#pragma once

#include <functional>

#include "sporekit.h"

namespace adventure_pause_native {
struct Ctx {
    std::function<bool()> active;
    std::function<bool()> held;
    std::function<void(bool)> local_intent;
    std::function<void()> reset_movement;
};
bool init(const SkHost*, SkMod*, Ctx);
bool local_intent();
void changed();
// Invalidate queued native work and restore ordinary pause semantics once the
// policy has been reset. Native reason counters are never consumed or cleared.
void reset();
} // namespace adventure_pause_native
