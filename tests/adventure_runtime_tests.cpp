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
bool init(const SkHost*, SkMod*, Ctx ctx) { context = std::move(ctx); return true; }
bool prepare(uint64_t run, const Key& key)
{
    preparations++;
    if (context.sess->role() == net::Role::Host)
        context.prepared(run, key, std::string(64, 'a'));
    return true;
}
bool share_with(uint8_t) { return true; }
void on_message(uint8_t, net::Reader&) {}
void tick(double) {}
uint8_t progress() { return 0; }
void reset() {}
void release() {}
std::string status() { return "fake assets"; }
} // namespace adventure_assets

static std::vector<std::pair<SkTaskFn, void*>> tasks;
static void queue(SkTaskFn fn, void* user) { tasks.push_back({fn, user}); }
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
static net::Writer captain_snapshot(uint64_t run, uint64_t revision, double now)
{
    net::Writer w;
    w.u8(9); put_run(w, run); put_run(w, revision);
    w.u8(uint8_t(ga::RunPhase::Playing)); w.u8(0); w.f64(now); w.u8(2);
    for (uint8_t player = 0; player < 2; ++player) {
        w.u8(player); w.u8(uint8_t(player ? ga::Life::Downed : ga::Life::Alive));
        w.f32(player ? 0 : 100); w.f32(100);
        w.f32(0); w.f32(0); w.f32(0);
        w.f64(0); w.f64(0);
        w.u8(player ? 0 : ga::kNoPlayer); w.f64(player ? 1 : 0);
    }
    return w;
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
    unsigned launches = 0, results = 0, invalidated = 0, native_aborts = 0;
    bool launch_ok = true;
    bool success = false;
    std::vector<std::string> notices;
    std::vector<ga::Captain> latest;
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
    ctx.abort_native = [&] { CHECK(!adventure::active()); native_aborts++; };
    ctx.freeze = [&](bool value) { frozen = value; };
    ctx.loaded = [&](const dbpf::Key&) { return loaded; };
    ctx.paused = [&] { return paused; };
    ctx.captain_of = [&](uint8_t id) -> void* { return id < 4 ? &objects[id] : nullptr; };
    ctx.native_result = [&](bool value) { results++; success = value; };
    ctx.sample = [](uint8_t id, ga::CaptainSeed& seed) {
        seed = {id, 100, {float(id) * 10, 0, 0}};
        return true;
    };
    ctx.apply = [&](const std::vector<ga::Captain>& captains) { latest = captains; };
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
    CHECK(results == 0); // duplicate/conflicting completions cannot enqueue a second callback
    drain();
    CHECK(results == 1 && success && !adventure::active());
    CHECK(!adventure::captain_state(1, state));
    CHECK(!adventure::complete(true) && results == 1);

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
    CHECK(native_aborts == 0);
    drain();
    CHECK(launches == 3 && native_aborts == 1 && results == 2 && !frozen);
    CHECK(!adventure::cancel("already cancelled")); drain();
    CHECK(native_aborts == 1);
    launch_ok = true;
    // Cancelling captain selection must leave the surrounding native menu intact.
    CHECK(adventure::request_play(key));
    unsigned before_preparation_abort = preparation_aborts;
    CHECK(adventure::cancel("captain selection cancelled")); drain();
    CHECK(native_aborts == 1 && results == 2);
    CHECK(preparation_aborts == before_preparation_abort + 1);
    // Reset invalidates a queued native abort before it can affect a later attempt.
    CHECK(adventure::request_play(key)); drain();
    const auto reset_abort = *adventure::current();
    acknowledge(reset_abort, guest.my_id()); acknowledge(reset_abort, guest2.my_id());
    CHECK(adventure::set_ready(true));
    adventure::tick(201.75);
    CHECK(adventure::cancel("cancel queued load"));
    adventure::reset(); drain();
    CHECK(native_aborts == 1 && launches == 3 && results == 2);

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
    incoming(0, client_result);
    incoming(0, client_result);
    net::Writer conflicting_result;
    conflicting_result.u8(11); put_run(conflicting_result, 100); conflicting_result.u8(0);
    incoming(0, conflicting_result);
    CHECK(results == 2);
    drain();
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
    CHECK(!adventure::active() && launches == 4 && native_aborts == 1);
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
    incoming(0, reset_result);
    adventure::reset(); drain();
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
    ctx.reuse_captain=[&](const dbpf::Key& key,auto selected) { CHECK(key==dbpf::Key(330,10,20)); ++reuses; selected(key); };
    CHECK(adventure::init(&api,nullptr,ctx));
    CHECK(adventure::request_play(key)); drain();
    auto replay_descriptor=*adventure::current();
    for(uint8_t id:replay_descriptor.roster) if(id) acknowledge(replay_descriptor,id);
    CHECK(adventure::set_ready(true));
    loaded=true; adventure::tick(300); drain(); adventure::tick(301); drain();
    for(uint8_t id:replay_descriptor.roster) if(id) acknowledge_loaded(replay_descriptor,id);
    adventure::tick(302); drain(); CHECK(adventure::running());
    CHECK(adventure::complete(true)); drain();
    CHECK(adventure::results_accepted(false)); adventure::tick(303); drain();
    CHECK(!adventure::active());
    for(uint8_t id:replay_descriptor.roster) if(id) {
        net::Writer accepted; accepted.u8(17); put_run(accepted,replay_descriptor.run_id); accepted.u8(1); incoming(id,accepted);
    }
    adventure::tick(304); drain();
    CHECK(adventure::active() && adventure::current()->run_id>replay_descriptor.run_id);
    CHECK(reuses==1 && adventure::selected_captain(0)==dbpf::Key(330,10,20) && !adventure::ready());
    CHECK(!adventure::results_accepted(true));
    CHECK(adventure::cancel("replay test complete")); drain();

    guest.leave(); guest2.leave(); host.leave();
    std::printf("adventure coordinator: %s (%d failures)\n", failures ? "FAILED" : "passed", failures);
    return failures ? 1 : 0;
}
