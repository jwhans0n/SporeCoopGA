// Native adventure actor and goal replication. Uses the adventure run's host authority.
#pragma once

#include <functional>
#include <string>

#include "net/adventure.h"
#include "net/session.h"
#include "sporekit.h"
#include "adventure_flow.h"
#include "adventure_pickups.h"
#include "adventure_explosives.h"

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
    // Protects existing native dialog resources; an unpresented future Talk may
    // need its authoritative act batch before it can open.
    std::function<bool()> goals_held;
    std::function<bool()> explosive_environment, explosive_visual;
    std::function<double()> simulation_time;
    std::function<void(uint8_t,uint64_t,const net::Vec3&,const net::Quat&)> teleport;
    std::function<void(const std::string&)> interaction_failed;
};

bool init(const SkHost* host, SkMod* mod, Ctx ctx);
bool available();
void tick(double now);
void on_message(uint8_t from, net::Reader& payload); // subkinds 128..191, consumes subkind itself
void reset();
void peer_changed(uint8_t player);
adventure_pickups::Ctx pickups_context();
adventure_explosives::Ctx explosives_context();
bool npc_ai_filter(void* creature); // filter autonomous AI, preserving the enclosing native Update
// Observe native visual clips only. Replication never replays abilities or damage.
void on_animation(void* creature, uint32_t animation, int block, bool disable_blend, bool to_target);
bool is_live_mirror(void* actor); // current guest binding only; no native updates or state changes
bool party_relationship(void* actor, int& value); // current live guest NPC's host-authored party relationship
bool damage_filter(void* victim, float& damage, int type, void* attacker);
bool source(void* victim, void* attacker, int type, ga::Source& out);
void begin_native_attempt(void* play); // call before native Initialize, including solo/retry
void apply_result(bool success); // authoritative native result, outside the frame
bool validate_talk(uint8_t player, const adventure_flow::Scene&);
bool present_talk(const adventure_flow::Scene&);
void release_talk(const adventure_flow::Scene&);
// Read-only, run-scoped native goal work. The coordinator retains a received
// result until this validated queued/deferred batch has applied outside frame.
bool goal_application_pending();
std::string status(bool verbose = false);
int debug(int argc, const char* const* argv, SkOut* output); // outside frame; argv[0] is the subcommand

} // namespace adventure_world
