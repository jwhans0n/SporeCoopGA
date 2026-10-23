#pragma once

#include <cstdint>
#include <string>

#include "net/protocol.h"
#include "sporekit.h"

namespace adventure_world_lifecycle {

// These operations touch native scenario objects and must run outside Present.
bool init(const SkHost* host, SkMod* mod);
bool available();
bool remove(uint8_t* data, uint8_t* marker, void* object, std::string& error, void* retained = nullptr);
// If removing the primary incarnation, retained can promote a validated live
// sibling into that forward slot. Its transform and native state are preserved.
bool recreate(uint8_t* data, uint8_t* marker, int act, const net::Vec3& position,
              const net::Quat& orientation, std::string& error, void** recreated = nullptr,
              uint32_t expected_noun = 0);
// A nonzero expected_noun is an authoritative request for a distinct incarnation.
// It selects the native creature/container factory and permits a validated live
// forward-map owner to remain while the new actor gets its own reverse binding.

} // namespace adventure_world_lifecycle
