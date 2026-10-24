#include "adventure_pickups.h"

#include <algorithm>
#include <cmath>
#include <cstring>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <utility>
#include <vector>

#include "game.h"
#include "sporekit_game.hpp"

namespace adventure_pickups {
namespace {
const SkHost* H;
Ctx C;
bool ready, queued;
bool overflow,overflow_reported;
uint64_t overflow_run;
uint64_t generation=1,current_run,serial;
constexpr uint8_t kNone=0xFF;
enum Message : uint8_t { Claim=152, Grant=153, Expire=154, Teleport=155 };
using Activate=void(SK_CDECL*)(void*,void*);
Activate originals[9]{};
uintptr_t a_mode,a_float,a_speed,a_effect,a_collect,a_goals,a_destination,a_teleport;
struct Effect {
    uint64_t revision=0;
    float amount=0;
    double until=0;
    bool active=false;
    uint32_t artwork=0;
    void* applied_captain=nullptr;
    uint32_t applied_incarnation=0;
    uint64_t applied_revision=0;
    bool applied_active=false;
};
struct Original {
    void* captain=nullptr;
    uint32_t incarnation=0;
    bool damage=false,armor=false,speed=false,cooldown=false;
    float damage_amount=0,armor_amount=0;
};
struct Event {
    uint8_t kind=0,act=0,player=0,type=0;
    Identity item{};
    uint64_t revision=0,ref=0,run=0;
    float amount=0,until=0;
    uint32_t effect=0;
    net::Vec3 position{};
    net::Quat orientation{};
};
std::map<std::pair<uint8_t,uint8_t>,Effect> effects;
std::map<uint8_t,Original> originals_state;
std::map<uint8_t,uint64_t> teleports;
std::map<uint8_t,uint64_t> teleport_cleanup;
std::map<uint8_t,double> cooldowns;
std::map<uint8_t,uint64_t> claims;
std::map<std::pair<uint8_t,Identity>,double> requested;
std::set<Identity> consumed;
std::vector<Event> pending;
std::map<uint8_t,std::vector<Event>> outgoing;
unsigned grants,warps,rejected;

bool host() { return C.sess && C.sess->role()==net::Role::Host; }
bool active() { return ready && C.sess && C.sess->connected() && C.active && C.active() && C.run && C.run(); }
bool held() { return C.held && C.held(); }
int act() { return C.act ? C.act() : -1; }
double clock() { return C.simulation_time ? C.simulation_time() : 0; }
bool supported(int kind) { return kind==1 || kind==3 || kind==4 || kind==5 || kind==8; }
bool finite(net::Vec3 p) { return std::isfinite(p.x)&&std::isfinite(p.y)&&std::isfinite(p.z); }
bool finite(net::Quat q) { return std::isfinite(q.x)&&std::isfinite(q.y)&&std::isfinite(q.z)&&std::isfinite(q.w); }
void put64(net::Writer& w,uint64_t n) { w.u32(uint32_t(n));w.u32(uint32_t(n>>32)); }
uint64_t get64(net::Reader& r) { uint64_t n=r.u32();return n|(uint64_t(r.u32())<<32); }
void put_id(net::Writer& w,Identity n) { w.u32(n.marker);w.u32(n.incarnation); }
Identity get_id(net::Reader& r) { return {r.u32(),r.u32()}; }
void* powerup(void* object) {
    void* value=object ? game::cast(object,0x08367427) : nullptr;
    return value&&skg::readable(value,0x3C) ? value : nullptr;
}
uint8_t owner(void* captain) {
    if(captain && C.captain_of) for(const auto& p:C.sess->players()) if(C.captain_of(p.id)==captain) return p.id;
    return kNone;
}
void* play() {
    auto* m=a_mode ? *reinterpret_cast<uint8_t**>(a_mode) : nullptr;
    if(!m || !skg::readable(m,0xD0) || skg::field<int>(m,0xCC)!=2) return nullptr;
    auto* p=skg::field<void*>(m,0x78);
    return p && skg::readable(p,0xBC) ? p : nullptr;
}
uint32_t integer(void* list,uint32_t key) {
    // Same type and inline/pointer checks as the native pickup helpers.
    void* prop=nullptr;
    if(!list || !skg::readable(list,4) || !skg::vcall<bool>(list,0x24/4,key,&prop) ||
       !prop || !skg::readable(prop,0x14)) return 0;
    uint16_t type=skg::field<uint16_t>(prop,0x12);
    if(type!=0x0A && type!=0x10) return 0;
    if(skg::field<uint8_t>(prop,0x10)&0x30) {
        void* data=skg::field<void*>(prop,0);return data&&skg::readable(data,4) ? skg::field<uint32_t>(data,0) : 0;
    }
    return type ? skg::field<uint32_t>(prop,0) : 0;
}
float property(void* value,uint32_t key) {
    return skg::cdecl_call<float>(a_float,skg::field<void*>(value,0x18),key,0.f);
}
Original* remember(uint8_t player,void* captain) {
    if(!captain || !skg::readable(captain,0xFA8)) return nullptr;
    uint32_t incarnation=skg::field<uint32_t>(captain,0x24);
    auto& previous=originals_state[player];
    if(previous.captain!=captain || previous.incarnation!=incarnation) {
        previous={captain,incarnation,skg::field<bool>(captain,0xB10),skg::field<bool>(captain,0xB18),
            (skg::field<uint32_t>(captain,0xFA4)&128)!=0,skg::field<bool>(captain,0xE60),
            skg::field<float>(captain,0xB14),skg::field<float>(captain,0xB1C)};
    }
    return &previous;
}
void apply_effect(uint8_t player,uint8_t type,Effect& effect) {
    void* captain=C.captain_of(player);
    if(!remember(player,captain)) return;
    uint32_t incarnation=skg::field<uint32_t>(captain,0x24);
    if(effect.applied_captain==captain && effect.applied_incarnation==incarnation &&
       effect.applied_revision==effect.revision && effect.applied_active==effect.active) return;
    effect.applied_captain=captain;effect.applied_incarnation=incarnation;
    effect.applied_revision=effect.revision;effect.applied_active=effect.active;
    if(type==3) skg::thiscall<void>(a_speed,captain,7,effect.active);
    if(type==4) {
        skg::field<bool>(captain,0xB10)=effect.active;
        if(effect.active) skg::field<float>(captain,0xB14)=effect.amount;
    }
    if(type==5) {
        // Native attack calculation divides by B1C before our damage hook.
        // Captain armor instead belongs to the host's final damage authority,
        // covering environmental damage too, and must never be applied twice.
        skg::field<bool>(captain,0xB18)=false;
        if(effect.active) skg::field<float>(captain,0xB1C)=effect.amount;
    }
    if(effect.active && effect.artwork) skg::cdecl_call<void>(a_effect,captain,effect.artwork);
}
net::Writer encode(const Event& e) {
    net::Writer w;w.u8(e.kind);put64(w,current_run);put64(w,e.revision);w.u8(e.act);w.u8(e.player);w.u8(e.type);put_id(w,e.item);
    if(e.kind==Grant) {w.f32(e.amount);w.f32(e.until);w.u32(e.effect);}
    else if(e.kind==Expire) put64(w,e.ref);
    else if(e.kind==Teleport) {
        w.f32(e.position.x);w.f32(e.position.y);w.f32(e.position.z);
        w.f32(e.orientation.x);w.f32(e.orientation.y);w.f32(e.orientation.z);w.f32(e.orientation.w);w.f32(e.until);
    }
    return w;
}
bool publication_space() {
    // Leave room for all twelve captain boost expirations and four teleports.
    // A critical claim is rejected before mutation when its reliable backlog is
    // full. Native contact retries it after the connection catches up.
    for(const auto& p:C.sess->players()) if(p.id && outgoing[p.id].size()>=496) return false;
    return true;
}
void send(const Event& e) {
    for(const auto& p:C.sess->players()) if(p.id) outgoing[p.id].push_back(e);
}
void flush() {
    for(auto it=outgoing.begin();it!=outgoing.end();) {
        bool connected=false;
        for(const auto& p:C.sess->players()) if(p.id==it->first) connected=true;
        if(!connected) {it=outgoing.erase(it);continue;}
        auto& messages=it->second;
        for(unsigned n=0;n<4&&!messages.empty()&&C.sess->pending_to(it->first)<64;++n) {
            C.sess->send_reliable_to(it->first,net::MSG_ADVENTURE,encode(messages.front()));
            messages.erase(messages.begin());
        }
        ++it;
    }
}
void consume(Identity id,void* object) {
    consumed.insert(id);
    C.retire(id);
    if(object && game::object_alive(object)) game::destroy_creature(object);
}
void collected(void* item,void* captain) {
    auto* p=play();
    if(!p || !captain || !skg::readable(captain,0xB5C)) return;
    // Native collect goals require the avatar bit even when a correctly owned
    // co-op captain is represented by an ordinary creature on the host.
    uint32_t flags=skg::field<uint32_t>(captain,0xB58);
    uint32_t incarnation=skg::field<uint32_t>(captain,0x24);
    skg::field<uint32_t>(captain,0xB58)=flags|512;
    skg::thiscall<void>(a_collect,p,item,captain);
    if(skg::readable(captain,0xB5C)&&skg::field<uint32_t>(captain,0x24)==incarnation)
        skg::field<uint32_t>(captain,0xB58)=flags;
}
bool in_range(void* item,void* captain) {
    net::Vec3 a,b;
    if(!game::object_position(item,a)||!game::object_position(captain,b)) return false;
    void* spatial=game::cast(item,0x01186577);
    if(!spatial) return false;
    float radius=skg::vcall<float>(spatial,0x70/4)+game::bounding_radius(captain)+1.f;
    if(!std::isfinite(radius)||radius<0||radius>1000) return false;
    float x=a.x-b.x,y=a.y-b.y,z=a.z-b.z;
    return x*x+y*y+z*z<=radius*radius;
}
void commit(Event e) {
    if(!active() || held() || e.act!=act() || !C.captain_can_act(e.player) || !publication_space()) {++rejected;return;}
    void* item=C.object_for(e.item);void* captain=C.captain_of(e.player);void* value=powerup(item);
    if(!item || !value || !game::object_alive(item) || consumed.count(e.item) || !captain ||
       !skg::readable(captain,0xFA8) || skg::field<int>(value,4)!=e.type ||
       !supported(e.type) || skg::field<bool>(captain,0xB5E) || !in_range(item,captain)) {++rejected;return;}
    if(e.type==8) {
        if(skg::field<bool>(captain,0xE60) || (skg::field<uint32_t>(captain,0xB58)&256) || cooldowns[e.player]>clock() ||
           !skg::cdecl_call<bool>(a_destination,value,&e.position,&e.orientation) || !finite(e.position)||!finite(e.orientation)) {++rejected;return;}
        e.kind=Teleport;e.revision=++serial;e.until=float(clock()+2.0);
        // The owner helper has the native clamp, flight/path cleanup, stop and
        // authored effect. The larger native activation also moves the posse.
        skg::cdecl_call<void>(a_teleport,value,captain,&e.position,&e.orientation,true);
        game::object_position(captain,e.position);e.orientation=game::orientation(captain);
        remember(e.player,captain);skg::field<bool>(captain,0xE60)=true;
        cooldowns[e.player]=e.until;teleports[e.player]=teleport_cleanup[e.player]=e.revision;
        C.teleport(e.player,e.revision,e.position,e.orientation);
        collected(item,captain);
        send(e);++warps;return;
    }
    // Native ReactToPowerup permits upgrade collection only for avatars. Remote
    // co-op captains are equivalent, but NPCs and dead captains are not.
    float duration=e.type==1 ? 0 : property(value,0xAAC3B137);
    float amount=e.type==3 ? 1.f : e.type==1 ? 0.f : property(value,0x6219412B);
    if(!std::isfinite(duration)||duration<0||!std::isfinite(amount)||amount<0 || (e.type==5&&amount==0)) {++rejected;return;}
    e.kind=Grant;e.revision=++serial;e.amount=amount;e.until=float(clock()+duration);
    e.effect=integer(skg::field<void*>(value,0x18),0x72AEEDA0);
    consumed.insert(e.item); // claim before goal callbacks can reenter activation
    if(e.type==1) {
        void* combatant=game::cast(captain,0x013F94D4);
        if(!combatant || !C.captain_heal(e.player,skg::vcall<float>(combatant,0x58/4))) {
            consumed.erase(e.item);++rejected;return;
        }
    } else {
        auto& effect=effects[{e.player,e.type}];effect={e.revision,e.amount,e.until,true};effect.artwork=e.effect;apply_effect(e.player,e.type,effect);
    }
    if(e.type==1 && e.effect) skg::cdecl_call<void>(a_effect,captain,e.effect);
    send(e);++grants;
    collected(item,captain);
    consume(e.item,item);
    if(auto* p=play()) skg::thiscall<void>(a_goals,p);
}
bool apply(Event e) {
    if(!active()) return true;
    if(e.kind==Claim) {commit(e);return true;}
    if(e.kind==Expire) {
        auto& effect=effects[{e.player,e.type}];
        if(e.ref>=effect.revision) {
            bool was_active=effect.active;effect.revision=e.ref;effect.active=false;
            if(was_active) apply_effect(e.player,e.type,effect);
        }
        return true;
    }
    if(e.act>act()) return false;
    if(e.kind==Grant) {
        if(consumed.count(e.item)) return true;
        if(e.type!=1) {
            auto& effect=effects[{e.player,e.type}];
            if(e.revision>effect.revision) {
                effect={e.revision,e.amount,e.until,true};effect.artwork=e.effect;apply_effect(e.player,e.type,effect);
            }
        }
        if(e.type==1 && e.effect) if(void* captain=C.captain_of(e.player)) skg::cdecl_call<void>(a_effect,captain,e.effect);
        consume(e.item,C.object_for(e.item));++grants;return true;
    }
    if(e.act<act()) return true;
    if(e.kind==Teleport) {
        if(e.revision<teleports[e.player] || e.revision<=teleport_cleanup[e.player]) return true;
        if(e.revision>teleports[e.player]) {
            // Accept the motion fence before asking for a puppet. Otherwise
            // rejecting its new-generation UDP state prevents that puppet from
            // ever appearing and leaves this native cleanup waiting forever.
            teleports[e.player]=e.revision;cooldowns[e.player]=e.until;
            C.teleport(e.player,e.revision,e.position,e.orientation);
        }
        void* captain=C.captain_of(e.player);void* value=powerup(C.object_for(e.item));
        if(!captain || !value) return false;
        // Native cleanup is local to this captain. No posse enumeration.
        skg::cdecl_call<void>(a_teleport,value,captain,&e.position,&e.orientation,true);
        remember(e.player,captain);skg::field<bool>(captain,0xE60)=clock()<e.until;
        cooldowns[e.player]=e.until;teleport_cleanup[e.player]=e.revision;++warps;
    }
    return true;
}
void drain(void* token) {
    std::unique_ptr<uint64_t> task(static_cast<uint64_t*>(token));
    if(*task!=generation) return;
    auto work=std::move(pending);pending.clear();
    if(!active()) {queued=false;return;}
    bool failed_queue=overflow && overflow_run==C.run();
    if(current_run!=C.run()) {reset();current_run=C.run();}
    queued=false;
    if(failed_queue) {
        overflow=true;overflow_run=current_run;
        if(!overflow_reported) {
            overflow_reported=true;
            C.failed("Adventure pickup synchronization exceeded its bounded pending queue. Cancel and retry the adventure.");
        }
        return;
    }
    for(const auto& e:work) if(e.run==current_run && !apply(e) && pending.size()<128) pending.push_back(e);
    double now=clock();
    for(auto& [key,effect]:effects) if(host() && effect.active && now>effect.until) {
        effect.active=false;apply_effect(key.first,key.second,effect);
        if(host()) {Event e;e.kind=Expire;e.act=uint8_t(act());e.player=key.first;e.type=key.second;e.revision=++serial;e.ref=effect.revision;send(e);}
    }
    // Captains can be recreated after a model/resource or connection delay.
    // Materialize each current grant once per pointer/incarnation/revision,
    // including an expiry that arrived while its previous puppet was absent.
    for(auto& [key,effect]:effects) if(effect.active || effect.applied_active)
        apply_effect(key.first,key.second,effect);
    for(auto it=cooldowns.begin();it!=cooldowns.end();) {
        auto [player,until]=*it;
        if(now<until) {++it;continue;}
        void* captain=C.captain_of(player);auto old=originals_state.find(player);
        if(captain && old!=originals_state.end() && captain==old->second.captain && skg::readable(captain,0xE61) &&
           skg::field<uint32_t>(captain,0x24)==old->second.incarnation) skg::field<bool>(captain,0xE60)=false;
        it=cooldowns.erase(it);
    }
    if(host()) flush();
}
void hook(void* value,void* captain,int type) {
    if(!active()) {originals[type](value,captain);return;}
    uint8_t player=owner(captain);
    // Native helpers grant to the posse, so even a non-owner mirror must not
    // execute an independent full helper during an active co-op adventure.
    if(player==kNone || (!host()&&player!=C.sess->my_id())) return;
    void* item=value ? skg::vcall<void*>(value,1) : nullptr;
    activate(item,captain);
}
void SK_CDECL medkit(void* p,void* c) {hook(p,c,1);}
void SK_CDECL speed(void* p,void* c) {hook(p,c,3);}
void SK_CDECL damage(void* p,void* c) {hook(p,c,4);}
void SK_CDECL armor(void* p,void* c) {hook(p,c,5);}
void SK_CDECL teleporter(void* p,void* c) {hook(p,c,8);}
} // namespace

bool init(const SkHost* api,SkMod* mod,Ctx ctx) {
    if(ready) return true;
    constexpr const char* hash="dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37";
    if(!api||!api->sym||!api->hook_create||!api->exe_sha256||!api->run_outside_frame||
       !api->exe_sha256()||std::strcmp(api->exe_sha256(),hash)||!ctx.sess||!ctx.run||!ctx.act||!ctx.active||
       !ctx.simulation_time||!ctx.captain_of||!ctx.captain_can_act||!ctx.identity||!ctx.object_for||!ctx.retire||!ctx.captain_heal||!ctx.teleport||!ctx.failed) return false;
    H=api;C=std::move(ctx);
    a_mode=H->sym("App::cScenarioMode::ptr");
    a_float=H->sym("App::Property::GetFloat");a_speed=H->sym("SporeCoop::Adventure::SetSpeedModifier");
    a_effect=H->sym("SporeCoop::Adventure::PickupEffect");a_collect=H->sym("SporeCoop::Adventure::OnCollect");
    a_goals=H->sym("Simulator::cScenarioPlayMode::UpdateGoals");
    a_destination=H->sym("SporeCoop::Adventure::TeleportDestination");a_teleport=H->sym("SporeCoop::Adventure::TeleportCaptain");
    if(!a_mode||!a_float||!a_speed||!a_effect||!a_collect||!a_goals||!a_destination||!a_teleport) return false;
    const char* names[]={"SporeCoop::Adventure::ActivateMedKit","SporeCoop::Adventure::ActivateSpeedBoost",
        "SporeCoop::Adventure::ActivateDamageBoost","SporeCoop::Adventure::ActivateArmorBoost","SporeCoop::Adventure::ActivateTeleporter"};
    void* hooks[]={reinterpret_cast<void*>(medkit),reinterpret_cast<void*>(speed),reinterpret_cast<void*>(damage),reinterpret_cast<void*>(armor),reinterpret_cast<void*>(teleporter)};
    int types[]={1,3,4,5,8};
    for(int i=0;i<5;++i) if(!H->sym(names[i])) return false;
    for(int i=0;i<5;++i) if(H->hook_create(mod,reinterpret_cast<void*>(H->sym(names[i])),hooks[i],reinterpret_cast<void**>(&originals[types[i]]))) return false;
    ready=true;return true;
}
bool available() {return ready;}
bool activate(void* item,void* captain) {
    if(!active()) return false;
    void* value=powerup(item);int type=value?skg::field<int>(value,4):0;
    if(!supported(type)) return false;
    uint8_t player=owner(captain);Identity id;
    if(held()||player==kNone||(!host()&&player!=C.sess->my_id())||!C.identity(item,id)||consumed.count(id)) return true;
    if(current_run!=C.run()) {reset();current_run=C.run();}
    auto key=std::make_pair(player,id);double now=H->time_seconds();
    if(requested.count(key)&&now-requested[key]<0.3) return true;
    if(requested.size()>=1024) {
        // This is a short contact throttle, not a lifetime limit on authored
        // respawns. Old incarnations must not permanently exhaust the run.
        for(auto it=requested.begin();it!=requested.end();) {
            if(now-it->second>=0.3) it=requested.erase(it);else ++it;
        }
    }
    if(requested.size()>=1024 && !requested.count(key)) return true;
    requested[key]=now;
    Event e;e.kind=Claim;e.act=uint8_t(act());e.player=player;e.type=uint8_t(type);e.item=id;e.revision=++serial;e.run=current_run;
    if(host()) {if(pending.size()<128) pending.push_back(e);}
    else if(C.sess->pending_to(0)<64) {
        net::Writer w;w.u8(Claim);put64(w,current_run);put64(w,e.revision);w.u8(e.act);w.u8(e.player);w.u8(e.type);put_id(w,id);
        C.sess->send_reliable_to(0,net::MSG_ADVENTURE,w);
    }
    return true;
}
void tick() {
    if(!active()) return;
    if((held()&&!overflow)||queued) return;
    queued=true;H->run_outside_frame(drain,new uint64_t(generation));
}
void on_message(uint8_t from,net::Reader& r) {
    if(!active()) return;
    Event e;e.kind=r.u8();uint64_t run=get64(r);e.run=run;e.revision=get64(r);e.act=r.u8();e.player=r.u8();e.type=r.u8();e.item=get_id(r);
    if(!r.ok()||run!=C.run()||!e.revision||e.act>=8||e.player>=4||!supported(e.type)) return;
    if(e.kind==Claim) {
        if(!host()||!from||from!=e.player||(current_run==run&&e.revision<=claims[from])||r.left()) return;
        if(current_run==run) claims[from]=e.revision;
    } else {
        if(host()||from!=0) return;
        if(e.kind==Grant) {e.amount=r.f32();e.until=r.f32();e.effect=r.u32();if(e.type==8||!std::isfinite(e.amount)||e.amount<0||!std::isfinite(e.until)||(e.type==5&&e.amount==0)) return;}
        else if(e.kind==Expire) {e.ref=get64(r);if(e.type!=3&&e.type!=4&&e.type!=5) return;}
        else if(e.kind==Teleport) {
            e.position={r.f32(),r.f32(),r.f32()};e.orientation={r.f32(),r.f32(),r.f32(),r.f32()};e.until=r.f32();
            if(e.type!=8||!finite(e.position)||!finite(e.orientation)||!std::isfinite(e.until)) return;
        } else return;
        if(!r.ok()||r.left()) return;
    }
    if(overflow && overflow_run==run) return;
    const size_t limit=host()?128:1024;
    if(pending.size()<limit) pending.push_back(e);
    else if(host()) ++rejected; // a request was never accepted or mutated
    else {overflow=true;overflow_run=run;} // report from the outside-frame drain
}
float adjust_damage(uint8_t recipient,float amount) {
    auto i=effects.find({recipient,5});
    if(active()&&host()&&amount>0&&i!=effects.end()&&i->second.active&&clock()<=i->second.until&&i->second.amount>0) return amount/i->second.amount;
    return amount;
}
void reset() {
    ++generation;queued=false;
    for(const auto& [player,old]:originals_state) {
        void* captain=C.captain_of?C.captain_of(player):nullptr;
        if(!captain||captain!=old.captain||!skg::readable(captain,0xFA8)||skg::field<uint32_t>(captain,0x24)!=old.incarnation) continue;
        if(effects.count({player,3})) skg::thiscall<void>(a_speed,captain,7,old.speed);
        if(effects.count({player,4})) {skg::field<bool>(captain,0xB10)=old.damage;skg::field<float>(captain,0xB14)=old.damage_amount;}
        if(effects.count({player,5})) {skg::field<bool>(captain,0xB18)=old.armor;skg::field<float>(captain,0xB1C)=old.armor_amount;}
        if(cooldowns.count(player)) skg::field<bool>(captain,0xE60)=old.cooldown;
    }
    effects.clear();originals_state.clear();teleports.clear();teleport_cleanup.clear();cooldowns.clear();claims.clear();requested.clear();consumed.clear();pending.clear();
    outgoing.clear();current_run=serial=0;grants=warps=rejected=0;
    overflow=overflow_reported=false;overflow_run=0;
}
std::string status() {
    std::ostringstream s;s<<"pickups "<<(ready?"ready":"unavailable")<<" grants="<<grants<<" teleports="<<warps<<" rejected="<<rejected<<" pending="<<pending.size()<<" overflow="<<overflow;
    for(const auto& [key,e]:effects) if(e.active) s<<" player="<<unsigned(key.first)<<" type="<<unsigned(key.second)<<" amount="<<e.amount<<" remaining="<<std::max(0.,e.until-clock());
    return s.str();
}
} // namespace adventure_pickups
