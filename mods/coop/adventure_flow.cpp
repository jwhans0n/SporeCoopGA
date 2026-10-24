#include "adventure_flow.h"

#include <algorithm>
#include <deque>
#include <memory>
#include <set>
#include <utility>

namespace adventure_flow {
namespace {
enum Packet : uint8_t { Snapshot = 160, Confirm, Release, RequestTalk };
const SkHost* H;
Ctx C;
Scene current;
uint64_t generation, started_run, early_intro, sequence;
bool intro_finished, presented, confirmed, confirm_sent, queued, releasing;
std::set<uint8_t> done;
std::deque<std::pair<uint8_t, Scene>> requests;

bool host() { return C.sess && C.sess->role() == net::Role::Host; }
uint64_t run() { return C.run ? C.run() : 0; }
bool running() { return C.sess && C.sess->connected() && C.running && C.running() && run(); }
void put64(net::Writer& w, uint64_t n) { w.u32(uint32_t(n)); w.u32(uint32_t(n >> 32)); }
uint64_t get64(net::Reader& r) { uint64_t n = r.u32(); return n | (uint64_t(r.u32()) << 32); }
void write(net::Writer& w, const Scene& s)
{
    put64(w,s.run); put64(w,s.serial); w.u8(uint8_t(s.kind)); w.u8(s.act); w.u8(s.goal);
    w.u32(s.marker); w.u32(s.incarnation);
}
Scene read(net::Reader& r)
{
    Scene s; s.run=get64(r); s.serial=get64(r); s.kind=Kind(r.u8()); s.act=r.u8(); s.goal=r.u8();
    s.marker=r.u32(); s.incarnation=r.u32(); return s;
}
bool same(const Scene& a, const Scene& b)
{
    return a.run==b.run && a.serial==b.serial && a.kind==b.kind && a.act==b.act && a.goal==b.goal &&
        a.marker==b.marker && a.incarnation==b.incarnation;
}
std::vector<uint8_t> members()
{
    auto ids = C.members ? C.members() : std::vector<uint8_t>{};
    auto players = C.sess->players();
    ids.erase(std::remove_if(ids.begin(),ids.end(),[&](uint8_t id) {
        return std::none_of(players.begin(),players.end(),[&](const auto& p) { return p.id==id; });
    }),ids.end());
    return ids;
}
bool member(uint8_t id)
{
    auto ids=members(); return std::find(ids.begin(),ids.end(),id)!=ids.end();
}
void send(Packet kind, const Scene& s)
{
    net::Writer w; w.u8(kind); write(w,s);
    if(kind==Snapshot) {
        auto ids=members();
        std::vector<uint8_t> live;
        for(uint8_t id:ids) if(done.count(id)) live.push_back(id);
        w.u8(uint8_t(live.size())); for(uint8_t id:live) w.u8(id);
    }
    if(host()) C.sess->broadcast_reliable(net::MSG_ADVENTURE,w);
    else C.sess->send_reliable_to(0,net::MSG_ADVENTURE,w);
}
void begin(Scene s)
{
    current=s; done.clear(); presented=s.kind==Kind::Intro;
    confirmed=s.kind==Kind::Intro && early_intro==s.run;
    confirm_sent=false;
}
void complete()
{
    Scene previous=current;
    send(Release,previous);
    current.kind=Kind::None;
    if(previous.kind==Kind::Intro) intro_finished=true;
    confirmed=confirm_sent=presented=false;
    releasing=true;
    C.waiting(false);
    C.release(previous);
    releasing=false;
}
void update_native()
{
    if(!running()) return;
    if(current.kind!=Kind::None && !presented) presented=C.present(current);
    if(current.kind!=Kind::None && confirmed && !confirm_sent && presented) {
        confirm_sent=true;
        if(host()) { done.insert(C.sess->my_id()); send(Snapshot,current); }
        else send(Confirm,current);
        C.waiting(true);
    }
    if(host() && current.kind!=Kind::None) {
        auto ids=members();
        if(!ids.empty() && std::all_of(ids.begin(),ids.end(),[](uint8_t id) { return done.count(id); })) complete();
    }
    if(host() && current.kind==Kind::None && intro_finished && !releasing) {
        while(!requests.empty()) {
            auto [id,s]=requests.front(); requests.pop_front();
            if(s.run!=run() || !member(id) || !C.validate_talk(id,s)) continue;
            s.serial=++sequence; begin(s); send(Snapshot,current); break;
        }
    }
}
} // namespace

bool init(const SkHost* h, Ctx ctx)
{
    H=h; C=std::move(ctx); reset();
    return H && H->run_outside_frame && C.sess && C.run && C.running && C.members &&
        C.validate_talk && C.present && C.release && C.waiting;
}
void reset()
{
    ++generation; current={}; started_run=early_intro=sequence=0;
    intro_finished=presented=confirmed=confirm_sent=queued=releasing=false;
    done.clear(); requests.clear();
}
void tick()
{
    if(!running()) {
        if(started_run) { auto early=early_intro; reset(); if(early==run()) early_intro=early; }
        return;
    }
    if(started_run!=run()) {
        ++generation; started_run=run(); sequence=1; intro_finished=false; requests.clear();
        begin({run(),sequence,Kind::Intro});
        if(host()) send(Snapshot,current);
    }
    if(queued) return;
    queued=true;
    struct Task { uint64_t generation, run; };
    H->run_outside_frame([](void* arg) {
        std::unique_ptr<Task> t(static_cast<Task*>(arg));
        if(t->generation!=generation) return;
        queued=false;
        if(t->run==run()) update_native();
    },new Task{generation,run()});
}
void confirm_intro()
{
    early_intro=run();
    if(running() && current.kind==Kind::Intro) confirmed=true;
}
bool request_talk(Scene s)
{
    if(!running() || !intro_finished || s.kind!=Kind::Talk || s.act>7 || s.goal>7) return false;
    s.run=run(); s.serial=0;
    if(host()) {
        if(requests.size()<8) requests.emplace_back(C.sess->my_id(),s);
    } else send(RequestTalk,s);
    return true;
}
bool finish_talk()
{
    if(!running() || current.kind!=Kind::Talk || !presented || confirmed) return false;
    confirmed=true; return true;
}
bool held() { return running() && (!intro_finished || current.kind!=Kind::None); }
bool local_waiting() { return held() && confirmed; }
bool ready(uint8_t player) { return held() && (done.count(player) || (player==C.sess->my_id() && confirmed)); }
Scene scene() { return current; }
std::string status()
{
    if(!held()) return {};
    std::string s=current.kind==Kind::Talk ? "Conversation" : "Beam-down";
    auto ids=members();
    size_t count=std::count_if(ids.begin(),ids.end(),[](uint8_t id) { return done.count(id); });
    s+=" confirmations: "+std::to_string(count)+"/"+std::to_string(ids.size());
    for(uint8_t id:ids) s+="\n"+C.sess->name_of(id)+(done.count(id)?": Ready":": Waiting");
    return s;
}
void on_message(uint8_t from, net::Reader& r)
{
    uint8_t kind=r.u8(); Scene s=read(r);
    if(!r.ok() || !running() || s.run!=run() || s.kind<Kind::Intro || s.kind>Kind::Talk || s.act>7 || s.goal>7) return;
    if(host()) {
        if(!from || !member(from)) return;
        if(kind==Confirm && !r.left() && same(s,current)) { done.insert(from); send(Snapshot,current); }
        else if(kind==RequestTalk && !r.left() && s.kind==Kind::Talk && !s.serial && intro_finished && requests.size()<8) {
            bool duplicate=std::any_of(requests.begin(),requests.end(),[&](const auto& q) {
                return q.second.act==s.act && q.second.goal==s.goal && q.second.marker==s.marker && q.second.incarnation==s.incarnation;
            });
            if(!duplicate && !(current.kind==Kind::Talk && current.act==s.act && current.goal==s.goal && current.marker==s.marker && current.incarnation==s.incarnation))
                requests.emplace_back(from,s);
        }
        return;
    }
    if(from || !s.serial || s.serial<current.serial) return;
    if(kind==Snapshot) {
        uint8_t count=r.u8(); std::set<uint8_t> ready;
        if(count>net::kMaxPlayers) return;
        for(uint8_t i=0;i<count;++i) { uint8_t id=r.u8(); if(!member(id) || !ready.insert(id).second) return; }
        if(!r.ok() || r.left() || (s.serial==current.serial && !same(s,current))) return;
        if(s.serial>current.serial) begin(s);
        done=std::move(ready);
    } else if(kind==Release && !r.left() && same(s,current)) {
        current.kind=Kind::None;
        if(s.kind==Kind::Intro) intro_finished=true;
        confirmed=confirm_sent=presented=false;
        struct Task { uint64_t generation; Scene scene; };
        H->run_outside_frame([](void* arg) {
            std::unique_ptr<Task> t(static_cast<Task*>(arg));
            if(t->generation!=generation || t->scene.run!=run() || !running()) return;
            C.waiting(false); C.release(t->scene);
        },new Task{generation,s});
    }
}
} // namespace adventure_flow
