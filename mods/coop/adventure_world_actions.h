#pragma once

#include <functional>
#include <string>

#include "sporekit.h"

namespace adventure_world_actions {

struct Ctx {
    std::function<bool()> active; // committed native adventure only
    std::function<bool()> host;
    std::function<bool(void*)> local_captain;
    // Called for the local captain on host or guest, before native drop/delete.
    // Copy identities and send/queue here; mutation belongs outside Present.
    std::function<bool(void* captain, void* item, void* recipient)> request_give;
};

enum class GiveResult { Stored, ConsumedWithGoal, ConsumedWithoutGoal };

// Exact executable capability gate; installs only the container Give intent hook.
bool init(const SkHost* host, SkMod* mod, Ctx ctx);
bool available();

// Caller validates network ownership, run, act, placement identity and distance.
// Caller also supplies its native avatar override and recursion guard. Execute
// outside Present. give() accepts an item still held by captain or already dropped.
bool give(void* captain, void* recipient, void* item, std::string& error,
          GiveResult* result = nullptr);
bool recruit(void* target, std::string& error);

// Native authored class/act relationship, for non-creature scenario actors.
// False return means unavailable/malformed, not friendly. Does not infer a team.
bool hostile(void* data, void* object, int act, bool& result, std::string& error);

} // namespace adventure_world_actions
