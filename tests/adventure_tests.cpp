// Standalone portable rules tests. No installed game or player profile required.
#include "net/adventure.h"

#include <cmath>
#include <cstdio>
#include <limits>

using namespace ga;

static int failures;
#define CHECK(condition) do { if (!(condition)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #condition); ++failures; } } while (0)

static Descriptor descriptor(uint64_t run = 1, size_t count = 2)
{
    Descriptor d;
    d.run_id = run;
    d.adventure = {0x12345678, 0x366A930D, 0xDEADBEEF};
    d.manifest_hash[0] = 0xA1;
    d.manifest_hash[31] = 0xB2;
    for (size_t i = 0; i < count; i++)
        d.roster.push_back(static_cast<uint8_t>(i));
    return d;
}

static std::vector<CaptainSeed> seeds(const Descriptor& d)
{
    std::vector<CaptainSeed> out;
    for (uint8_t player : d.roster)
        out.push_back({player, 100.f, {float(player), 0, 0}});
    return out;
}

static Authority authority(DeathMode mode = DeathMode::ManualRevive, size_t count = 2)
{
    Authority a;
    auto d = descriptor(1, count);
    d.difficulty.death_mode = mode;
    CHECK(a.start(d, seeds(d), 0));
    return a;
}

static Source enemy() { return {SourceKind::HostileActor, kNoPlayer}; }

static void test_difficulty()
{
    Difficulty d;
    CHECK(d.valid());
    CHECK(d.health_multiplier(1) == 1.f && d.health_multiplier(2) == 1.5f);
    CHECK(d.health_multiplier(3) == 2.f && d.health_multiplier(4) == 2.5f);
    CHECK(d.health_multiplier(0) == 0 && d.health_multiplier(5) == 0);
    CHECK(d.damage_multiplier() == 1.f);
    d.enemy_health_override = 2;
    d.enemy_damage_override = 3;
    CHECK(d.health_multiplier(4) == 2.f && d.damage_multiplier() == 3.f);
    CHECK(2.f * d.health_multiplier(4) == 4.f); // authored tuning multiplied once
    for (float invalid : {-1.f, .24f, 10.01f, std::numeric_limits<float>::infinity(),
                          std::numeric_limits<float>::quiet_NaN()}) {
        d.enemy_health_override = invalid;
        CHECK(!d.valid() && d.health_multiplier(4) == 0);
    }
    d.enemy_health_override = .25f;
    d.enemy_damage_override = 10;
    CHECK(d.valid());
    for (int ff = 0; ff <= 100; ff++) {
        d.friendly_fire_percent = static_cast<uint8_t>(ff);
        CHECK(d.valid() == (ff == 0 || ff == 25 || ff == 50 || ff == 100));
    }
    d.friendly_fire_percent = 0;
    d.death_mode = static_cast<DeathMode>(3);
    CHECK(!d.valid());
}

static void test_wire()
{
    auto original = descriptor(0x123456789ABCDEF0ULL, 4);
    original.locked_captain = {123, 456, 789};
    original.difficulty.death_mode = DeathMode::AutomaticRevive;
    original.difficulty.friendly_fire_percent = 25;
    original.difficulty.enemy_damage_override = 1.75f;
    net::Writer w;
    original.write(w);
    Descriptor result;
    net::Reader r(w.data.data(), w.size());
    CHECK(result.read(r) && r.left() == 0);
    CHECK(result.run_id == original.run_id && result.adventure == original.adventure);
    CHECK(result.manifest_hash == original.manifest_hash && result.roster == original.roster);
    CHECK(result.locked_captain == original.locked_captain);
    CHECK(result.difficulty.death_mode == DeathMode::AutomaticRevive);
    CHECK(result.difficulty.friendly_fire_percent == 25 && result.difficulty.enemy_damage_override == 1.75f);
    for (size_t n = 0; n < w.size(); n++) {
        Descriptor unchanged = descriptor(77);
        net::Reader truncated(w.data.data(), n);
        CHECK(!unchanged.read(truncated) && unchanged.run_id == 77);
    }
    Difficulty unchanged;
    net::Writer bad;
    bad.u8(9); bad.f32(0); bad.f32(0); bad.u8(0);
    net::Reader invalid_mode(bad.data.data(), bad.size());
    CHECK(!unchanged.read(invalid_mode) && unchanged.death_mode == DeathMode::ManualRevive);
    auto invalid = original;
    invalid.roster.push_back(4);
    net::Writer oversized;
    invalid.write(oversized);
    net::Reader over(oversized.data.data(), oversized.size());
    CHECK(!result.read(over));
}

static void test_descriptor_validation()
{
    auto d = descriptor();
    CHECK(d.valid());
    d.adventure.instance = 0;
    CHECK(!d.valid());
    d = descriptor(); d.adventure.type = 0;
    CHECK(!d.valid());
    d = descriptor(); d.run_id = 0;
    CHECK(!d.valid());
    d = descriptor(); d.manifest_hash = {};
    CHECK(!d.valid());
    d = descriptor(); d.roster = {1, 2};
    CHECK(!d.valid());
    d = descriptor(); d.roster = {0, 0};
    CHECK(!d.valid());
    d = descriptor(); d.roster = {0, 255};
    CHECK(!d.valid());
    d = descriptor(); d.locked_captain = {0, 1, 2};
    CHECK(!d.valid());
}

static void prepare(LaunchBarrier& b)
{
    const auto d = b.descriptor();
    // Captain changes clear everybody's readiness, so acknowledge all choices first.
    for (uint8_t player : d.roster) {
        CHECK(b.ack_content(d.run_id, player, d.adventure, d.manifest_hash));
        CHECK(b.ack_captain(d.run_id, player, {uint32_t(player) + 1, 10, 20}));
    }
    for (uint8_t player : d.roster)
        CHECK(b.set_ready(d.run_id, player, true));
}

static void test_launch_barrier()
{
    LaunchBarrier b;
    auto d = descriptor();
    CHECK(!b.all_ready() && !b.all_loaded() && !b.commit(1));
    CHECK(b.begin(d));
    CHECK(!b.begin(d));
    CHECK(!b.set_ready(1, 0, true));
    CHECK(!b.ack_loaded(1, 0));
    CHECK(!b.begin_loading(1));
    auto wrong_hash = d.manifest_hash;
    wrong_hash[31]++;
    auto wrong_key = d.adventure;
    wrong_key.type++;
    CHECK(!b.ack_content(1, 0, wrong_key, d.manifest_hash));
    CHECK(!b.ack_content(1, 0, d.adventure, wrong_hash));
    CHECK(!b.ack_content(2, 0, d.adventure, d.manifest_hash));
    CHECK(!b.ack_content(1, 7, d.adventure, d.manifest_hash));
    prepare(b);
    CHECK(b.all_ready());
    CHECK(b.ack_captain(1, 0, {1, 10, 20}) && b.all_ready()); // duplicate is idempotent
    CHECK(b.ack_captain(1, 0, {9, 10, 20}) && !b.all_ready());
    CHECK(b.set_ready(1, 0, true) && b.set_ready(1, 1, true));
    CHECK(b.begin_loading(1));
    CHECK(!b.begin(descriptor(2)) && !b.set_ready(1, 1, false));
    CHECK(!b.ack_loaded(0, 0) && !b.ack_loaded(1, 3));
    CHECK(b.ack_loaded(1, 0) && !b.commit(1));
    CHECK(b.ack_loaded(1, 1) && b.all_loaded());
    CHECK(b.commit(1) && b.phase() == LaunchPhase::Running);
    CHECK(!b.commit(1) && !b.ack_loaded(1, 1));
    CHECK(!b.begin(descriptor(2)));
    CHECK(b.abort(1) && !b.abort(1));
    CHECK(b.begin(descriptor(2)));
    CHECK(!b.ack_loaded(1, 0) && !b.set_ready(1, 0, true));
    CHECK(b.disconnect(2, 1) && b.phase() == LaunchPhase::Aborted);
    CHECK(!b.ack_captain(2, 1, {1, 2, 3}) && !b.commit(2));
    CHECK(!b.begin(descriptor(1)));

    LaunchBarrier locked;
    d.locked_captain = {42, 43, 44};
    CHECK(locked.begin(d));
    CHECK(!locked.ack_captain(1, 0, {42, 43, 45}));
    CHECK(locked.ack_captain(1, 0, d.locked_captain));
    CHECK(locked.disconnect(1, 0) && !locked.begin_loading(1));
}

static void test_damage_sources()
{
    Difficulty d;
    const Source captain0{SourceKind::Captain, 0};
    const Source captain1{SourceKind::Captain, 1};
    const Source ally{SourceKind::OtherActor, kNoPlayer};
    for (uint8_t percent : {0, 25, 50, 100}) {
        d.friendly_fire_percent = percent;
        CHECK(resolve_damage(d, 2, captain0, 1, 40) == 40.f * percent / 100.f);
        CHECK(resolve_damage(d, 2, captain1, 0, 40) == 40.f * percent / 100.f);
        CHECK(resolve_damage(d, 2, captain0, 0, 40) == 40); // authored self damage
        CHECK(resolve_damage(d, 2, captain0, kNoPlayer, 40) == 40); // same blast still hits NPCs
        CHECK(resolve_damage(d, 2, ally, 0, 40) == 40); // authored allied NPC behavior
    }
    d.enemy_damage_override = 2;
    CHECK(resolve_damage(d, 4, enemy(), 0, 25) == 50);
    CHECK(resolve_damage(d, 4, enemy(), kNoPlayer, 25) == 50);
    CHECK(resolve_damage(d, 4, {}, 0, 25) == 25); // environmental damage unaffected
    CHECK(resolve_damage(d, 4, ally, 0, 25) == 25);
    CHECK(resolve_damage(d, 4, captain0, kNoPlayer, 25) == 25);
    CHECK(resolve_damage(d, 4, {SourceKind::Captain, kNoPlayer}, 0, 25) == 0);
    CHECK(resolve_damage(d, 4, {static_cast<SourceKind>(8), 0}, 0, 25) == 0);
    CHECK(resolve_damage(d, 4, enemy(), 0, -10) == 0);
    CHECK(resolve_damage(d, 4, enemy(), 0, std::numeric_limits<float>::infinity()) == 0);
    CHECK(resolve_damage(d, 4, enemy(), 0, std::numeric_limits<float>::max()) == 0);
}

static void test_manual_revive()
{
    auto a = authority();
    CHECK(a.damage(1, 1, 1, enemy(), 100) == 100);
    CHECK(a.phase() == RunPhase::Playing && a.captain(1)->life == Life::Downed);
    CHECK(!a.position(1, 1, {9, 9, 9}));
    CHECK(!a.revive(1, 1, 1, true));
    CHECK(a.revive(1, 0, 1, true));
    CHECK(a.tick(1, 2.99) && a.captain(1)->life == Life::Downed);
    CHECK(a.revive(1, 0, 1, true)); // repeated held request preserves progress
    CHECK(a.tick(1, 3) && a.captain(1)->life == Life::Alive);
    CHECK(a.captain(1)->health == 50 && a.captain(1)->position.x == 1);
    CHECK(a.damage(1, 2, 1, enemy(), 1000) == 0); // two-second grace
    CHECK(a.tick(1, 4.99));
    CHECK(a.damage(1, 3, 1, enemy(), 1000) == 0);
    CHECK(a.tick(1, 5));
    CHECK(a.damage(1, 4, 1, enemy(), 1000) == 50);
    CHECK(a.captain(1)->life == Life::Downed);
    CHECK(a.tick(1, 100) && a.captain(1)->life == Life::Downed); // manual mode has no auto timer
    CHECK(!a.tick(1, 99));
    CHECK(!a.tick(1, std::numeric_limits<double>::quiet_NaN()));
    CHECK(!a.tick(2, 101));
}

static void test_revive_interruptions()
{
    auto a = authority(DeathMode::ManualRevive, 4);
    CHECK(a.damage(1, 1, 1, enemy(), 100) == 100);
    CHECK(a.damage(1, 2, 2, enemy(), 100) == 100);
    CHECK(a.position(1, 0, {2, 0, 0}));
    CHECK(a.revive(1, 0, 1, true));
    CHECK(!a.revive(1, 0, 2, true)); // one rescuer cannot work on two targets
    CHECK(a.position(1, 3, {2, 0, 0}));
    CHECK(!a.revive(1, 3, 1, true)); // additional rescuers cannot shorten the timer
    CHECK(a.tick(1, 2));
    CHECK(a.damage(1, 3, 0, enemy(), 1) == 1);
    CHECK(a.captain(1)->reviver == kNoPlayer);
    CHECK(a.revive(1, 0, 1, true));
    CHECK(a.tick(1, 4.99) && a.captain(1)->life == Life::Downed);
    CHECK(a.position(1, 0, {3.01f, 0, 0})); // more than 2 m away
    CHECK(a.captain(1)->reviver == kNoPlayer);
    CHECK(!a.revive(1, 0, 1, true));
    CHECK(a.position(1, 0, {3, 0, 0})); // exactly 2 m is accepted
    CHECK(a.revive(1, 0, 1, true));
    CHECK(a.revive(1, 0, 1, false));
    CHECK(a.tick(1, 10) && a.captain(1)->life == Life::Downed);
    CHECK(a.position(1, 0, {20, 0, 0}));
    CHECK(a.revive(1, 3, 1, true));
    CHECK(a.disconnect(1, 3) && a.captain(1)->reviver == kNoPlayer);
    CHECK(!a.position(1, 3, {1, 0, 0}) && !a.revive(1, 3, 1, true));
    CHECK(a.tick(1, 20) && a.captain(1)->life == Life::Downed);
}

static void test_automatic_and_wipe()
{
    auto a = authority(DeathMode::AutomaticRevive);
    CHECK(a.damage(1, 1, 1, enemy(), 100) == 100);
    CHECK(a.tick(1, 29.99) && a.captain(1)->life == Life::Downed);
    CHECK(a.tick(1, 30) && a.captain(1)->life == Life::Alive);
    CHECK(a.captain(1)->health == 50 && a.captain(1)->position.x == 1);
    CHECK(a.captain(1)->protected_until == 32);

    auto manual = authority(DeathMode::AutomaticRevive);
    CHECK(manual.damage(1, 1, 1, enemy(), 100) == 100);
    CHECK(manual.revive(1, 0, 1, true));
    CHECK(manual.tick(1, 3) && manual.captain(1)->life == Life::Alive);

    for (DeathMode mode : {DeathMode::ManualRevive, DeathMode::AutomaticRevive}) {
        auto wipe = authority(mode);
        CHECK(wipe.damage(1, 1, 1, enemy(), 100) == 100);
        CHECK(wipe.phase() == RunPhase::Playing);
        CHECK(wipe.damage(1, 2, 0, enemy(), 100) == 100);
        CHECK(wipe.phase() == RunPhase::Failed);
        CHECK(!wipe.tick(1, 100) && wipe.captain(1)->life == Life::Downed);
        CHECK(!wipe.revive(1, 0, 1, true));
    }
    auto solo = authority(DeathMode::AutomaticRevive, 1);
    CHECK(solo.damage(1, 1, 0, enemy(), 100) == 100 && solo.phase() == RunPhase::Failed);

    auto last_disconnect = authority(DeathMode::AutomaticRevive, 3);
    CHECK(last_disconnect.damage(1, 1, 0, enemy(), 100) == 100);
    CHECK(last_disconnect.damage(1, 2, 1, enemy(), 100) == 100);
    CHECK(last_disconnect.disconnect(1, 2) && last_disconnect.phase() == RunPhase::Failed);
    auto host_disconnect = authority();
    CHECK(host_disconnect.disconnect(1, 0) && host_disconnect.phase() == RunPhase::Aborted);

    auto strict = authority(DeathMode::AnyDeathFails);
    CHECK(strict.damage(1, 1, 1, enemy(), 100) == 100 && strict.phase() == RunPhase::Failed);
    CHECK(strict.captain(0)->life == Life::Alive && !strict.finish(1, true));
}

static void test_authority_replays_and_health()
{
    auto a = authority();
    CHECK(a.damage(9, 1, 1, enemy(), 10) == 0);
    CHECK(a.damage(1, 1, 1, enemy(), 10) == 10);
    CHECK(a.damage(1, 1, 1, enemy(), 10) == 0);
    CHECK(a.damage(1, 1, 0, enemy(), 10) == 10); // one area event may hit multiple victims
    CHECK(a.damage(1, 0, 1, enemy(), 10) == 0);
    CHECK(a.damage(1, 2, 9, enemy(), 10) == 0);
    CHECK(a.damage(1, 2, 1, {SourceKind::Captain, 9}, 10) == 0);
    CHECK(a.damage(1, 2, 1, {SourceKind::Captain, 0}, 10) == 0); // FF off
    CHECK(a.heal(1, 3, 1, 1000) == 10 && a.captain(1)->health == 100);
    CHECK(a.heal(1, 3, 1, 10) == 0);
    CHECK(a.damage(1, 4, 1, enemy(), 100) == 100);
    CHECK(a.heal(1, 5, 1, 100) == 0); // healing is not a revival
    CHECK(a.damage(1, 6, 1, enemy(), 100) == 0);
    CHECK(!a.position(1, 0, {std::numeric_limits<float>::infinity(), 0, 0}));
    CHECK(!a.claim_reward(1, 0));
    CHECK(a.finish(1, true) && !a.finish(1, true));
    CHECK(a.claim_reward(1, 0) && !a.claim_reward(1, 0));
    CHECK(!a.claim_reward(2, 0) && !a.claim_reward(1, 9));
    auto d = descriptor(2);
    CHECK(a.start(d, seeds(d), 100));
    CHECK(a.damage(1, 1, 1, enemy(), 100) == 0);
    CHECK(a.damage(2, 1, 1, enemy(), 10) == 10); // previous run's event IDs can be reused
    CHECK(a.finish(2, false) && a.claim_reward(2, 0));
    CHECK(!a.start(descriptor(1), seeds(descriptor(1)), 100));

    auto ff = descriptor();
    ff.difficulty.friendly_fire_percent = 25;
    ff.difficulty.enemy_damage_override = 2;
    Authority friendly;
    CHECK(friendly.start(ff, seeds(ff), 0));
    CHECK(friendly.damage(1, 1, 1, {SourceKind::Captain, 0}, 40) == 10);
    CHECK(friendly.damage(1, 2, 1, enemy(), 20) == 40);
    CHECK(friendly.captain(1)->health == 50);
    CHECK(friendly.damage(1, 3, 1, {SourceKind::Captain, 1}, 10) == 10);
}

static void test_objectives_and_rewards()
{
    auto a = authority();
    CHECK(!a.define_objective(1, 1, 7, 3));
    CHECK(!a.define_objective(1, 0, 7, 0));
    CHECK(a.define_objective(1, 0, 7, 3));
    CHECK(!a.define_objective(1, 0, 7, 3));
    CHECK(!a.advance_objective(2, 0, 7, 1));
    CHECK(!a.advance_objective(1, 0, 8, 1));
    CHECK(!a.advance_objective(1, 0, 7, 0));
    CHECK(a.advance_objective(1, 0, 7, 1));
    CHECK(!a.advance_objective(1, 0, 7, 1));
    CHECK(a.objective_count(0, 7) == 1);
    CHECK(a.advance_objective(1, 0, 7, 2, std::numeric_limits<uint32_t>::max()));
    CHECK(a.objective_count(0, 7) == 3);
    CHECK(!a.advance_objective(1, 0, 7, 3));
    CHECK(!a.next_act(1, 0) && !a.next_act(1, 2));
    CHECK(a.next_act(1, 1) && a.act() == 1);
    CHECK(!a.advance_objective(1, 0, 7, 4));
    CHECK(a.define_objective(1, 1, 7, 1));
    CHECK(a.advance_objective(1, 1, 7, 1)); // distinct act has its own event ledger
    CHECK(a.objective_count(0, 7) == 3 && a.objective_count(1, 7) == 1);
    CHECK(a.disconnect(1, 1));
    CHECK(a.finish(1, true));
    CHECK(a.claim_reward(1, 0) && !a.claim_reward(1, 1));
}

static void test_result_reward_eligibility()
{
    auto d = descriptor(1, 3);
    Authority win;
    CHECK(win.start(d, seeds(d), 0));
    CHECK(win.damage(1, 1, 1, enemy(), 100) == 100);
    CHECK(win.disconnect(1, 2));
    CHECK(win.finish(1, true));
    CHECK(!win.finish(1, false) && !win.abort(1));
    CHECK(win.claim_reward(1, 0) && !win.claim_reward(1, 0));
    CHECK(win.claim_reward(1, 1) && !win.claim_reward(1, 1)); // downed teammates share the result
    CHECK(!win.claim_reward(1, 2) && !win.claim_reward(1, ga::kNoPlayer));

    auto failed = authority();
    CHECK(failed.damage(1, 1, 0, enemy(), 100) == 100);
    CHECK(failed.damage(1, 2, 1, enemy(), 100) == 100);
    CHECK(failed.phase() == RunPhase::Failed);
    CHECK(failed.claim_reward(1, 0) && failed.claim_reward(1, 1));
    CHECK(!failed.claim_reward(1, 0) && !failed.claim_reward(1, 1));

    auto cancelled = authority();
    CHECK(cancelled.abort(1));
    CHECK(!cancelled.claim_reward(1, 0) && !cancelled.claim_reward(1, 1));
}

static void test_friendly_fire_revive_and_persistent_sources()
{
    for (uint8_t percent : {0, 25}) {
        auto d = descriptor(1, 3);
        d.difficulty.friendly_fire_percent = percent;
        Authority a;
        CHECK(a.start(d, seeds(d), 0));
        CHECK(a.damage(1, 1, 1, enemy(), 100) == 100);
        CHECK(a.revive(1, 0, 1, true));
        CHECK(a.tick(1, 1));
        CHECK(a.damage(1, 2, 0, {SourceKind::Captain, 2}, 4) == float(percent) / 25.f);
        CHECK((a.captain(1)->reviver == kNoPlayer) == (percent != 0));
        // The original owner remains valid for an already spawned effect after disconnect.
        CHECK(a.disconnect(1, 2));
        CHECK(a.damage(1, 3, 0, {SourceKind::Captain, 2}, 4) == float(percent) / 25.f);
        if (percent == 0) {
            CHECK(a.tick(1, 3) && a.captain(1)->life == Life::Alive);
        } else {
            CHECK(a.tick(1, 3) && a.captain(1)->life == Life::Downed);
        }
    }
}

static void test_proximity_alone_starts_revive()
{
    auto a = authority();
    CHECK(a.damage(1, 1, 1, enemy(), 100) == 100);
    CHECK(a.tick(1, 0));
    CHECK(a.captain(1)->reviver == 0);
    CHECK(a.tick(1, 2.99) && a.captain(1)->life == Life::Downed);
    CHECK(a.tick(1, 3) && a.captain(1)->life == Life::Alive);

    auto interrupted = authority();
    CHECK(interrupted.damage(1, 1, 1, enemy(), 100) == 100);
    CHECK(interrupted.tick(1, 0));
    CHECK(interrupted.tick(1, 2));
    CHECK(interrupted.damage(1, 2, 0, enemy(), 1) == 1);
    CHECK(interrupted.tick(1, 2));
    CHECK(interrupted.tick(1, 4.99) && interrupted.captain(1)->life == Life::Downed);
    CHECK(interrupted.tick(1, 5) && interrupted.captain(1)->life == Life::Alive);
}

static void test_invalid_start_is_transactional()
{
    Authority a;
    auto d = descriptor();
    auto bad = seeds(d);
    bad[1].player = 0;
    CHECK(!a.start(d, bad, 0) && a.phase() == RunPhase::Idle);
    bad = seeds(d); bad[0].max_health = std::numeric_limits<float>::quiet_NaN();
    CHECK(!a.start(d, bad, 0));
    bad = seeds(d); bad.pop_back();
    CHECK(!a.start(d, bad, 0));
    CHECK(!a.start(d, seeds(d), -1));
    CHECK(a.start(d, seeds(d), 0));
    CHECK(!a.start(descriptor(2), seeds(descriptor(2)), 0));
    CHECK(a.descriptor().run_id == 1 && a.captain(0)->health == 100);
}

static void test_preparation_wire()
{
    PreparationView v;
    v.run=5; v.revision=7; v.epoch=2; v.adventure={123,0x366A930D,456};
    for(uint8_t i=0;i<4;++i) {
        PreparationMember m; m.player=i; m.serial=i+1; m.stage=PreparationStage::Prepared;
        m.captain={uint32_t(i)+1,10,20}; m.content=m.validated=m.ready=true; m.progress=100;
        v.members.push_back(m);
    }
    auto decode=[](const PreparationView& x) {
        net::Writer w; x.write(w); PreparationView out; net::Reader r(w.data.data(),w.size());
        return out.read(r) && r.left()==0;
    };
    CHECK(decode(v));
    auto bad=v; bad.members[1].player=0; CHECK(!decode(bad));
    bad=v; bad.members[1].serial=0; CHECK(!decode(bad));
    bad=v; bad.members[1].content=false; CHECK(!decode(bad));
    bad=v; bad.members[1].validated=false; CHECK(!decode(bad));
    bad=v; bad.members[1].progress=101; CHECK(!decode(bad));
    bad=v; bad.epoch=0; CHECK(!decode(bad));
    bad=v; bad.locked_captain={999,10,20}; CHECK(!decode(bad));
    net::Writer w; v.write(w);
    for(size_t n=0;n<w.size();++n) { PreparationView out; net::Reader r(w.data.data(),n); CHECK(!out.read(r)); }
}

int main()
{
    test_preparation_wire();
    test_difficulty();
    test_wire();
    test_descriptor_validation();
    test_launch_barrier();
    test_damage_sources();
    test_manual_revive();
    test_revive_interruptions();
    test_automatic_and_wipe();
    test_authority_replays_and_health();
    test_objectives_and_rewards();
    test_result_reward_eligibility();
    test_friendly_fire_revive_and_persistent_sources();
    test_proximity_alone_starts_revive();
    test_invalid_start_is_transactional();
    std::printf("adventure rules: %s (%d failures)\n", failures ? "FAILED" : "passed", failures);
    return failures ? 1 : 0;
}
