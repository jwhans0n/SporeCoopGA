// Passive accounting exercises actual coordinator/transport containers only.
// No game, native call, profile, disk store or process-memory threshold is used.
#include "editor_session.h"
#include "../updater/sha256.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <type_traits>
using namespace editor;
namespace es=editor_session; namespace w=editor::wire;
unsigned checks;
#define CHECK(...) do { ++checks; if (!(__VA_ARGS__)) { std::printf("FAIL %d: %s\n",__LINE__,#__VA_ARGS__);std::exit(1); } } while(0)
State seed() {
    State s;auto& d=s.document;d.project[0]=1;d.epoch=7;d.next_id=5;d.classes.insert({1,1});d.acts.insert({2,1});
    Placement p;p.object={3,1};p.class_id={1,1};p.act_id={2,1};d.placements[p.object]=p;return s;
}
int main() {
    static_assert(std::is_trivially_copyable_v<es::Accounting>);
    net::Connection connection; std::vector<uint8_t> packet;
    for (unsigned n = 1; n <= 1100; ++n) CHECK(connection.build_packet(n * 250, packet));
    const auto retained = connection.retained_accounting(); const auto sent = connection.stats.packets_sent;
    CHECK(retained.sent_packets == 1024 && retained.reliable_out == 0 && retained.reliable_in == 0);
    CHECK(connection.retained_accounting() == retained && connection.stats.packets_sent == sent);
    net::Writer payload; payload.u32(7); connection.send_reliable(99, payload);
    CHECK(connection.retained_accounting().reliable_out == 1 && connection.retained_accounting().payload_size == 4);
    net::Session session;std::string error;CHECK(session.host(0,"accounting",error));
    es::Callbacks callbacks;unsigned persists=0;
    callbacks.host_hooks.apply=[](const Document&,const Document&,const Change&,std::string&){return true;};
    callbacks.host_hooks.persist=[&](const State&,const Change&,std::string&){++persists;return true;};
    es::Controller c(session,callbacks);CHECK(c.start(seed(),{1,2,3},"Bounded accounting",error));
    auto a=c.accounting();CHECK(a.history==0&&a.decisions==0&&a.hashes==1&&a.resource_size==3&&a.baseline_size==0);
    Time time=10;CHECK(c.request_leases({{ScopeKind::Document,{},{}}},15000,error));c.tick(time);
    CHECK(c.view().local_leases.size()==1);
    for(unsigned n=1;n<=1030;++n) {
        Request r;r.base_revision=c.state().document.revision;r.summary="Measured logical Move";
        for(const auto& lease:c.view().local_leases)r.leases.push_back({lease.scope,lease.token});
        Mutation m;m.target={3,1};m.field={Field::Position,0};m.expected=field_revision(c.state().document.placements.at(m.target),m.field);m.value=Vec3{float(n),0,0};r.mutations={m};
        CHECK(c.submit(std::move(r),error));c.tick(++time);CHECK(c.state().document.revision==n);
        if(n==130||n==300||n==1030) {
            const auto before=c.state();const auto persisted=persists;
            for(unsigned repeat=0;repeat<8;++repeat) {
                a=c.accounting();CHECK(c.state()==before&&persists==persisted);
                CHECK(a.history==128&&a.coordinator_history==128&&a.replica_history==0);
                CHECK(a.decisions==std::min<size_t>(n,1024)&&a.decision_order==a.decisions);
                CHECK(a.hashes==std::min<size_t>(n+1,272)&&a.state_estimate==editor::bytes(c.state()));
                CHECK(a.input_count==0&&a.local_count==0&&a.queued_packets==0&&a.output_bytes==0&&a.deferred_count==0);
                CHECK(a.resource_size==3&&a.resource_capacity>=3&&a.staged_size==0);
            }
            std::printf("commit=%u history=%zu decisions=%zu hashes=%zu estimated_state=%zu\n",n,a.history,a.decisions,a.hashes,a.state_estimate);
        }
    }
    CHECK(persists==1030);CHECK(c.accounting().retired_sequences==1);
    // The 64-entry reply cache is a different limit than semantic decisions.
    for(unsigned n=0;n<100;++n) {
        std::vector<LeaseToken> tokens;for(const auto& lease:c.view().local_leases)tokens.push_back({lease.scope,lease.token});
        CHECK(c.renew_leases(tokens,15000,error));c.tick(++time);
    }
    CHECK(c.accounting().lease_decisions==64);CHECK(c.accounting().lease_reply_capacity>0);
    c.end();a=c.accounting();
    // End retires visible authority and transport/cache queues, while retaining
    // its bounded Coordinator state until replacement/destruction. Report it.
    CHECK(c.view().phase==es::Phase::Idle&&a.history==0&&a.decisions==0);
    CHECK(a.coordinator_history==128&&a.coordinator_decisions==1024&&a.coordinator_estimate>0);
    CHECK(a.hashes==0&&a.lease_decisions==0&&a.channels==0&&a.resource_capacity==0);
    CHECK(c.start(seed(),{9},"Replacement",error));a=c.accounting();CHECK(a.coordinator_decisions==0&&a.coordinator_history==0&&a.hashes==1);
    w::Receiver receiver;w::Context context{};context.project[0]=1;context.epoch=7;
    std::vector<uint8_t> bytes(1u<<20,0xA7);
    CHECK(receiver.begin(context,{w::TransferKind::Resources,1,uint32_t(bytes.size()),upd::sha256_hex(bytes)},{0,1},true,error));
    CHECK(receiver.retained_data_size()==bytes.size()&&receiver.retained_data_capacity()>=bytes.size());
    const auto capacity=receiver.retained_data_capacity();receiver.reset();
    CHECK(!receiver.active()&&receiver.retained_data_size()==0&&receiver.retained_data_capacity()==capacity);
    CHECK(receiver.retained_chunk_capacity()>0);
    // Reset's retained high-water capacity is observable and no invented heap
    // reclaim is implied. Destroying its owning channel frees this container.
    c.end();session.leave();std::printf("editor passive accounting: %u checks passed\n",checks);
}
