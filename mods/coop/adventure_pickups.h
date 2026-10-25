// Collector-only native adventure upgrades and independent teleporters.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <tuple>

#include "net/session.h"
#include "sporekit.h"

namespace adventure_pickups {
struct Identity {
    uint32_t marker = 0, incarnation = 0;
    bool operator<(const Identity& b) const { return std::tie(marker,incarnation)<std::tie(b.marker,b.incarnation); }
};
struct Ctx {
    net::Session* sess = nullptr;
    std::function<uint64_t()> run;
    std::function<int()> act;
    std::function<bool()> active, held;
    std::function<double()> simulation_time; // shared seconds, unchanged during group holds
    std::function<void*(uint8_t)> captain_of;
    std::function<bool(uint8_t)> captain_can_act;
    std::function<bool(void*,Identity&)> identity;
    std::function<void*(Identity)> object_for;
    std::function<void(Identity)> retire; // tombstone the current host identity on every peer
    std::function<bool(uint8_t,float)> captain_heal;
    // Called on every peer, including observers, to fence older movement packets.
    std::function<void(uint8_t,uint64_t,const net::Vec3&,const net::Quat&)> teleport;
    std::function<void(const std::string&)> failed; // cancel safely outside the packet iterator
};
bool init(const SkHost*, SkMod*, Ctx);
bool available();
void tick();
void on_message(uint8_t from,net::Reader&); // owns subkinds 152..155
void reset(); // outside the frame; restores only fields owned by this module
bool activate(void* object,void* captain); // native contact bridge, outside the frame
float adjust_damage(uint8_t recipient,float amount); // host captain authority, once per hit
std::string status();
} // namespace adventure_pickups
