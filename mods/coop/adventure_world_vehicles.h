#pragma once

#include <cstdint>
#include <functional>

#include "sporekit.h"

namespace adventure_world_vehicles {

struct Ctx {
    std::function<bool()> active;
    std::function<bool()> host;
    std::function<uint64_t()> run;
    // True only for live bound NPC mirrors. Exclude captains and dead actors so
    // native death cleanup continues. Receives cGameData*, never cCombatant*.
    std::function<bool(void*)> is_mirrored;
    std::function<bool()> paused = {};
};

struct Counters {
    uint64_t trees = 0;
    uint64_t weapons = 0;
    uint64_t timers = 0;
    uint64_t actors = 0;
};

// Installs narrow native hooks. The vehicle aggregate, initialization, model
// updates and final cleanup keep running. Capability failure removes all hooks
// installed by this helper and leaves available() false.
bool init(const SkHost* host, SkMod* mod, Ctx ctx);
bool available();
Counters counters();
void reset();

} // namespace adventure_world_vehicles
