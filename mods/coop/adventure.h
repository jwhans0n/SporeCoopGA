// GA lobby/run coordination. Native operations are supplied by the verified game adapter.
#pragma once

#include <functional>
#include <string>
#include <vector>

#include "net/adventure.h"
#include "net/session.h"
#include "sporekit.h"

namespace adventure {

using Key = dbpf::Key;
struct Ctx {
    net::Session* sess = nullptr;
    std::function<void(const std::string&)> feed;
    std::function<void()> invalidate_ready;
    std::function<bool()> native_available;
    std::function<std::string()> native_reason;
    std::function<bool(const Key&, std::vector<dbpf::Record>&, std::string&)> collect;
    // A zero locked key requests a personal captain. Return a zero key to cancel.
    std::function<void(const Key& locked, std::function<void(const Key&)> selected)> choose_captain;
    std::function<void(const Key&, std::function<void(const Key&)>)> reuse_captain;
    std::function<bool(uint8_t, uint64_t, const Key&)> captain_validated;
    std::function<bool(const Key& adventure, const Key& captain, std::string& error)> launch;
    // Close owned setup UI and restore the native menu before Scenario was entered.
    std::function<void()> abort_preparation;
    // Leave a partially loaded/running native adventure after a cancelled attempt.
    // Called once outside the frame, after the coordinator enters its ended state.
    std::function<void()> abort_native;
    std::function<void(bool)> freeze;
    std::function<bool(const Key&)> loaded;
    std::function<bool()> paused;
    std::function<void*(uint8_t)> captain_of;
    std::function<void(bool)> native_result;
    std::function<float(uint8_t, float)> incoming_damage;
    // Native/captain messages share the coordinator's ordered cleanup barrier.
    std::function<void(uint8_t, net::Reader&)> native_message;
    // Sample captain positions/max HP without modifying game state. The host samples all
    // captains; each client supplies its own validated seed in its loaded acknowledgement.
    std::function<bool(uint8_t, ga::CaptainSeed&)> sample;
    // Apply an authoritative snapshot. Downed captains remain native-alive but cannot act.
    std::function<void(const std::vector<ga::Captain>&)> apply;
    // Must attribute projectiles/DoT to their source owner and identify genuine hostiles.
    std::function<bool(void* victim, void* attacker, int damage_type, ga::Source&)> source;
};

bool init(const SkHost* host, SkMod* mod, Ctx ctx);
void tick(double now);
void on_message(uint8_t from, net::Reader& payload); // reads subkind, including asset range 64..71
void on_join(uint8_t player);
void on_leave(uint8_t player);
void reset();
const ga::Difficulty& settings();
bool set_settings(const ga::Difficulty& difficulty);
bool ready();
bool launch_ready(uint8_t player);
bool preparation(ga::PreparationView& view);
bool can_ready();
bool change_captain();
uint64_t selection_serial(uint8_t player);
Key selected_captain(uint8_t player);
void captain_progress(ga::PreparationStage stage, uint8_t progress = 0);
bool set_ready(bool value);
bool active();
bool running(); // all frozen-roster captains loaded and the host committed the attempt
bool native_attempt(); // a current attempt has entered native loading/gameplay
bool participating(uint8_t player); // excludes joins/reconnects after roster freeze
struct CaptainState {
    uint64_t run_id = 0;
    ga::Captain captain;
    double simulation_time = 0;
    float revive_progress = 0; // 0..1, measured against the host's simulation time
    double automatic_remaining = -1; // -1 unless automatic revival is enabled and downed
    double grace_remaining = 0;
};
// Read-only UI state. Clients wait for their first validated snapshot after commit.
// Timers are not extrapolated from wall time, so paused adventures stay paused.
bool captain_state(uint8_t player, CaptainState& state);
std::string status();
bool request_play(const Key& adventure, const Key& locked_captain = {});
void captain_selected(const Key& captain);
// Captain damage belongs exclusively to the GA host while a run is committed.
// Return true when the native hit may continue; false means handled/suppressed here.
bool damage_filter(void* victim, float& amount, int type, void* attacker);
bool captain_damage(uint8_t victim, const ga::Source& source, float amount);
bool captain_heal(uint8_t victim, float amount);
bool revive(uint8_t target, bool held);
bool complete(bool success);
// Called after the native results panel has saved local progression.
bool results_accepted(bool replay);
bool cancel(const std::string& reason);
const ga::Descriptor* current();

} // namespace adventure
