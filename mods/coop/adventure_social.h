#pragma once

#include <cstdint>
#include <functional>

#include "sporekit.h"

namespace adventure_social {
struct Ctx {
    // Running, unfrozen guest co-op Scenario only.
    std::function<bool()> active;
    std::function<uint64_t()> run;
    std::function<int()> act;
    std::function<void*()> local_avatar;
    // Current binding, incarnation, act and live HP must all match.
    std::function<bool(void*)> mirrored;
    // Read-only native noun-registry membership. Used before stale-pointer reads
    // during cancellation; an address being readable is insufficient.
    std::function<bool(void*)> listed;
};
struct Counters { uint64_t starts = 0, ticks = 0, ends = 0, rejected = 0, input_queries_allowed = 0; };

bool init(const SkHost* host, SkMod* mod, Ctx ctx);
bool available();
// Call only from game::h_Update's rejected-update handler. Native autonomous
// behavior, combat, movement and the complete creature Update stay suppressed.
void blocked_update(void* creature, int milliseconds);
// Native exit is called once for each still-owned callback before forgetting it.
void reset();
Counters counters();
} // namespace adventure_social
