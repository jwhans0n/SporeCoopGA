#include "adventure_flow.h"

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
int main()
{
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
    c.release=[&](const flow::Scene& s) { CHECK(!flow::held()); if(s.kind==flow::Kind::Intro) ++intros; else ++talks; };
    c.waiting=[&](bool w) { waiting=w; };
    CHECK(flow::init(&api,c));
    flow::confirm_intro(); flow::tick(); drain();
    CHECK(flow::held() && flow::local_waiting() && waiting && intros==0);
    auto intro=flow::scene();
    auto stale=intro; stale.run--; incoming(1,packet(161,stale));
    incoming(1,packet(161,intro)); flow::tick(); drain(); CHECK(flow::held());
    incoming(3,packet(161,intro)); flow::tick(); drain(); CHECK(flow::held());
    incoming(2,packet(161,intro)); flow::tick(); drain(); CHECK(!flow::held() && intros==1 && !waiting);
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
    incoming(0,packet(162,intro)); drain(); CHECK(!flow::held() && intros==2);
    incoming(0,packet(162,intro)); drain(); CHECK(intros==2);
    request.run=run; request.serial=2; auto snapshot=packet(160,request); snapshot.u8(0);
    available=false; incoming(0,snapshot); flow::tick(); drain(); CHECK(!flow::finish_talk());
    available=true; flow::tick(); drain(); CHECK(flow::finish_talk());
    flow::tick(); drain(); incoming(0,packet(162,request)); drain(); CHECK(talks==3 && !flow::held());
    running=false; flow::tick(); drain(); CHECK(!flow::held());
    std::printf("adventure flow: %d failures\n",failures); return failures?1:0;
}
