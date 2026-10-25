// Downed captain input fences. Native visual updates continue while controls are held.
#pragma once

#include <functional>
#include <string>

#include "net/adventure.h"
#include "sporekit.h"

namespace adventure_control {
struct Ctx {
    std::function<bool()> active;
    std::function<void*()> local_avatar;
    std::function<bool(void*, ga::Captain&)> captain_state;
    std::function<bool()> local_paused;
    // The outcome remains authoritative after Running and captain state end.
    // The adapter scopes this to its connected native Scenario attempt.
    std::function<bool()> terminal_pending;
};
bool init(const SkHost* host, SkMod* mod, Ctx context);
// Called before and after the real native Creature::Update, never by filter queries.
void creature_update(void* creature, bool before);
void reset();
std::string status();
} // namespace adventure_control
