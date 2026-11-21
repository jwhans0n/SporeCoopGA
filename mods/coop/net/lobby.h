// The host's lobby table (MSG_LOBBY): ready flag, round trip time and civilization role of every
// player. Portable so that `make test` checks the write and read order (BUG-0079).
//
// Layout: u8 n, n x (u8 id, u8 ready, u16 rtt ms or 0xFFFF), then n x u8 role in the same order
// (older guests stop reading before the roles).
#pragma once

#include <algorithm>
#include <cstdint>
#include <vector>

#include "buffer.h"

namespace net {

struct LobbyEntry {
    uint8_t id = 0;
    bool ready = false;
    int rtt = -1; // ms, -1 unknown
    uint8_t role = 0;
};

// fix: the entries go out sorted by id (a guest of an older build reads the roles in id order)
inline void write_lobby(Writer& w, std::vector<LobbyEntry> pl, bool fix)
{
    if (fix)
        std::sort(pl.begin(), pl.end(), [](const LobbyEntry& a, const LobbyEntry& b) { return a.id < b.id; });
    w.u8((uint8_t)pl.size());
    for (auto& p : pl) {
        w.u8(p.id);
        w.u8(p.ready ? 1 : 0);
        w.u16((uint16_t)(p.rtt < 0 || p.rtt > 0xFFFE ? 0xFFFF : p.rtt));
    }
    for (auto& p : pl)
        w.u8(p.role);
}

// false when the main table is cut short; has_roles false when the roles are missing (old host).
// fix: the roles belong to the ids in the order they were written; !fix reproduces the reading
// before BUG-0079 (roles taken in ascending id order, wrong after an id was reused).
inline bool read_lobby(Reader& r, std::vector<LobbyEntry>& out, bool& has_roles, bool fix)
{
    out.clear();
    has_roles = false;
    uint8_t n = r.u8();
    for (int i = 0; i < n && r.ok(); i++) {
        LobbyEntry e;
        e.id = r.u8();
        e.ready = r.u8() != 0;
        uint16_t t = r.u16();
        e.rtt = t == 0xFFFF ? -1 : t;
        out.push_back(e);
    }
    if (!r.ok())
        return false;
    if (r.left() < out.size())
        return true;
    has_roles = true;
    if (fix) {
        for (auto& e : out)
            e.role = r.u8();
    } else {
        std::vector<LobbyEntry*> by_id;
        for (auto& e : out)
            by_id.push_back(&e);
        std::stable_sort(by_id.begin(), by_id.end(), [](LobbyEntry* a, LobbyEntry* b) { return a->id < b->id; });
        for (auto* e : by_id)
            e->role = r.u8();
    }
    return r.ok();
}

} // namespace net
