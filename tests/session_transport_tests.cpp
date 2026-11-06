#include "net/session.h"
#include <chrono>
#include <cstdio>
#include <cstdlib>
#include <thread>
#include <string>
#include <vector>
#include <source_location>
#include <source_location>
unsigned checks;
#define CHECK(x) do { ++checks; if (!(x)) { std::printf("FAIL %d: %s\n",__LINE__,#x); std::exit(1); } } while(0)
template<class F> void until(net::Session& host,net::Session& guest,F done) {
    const auto began=net::now_ms();
    while(!done() && uint32_t(net::now_ms()-began)<5000) {host.update();guest.update();std::this_thread::sleep_for(std::chrono::milliseconds(1));}
    CHECK(done());
}

template<class F> void receive_until(net::Session& receiver,F done,std::source_location caller=std::source_location::current()) {
    const auto began=net::now_ms();
    while(!done() && uint32_t(net::now_ms()-began)<5000) {
        receiver.update();std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    if(!done())std::printf("receive wait expired: caller=%u checks=%u role=%u pending=%zu\n",caller.line(),checks,unsigned(receiver.role()),receiver.pending_to(0));
    if(!done())std::printf("receive wait expired: caller=%u checks=%u role=%u pending=%zu\n",caller.line(),checks,unsigned(receiver.role()),receiver.pending_to(0));
    CHECK(done());
}
net::Writer numbered(uint32_t value) {
    net::Writer w;w.u32(value);
    // Two such messages cannot fit one1200-byte datagram. Counts below
    // therefore witness actual packet budgets rather than message batching.
    for(unsigned i=4;i<600;++i)w.u8(uint8_t(value));
    return w;
}

void send_only_flush_is_scoped() {
    net::Session host,guest,offline;std::string error;
    CHECK(!offline.flush_pending(offline.transport_generation()));
    CHECK(host.host(0,"flush host",error));
    CHECK(guest.join("127.0.0.1:"+std::to_string(host.port()),"flush guest",error));
    until(host,guest,[&]{return guest.connected()&&host.players().size()==2;});
    unsigned host_messages=0,guest_messages=0,departures=0;
    host.cb.custom=[&](uint8_t type,uint8_t from,net::Reader& r){CHECK(type==99&&from==1&&r.u32()==11);++host_messages;};
    guest.cb.custom=[&](uint8_t type,uint8_t from,net::Reader& r){CHECK(type==99&&from==0&&r.u32()==22);++guest_messages;};
    host.cb.player_leave=[&](uint8_t,uint8_t){++departures;};
    const auto current=host.transport_generation();
    CHECK(guest.send_reliable_to(0,99,numbered(11)));
    CHECK(guest.flush_pending(guest.transport_generation()));
    std::this_thread::sleep_for(std::chrono::milliseconds(5));
    CHECK(host.send_reliable_to(1,99,numbered(22)));
    // Inbound data and an expired timeout are deliberately pending. A flush
    // must send our message without dispatching callbacks or dropping peers.
    host.timeout_ms=1;
    CHECK(host.flush_pending(current));
    CHECK(host_messages==0&&guest_messages==0&&departures==0&&host.players().size()==2);
    host.timeout_ms=30000;
    receive_until(guest,[&]{return guest_messages==1;});
    CHECK(host_messages==0&&host.transport_generation()==current);
    host.update();CHECK(host_messages==1&&departures==0);
    guest.leave();host.leave();
    CHECK(!host.flush_pending(current)&&!host.flush_pending(host.transport_generation()));
    CHECK(host.host(0,"replacement",error));
    CHECK(guest.join("127.0.0.1:"+std::to_string(host.port()),"new guest",error));
    until(host,guest,[&]{return guest.connected()&&host.players().size()==2;});
    guest_messages=0;
    CHECK(host.send_reliable_to(1,99,numbered(22)));
    const auto retained=host.retained_accounting();
    CHECK(!host.flush_pending(current));
    CHECK(host.retained_accounting()==retained&&host.pending_to(1)>=1);
    guest.update();CHECK(guest_messages==0);
    CHECK(host.flush_pending(host.transport_generation()));
    receive_until(guest,[&]{return guest_messages==1;});
    CHECK(host.transport_generation()!=current&&host_messages==1);
    guest.leave();host.leave();
}

// Real UDP relay: records outgoing datagrams and can drop one reliable
// payload. It neither inspects Session internals nor drives its sender.
struct FlushRelay {
    net::UdpSocket front,back;
    net::Endpoint host,guest;
    unsigned downstream=0,reliable=0,unreliable=0;
    bool drop_one=false,dropped=false;
    explicit FlushRelay(uint16_t port) {
        CHECK(front.open(0)&&back.open(0));
        CHECK(net::Endpoint::parse("127.0.0.1",port,host));
    }
    void pump() {
        uint8_t buf[2048];net::Endpoint from;
        for(int n;(n=front.recv_from(from,buf,sizeof buf))>0;) {
            guest=from;CHECK(back.send_to(host,buf,size_t(n)));
        }
        for(int n;(n=back.recv_from(from,buf,sizeof buf))>0;) {
            CHECK(from==host&&net::Connection::looks_valid(buf,size_t(n)));
            ++downstream;net::Reader r(buf,size_t(n));r.skip(30);const auto count=r.u8();
            bool has_reliable=false;
            for(unsigned i=0;i<count;++i) {
                const auto type=r.u8(),flags=r.u8();const auto length=r.u16();
                if(flags&1)r.u16();
                if(type==99){++reliable;has_reliable=true;}
                if(type==100)++unreliable;
                r.skip(length);
            }
            CHECK(r.ok()&&r.left()==0);
            if(drop_one&&!dropped&&has_reliable){dropped=true;continue;}
            CHECK(guest.port&&front.send_to(guest,buf,size_t(n)));
        }
    }
};
template<class F> void relay_until(net::Session& host,net::Session& guest,FlushRelay& relay,F done) {
    const auto began=net::now_ms();
    while(!done() && uint32_t(net::now_ms()-began)<5000) {
        relay.pump();host.update();relay.pump();guest.update();relay.pump();
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    CHECK(done());
}

void flush_retry_uses_fresh_time() {
    net::Session host,guest;std::string error;CHECK(host.host(0,"retry host",error));
    FlushRelay relay(host.port());
    CHECK(guest.join("127.0.0.1:"+std::to_string(relay.front.local_port()),"retry guest",error));
    relay_until(host,guest,relay,[&]{return guest.connected()&&host.pending_to(1)==0&&guest.pending_to(0)==0;});
    std::vector<uint32_t> delivered;
    guest.cb.custom=[&](uint8_t type,uint8_t from,net::Reader& r){CHECK(type==99&&from==0);delivered.push_back(r.u32());};
    relay.drop_one=true;
    CHECK(host.send_reliable_to(1,99,numbered(1)));
    CHECK(host.flush_pending(host.transport_generation()));relay.pump();
    CHECK(relay.dropped);
    CHECK(host.send_reliable_to(1,99,numbered(2)));
    CHECK(host.flush_pending(host.transport_generation()));relay.pump();guest.update();
    CHECK(delivered.empty()&&host.pending_to(1)==2);
    // No sender update occurs between first send and retry. A cached update
    // timestamp would leave missing1 undelivered and2 buffered indefinitely.
    const auto before=relay.reliable;
    std::this_thread::sleep_for(std::chrono::milliseconds(250));
    CHECK(host.flush_pending(host.transport_generation()));relay.pump();
    // Upstream bulk_acks waits for acknowledged newer traffic or at least
    // one second of silence. A missing ACK must not trigger the old 250ms retry.
    CHECK(relay.reliable==before&&delivered.empty());
    std::this_thread::sleep_for(std::chrono::milliseconds(800));
    CHECK(host.flush_pending(host.transport_generation()));relay.pump();
    receive_until(guest,[&]{return delivered.size()==2;});
    CHECK(relay.reliable>before&&delivered==std::vector<uint32_t>({1,2}));
    relay_until(host,guest,relay,[&]{return host.pending_to(1)==0;});
    CHECK(delivered==std::vector<uint32_t>({1,2}));
    guest.leave();host.leave();
}

void flush_packet_allowance_is_shared() {
    net::Session host,guest;std::string error;CHECK(host.host(0,"budget host",error));
    FlushRelay relay(host.port());
    CHECK(guest.join("127.0.0.1:"+std::to_string(relay.front.local_port()),"budget guest",error));
    relay_until(host,guest,relay,[&]{return guest.connected()&&host.pending_to(1)==0&&guest.pending_to(0)==0;});
    // Initial public RTT starts in the conservative internet-sized allowance.
    CHECK(host.rtt_of(1)>=40);
    auto exercise=[&](unsigned burst,unsigned total) {
        std::vector<uint32_t> delivered;unsigned transient=0;
        guest.cb.custom=[&](uint8_t type,uint8_t from,net::Reader& r){
            CHECK(from==0);
            if(type==99)delivered.push_back(r.u32());else{CHECK(type==100);++transient;}
        };
        const auto packets=relay.downstream,rel=relay.reliable,unrel=relay.unreliable;
        for(unsigned i=0;i<10;++i)CHECK(host.send_reliable_to(1,99,numbered(i)));
        host.update();relay.pump();CHECK(relay.downstream-packets==10&&relay.reliable-rel==10);
        for(unsigned i=10;i<total;++i)CHECK(host.send_reliable_to(1,99,numbered(i)));
        CHECK(host.flush_pending(host.transport_generation()));relay.pump();
        CHECK(relay.downstream-packets==burst&&relay.reliable-rel==burst);
        CHECK(host.pending_to(1)==total);
        for(unsigned i=0;i<4;++i){CHECK(host.flush_pending(host.transport_generation()));relay.pump();}
        // Even due retransmissions cannot start another allowance without
        // update. Unsent reliable data remains queued for the next update.
        std::this_thread::sleep_for(std::chrono::milliseconds(1050));
        CHECK(host.flush_pending(host.transport_generation()));relay.pump();
        CHECK(relay.downstream-packets==burst&&host.pending_to(1)==total);
        for(unsigned i=0;i<80;++i)CHECK(host.send_unreliable_to(1,100,numbered(i)));
        CHECK(host.flush_pending(host.transport_generation()));relay.pump();
        CHECK(relay.downstream-packets==burst+64&&relay.unreliable-unrel==64&&relay.reliable-rel==burst);
        CHECK(host.retained_accounting().unreliable_out==0);
        // Requeuing unreliable traffic cannot replenish the extra64 budget.
        CHECK(host.send_unreliable_to(1,100,numbered(999)));
        CHECK(host.flush_pending(host.transport_generation()));relay.pump();
        CHECK(relay.downstream-packets==burst+64&&host.retained_accounting().unreliable_out==0);
        CHECK(delivered.empty()&&transient==0);
        receive_until(guest,[&]{return delivered.size()==burst&&transient==64;});
        for(unsigned i=0;i<burst;++i)CHECK(delivered[i]==i);
        relay_until(host,guest,relay,[&]{return delivered.size()==total&&host.pending_to(1)==0;});
        CHECK(delivered.size()==total&&transient==64);
        for(unsigned i=0;i<total;++i)CHECK(delivered[i]==i);
        guest.cb.custom={};
    };
    exercise(net::kAckWindow/3,160);
    // The acknowledged bulk trains the real loopback connection's RTT into
    // the LAN branch; no test-only RTT setter or private peer access is used.
    // The deliberate silent interval contributes high RTT samples with upstream
    // bulk ACKs. Train the LAN branch using fresh real traffic, never an RTT setter.
    relay_until(host,guest,relay,[&]{
        if(host.rtt_of(1)>=0&&host.rtt_of(1)<40)return true;
        CHECK(host.send_unreliable_to(1,100,numbered(9999)));return false;
    });
    CHECK(host.rtt_of(1)>=0&&host.rtt_of(1)<40);
    exercise(net::kAckWindow,240);
    guest.leave();host.leave();
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
    // An invalid port fails synchronously even with upstream's async DNS path.
    CHECK(!guest.join("127.0.0.1:70000","invalid",error));
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
    CHECK(!host.join("127.0.0.1:70000","failed join",error));CHECK(host.transport_generation()==active+1);
    CHECK(host.host(0,"Mismatch",error));host.build=23;guest.build=24;disconnected=false;
    CHECK(guest.join("127.0.0.1:"+std::to_string(host.port()),"wrong build",error));
    const auto rejected=guest.transport_generation();
    guest.cb.disconnected=[&](const std::string&){CHECK(guest.role()==net::Role::None&&guest.transport_generation()==rejected+1);disconnected=true;};
    until(host,guest,[&]{return disconnected;});
    CHECK(!guest.connected()&&guest.transport_generation()==rejected+1);
    host.leave();blocker.leave();
    callback_restarts_are_scoped();
    send_only_flush_is_scoped();
    flush_retry_uses_fresh_time();
    flush_packet_allowance_is_shared();
    std::printf("session transport generation: %u checks passed\n",checks);
}
