// Host-owned native GA explosives. Native callbacks execute outside Present.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <tuple>

#include "net/session.h"
#include "sporekit.h"

namespace adventure_explosives {

struct Identity {
    uint32_t marker = 0, incarnation = 0;
    bool operator<(const Identity& b) const { return std::tie(marker,incarnation)<std::tie(b.marker,b.incarnation); }
};
struct Ctx {
    net::Session* sess = nullptr;
    std::function<uint64_t()> run;
    std::function<uint8_t()> act;
    std::function<bool()> active, held;
    std::function<bool(void*,Identity&)> identify;
    std::function<void*(Identity)> object;
    std::function<void*(void*)> powerup, combatant, from_spatial;
    std::function<void*(uint8_t)> captain;
    std::function<uint8_t(void*)> owner;
    std::function<bool(uint8_t)> can_act;
    // Current inventory ownership or a validated native drop in the last two seconds.
    std::function<bool(uint8_t,void*)> owns_throw;
    std::function<bool(void*,void*,float)> nearby;
    // World-space native spatial AABB (GetBoundingBox slot0x6C), including scale.
    std::function<bool(void*,net::Vec3&,net::Vec3&)> bounds;
    // Tombstone a guest identity and forget its snapshots/binding. No native removal.
    std::function<void(Identity)> retire;
    std::function<void(const std::string&)> failed;
    // Return true when the world adapter handled an ordinary native item throw.
    std::function<bool(void*,void*,const net::Vec3&,bool)> ordinary_throw;
    // Host-only accepted flight. Restores native source/grace; only timed bombs
    // enter the world's catch path and keep the same physical fuse.
    std::function<void(Identity,uint8_t)> explosive_thrown;
};

bool init(const SkHost*,SkMod*,Ctx);
bool available();
void tick();
void reset();
void on_message(uint8_t from,net::Reader&); // consumes subkind144..151
// Called only after trusted native OnPicked bookkeeping for a captain. A new
// timed-bomb pickup permits another throw without restarting its running fuse.
void picked(Identity,uint8_t player);
// During native host blast/activation, damage attribution is environmental and
// must bypass the usual victim-client origin filter. Guest blast damage is suppressed.
bool environmental_damage();
bool visual_only();
std::string status();

} // namespace adventure_explosives
