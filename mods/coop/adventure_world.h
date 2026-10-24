// Native adventure actor and goal replication. Uses the adventure run's host authority.
#pragma once

#include <functional>
#include <string>

#include "net/adventure.h"
#include "net/session.h"
#include "sporekit.h"
#include "adventure_flow.h"

namespace adventure_world {

struct Ctx {
    net::Session* sess = nullptr;
    std::function<void(const std::string&)> feed;
    std::function<uint64_t()> run;
    std::function<bool()> active;
    std::function<void*(uint8_t)> captain_of;
    // Host-authoritative life/participation check, including downed/disconnected captains.
    std::function<bool(uint8_t)> captain_can_act;
    std::function<ga::Difficulty()> difficulty;
    std::function<size_t()> party_size;
    // Native hostility must be established by the runtime, not inferred from a class index.
    std::function<bool(void*)> hostile_to_party;
    std::function<bool(uint8_t, const ga::Source&, float)> captain_damage;
    std::function<bool(uint8_t, float)> captain_heal;
    std::function<void(bool success)> result;
    std::function<bool()> held;
};

bool init(const SkHost* host, SkMod* mod, Ctx ctx);
bool available();
void tick(double now);
void on_message(uint8_t from, net::Reader& payload); // subkinds 128..191, consumes subkind itself
void reset();
bool update_filter(void* creature);
bool is_live_mirror(void* actor); // current guest binding only; no native updates or state changes
bool party_relationship(void* actor, int& value); // current live guest NPC's host-authored party relationship
bool damage_filter(void* victim, float& damage, int type, void* attacker);
bool source(void* victim, void* attacker, int type, ga::Source& out);
void begin_native_attempt(void* play); // call before native Initialize, including solo/retry
void apply_result(bool success); // authoritative native result, outside the frame
bool validate_talk(uint8_t player, const adventure_flow::Scene&);
bool present_talk(const adventure_flow::Scene&);
void release_talk(const adventure_flow::Scene&);
std::string status(bool verbose = false);
int debug(int argc, const char* const* argv, SkOut* output); // outside frame; argv[0] is the subcommand

} // namespace adventure_world
