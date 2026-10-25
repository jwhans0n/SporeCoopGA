#pragma once
#include <functional>
#include <string>
#include <vector>
#include "sporekit.h"

namespace adventure_hud {
struct Player { uint8_t id=0; std::string name; bool local=false; };
struct Ctx {
    std::function<std::vector<Player>()> players;
    std::function<bool()> blocked;
};
void init(const SkHost*, SkMod*, Ctx);
void tick(); // schedules native UI work outside Present
void draw(); // SK_EV_OVERLAY only, uses cached state and restores all D3D state
void shutdown();
std::string status();
}
