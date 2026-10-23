// Native Galactic Adventures adapter. Every engine entry is resolved by verified symbol.
#pragma once

#include "adventure.h"

namespace adventure_native {

struct Ctx {
    net::Session* sess = nullptr;
    std::function<void(const std::string&)> feed;
    std::function<void*(uint8_t)> captain_of;
    std::function<bool(uint8_t, net::PlayerState&)> remote_state;
    std::function<void()> invalidate_ready;
};

bool init(const SkHost* host, SkMod* mod, Ctx context);
adventure::Ctx coordinator_context();
void tick(double now);
void on_message(uint8_t from, net::Reader& message);
void reset();
void shutdown();
bool update_filter(void* creature);
void blocked_update(void* creature, int milliseconds);
bool damage_filter(void* victim, float& amount, int type, void* attacker);
std::string status();

} // namespace adventure_native
