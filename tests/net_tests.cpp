// Native (Linux) tests for the portable coop netcode: make test
#include "../updater/pack.h"
#include <algorithm>
#include <cassert>
#include <cstdlib>
#include <fstream>
#include <iterator>
#include "net/dbpf.h"
#include <cmath>
#include <cstdio>
#include <random>
#include <chrono>
#include <thread>

#include "net/connection.h"
#include "net/discovery.h"
#include "net/interp.h"
#include "net/adventure_motion.h"
#include "net/session.h"

using namespace net;

static int g_fail = 0;
#define CHECK(c)                                                         \
    do {                                                                 \
        if (!(c)) {                                                      \
            printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c);        \
            g_fail++;                                                    \
        }                                                                \
    } while (0)

static void test_buffer()
{
    printf("buffer roundtrip\n");
    PlayerState s;
    s.id = 3; s.time_ms = 123456; s.pos = {1, 2, 3}; s.rot = {0, 0, 0.7071f, 0.7071f};
    s.vel = {4, 5, 6}; s.flags = PF_IN_WORLD | PF_RUNNING; s.anim = 7; s.health = 50; s.max_health = 100;
    s.species = 0xDEADBEEF;
    s.scale=1.73f; s.adventure_run=0x1122334455667788ULL; s.captain_selection=0x8877665544332211ULL;
    s.movement_generation=0xFEDCBA9876543210ULL;
    Writer w;
    s.write(w);
    CHECK(w.size() == 95); // protocol 12 retains the GA identity and teleport fields
    Reader r(w.data.data(), w.size());
    PlayerState t;
    CHECK(t.read(r));
    CHECK(t.id == 3 && t.time_ms == 123456 && t.pos.z == 3 && t.rot.w > 0.7f && t.flags == s.flags);
    CHECK(t.species == 0xDEADBEEF && t.max_health == 100);
    CHECK(t.scale==s.scale && t.adventure_run==s.adventure_run && t.captain_selection==s.captain_selection);
    CHECK(t.movement_generation==s.movement_generation);
    CHECK(r.left() == 0);
    // In particular, the upstream protocol 7 PlayerState prefix (67 bytes) cannot
    // silently supply default GA identity fields after merging the two branches.
    for (size_t bytes = 0; bytes < w.size(); ++bytes) {
        Reader truncated(w.data.data(), bytes);
        PlayerState incomplete;
        CHECK(!incomplete.read(truncated));
    }
    Reader bad(w.data.data(), 5);
    PlayerState u;
    CHECK(!u.read(bad));
    // v2: mode + world identify where the player is
    s.mode = 0x1654C05; s.world = 0x80B16021;
    Writer w2;
    s.write(w2);
    Reader r2(w2.data.data(), w2.size());
    PlayerState t2;
    CHECK(t2.read(r2) && t2.mode == 0x1654C05 && t2.world == 0x80B16021);
    // shared resource message payload (u8 id, f64)
    Writer w3;
    w3.u8(3);
    w3.f64(123456.75);
    Reader r3(w3.data.data(), w3.size());
    CHECK(r3.u8() == 3 && r3.f64() == 123456.75 && r3.ok() && r3.left() == 0);
}

static void test_adventure_motion()
{
    printf("adventure teleport ordering and movement history\n");
    AdventureMotion fence;
    fence.begin(12);
    PlayerState player;
    player.id=2; player.mode=0x01654C10; player.adventure_run=12;
    CHECK(fence.accepts(player));
    const Vec3 exit{200,300,400}; const Quat rotation{0,0,0,1};
    CHECK(fence.correct(12,2,8,exit,rotation));
    CHECK(!fence.accepts(player)); // old UDP position after the reliable teleport
    player.movement_generation=8;
    CHECK(fence.accepts(player));
    CHECK(!fence.correct(12,2,8,exit,rotation)); // duplicate must not reset movement again
    CHECK(!fence.correct(12,2,7,exit,rotation));
    CHECK(!fence.correct(11,2,20,exit,rotation)); // cancelled run callback
    CHECK(!fence.correct(12,2,9,exit,{0,0,0,0}));
    CHECK(!fence.correct(12,255,9,exit,rotation));
    player.movement_generation=9;
    CHECK(!fence.accepts(player)); // future pose waits for its host correction
    player.id=1; player.movement_generation=0;
    CHECK(fence.accepts(player)); // independent captain teleports
    fence.begin(13); player.adventure_run=12;
    CHECK(!fence.accepts(player));
    player.adventure_run=13;
    CHECK(fence.accepts(player));
    fence.reset();
    CHECK(fence.generation(2)==0);
}

// a snapshot in several big unreliable chunks: every chunk goes out (one per packet), none is lost
static void test_unreliable_chunks()
{
    printf("unreliable chunks larger than one packet\n");
    Connection a, b;
    for (int k = 0; k < 5; k++) {
        Writer w;
        w.u8((uint8_t)k);
        for (int i = 0; i < 1000; i++)
            w.u8(0x55);
        a.send_unreliable(9, w);
    }
    std::vector<uint8_t> pkt;
    std::vector<int> got;
    int packets = 0;
    while (a.build_packet(2000, pkt)) {
        packets++;
        b.on_packet(pkt.data(), pkt.size(), 2000, [&](uint8_t type, Reader& r) {
            if (type == 9) got.push_back(r.u8());
        });
    }
    CHECK(packets == 5);
    CHECK(got.size() == 5 && got[0] == 0 && got[4] == 4);
    CHECK(a.unreliable_queued() == 0);
}

static void test_dbpf()
{
    printf("dbpf write/read, EditorSaves of the test profile\n");
    std::vector<dbpf::Record> recs = {{{0x7C000001, 0x1A99B06B, 0x40626200}, {1, 2, 3, 4}},
                                      {{0x7C000001, 0x030BDEE3, 0x40626200}, std::vector<uint8_t>(300, 9)}};
    dbpf::Package pk;
    CHECK(pk.load(dbpf::write(recs)));
    CHECK(pk.entries().size() == 2);
    std::vector<uint8_t> out;
    const dbpf::Entry* e = pk.find(recs[1].key);
    CHECK(e && pk.read(*e, out) && out == recs[1].data);
    // the real thing (RefPack records, pollen metadata names), when the test profile exists
    const char* home = getenv("HOME");
    const char* user = getenv("USER");
    if (!home || !user)
        return;
    std::string path = std::string(home) + "/.var/app/com.usebottles.bottles/data/bottles/bottles/Games/drive_c/users/" +
                       user + "/AppData/Roaming/SporeKit-test/Spore/EditorSaves.package";
    std::ifstream f(path, std::ios::binary);
    if (!f) {
        printf("  (no test profile EditorSaves.package)\n");
        return;
    }
    std::vector<uint8_t> file((std::istreambuf_iterator<char>(f)), std::istreambuf_iterator<char>());
    dbpf::Package es;
    CHECK(es.load(file));
    int bems = 0, names = 0;
    for (auto& en : es.entries()) {
        std::vector<uint8_t> d;
        if (en.key.type == 0x1A99B06B && en.key.group == 0x40626200) {
            CHECK(es.read(en, d) && d.size() > 1000 && d[0] == 0xD3 && d[1] == 0x7E);
            bems++;
        }
        std::string a, n;
        if (en.key.type == 0x030BDEE3 && es.read(en, d) && dbpf::metadata_names(d, a, n) && !n.empty())
            names++;
    }
    printf("  %d creatures, %d names\n", bems, names);
    CHECK(bems > 0 && names >= bems);
}

static void test_reliability()
{
    printf("reliable channel under 30%% loss + reordering\n");
    Connection a, b;
    std::mt19937 rng(42);
    std::uniform_real_distribution<float> U(0, 1);
    std::vector<std::vector<uint8_t>> a2b, b2a;
    std::vector<uint32_t> got;
    const int N = 400;
    uint32_t now = 1000;
    int sent = 0;
    for (int step = 0; step < 4000 && ((int)got.size() < N || a.pending_reliable()); step++) {
        now += 10;
        if (sent < N && step % 2 == 0) {
            Writer w;
            w.u32(sent++);
            a.send_reliable(42, w);
        }
        if (step % 3 == 0) {
            Writer w;
            w.u32(0xAAAA);
            a.send_unreliable(7, w);
        }
        std::vector<uint8_t> pkt;
        while (a.build_packet(now, pkt))
            if (U(rng) > 0.3f) a2b.push_back(pkt);
        while (b.build_packet(now, pkt))
            if (U(rng) > 0.3f) b2a.push_back(pkt);
        // deliver with random reordering
        std::shuffle(a2b.begin(), a2b.end(), rng);
        for (auto& p : a2b)
            b.on_packet(p.data(), p.size(), now, [&](uint8_t type, Reader& r) {
                if (type == 42) got.push_back(r.u32());
            });
        a2b.clear();
        for (auto& p : b2a)
            a.on_packet(p.data(), p.size(), now, [](uint8_t, Reader&) {});
        b2a.clear();
    }
    CHECK((int)got.size() == N);
    bool ordered = true;
    for (size_t i = 0; i < got.size(); i++)
        if (got[i] != i) ordered = false;
    CHECK(ordered);
    CHECK(a.pending_reliable() == 0);
    printf("  delivered %zu/%d, resends=%u rtt=%.0f\n", got.size(), N, a.stats.resends, a.rtt_ms);
}

static void pump(Session& h, Session& c, int ms)
{
    for (int i = 0; i < ms / 5; i++) {
        h.update();
        c.update();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}

static void test_session()
{
    printf("session host/join/state/chat/leave over localhost\n");
    Session host, cli;
    int joins = 0, states = 0, chats = 0, leaves = 0;
    bool connected = false;
    host.build = cli.build = 0x922af7c7;
    host.cb.player_join = [&](const PlayerInfo& p) { joins++; CHECK(p.name == "guest-bob"); };
    host.cb.player_state = [&](const PlayerState& s) { states++; CHECK(s.id == 1 && s.pos.x == 10); };
    host.cb.chat = [&](uint8_t id, const std::string& t) { if (id == 1) { chats++; CHECK(t == "hi"); } };
    host.cb.player_leave = [&](uint8_t, uint8_t) { leaves++; };
    cli.cb.connected = [&] { connected = true; };
    std::string err;
    CHECK(host.host(0, "host-alice", err));
    uint16_t port = host.port();
    CHECK(cli.join("127.0.0.1:" + std::to_string(port), "guest-bob", err));
    pump(host, cli, 300);
    CHECK(connected);
    CHECK(joins == 1);
    CHECK(cli.my_id() == 1);
    CHECK(cli.name_of(0) == "host-alice");
    PlayerState s;
    s.pos = {10, 0, 0};
    for (int i = 0; i < 5; i++) {
        cli.send_state(s);
        pump(host, cli, 50);
    }
    CHECK(states >= 3);
    int cstates = 0;
    cli.cb.player_state = [&](const PlayerState& st) { if (st.id == 0) cstates++; };
    host.send_state(s);
    pump(host, cli, 100);
    CHECK(cstates == 1);
    cli.send_chat("hi");
    pump(host, cli, 100);
    CHECK(chats == 1);
    cli.leave("test");
    pump(host, cli, 200);
    CHECK(leaves == 1);
    printf("  %s", host.status().c_str());

    // build mismatch must be rejected
    Session bad;
    bad.build = 0x1234;
    std::string why;
    bad.cb.disconnected = [&](const std::string& w) { why = w; };
    CHECK(bad.join("127.0.0.1:" + std::to_string(port), "x", err));
    pump(host, bad, 300);
    CHECK(why.find("build") != std::string::npos);
}

static void test_protocol_versions()
{
    printf("merged protocol boundary and HELLO version validation\n");
    CHECK(kProtoVersion == 12 && kDiscoverPorts == 20);
    CHECK(MSG_SPACE == 48 && MSG_ADVENTURE == 52);
    Session host;
    std::string err;
    CHECK(host.host(0, "host", err));
    UdpSocket socket;
    CHECK(socket.open(0));
    Endpoint destination;
    CHECK(Endpoint::parse("127.0.0.1", host.port(), destination));
    Connection guest;
    guest.conn_id = 0x12345678;
    Hello old;
    old.proto = 7;
    old.name = "old-version";
    Writer hello;
    old.write(hello);
    guest.send_reliable(MSG_HELLO, hello);
    std::vector<uint8_t> packet;
    CHECK(guest.build_packet(1000, packet));
    CHECK(Connection::looks_valid(packet.data(), packet.size()));
    auto upstream = packet;
    upstream[4] = 7;
    upstream[5] = 0;
    CHECK(!Connection::looks_valid(upstream.data(), upstream.size()));
    bool delivered = false;
    Connection receiver;
    CHECK(!receiver.on_packet(upstream.data(), upstream.size(), 1000,
                             [&](uint8_t, Reader&) { delivered = true; }));
    CHECK(!delivered);
    // A current transport carrying an incompatible HELLO receives an explicit
    // rejection. Older transport headers are discarded before HELLO decoding.
    CHECK(socket.send_to(destination, packet.data(), packet.size()));
    Reject rejection;
    for (int i = 0; i < 100 && !rejection.reason; ++i) {
        host.update();
        uint8_t data[2048];
        Endpoint from;
        for (int n; (n = socket.recv_from(from, data, sizeof data)) > 0;)
            guest.on_packet(data, size_t(n), 1000 + i * 5, [&](uint8_t type, Reader& payload) {
                CHECK(type == MSG_REJECT);
                CHECK(rejection.read(payload));
            });
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(rejection.reason == REJ_VERSION);
    CHECK(rejection.text.find("protocol") != std::string::npos);
    CHECK(host.players().size() == 1);
}

// a UDP relay between guests and the host (like tools/tests/netem.py or a NAT that keeps its mapping):
// everything from the guests reaches the host from ONE address and port
struct Relay {
    UdpSocket a, b; // a faces the guests, b faces the host
    Endpoint host, guest;
    bool drop = false;
    std::vector<std::vector<uint8_t>> up; // what went to the host (for replays)
    Relay(uint16_t host_port)
    {
        a.open(0);
        b.open(0);
        Endpoint::parse("127.0.0.1", host_port, host);
    }
    void pump()
    {
        uint8_t buf[2048];
        Endpoint from;
        for (int n; (n = a.recv_from(from, buf, sizeof buf)) > 0;) {
            guest = from;
            up.emplace_back(buf, buf + n);
            if (!drop)
                b.send_to(host, buf, n);
        }
        for (int n; (n = b.recv_from(from, buf, sizeof buf)) > 0;)
            if (!drop && guest.port)
                a.send_to(guest, buf, n);
    }
};

// a guest whose goodbye got lost joins again through the same relay: the host sees the same address
// and port; the new HELLO used to land in the old (accepted) peer and was dropped as a duplicate,
// the old peer never timed out (the new connection kept it alive): the guest never got back in
static void test_rejoin_same_endpoint()
{
    printf("rejoin through a relay (same address for the host), goodbye lost\n");
    Session host;
    int joins = 0, leaves = 0, chats = 0;
    host.cb.player_join = [&](const PlayerInfo&) { joins++; };
    host.cb.player_leave = [&](uint8_t, uint8_t) { leaves++; };
    host.cb.chat = [&](uint8_t id, const std::string& t) { chats += id == 1 && t == "back"; };
    std::string err;
    CHECK(host.host(0, "host", err));
    Relay relay(host.port());
    std::string addr = "127.0.0.1:" + std::to_string(relay.a.local_port());
    auto run = [&](Session& c, int ms) {
        for (int i = 0; i < ms / 5; i++) {
            host.update();
            c.update();
            relay.pump();
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
        }
    };
    Session g1;
    CHECK(g1.join(addr, "bob", err));
    run(g1, 300);
    CHECK(g1.connected() && joins == 1);
    for (int i = 0; i < 5; i++) { // some traffic of the first connection (late copies replayed below)
        g1.send_chat("old");
        run(g1, 20);
    }
    size_t old_pkts = relay.up.size();
    relay.drop = true; // the goodbye is lost
    g1.leave("test");
    run(g1, 50);
    relay.drop = false;
    CHECK(leaves == 0);
    Session g2; // joins again (a new socket, but the relay keeps the host-facing address)
    bool back = false;
    g2.cb.connected = [&] { back = true; };
    CHECK(g2.join(addr, "bob", err));
    run(g2, 600);
    CHECK(back);
    CHECK(leaves == 1 && joins == 2);
    CHECK(host.players().size() == 2);
    // late packets of the first connection (delayed / duplicated by the network) are ignored
    for (size_t i = 0; i < old_pkts; i++)
        relay.b.send_to(relay.host, relay.up[i].data(), relay.up[i].size());
    run(g2, 100);
    g2.send_chat("back");
    run(g2, 200);
    CHECK(chats == 1);
    CHECK(leaves == 1 && host.players().size() == 2);
    printf("  %s", host.status().c_str());
}

// the host's player state and every chunk of a big snapshot (a cell pool of 600 cells = 43
// chunks of ~900 bytes, more than the 32 packets of one flush) reach the guest in the same frame
static void test_big_snapshot()
{
    printf("player state and a 43-chunk snapshot in one frame\n");
    Session host, cli;
    host.build = cli.build = 0x922af7c7;
    std::string err;
    CHECK(host.host(0, "host-alice", err));
    CHECK(cli.join("127.0.0.1:" + std::to_string(host.port()), "guest-bob", err));
    pump(host, cli, 300);
    CHECK(cli.my_id() == 1);
    int chunks = 0, states = 0;
    cli.cb.player_state = [&](const PlayerState& st) { if (st.id == 0) states++; };
    cli.cb.custom = [&](uint8_t type, uint8_t, Reader&) { if (type == 33) chunks++; };
    for (int round = 0; round < 3; round++) {
        for (int k = 0; k < 43; k++) {
            Writer w;
            for (int i = 0; i < 887; i++)
                w.u8((uint8_t)k);
            host.send_unreliable_to(1, 33, w);
        }
        PlayerState s;
        host.send_state(s);
        host.update(); // one frame: one flush
        pump(host, cli, 40);
    }
    CHECK(states == 3);
    CHECK(chunks == 3 * 43);
}

static void test_interp()
{
    printf("snapshot interpolation\n");
    SnapshotBuffer b;
    b.delay = 0.1;
    for (int i = 0; i <= 10; i++) {
        PlayerState s;
        s.time_ms = 1000 + i * 50;
        s.pos = {(float)i, 0, 0};
        s.vel = {20, 0, 0};
        b.push(s, 5.0 + i * 0.05); // local clock offset 4s
    }
    PlayerState o;
    CHECK(b.sample(5.0 + 0.5, o)); // newest local recv time -> render 0.1 s behind -> x=8
    CHECK(std::fabs(o.pos.x - 8.0f) < 0.01f);
    CHECK(b.sample(5.0 + 0.6, o)); // extrapolate 0 s past newest: x=10
    CHECK(std::fabs(o.pos.x - 10.0f) < 0.01f);
    CHECK(b.sample(5.0 + 2.0, o)); // capped at 0.25 s -> x = 10 + 5
    CHECK(std::fabs(o.pos.x - 15.0f) < 0.01f);
}

static void test_discovery()
{
    Session host;
    std::string err;
    CHECK(host.host(kDefaultPort + kDiscoverPorts - 1, "LanHost", err));
    host.lobby_info = "Creature - Testworld";
    Discovery d;
    CHECK(d.start());
    d.targets = {0x0100007F}; // 127.0.0.1
    d.interval_ms = 100;
    std::vector<Discovery::Game> found;
    for (int i = 0; i < 200 && found.empty(); i++) {
        d.update(now_ms());
        host.update();
        d.update(now_ms());
        found = d.games(now_ms());
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    CHECK(found.size() == 1);
    if (!found.empty()) {
        CHECK(found[0].info.name == "LanHost");
        CHECK(found[0].info.info == "Creature - Testworld");
        CHECK(found[0].info.players == 1);
        CHECK(found[0].host.port == kDefaultPort + kDiscoverPorts - 1);
    }
    printf("discovery ok\n");
}

static void test_ackbits()
{
    printf("128-packet ack window\n");
    AckBits b;
    b.set(0);
    b.set(63);
    b.set(64);
    b.set(127);
    CHECK(b.test(0) && b.test(63) && b.test(64) && b.test(127) && !b.test(1) && !b.test(128));
    b.shift(1);
    CHECK(b.test(1) && b.test(64) && b.test(65) && !b.test(0) && !b.test(127));
    b.shift(64);
    CHECK(b.test(65) && !b.test(1));
    b.shift(200);
    CHECK(!b.lo && !b.hi);
}

// a big reliable transfer (the world sent to a guest) in bursts of a whole ack window, with loss
static void test_bulk()
{
    printf("bulk transfer: 4 MB in bursts of %u packets, 5%% loss\n", kAckWindow);
    Connection a, b;
    std::mt19937 rng(7);
    std::uniform_real_distribution<float> U(0, 1);
    const int N = 4000; // 1 KB messages
    uint32_t now = 1000;
    int queued = 0, got = 0, frames = 0;
    bool ordered = true;
    std::vector<uint8_t> pkt;
    a.rtt_ms = b.rtt_ms = 5;
    while (got < N && frames < 3000) {
        frames++;
        now += 33; // one game frame
        while (queued < N && a.pending_reliable() < 600) {
            Writer w;
            w.u32(queued++);
            std::vector<uint8_t> pad(1020, 0x55);
            w.raw(pad.data(), pad.size());
            a.send_reliable(42, w);
        }
        std::vector<std::vector<uint8_t>> a2b, b2a;
        for (unsigned i = 0; i < kAckWindow && a.build_packet(now, pkt); i++)
            if (U(rng) > 0.05f) a2b.push_back(pkt);
        for (auto& p : a2b)
            b.on_packet(p.data(), p.size(), now, [&](uint8_t type, Reader& r) {
                if (type == 42 && r.u32() != (uint32_t)got++) ordered = false;
            });
        while (b.build_packet(now + 1, pkt))
            if (U(rng) > 0.05f) b2a.push_back(pkt);
        for (auto& p : b2a)
            a.on_packet(p.data(), p.size(), now + 2, [](uint8_t, Reader&) {});
    }
    double mbps = got * 1024.0 / (frames * 0.033) / 1e6;
    printf("  %d/%d in %d frames (%.1f s at 30 fps: %.1f MB/s), resends=%u\n", got, N, frames, frames * 0.033, mbps,
           a.stats.resends);
    CHECK(got == N && ordered);
    CHECK(mbps > 2.5); // the old 32-packet window gave ~1.1 MB/s
}

// the world over a very bad link (netem "awful": 250 +- 120 ms each way, 10 % loss, 2 % duplicates)
// a message lost several times in a row let the receiver buffer 1024 later ones; it then dropped new
// messages while still acknowledging their packets, so they were never sent again and the transfer
// stalled for good (a guest rejoin over SPORE_NET=awful never completed)
static void test_bulk_bad_link()
{
    printf("bulk transfer over 250 +- 120 ms, 10%% loss, 2%% duplicates\n");
    struct Pk {
        uint32_t t;
        std::vector<uint8_t> d;
        bool operator<(const Pk& o) const { return t > o.t; }
    };
    for (int run = 0; run < 4; run++) {
        size_t window = run < 2 ? 160 : 600; // world.cpp: 160 on the internet, 600 on a LAN
        Connection a, b;
        std::mt19937 rng(run + 1);
        std::uniform_real_distribution<float> U(0, 1);
        std::vector<Pk> ab, ba; // heaps by arrival time
        // and one message (#100) lost every time for 4 s: the receiver buffers everything after it
        const uint8_t hole[8] = {100, 0, 0, 0, 0x55, 0x55, 0x55, 0x55};
        uint32_t hole_until = 0;
        auto send = [&](std::vector<Pk>& q, const std::vector<uint8_t>& p, uint32_t now) {
            if (&q == &ab && std::search(p.begin(), p.end(), hole, hole + 8) != p.end()) {
                if (!hole_until)
                    hole_until = now + 4000;
                if (now < hole_until)
                    return;
            }
            if (U(rng) < 0.10f)
                return;
            for (int k = U(rng) < 0.02f ? 2 : 1; k > 0; k--) {
                q.push_back({now + (uint32_t)(250 + (U(rng) * 2 - 1) * 120), p});
                std::push_heap(q.begin(), q.end());
            }
        };
        const int N = 3300; // a tribe save: ~3.3 MB in 1 KB chunks
        int queued = 0, got = 0;
        bool ordered = true;
        uint32_t now = 1000;
        std::vector<uint8_t> pkt;
        for (; got < N && now < 1000 + 120000; now += 33) {
            while (queued < N && a.pending_reliable() < window) {
                Writer w;
                w.u32(queued++);
                std::vector<uint8_t> pad(1010, 0x55);
                w.raw(pad.data(), pad.size());
                a.send_reliable(42, w);
            }
            for (unsigned i = 0; i < kAckWindow / 3 && a.build_packet(now, pkt); i++)
                send(ab, pkt, now);
            for (; !ab.empty() && ab.front().t <= now; ab.pop_back()) {
                std::pop_heap(ab.begin(), ab.end());
                auto& p = ab.back();
                b.on_packet(p.d.data(), p.d.size(), now, [&](uint8_t type, Reader& r) {
                    if (type == 42 && r.u32() != (uint32_t)got++)
                        ordered = false;
                });
            }
            while (b.build_packet(now, pkt))
                send(ba, pkt, now);
            for (; !ba.empty() && ba.front().t <= now; ba.pop_back()) {
                std::pop_heap(ba.begin(), ba.end());
                auto& p = ba.back();
                a.on_packet(p.d.data(), p.d.size(), now, [](uint8_t, Reader&) {});
            }
        }
        printf("  window %zu, seed %d: %d/%d in %.1f s, resends=%u rtt=%.0f\n", window, run + 1, got, N, (now - 1000) / 1000.0,
               a.stats.resends, a.rtt_ms);
        CHECK(got == N && ordered);
        CHECK(now - 1000 < 45000);
    }
}

// creations arrive from other players: the RefPack decoder must survive any input
static void test_refpack_untrusted()
{
    printf("RefPack decoder on hostile input\n");
    std::mt19937 rng(11);
    std::vector<uint8_t> out;
    int ok = 0;
    for (int k = 0; k < 2000; k++) {
        std::vector<uint8_t> in(5 + rng() % 600);
        for (auto& b : in)
            b = (uint8_t)rng();
        in[0] = 0x10;
        in[1] = 0xFB;
        in[2] = 0;
        in[3] = (uint8_t)(rng() % 8);
        in[4] = (uint8_t)rng(); // declared size < 2 KB
        uint32_t size = (uint32_t)in[3] << 8 | in[4];
        if (dbpf::refpack_decode(in.data(), in.size(), out))
            ok++;
        CHECK(out.size() <= size);
    }
    // a "bomb": a tiny literal then long back-references, far more than the declared 16 bytes
    std::vector<uint8_t> bomb = {0x10, 0xFB, 0, 0, 16, 0xE0 /* 4 literal bytes */, 'a', 'b', 'c', 'd'};
    for (int i = 0; i < 4000; i++) { bomb.push_back(0xDC); bomb.push_back(0); bomb.push_back(0); bomb.push_back(0xFF); }
    CHECK(!dbpf::refpack_decode(bomb.data(), bomb.size(), out));
    CHECK(out.size() <= 16);
    printf("  2000 random inputs (%d decoded), bomb rejected\n", ok);
}

// the updater: packs, paths, versions, the GitHub answer (updater/pack.h)
static void test_updater()
{
    printf("updater: packs, paths, versions, release answer\n");
    upd::Pack p;
    p.version = "0.5.1";
    p.files.push_back({"SporebinEP1/mods/coop.dll", 0, {1, 2, 3}});
    p.files.push_back({"SporebinEP1/mods/coop.ini", upd::kOnlyIfMissing, {'a'}});
    auto bytes = upd::write_pack(p);
    upd::Pack q;
    std::string err;
    CHECK(upd::parse_pack(bytes, q, err) && q.version == "0.5.1" && q.files.size() == 2 && q.files[0].data.size() == 3);
    auto bad = bytes;
    bad.back() ^= 1; // a damaged file
    CHECK(!upd::parse_pack(bad, q, err));
    bad = bytes;
    bad.resize(bad.size() - 1); // truncated
    CHECK(!upd::parse_pack(bad, q, err));
    for (const char* evil : {"../steam_api.dll", "SporebinEP1/../../x.dll", "C:/Windows/x.dll", "/etc/x",
                             "SporebinEP1\\mods\\x.dll", "SporebinEP1/SporeApp.exe", "SporebinEP1/mods/../x.dll",
                             "SporebinEP1/mods/a:b.dll", "SporebinEP1/mods//x.dll"})
        CHECK(!upd::allowed_path(evil));
    for (const char* ok : {"SporebinEP1/steam_api.dll", "SporebinEP1/sporekit.dll", "SporebinEP1/mods/coop.dll",
                           "SporebinEP1/mods/sporekit/symbols/gog2016_922af7c7.sym", "Data/SporeCoop.package"})
        CHECK(upd::allowed_path(ok));
    upd::Pack evil;
    evil.version = "9";
    evil.files.push_back({"SporebinEP1/SporeApp.exe", 0, {0}});
    CHECK(!upd::parse_pack(upd::write_pack(evil), q, err));
    CHECK(upd::compare_versions("0.5.1", "0.5.0") == 1 && upd::compare_versions("v0.5.0", "0.5.0") == 0 &&
          upd::compare_versions("0.10.0", "0.9.9") == 1 && upd::compare_versions("1.0", "1.0.1") == -1);
    CHECK(upd::compare_versions("0.7.2.1", "0.7.2") == 1 && upd::compare_versions("0.7.3", "0.7.2.1") == 1 &&
          upd::compare_versions("v0.7.2.1", "0.7.2.1") == 0);
    CHECK(upd::compare_versions("0.7.3.0-ga-preview.7", "0.7.2.1") == 1);
    CHECK(upd::compare_versions("0.7.3.1-ga-preview.8", "0.7.3.0-ga-preview.7") == 1);
    const char* json = R"({"html_url":"https://github.com/WoodyWizard/SporeCoop/releases/tag/v0.5.1","tag_name":"v0.5.1",
      "assets":[{"name":"SporeCoop-0.5.1.zip","browser_download_url":"https://github.com/WoodyWizard/SporeCoop/releases/download/v0.5.1/SporeCoop-0.5.1.zip"},
      {"name":"SporeCoop-0.5.1.pack","browser_download_url":"https://github.com/WoodyWizard/SporeCoop/releases/download/v0.5.1/SporeCoop-0.5.1.pack"},
      {"name":"evil.pack","browser_download_url":"https://evil.example/x.pack"},
      {"name":"SHA256SUMS.txt","browser_download_url":"https://github.com/WoodyWizard/SporeCoop/releases/download/v0.5.1/SHA256SUMS.txt"}]})";
    upd::Release r;
    CHECK(upd::parse_release(json, r) && r.version == "0.5.1" && r.tag == "v0.5.1");
    CHECK(r.pack_url.find("github.com/WoodyWizard") != std::string::npos && r.sums_url.size() > 0);
    std::string h(64, 'a');
    CHECK(upd::sums_lookup(h + "  SporeCoop-0.5.1.pack\r\n" + std::string(64, 'b') + " *x.zip\n", "SporeCoop-0.5.1.pack") == h);
    CHECK(upd::sums_lookup(h + "  other.pack\n", "SporeCoop-0.5.1.pack").empty());
}

int main()
{
    test_buffer();
    test_adventure_motion();
    test_unreliable_chunks();
    test_dbpf();
    test_refpack_untrusted();
    test_updater();
    test_reliability();
    test_ackbits();
    test_bulk();
    test_bulk_bad_link();
    test_session();
    test_protocol_versions();
    test_rejoin_same_endpoint();
    test_big_snapshot();
    test_interp();
    test_discovery();
    printf(g_fail ? "FAILED (%d)\n" : "ALL OK\n", g_fail);
    return g_fail ? 1 : 0;
}
