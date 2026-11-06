// Net ids of the host's NPCs (npc.cpp host_tick), portable for the unit tests (tests/npc_tests.cpp).
#pragma once

#include <cstdint>

namespace npc {

// Ids run 1..0xFFFE: 0 is never given, 0xFFFF means "no NPC" in MSG_DAMAGE (attacker).
constexpr uint16_t kFirstNpcId = 1, kLastNpcId = 0xFFFE;

// The next id after `next` that `in_use(id)` says is free, advancing `next` past it (BUG-0023:
// `next++` wrapped after about 65k allocations in one world and gave a living NPC's id to a second
// creature). Returns 0 when every id is in use. `skip_used` false keeps the old wrap (tests).
template <class InUse> uint16_t alloc_npc_id(uint16_t& next, InUse in_use, bool skip_used = true)
{
    if (next < kFirstNpcId || next > kLastNpcId)
        next = kFirstNpcId;
    for (uint32_t tries = 0; tries < kLastNpcId; tries++) {
        uint16_t id = next;
        next = next >= kLastNpcId ? kFirstNpcId : (uint16_t)(next + 1);
        if (!skip_used || !in_use(id))
            return id;
    }
    return 0;
}

// A known creature object that now is another creature: the game frees creatures into a pool and
// builds new ones in the same object (cCreatureAnimal::Create, reuse), so the pointer alone does
// not name an NPC (BUG-0044). Another species, or alive again after it was dead, is a new NPC.
inline bool npc_object_reused(uint32_t old_species, bool old_dead, uint32_t species, bool dead)
{
    return species != old_species || (old_dead && !dead);
}

} // namespace npc
