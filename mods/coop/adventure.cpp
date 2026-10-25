#include "adventure.h"

#include <algorithm>
#include <cmath>
#include <deque>
#include <map>
#include <memory>
#include <utility>

#include "adventure_assets.h"

namespace adventure {
namespace {
enum Kind : uint8_t {
    K_SETTINGS, K_PREPARE, K_CONTENT, K_CAPTAIN, K_READY, K_LAUNCH, K_LOADED,
    K_COMMIT, K_ABORT, K_SNAPSHOT, K_REVIVE, K_RESULT, K_WAIT, K_INVALIDATE,
    K_PREPARATION, K_CHANGE_CAPTAIN, K_PREPARATION_STAGE, K_RESULTS_ACCEPTED, K_REPLAY_PREPARE
};
enum class Phase { Idle, Collecting, Preparing, Loading, Running, Ended };

const SkHost* H;
Ctx C;
ga::Difficulty g_settings;
ga::Descriptor g_descriptor;
ga::LaunchBarrier g_barrier;
ga::Authority g_authority;
Phase g_phase = Phase::Idle;
Key g_captain;
bool g_ready, g_content, g_chosen, g_launch_started, g_loaded_sent, g_waiting;
bool g_assets_available, g_have_wall, g_cleaning;
uint64_t g_generation, g_next_run, g_newest_run, g_settings_revision = 1, g_received_settings;
uint64_t g_snapshot_revision, g_health_event, g_result_run;
uint64_t g_pending_result_run;
bool g_pending_result_success, g_pending_result_announce;
double g_last_wall, g_simulation, g_last_snapshot, g_last_revive;
uint8_t g_revive_target = ga::kNoPlayer;
bool g_revive_held;
std::map<uint8_t, ga::CaptainSeed> g_seeds;
std::set<uint8_t> g_excluded;
std::vector<ga::Captain> g_client_captains;
uint64_t g_client_snapshot_run;
double g_client_snapshot_time;
std::string g_error;
ga::PreparationView g_preparation;
bool g_preparation_dirty, g_change_pending;
double g_last_preparation;
std::set<uint8_t> g_results_accepted;
bool g_replay_requested, g_replay_started;
Key g_replay_captain;
std::deque<std::vector<uint8_t>> g_cleanup_messages;
size_t g_cleanup_bytes;
bool g_cleanup_overflow;
struct DeferredHealth {
    uint64_t run, event;
    uint8_t victim;
    ga::Source source;
    float amount;
    bool healing;
};
constexpr size_t kDeferredHealthLimit = 256;
std::deque<DeferredHealth> g_deferred_health;

void clear_cleanup_messages()
{
    g_cleanup_messages.clear(); g_cleanup_bytes=0; g_cleanup_overflow=false;
}

ga::PreparationMember* preparation_member(uint8_t player)
{
    for (auto& m : g_preparation.members) if (m.player == player) return &m;
    return nullptr;
}
void initialize_preparation()
{
    g_deferred_health.clear();
    g_pending_result_run = 0;
    g_preparation = {};
    g_preparation.run = g_descriptor.run_id;
    g_preparation.adventure = g_descriptor.adventure;
    g_preparation.locked_captain = g_descriptor.locked_captain;
    for (uint8_t id : g_descriptor.roster) {
        ga::PreparationMember m; m.player = id; g_preparation.members.push_back(m);
    }
    g_preparation_dirty = true; g_change_pending = false;
}

void write_run(net::Writer& w, uint64_t run)
{
    w.u32(static_cast<uint32_t>(run)); w.u32(static_cast<uint32_t>(run >> 32));
}
uint64_t read_run(net::Reader& r)
{
    uint64_t run = r.u32();
    return run | (uint64_t(r.u32()) << 32);
}
void write_key(net::Writer& w, const Key& key) { w.u32(key.instance); w.u32(key.type); w.u32(key.group); }
Key read_key(net::Reader& r) { return {r.u32(), r.u32(), r.u32()}; }
bool valid_key(const Key& key) { return key.instance && key.type; }
bool host() { return C.sess && C.sess->connected() && C.sess->role() == net::Role::Host; }
bool client() { return C.sess && C.sess->connected() && C.sess->role() == net::Role::Client; }
uint8_t local_id() { return C.sess ? C.sess->my_id() : 0; }
bool member(uint8_t player)
{
    return std::find(g_descriptor.roster.begin(), g_descriptor.roster.end(), player) != g_descriptor.roster.end();
}
void feed(const std::string& text) { if (C.feed) C.feed(text); }
bool finished(net::Reader& r) { return r.ok() && r.left() == 0; }

void outside(std::function<void()> fn)
{
    if (!H || !H->run_outside_frame)
        return;
    struct Task { uint64_t generation; std::function<void()> fn; };
    auto* task = new Task{g_generation, std::move(fn)};
    H->run_outside_frame([](void* opaque) {
        std::unique_ptr<Task> task(static_cast<Task*>(opaque));
        if (task->generation == g_generation)
            task->fn();
    }, task);
}

void send(uint8_t kind, const net::Writer& body, int to = -1)
{
    if (!C.sess || !C.sess->connected())
        return;
    net::Writer w;
    w.u8(kind); w.raw(body.data.data(), body.size());
    if (to >= 0)
        C.sess->send_reliable_to(static_cast<uint8_t>(to), net::MSG_ADVENTURE, w);
    else if (host())
        C.sess->broadcast_reliable(net::MSG_ADVENTURE, w);
    else
        C.sess->send_reliable_to(0, net::MSG_ADVENTURE, w);
}

void send_settings(int to = -1)
{
    net::Writer w;
    write_run(w, g_settings_revision);
    g_settings.write(w);
    send(K_SETTINGS, w, to);
}

void send_preparation()
{
    if (!host() || (g_phase != Phase::Preparing && g_phase != Phase::Loading)) return;
    g_preparation.loading = g_phase == Phase::Loading;
    for (auto& m : g_preparation.members) {
        for (const auto& b : g_barrier.members()) if (b.player == m.player) {
            m.content = b.content; m.validated = b.captain;
            m.ready = b.content && b.captain && b.ready;
            if (b.captain) { m.captain = b.captain_key; m.stage = ga::PreparationStage::Prepared; m.progress = 100; }
        }
    }
    ++g_preparation.revision;
    net::Writer w; g_preparation.write(w); send(K_PREPARATION, w);
    g_preparation_dirty = false;
}

bool parse_hash(const std::string& text, ga::Hash& hash)
{
    if (text.size() != hash.size() * 2)
        return false;
    auto nibble = [](char c) -> int {
        if (c >= '0' && c <= '9') return c - '0';
        if (c >= 'a' && c <= 'f') return c - 'a' + 10;
        if (c >= 'A' && c <= 'F') return c - 'A' + 10;
        return -1;
    };
    for (size_t i = 0; i < hash.size(); i++) {
        int hi = nibble(text[i * 2]), lo = nibble(text[i * 2 + 1]);
        if (hi < 0 || lo < 0) return false;
        hash[i] = static_cast<uint8_t>((hi << 4) | lo);
    }
    return true;
}

std::string unavailable()
{
    if (!C.native_available || !C.native_available()) {
        std::string why = C.native_reason ? C.native_reason() : std::string();
        return why.empty() ? "The game adapter for shared adventures is unavailable." : why;
    }
    if (!g_assets_available)
        return "Adventure resource registration is unavailable for this game build.";
    if (!C.choose_captain || !C.launch || !C.abort_preparation || !C.abort_native || !C.freeze || !C.loaded || !C.sample || !C.apply || !C.source ||
        !C.collect || !C.captain_of || !C.native_result)
        return "The game adapter has incomplete launch, content, or combat support.";
    return {};
}

void invalidate_ready()
{
    g_ready = false;
    if (C.invalidate_ready) C.invalidate_ready();
}

void abort_run(const std::string& reason, bool announce)
{
    g_deferred_health.clear();
    clear_cleanup_messages();
    g_pending_result_run = 0;
    const bool leave_native = g_launch_started || g_phase == Phase::Running;
    if (announce && g_descriptor.run_id) {
        net::Writer w;
        write_run(w, g_descriptor.run_id); w.str16(reason);
        send(K_ABORT, w);
    }
    g_barrier.abort(g_descriptor.run_id);
    g_authority.abort(g_descriptor.run_id);
    g_phase = Phase::Ended;
    ++g_generation; // Discard selection, transfer and launch work from this attempt.
    g_launch_started = false;
    g_error = reason;
    g_preparation = {}; g_change_pending = false;
    g_replay_captain = {}; g_results_accepted.clear(); g_replay_requested = g_replay_started = false;
    g_revive_held = false;
    invalidate_ready();
    adventure_assets::reset();
    g_cleaning = true;
    // Cleanup must survive reset/disconnect invalidating ordinary attempt callbacks.
    H->run_outside_frame([](void* opaque) {
        std::unique_ptr<bool> leave(static_cast<bool*>(opaque));
        if (C.freeze) C.freeze(false);
        if (*leave && C.abort_native) C.abort_native();
        else if (C.abort_preparation) C.abort_preparation();
        g_cleaning = false;
    }, new bool(leave_native));
    feed("Adventure: " + reason);
}

bool defer_cleanup_message(uint8_t from, const net::Reader& message)
{
    if(!client() || from || !message.left()) return false;
    if(g_cleanup_overflow) return true;
    if(g_cleanup_messages.empty()) {
        if(!g_cleaning || *message.cur()!=K_PREPARE) return false;
        auto check=message; check.u8(); ga::Descriptor descriptor;
        if(!descriptor.read(check) || !finished(check) || descriptor.run_id<=g_newest_run ||
            g_phase==Phase::Running || g_phase==Phase::Loading) return false;
    }
    constexpr size_t limit=512, byte_limit=2*1024*1024;
    if(g_cleanup_messages.size()>=limit || message.left()>byte_limit-g_cleanup_bytes) {
        g_cleanup_overflow=true;
        return true;
    }
    g_cleanup_messages.emplace_back(message.cur(),message.cur()+message.left());
    g_cleanup_bytes+=message.left();
    return true;
}

void drain_cleanup_messages()
{
    if(g_cleaning || g_cleanup_messages.empty()) return;
    auto messages=std::move(g_cleanup_messages);
    bool overflow=g_cleanup_overflow;
    clear_cleanup_messages();
    if(!client()) return;
    for(const auto& bytes:messages) {
        net::Reader message(bytes.data(),bytes.size()); on_message(0,message);
        if(overflow) {
            // The first retained message is a validated new preparation. Abort
            // that run, rather than silently losing acknowledged content or
            // cancelling the previous run while the host waits on the new one.
            abort_run("Adventure preparation exceeded its bounded cleanup message queue. Select Play again.",true);
            break;
        }
        if(g_cleaning || !client()) break;
    }
}

void choose_captain()
{
    const uint64_t run = g_descriptor.run_id;
    outside([run] {
        if (run != g_descriptor.run_id || g_phase != Phase::Preparing || !C.choose_captain)
            return;
        const uint64_t generation = g_generation, serial = selection_serial(local_id());
        captain_progress(ga::PreparationStage::Choosing);
        auto selected = [run, generation, serial](const Key& captain) {
            if (generation == g_generation && run == g_descriptor.run_id && g_phase == Phase::Preparing && serial == selection_serial(local_id()))
                captain_selected(captain);
        };
        Key previous=std::exchange(g_replay_captain,{});
        if(previous.instance && C.reuse_captain) C.reuse_captain(previous,std::move(selected));
        else C.choose_captain(g_descriptor.locked_captain,std::move(selected));
    });
}

void prepare_replay()
{
    if(g_replay_started) return;
    g_replay_started=true; g_cleaning=true; ++g_generation;
    const auto descriptor=g_descriptor;
    g_replay_captain=g_captain;
    // Leaving the old native run is mandatory even if disconnect/reset cancels
    // the proposed retry before this task runs. Only starting that retry is
    // generation guarded, just as cancellation preserves mandatory cleanup.
    using ReplayTask=std::pair<uint64_t,ga::Descriptor>;
    H->run_outside_frame([](void* opaque) {
        std::unique_ptr<ReplayTask> task(static_cast<ReplayTask*>(opaque));
        if(C.abort_native) C.abort_native();
        g_cleaning=false;
        if(task->first==g_generation && g_phase==Phase::Ended &&
            g_descriptor.run_id==task->second.run_id && host())
            request_play(task->second.adventure,task->second.locked_captain);
    },new ReplayTask(g_generation,descriptor));
}

void send_ready()
{
    if (g_phase != Phase::Preparing || !g_content || !g_chosen)
        return;
    if (host())
        g_barrier.set_ready(g_descriptor.run_id, 0, g_ready);
    else {
        net::Writer w;
        write_run(w, g_descriptor.run_id); write_run(w, g_preparation.epoch);
        write_run(w, selection_serial(local_id())); w.u8(g_ready ? 1 : 0);
        send(K_READY, w);
    }
}

void invalidate_party()
{
    invalidate_ready();
    for (uint8_t player : g_descriptor.roster)
        g_barrier.set_ready(g_descriptor.run_id, player, false);
    net::Writer w; write_run(w, g_descriptor.run_id); send(K_INVALIDATE, w);
    g_preparation_dirty = true;
}

bool grant_change(uint8_t player, uint64_t serial)
{
    auto* m = preparation_member(player);
    if (!host() || g_phase != Phase::Preparing || g_descriptor.locked_captain.instance || !m ||
        !m->validated || m->serial != serial) return false;
    ++m->serial; ++g_preparation.epoch;
    m->validated = m->ready = false; m->stage = ga::PreparationStage::Choosing; m->progress = 0;
    g_barrier.clear_captain(g_descriptor.run_id, player);
    invalidate_party();
    send_preparation();
    if (player == local_id()) { g_chosen = false; g_change_pending = false; choose_captain(); }
    return true;
}

void prepared(uint64_t run, const Key& key, const std::string& hash_text)
{
    if (run != g_descriptor.run_id || !(key == g_descriptor.adventure) ||
        (g_phase != Phase::Collecting && g_phase != Phase::Preparing))
        return;
    ga::Hash hash;
    if (!parse_hash(hash_text, hash)) {
        abort_run("Invalid adventure manifest digest.", true);
        return;
    }
    if (host()) {
        g_descriptor.manifest_hash = hash;
        if (!g_barrier.begin(g_descriptor)) {
            abort_run("The adventure launch descriptor is invalid.", true);
            return;
        }
        g_phase = Phase::Preparing;
        g_barrier.ack_content(run, 0, key, hash);
        net::Writer w;
        g_descriptor.write(w);
        send(K_PREPARE, w);
        for (uint8_t player : g_descriptor.roster)
            if (player && !adventure_assets::share_with(player)) {
                abort_run("Could not queue adventure content for every party member.", true);
                return;
            }
    } else {
        if (hash != g_descriptor.manifest_hash) {
            abort_run("Adventure content does not match the host manifest.", true);
            return;
        }
        net::Writer w;
        write_run(w, run); write_key(w, key); w.raw(hash.data(), hash.size());
        send(K_CONTENT, w);
    }
    g_content = true;
    if (auto* m = preparation_member(local_id())) m->content = true;
    g_preparation_dirty = true;
    choose_captain();
}

void peer_prepared(uint8_t peer, uint64_t run, const Key& key, const std::string& hash_text)
{
    ga::Hash hash;
    if (host() && run == g_descriptor.run_id && parse_hash(hash_text, hash))
        g_barrier.ack_content(run, peer, key, hash);
    g_preparation_dirty = true;
}

void queue_launch()
{
    const uint64_t run = g_descriptor.run_id;
    outside([run] {
        if (run != g_descriptor.run_id || g_phase != Phase::Loading)
            return;
        C.freeze(true);
        std::string error;
        g_launch_started = true; // Even a failed native entry may have changed application state.
        if (!C.launch(g_descriptor.adventure, g_captain, error)) {
            abort_run(error.empty() ? "The adventure could not be launched." : error, true);
            return;
        }
    });
}

void apply_snapshot(const std::vector<ga::Captain>& captains)
{
    const uint64_t run = g_descriptor.run_id;
    outside([run, captains] {
        if (run == g_descriptor.run_id && C.apply &&
            (g_phase == Phase::Running || (g_phase == Phase::Ended && g_result_run == run)))
            C.apply(captains);
    });
}

void snapshot()
{
    net::Writer w;
    write_run(w, g_descriptor.run_id);
    write_run(w, g_authority.revision());
    w.u8(static_cast<uint8_t>(g_authority.phase()));
    w.u8(g_authority.act()); w.f64(g_authority.now());
    w.u8(static_cast<uint8_t>(g_authority.captains().size()));
    for (const auto& c : g_authority.captains()) {
        w.u8(c.player); w.u8(static_cast<uint8_t>(c.life));
        w.f32(c.health); w.f32(c.max_health);
        w.f32(c.position.x); w.f32(c.position.y); w.f32(c.position.z);
        w.f64(c.downed_at); w.f64(c.protected_until);
        w.u8(c.reviver); w.f64(c.revive_started);
    }
    send(K_SNAPSHOT, w);
    apply_snapshot(g_authority.captains());
    g_last_snapshot = g_last_wall;
}

void native_result(bool success)
{
    if (g_result_run == g_descriptor.run_id)
        return;
    g_result_run = g_descriptor.run_id;
    const uint64_t run = g_descriptor.run_id;
    outside([run, success] {
        if (run == g_descriptor.run_id) {
            if (C.freeze) C.freeze(false);
            if (C.native_result) C.native_result(success);
        }
    });
}

void finish_result(bool success, bool announce)
{
    if (g_phase != Phase::Running)
        return;
    g_deferred_health.clear();
    if (announce) {
        snapshot();
        net::Writer w;
        write_run(w, g_descriptor.run_id); w.u8(success ? 1 : 0);
        send(K_RESULT, w);
    }
    g_phase = Phase::Ended;
    g_barrier.abort(g_descriptor.run_id);
    g_revive_held = false;
    if (!announce || g_authority.claim_reward(g_descriptor.run_id, local_id()))
        native_result(success);
    feed(success ? "Adventure completed together." : "The party failed the adventure.");
}

void accept_result(bool success, bool announce)
{
    if (g_phase != Phase::Running || g_pending_result_run) return;
    if (C.native_settling && C.native_settling()) {
        // At most one result for this run. Preserve the first authoritative
        // outcome until native Finished has released its old dialog resources.
        g_pending_result_run = g_descriptor.run_id;
        g_pending_result_success = success; g_pending_result_announce = announce;
        return;
    }
    finish_result(success, announce);
}
void result(bool success) { accept_result(success, true); }

bool apply_health(const DeferredHealth& event)
{
    if (!host() || g_phase != Phase::Running || event.run != g_descriptor.run_id ||
        !member(event.victim) || g_excluded.count(event.victim) ||
        (event.source.kind == ga::SourceKind::Captain && g_excluded.count(event.source.owner))) return false;
    if (event.healing) g_authority.heal(event.run, event.event, event.victim, event.amount);
    else g_authority.damage(event.run, event.event, event.victim, event.source, event.amount);
    return true;
}
void drain_health()
{
    if (!host() || g_phase != Phase::Running || (C.paused && C.paused())) return;
    bool applied = false;
    for (unsigned count = 0; count < 64 && !g_deferred_health.empty(); ++count) {
        auto event = g_deferred_health.front(); g_deferred_health.pop_front();
        applied = apply_health(event) || applied;
        if (g_authority.phase() == ga::RunPhase::Failed) { result(false); return; }
    }
    if (applied) snapshot();
}
bool health(uint8_t victim, const ga::Source& source, float amount, bool healing)
{
    if (!host() || g_phase != Phase::Running || !member(victim) || g_excluded.count(victim) ||
        !std::isfinite(amount) || amount <= 0) return false;
    if (!healing && C.incoming_damage) amount = C.incoming_damage(victim, amount);
    if (!std::isfinite(amount) || amount < 0) return false;
    DeferredHealth event{g_descriptor.run_id, ++g_health_event, victim, source, amount, healing};
    // Keep arrival order across Resume: fresh callbacks join older held work
    // until the bounded FIFO drains, rather than overtaking a deferred hit.
    if (!g_deferred_health.empty() || (C.paused && C.paused())) {
        if (g_deferred_health.size() >= kDeferredHealthLimit) {
            abort_run("Adventure health synchronization exceeded its bounded pending queue. Cancel and retry the adventure.", true);
            return false;
        }
        g_deferred_health.push_back(event);
    } else {
        apply_health(event);
        if (g_authority.phase() == ga::RunPhase::Failed) result(false);
        else snapshot();
    }
    return true;
}

bool valid_seed(const ga::CaptainSeed& seed)
{
    return member(seed.player) && std::isfinite(seed.max_health) && seed.max_health > 0 &&
           std::isfinite(seed.position.x) && std::isfinite(seed.position.y) && std::isfinite(seed.position.z);
}

void loaded()
{
    ga::CaptainSeed seed;
    seed.player = local_id();
    if (!C.sample || !C.sample(local_id(), seed) || seed.player != local_id() || !valid_seed(seed)) {
        abort_run("The native captain state could not be read safely.", true);
        return;
    }
    g_loaded_sent = true;
    if (host()) {
        g_seeds[0] = seed;
        g_barrier.ack_loaded(g_descriptor.run_id, 0);
    } else {
        net::Writer w;
        write_run(w, g_descriptor.run_id);
        w.f32(seed.max_health); w.f32(seed.position.x); w.f32(seed.position.y); w.f32(seed.position.z);
        send(K_LOADED, w);
    }
}

bool read_snapshot(net::Reader& r)
{
    uint64_t run = read_run(r), revision = read_run(r);
    uint8_t phase = r.u8(), act = r.u8();
    double now = r.f64();
    uint8_t count = r.u8();
    if (!client() || run != g_descriptor.run_id || g_phase != Phase::Running || revision <= g_snapshot_revision ||
        count != g_descriptor.roster.size() || phase < static_cast<uint8_t>(ga::RunPhase::Playing) ||
        phase > static_cast<uint8_t>(ga::RunPhase::Failed) || act > 7 || !std::isfinite(now) || now < 0)
        return false;
    std::vector<ga::Captain> captains;
    std::set<uint8_t> ids;
    for (uint8_t i = 0; i < count; i++) {
        ga::Captain c;
        c.player = r.u8(); c.life = static_cast<ga::Life>(r.u8());
        c.health = r.f32(); c.max_health = r.f32();
        c.position = {r.f32(), r.f32(), r.f32()};
        c.downed_at = r.f64(); c.protected_until = r.f64();
        c.reviver = r.u8(); c.revive_started = r.f64();
        if (!member(c.player) || !ids.insert(c.player).second || c.life > ga::Life::Disconnected ||
            !std::isfinite(c.health) || !std::isfinite(c.max_health) || c.max_health <= 0 || c.health < 0 || c.health > c.max_health ||
            !std::isfinite(c.position.x) || !std::isfinite(c.position.y) || !std::isfinite(c.position.z) ||
            !std::isfinite(c.downed_at) || !std::isfinite(c.protected_until) || !std::isfinite(c.revive_started) ||
            (c.reviver != ga::kNoPlayer && !member(c.reviver)))
            return false;
        captains.push_back(c);
    }
    if (!finished(r))
        return false;
    g_snapshot_revision = revision;
    g_client_captains = captains;
    g_client_snapshot_run = run;
    g_client_snapshot_time = now;
    apply_snapshot(captains);
    return true;
}
} // namespace

bool init(const SkHost* host_api, SkMod* mod, Ctx ctx)
{
    H = host_api;
    C = std::move(ctx);
    if (!H || !H->run_outside_frame)
        return false;
    adventure_assets::Ctx assets;
    assets.sess = C.sess;
    assets.feed = C.feed;
    assets.collect = C.collect;
    assets.prepared = prepared;
    assets.peer_prepared = peer_prepared;
    assets.failed = [](uint64_t run, const std::string& error) {
        if (run == g_descriptor.run_id && active()) abort_run(error, true);
    };
    g_assets_available = adventure_assets::init(H, mod, std::move(assets));
    // Lobby configuration remains usable when this build lacks verified game integration.
    return C.sess != nullptr;
}

const ga::Difficulty& settings() { return g_settings; }
bool ready() { return g_ready; }
bool launch_ready(uint8_t player)
{
    if (!active() || !member(player)) return false;
    if (player == local_id()) return g_content && g_chosen && g_ready;
    if (!host()) {
        auto* m = preparation_member(player);
        return m && m->ready;
    }
    for (const auto& m : g_barrier.members())
        if (m.player == player) return m.connected && m.content && m.captain && m.ready;
    return false;
}
bool preparation(ga::PreparationView& view)
{
    if (!active() || running()) return false;
    view = g_preparation;
    if (preparation_member(local_id())) {
        for (auto& row : view.members) if (row.player == local_id()) {
            row.ready = g_content && g_chosen && g_ready && !g_change_pending;
            row.content = g_content; row.validated = g_chosen;
        }
    }
    return view.run != 0;
}
uint64_t selection_serial(uint8_t player)
{
    auto* m = preparation_member(player); return m ? m->serial : 0;
}
Key selected_captain(uint8_t player)
{
    auto* m = preparation_member(player); return active() && m && m->validated ? m->captain : Key{};
}
bool can_ready() { return g_phase == Phase::Preparing && g_content && g_chosen && !g_change_pending; }
bool change_captain()
{
    if (!can_ready() || g_descriptor.locked_captain.instance) return false;
    if (host()) return grant_change(local_id(), selection_serial(local_id()));
    net::Writer w; write_run(w, g_descriptor.run_id); write_run(w, selection_serial(local_id()));
    g_change_pending = true; g_ready = false;
    send_ready(); send(K_CHANGE_CAPTAIN, w); return true;
}
void captain_progress(ga::PreparationStage stage, uint8_t progress)
{
    auto* m = preparation_member(local_id());
    if (!m || g_phase != Phase::Preparing || stage >= ga::PreparationStage::Prepared || progress > 100) return;
    if (m->stage == stage && m->progress == progress) return;
    m->stage = stage; m->progress = progress; g_preparation_dirty = true;
    if (client()) {
        net::Writer w; write_run(w, g_descriptor.run_id); write_run(w, m->serial);
        w.u8(uint8_t(stage)); w.u8(progress); send(K_PREPARATION_STAGE, w);
    }
}
bool active() { return g_phase != Phase::Idle && g_phase != Phase::Ended; }
bool running() { return g_phase == Phase::Running; }
bool native_attempt() { return active() && (g_launch_started || g_phase==Phase::Running); }
bool captain_state(uint8_t player, CaptainState& state)
{
    state = {};
    if (!running() || !member(player)) return false;
    const ga::Captain* captain = nullptr;
    double now = 0;
    if (host()) {
        captain = g_authority.captain(player);
        now = g_authority.now();
    } else if (client() && g_client_snapshot_run == g_descriptor.run_id) {
        auto it = std::find_if(g_client_captains.begin(), g_client_captains.end(),
                               [player](const ga::Captain& c) { return c.player == player; });
        if (it != g_client_captains.end()) captain = &*it;
        now = g_client_snapshot_time;
    }
    if (!captain) return false;
    state.run_id = g_descriptor.run_id;
    state.captain = *captain;
    state.simulation_time = now;
    if (captain->life == ga::Life::Downed) {
        if (captain->reviver != ga::kNoPlayer)
            state.revive_progress = float(std::clamp((now - captain->revive_started) / ga::kReviveSeconds, 0.0, 1.0));
        if (g_descriptor.difficulty.death_mode == ga::DeathMode::AutomaticRevive)
            state.automatic_remaining = std::clamp(ga::kAutomaticSeconds - (now - captain->downed_at), 0.0, ga::kAutomaticSeconds);
    } else if (captain->life == ga::Life::Alive)
        state.grace_remaining = std::max(0.0, captain->protected_until - now);
    return true;
}
bool participating(uint8_t player) { return active() && member(player) && g_excluded.count(player) == 0; }
const ga::Descriptor* current() { return active() ? &g_descriptor : nullptr; }

bool set_settings(const ga::Difficulty& difficulty)
{
    if (!difficulty.valid() || client() || g_phase == Phase::Loading || g_phase == Phase::Running)
        return false;
    if (active())
        abort_run("Difficulty changed. Select Play again to prepare the party.", true);
    g_settings = difficulty;
    g_settings_revision++;
    invalidate_ready();
    if (host()) send_settings();
    return true;
}

bool set_ready(bool value)
{
    if (g_waiting || g_phase == Phase::Loading || g_phase == Phase::Running)
        return false;
    if (value && !can_ready()) return false;
    g_ready = value;
    send_ready();
    g_preparation_dirty = true;
    return true;
}

bool request_play(const Key& key, const Key& locked)
{
    if (!host() || active() || g_cleaning || !valid_key(key))
        return false;
    std::string why = unavailable();
    if (!why.empty()) {
        g_error = why;
        feed("Adventure: " + why);
        return false;
    }
    ga::Descriptor descriptor;
    descriptor.run_id = ++g_next_run;
    descriptor.adventure = key;
    descriptor.locked_captain = locked;
    descriptor.difficulty = g_settings;
    for (const auto& player : C.sess->players())
        descriptor.roster.push_back(player.id);
    if (descriptor.roster.empty() || descriptor.roster.size() > ga::kPartyLimit)
        return false;
    g_descriptor = descriptor;
    initialize_preparation();
    g_newest_run = descriptor.run_id;
    g_phase = Phase::Collecting;
    g_content = g_chosen = g_launch_started = g_loaded_sent = g_waiting = false;
    g_captain = {};
    g_seeds.clear();
    g_excluded.clear();
    g_client_captains.clear();
    g_client_snapshot_run = 0;
    g_client_snapshot_time = 0;
    g_snapshot_revision = g_health_event = 0;
    g_error.clear();
    g_results_accepted.clear(); g_replay_requested=g_replay_started=false;
    // Every participant explicitly confirms after reviewing the shared lobby.
    g_ready = false;
    if (!adventure_assets::prepare(descriptor.run_id, key)) {
        abort_run("Adventure content preparation could not be started.", true);
        return false;
    }
    return true;
}

void captain_selected(const Key& captain)
{
    if (g_phase != Phase::Preparing || !g_content)
        return;
    if (!valid_key(captain)) {
        abort_run("Captain selection was cancelled.", true);
        return;
    }
    if (g_descriptor.locked_captain.instance && !(captain == g_descriptor.locked_captain)) {
        abort_run("This adventure requires its authored captain.", true);
        return;
    }
    bool changed = g_chosen && !(g_captain == captain);
    g_captain = captain;
    g_chosen = true;
    if (auto* m = preparation_member(local_id())) {
        m->captain = captain; m->validated = true; m->stage = ga::PreparationStage::Prepared; m->progress = 100;
    }
    g_preparation_dirty = true;
    if (host()) {
        if (!g_barrier.ack_captain(g_descriptor.run_id, 0, captain))
            return;
        if (changed) invalidate_party();
    } else {
        net::Writer w;
        write_run(w, g_descriptor.run_id); write_run(w, selection_serial(local_id())); write_key(w, captain);
        send(K_CAPTAIN, w);
    }
    send_ready();
}

void on_join(uint8_t player)
{
    if (!host() || player == 0)
        return;
    send_settings(player);
    if(g_phase==Phase::Ended && g_result_run==g_descriptor.run_id) g_excluded.insert(player);
    if (active()) {
        g_excluded.insert(player);
        net::Writer w;
        write_run(w, g_descriptor.run_id);
        send(K_WAIT, w, player);
        feed(C.sess->name_of(player) + " will join the next adventure run.");
    }
}

void on_leave(uint8_t player)
{
    if (!active() || !member(player))
        return;
    if (!host()) {
        if (player == 0) abort_run("The adventure host disconnected.", false);
        else g_excluded.insert(player);
        return;
    }
    g_excluded.insert(player);
    g_deferred_health.erase(std::remove_if(g_deferred_health.begin(), g_deferred_health.end(), [&](const auto& event) {
        return event.victim == player || (event.source.kind == ga::SourceKind::Captain && event.source.owner == player);
    }), g_deferred_health.end());
    if (g_phase == Phase::Running) {
        g_authority.disconnect(g_descriptor.run_id, player);
        if (g_authority.phase() == ga::RunPhase::Failed) result(false);
        else snapshot();
    } else
        abort_run("A party member disconnected before launch. Select Play again.", true);
}

void on_message(uint8_t from, net::Reader& r)
{
    if (!r.left()) return;
    if(defer_cleanup_message(from,r)) return;
    uint8_t kind = *r.cur();
    if(kind>=128) {
        if(C.native_message) C.native_message(from,r);
        return;
    }
    if (kind >= 64 && kind <= 71) {
        if (host() && g_excluded.count(from)) return;
        adventure_assets::on_message(from, r);
        return;
    }
    r.u8();
    if (kind == K_SETTINGS) {
        uint64_t revision = read_run(r);
        ga::Difficulty difficulty;
        if (from != 0 || !client() || !difficulty.read(r) || !finished(r) || !revision || revision <= g_received_settings ||
            g_phase == Phase::Running || g_phase == Phase::Loading)
            return;
        g_received_settings = revision;
        g_settings = difficulty;
        invalidate_ready();
        return;
    }
    if (kind == K_PREPARE) {
        ga::Descriptor descriptor;
        if (from != 0 || !client() || !descriptor.read(r) || !finished(r) || descriptor.run_id <= g_newest_run ||
            g_phase == Phase::Running || g_phase == Phase::Loading)
            return;
        g_descriptor = descriptor;
        initialize_preparation();
        g_newest_run = descriptor.run_id;
        g_settings = descriptor.difficulty;
        g_phase = Phase::Preparing;
        g_content = g_chosen = g_launch_started = g_loaded_sent = false;
        g_captain = {};
        g_snapshot_revision = 0;
        g_excluded.clear();
        g_error.clear();
        g_results_accepted.clear(); g_replay_requested=g_replay_started=false;
        g_waiting = !member(local_id());
        invalidate_ready();
        if (g_waiting) { g_phase = Phase::Ended; return; }
        std::string why = unavailable();
        if (!why.empty()) { abort_run(why, true); return; }
        if (!adventure_assets::prepare(descriptor.run_id, descriptor.adventure))
            abort_run("Adventure content receiver could not be prepared.", true);
        return;
    }
    if (kind == K_PREPARATION) {
        ga::PreparationView view;
        if (from != 0 || !client() || g_phase != Phase::Preparing || !view.read(r) || !finished(r) ||
            view.run != g_descriptor.run_id || view.revision <= g_preparation.revision ||
            !(view.adventure == g_descriptor.adventure) || !(view.locked_captain == g_descriptor.locked_captain) ||
            view.members.size() != g_descriptor.roster.size()) return;
        for (const auto& m : view.members) if (!member(m.player)) return;
        uint64_t old_serial = selection_serial(local_id()), old_epoch = g_preparation.epoch;
        g_preparation = std::move(view);
        if (g_preparation.epoch != old_epoch) invalidate_ready();
        if (selection_serial(local_id()) > old_serial) {
            g_chosen = false; g_change_pending = false; choose_captain();
        }
        return;
    }
    if (kind == K_SNAPSHOT) {
        if (from == 0) read_snapshot(r);
        return;
    }
    uint64_t run = read_run(r);
    if (!r.ok() || !run)
        return;
    if (kind == K_WAIT) {
        if (from == 0 && client() && finished(r) && !active()) {
            g_waiting = true;
            g_newest_run = std::max(g_newest_run, run);
            feed("This adventure is underway. You will join the next run.");
        }
        return;
    }
    if (run != g_descriptor.run_id || (host() && g_excluded.count(from))) return;
    if(kind==K_RESULTS_ACCEPTED) {
        uint8_t replay=r.u8();
        if(!host() || !from || !member(from) || g_phase!=Phase::Ended || g_result_run!=run || replay>1 || !finished(r)) return;
        g_results_accepted.insert(from);
        if(replay) {
            g_replay_requested=true;
            feed(C.sess->name_of(from)+" requested a co-op replay. Waiting for everyone to accept results.");
        }
        return;
    }
    if(kind==K_REPLAY_PREPARE) {
        if(client() && !from && finished(r) && g_phase==Phase::Ended && g_result_run==run) prepare_replay();
        return;
    }
    if (kind == K_ABORT) {
        std::string reason = r.str16();
        if (!finished(r) || (host() ? !member(from) : from != 0) || !active()) return;
        abort_run(reason.empty() ? "Adventure preparation was cancelled." : reason, host());
        return;
    }
    if (kind == K_INVALIDATE) {
        if (from == 0 && client() && finished(r) && g_phase == Phase::Preparing)
            invalidate_ready();
        return;
    }
    if (host()) {
        if (!member(from) || from == 0) return;
        switch (kind) {
        case K_CONTENT: {
            Key key = read_key(r); ga::Hash hash;
            if (r.raw(hash.data(), hash.size()) && finished(r))
                g_barrier.ack_content(run, from, key, hash);
            break;
        }
        case K_CAPTAIN: {
            uint64_t serial = read_run(r);
            Key key = read_key(r);
            bool changed = false;
            for (const auto& m : g_barrier.members())
                if (m.player == from) changed = m.captain && !(m.captain_key == key);
            if (finished(r) && serial == selection_serial(from) &&
                (!C.captain_validated || C.captain_validated(from, serial, key)) && g_barrier.ack_captain(run, from, key)) {
                if (changed) invalidate_party();
                g_preparation_dirty = true;
            }
            break;
        }
        case K_READY: {
            uint64_t epoch = read_run(r), serial = read_run(r);
            uint8_t value = r.u8();
            if (finished(r) && value <= 1 && epoch == g_preparation.epoch && serial == selection_serial(from)) {
                g_barrier.set_ready(run, from, value != 0); g_preparation_dirty = true;
            }
            break;
        }
        case K_CHANGE_CAPTAIN: {
            uint64_t serial = read_run(r);
            if (finished(r)) grant_change(from, serial);
            break;
        }
        case K_PREPARATION_STAGE: {
            uint64_t serial = read_run(r);
            auto stage = ga::PreparationStage(r.u8()); uint8_t progress = r.u8();
            auto* m = preparation_member(from);
            if (finished(r) && g_phase == Phase::Preparing && m && !m->validated && serial == m->serial &&
                stage < ga::PreparationStage::Prepared && progress <= 100) {
                m->stage = stage; m->progress = progress; g_preparation_dirty = true;
            }
            break;
        }
        case K_LOADED: {
            ga::CaptainSeed seed;
            seed.player = from; seed.max_health = r.f32(); seed.position = {r.f32(), r.f32(), r.f32()};
            if (finished(r) && valid_seed(seed) && g_barrier.ack_loaded(run, from))
                g_seeds.emplace(from, seed);
            break;
        }
        case K_REVIVE: {
            uint8_t target = r.u8(), held = r.u8();
            if (finished(r) && held <= 1 && g_phase == Phase::Running)
                g_authority.revive(run, from, target, held != 0);
            break;
        }
        default: break;
        }
        return;
    }
    if (!client() || from != 0) return;
    if (kind == K_LAUNCH && finished(r) && g_phase == Phase::Preparing) {
        if (!g_content || !g_chosen || !g_ready) {
            // A local unready click can cross the host's commit in the opposite direction.
            // Cancel explicitly rather than leave the host waiting for an impossible load.
            abort_run("Party readiness changed while the adventure was launching. Select Play again.", true);
            return;
        }
        g_phase = Phase::Loading;
        queue_launch();
    } else if (kind == K_COMMIT && finished(r) && g_phase == Phase::Loading && g_loaded_sent) {
        g_phase = Phase::Running;
        outside([] { if (C.freeze) C.freeze(false); });
    } else if (kind == K_RESULT) {
        uint8_t success = r.u8();
        if (!finished(r) || success > 1 || g_phase != Phase::Running) return;
        accept_result(success != 0, false);
    }
}

void tick(double now)
{
    drain_cleanup_messages();
    if (!std::isfinite(now)) return;
    double elapsed = g_have_wall && now >= g_last_wall ? now - g_last_wall : 0;
    g_last_wall = now;
    g_have_wall = true;
    adventure_assets::tick(now);
    if(host() && g_phase==Phase::Ended && g_replay_requested && !g_replay_started && !g_cleaning) {
        auto players=C.sess->players();
        bool accepted=std::all_of(players.begin(),players.end(),[](const auto& p) {
            return !member(p.id) || g_excluded.count(p.id) || g_results_accepted.count(p.id);
        });
        if(accepted) {
            net::Writer w; write_run(w,g_descriptor.run_id); send(K_REPLAY_PREPARE,w);
            prepare_replay();
        }
    }
    if (g_phase == Phase::Preparing && !g_content) captain_progress(ga::PreparationStage::Content,adventure_assets::progress());
    if (!active()) return;
    if (!C.sess || !C.sess->connected()) {
        abort_run("The co-op session ended.", false);
        return;
    }
    if (g_pending_result_run) {
        if (g_pending_result_run != g_descriptor.run_id || g_phase != Phase::Running) g_pending_result_run = 0;
        else if (!(C.native_settling && C.native_settling())) {
            bool success = g_pending_result_success, announce = g_pending_result_announce;
            g_pending_result_run = 0;
            finish_result(success, announce);
            return;
        }
    }
    if (host() && g_preparation_dirty && now - g_last_preparation >= .1) {
        send_preparation(); g_last_preparation = now;
    }
    if (host() && g_phase == Phase::Preparing && g_barrier.all_ready()) {
        if (!g_barrier.begin_loading(g_descriptor.run_id)) return;
        g_phase = Phase::Loading;
        send_preparation();
        net::Writer w; write_run(w, g_descriptor.run_id); send(K_LAUNCH, w);
        queue_launch();
    }
    if (g_phase == Phase::Loading && g_launch_started && !g_loaded_sent && C.loaded && C.loaded(g_descriptor.adventure))
        loaded();
    if (host() && g_phase == Phase::Loading && g_barrier.all_loaded()) {
        std::vector<ga::CaptainSeed> seeds;
        for (uint8_t player : g_descriptor.roster) {
            auto it = g_seeds.find(player);
            if (it == g_seeds.end()) return;
            ga::CaptainSeed seed = it->second;
            if (!C.sample || !C.sample(player, seed) || seed.player != player || !valid_seed(seed)) {
                abort_run("The host could not verify every captain before launch.", true);
                return;
            }
            seeds.push_back(seed);
        }
        if (!g_authority.start(g_descriptor, seeds, 0) || !g_barrier.commit(g_descriptor.run_id)) {
            abort_run("The all-player launch barrier could not be committed.", true);
            return;
        }
        g_simulation = 0;
        g_phase = Phase::Running;
        net::Writer w; write_run(w, g_descriptor.run_id); send(K_COMMIT, w);
        outside([] { if (C.freeze) C.freeze(false); });
        snapshot();
        return;
    }
    if (g_phase != Phase::Running) return;
    if (g_pending_result_run) return;
    if (g_revive_held && now - g_last_revive >= 0.1) {
        if (host()) g_authority.revive(g_descriptor.run_id, local_id(), g_revive_target, true);
        else {
            net::Writer w; write_run(w, g_descriptor.run_id); w.u8(g_revive_target); w.u8(1); send(K_REVIVE, w);
        }
        g_last_revive = now;
    }
    if (!host()) return;
    drain_health();
    if (g_phase != Phase::Running || g_pending_result_run) return;
    if (!C.paused || !C.paused()) g_simulation += elapsed;
    for (uint8_t player : g_descriptor.roster) {
        ga::CaptainSeed seed;
        seed.player = player;
        if (C.sample && C.sample(player, seed) && seed.player == player && valid_seed(seed))
            g_authority.position(g_descriptor.run_id, player, seed.position);
    }
    g_authority.tick(g_descriptor.run_id, g_simulation);
    if (g_authority.phase() == ga::RunPhase::Failed) result(false);
    else if (now - g_last_snapshot >= 0.1) snapshot();
}

bool captain_damage(uint8_t victim, const ga::Source& source, float amount)
{
    return health(victim, source, amount, false);
}

bool captain_heal(uint8_t victim, float amount)
{
    return health(victim, {}, amount, true);
}

bool damage_filter(void* victim, float& amount, int type, void* attacker)
{
    if (g_phase != Phase::Running || !C.captain_of || !victim) return true;
    for (uint8_t player : g_descriptor.roster) {
        if (C.captain_of(player) != victim) continue;
        if (host()) {
            if (type == 10) captain_heal(player, std::abs(amount));
            else {
                ga::Source source;
                if (C.source && C.source(victim, attacker, type, source)) captain_damage(player, source, amount);
            }
        }
        return false; // guest native simulation never applies an independent captain hit
    }
    return true;
}

bool revive(uint8_t target, bool held)
{
    if (g_phase != Phase::Running || !member(target)) return false;
    g_revive_target = target;
    g_revive_held = held;
    if (host()) return g_authority.revive(g_descriptor.run_id, local_id(), target, held);
    net::Writer w; write_run(w, g_descriptor.run_id); w.u8(target); w.u8(held ? 1 : 0); send(K_REVIVE, w);
    return true;
}

bool complete(bool success)
{
    if (!host() || g_phase != Phase::Running || !g_authority.finish(g_descriptor.run_id, success)) return false;
    result(success);
    return true;
}

bool cancel(const std::string& reason)
{
    if (!active()) return false;
    abort_run(reason.empty() ? "Adventure cancelled." : reason, true);
    return true;
}

bool results_accepted(bool replay)
{
    if(!C.sess || !C.sess->connected() || g_phase!=Phase::Ended || !g_descriptor.run_id ||
        g_result_run!=g_descriptor.run_id || !member(local_id())) return false;
    if(host()) {
        g_results_accepted.insert(local_id());
        if(replay) g_replay_requested=true;
    } else {
        net::Writer w; write_run(w,g_descriptor.run_id); w.u8(replay?1:0); send(K_RESULTS_ACCEPTED,w);
    }
    if(replay) feed("Co-op replay requested. Waiting for the party to accept their results.");
    return true;
}

void reset()
{
    g_deferred_health.clear();
    clear_cleanup_messages();
    if (active()) abort_run("The party disconnected during the adventure.", false);
    g_generation++;
    adventure_assets::reset();
    g_barrier = {};
    g_authority = {};
    g_descriptor = {};
    g_phase = Phase::Idle;
    g_ready = g_content = g_chosen = g_launch_started = g_loaded_sent = g_waiting = false;
    g_revive_held = false;
    g_revive_target = ga::kNoPlayer;
    g_captain = {};
    g_seeds.clear();
    g_excluded.clear();
    g_client_captains.clear();
    g_client_snapshot_run = 0;
    g_client_snapshot_time = 0;
    g_next_run = g_newest_run = g_snapshot_revision = g_health_event = g_result_run = g_received_settings = 0;
    g_pending_result_run = 0; g_pending_result_success = g_pending_result_announce = false;
    g_last_wall = g_simulation = g_last_snapshot = g_last_revive = 0;
    g_have_wall = false;
    g_error.clear();
    g_preparation = {}; g_change_pending = false; g_preparation_dirty = false; g_last_preparation = 0;
    g_results_accepted.clear(); g_replay_requested=g_replay_started=false; g_replay_captain={};
    if (C.freeze) C.freeze(false);
}

std::string status()
{
    if (!g_error.empty()) return g_error;
    if (g_waiting) return "Waiting for the next adventure run";
    switch (g_phase) {
    case Phase::Collecting: return "Preparing adventure content";
    case Phase::Preparing: return g_chosen ? "Waiting for party readiness" : "Choose your captain";
    case Phase::Loading: return "Loading adventure: waiting for every captain";
    case Phase::Running: return "Adventure in progress";
    case Phase::Ended: return g_replay_requested ? "Replay: waiting for the party to accept results" : "Adventure finished";
    default: {
        std::string why = unavailable();
        return why.empty() ? "Select an adventure in Sporepedia and press Play" : why;
    }
    }
}

} // namespace adventure
