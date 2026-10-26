#include "net/session.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <string>
unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { std::printf("FAIL %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
template<class F> void until(net::Session& host,net::Session& guest,F done) {
    const auto began=net::now_ms();
    while(!done() && uint32_t(net::now_ms()-began)<5000) {host.update();guest.update();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    CHECK(done());
}

void callback_restarts_are_scoped() {
    net::Session first,next,guest;std::string error;CHECK(first.host(0,"first",error));CHECK(next.host(0,"next",error));
    const auto target="127.0.0.1:"+std::to_string(next.port());
    CHECK(guest.join("127.0.0.1:"+std::to_string(first.port()),"guest",error));until(first,guest,[&]{return guest.connected();});
    unsigned stale=0;guest.cb.custom=[&](uint8_t,uint8_t,net::Reader&){++stale;};
    const auto old=guest.transport_generation();bool restarted=false;
    guest.cb.disconnected=[&](const std::string&){CHECK(guest.role()==net::Role::None&&guest.transport_generation()==old+1);
        std::this_thread::sleep_for(std::chrono::milliseconds(3));CHECK(guest.join(target,"callback rejoin",error));restarted=true;};
    net::Writer bye;bye.u8(0);CHECK(first.send_reliable_to(1,net::MSG_BYE,bye));
    net::Writer late;late.u8(42);CHECK(first.send_reliable_to(1,99,late));
    until(first,guest,[&]{return restarted;});CHECK(guest.role()==net::Role::Client&&guest.transport_generation()==old+2&&stale==0);
    until(next,guest,[&]{return guest.connected();});CHECK(stale==0);
    guest.cb.disconnected={};guest.leave();first.leave();next.leave();
    // Reject can restart the same Session too, without the old receive pass
    // evaluating a freshly set join_started against its earlier timestamp.
    first.build=11;guest.build=12;CHECK(first.host(0,"wrong build",error));CHECK(next.host(0,"matching",error));next.build=12;
    const auto valid="127.0.0.1:"+std::to_string(next.port());restarted=false;
    guest.cb.disconnected=[&](const std::string&){std::this_thread::sleep_for(std::chrono::milliseconds(3));CHECK(guest.join(valid,"after reject",error));restarted=true;};
    CHECK(guest.join("127.0.0.1:"+std::to_string(first.port()),"reject me",error));until(first,guest,[&]{return restarted;});
    CHECK(guest.role()==net::Role::Client);until(next,guest,[&]{return guest.connected();});
    guest.cb.disconnected={};guest.leave();first.leave();next.leave();
    // Host player_leave callbacks may likewise change its role mid-drop loop.
    CHECK(first.host(0,"old host",error));CHECK(next.host(0,"new host",error));first.build=next.build=guest.build=0;
    CHECK(guest.join("127.0.0.1:"+std::to_string(first.port()),"leaving",error));until(first,guest,[&]{return guest.connected();});
    const auto next_target="127.0.0.1:"+std::to_string(next.port());restarted=false;
    first.cb.player_leave=[&](uint8_t,uint8_t){std::this_thread::sleep_for(std::chrono::milliseconds(3));CHECK(first.join(next_target,"former host",error));restarted=true;};
    guest.leave();until(first,guest,[&]{return restarted;});CHECK(first.role()==net::Role::Client);
    until(next,first,[&]{return first.connected();});CHECK(first.players().size()==2);
    first.cb.player_leave={};first.leave();next.leave();
}
int main() {
    net::Session host,guest,blocker;std::string error;
    CHECK(host.transport_generation()==0&&guest.transport_generation()==0);
    host.leave();CHECK(host.transport_generation()==0);
    CHECK(!guest.join("999.999.999.999:1","invalid",error));
    CHECK(guest.transport_generation()==0&&guest.role()==net::Role::None);
    CHECK(blocker.host(0,"occupied",error));
    CHECK(!host.host(blocker.port(),"cannot bind",error));
    CHECK(host.transport_generation()==0&&host.role()==net::Role::None);
    CHECK(host.host(0,"Host",error));const auto h1=host.transport_generation();CHECK(h1==1);
    CHECK(guest.join("127.0.0.1:"+std::to_string(host.port()),"Guest",error));
    const auto g1=guest.transport_generation();CHECK(g1==1&&!guest.connected());
    until(host,guest,[&]{return guest.connected()&&host.players().size()==2;});
    CHECK(host.transport_generation()==h1&&guest.transport_generation()==g1);
    guest.leave();CHECK(guest.transport_generation()==g1+1&&guest.role()==net::Role::None);
    guest.leave();CHECK(guest.transport_generation()==g1+1);
    until(host,guest,[&]{return host.players().size()==1;});
    CHECK(host.transport_generation()==h1);
    CHECK(guest.join("127.0.0.1:"+std::to_string(host.port()),"Guest again",error));
    CHECK(guest.transport_generation()==g1+2);
    until(host,guest,[&]{return guest.connected();});
    bool disconnected=false;const auto old=guest.transport_generation();
    guest.cb.disconnected=[&](const std::string&){CHECK(guest.role()==net::Role::None);CHECK(guest.transport_generation()==old+1);disconnected=true;};
    host.leave();CHECK(host.transport_generation()==h1+1);
    until(host,guest,[&]{return disconnected;});
    CHECK(guest.transport_generation()==old+1);
    CHECK(host.host(0,"Host restart",error));const auto active=host.transport_generation();
    CHECK(!host.host(blocker.port(),"failed restart",error));
    CHECK(host.role()==net::Role::None&&host.transport_generation()==active+1);
    CHECK(!host.join("999.999.999.999:1","failed join",error));CHECK(host.transport_generation()==active+1);
    CHECK(host.host(0,"Mismatch",error));host.build=23;guest.build=24;disconnected=false;
    CHECK(guest.join("127.0.0.1:"+std::to_string(host.port()),"wrong build",error));
    const auto rejected=guest.transport_generation();
    guest.cb.disconnected=[&](const std::string&){CHECK(guest.role()==net::Role::None&&guest.transport_generation()==rejected+1);disconnected=true;};
    until(host,guest,[&]{return disconnected;});
    CHECK(!guest.connected()&&guest.transport_generation()==rejected+1);
    host.leave();blocker.leave();
    callback_restarts_are_scoped();
    std::printf("session transport generation: %u checks passed\n",checks);
}
