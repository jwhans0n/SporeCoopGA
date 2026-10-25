#include "adventure_flow.h"
#include "adventure_intro_gate.h"

#include <chrono>
#include <cstdio>
#include <memory>
#include <thread>
#include <utility>

static int failures;
#define CHECK(c) do { if(!(c)) { std::printf("FAIL %d: %s\n",__LINE__,#c); ++failures; } } while(0)
static std::vector<std::pair<SkTaskFn,void*>> tasks;
static void queue(SkTaskFn f,void* p) { tasks.emplace_back(f,p); }
static void drain() { while(!tasks.empty()) { auto pending=std::move(tasks); tasks.clear(); for(auto [f,p]:pending) f(p); } }
static void put64(net::Writer& w,uint64_t n) { w.u32(uint32_t(n)); w.u32(uint32_t(n>>32)); }
static net::Writer packet(uint8_t kind,adventure_flow::Scene s)
{
    net::Writer w; w.u8(kind); put64(w,s.run); put64(w,s.serial); w.u8(uint8_t(s.kind));
    w.u8(s.act); w.u8(s.goal); w.u32(s.marker); w.u32(s.incarnation); return w;
}
static void incoming(uint8_t from,const net::Writer& w) { net::Reader r(w.data.data(),w.size()); adventure_flow::on_message(from,r); }
static bool connect(net::Session& h,net::Session& g)
{
    std::string error;
    if(!g.join("127.0.0.1:"+std::to_string(h.port()),"Guest",error)) return false;
    for(int i=0;i<1000;++i) {
        g.update(); h.update(); g.update();
        if(g.connected()) return true;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    return false;
}
static void test_intro_cinematic_gate()
{
    using namespace adventure_intro;
    Gate gate;
    Cinematic current{0x12340000, 2, 0x789ABCDE};
    bool black_bar = true, input_locked = true;
    unsigned cleanups = 0, native_messages = 0;
    // The reviewed native function cleans the UI first and only then emits its
    // end message. Capturing at that function's entry must retain both resources.
    CHECK(gate.capture(77, current));
    CHECK(black_bar && input_locked && cleanups == 0 && native_messages == 0);
    CHECK(gate.capture(77, current)); // repeated local Continue is idempotent
    CHECK(gate.pending() && gate.run() == 77);
    CHECK(gate.release(76, current).action == Action::Ignore);
    CHECK(gate.pending());
    auto release = gate.release(77, current);
    CHECK(release.action == Action::EndCinematic && gate.releasing());
    if (release.action == Action::EndCinematic) {
        black_bar = input_locked = false; ++cleanups;
        CHECK(gate.releasing()); // emitted044F1189 must bypass the message gate
        ++native_messages;
    }
    gate.finish(release);
    CHECK(!gate.pending() && !gate.releasing() && cleanups == 1 && native_messages == 1);
    CHECK(gate.release(77, current).action == Action::Ignore);
    CHECK(!gate.capture(0, current)); CHECK(!gate.capture(77, {}));
    CHECK(!gate.capture(77, {current.manager, 0, current.id}));
    // A reused pointer alone is insufficient: match the native scene ID and run.
    CHECK(gate.capture(78, current));
    auto replaced = current; ++replaced.id;
    CHECK(!gate.capture(78, replaced));
    CHECK(gate.release(78, replaced).action == Action::Changed);
    CHECK(!gate.pending() && !gate.releasing());
    CHECK(gate.capture(79, current));
    auto other_manager = current; ++other_manager.manager;
    CHECK(gate.release(79, other_manager).action == Action::Changed);
    CHECK(gate.capture(80, current));
    gate.reset();
    CHECK(gate.release(80, current).action == Action::Ignore);
    CHECK(gate.capture(81, current));
    CHECK(gate.release(80, current).action == Action::Ignore);
    CHECK(gate.pending() && gate.run() == 81);
    // A fallback end message cannot erase a previously captured native token.
    gate.legacy_message(81);
    CHECK(gate.release(81, current).action == Action::EndCinematic);
    gate.reset();
    gate.legacy_message(82);
    release = gate.release(82, {});
    CHECK(release.action == Action::PlayMessage && gate.releasing());
    gate.reset();
    CHECK(gate.capture(83, current));
    auto next = gate.release(83, current);
    gate.finish(release); // an older completion cannot clear a later guard
    CHECK(gate.releasing());
    gate.finish(next);
    CHECK(!gate.releasing());
    gate.reset();
    CHECK(gate.capture(84, current, 0x0445F729));
    release = gate.release(84, current);
    CHECK(release.action == Action::CinematicMessage && release.native_message == 0x0445F729);
    CHECK(gate.releasing() && gate.released(84));
    gate.finish(release);
    CHECK(!gate.releasing() && gate.released(84));
    // Native caption advancement can emit EndCinematic after the release call
    // returned. The released run still bypasses both cinematic/play message gates.
    CHECK(!gate.capture(84, current));
    gate.legacy_message(84);
    CHECK(!gate.pending());
    CHECK(!gate.capture(84, current, 0x044EE075));
    gate.reset();
    CHECK(!gate.released(84));
    CHECK(gate.capture(85, current, 0x044EE076));
    release = gate.release(85, current);
    CHECK(release.native_message == 0x044EE076);
    gate.finish(release);
}

static void test_talk_cinematic_gate()
{
    using namespace adventure_intro;
    SceneGate gate;
    Cinematic cinematic{0x12340000, 2, 0x11223344};
    CHECK(gate.begin(7, 2, cinematic));
    CHECK(!gate.capture(6, 2, cinematic, 0x0445F729));
    CHECK(!gate.capture(7, 1, cinematic, 0x0445F729));
    auto replaced = cinematic; ++replaced.id;
    CHECK(!gate.capture(7, 2, replaced, 0x0445F729));
    CHECK(gate.capture(7, 2, cinematic, 0x0445F729));
    CHECK(gate.release(7, 1, cinematic).action == Action::Ignore);
    auto release = gate.release(7, 2, cinematic);
    CHECK(release.action == Action::CinematicMessage && gate.settling());
    gate.replayed(release);
    // The replayed Finished branch returns before the cinematic publishes its
    // end on another frame. Keep settling until the exact native ID arrives.
    CHECK(gate.settling() && !gate.complete(8, cinematic.id) && !gate.complete(7, cinematic.id + 1));
    CHECK(!gate.capture(7, 2, cinematic, 0x0445F729));
    CHECK(gate.complete(7, cinematic.id));
    CHECK(!gate.settling() && gate.duplicate(7, cinematic.id));
    CHECK(!gate.complete(7, cinematic.id)); // one cleanup and host objective commit
    CHECK(gate.begin(7, 3, replaced));
    CHECK(!gate.duplicate(7, cinematic.id));
    CHECK(gate.release(7, 2, cinematic).action == Action::Ignore);
    release = gate.release(7, 3, replaced);
    CHECK(gate.settling());
    auto generation = gate.generation();
    gate.reset();
    CHECK(gate.generation() != generation && !gate.settling());
    CHECK(!gate.complete(7, replaced.id)); // cancelled delayed native end
    CHECK(gate.begin(8, 2, cinematic));
    gate.replayed(release); // old native replay completion cannot clear new ownership
    CHECK(gate.owns(8, cinematic));
    CHECK(gate.release(8, 2, replaced).action == Action::Changed);
    CHECK(!gate.settling());
}

static void test_async_queued_talk()
{
    namespace flow = adventure_flow;
    net::Session host, guest; host.build = guest.build = 1;
    std::string error; CHECK(host.host(0, "Host", error)); CHECK(connect(host, guest));
    SkHost api{}; api.run_outside_frame = queue;
    uint64_t run = 700; bool running = true, settling = false; uint8_t native_act = 1;
    unsigned presentations = 0, releases = 0;
    flow::Ctx c; c.sess = &host; c.run = [&] { return run; }; c.running = [&] { return running; };
    c.members = [] { return std::vector<uint8_t>{0, 1}; };
    c.validate_talk = [](uint8_t, const flow::Scene&) { return true; };
    c.present = [&](const flow::Scene& s) {
        if (settling || s.act != native_act) return false;
        ++presentations; return true;
    };
    c.release = [&](const flow::Scene& s) { if (s.kind == flow::Kind::Talk) { ++releases; settling = true; } };
    c.settling = [&] { return settling; }; c.waiting = [](bool) {};
    CHECK(flow::init(&api, c)); flow::tick(); drain();
    flow::confirm_intro(); incoming(1, packet(161, flow::scene())); flow::tick(); drain();
    CHECK(!flow::held());
    flow::Scene request; request.kind = flow::Kind::Talk; request.act = 1; request.marker = 42; request.incarnation = 3;
    CHECK(flow::request_talk(request)); flow::tick(); drain(); flow::tick(); drain();
    auto first = flow::scene(); CHECK(presentations == 1 && flow::finish_talk());
    request.marker = 43; CHECK(flow::request_talk(request));
    incoming(1, packet(161, first)); flow::tick(); drain();
    CHECK(releases == 1 && settling && flow::held() && flow::scene().kind == flow::Kind::None);
    flow::tick(); drain(); flow::tick(); drain();
    CHECK(presentations == 1 && flow::scene().kind == flow::Kind::None);
    incoming(1, packet(161, first)); flow::tick(); drain(); CHECK(releases == 1);
    // Native cleanup arrives asynchronously. Only then may the queued Talk own
    // the manager and advance the new dialog independently.
    settling = false; flow::tick(); drain(); flow::tick(); drain();
    auto second = flow::scene();
    CHECK(second.marker == 43 && second.serial > first.serial && presentations == 2);
    CHECK(flow::finish_talk()); incoming(1, packet(161, second)); flow::tick(); drain();
    CHECK(releases == 2 && settling && flow::held());
    // Reset invalidates queued releases; the native adapter resets its matching
    // settling token as part of the same cancellation cleanup.
    flow::tick(); flow::reset(); settling = false; running = false; drain();
    CHECK(!flow::held() && releases == 2);
    run = 701; running = true; flow::tick(); drain();
    incoming(1, packet(162, second)); drain();
    CHECK(flow::scene().kind == flow::Kind::Intro && releases == 2);
    running = false; flow::reset();

    // A guest sets release-pending synchronously on receive, before the queued
    // adapter call has had a chance to establish its own settling token.
    c.sess = &guest; run = 702; running = true;
    CHECK(flow::init(&api, c)); flow::tick(); drain();
    incoming(0, packet(162, flow::scene()));
    CHECK(flow::release_pending() && flow::held());
    drain(); CHECK(!flow::release_pending() && !flow::held());
    request.run = run; request.serial = 2;
    auto snapshot = packet(160, request); snapshot.u8(0); incoming(0, snapshot);
    flow::tick(); drain(); CHECK(flow::finish_talk());
    incoming(0, packet(162, flow::scene()));
    CHECK(flow::release_pending() && flow::held() && !settling);
    // The coordinator's result-hold callback sees this flag even in the gap.
    bool hold_result = flow::release_pending() || settling; CHECK(hold_result);
    drain(); CHECK(!flow::release_pending() && settling && flow::held() && releases == 3);
    // The host can advance the act and offer another Talk before this guest's
    // old native cinematic end arrives. Protect the old dialog first, then let
    // the authoritative act prerequisite through before presenting the new one.
    request.serial = 3; request.act = 2;
    snapshot = packet(160, request); snapshot.u8(0); incoming(0, snapshot);
    flow::tick(); drain(); CHECK(flow::native_scene_held() && !flow::finish_talk());
    settling = false;
    CHECK(flow::held() && !flow::native_scene_held());
    native_act = 2; // deferred authoritative act is now eligible to apply
    flow::tick(); drain(); CHECK(flow::native_scene_held() && flow::finish_talk());
    incoming(0, packet(162, flow::scene())); CHECK(flow::release_pending());
    flow::reset(); running = false; drain();
    CHECK(!flow::release_pending() && releases == 3); // cancelled queued release never owns the manager
}
int main()
{
    test_intro_cinematic_gate();
    test_talk_cinematic_gate();
    test_async_queued_talk();
    namespace flow=adventure_flow;
    net::Session host,g1,g2; host.build=g1.build=g2.build=1;
    std::string error; CHECK(host.host(0,"Host",error)); CHECK(connect(host,g1)); CHECK(connect(host,g2));
    SkHost api{}; api.run_outside_frame=queue;
    uint64_t run=99; bool running=true,available=true,waiting=false;
    unsigned presentations=0,intros=0,talks=0;
    flow::Ctx c; c.sess=&host; c.run=[&] { return run; }; c.running=[&] { return running; };
    c.members=[] { return std::vector<uint8_t>{0,1,2}; };
    c.validate_talk=[](uint8_t,const flow::Scene& s) { return s.act==1 && s.goal<2 && s.marker>=42 && s.marker<=43; };
    c.present=[&](const flow::Scene&) { ++presentations; return available; };
    c.release=[&](const flow::Scene& s) { CHECK(!flow::held() || flow::release_pending()); if(s.kind==flow::Kind::Intro) ++intros; else ++talks; };
    c.waiting=[&](bool w) { waiting=w; };
    CHECK(flow::init(&api,c));
    flow::confirm_intro(); flow::tick(); drain();
    CHECK(flow::held() && flow::local_waiting() && waiting && intros==0);
    flow::ConfirmationView view;
    CHECK(flow::confirmation_view(view) && view.players.size()==3);
    CHECK(view.players[0].local && view.players[0].confirmed && !view.players[0].pending);
    CHECK(view.players[0].name=="Host" && !view.players[1].confirmed);
    auto intro=flow::scene();
    auto stale=intro; stale.run--; incoming(1,packet(161,stale));
    incoming(1,packet(161,intro)); flow::tick(); drain(); CHECK(flow::held());
    incoming(3,packet(161,intro)); flow::tick(); drain(); CHECK(flow::held());
    incoming(2,packet(161,intro)); flow::tick(); drain(); CHECK(!flow::held() && intros==1 && !waiting);
    CHECK(!flow::confirmation_view(view) && view.players.empty());
    incoming(2,packet(161,intro)); flow::tick(); drain(); CHECK(intros==1);
    flow::Scene request; request.kind=flow::Kind::Talk; request.act=1; request.goal=0; request.marker=42; request.incarnation=7;
    CHECK(flow::request_talk(request)); flow::tick(); drain(); flow::tick(); drain();
    auto talk=flow::scene(); CHECK(flow::held() && talk.kind==flow::Kind::Talk && presentations==1);
    CHECK(flow::finish_talk()); CHECK(!flow::finish_talk());
    incoming(1,packet(161,intro)); incoming(1,packet(161,talk)); flow::tick(); drain(); CHECK(talks==0);
    auto second=request; second.run=run; second.goal=1; second.marker=43;
    incoming(2,packet(163,second));
    incoming(2,packet(161,talk)); flow::tick(); drain(); CHECK(talks==1);
    flow::tick(); drain(); auto next=flow::scene(); CHECK(next.serial>talk.serial && next.marker==43);
    incoming(1,packet(161,talk)); incoming(2,packet(161,talk)); flow::tick(); drain(); CHECK(talks==1);
    CHECK(flow::finish_talk()); incoming(1,packet(161,next));
    g2.leave("test");
    for(int i=0;i<10;++i) { host.update(); g1.update(); }
    flow::tick(); drain(); CHECK(talks==2 && !flow::held());
    CHECK(flow::request_talk(request)); flow::tick(); run=100; flow::reset(); drain(); CHECK(talks==2);
    // A client accepts only host scenes/releases and cannot finish unseen dialog.
    c.sess=&g1; c.members=[] { return std::vector<uint8_t>{0,1}; };
    CHECK(flow::init(&api,c)); flow::tick(); drain(); intro=flow::scene();
    auto forged=packet(160,intro); forged.u8(0); incoming(2,forged); CHECK(flow::held());
    flow::confirm_intro(); flow::tick(); drain();
    CHECK(flow::confirmation_view(view) && view.players.size()==2);
    CHECK(view.players[1].local && view.players[1].pending && !view.players[1].confirmed);
    auto accepted=packet(160,intro); accepted.u8(1); accepted.u8(g1.my_id()); incoming(0,accepted);
    CHECK(flow::confirmation_view(view) && view.players[1].confirmed && !view.players[1].pending);
    incoming(0,packet(162,intro)); drain(); CHECK(!flow::held() && intros==2);
    incoming(0,packet(162,intro)); drain(); CHECK(intros==2);
    request.run=run; request.serial=2; auto snapshot=packet(160,request); snapshot.u8(0);
    available=false; incoming(0,snapshot); flow::tick(); drain(); CHECK(!flow::finish_talk());
    available=true; flow::tick(); drain(); CHECK(flow::finish_talk());
    flow::tick(); drain(); incoming(0,packet(162,request)); drain(); CHECK(talks==3 && !flow::held());
    running=false; flow::tick(); drain(); CHECK(!flow::held());
    std::printf("adventure flow: %d failures\n",failures); return failures?1:0;
}
