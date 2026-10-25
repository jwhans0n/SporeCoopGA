// Coordinator tests with real portable sessions and a fake native/content adapter.
#include "adventure.h"
#include "adventure_assets.h"

#include <chrono>
#include <cstdio>
#include <thread>
#include <utility>

static int failures;
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

namespace adventure_assets {
static Ctx context;
static unsigned preparations;
static uint64_t prepared_run;
static Key prepared_key;
static bool complete_on_offer;
static unsigned wrong_run_messages;
static std::vector<uint64_t> preparation_history;
static std::vector<std::pair<uint8_t,uint64_t>> delivered;
bool init(const SkHost*, SkMod*, Ctx ctx) { context = std::move(ctx); return true; }
bool prepare(uint64_t run, const Key& key)
{
    preparations++;
    preparation_history.push_back(run);
    prepared_run=run; prepared_key=key;
    if (context.sess->role() == net::Role::Host)
        context.prepared(run, key, std::string(64, 'a'));
    return true;
}
bool share_with(uint8_t) { return true; }
void on_message(uint8_t, net::Reader& r)
{
    uint8_t kind=r.u8();
    uint64_t run=r.u32(); run|=uint64_t(r.u32())<<32;
    if(!r.ok() || run!=prepared_run) {++wrong_run_messages;return;}
    delivered.push_back({kind,run});
    if(complete_on_offer && kind==64) context.prepared(run,prepared_key,std::string(64,'a'));
}
void tick(double) {}
uint8_t progress() { return 0; }
void reset() { prepared_run=0;prepared_key={}; }
void release() {}
std::string status() { return "fake assets"; }
} // namespace adventure_assets

static std::vector<std::pair<SkTaskFn, void*>> tasks;
static void queue(SkTaskFn fn, void* user) { tasks.push_back({fn, user}); }
static void drain_turn()
{
    auto pending = std::move(tasks);
    tasks.clear();
    for (auto [fn, user] : pending) fn(user);
}
static void drain()
{
    while (!tasks.empty()) {
        auto pending = std::move(tasks);
        tasks.clear();
        for (auto [fn, user] : pending) fn(user);
    }
}

static void put_run(net::Writer& w, uint64_t run) { w.u32(uint32_t(run)); w.u32(uint32_t(run >> 32)); }
static void put_key(net::Writer& w, const dbpf::Key& key) { w.u32(key.instance); w.u32(key.type); w.u32(key.group); }
static void incoming(uint8_t from, const net::Writer& w)
{
    net::Reader r(w.data.data(), w.size());
    adventure::on_message(from, r);
}
static void acknowledge(const ga::Descriptor& d, uint8_t peer)
{
    net::Writer content;
    content.u8(2); put_run(content, d.run_id); put_key(content, d.adventure);
    content.raw(d.manifest_hash.data(), d.manifest_hash.size()); incoming(peer, content);
    net::Writer captain;
    captain.u8(3); put_run(captain, d.run_id); put_run(captain, 1); put_key(captain, {uint32_t(peer) + 1, 10, 20}); incoming(peer, captain);
    net::Writer ready;
    ready.u8(4); put_run(ready, d.run_id); put_run(ready, 1); put_run(ready, 1); ready.u8(1); incoming(peer, ready);
}
static void acknowledge_loaded(const ga::Descriptor& d, uint8_t peer)
{
    net::Writer loaded;
    loaded.u8(6); put_run(loaded, d.run_id);
    loaded.f32(100); loaded.f32(float(peer)); loaded.f32(0); loaded.f32(0); incoming(peer, loaded);
}
static net::Writer captain_snapshot(uint64_t run, uint64_t revision, double now,
                                    ga::RunPhase phase = ga::RunPhase::Playing)
{
    net::Writer w;
    w.u8(9); put_run(w, run); put_run(w, revision);
    w.u8(uint8_t(phase)); w.u8(0); w.f64(now); w.u8(2);
    for (uint8_t player = 0; player < 2; ++player) {
        w.u8(player); w.u8(uint8_t(player ? ga::Life::Downed : ga::Life::Alive));
        w.f32(player ? 0 : 100); w.f32(100);
        w.f32(0); w.f32(0); w.f32(0);
        w.f64(0); w.f64(0);
        w.u8(player ? 0 : ga::kNoPlayer); w.f64(player ? 1 : 0);
    }
    return w;
}
static void acknowledge_result(uint64_t run, uint8_t peer, bool success)
{
    net::Writer applied;
    applied.u8(19); put_run(applied, run); applied.u8(success ? 1 : 0); incoming(peer, applied);
}

static bool connect(net::Session& host, net::Session& guest, const std::string& name)
{
    std::string error;
    if (!guest.join("127.0.0.1:" + std::to_string(host.port()), name, error)) return false;
    for (int i = 0; i < 1000; i++) {
        guest.update(); host.update(); guest.update();
        if (guest.connected()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}

int main()
{
    net::Session host, guest, guest2;
    host.build = guest.build = guest2.build = 1;
    std::string error;
    CHECK(host.host(0, "Host", error));
    CHECK(connect(host, guest, "Guest"));
    CHECK(connect(host, guest2, "Guest 2"));
    CHECK(host.players().size() == 3);

    SkHost api{};
    api.run_outside_frame = queue;
    bool available = false, loaded = false, frozen = false, paused = false;
    bool native_release_queued = false, native_settling = false, native_goals_pending = false;
    unsigned launches = 0, results = 0, invalidated = 0, native_aborts = 0;
    bool launch_ok = true;
    bool success = false;
    bool result_readback = true, native_result_success = false;
    uint64_t native_result_run = 0;
    bool result_exit_ready = true;
    unsigned result_exit_calls = 0;
    std::vector<std::string> notices;
    std::vector<ga::Captain> latest;
    float shield_divisor=1;
    unsigned shield_calls=0;
    int objects[4]{};
    adventure::Ctx ctx;
    ctx.sess = &host;
    ctx.feed = [&](const std::string& text) { notices.push_back(text); };
    ctx.invalidate_ready = [&] { invalidated++; };
    ctx.native_available = [&] { return available; };
    ctx.native_reason = [] { return "Native shared-adventure hooks need verification."; };
    ctx.collect = [](const dbpf::Key&, std::vector<dbpf::Record>&, std::string&) { return true; };
    ctx.choose_captain = [](const dbpf::Key& locked, auto selected) { selected(locked.instance ? locked : dbpf::Key{1, 10, 20}); };
    ctx.launch = [&](const dbpf::Key&, const dbpf::Key&, std::string& error) {
        launches++;
        if (!launch_ok) error = "fake native load failure";
        return launch_ok;
    };
    unsigned preparation_aborts = 0;
    ctx.abort_preparation = [&] { CHECK(!adventure::active()); preparation_aborts++; };
    ctx.abort_native = [&] { CHECK(!adventure::active()); native_aborts++; native_result_run = 0; };
    ctx.freeze = [&](bool value) { frozen = value; };
    ctx.loaded = [&](const dbpf::Key&) { return loaded; };
    ctx.paused = [&] { return paused; };
    ctx.captain_of = [&](uint8_t id) -> void* { return id < 4 ? &objects[id] : nullptr; };
    ctx.native_result = [&](bool value) {
        CHECK(!native_goals_pending); results++; success = value;
        native_result_run = adventure::outcome_run(); native_result_success = value;
    };
    ctx.native_result_applied = [&](bool value) {
        return result_readback && native_result_run && native_result_run == adventure::outcome_run() &&
               native_result_success == value;
    };
    ctx.prepare_result_exit = [&] { ++result_exit_calls; return result_exit_ready; };
    ctx.native_settling = [&] { return native_release_queued || native_settling || native_goals_pending; };
    ctx.sample = [](uint8_t id, ga::CaptainSeed& seed) {
        seed = {id, 100, {float(id) * 10, 0, 0}};
        return true;
    };
    ctx.apply = [&](const std::vector<ga::Captain>& captains) { latest = captains; };
    ctx.incoming_damage = [&](uint8_t, float amount) { ++shield_calls; return amount/shield_divisor; };
    ctx.source = [](void*, void*, int, ga::Source& source) { source = {ga::SourceKind::HostileActor, ga::kNoPlayer}; return true; };
    CHECK(adventure::init(&api, nullptr, ctx));
    const dbpf::Key key{123, 0x366A930D, 456};
    CHECK(!adventure::request_play(key));
    CHECK(adventure_assets::preparations == 0 && launches == 0);
    CHECK(adventure::status().find("verification") != std::string::npos);
    available = true;

    ga::Difficulty difficulty;
    difficulty.death_mode = ga::DeathMode::AutomaticRevive;
    difficulty.friendly_fire_percent = 25;
    CHECK(adventure::set_settings(difficulty));
    CHECK(invalidated != 0 && adventure::settings().friendly_fire_percent == 25);
    CHECK(adventure::request_play(key));
    CHECK(adventure::active() && adventure::current());
    adventure::CaptainState state;
    CHECK(!adventure::captain_state(0, state)); // no gameplay state before commit
    auto first = *adventure::current();
    CHECK(first.roster.size() == 3 && first.difficulty.health_multiplier(3) == 2);
    drain(); // host captain choice is outside-frame
    CHECK(launches == 0);
    acknowledge(first, guest.my_id());
    adventure::tick(1); drain();
    CHECK(launches == 0); // third member has not confirmed
    acknowledge(first, guest2.my_id());
    adventure::tick(1.5); drain();
    CHECK(launches == 0 && !adventure::ready()); // host must explicitly confirm too
    CHECK(adventure::set_ready(true));
    adventure::tick(2); drain();
    CHECK(launches == 1 && frozen);
    CHECK(!adventure::set_settings(difficulty));
    CHECK(!adventure::set_ready(false));
    loaded = true;
    adventure::tick(3); drain();
    CHECK(frozen); // host loaded, clients still loading
    acknowledge_loaded(first, guest.my_id());
    adventure::tick(4); drain();
    CHECK(frozen);
    acknowledge_loaded(first, guest2.my_id());
    adventure::tick(5); drain();
    CHECK(!frozen && latest.size() == 3);
    net::Writer forged_result;
    forged_result.u8(11); put_run(forged_result, first.run_id); forged_result.u8(1);
    incoming(guest.my_id(), forged_result); drain();
    CHECK(adventure::running() && results == 0); // clients cannot award or end a host run
    CHECK(adventure::captain_state(1, state) && state.captain.health == 100 && state.run_id == first.run_id);
    CHECK(state.automatic_remaining == -1 && state.revive_progress == 0);

    // Remote environmental hits and the local native damage path share exactly
    // one shield calculation before captain authority. Healing bypasses it.
    shield_divisor=2;
    CHECK(adventure::captain_damage(2,{ga::SourceKind::Environment,ga::kNoPlayer},20)); drain();
    CHECK(adventure::captain_state(2,state) && state.captain.health==90 && shield_calls==1);
    float shield_hit=20;
    CHECK(!adventure::damage_filter(&objects[2],shield_hit,0,nullptr)); drain();
    CHECK(adventure::captain_state(2,state) && state.captain.health==80 && shield_calls==2);
    CHECK(adventure::captain_heal(2,100)); drain();
    CHECK(adventure::captain_state(2,state) && state.captain.health==100 && shield_calls==2);
    shield_divisor=1;

    float amount = 100;
    CHECK(!adventure::damage_filter(&objects[1], amount, 0, nullptr)); drain();
    CHECK(latest[1].life == ga::Life::Downed && latest[1].health == 0);
    CHECK(adventure::captain_state(1, state) && state.captain.life == ga::Life::Downed);
    CHECK(state.automatic_remaining == 30 && state.revive_progress == 0);
    paused = true;
    adventure::tick(100); drain();
    CHECK(latest[1].life == ga::Life::Downed); // paused host time does not revive
    CHECK(adventure::captain_state(1, state) && state.automatic_remaining == 30);
    paused = false;
    adventure::tick(129.99); drain();
    CHECK(latest[1].life == ga::Life::Downed);
    adventure::tick(130.11); drain(); // network snapshots have a 100 ms send interval
    CHECK(latest[1].life == ga::Life::Alive && latest[1].health == 50);
    CHECK(adventure::captain_state(1, state) && state.grace_remaining == 2 && state.automatic_remaining == -1);

    // Reconnecting under a recycled numeric id must not regain authority in this run.
    adventure::on_leave(guest2.my_id()); drain();
    adventure::on_join(guest2.my_id());
    CHECK(!adventure::participating(guest2.my_id()));
    net::Writer forged_abort;
    forged_abort.u8(8); put_run(forged_abort, first.run_id); forged_abort.str16("reconnected peer");
    incoming(guest2.my_id(), forged_abort);
    CHECK(adventure::active());

    CHECK(adventure::complete(true));
    CHECK(!adventure::complete(true) && !adventure::complete(false));
    CHECK(adventure::outcome_locked() && adventure::outcome_run() == first.run_id);
    CHECK(!adventure::captain_damage(0, {ga::SourceKind::Environment, ga::kNoPlayer}, 100));
    CHECK(!adventure::captain_heal(1, 100) && !adventure::revive(1, true));
    CHECK(results == 0); // duplicate/conflicting completions cannot enqueue a second callback
    drain();
    CHECK(results == 1 && success && !adventure::active());
    CHECK(!adventure::captain_state(1, state));
    CHECK(!adventure::complete(true) && results == 1);
    CHECK(adventure::results_accepted(false)); drain();
    unsigned result_menu_aborts = native_aborts;

    // Stale acknowledgements cannot release the next run's barrier.
    loaded = false;
    CHECK(adventure::request_play(key)); drain();
    auto second = *adventure::current();
    CHECK(second.run_id > first.run_id);
    acknowledge(first, guest.my_id());
    acknowledge(first, guest2.my_id());
    adventure::tick(131); drain();
    CHECK(launches == 1);
    acknowledge(second, guest.my_id());
    acknowledge(second, guest2.my_id());
    CHECK(adventure::set_ready(true));
    adventure::tick(132); drain();
    CHECK(launches == 2 && frozen);
    loaded = true;
    acknowledge_loaded(second, guest.my_id());
    acknowledge_loaded(second, guest2.my_id());
    adventure::tick(133); drain();
    CHECK(!frozen);
    CHECK(adventure::captain_damage(1, {ga::SourceKind::Environment, ga::kNoPlayer}, 100));
    CHECK(adventure::captain_damage(2, {ga::SourceKind::Environment, ga::kNoPlayer}, 100));
    CHECK(adventure::captain_damage(0, {ga::SourceKind::Environment, ga::kNoPlayer}, 100)); drain();
    CHECK(results == 2 && !success && !adventure::active());
    adventure::tick(200); drain();
    CHECK(results == 2); // pending automatic timers cannot revive a wiped party
    CHECK(adventure::results_accepted(false)); drain();
    result_menu_aborts = native_aborts;

    // Reset discards queued launch work before it can call the native adapter.
    loaded = false;
    CHECK(adventure::request_play(key)); drain();
    auto third = *adventure::current();
    acknowledge(third, guest.my_id()); acknowledge(third, guest2.my_id());
    CHECK(adventure::set_ready(true));
    adventure::tick(201);
    adventure::reset(); drain();
    CHECK(launches == 2 && !frozen && !adventure::active());

    // A failed native load leaves the partial native mode once, with no result/reward.
    launch_ok = false;
    CHECK(adventure::request_play(key)); drain();
    const auto failed = *adventure::current();
    acknowledge(failed, guest.my_id()); acknowledge(failed, guest2.my_id());
    CHECK(adventure::set_ready(true));
    adventure::tick(201.5);
    CHECK(native_aborts == result_menu_aborts);
    drain();
    CHECK(launches == 3 && native_aborts == result_menu_aborts + 1 && results == 2 && !frozen);
    CHECK(!adventure::cancel("already cancelled")); drain();
    CHECK(native_aborts == result_menu_aborts + 1);
    launch_ok = true;
    // Cancelling captain selection must leave the surrounding native menu intact.
    CHECK(adventure::request_play(key));
    unsigned before_preparation_abort = preparation_aborts;
    CHECK(adventure::cancel("captain selection cancelled")); drain();
    CHECK(native_aborts == result_menu_aborts + 1 && results == 2);
    CHECK(preparation_aborts == before_preparation_abort + 1);
    // Reset invalidates a queued native abort before it can affect a later attempt.
    CHECK(adventure::request_play(key)); drain();
    const auto reset_abort = *adventure::current();
    acknowledge(reset_abort, guest.my_id()); acknowledge(reset_abort, guest2.my_id());
    CHECK(adventure::set_ready(true));
    adventure::tick(201.75);
    CHECK(adventure::cancel("cancel queued load"));
    adventure::reset(); drain();
    CHECK(native_aborts == result_menu_aborts + 1 && launches == 3 && results == 2);

    // Exercise the client trust boundary and its half of the loading barrier.
    ctx.sess = &guest;
    CHECK(adventure::init(&api, nullptr, ctx));
    net::Writer settings;
    settings.u8(0); put_run(settings, 100);
    auto remote = difficulty;
    remote.friendly_fire_percent = 50;
    remote.write(settings);
    incoming(2, settings);
    CHECK(adventure::settings().friendly_fire_percent == 25); // only host may configure
    incoming(0, settings);
    CHECK(adventure::settings().friendly_fire_percent == 50);
    CHECK(!adventure::set_settings(difficulty));
    net::Writer wait;
    wait.u8(12); put_run(wait, 99); incoming(0, wait);
    CHECK(!adventure::set_ready(true));
    auto client_descriptor = first;
    client_descriptor.run_id = 100;
    client_descriptor.roster = {0, guest.my_id()};
    client_descriptor.difficulty = remote;
    net::Writer prepare;
    prepare.u8(1); client_descriptor.write(prepare); incoming(0, prepare);
    CHECK(adventure::active());
    adventure_assets::context.prepared(100, key, std::string(64, 'a'));
    drain();
    ga::PreparationView view;
    CHECK(adventure::preparation(view));
    view.revision=1;
    for(auto& m:view.members) {
        m.content=m.validated=true; m.captain={1,10,20}; m.stage=ga::PreparationStage::Prepared;
        m.ready=m.player==0;
    }
    net::Writer preparation_packet; preparation_packet.u8(14); view.write(preparation_packet);
    incoming(2,preparation_packet);
    CHECK(!adventure::launch_ready(0)); // only the host publishes preparation
    incoming(0,preparation_packet);
    CHECK(adventure::launch_ready(0));
    view.members[0].ready=false;
    net::Writer stale_preparation; stale_preparation.u8(14); view.write(stale_preparation);
    incoming(0,stale_preparation);
    CHECK(adventure::launch_ready(0)); // same revision cannot overwrite readiness
    view.revision=2;
    net::Writer updated_preparation; updated_preparation.u8(14); view.write(updated_preparation);
    incoming(0,updated_preparation);
    CHECK(!adventure::launch_ready(0));
    CHECK(adventure::set_ready(true));
    net::Writer launch;
    launch.u8(5); put_run(launch, 100); incoming(2, launch); drain();
    CHECK(launches == 3); // a non-host cannot start the native adventure
    incoming(0, launch); drain();
    CHECK(launches == 4 && frozen);
    net::Writer commit;
    commit.u8(7); put_run(commit, 100); incoming(0, commit); drain();
    CHECK(frozen); // COMMIT is rejected before the local loaded acknowledgement
    loaded = true;
    adventure::tick(202); drain();
    incoming(0, commit); drain();
    CHECK(!frozen);
    CHECK(!adventure::captain_state(guest.my_id(), state)); // client waits for a host snapshot
    auto current_snapshot = captain_snapshot(100, 7, 2.5);
    incoming(2, current_snapshot); drain();
    CHECK(!adventure::captain_state(guest.my_id(), state));
    incoming(0, current_snapshot); drain();
    CHECK(adventure::captain_state(guest.my_id(), state));
    CHECK(state.captain.life == ga::Life::Downed && state.captain.reviver == 0);
    CHECK(state.simulation_time == 2.5 && state.revive_progress == .5f && state.automatic_remaining == 27.5);
    auto stale_snapshot = captain_snapshot(100, 6, 20);
    incoming(0, stale_snapshot); drain();
    CHECK(adventure::captain_state(guest.my_id(), state) && state.simulation_time == 2.5);
    net::Writer client_result;
    client_result.u8(11); put_run(client_result, 100); client_result.u8(1);
    incoming(2, client_result); drain();
    CHECK(results == 2 && adventure::active());
    // Release and terminal result can share a receive pump, before the queued
    // native release establishes its asynchronous cinematic ownership token.
    native_release_queued = true;
    native_goals_pending = true; // validated final Goals received earlier in the same pump
    incoming(0, client_result);
    incoming(0, client_result);
    net::Writer conflicting_result;
    conflicting_result.u8(11); put_run(conflicting_result, 100); conflicting_result.u8(0);
    incoming(0, conflicting_result);
    CHECK(results == 2 && adventure::running() && adventure::outcome_locked() && adventure::outcome_run() == 100);
    CHECK(!adventure::captain_damage(1, {ga::SourceKind::Environment, ga::kNoPlayer}, 1));
    CHECK(!adventure::captain_heal(1, 100) && !adventure::revive(1, true));
    auto late_playing = captain_snapshot(100, 100, 999);
    incoming(0, late_playing); drain();
    auto conflicting_snapshot = captain_snapshot(100, 101, 1000, ga::RunPhase::Failed);
    incoming(0, conflicting_snapshot); drain();
    CHECK(adventure::captain_state(guest.my_id(), state) && state.simulation_time == 2.5);
    drain();
    adventure::tick(202.1); drain();
    CHECK(results == 2 && adventure::running());
    native_settling = true; native_release_queued = false;
    adventure::tick(202.2); drain();
    CHECK(results == 2 && adventure::running());
    native_settling = false;
    adventure::tick(202.3); drain();
    CHECK(results == 2 && adventure::running()); // queued goal fields still precede results
    queue([](void* pending) { *static_cast<bool*>(pending) = false; }, &native_goals_pending);
    drain(); CHECK(results == 2 && adventure::running());
    adventure::tick(202.4); drain();
    CHECK(results == 3 && success && !adventure::active());
    incoming(0, client_result); drain();
    CHECK(results == 3);
    // A local unready click crossing the host launch must cancel, not deadlock the barrier.
    client_descriptor.run_id = 101;
    net::Writer next_prepare;
    next_prepare.u8(1); client_descriptor.write(next_prepare); incoming(0, next_prepare);
    incoming(0, client_result); drain();
    CHECK(adventure::active() && results == 3); // previous-run result cannot finish a new preparation
    adventure_assets::context.prepared(101, key, std::string(64, 'a')); drain();
    CHECK(!adventure::ready());
    net::Writer raced_launch;
    raced_launch.u8(5); put_run(raced_launch, 101); incoming(0, raced_launch); drain();
    CHECK(!adventure::active() && launches == 4 && native_aborts == result_menu_aborts + 1);
    CHECK(adventure::status().find("readiness changed") != std::string::npos);
    // An explicit reset invalidates queued native work. This is cancellation,
    // not a persistent reward transaction that survives disconnect/reload.
    client_descriptor.run_id = 102;
    net::Writer final_prepare;
    final_prepare.u8(1); client_descriptor.write(final_prepare); incoming(0, final_prepare);
    adventure_assets::context.prepared(102, key, std::string(64, 'a')); drain();
    CHECK(adventure::set_ready(true));
    net::Writer final_launch;
    final_launch.u8(5); put_run(final_launch, 102); incoming(0, final_launch); drain();
    adventure::tick(203); drain();
    net::Writer final_commit;
    final_commit.u8(7); put_run(final_commit, 102); incoming(0, final_commit); drain();
    net::Writer reset_result;
    reset_result.u8(11); put_run(reset_result, 102); reset_result.u8(1);
    native_settling = true;
    native_goals_pending = true;
    incoming(0, reset_result);
    CHECK(adventure::running());
    adventure::reset(); drain();
    CHECK(!adventure::outcome_locked() && !adventure::outcome_run());
    native_settling = native_goals_pending = false; adventure::tick(203.1); drain();
    CHECK(results == 3); // the old result cannot mutate the reset native session
    // Delayed native selection from a cancelled attempt cannot ready its retry.
    ctx.sess = &host;
    std::function<void(const dbpf::Key&)> pending_selection;
    ctx.choose_captain = [&](const dbpf::Key&, auto selected) { pending_selection = std::move(selected); };
    CHECK(adventure::init(&api, nullptr, ctx));
    CHECK(adventure::request_play(key)); drain();
    auto stale_selection = pending_selection;
    CHECK(stale_selection && !adventure::launch_ready(0));
    CHECK(adventure::cancel("cancel during naming"));
    CHECK(!adventure::request_play(key)); // native cleanup is still queued
    auto before_cleanup = preparation_aborts;
    adventure::reset(); drain(); // disconnect must not discard mandatory cleanup
    CHECK(preparation_aborts == before_cleanup + 1);
    CHECK(adventure::request_play(key)); drain();
    CHECK(!adventure::launch_ready(0));
    stale_selection({321, 10, 20}); drain();
    CHECK(!adventure::launch_ready(0));
    pending_selection({322, 10, 20}); drain();
    CHECK(!adventure::launch_ready(0));
    CHECK(adventure::set_ready(true));
    CHECK(adventure::launch_ready(0));
    auto replaced_selection = pending_selection;
    CHECK(adventure::change_captain()); drain();
    CHECK(!adventure::ready() && !adventure::can_ready());
    ga::PreparationView preparation_view;
    CHECK(adventure::preparation(preparation_view) && preparation_view.epoch == 2 && adventure::selection_serial(0)==2);
    replaced_selection({323,10,20}); drain();
    CHECK(!adventure::can_ready());
    pending_selection({324,10,20}); drain();
    CHECK(adventure::can_ready() && !adventure::launch_ready(0));
    net::Writer old_ready; old_ready.u8(4); put_run(old_ready,adventure::current()->run_id);
    put_run(old_ready,1); put_run(old_ready,1); old_ready.u8(1); incoming(guest.my_id(),old_ready);
    CHECK(!adventure::launch_ready(guest.my_id()));
    CHECK(adventure::cancel("retry finished")); drain();
    CHECK(!adventure::cancel("repeat cleanup")); drain();

    // Native results saving must finish on every connected participant before
    // Replay returns the party to preparation, with saved captains and no readiness.
    ctx.choose_captain=[](const dbpf::Key&,auto selected) { selected({330,10,20}); };
    unsigned reuses=0;
    ctx.reuse_captain=[&](const dbpf::Key& key,auto selected) { CHECK((key==dbpf::Key{330,10,20})); ++reuses; selected(key); };
    CHECK(adventure::init(&api,nullptr,ctx));
    CHECK(adventure::request_play(key)); drain();
    auto replay_descriptor=*adventure::current();
    for(uint8_t id:replay_descriptor.roster) if(id) acknowledge(replay_descriptor,id);
    CHECK(adventure::set_ready(true));
    loaded=true; adventure::tick(300); drain(); adventure::tick(301); drain();
    for(uint8_t id:replay_descriptor.roster) if(id) acknowledge_loaded(replay_descriptor,id);
    adventure::tick(302); drain(); CHECK(adventure::running());
    CHECK(adventure::complete(true)); drain();
    CHECK(adventure::results_accepted(true)); adventure::tick(303); drain();
    CHECK(!adventure::active());
    // A UI click cannot substitute for proof that a peer actually entered its
    // native result state. Wrong-outcome ACKs cannot satisfy that proof either.
    for(uint8_t id:replay_descriptor.roster) if(id) {
        net::Writer premature; premature.u8(17); put_run(premature,replay_descriptor.run_id); premature.u8(1); incoming(id,premature);
        acknowledge_result(replay_descriptor.run_id,id,false);
        incoming(id,premature);
    }
    adventure::tick(303.5); drain();
    CHECK(!adventure::active() && adventure::outcome_run()==replay_descriptor.run_id && reuses==0);
    for(uint8_t id:replay_descriptor.roster) if(id) {
        acknowledge_result(replay_descriptor.run_id,id,true);
        net::Writer accepted; accepted.u8(17); put_run(accepted,replay_descriptor.run_id); accepted.u8(1); incoming(id,accepted);
    }
    adventure::tick(304); drain();
    CHECK(adventure::active() && adventure::current()->run_id>replay_descriptor.run_id);
    CHECK((reuses==1 && adventure::selected_captain(0)==dbpf::Key{330,10,20} && !adventure::ready()));
    CHECK(!adventure::results_accepted(true));
    CHECK(adventure::cancel("replay test complete")); drain();

    // A Replay descriptor and its reliable transfer packets can share a receive
    // batch while the previous native mode is still cleaning up. Preserve their
    // order across that cleanup, including native captain messages routed by the
    // coordinator, rather than acknowledging manifests which were never applied.
    adventure::reset(); drain();
    ctx.sess=&guest;
    ctx.choose_captain=[](const dbpf::Key&,auto selected) {selected({440,10,20});};
    std::vector<std::pair<uint8_t,uint64_t>> native_delivered;
    ctx.native_message=[&](uint8_t from,net::Reader& r) {
        uint8_t kind=r.u8();
        uint64_t run=r.u32();run|=uint64_t(r.u32())<<32;
        CHECK(r.ok() && from==0 && kind==192);
        CHECK(adventure::current() && adventure::current()->run_id==run);
        CHECK(adventure_assets::prepared_run==run);
        CHECK(!adventure_assets::delivered.empty() && adventure_assets::delivered.back().second==run);
        native_delivered.push_back({kind,run});
    };
    CHECK(adventure::init(&api,nullptr,ctx));
    auto prepare_client=[&](uint64_t run) {
        auto descriptor=client_descriptor;descriptor.run_id=run;
        net::Writer w;w.u8(1);descriptor.write(w);incoming(0,w);
    };
    auto transfer_packet=[](uint8_t kind,uint64_t run,size_t bytes=0) {
        net::Writer w;w.u8(kind);put_run(w,run);
        if(bytes>w.size()) w.data.resize(bytes);
        return w;
    };
    auto begin_cleanup=[&](uint64_t run) {
        prepare_client(run);
        CHECK(adventure::active() && adventure::current()->run_id==run);
        adventure_assets::context.prepared(run,key,std::string(64,'a'));drain();
        CHECK(adventure::can_ready() && !adventure::ready());
        CHECK(adventure::cancel("previous run cleanup pending"));
    };
    auto clear_delivery=[&] {
        adventure_assets::delivered.clear();adventure_assets::wrong_run_messages=0;native_delivered.clear();
    };
    adventure_assets::complete_on_offer=true;
    begin_cleanup(1000);
    clear_delivery();
    prepare_client(1001);
    incoming(0,transfer_packet(64,1001));
    incoming(0,transfer_packet(192,1001));
    CHECK(adventure_assets::delivered.empty() && native_delivered.empty());
    CHECK(adventure_assets::wrong_run_messages==0 && !adventure::ready());
    drain(); // Mandatory native cleanup must finish before the FIFO is applied.
    CHECK(adventure_assets::delivered.empty() && native_delivered.empty());
    adventure::tick(400);drain();
    CHECK(adventure::current() && adventure::current()->run_id==1001);
    CHECK(adventure_assets::delivered.size()==1 && adventure_assets::delivered.front().first==64 && adventure_assets::delivered.front().second==1001);
    CHECK(native_delivered.size()==1 && native_delivered.front().first==192 && native_delivered.front().second==1001);
    CHECK(adventure_assets::wrong_run_messages==0);
    CHECK((adventure::selected_captain(guest.my_id())==dbpf::Key{440,10,20}));
    CHECK(adventure::can_ready() && !adventure::ready());
    CHECK(adventure::set_ready(true) && adventure::ready());
    CHECK(adventure::cancel("ordered replay complete"));drain();

    // Reset cancels deferred transfer work, while retaining the mandatory old
    // native cleanup. A host abort in the FIFO also discards all packets after it.
    begin_cleanup(1010);
    clear_delivery();
    prepare_client(1011);incoming(0,transfer_packet(64,1011));incoming(0,transfer_packet(192,1011));
    auto aborts_before_reset=preparation_aborts;
    adventure::reset();drain();adventure::tick(401);drain();
    CHECK(!adventure::active() && !adventure::ready());
    CHECK(preparation_aborts==aborts_before_reset+1);
    CHECK(adventure_assets::delivered.empty() && native_delivered.empty() && adventure_assets::wrong_run_messages==0);
    begin_cleanup(1020);
    clear_delivery();
    prepare_client(1021);
    net::Writer cancel_queued;cancel_queued.u8(8);put_run(cancel_queued,1021);cancel_queued.str16("cancel deferred Replay");
    incoming(0,cancel_queued);
    incoming(0,transfer_packet(64,1021));incoming(0,transfer_packet(192,1021));
    auto aborts_before_cancel=preparation_aborts;
    drain();adventure::tick(402);drain();
    CHECK(!adventure::active() && !adventure::ready() && !adventure::can_ready());
    CHECK(preparation_aborts==aborts_before_cancel+2);
    CHECK(adventure_assets::delivered.empty() && native_delivered.empty() && adventure_assets::wrong_run_messages==0);

    // Overflow must identify and abort the new run instead of losing its first
    // accepted packets silently. Exercise both the packet-count and byte bound.
    for(bool byte_bound:{false,true}) {
        uint64_t old_run=byte_bound?1040:1030,new_run=old_run+1;
        begin_cleanup(old_run);clear_delivery();
        auto aborts_before_overflow=preparation_aborts,launches_before_overflow=launches;
        prepare_client(new_run);
        unsigned count=byte_bound?40:520;
        for(unsigned i=0;i<count;++i) incoming(0,transfer_packet(192,new_run,byte_bound?65535:9));
        CHECK(adventure_assets::delivered.empty() && native_delivered.empty() && !adventure::ready());
        drain();adventure::tick(byte_bound?404:403);drain();
        CHECK(!adventure_assets::preparation_history.empty() && adventure_assets::preparation_history.back()==new_run);
        CHECK(!adventure::active() && !adventure::ready() && !adventure::can_ready());
        CHECK(preparation_aborts==aborts_before_overflow+2 && launches==launches_before_overflow);
        CHECK(adventure_assets::delivered.empty() && native_delivered.empty() && adventure_assets::wrong_run_messages==0);
        CHECK(guest.connected() && host.connected());
        adventure::tick(byte_bound?406:405);drain();
        CHECK(preparation_aborts==aborts_before_overflow+2);
    }
    adventure_assets::complete_on_offer=false;
    adventure::reset();drain();

    // Replay's native cleanup is mandatory even if reset/disconnect invalidates
    // the queued attempt before the outside-frame task runs. Its old transfer
    // queue must disappear, and reset must not leave future preparations blocked.
    prepare_client(1100);
    adventure_assets::context.prepared(1100,key,std::string(64,'a'));drain();
    CHECK(adventure::set_ready(true));
    incoming(0,transfer_packet(5,1100));drain();
    loaded=true;adventure::tick(500);drain();
    incoming(0,transfer_packet(7,1100));drain();
    CHECK(adventure::running());
    net::Writer ended_client;ended_client.u8(11);put_run(ended_client,1100);ended_client.u8(1);
    incoming(0,ended_client);drain();
    CHECK(adventure::results_accepted(true));
    auto cleanup_aborts=native_aborts,cleanup_launches=launches;
    incoming(0,transfer_packet(18,1100)); // Native Replay cleanup is now queued.
    CHECK(!tasks.empty() && !adventure::active());
    clear_delivery();
    prepare_client(1101);incoming(0,transfer_packet(64,1101));incoming(0,transfer_packet(192,1101));
    CHECK(adventure_assets::delivered.empty() && native_delivered.empty());
    adventure::reset();drain();adventure::tick(501);drain();
    CHECK(native_aborts==cleanup_aborts+1);
    CHECK(launches==cleanup_launches && !adventure::active() && !adventure::ready());
    CHECK(adventure_assets::delivered.empty() && native_delivered.empty());
    CHECK(adventure_assets::preparation_history.back()==1100); // Queued run1101 was discarded.
    CHECK(guest.connected());
    prepare_client(1102);
    bool replay_cleanup_recovered=adventure::active() && adventure::current()->run_id==1102;
    CHECK(replay_cleanup_recovered); // g_cleaning cannot remain stuck after reset.
    if(replay_cleanup_recovered) {
        CHECK(adventure::cancel("post-reset client retry"));drain();

        // The host must also perform mandatory cleanup once, while suppressing
        // Replay's automatic request_play after the reset changed generation.
        adventure::reset();drain();ctx.sess=&host;
        CHECK(adventure::init(&api,nullptr,ctx));
        CHECK(adventure::request_play(key));drain();
        const auto reset_replay=*adventure::current();
        for(uint8_t id:reset_replay.roster) if(id) acknowledge(reset_replay,id);
        CHECK(adventure::set_ready(true));
        loaded=true;adventure::tick(510);drain();adventure::tick(511);drain();
        for(uint8_t id:reset_replay.roster) if(id) acknowledge_loaded(reset_replay,id);
        adventure::tick(512);drain();CHECK(adventure::running());
        CHECK(adventure::complete(true));drain();
        CHECK(adventure::results_accepted(true));
        for(uint8_t id:reset_replay.roster) if(id) {
            acknowledge_result(reset_replay.run_id,id,true);
            net::Writer accepted;accepted.u8(17);put_run(accepted,reset_replay.run_id);accepted.u8(0);incoming(id,accepted);
        }
        auto host_cleanup_aborts=native_aborts,host_cleanup_launches=launches;
        auto host_preparations=adventure_assets::preparations;
        adventure::tick(513); // Replay cleanup queued; no native task has run yet.
        CHECK(!tasks.empty() && !adventure::active());
        adventure::reset();drain();adventure::tick(514);drain();
        CHECK(native_aborts==host_cleanup_aborts+1);
        CHECK(adventure_assets::preparations==host_preparations && launches==host_cleanup_launches);
        CHECK(!adventure::active() && !adventure::ready() && host.connected());
        CHECK(adventure::request_play(key));
        CHECK(adventure::cancel("post-reset host retry"));drain();
        adventure::reset();drain();
    }

    // A guest's environmental hit/heal can cross the final player's Pause in
    // the opposite direction. Source API admission must not mutate health or
    // life until Resume, and must retain the original arrival order.
    double health_clock = 600;
    auto health_run = [&] {
        adventure::reset(); drain(); paused = false; loaded = true;
        native_release_queued = native_settling = native_goals_pending = false;
        shield_divisor = 1; ctx.sess = &host; ctx.reuse_captain = {};
        ctx.choose_captain = [](const dbpf::Key&, auto selected) { selected({440,10,20}); };
        CHECK(adventure::init(&api, nullptr, ctx));
        ga::Difficulty settings; settings.death_mode = ga::DeathMode::AutomaticRevive;
        CHECK(adventure::set_settings(settings)); CHECK(adventure::request_play(key)); drain();
        const auto descriptor = *adventure::current();
        for (uint8_t id : descriptor.roster) if (id) acknowledge(descriptor,id);
        CHECK(adventure::set_ready(true)); adventure::tick(++health_clock); drain();
        adventure::tick(++health_clock); drain();
        for (uint8_t id : descriptor.roster) if (id) acknowledge_loaded(descriptor,id);
        adventure::tick(++health_clock); drain(); CHECK(adventure::running());
    };
    const ga::Source environment{ga::SourceKind::Environment,ga::kNoPlayer};
    health_run(); paused = true; shield_divisor = 2;
    auto shield_before = shield_calls;
    CHECK(adventure::captain_damage(1,environment,20)); CHECK(adventure::captain_heal(1,5));
    CHECK(adventure::captain_state(1,state) && state.captain.health == 100 && state.captain.life == ga::Life::Alive);
    shield_divisor = 4; adventure::tick(++health_clock); drain();
    CHECK(adventure::captain_state(1,state) && state.captain.health == 100 && shield_calls == shield_before + 1);
    paused = false; adventure::tick(++health_clock); drain();
    CHECK(adventure::captain_state(1,state) && state.captain.health == 95 && shield_calls == shield_before + 1);
    adventure::tick(++health_clock); drain();
    CHECK(adventure::captain_state(1,state) && state.captain.health == 95); // exact-once drain
    shield_divisor = 1; CHECK(adventure::captain_heal(1,100)); drain(); paused = true;
    CHECK(adventure::captain_damage(1,environment,100)); adventure::tick(++health_clock); drain();
    CHECK(adventure::captain_state(1,state) && state.captain.health == 100 && state.captain.life == ga::Life::Alive);
    paused = false; adventure::tick(++health_clock); drain();
    CHECK(adventure::captain_state(1,state) && state.captain.life == ga::Life::Downed && state.captain.health == 0);

    health_run(); paused = true;
    CHECK(adventure::captain_damage(1,environment,100)); CHECK(adventure::captain_heal(1,100));
    CHECK(adventure::captain_damage(0,{ga::SourceKind::Captain,1},100));
    adventure::on_leave(1); drain(); paused = false; adventure::tick(++health_clock); drain();
    CHECK(adventure::captain_state(1,state) && state.captain.life == ga::Life::Disconnected);
    CHECK(adventure::captain_state(0,state) && state.captain.health == 100); // disconnected source/recipient removed

    health_run(); paused = true; CHECK(adventure::captain_damage(0,environment,100));
    CHECK(adventure::cancel("cancel held health")); drain(); paused = false;
    adventure::tick(++health_clock); drain(); CHECK(!adventure::active());
    health_run(); adventure::tick(++health_clock); drain();
    CHECK(adventure::captain_state(0,state) && state.captain.health == 100); // cancelled attempt cannot affect retry
    paused = true; CHECK(adventure::captain_damage(0,environment,100)); adventure::reset(); drain();
    health_run(); adventure::tick(++health_clock); drain();
    CHECK(adventure::captain_state(0,state) && state.captain.health == 100); // reset invalidates held work

    paused = true;
    for (unsigned i = 0; i < 256; ++i) CHECK(adventure::captain_damage(0,environment,1));
    CHECK(adventure::running() && adventure::captain_state(0,state) && state.captain.health == 100);
    CHECK(!adventure::captain_damage(0,environment,1)); drain();
    CHECK(!adventure::active() && adventure::status().find("health synchronization") != std::string::npos);
    health_run(); adventure::tick(++health_clock); drain();
    CHECK(adventure::captain_state(0,state) && state.captain.health == 100);
    adventure::reset(); drain();

    // Sealing an outcome is immediate even when native cinematic/goal cleanup
    // still owns the result screen. Pending health and revive work must stop.
    health_run();
    const auto held_outcome_run = adventure::current()->run_id;
    const auto held_results = results;
    paused = true;
    CHECK(adventure::captain_damage(1,environment,100));
    native_settling = true;
    CHECK(adventure::complete(false));
    CHECK(adventure::running() && adventure::outcome_run()==held_outcome_run && adventure::outcome_locked());
    CHECK(!adventure::captain_damage(0,environment,100) && !adventure::captain_heal(0,100));
    CHECK(!adventure::revive(1,true) && !adventure::complete(true));
    paused = false; adventure::tick(++health_clock); drain();
    CHECK(results==held_results && adventure::captain_state(1,state) && state.captain.health==100);
    native_settling = false; adventure::tick(++health_clock); drain();
    CHECK(results==held_results+1 && !success && adventure::outcome_locked());
    CHECK(!adventure::captain_state(1,state));
    adventure::reset(); drain(); CHECK(!adventure::outcome_locked());

    // Peers can finish their native UI before the host's current cinematic or
    // goal callback settles. Keep their independent apply/accept facts while the
    // host is still Running, including accept-first receive order.
    for(bool accept_first:{false,true}) {
        health_run();
        const auto settling_descriptor=*adventure::current();
        const auto settling_results=results, settling_preparations=adventure_assets::preparations;
        native_settling=true;
        CHECK(adventure::complete(true) && adventure::running() && adventure::outcome_locked());
        uint8_t withheld=ga::kNoPlayer;
        for(uint8_t id:settling_descriptor.roster) if(id) {
            net::Writer accepted; accepted.u8(17); put_run(accepted,settling_descriptor.run_id); accepted.u8(1);
            if(accept_first) incoming(id,accepted);
            if(accept_first && id==settling_descriptor.roster.back()) withheld=id;
            else acknowledge_result(settling_descriptor.run_id,id,true);
            if(!accept_first) incoming(id,accepted);
        }
        adventure::tick(++health_clock); drain();
        CHECK(adventure::running() && results==settling_results && !adventure::results_accepted(true));
        CHECK(adventure_assets::preparations==settling_preparations);
        native_settling=false; adventure::tick(++health_clock); drain();
        CHECK(!adventure::active() && results==settling_results+1 && adventure::outcome_locked());
        adventure::tick(++health_clock); drain();
        CHECK(adventure_assets::preparations==settling_preparations); // host has not accepted
        CHECK(adventure::results_accepted(true));
        if(withheld!=ga::kNoPlayer) {
            adventure::tick(++health_clock); drain();
            CHECK(!adventure::active() && adventure::outcome_run()==settling_descriptor.run_id);
            CHECK(adventure_assets::preparations==settling_preparations); // missing actual native proof
            acknowledge_result(settling_descriptor.run_id,withheld,true);
        }
        adventure::tick(++health_clock); drain();
        CHECK(adventure::current() && adventure::current()->run_id>settling_descriptor.run_id);
        CHECK(adventure_assets::preparations==settling_preparations+1 && !adventure::ready());
        CHECK((adventure::selected_captain(0)==dbpf::Key{440,10,20}));
        CHECK(adventure::cancel("settling acknowledgement test complete")); drain();
    }

    // A callback having run does not prove native SetState survived its own
    // callbacks. Retry a missing native result, then accept only its readback.
    health_run(); result_readback = false;
    const auto unread_results = results;
    CHECK(adventure::complete(false)); drain();
    CHECK(results==unread_results+1 && adventure::outcome_locked() && !adventure::results_accepted(true));
    health_clock += .3; adventure::tick(health_clock); drain();
    CHECK(results==unread_results+2 && !adventure::results_accepted(false));
    result_readback = true;
    health_clock += .1; adventure::tick(health_clock); drain();
    CHECK(results==unread_results+2 && adventure::results_accepted(true));
    adventure::reset(); drain(); CHECK(!adventure::outcome_locked());

    // A persistent native result failure returns to the menu, retaining the
    // gameplay fence until mandatory cleanup has run. It cannot become freeplay.
    health_run(); result_readback = false;
    CHECK(adventure::complete(false)); drain();
    const auto timeout_aborts = native_aborts;
    health_clock += 11; adventure::tick(health_clock);
    CHECK(!adventure::active() && adventure::outcome_locked() && !tasks.empty());
    drain();
    CHECK(native_aborts==timeout_aborts+1 && !adventure::outcome_locked());
    CHECK(adventure::status().find("results could not be displayed")!=std::string::npos);
    result_readback = true;

    // Host Done is an authoritative shared exit even if guests have not applied
    // or accepted results. Its reliable menu packet must reach a real peer.
    health_run();
    const auto done_run = adventure::current()->run_id;
    CHECK(adventure::complete(false)); drain();
    // Earlier tests inject decoded packets directly. Drain their queued real
    // reliable traffic before timing the new shared-exit wire observation.
    for(int i=0;i<5000 && (host.pending_to(guest.my_id()) || host.pending_to(guest2.my_id()) ||
                          guest.pending_to(0) || guest2.pending_to(0));++i) {
        host.update(); guest.update(); guest2.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    unsigned return_packets = 0;
    const auto old_guest_custom = guest.cb.custom;
    guest.cb.custom = [&](uint8_t type,uint8_t from,net::Reader& packet) {
        if(type!=net::MSG_ADVENTURE || from || !packet.left()) return;
        const auto kind=packet.u8();
        uint64_t run=packet.u32(); run|=uint64_t(packet.u32())<<32;
        if(packet.ok() && kind==20 && run==done_run && !packet.left()) ++return_packets;
    };
    const auto done_aborts = native_aborts, done_preparations = adventure_assets::preparations;
    CHECK(adventure::results_accepted(false));
    CHECK(adventure::outcome_locked() && !adventure::results_accepted(true));
    for(const auto& player:host.players()) if(player.id) {
        const auto id=player.id;
        acknowledge_result(done_run,id,false);
        net::Writer late_replay; late_replay.u8(17); put_run(late_replay,done_run); late_replay.u8(1); incoming(id,late_replay);
    }
    for(int i=0;i<1000 && !return_packets;++i) {
        host.update(); guest.update(); guest2.update();
        if(!return_packets) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(return_packets==1); guest.cb.custom = old_guest_custom;
    drain(); adventure::tick(++health_clock); drain();
    CHECK(native_aborts==done_aborts+1 && adventure_assets::preparations==done_preparations);
    CHECK(!adventure::outcome_locked() && !adventure::active() && host.connected() && guest.connected());
    CHECK(!adventure::results_accepted(true));

    // A terminal health snapshot is itself an authoritative outcome. No separate
    // K_RESULT is required, and later Playing or conflicting results cannot win.
    adventure::reset(); drain(); ctx.sess=&guest; ctx.native_message={};
    unsigned guest_result_reuses=0;
    ctx.reuse_captain=[&](const dbpf::Key& saved,auto selected) {
        CHECK((saved==dbpf::Key{440,10,20})); ++guest_result_reuses; selected(saved);
    };
    CHECK(adventure::init(&api,nullptr,ctx));
    auto start_client = [&](uint64_t run) {
        native_release_queued=native_settling=native_goals_pending=false;
        prepare_client(run);
        adventure_assets::context.prepared(run,key,std::string(64,'a')); drain();
        CHECK(adventure::set_ready(true)); incoming(0,transfer_packet(5,run)); drain();
        loaded=true; adventure::tick(++health_clock); drain();
        incoming(0,transfer_packet(7,run)); drain(); CHECK(adventure::running());
    };
    start_client(2000);
    const auto terminal_results = results;
    native_goals_pending=true;
    auto failed_snapshot = captain_snapshot(2000,1,3,ga::RunPhase::Failed);
    incoming(2,failed_snapshot); CHECK(!adventure::outcome_locked());
    incoming(0,failed_snapshot);
    CHECK(adventure::outcome_locked() && adventure::outcome_run()==2000 && adventure::running());
    incoming(0,captain_snapshot(2000,2,400));
    incoming(0,captain_snapshot(2000,3,401,ga::RunPhase::Won));
    CHECK(adventure::captain_state(1,state) && state.simulation_time==3);
    native_goals_pending=false; adventure::tick(++health_clock); drain();
    CHECK(results==terminal_results+1 && !success && !adventure::active());
    std::vector<uint8_t> result_wire_order;
    const auto old_host_custom = host.cb.custom;
    host.cb.custom = [&](uint8_t type,uint8_t from,net::Reader& packet) {
        if(type!=net::MSG_ADVENTURE || from!=guest.my_id() || !packet.left()) return;
        const auto kind=packet.u8();
        uint64_t run=packet.u32(); run|=uint64_t(packet.u32())<<32;
        if(packet.ok() && run==2000 && (kind==19 || kind==17)) result_wire_order.push_back(kind);
    };
    CHECK(adventure::results_accepted(true));
    for(int i=0;i<1000 && result_wire_order.size()<2;++i) {
        guest.update(); host.update(); guest2.update();
        if(result_wire_order.size()<2) std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(result_wire_order==std::vector<uint8_t>({19,17})); host.cb.custom = old_host_custom;
    net::Writer late_win; late_win.u8(11); put_run(late_win,2000); late_win.u8(1); incoming(0,late_win); drain();
    CHECK(results==terminal_results+1 && !success && adventure::outcome_locked());

    // Host exit overtakes a guest's still-pending native results. Cleanup wins
    // once, and late results or Replay cannot revive the departed attempt.
    adventure::reset(); drain(); start_client(2001);
    const auto pending_exit_results = results, pending_exit_aborts = native_aborts;
    native_goals_pending=true;
    incoming(0,captain_snapshot(2001,1,4,ga::RunPhase::Failed));
    CHECK(adventure::outcome_locked() && adventure::running());
    incoming(2,transfer_packet(20,2001)); CHECK(adventure::running());
    incoming(0,transfer_packet(20,2001));
    CHECK(!adventure::active() && adventure::outcome_locked());
    incoming(0,transfer_packet(20,2001)); drain_turn();
    CHECK(native_aborts==pending_exit_aborts && results==pending_exit_results && adventure::outcome_locked());
    native_goals_pending=false; adventure::tick(++health_clock); drain();
    CHECK(native_aborts==pending_exit_aborts+1 && results==pending_exit_results+1 && !adventure::outcome_locked());
    incoming(0,transfer_packet(18,2001));
    net::Writer late_failure; late_failure.u8(11); put_run(late_failure,2001); late_failure.u8(0); incoming(0,late_failure); drain();
    CHECK(native_aborts==pending_exit_aborts+1 && results==pending_exit_results+1 && !adventure::active());

    // Reset while result application is queued must cancel it, retain mandatory
    // menu cleanup, and release the terminal fence for the next native run.
    start_client(2002);
    const auto reset_results = results, reset_native_aborts = native_aborts;
    incoming(0,captain_snapshot(2002,1,5,ga::RunPhase::Failed));
    CHECK(adventure::outcome_locked() && !tasks.empty());
    adventure::reset(); drain();
    CHECK(results==reset_results && native_aborts==reset_native_aborts+1 && !adventure::outcome_locked());

    // A guest can accept Done locally and still join a later host Replay. The
    // host's new descriptor remains ordered behind that final native cleanup.
    start_client(2003);
    incoming(0,captain_snapshot(2003,1,6,ga::RunPhase::Won)); drain();
    const auto local_done_aborts=native_aborts, local_done_reuses=guest_result_reuses;
    CHECK(adventure::results_accepted(false)); drain();
    CHECK(native_aborts==local_done_aborts+1);
    CHECK(!adventure::outcome_locked() && !adventure::active() && guest.connected());
    incoming(0,transfer_packet(18,2003));
    prepare_client(2004);
    drain(); adventure::tick(++health_clock); drain();
    CHECK(adventure::current() && adventure::current()->run_id==2004 && !adventure::ready());
    adventure_assets::context.prepared(2004,key,std::string(64,'a')); drain();
    CHECK(native_aborts==local_done_aborts+1 && guest_result_reuses==local_done_reuses+1);
    CHECK((adventure::selected_captain(guest.my_id())==dbpf::Key{440,10,20}));
    CHECK(adventure::cancel("outcome tests complete")); drain(); adventure::reset(); drain();

    // Host Done arriving after local Done cleanup cannot recreate a terminal
    // latch for an already departed native attempt or repeat native teardown.
    start_client(2010);
    incoming(0,captain_snapshot(2010,1,7,ga::RunPhase::Won)); drain();
    const auto completed_done_aborts=native_aborts;
    CHECK(adventure::results_accepted(false)); drain();
    CHECK(native_aborts==completed_done_aborts+1 && !adventure::outcome_locked());
    incoming(0,transfer_packet(20,2010)); incoming(0,transfer_packet(20,2010));
    CHECK(!adventure::outcome_locked() && !adventure::outcome_run() && tasks.empty());
    adventure::tick(++health_clock); drain();
    CHECK(native_aborts==completed_done_aborts+1 && !adventure::active());

    // Done and Replay share one mandatory cleanup lease. An early new manifest
    // waits for that lease, and the saved captain survives the successful retry.
    start_client(2020);
    incoming(0,captain_snapshot(2020,1,8,ga::RunPhase::Won)); drain();
    const auto pending_replay_aborts=native_aborts, pending_replay_reuses=guest_result_reuses;
    CHECK(adventure::results_accepted(false));
    incoming(0,transfer_packet(18,2020)); incoming(0,transfer_packet(18,2020));
    prepare_client(2021);
    CHECK(!adventure::active() && !tasks.empty() && adventure_assets::prepared_run!=2021);
    drain(); CHECK(native_aborts==pending_replay_aborts+1 && !adventure::active());
    adventure::tick(++health_clock); drain();
    CHECK(adventure::current() && adventure::current()->run_id==2021 && !adventure::ready());
    adventure_assets::context.prepared(2021,key,std::string(64,'a')); drain();
    CHECK(native_aborts==pending_replay_aborts+1 && guest_result_reuses==pending_replay_reuses+1);
    CHECK((adventure::selected_captain(guest.my_id())==dbpf::Key{440,10,20}));
    CHECK(adventure::cancel("pending Done Replay complete")); drain(); adventure::reset(); drain();

    // A final host exit beats a queued retry; repeated/reordered old commands
    // cannot run two native teardowns or bring back the ended attempt.
    start_client(2030);
    incoming(0,captain_snapshot(2030,1,9,ga::RunPhase::Won)); drain();
    const auto crossed_exit_aborts=native_aborts, crossed_exit_preparations=adventure_assets::preparations;
    CHECK(adventure::results_accepted(false)); incoming(0,transfer_packet(18,2030));
    incoming(0,transfer_packet(20,2030)); incoming(0,transfer_packet(18,2030)); incoming(0,transfer_packet(20,2030));
    CHECK(!adventure::active() && adventure::outcome_locked()); drain();
    adventure::tick(++health_clock); drain();
    CHECK(native_aborts==crossed_exit_aborts+1 && !adventure::outcome_locked() && !adventure::active());
    CHECK(adventure_assets::preparations==crossed_exit_preparations);

    // Reset/disconnect invalidates retry work but retains the already queued
    // mandatory native cleanup. A new preparation cannot overtake that cleanup.
    start_client(2040);
    incoming(0,captain_snapshot(2040,1,10,ga::RunPhase::Won)); drain();
    const auto crossed_reset_aborts=native_aborts, crossed_reset_preparations=adventure_assets::preparations;
    CHECK(adventure::results_accepted(false)); incoming(0,transfer_packet(18,2040)); prepare_client(2041);
    CHECK(!adventure::active() && adventure_assets::prepared_run!=2041);
    adventure::reset(); drain(); adventure::tick(++health_clock); drain();
    CHECK(native_aborts==crossed_reset_aborts+1 && !adventure::active() && !adventure::outcome_locked());
    CHECK(adventure_assets::preparations==crossed_reset_preparations);

    start_client(2050);
    incoming(0,captain_snapshot(2050,1,11,ga::RunPhase::Won)); drain();
    const auto crossed_abort_aborts=native_aborts;
    CHECK(adventure::results_accepted(false)); incoming(0,transfer_packet(18,2050));
    net::Writer crossed_abort; crossed_abort.u8(8); put_run(crossed_abort,2050); crossed_abort.str16("host aborted crossed result cleanup");
    incoming(0,crossed_abort); incoming(0,crossed_abort);
    CHECK(!adventure::active()); drain(); adventure::tick(++health_clock); drain();
    CHECK(native_aborts==crossed_abort_aborts+1 && !adventure::outcome_locked() && !adventure::active());

    // Shared exits must preserve the normal captain save/reward choice before
    // native teardown. Waiting neither spins the outside-frame queue nor admits
    // the next adventure, and Done/Replay keep one cleanup lease.
    start_client(2060);
    incoming(0,captain_snapshot(2060,1,12,ga::RunPhase::Won)); drain();
    const auto saving_aborts=native_aborts, saving_calls=result_exit_calls, saving_reuses=guest_result_reuses;
    result_exit_ready=false;
    CHECK(adventure::results_accepted(false)); drain_turn();
    CHECK(native_aborts==saving_aborts && result_exit_calls==saving_calls+1 && adventure::outcome_locked());
    CHECK(adventure::results_accepted(false)); incoming(0,transfer_packet(18,2060)); prepare_client(2061);
    CHECK(!adventure::active() && adventure_assets::prepared_run!=2061);
    adventure::tick(++health_clock); drain_turn();
    CHECK(native_aborts==saving_aborts && result_exit_calls==saving_calls+2 && adventure::outcome_locked());
    CHECK(!adventure::active() && adventure_assets::prepared_run!=2061);
    result_exit_ready=true; adventure::tick(++health_clock); drain();
    CHECK(native_aborts==saving_aborts+1 && !adventure::outcome_locked());
    adventure::tick(++health_clock); drain();
    CHECK(adventure::current() && adventure::current()->run_id==2061);
    adventure_assets::context.prepared(2061,key,std::string(64,'a')); drain();
    CHECK(guest_result_reuses==saving_reuses+1 && !adventure::ready());
    CHECK((adventure::selected_captain(guest.my_id())==dbpf::Key{440,10,20}));
    CHECK(adventure::cancel("saved result retry complete")); drain(); adventure::reset(); drain();

    // A host exit can reach a guest before native result application. Finish its
    // old scene, apply the authoritative result, then complete the ordinary save
    // before allowing the native mode to leave.
    start_client(2062);
    const auto save_pending_results=results, save_pending_aborts=native_aborts, save_pending_calls=result_exit_calls;
    native_goals_pending=true;
    incoming(0,captain_snapshot(2062,1,13,ga::RunPhase::Won)); incoming(0,transfer_packet(20,2062));
    result_exit_ready=false; drain_turn();
    CHECK(results==save_pending_results && native_aborts==save_pending_aborts && result_exit_calls==save_pending_calls);
    CHECK(adventure::outcome_locked() && !adventure::active());
    native_goals_pending=false; result_readback=false;
    adventure::tick(++health_clock); drain_turn();
    CHECK(results==save_pending_results+1 && native_aborts==save_pending_aborts && result_exit_calls==save_pending_calls);
    result_readback=true; adventure::tick(++health_clock); drain_turn();
    CHECK(results==save_pending_results+1 && native_aborts==save_pending_aborts && result_exit_calls==save_pending_calls+1);
    CHECK(adventure::outcome_locked());
    result_exit_ready=true; adventure::tick(++health_clock); drain();
    CHECK(native_aborts==save_pending_aborts+1 && !adventure::outcome_locked() && !adventure::active());

    // Explicit reset/disconnect cancels a pending reward choice. It supersedes
    // the save wait but keeps the same mandatory native cleanup, which must finish
    // before another manifest can start preparing the next attempt.
    start_client(2063);
    incoming(0,captain_snapshot(2063,1,14,ga::RunPhase::Won)); drain();
    const auto save_reset_aborts=native_aborts;
    result_exit_ready=false; CHECK(adventure::results_accepted(false)); drain_turn();
    CHECK(native_aborts==save_reset_aborts && adventure::outcome_locked());
    CHECK(adventure::results_accepted(false)); incoming(0,transfer_packet(18,2063));
    adventure::reset(); prepare_client(2064);
    CHECK(!adventure::active() && adventure_assets::prepared_run!=2064);
    drain(); adventure::tick(++health_clock); drain();
    CHECK(native_aborts==save_reset_aborts+1 && !adventure::outcome_locked());
    CHECK(adventure::current() && adventure::current()->run_id==2064);
    result_exit_ready=true; CHECK(adventure::cancel("cancelled reward wait cleanup complete")); drain(); adventure::reset(); drain();

    // Exercise the actual session-disconnected predicate, then the normal reset
    // notification, while Done and Replay still own a single native cleanup.
    start_client(2100);
    incoming(0,captain_snapshot(2100,1,15,ga::RunPhase::Won)); drain();
    const auto disconnected_aborts=native_aborts;
    CHECK(adventure::results_accepted(false)); incoming(0,transfer_packet(18,2100));
    guest.leave(); adventure::tick(++health_clock); adventure::reset(); drain();
    CHECK(native_aborts==disconnected_aborts+1 && !adventure::outcome_locked() && !adventure::active());

    guest.leave(); guest2.leave(); host.leave();
    std::printf("adventure coordinator: %s (%d failures)\n", failures ? "FAILED" : "passed", failures);
    return failures ? 1 : 0;
}
