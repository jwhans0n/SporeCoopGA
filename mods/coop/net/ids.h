// Net ids of host objects (ships, vehicles, tribe members, NPCs): u16, 0 = none (portable).
#pragma once

#include <cstdint>

namespace net {

// BUG-0154: the next free id. `next++` wrapped after 65 535 allocations onto ids still in use (two
// objects under one id: the guest's mirror jumped between them, orders reached the wrong one), and
// one caller could hand out 0. Skips 0 and every id in `in_use` (anything with count(id)); returns
// 0 only when all 65 535 ids are taken.
template <class Map>
uint16_t alloc_id(uint16_t& next, const Map& in_use)
{
    for (uint32_t tries = 0; tries < 0xFFFF; tries++) {
        uint16_t id = next++;
        if (!next)
            next = 1;
        if (id && !in_use.count(id))
            return id;
    }
    return 0;
}

} // namespace net
