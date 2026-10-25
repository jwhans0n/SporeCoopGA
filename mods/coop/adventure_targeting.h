#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "sporekit.h"

namespace adventure_targeting {

struct Captain {
    void* object = nullptr;
    uint8_t player = 0;
};

struct Ctx {
    // Running host native adventure with no scene or unanimous party hold.
    std::function<bool()> active;
    // Exact live native actor in the current run/act, excluding captains.
    std::function<bool(void*)> live_npc;
    // Current, participating, alive captains with exact owned native pointers.
    std::function<std::vector<Captain>()> captains;
    // Current remote captain identity, including downed captains. Alive eligibility
    // separately requires membership in captains(); arbitrary puppets never qualify.
    std::function<bool(void*)> remote_captain;
};

bool init(const SkHost* host, SkMod* mod, Ctx ctx);
bool available();
void reset();
std::string status();
} // namespace adventure_targeting
