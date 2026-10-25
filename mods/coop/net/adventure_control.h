// Small portable policies shared by native captain control and replication.
#pragma once

#include "adventure.h"
#include "protocol.h"

namespace ga {
// The legacy packet carries planet orientation, not a location-independent clock.
// Scenario applies its authored lighting relative to each local captain instead.
inline bool legacy_world_time(uint32_t mode) { return mode != 0x01654C10; }

inline bool hold_downed_movement(const Captain& captain, net::PlayerState& state)
{
    if (captain.life != Life::Downed) return false;
    state.pos = {captain.position.x, captain.position.y, captain.position.z};
    state.vel = {};
    state.flags &= ~(net::PF_MOVING | net::PF_RUNNING | net::PF_JUMPING | net::PF_DEAD);
    return true;
}
} // namespace ga
