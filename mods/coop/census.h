// Census of the shared objects (tests; docs/06-testing.md#testing-census).
//
// `coop.census` prints, on the host and on a guest, the objects the co-op shares in the current mode
// as the game has them: creatures (Creature stage: the avatar, NPCs of the host with their net id,
// proxies of a guest with the host's net id, puppets, the guest's own NPCs) and civilization
// vehicles (net id, owner). tools/tests/census.py parses the dump, matches the host's objects with
// the guest's by net id and lists what differs, and tells which objects moved between two dumps of
// one machine (a frozen proxy, BUG-0205). Read-only.
//
// Line format (one object per line):
//   census v1 role=<host|guest|none> me=<id> mode=<name> modeid=<hex> world=<hex> t=<s>
//   obj <kind><id> key=<hex> alive=<0|1> hp=<f> ai=<0|1|-1> own=<who> owner=<player|-1> pos=(x y z)
//   end objects=<n>
// kind: c creature, v vehicle; id: the net id (-1 none: the avatar, a local object).
// own (creatures): avatar, npc (host), proxy (guest), puppet (a player's), local (no net id).
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "net/protocol.h"
#include "net/session.h"
#include "sporekit.h"

namespace census {

struct Row {
    char kind = '?';
    int id = -1;
    uint32_t key = 0;
    int alive = 1;
    float hp = -1;
    int ai = -1;
    const char* own = "";
    int owner = -1;
    net::Vec3 pos{};
};

struct Ctx {
    net::Session* sess = nullptr;
    std::function<void(uint32_t& mode, uint32_t& world)> place; // g_mode, g_world
};

void init(const SkHost* host, SkMod* mod, Ctx ctx); // registers coop.census

} // namespace census

namespace civ {
void census_rows(std::vector<census::Row>& out); // the vehicles (civ.cpp)
}
