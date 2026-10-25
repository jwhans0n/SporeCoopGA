#include "adventure_pause.h"
#include "net/adventure_motion.h"
#include "net/adventure_pause_motion.h"
#include "net/interp.h"

#include <chrono>
#include <cstdio>
#include <thread>

static int failures;
#define CHECK(c) do { if (!(c)) { std::printf("FAIL %d: %s\n", __LINE__, #c); ++failures; } } while (0)
namespace party_pause = adventure_pause;
static void put64(net::Writer& w, uint64_t n) { w.u32(uint32_t(n)); w.u32(uint32_t(n >> 32)); }
static net::Writer intent(uint64_t run, uint64_t serial, uint8_t paused)
{
    net::Writer w; w.u8(164); put64(w, run); put64(w, serial); w.u8(paused); return w;
}
struct Player { uint8_t id; uint64_t serial; uint8_t paused; };
static net::Writer snapshot(uint64_t run, uint64_t revision, std::initializer_list<Player> players)
{
    net::Writer w; w.u8(165); put64(w, run); put64(w, revision); w.u8(uint8_t(players.size()));
    for (auto p : players) { w.u8(p.id); put64(w, p.serial); w.u8(p.paused); }
    return w;
}
static void incoming(uint8_t from, const net::Writer& w)
{
    net::Reader r(w.data.data(), w.size()); party_pause::on_message(from, r);
}
static bool connect(net::Session& h, net::Session& g)
{
    std::string error;
    if (!g.join("127.0.0.1:" + std::to_string(h.port()), "Guest", error)) return false;
    for (int i = 0; i < 1000; ++i) {
        g.update(); h.update(); g.update();
        if (g.connected()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}
int main()
{
    {
        net::AdventurePauseMotion motion; net::AdventureMotion teleports;
        net::PlayerState seed; seed.id = 1; seed.mode = 0x01654C10; seed.adventure_run = 8;
        seed.time_ms = 1000; seed.pos = {10, 20, 30}; seed.vel = {20, 0, 0}; seed.health = 100;
        seed.flags = net::PF_IN_WORLD | net::PF_MOVING; seed.movement_generation = 9;
        CHECK(teleports.correct(8, 1, 9, seed.pos, seed.rot));
        CHECK(motion.transition(8, true, &seed));
        auto late = seed; late.time_ms = 1100; late.pos.x = 900; late.health = 50;
        CHECK(motion.admit(late) && late.pos.x == 10 && late.vel.x == 0 && late.health == 50 && !(late.flags & net::PF_MOVING));
        auto reordered = seed; reordered.time_ms = 1050; CHECK(!motion.admit(reordered));
        CHECK(motion.transition(8, false, &late));
        CHECK(!motion.admit(reordered)); // clearing interpolation cannot admit an old delayed movement packet
        auto resumed = seed; resumed.time_ms = 1200; resumed.pos.x = 11; resumed.vel = {};
        CHECK(motion.admit(resumed) && resumed.pos.x == 11 && teleports.accepts(resumed));
        CHECK(teleports.generation(1) == 9); // pause does not erase native teleport authority
        net::SnapshotBuffer buffer; buffer.push(*motion.anchor(), 3); buffer.push(resumed, 3.1);
        net::PlayerState sampled; CHECK(buffer.sample(100, sampled) && sampled.pos.x == 11);
        CHECK(motion.transition(8, true, &resumed)); motion.reset();
        CHECK(!motion.paused() && !motion.anchor());
    }
    net::Session host, g1, g2, g3; host.build = g1.build = g2.build = g3.build = 1;
    std::string error; CHECK(host.host(0, "Host", error));
    CHECK(connect(host, g1)); CHECK(connect(host, g2)); CHECK(connect(host, g3));
    for (int i = 0; i < 8; ++i) { host.update(); g1.update(); g2.update(); g3.update(); }
    uint64_t run = 42; bool running = true, local = false; unsigned changes = 0, intent_reads = 0;
    party_pause::Ctx c; c.sess = &host; c.run = [&] { return run; }; c.running = [&] { return running; };
    c.members = [] { return std::vector<uint8_t>{0, 1, 2, 3}; };
    c.local_intent = [&] { ++intent_reads; return local; }; c.changed = [&] { ++changes; };
    CHECK(party_pause::init(c));
    incoming(1, intent(run - 1, 1, 1)); incoming(0, intent(run, 1, 1)); incoming(4, intent(run, 1, 1));
    incoming(1, intent(run, 1, 2)); incoming(1, snapshot(run, 1, {{0, 1, 1}, {1, 1, 1}}));
    CHECK(!party_pause::active() && intent_reads == 0 && changes == 0);
    incoming(1, intent(run, 1, 1)); // ACKed from keepalive before the first native tick
    CHECK(party_pause::active() && intent_reads == 1 && !party_pause::held());
    party_pause::tick(); CHECK(party_pause::active() && !party_pause::held());
    local = true; party_pause::local_intent(local); CHECK(!party_pause::held());
    incoming(1, intent(run, 1, 1)); incoming(2, intent(run, 1, 1)); CHECK(!party_pause::held());
    unsigned before = changes; incoming(3, intent(run, 1, 1)); CHECK(party_pause::held() && changes == before + 1);
    // One Resume must immediately end the shared hold, even with all other menus open.
    incoming(2, intent(run, 2, 0)); CHECK(!party_pause::held());
    incoming(2, intent(run, 1, 1)); CHECK(!party_pause::held()); // reordered old pause
    incoming(2, intent(run, 2, 1)); CHECK(!party_pause::held()); // conflicting duplicate
    incoming(2, intent(run - 1, 100, 1)); incoming(2, intent(run, 3, 2)); CHECK(!party_pause::held());
    incoming(0, intent(run, 99, 0)); incoming(4, intent(run, 99, 1)); CHECK(!party_pause::held());
    auto trailing = intent(run, 3, 1); trailing.u8(0); incoming(2, trailing); CHECK(!party_pause::held());
    incoming(2, intent(run, 3, 1)); CHECK(party_pause::held());
    local = false; party_pause::local_intent(local); CHECK(!party_pause::held());
    local = true; party_pause::local_intent(local); CHECK(party_pause::held());
    // Leaving participants no longer contribute, and the changed callback still fires.
    incoming(3, intent(run, 2, 0)); CHECK(!party_pause::held()); before = changes;
    g3.leave("test"); for (int i = 0; i < 8; ++i) { host.update(); g1.update(); g2.update(); }
    party_pause::tick(); CHECK(party_pause::held() && changes == before + 1);
    ++run; party_pause::tick(); CHECK(!party_pause::held()); // no previous-run readiness leaks
    incoming(1, intent(run - 1, 50, 1)); incoming(2, intent(run - 1, 50, 1)); CHECK(!party_pause::held());
    running = false; party_pause::tick(); CHECK(!party_pause::active() && !party_pause::held());
    // A guest accepts only complete host snapshots. The host must acknowledge
    // the current local menu visit, never an earlier pause/resume serial.
    running = true; run = 100; local = true; c.sess = &g1;
    c.members = [] { return std::vector<uint8_t>{0, 1, 2}; };
    CHECK(party_pause::init(c));
    incoming(0, snapshot(run - 1, 1, {{0, 1, 1}, {1, 1, 1}, {2, 1, 1}}));
    incoming(2, snapshot(run, 1, {{0, 1, 1}, {1, 1, 1}, {2, 1, 1}}));
    CHECK(!party_pause::active());
    incoming(0, snapshot(run, 1, {{0, 1, 1}, {1, 1, 1}, {2, 1, 1}}));
    CHECK(party_pause::active() && party_pause::held()); // host snapshot also initializes before tick
    party_pause::reset();
    CHECK(party_pause::init(c)); party_pause::tick(); CHECK(!party_pause::held());
    auto all = snapshot(run, 1, {{0, 3, 1}, {1, 1, 1}, {2, 8, 1}});
    incoming(2, all); CHECK(!party_pause::held()); incoming(0, all); CHECK(party_pause::held());
    local = false; party_pause::local_intent(local); CHECK(!party_pause::held());
    local = true; party_pause::local_intent(local); CHECK(!party_pause::held());
    incoming(0, snapshot(run, 2, {{0, 3, 1}, {1, 1, 1}, {2, 8, 1}})); CHECK(!party_pause::held());
    incoming(0, snapshot(run, 3, {{0, 3, 1}, {1, 4, 1}, {2, 8, 1}})); CHECK(!party_pause::held()); // future local serial
    incoming(0, snapshot(run, 4, {{0, 3, 1}, {1, 3, 0}, {2, 8, 1}})); CHECK(!party_pause::held()); // false acknowledgement
    incoming(0, snapshot(run, 5, {{0, 3, 1}, {1, 3, 1}, {1, 8, 1}})); CHECK(!party_pause::held());
    incoming(0, snapshot(run, 6, {{1, 3, 1}, {2, 8, 1}})); CHECK(!party_pause::held()); // missing host
    incoming(0, snapshot(run, 7, {{0, 3, 1}, {1, 3, 1}, {2, 8, 1}})); CHECK(party_pause::held());
    incoming(0, snapshot(run, 6, {{0, 4, 0}, {1, 3, 1}, {2, 8, 1}})); CHECK(party_pause::held());
    CHECK(g1.players().size() == 3); // the recipient still sees player 2 connected
    incoming(0, snapshot(run, 8, {{0, 4, 0}, {1, 3, 1}})); CHECK(!party_pause::held());
    // A host Resume plus participant departure reaches the client before its
    // Session roster changes. It must apply, rather than leave the old hold forever.
    g2.leave("reordered departure"); for (int i = 0; i < 8; ++i) { host.update(); g1.update(); }
    CHECK(g1.players().size() == 2); party_pause::tick(); CHECK(!party_pause::held());
    incoming(0, snapshot(run, 9, {{0, 5, 1}, {1, 3, 1}})); CHECK(party_pause::held());
    incoming(0, snapshot(run, 10, {{0, 5, 1}, {2, 9, 1}})); CHECK(party_pause::held()); // missing self is rejected
    running = false; party_pause::reset(); CHECK(!party_pause::active());
    incoming(0, all); CHECK(!party_pause::held()); // cancellation has retired Running
    ++run; running = true; party_pause::tick(); incoming(0, all); CHECK(!party_pause::held());
    std::printf("adventure pause: %d failures\n", failures);
    return failures ? 1 : 0;
}
