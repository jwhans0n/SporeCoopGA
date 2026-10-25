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
};
bool init(const SkHost* host, SkMod* mod, Ctx context);
// Called before and after the real native Creature::Update, never by filter queries.
void creature_update(void* creature, bool before);
void reset();
std::string status();
} // namespace adventure_control
