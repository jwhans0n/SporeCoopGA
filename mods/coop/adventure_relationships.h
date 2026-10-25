#pragma once

#include <cstdint>
#include <functional>

#include "sporekit.h"

namespace adventure_relationships {

struct Ctx {
    // True only for a committed cooperative attempt in the native Scenario mode.
    std::function<bool()> active;
    // Only registered, live remote captains. Never authored NPCs or arbitrary puppets.
    std::function<bool(void*)> remote_captain;
    // The real local avatar, independent of temporary native GetAvatar overrides.
    std::function<void*()> local_avatar;
    // Current host snapshot for one exact live mirrored NPC. False preserves
    // native evaluation; only an NPC-versus-party query may use this value.
    std::function<bool(void*, int&)> authoritative_relationship;
};

// Preserves native relationship evaluation while resolving remote captains as
// members of the player's party. No flags, objects or relationship records change.
bool init(const SkHost* host, SkMod* mod, Ctx ctx);
bool available();
uint64_t aliased_calls();
uint64_t authoritative_calls();
// Host snapshot capture through the original native evaluator, with the actual
// local avatar. The caller supplies a verified manager and live creature/key.
bool query_local_party(void* manager, const void* species_key, void* npc, int& value);
void reset();

} // namespace adventure_relationships
