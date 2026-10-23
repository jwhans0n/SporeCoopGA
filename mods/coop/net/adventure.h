// Portable Galactic Adventures rules. No game addresses or native object layouts.
#pragma once

#include <array>
#include <cstdint>
#include <map>
#include <set>
#include <utility>
#include <vector>

#include "dbpf.h"
#include "buffer.h"

namespace ga {

constexpr uint8_t kNoPlayer = 255;
constexpr size_t kPartyLimit = 4;
constexpr float kReviveDistance = 2.f, kReviveHealth = 0.5f;
constexpr double kReviveSeconds = 3.0, kAutomaticSeconds = 30.0, kGraceSeconds = 2.0;

enum class DeathMode : uint8_t { ManualRevive, AnyDeathFails, AutomaticRevive };

struct Difficulty {
    DeathMode death_mode = DeathMode::ManualRevive;
    // Zero selects the party default; a nonzero value replaces that default.
    float enemy_health_override = 0, enemy_damage_override = 0;
    uint8_t friendly_fire_percent = 0; // 0, 25, 50 or 100
    bool valid() const;
    float health_multiplier(size_t party_size) const;
    float damage_multiplier() const;
    void write(net::Writer& writer) const;
    bool read(net::Reader& reader);
};

using Hash = std::array<uint8_t, 32>;

struct Descriptor {
    uint64_t run_id = 0; // strictly increasing within one network session
    dbpf::Key adventure;
    Hash manifest_hash{};
    std::vector<uint8_t> roster; // immutable launch roster, includes host 0
    Difficulty difficulty;
    // A zero key permits each player's own captain; otherwise all must use this key.
    dbpf::Key locked_captain;
    bool valid() const;
    void write(net::Writer& writer) const;
    bool read(net::Reader& reader);
};

enum class LaunchPhase : uint8_t { Idle, Preparing, Loading, Running, Aborted };

struct Member {
    uint8_t player = kNoPlayer;
    bool connected = true, content = false, captain = false, ready = false, loaded = false;
    dbpf::Key captain_key;
};

class LaunchBarrier {
public:
    bool begin(const Descriptor& descriptor);
    bool ack_content(uint64_t run, uint8_t player, const dbpf::Key& adventure, const Hash& hash);
    bool ack_captain(uint64_t run, uint8_t player, const dbpf::Key& captain);
    bool set_ready(uint64_t run, uint8_t player, bool ready);
    bool begin_loading(uint64_t run);
    bool ack_loaded(uint64_t run, uint8_t player);
    bool commit(uint64_t run);
    bool disconnect(uint64_t run, uint8_t player);
    bool abort(uint64_t run);
    LaunchPhase phase() const { return phase_; }
    const Descriptor& descriptor() const { return descriptor_; }
    const std::vector<Member>& members() const { return members_; }
    bool all_ready() const;
    bool all_loaded() const;

private:
    Member* member(uint8_t player);
    Descriptor descriptor_;
    std::vector<Member> members_;
    LaunchPhase phase_ = LaunchPhase::Idle;
    uint64_t newest_run_ = 0;
};

struct Point {
    float x = 0, y = 0, z = 0;
};

enum class SourceKind : uint8_t { Environment, Captain, HostileActor, OtherActor };
struct Source {
    SourceKind kind = SourceKind::Environment;
    uint8_t owner = kNoPlayer; // retain captain ownership on projectiles and lingering effects
};

// Call once at the host after resolving source ownership. kNoPlayer permits NPC victims.
// Self damage and allied NPC attacks retain their authored amount.
float resolve_damage(const Difficulty& difficulty, size_t party_size, const Source& source,
                     uint8_t victim_owner, float authored_damage);

enum class Life : uint8_t { Alive, Downed, Disconnected };
enum class RunPhase : uint8_t { Idle, Playing, Won, Failed, Aborted };

struct CaptainSeed {
    uint8_t player = kNoPlayer;
    float max_health = 0;
    Point position;
};

struct Captain {
    uint8_t player = kNoPlayer;
    Life life = Life::Alive;
    float health = 0, max_health = 0;
    Point position;
    double downed_at = 0, protected_until = 0;
    uint8_t reviver = kNoPlayer;
    double revive_started = 0;
};

// Host-only authoritative model. The adapter applies resulting state to native objects,
// suppresses native independent deaths/results, and broadcasts snapshots/transitions.
// Times are monotonic host simulation seconds, never wall time or guest timestamps.
class Authority {
public:
    bool start(const Descriptor& descriptor, const std::vector<CaptainSeed>& captains, double now);
    bool tick(uint64_t run, double now);
    bool position(uint64_t run, uint8_t player, const Point& point);
    float damage(uint64_t run, uint64_t event, uint8_t victim, const Source& source, float authored_damage);
    float heal(uint64_t run, uint64_t event, uint8_t victim, float amount);
    bool revive(uint64_t run, uint8_t rescuer, uint8_t target, bool active);
    bool disconnect(uint64_t run, uint8_t player);
    bool finish(uint64_t run, bool success);
    bool abort(uint64_t run);
    // Native result/reward processing must claim once before invoking the game's reward path.
    bool claim_reward(uint64_t run, uint8_t player);

    bool define_objective(uint64_t run, uint8_t act, uint32_t objective, uint32_t required);
    bool advance_objective(uint64_t run, uint8_t act, uint32_t objective, uint64_t event, uint32_t amount = 1);
    uint32_t objective_count(uint8_t act, uint32_t objective) const;
    bool next_act(uint64_t run, uint8_t act);

    RunPhase phase() const { return phase_; }
    uint8_t act() const { return act_; }
    double now() const { return now_; }
    uint64_t revision() const { return revision_; }
    const Descriptor& descriptor() const { return descriptor_; }
    const std::vector<Captain>& captains() const { return captains_; }
    const Captain* captain(uint8_t player) const;

private:
    struct Objective {
        uint32_t count = 0, required = 0;
        std::set<uint64_t> events;
    };
    Captain* mutable_captain(uint8_t player);
    bool current(uint64_t run) const;
    bool can_revive(const Captain& rescuer, const Captain& target) const;
    void clear_revives();
    void interrupt_revives(uint8_t player);
    void check_survivors();
    void restore(Captain& captain);

    Descriptor descriptor_;
    std::vector<Captain> captains_;
    RunPhase phase_ = RunPhase::Idle;
    uint64_t newest_run_ = 0, revision_ = 0;
    uint8_t act_ = 0;
    double now_ = 0;
    std::set<std::pair<uint64_t, uint8_t>> health_events_;
    std::set<uint8_t> rewarded_;
    std::map<std::pair<uint8_t, uint32_t>, Objective> objectives_;
};

} // namespace ga
