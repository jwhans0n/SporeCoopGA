// Real Session UDP loopback, deliberately fake native adapters. No game/profile.
#include "editor_session.h"
#include "../updater/sha256.h"

#include <algorithm>
#include <chrono>
#include <cstdio>
#include <thread>

using namespace editor;
namespace es = editor_session;
namespace w = editor::wire;
static int checks, failures;
#define CHECK(x) do { ++checks; if (!(x)) { std::printf("FAIL %s:%d: %s\n", __FILE__, __LINE__, #x); ++failures; } } while (0)

static constexpr ObjectId object_a{3, 1}, object_b{4, 1};
static constexpr FieldKey position{Field::Position, 0}, scale{Field::Scale, 0};
static State seed()
{
    State s; auto& d = s.document; d.project[0] = 9; d.project[15] = 7; d.epoch = 0xABCD; d.next_id = 5;
    d.classes.insert({1, 1}); d.acts.insert({2, 1});
    for (auto id : {object_a, object_b}) { Placement p; p.object = id; p.class_id = {1, 1}; p.act_id = {2, 1}; p.position.x = float(id.id); d.placements[id] = p; }
    d.schema[100] = {ValueKind::Text, 0, 0, 1024, true, false}; return s;
}
static std::vector<uint8_t> resource(Revision revision, size_t count = 12000)
{
    std::vector<uint8_t> bytes(count, 0xA7); for (unsigned i = 0; i < 8; ++i) bytes[i] = uint8_t(revision >> (8 * i)); return bytes;
}
static net::Writer addressed(Peer source, Peer target, const net::Writer& packet)
{
    net::Writer result; result.u8(source.slot); result.u32(source.generation); result.u32(target.generation);
    result.raw(packet.data.data(), packet.size()); return result;
}

struct FakeNative {
    Document document;
    Revision staged = 0;
    bool ready = true, authoring = true, controls_ready = true, fail_import = false, fail_apply = false, fail_persist = false;
    int imports = 0, baselines = 0, commits = 0, persists = 0, controls = 0, decisions = 0, errors = 0;
    int presence = 0, saved = 0, wire_decisions = 0;
    std::vector<Revision> imported;
    std::vector<std::vector<uint8_t>> imported_payloads;
    std::vector<size_t> own_decision_leases;
};

// A real UDP forwarder; loss/duplication/delay occurs before Session reliability.
struct UdpAdversary {
    net::UdpSocket socket;
    net::Endpoint host, client;
    struct Delayed { net::Endpoint to; std::vector<uint8_t> bytes; uint32_t due; };
    std::deque<Delayed> delayed;
    unsigned packets = 0, dropped = 0, duplicated = 0, reordered = 0, delayed_count = 0;
    explicit UdpAdversary(uint16_t port) { CHECK(socket.open(0)); CHECK(net::Endpoint::parse("127.0.0.1", port, host)); }
    void pump() {
        const auto now = net::now_ms();
        while (!delayed.empty() && int32_t(now - delayed.front().due) >= 0) {
            auto packet = std::move(delayed.front()); delayed.pop_front(); CHECK(socket.send_to(packet.to, packet.bytes.data(), packet.bytes.size()));
        }
        std::array<uint8_t, 2048> bytes{};
        for (int budget = 512; budget; --budget) {
            net::Endpoint from; const auto n = socket.recv_from(from, bytes.data(), bytes.size()); CHECK(n >= 0); if (n <= 0) break;
            const bool from_host = from == host; if (!from_host) client = from;
            const auto to = from_host ? client : host; CHECK(to.port != 0); ++packets;
            if (packets % 11 == 0) { ++dropped; continue; }
            if (packets % 7 == 0 && delayed.size() < 256) { delayed.push_back({to, {bytes.begin(), bytes.begin() + n}, now + 6}); ++delayed_count; continue; }
            if (!delayed.empty()) ++reordered;
            CHECK(socket.send_to(to, bytes.data(), size_t(n)));
            if (packets % 13 == 0) { CHECK(socket.send_to(to, bytes.data(), size_t(n))); ++duplicated; }
        }
    }
};

struct Party {
    std::array<net::Session, 4> sessions;
    std::array<std::unique_ptr<es::Controller>, 4> controllers;
    std::array<FakeNative, 4> native;
    std::array<std::unique_ptr<UdpAdversary>, 4> adversaries;
    std::array<Time, 4> origins{1000000, 4000, 900000000, 700000};
    std::array<bool, 4> tick_enabled{true, true, true, true}, hold_lease_replies{};
    std::array<std::vector<std::pair<w::LeaseReply,std::vector<uint8_t>>>,4> held_lease_replies;
    std::array<std::vector<w::LeaseRequest>,4> wire_lease_requests;
    std::array<std::vector<w::LeaseReply>,4> wire_lease_replies;
    bool dispatching = false, ticking = false, release_on_decision = false, publish_deltas = false, auto_ack_saved = true;
    size_t structural_bytes = 12000;
    std::function<void(unsigned)> after_own_decision;
    std::array<w::Join,4> join_claims;
    bool fail_join_prepare=false,fail_join_resources=false,small_join_resources=false;
    std::vector<w::Join> received_joins;
    std::vector<std::vector<uint8_t>> pinned_join_resources;
    unsigned count;
    uint32_t begun = net::now_ms();
    explicit Party(unsigned players, es::Config config = {}, bool adverse = false) : count(players)
    {
        std::string error;
        for (unsigned p = 0; p < 4; ++p) {
            sessions[p].build = 0xDC04AEE5;
            auto& f = native[p]; f.document = seed().document;
            es::Callbacks cb;
            cb.prepare_join=[&,p](w::Join& join,std::string& error){CHECK(!dispatching);if(fail_join_prepare){error="Injected inventory read failure";return false;}join=join_claims[p];return true;};
            cb.resources_for_join=[&](const std::vector<uint8_t>& full,const w::Join& join,std::vector<uint8_t>& output,std::string& error){
                CHECK(ticking&&!dispatching);received_joins.push_back(join);pinned_join_resources.push_back(full);
                if(fail_join_resources){error="Injected cache negotiation failure";return false;}output=full;if(small_join_resources)output.resize(96);return true;
            };
            cb.host_hooks.apply = [&, p](const Document& before, const Document& after, const Change&, std::string&) { CHECK(ticking && !dispatching && native[p].document == before); native[p].document = after; return true; };
            cb.host_hooks.rollback = [&, p](const Document& before, const Document&, const Change&, std::string&) { native[p].document = before; return true; };
            cb.host_hooks.persist = [&, p](const State&, const Change& change, std::string& error) {
                CHECK(ticking && !dispatching); native[p].persists++;
                if (!change.patch.objects.empty() && !controllers[p]->update_resources(resource(change.revision, structural_bytes), change.revision, error, true,
                    publish_deltas ? resource(change.revision, 96) : std::vector<uint8_t>{})) return false;
                if (native[p].fail_persist) { error = "Injected host persistence failure after resource staging"; return false; }
                return true;
            };
            cb.apply_resources = [&, p](const std::vector<uint8_t>& bytes, const w::Context& context, std::string& error) {
                CHECK(ticking && !dispatching && !bytes.empty()); auto& f = native[p]; f.imports++; f.imported.push_back(context.revision); f.imported_payloads.push_back(bytes);
                if (f.fail_import) { error = "Injected native import failure"; return false; } f.staged = context.revision; return true;
            };
            cb.baseline_ready = [&, p](const State&) { CHECK(ticking && !dispatching); return native[p].ready; };
            cb.authoring_ready = [&, p] { CHECK(ticking && !dispatching); return native[p].authoring; };
            cb.apply_baseline = [&, p](const State& s, std::string&) { CHECK(ticking && !dispatching && native[p].staged == s.document.revision); native[p].document = s.document; native[p].baselines++; return true; };
            cb.apply_committed = [&, p](const Document& before, const Document& after, const Change& change, std::string& error) {
                CHECK(ticking && !dispatching && native[p].document == before);
                if (!change.patch.objects.empty()) CHECK(native[p].staged == change.revision);
                if (native[p].fail_apply) { error = "Injected native application failure"; return false; }
                native[p].document = after; native[p].commits++; return true;
            };
            cb.rollback_committed = [&, p](const Document& before, const Document&, const Change&, std::string&) { native[p].document = before; return true; };
            cb.persist_guest = [&, p](const State& s, const Change&, std::string& error) {
                CHECK(ticking && !dispatching && native[p].document == s.document); native[p].persists++;
                if (native[p].fail_persist) { error = "Injected guest persistence failure"; return false; } return true;
            };
            cb.control = [&, p](const w::Control&, std::string&) { CHECK(ticking && !dispatching); native[p].controls++; return true; };
            cb.control_ready = [&, p](const w::Control&) { CHECK(ticking && !dispatching); return native[p].controls_ready; };
            cb.decision = [&, p](const Decision& decision) {
                CHECK(ticking && !dispatching); native[p].decisions++;
                if (decision.accepted() && decision.change->operation.author == controllers[p]->view().local) {
                    const auto& leases = controllers[p]->view().local_leases; native[p].own_decision_leases.push_back(leases.size());
                    if (after_own_decision) after_own_decision(p);
                    if (release_on_decision && !leases.empty()) {
                        std::vector<LeaseToken> tokens; for (const auto& lease : leases) tokens.push_back({lease.scope, lease.token});
                        std::string error; CHECK(controllers[p]->release_leases(tokens, error));
                    }
                }
            };
            cb.presence = [&, p](Peer, const w::Presence&) { CHECK(ticking && !dispatching); native[p].presence++; };
            cb.saved = [&, p](const w::Saved& saved) {
                CHECK(ticking && !dispatching); native[p].saved++;
                if (p && auto_ack_saved) { std::string error; CHECK(controllers[p]->acknowledge_saved(saved, error)); }
            };
            cb.error = [&, p](const std::string&) { CHECK(!dispatching); native[p].errors++; };
            controllers[p] = std::make_unique<es::Controller>(sessions[p], cb, config);
            sessions[p].cb.custom = [&, p, message_type = config.message_type](uint8_t type, uint8_t from, net::Reader& r) {
                if (type != message_type) return;
                net::Reader copy(r.cur(), r.left()); Peer sender{copy.u8(), copy.u32()}; copy.u32(); w::Message message; std::string error;
                if (w::read(copy, message, sender, from == 0, error)) {
                    if (auto* decision = std::get_if<Decision>(&message.payload); decision && decision->accepted()) ++native[p].wire_decisions;
                    if (auto* request = std::get_if<w::LeaseRequest>(&message.payload)) wire_lease_requests[from].push_back(*request);
                    if (auto* reply = std::get_if<w::LeaseReply>(&message.payload)) {
                        wire_lease_replies[p].push_back(*reply);
                        if (hold_lease_replies[p]) {
                            held_lease_replies[p].push_back({*reply,{r.cur(),r.cur()+r.left()}});r.skip(r.left());return;
                        }
                    }
                }
                controllers[p]->on_message(from, r);
            };
            sessions[p].cb.player_join = [&, p](const net::PlayerInfo& player) { controllers[p]->on_join(player.id); };
            sessions[p].cb.player_leave = [&, p](uint8_t id, uint8_t) { controllers[p]->on_leave(id); };
            sessions[p].cb.disconnected = [&, p](const std::string&) { controllers[p]->on_leave(0); };
        }
        CHECK(sessions[0].host(0, "Host", error));
        for (unsigned p = 1; p < count; ++p) {
            if (adverse) adversaries[p] = std::make_unique<UdpAdversary>(sessions[0].port());
            auto port = adverse ? adversaries[p]->socket.local_port() : sessions[0].port();
            CHECK(sessions[p].join("127.0.0.1:" + std::to_string(port), "Guest " + std::to_string(p), error));
        }
        CHECK(spin([&] { return sessions[0].players().size() == count && std::all_of(sessions.begin() + 1, sessions.begin() + count, [](const auto& s) { return s.connected(); }); }));
        for (unsigned p = 0; p < count; ++p) CHECK(sessions[p].my_id() == p);
    }
    void step() {
        for (auto& proxy : adversaries) if (proxy) proxy->pump();
        dispatching = true; for (unsigned p = 0; p < count; ++p) sessions[p].update(); dispatching = false;
        for (auto& proxy : adversaries) if (proxy) proxy->pump();
        ticking = true; const Time elapsed = uint32_t(net::now_ms() - begun);
        for (unsigned p = 0; p < count; ++p) if (tick_enabled[p]) controllers[p]->tick(origins[p] + elapsed); ticking = false;
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    template<class F> bool spin(F predicate, uint32_t timeout = 4000) {
        auto start = net::now_ms(); while (uint32_t(net::now_ms() - start) < timeout) { step(); if (predicate()) return true; } return false;
    }
    void start(State s = seed(), std::optional<Peer> resume = {}) {
        native[0].document = s.document; std::string error;
        CHECK(controllers[0]->start(s, resource(s.document.revision), "Shared native draft", error, resume));
        CHECK(spin([&] { for (unsigned p = 1; p < count; ++p) if (controllers[p]->view().phase != es::Phase::Offered) return false; return true; }));
    }
    void accept() {
        std::string error; for (unsigned p = 1; p < count; ++p) CHECK(controllers[p]->accept_offer(error));
        CHECK(spin([&] { for (unsigned p = 1; p < count; ++p) if (controllers[p]->view().phase != es::Phase::Ready) return false; return true; }));
        CHECK(spin([&] { for (unsigned p = 1; p < count; ++p) if (controllers[0]->view().members.at(sessions[p].my_id()).phase != es::Phase::Ready) return false; return true; }));
    }
    void deliver_lease_reply(unsigned p, uint64_t serial) {
        auto& held=held_lease_replies[p];auto found=std::find_if(held.begin(),held.end(),[&](const auto& item){return item.first.serial==serial;});
        CHECK(found!=held.end());if(found==held.end())return;
        auto bytes=std::move(found->second);held.erase(found);net::Reader reader(bytes.data(),bytes.size());controllers[p]->on_message(0,reader);CHECK(!reader.left());
    }
    std::vector<LeaseToken> grant(unsigned p, Scope scope = {}) {
        std::string error; CHECK(controllers[p]->request_leases({scope}, 3000, error));
        CHECK(spin([&] { return !controllers[p]->view().local_leases.empty(); }));
        std::vector<LeaseToken> tokens; for (const auto& lease : controllers[p]->view().local_leases) tokens.push_back({lease.scope, lease.token}); return tokens;
    }
    bool revision(Revision rev) {
        bool okay = spin([&] { for (unsigned p = 0; p < count; ++p) if (controllers[p]->state().document.revision != rev) return false; return true; });
        if (!okay) for (unsigned p = 0; p < count; ++p) std::printf("peer %u revision %llu phase %u: %s\n", p, static_cast<unsigned long long>(controllers[p]->state().document.revision), unsigned(controllers[p]->view().phase), controllers[p]->view().error.c_str());
        return okay;
    }
    void release(unsigned p, const std::vector<LeaseToken>& tokens) {
        std::string error;const auto serial=controllers[p]->release_leases(tokens,error);CHECK(serial);
        CHECK(controllers[p]->view().local_leases.empty());
        // Immediate local quarantine is not evidence the host processed Release.
        if(p)CHECK(spin([&]{return std::any_of(wire_lease_replies[p].begin(),wire_lease_replies[p].end(),[&](const auto& reply){return reply.serial==serial;});}));
        else step();
    }
    Request edit(unsigned p, ObjectId object, FieldKey field, Value value, const std::vector<LeaseToken>& tokens) {
        Request r; const auto& d = controllers[p]->state().document; r.base_revision = d.revision; r.summary = "Own native gesture"; r.leases = tokens;
        Mutation m; m.target = object; m.field = field; m.value = std::move(value); m.expected = field_revision(d.placements.at(object), field); r.mutations = {m}; return r;
    }
};

static void test_two_player_and_real_udp()
{
    Party p(2); p.start();
    CHECK(p.native[1].imports == 0 && p.native[1].baselines == 0);
    p.accept(); CHECK(p.native[1].imports == 1 && p.native[1].baselines == 1);
    auto tokens = p.grant(1, {ScopeKind::Placement, object_a, {}});
    auto deadline = p.controllers[1]->view().local_leases.front().expires;
    CHECK(deadline >= p.origins[1] && deadline < p.origins[1] + 10000); // Not the host's clock.
    std::string error; auto request = p.edit(1, object_a, position, Vec3{7, 8, 9}, tokens);
    CHECK(p.controllers[1]->submit(request, error)); CHECK(p.revision(1));
    CHECK(p.controllers[0]->state().document == p.controllers[1]->state().document);
    CHECK(p.native[1].commits == 1 && p.native[1].persists == 1 && p.native[1].imports == 1); // No resources per transform.
    p.release(1, tokens);
    auto host_tokens = p.grant(0); Mutation clone; clone.kind = MutationKind::Clone; clone.target = object_a; clone.expected = 1;
    Request clone_request; clone_request.base_revision = 1; clone_request.summary = "Clone unknown native marker data"; clone_request.leases = host_tokens; clone_request.mutations = {clone};
    CHECK(p.controllers[0]->submit(clone_request, error)); CHECK(p.revision(2));
    CHECK(p.native[1].imports == 2 && p.native[1].staged == 2 && p.native[1].commits == 2);
    CHECK(p.controllers[1]->state().document.placements.size() == 3);
    // An accepted retry at an older revision returns its original decision only.
    request.project = p.controllers[1]->view().context.project; request.epoch = p.controllers[1]->view().context.epoch;
    request.operation = {p.controllers[1]->view().local, 1, request.epoch}; request.mutations.front().value = Vec3{999, 0, 0};
    net::Writer retry; auto request_context = p.controllers[1]->view().context; request_context.revision = request.base_revision;
    CHECK(w::write({request_context, request}, retry, error)); const auto prior_decisions = p.native[1].decisions, prior_wire = p.native[1].wire_decisions;
    CHECK(p.sessions[1].send_reliable_to(0, 53, addressed(p.controllers[1]->view().local, p.controllers[0]->view().local, retry)));
    CHECK(p.spin([&] { return p.native[1].wire_decisions > prior_wire; }));
    CHECK(p.native[1].decisions == prior_decisions);
    CHECK(p.native[1].commits == 2 && p.controllers[0]->view().locally_durable == 2);
    CHECK(p.controllers[1]->state().document == p.controllers[0]->state().document);
    CHECK(p.controllers[0]->publish_saved({2, {0x555, 0x366A930D, 0}}, error));
    CHECK(p.spin([&] { return p.native[1].saved == 1; }));
    p.release(0, host_tokens);
    w::Presence presence; presence.selected = object_a; presence.act = {2, 1}; presence.editing = true;
    CHECK(p.controllers[1]->set_presence(presence, error)); CHECK(p.spin([&] { return p.native[0].presence > 0; }));
    CHECK(p.controllers[0]->view().members.at(1).presence.author == p.controllers[1]->view().local);
}

static void test_host_durable_coverage()
{
    Party p(4); p.start(); std::string error;
    auto check_host = [&] {
        const auto& view = p.controllers[0]->view();
        const auto& member = view.members.at(0);
        std::string expected;
        CHECK(es::document_hash(p.controllers[0]->state().document, expected, error));
        CHECK(member.peer == view.local && member.phase == es::Phase::Ready);
        CHECK(member.applied_revision == view.context.revision);
        CHECK(member.coverage.durable_revision == view.locally_durable);
        CHECK(member.coverage.state_hash == expected && !expected.empty());
    };
    check_host(); // Revision zero is known coverage, distinct from an empty hash.
    p.accept();
    auto guest_lease = p.grant(3, {ScopeKind::Field, object_b, scale});
    CHECK(p.controllers[3]->submit(p.edit(3, object_b, scale, 1.75f, guest_lease), error));
    CHECK(p.revision(1)); check_host(); p.release(3, guest_lease);
    auto host_lease = p.grant(0, {ScopeKind::Field, object_a, position});
    CHECK(p.controllers[0]->submit(p.edit(0, object_a, position, Vec3{70, 0, 0}, host_lease), error));
    CHECK(p.revision(2)); check_host();
    const auto committed_hash = p.controllers[0]->view().members.at(0).coverage.state_hash;
    p.native[0].fail_persist = true;
    const auto decisions = p.native[0].decisions;
    CHECK(p.controllers[0]->submit(p.edit(0, object_a, position, Vec3{80, 0, 0}, host_lease), error));
    CHECK(p.spin([&] { return p.native[0].decisions > decisions; }));
    CHECK(p.controllers[0]->state().document.revision == 2);
    check_host();
    CHECK(p.controllers[0]->view().members.at(0).coverage.state_hash == committed_hash);
    p.native[0].fail_persist = false; p.release(0, host_lease);
    auto recovered = p.controllers[0]->state(); recovered.document.epoch = 0x102;
    p.controllers[0]->end(); for (int i = 0; i < 10; ++i) p.step();
    p.start(recovered); check_host(); // Starting a recovered nonzero revision.
}

static void test_four_players_and_controls()
{
    Party p(4); p.start(); p.accept(); std::string error;
    std::array<Scope, 4> scopes{{{ScopeKind::Field, object_a, position}, {ScopeKind::Field, object_a, scale}, {ScopeKind::Field, object_b, position}, {ScopeKind::Field, object_b, scale}}};
    std::array<std::vector<LeaseToken>, 4> leases;
    for (unsigned i = 0; i < 4; ++i) leases[i] = p.grant(i, scopes[i]);
    for (unsigned i = 0; i < 4; ++i) {
        Value v = (i % 2) ? Value(float(i + 2)) : Value(Vec3{float(i + 10), 0, 0});
        CHECK(p.controllers[i]->submit(p.edit(i, scopes[i].object, scopes[i].field, v, leases[i]), error));
    }
    CHECK(p.revision(4));
    for (unsigned i = 1; i < 4; ++i) CHECK(p.controllers[i]->state().document == p.controllers[0]->state().document);
    CHECK(p.spin([&] { for (unsigned i = 1; i < 4; ++i) if (p.controllers[0]->view().members.at(i).applied_revision != 4) return false; return true; }));
    CHECK(p.controllers[0]->prepare_playtest(error));
    CHECK(p.spin([&] { for (unsigned i = 0; i < 4; ++i) if (p.controllers[i]->view().phase != es::Phase::Preparing) return false; return true; }));
    for (unsigned i = 0; i < 4; ++i) { CHECK(p.controllers[i]->view().local_leases.empty()); CHECK(!p.controllers[i]->submit(Request{}, error)); }
    auto round = p.controllers[0]->view().playtest_round;
    for (unsigned i = 0; i < 3; ++i) CHECK(p.controllers[i]->ready_playtest(true, error));
    for (int i = 0; i < 20; ++i) p.step(); CHECK(p.controllers[0]->view().phase == es::Phase::Preparing);
    CHECK(p.controllers[3]->ready_playtest(true, error));
    CHECK(p.spin([&] { for (unsigned i = 0; i < 4; ++i) if (p.controllers[i]->view().phase != es::Phase::Playing) return false; return true; }));
    for (unsigned i = 0; i < 4; ++i) for (const auto& [id, member] : p.controllers[i]->view().members) { (void)id; CHECK(member.ready); }
    p.native[0].controls_ready = false; p.native[1].controls_ready = false;
    const auto host_controls = p.native[0].controls, guest_controls = p.native[1].controls;
    CHECK(p.controllers[0]->return_editor(error));
    CHECK(p.controllers[0]->return_editor(error)); // Duplicates cannot repeat native teardown.
    for (int i = 0; i < 20; ++i) p.step();
    CHECK(p.controllers[0]->view().phase == es::Phase::Returning && p.native[0].controls == host_controls);
    p.native[0].controls_ready = true;
    CHECK(p.spin([&] { return p.native[0].controls == host_controls + 1; }));
    for (int i = 0; i < 20; ++i) p.step();
    CHECK(p.controllers[1]->view().phase == es::Phase::Returning && p.native[1].controls == guest_controls);
    CHECK(p.controllers[0]->view().phase == es::Phase::Returning);
    CHECK(!p.controllers[0]->prepare_playtest(error));
    CHECK(!p.controllers[0]->submit(Request{}, error));
    CHECK(!p.controllers[0]->request_leases({{}}, 1000, error));
    CHECK(!p.controllers[1]->submit(Request{}, error));
    p.native[1].controls_ready = true;
    CHECK(p.spin([&] { for (unsigned i = 0; i < 4; ++i) if (p.controllers[i]->view().phase != es::Phase::Ready) return false; return true; }));
    CHECK(p.native[0].controls == host_controls + 1 && p.native[1].controls == guest_controls + 1);
    CHECK(p.controllers[0]->prepare_playtest(error)); CHECK(p.spin([&] { return p.controllers[1]->view().phase == es::Phase::Preparing; }));
    CHECK(p.controllers[0]->view().playtest_round == round + 1);
    auto ctx = p.controllers[0]->view().context; net::Writer stale;
    CHECK(w::write({ctx, w::Control{w::ControlKind::GuestReadyPlaytest, round, ctx.revision, true, 0}}, stale, error));
    CHECK(p.sessions[1].send_reliable_to(0, 53, addressed(p.controllers[1]->view().local, p.controllers[0]->view().local, stale))); for (int i = 0; i < 20; ++i) p.step();
    CHECK(!p.controllers[0]->view().members.at(1).ready);
    net::Writer old_returned; CHECK(w::write({ctx, w::Control{w::ControlKind::GuestEditorReturned, round, ctx.revision, false, 0}}, old_returned, error));
    CHECK(p.sessions[1].send_reliable_to(0, 53, addressed(p.controllers[1]->view().local, p.controllers[0]->view().local, old_returned)));
    for (int i = 0; i < 20; ++i) p.step(); CHECK(p.controllers[0]->view().phase == es::Phase::Preparing);
    CHECK(p.controllers[0]->cancel_playtest(error)); CHECK(p.spin([&] { return p.controllers[1]->view().phase == es::Phase::Ready; }));
    CHECK(p.controllers[0]->prepare_playtest(error));
    CHECK(p.spin([&] { for (unsigned i = 0; i < 4; ++i) if (p.controllers[i]->view().phase != es::Phase::Preparing) return false; return true; }));
    for (unsigned i = 0; i < 4; ++i) CHECK(p.controllers[i]->ready_playtest(true, error));
    CHECK(p.spin([&] { for (unsigned i = 0; i < 4; ++i) if (p.controllers[i]->view().phase != es::Phase::Playing) return false; return true; }));
    p.native[3].controls_ready = false; const auto prior_peer = p.controllers[3]->view().local;
    CHECK(p.controllers[0]->return_editor(error));
    CHECK(p.spin([&] { return p.controllers[3]->view().phase == es::Phase::Returning; }));
    p.sessions[3].leave("return barrier disconnect");
    CHECK(p.spin([&] { for (unsigned i = 0; i < 3; ++i) if (p.controllers[i]->view().phase != es::Phase::Ready) return false; return true; }));
    CHECK(!p.controllers[0]->view().members.contains(3));
    p.native[3].controls_ready = true;
    CHECK(p.sessions[3].join("127.0.0.1:" + std::to_string(p.sessions[0].port()), "Rejoined", error));
    CHECK(p.spin([&] { return p.controllers[3]->view().phase == es::Phase::Offered; }));
    CHECK(p.controllers[3]->view().local.generation > prior_peer.generation);
    CHECK(p.controllers[3]->accept_offer(error));
    CHECK(p.spin([&] { return p.controllers[3]->view().phase == es::Phase::Ready && p.controllers[0]->view().members.at(3).phase == es::Phase::Ready; }));
}

static void test_large_operation_transfer()
{
    Party p(2); p.start(); p.accept(); std::string error;
    auto tokens = p.grant(1, {ScopeKind::Placement, object_a, {}});
    auto request = p.edit(1, object_a, {Field::Property, 100}, std::string(1000, 'x'), tokens);
    CHECK(p.controllers[1]->submit(request, error)); CHECK(p.revision(1));
    CHECK(p.controllers[0]->state().document == p.controllers[1]->state().document);
    CHECK(p.native[1].imports == 1 && p.native[1].commits == 1 && p.native[1].persists == 1);
    CHECK(p.controllers[1]->view().locally_durable == 1);
}

static void test_saved_snapshot_waits_for_native_gesture()
{
    Party p(2); p.start(); p.accept(); std::string error;
    p.native[1].authoring = false;
    CHECK(p.controllers[0]->publish_saved({0, {0x9911, 0x366A930D, 0}}, error));
    for (int i = 0; i < 30; ++i) p.step();
    CHECK(p.native[1].saved == 0 && !p.controllers[1]->view().saved.has_value());
    auto lease = p.grant(0, {ScopeKind::Placement, object_a, {}});
    CHECK(p.controllers[0]->submit(p.edit(0, object_a, position, Vec3{123, 45, 6}, lease), error));
    CHECK(p.spin([&] { return p.controllers[0]->state().document.revision == 1; }));
    for (int i = 0; i < 20; ++i) p.step();
    CHECK(p.native[1].commits == 0 && p.native[1].saved == 0);
    p.native[1].authoring = true; CHECK(p.revision(1));
    CHECK(p.native[1].saved == 1 && p.controllers[1]->view().saved->resource.instance == 0x9911);
    CHECK(p.native[1].commits == 1 && p.native[1].errors == 0);
}

static void test_delete_revokes_lease_before_native_decision(unsigned author)
{
    Party p(2); p.start(); p.accept(); p.release_on_decision = true; std::string error;
    auto lease = p.grant(author, {ScopeKind::Placement, object_a, {}});
    Mutation erase; erase.kind = MutationKind::Delete; erase.target = object_a;
    Request request; request.summary = "Delete leased placement"; request.leases = lease; request.mutations = {erase};
    CHECK(p.controllers[author]->submit(request, error)); CHECK(p.revision(1));
    CHECK(p.native[author].own_decision_leases.back() == 0);
    CHECK(p.controllers[author]->view().local_leases.empty());
    lease = p.grant(author); Mutation create; create.kind = MutationKind::Create; create.initial.class_id = {1, 1}; create.initial.act_id = {2, 1};
    request.base_revision = 1; request.summary = "Create after deleting"; request.leases = lease; request.mutations = {create};
    CHECK(p.controllers[author]->submit(request, error)); CHECK(p.revision(2));
    CHECK(p.native[author].own_decision_leases.back() == 1); // Valid Document lease is released normally.
    CHECK(p.spin([&] { return p.controllers[author]->view().local_leases.empty(); }));
    CHECK(p.native[author].errors == 0);
    auto other = p.grant(1 - author); CHECK(!other.empty()); // No stale token kept the Document lock alive.
}

static void test_accepted_retry_keeps_new_gesture_ownership(unsigned author)
{
    Party p(2); p.start(); p.accept(); std::string error;
    auto old_lease = p.grant(author, {ScopeKind::Placement, object_a, {}});
    auto original = p.edit(author, object_a, position, Vec3{11, 22, 33}, old_lease);
    CHECK(p.controllers[author]->submit(original, error)); CHECK(p.revision(1)); p.release(author, old_lease);
    auto current_lease = p.grant(author, {ScopeKind::Placement, object_b, {}});
    p.release_on_decision = true;
    original.operation = {p.controllers[author]->view().local, 1, p.controllers[author]->view().context.epoch};
    auto callbacks = p.native[author].decisions, wire = p.native[author].wire_decisions;
    CHECK(p.controllers[author]->submit(original, error));
    if (author) CHECK(p.spin([&] { return p.native[author].wire_decisions > wire; }));
    for (int i = 0; i < 25; ++i) p.step();
    CHECK(p.native[author].decisions == callbacks && p.controllers[author]->view().local_leases.size() == 1);
    CHECK(p.controllers[author]->view().local_leases.front().token == current_lease.front().token);
    CHECK(p.controllers[author]->state().document.revision == 1 && p.native[author].errors == 0);
    CHECK(p.controllers[author]->submit(p.edit(author, object_b, position, Vec3{44, 55, 66}, current_lease), error));
    CHECK(p.revision(2)); CHECK(p.spin([&] { return p.controllers[author]->view().local_leases.empty(); }));
}

static void test_queued_begin_and_new_roster()
{
    Party p(2); p.start(); p.accept(); std::string error;
    CHECK(p.controllers[0]->prepare_playtest(error));
    CHECK(p.spin([&] { return p.controllers[1]->view().phase == es::Phase::Preparing; }));
    CHECK(p.controllers[0]->ready_playtest(true, error)); CHECK(p.controllers[1]->ready_playtest(true, error));
    p.step(); p.tick_enabled[1] = false;
    CHECK(p.spin([&] { return p.controllers[0]->view().phase == es::Phase::Playing; }));
    p.count = 4;
    for (unsigned i = 2; i < 4; ++i) CHECK(p.sessions[i].join("127.0.0.1:" + std::to_string(p.sessions[0].port()), "Late arrival " + std::to_string(i), error));
    CHECK(p.spin([&] { return p.sessions[0].players().size() == 4 && p.sessions[1].players().size() == 4 && p.controllers[0]->view().phase == es::Phase::Returning; }));
    // Session membership is now newer than the queued old-round native callbacks.
    p.tick_enabled[1] = true;
    CHECK(p.spin([&] { return p.controllers[0]->view().phase == es::Phase::Ready && p.controllers[1]->view().phase == es::Phase::Ready; }));
    CHECK(p.controllers[1]->view().playtest_participants == 3 && p.native[1].errors == 0);
    CHECK(p.spin([&] { return p.controllers[2]->view().phase == es::Phase::Offered && p.controllers[3]->view().phase == es::Phase::Offered; }));
    CHECK(p.controllers[2]->accept_offer(error)); CHECK(p.controllers[3]->accept_offer(error));
    CHECK(p.spin([&] { for (unsigned i = 2; i < 4; ++i) if (p.controllers[i]->view().phase != es::Phase::Ready || p.controllers[0]->view().members.at(i).phase != es::Phase::Ready) return false; return true; }));
    CHECK(p.controllers[0]->prepare_playtest(error));
    CHECK(p.spin([&] { for (unsigned i = 0; i < 4; ++i) if (p.controllers[i]->view().phase != es::Phase::Preparing) return false; return true; }));
    for (unsigned i = 0; i < 4; ++i) CHECK(p.controllers[i]->view().playtest_participants == 15);
    CHECK(p.controllers[0]->cancel_playtest(error));
    CHECK(p.spin([&] { for (unsigned i = 0; i < 4; ++i) if (p.controllers[i]->view().phase != es::Phase::Ready) return false; return true; }));
}

static void test_native_gesture_transaction_holds()
{
    Party p(4); p.start(); p.accept(); std::string error;
    auto first = p.grant(1, {ScopeKind::Field, object_a, position});
    auto second = p.grant(2, {ScopeKind::Field, object_b, position});
    p.native[0].authoring = false;
    CHECK(p.controllers[1]->submit(p.edit(1, object_a, position, Vec3{10, 20, 30}, first), error));
    CHECK(p.controllers[2]->submit(p.edit(2, object_b, position, Vec3{40, 50, 60}, second), error));
    w::Presence presence; presence.selected = object_a; presence.act = {2, 1};
    CHECK(p.controllers[1]->set_presence(presence, error));
    CHECK(p.spin([&] { return p.native[0].presence > 0; }));
    for (int i = 0; i < 20; ++i) p.step();
    CHECK(p.controllers[0]->state().document.revision == 0 && p.native[0].persists == 0);
    p.native[0].authoring = true; CHECK(p.revision(2));
    p.release(1, first); p.release(2, second);
    auto structural = p.grant(0); p.native[3].authoring = false;
    CHECK(p.controllers[0]->submit(p.edit(0, object_a, scale, 2.f, structural), error));
    CHECK(p.spin([&] { return p.controllers[0]->state().document.revision == 3; }));
    Mutation clone; clone.kind = MutationKind::Clone; clone.target = object_b;
    clone.expected = p.controllers[0]->state().document.placements.at(object_b).revision;
    clone.clone_pose = Pose{{70, 80, 90}, {}, 1.5f};
    Request request; request.base_revision = 3; request.summary = "Structural commit behind a remote gesture"; request.leases = structural; request.mutations = {clone};
    CHECK(p.controllers[0]->submit(request, error));
    CHECK(p.spin([&] { return p.controllers[0]->state().document.revision == 4 && p.controllers[1]->state().document.revision == 4; }));
    for (int i = 0; i < 20; ++i) p.step();
    CHECK(p.controllers[3]->state().document.revision == 2 && p.native[3].imports == 1 && p.native[3].commits == 2);
    p.release(0, structural);
    auto local = p.grant(3, {ScopeKind::Field, object_b, scale});
    auto own = p.edit(3, object_b, scale, 3.f, local);
    p.native[3].authoring = true;
    CHECK(p.controllers[3]->submit(own, error));
    CHECK(p.revision(5));
    CHECK(p.native[3].imports == 2 && p.native[3].commits == 5);
    for (unsigned i = 0; i < 4; ++i) {
        CHECK(p.controllers[i]->view().phase == es::Phase::Ready && p.native[i].errors == 0);
        CHECK(p.controllers[i]->state().document == p.controllers[0]->state().document);
    }
    CHECK(p.controllers[0]->state().history.size() == 5); // Exactly one accepted local gesture.
}

static void test_same_controller_host_to_guest()
{
    Party p(2); p.start(); p.accept(); std::string error;
    for (unsigned i = 0; i < 2; ++i) { p.controllers[i]->end(); p.sessions[i].leave("switch editor host"); }
    for (int i = 0; i < 10; ++i) p.step();
    CHECK(p.sessions[1].host(0, "New host", error));
    CHECK(p.sessions[0].join("127.0.0.1:" + std::to_string(p.sessions[1].port()), "Former host", error));
    CHECK(p.spin([&] { return p.sessions[0].connected() && p.sessions[1].players().size() == 2; }));
    auto state = seed(); state.document.epoch = 0x3344; p.native[1].document = state.document;
    CHECK(p.controllers[1]->start(state, resource(0), "Role switch draft", error));
    CHECK(p.spin([&] { return p.controllers[0]->view().phase == es::Phase::Offered; }));
    CHECK(p.controllers[0]->view().local.slot == 1); CHECK(p.controllers[0]->accept_offer(error));
    CHECK(p.spin([&] { return p.controllers[0]->view().phase == es::Phase::Ready; }));
    CHECK(p.controllers[0]->state().document == p.controllers[1]->state().document);
}

static void test_delta_resources_keep_complete_late_join_cache()
{
    Party p(2); p.publish_deltas = true; p.start(); p.accept(); std::string error;
    auto lease = p.grant(0); Mutation clone; clone.kind = MutationKind::Clone; clone.target = object_a;
    Request request; request.summary = "Clone with root-only resource delta"; request.leases = lease; request.mutations = {clone};
    CHECK(p.controllers[0]->submit(request, error)); CHECK(p.revision(1));
    CHECK(p.native[1].imported_payloads.back() == resource(1, 96));
    CHECK(p.native[1].commits == 1); // Fake native apply requires target Resources first.
    p.release(0, lease);
    p.count = 4;
    for (unsigned i = 2; i < 4; ++i) CHECK(p.sessions[i].join("127.0.0.1:" + std::to_string(p.sessions[0].port()), "Full baseline late join " + std::to_string(i), error));
    CHECK(p.spin([&] { return p.controllers[2]->view().phase == es::Phase::Offered && p.controllers[3]->view().phase == es::Phase::Offered; }));
    CHECK(p.controllers[2]->accept_offer(error)); CHECK(p.controllers[3]->accept_offer(error));
    CHECK(p.spin([&] { return p.controllers[2]->view().phase == es::Phase::Ready && p.controllers[3]->view().phase == es::Phase::Ready; }));
    for (unsigned i = 2; i < 4; ++i) CHECK(p.native[i].imported_payloads.back() == resource(1));
    CHECK(p.spin([&] { return p.controllers[0]->view().members.at(2).phase == es::Phase::Ready && p.controllers[0]->view().members.at(3).phase == es::Phase::Ready; }));
    auto full = resource(1, 20000), delta = resource(1, 80); full.back() = 0xB1; delta.back() = 0xD1;
    const auto expected_full = full, expected_delta = delta;
    CHECK(p.controllers[0]->update_resources(full, 1, error, true, delta));
    full.back() = 0; delta.back() = 0; // Queued cache/delta own immutable copies.
    CHECK(p.controllers[0]->publish_saved({1, {0x8811, 0x366A930D, 0}}, error));
    CHECK(p.spin([&] { for (unsigned i = 1; i < 4; ++i) if (p.native[i].saved != 1) return false; return true; }));
    for (unsigned i = 1; i < 4; ++i) CHECK(p.native[i].imported_payloads.back() == expected_delta);
    p.controllers[0]->on_join(1);
    CHECK(p.spin([&] { return p.controllers[1]->view().phase == es::Phase::Offered; })); CHECK(p.controllers[1]->accept_offer(error));
    CHECK(p.spin([&] { return p.native[1].baselines == 2 && p.controllers[1]->view().phase == es::Phase::Ready; }));
    CHECK(p.native[1].imported_payloads.back() == expected_full);
    CHECK(p.controllers[1]->state().document == p.controllers[0]->state().document);
}

static void test_failed_delta_staging_publishes_nothing()
{
    Party p(2); p.publish_deltas = true; p.start(); p.accept(); std::string error;
    p.native[0].fail_persist = true; auto lease = p.grant(0);
    Mutation clone; clone.kind = MutationKind::Clone; clone.target = object_a;
    Request request; request.summary = "Reject persistence after delta staging"; request.leases = lease; request.mutations = {clone};
    CHECK(p.controllers[0]->submit(request, error)); CHECK(p.spin([&] { return p.native[0].decisions == 1; }));
    for (int i = 0; i < 25; ++i) p.step();
    CHECK(p.controllers[0]->state().document.revision == 0 && p.controllers[1]->state().document.revision == 0);
    CHECK(p.native[1].imports == 1 && p.native[1].commits == 0 && p.native[0].document == seed().document);
    p.count = 3; CHECK(p.sessions[2].join("127.0.0.1:" + std::to_string(p.sessions[0].port()), "Join after rejected delta", error));
    CHECK(p.spin([&] { return p.controllers[2]->view().phase == es::Phase::Offered; })); CHECK(p.controllers[2]->accept_offer(error));
    CHECK(p.spin([&] { return p.controllers[2]->view().phase == es::Phase::Ready; }));
    CHECK(p.native[2].imported_payloads.back() == resource(0));
    es::Config config; config.transfer_bytes = 20000; Party bounded(2, config); bounded.start(); bounded.accept();
    CHECK(!bounded.controllers[0]->update_resources(resource(0), 0, error, true, resource(0, 9000)));
    for (int i = 0; i < 25; ++i) bounded.step(); CHECK(bounded.native[1].imports == 1);
    // Rejected full+delta memory admission also leaves the late-join cache intact.
    bounded.controllers[0]->on_join(1);
    CHECK(bounded.spin([&] { return bounded.controllers[1]->view().phase == es::Phase::Offered; })); CHECK(bounded.controllers[1]->accept_offer(error));
    CHECK(bounded.spin([&] { return bounded.native[1].baselines == 2; })); CHECK(bounded.native[1].imported_payloads.back() == resource(0));
}

static void test_close_waits_for_accepted_structural_commit()
{
    es::Config config; config.packets_per_tick = 1;
    Party p(2, config, true); CHECK(p.controllers[0]->settled()); p.start();
    CHECK(p.controllers[0]->settled()); // Offered peers have not joined the draft.
    p.accept(); CHECK(p.spin([&] { return p.controllers[0]->settled(); }));
    auto lease = p.grant(0); p.structural_bytes = 64000; p.native[1].authoring = false;
    Mutation clone; clone.kind = MutationKind::Clone; clone.target = object_a;
    Request request; request.summary = "Commit then immediately request host close"; request.leases = lease; request.mutations = {clone};
    std::string error; CHECK(p.spin([&]{return p.controllers[0]->settled();})); CHECK(p.controllers[0]->submit(request, error));
    CHECK(p.controllers[0]->state().document.revision == 0 && p.controllers[0]->settled()); // Unaccepted Requests are not the barrier.
    CHECK(p.spin([&] { return p.controllers[0]->state().document.revision == 1; }));
    CHECK(!p.controllers[0]->settled()); // Most resource packets and the Decision are still queued.
    p.native[0].authoring = false; // Runtime close holds new edits but keeps transport ticking.
    auto unaccepted = p.edit(0, object_a, scale, 2.f, lease);
    CHECK(p.controllers[0]->submit(unaccepted, error));
    CHECK(p.spin([&] { return p.native[1].wire_decisions == 1 && p.sessions[0].pending_to(1) == 0; }));
    CHECK(p.native[1].imports == 2 && p.native[1].commits == 0);
    CHECK(p.controllers[1]->state().document.revision == 0);
    CHECK(!p.controllers[0]->settled()); // UDP ACKs cannot replace native apply/persist Coverage.
    p.native[1].authoring = true;
    CHECK(p.spin([&] { return p.controllers[0]->settled(); }));
    CHECK(p.controllers[0]->state().document.revision == 1); // Held, unaccepted Request did not block close or execute.
    CHECK(p.controllers[0]->view().members.at(1).coverage.durable_revision == 1);
    const auto committed = p.controllers[0]->state().document;
    CHECK(p.controllers[1]->state().document == committed && p.native[1].commits == 1 && p.native[1].persists == 1);
    p.controllers[0]->end();
    CHECK(p.spin([&] { return p.controllers[1]->view().phase == es::Phase::Idle; }));
    CHECK(p.native[1].document == committed && p.native[1].commits == 1 && p.native[1].errors == 0);
}

static void test_settled_waits_for_join_but_excludes_failed_peer()
{
    Party p(2); p.start(); p.native[1].ready = false; std::string error;
    CHECK(p.controllers[1]->accept_offer(error));
    CHECK(p.spin([&] { return p.native[1].imports == 1; })); CHECK(!p.controllers[0]->settled());
    p.native[1].ready = true; CHECK(p.spin([&] { return p.controllers[0]->settled(); }));
    auto lease = p.grant(0); p.native[1].fail_apply = true;
    CHECK(p.controllers[0]->submit(p.edit(0, object_a, position, Vec3{7, 8, 9}, lease), error));
    CHECK(p.spin([&] { return p.controllers[0]->view().members.at(1).phase == es::Phase::Frozen; }));
    CHECK(p.controllers[0]->settled()); // Explicitly failed peers need recovery, not an endless close wait.
}

static void test_same_revision_saved_needs_explicit_applied_ack()
{
    Party p(2); p.start(); p.accept(); std::string error; p.auto_ack_saved = false;
    CHECK(p.spin([&] { return p.controllers[0]->settled(); })); p.native[1].authoring = false;
    CHECK(p.controllers[0]->update_resources(resource(0), 0, error, true, resource(0, 80)));
    CHECK(p.controllers[0]->publish_saved({0, {0x8855, 0x366A930D, 0}}, error));
    CHECK(!p.controllers[0]->settled());
    CHECK(p.spin([&] { return p.native[0].saved == 1 && p.sessions[0].pending_to(1) == 0; }));
    CHECK(!p.controllers[0]->settled());
    CHECK(p.native[0].saved == 1 && p.native[1].saved == 0);
    CHECK(!p.controllers[1]->settled());
    p.native[1].authoring = true; CHECK(p.spin([&] { return p.native[1].saved == 1; }));
    CHECK(!p.controllers[0]->settled()); // A callback that deferred durable work is not an acknowledgement.
    const auto first = *p.controllers[1]->view().saved;
    CHECK(p.controllers[1]->acknowledge_saved(first, error)); CHECK(p.spin([&] { return p.controllers[0]->settled(); }));
    // Another save of the same revision/key needs a fresh acknowledgement.
    CHECK(p.controllers[0]->publish_saved({0, first.resource}, error));
    CHECK(p.spin([&] { return p.native[1].saved == 2 && p.sessions[0].pending_to(1) == 0; }));
    const auto second = *p.controllers[1]->view().saved; CHECK(second.serial > first.serial);
    CHECK(!p.controllers[1]->acknowledge_saved(first, error));
    auto context = p.controllers[0]->view().context;
    auto send = [&](w::Context scope, w::Saved saved, Peer source, Peer target) {
        net::Writer packet; CHECK(w::write({scope, w::SavedApplied{saved}}, packet, error));
        CHECK(p.sessions[1].send_reliable_to(0, 53, addressed(source, target, packet)));
        CHECK(p.spin([&] { return p.sessions[1].pending_to(0) == 0; })); CHECK(!p.controllers[0]->settled());
    };
    const auto source = p.controllers[1]->view().local, target = p.controllers[0]->view().local;
    send(context, first, source, target);
    auto stale = context; stale.epoch++; send(stale, second, source, target);
    stale = context; stale.project[0] ^= 1; send(stale, second, source, target);
    send(context, second, {source.slot, source.generation + 1}, target);
    send(context, second, source, {target.slot, target.generation + 1});
    auto other_key = second; other_key.resource.instance++; send(context, other_key, source, target);
    CHECK(p.controllers[1]->acknowledge_saved(second, error)); CHECK(p.spin([&] { return p.controllers[0]->settled(); }));
    CHECK(p.controllers[1]->acknowledge_saved(second, error)); // Idempotent, no repeated native work.
    p.count = 3; CHECK(p.sessions[2].join("127.0.0.1:" + std::to_string(p.sessions[0].port()), "Join saved root", error));
    CHECK(p.spin([&] { return p.controllers[2]->view().phase == es::Phase::Offered; }));
    CHECK(p.controllers[2]->accept_offer(error)); CHECK(p.spin([&] {
        return p.controllers[2]->view().phase == es::Phase::Ready && p.controllers[0]->view().members.at(2).phase == es::Phase::Ready && p.controllers[0]->settled();
    }));
    CHECK(p.native[2].baselines == 1 && p.native[2].saved == 0); // Verified durable baseline already includes the saved root.
}

static void test_local_freeze_notifies_party_once()
{
    Party p(2); p.start(); p.accept(); CHECK(p.spin([&] { return p.controllers[0]->settled(); }));
    const auto before = p.controllers[1]->state(); const auto errors = p.native[1].errors;
    p.controllers[1]->freeze_local("Injected runtime native-generation failure");
    CHECK(p.controllers[1]->view().phase == es::Phase::Frozen && p.controllers[1]->state() == before);
    CHECK(p.native[1].errors == errors + 1 && p.sessions[1].connected());
    p.controllers[1]->freeze_local("Duplicate runtime failure"); CHECK(p.native[1].errors == errors + 1);
    CHECK(p.spin([&] { return p.controllers[0]->view().members.at(1).phase == es::Phase::Frozen; }));
    CHECK(p.controllers[0]->settled() && p.controllers[1]->state() == before);
}

static void test_real_udp_packet_adversity(unsigned players)
{
    Party p(players, {}, true); p.start(); p.accept(); std::string error;
    for (unsigned round = 0; round < players * 2; ++round) {
        const auto author = round % players;
        auto lease = p.grant(author, {ScopeKind::Placement, object_a, {}});
        CHECK(p.controllers[author]->submit(p.edit(author, object_a, position, Vec3{float(round + 100), float(author), 0}, lease), error));
        CHECK(p.revision(round + 1)); p.release(author, lease);
    }
    auto lease = p.grant(0); Mutation clone; clone.kind = MutationKind::Clone; clone.target = object_a;
    clone.expected = p.controllers[0]->state().document.placements.at(object_a).revision;
    clone.clone_pose = Pose{{200, 300, 400}, {}, 2};
    Request request; request.base_revision = players * 2; request.summary = "Opaque clone under real packet loss"; request.leases = lease; request.mutations = {clone};
    CHECK(p.controllers[0]->submit(request, error)); CHECK(p.revision(players * 2 + 1));
    unsigned lost = 0, duplicates = 0, reordered = 0;
    for (unsigned i = 1; i < players; ++i) {
        lost += p.adversaries[i]->dropped; duplicates += p.adversaries[i]->duplicated; reordered += p.adversaries[i]->reordered;
        CHECK(p.controllers[i]->state().document == p.controllers[0]->state().document);
        CHECK(p.native[i].commits == int(players * 2 + 1) && p.native[i].imports == 2 && p.native[i].errors == 0);
    }
    CHECK(p.controllers[0]->publish_saved({players * 2 + 1, {0x8811, 0x366A930D, 0}}, error));
    CHECK(p.spin([&] { return p.controllers[0]->settled(); }));
    for (unsigned i = 1; i < players; ++i) CHECK(p.native[i].saved == 1 && p.native[i].errors == 0);
    CHECK(lost > 0 && duplicates > 0 && reordered > 0);
    std::printf("  UDP %u peers: %u dropped datagrams, %u duplicates, %u reordered deliveries\n", players, lost, duplicates, reordered);
}

static void test_deferred_baseline_and_fail_closed()
{
    Party p(2); p.native[1].ready = false; p.start(); std::string error;
    CHECK(p.controllers[1]->accept_offer(error));
    CHECK(p.spin([&] { return p.native[1].imports == 1; }));
    for (int i = 0; i < 20; ++i) p.step();
    CHECK(p.native[1].baselines == 0 && p.controllers[1]->view().phase == es::Phase::Syncing);
    auto tokens = p.grant(0); CHECK(p.controllers[0]->submit(p.edit(0, object_a, position, Vec3{20, 0, 0}, tokens), error));
    CHECK(p.spin([&] { return p.controllers[0]->state().document.revision == 1; }));
    Mutation clone; clone.kind = MutationKind::Clone; clone.target = object_a; clone.expected = 1;
    Request request; request.summary = "Clone while peer enters editor"; request.base_revision = 1; request.leases = tokens; request.mutations = {clone};
    CHECK(p.controllers[0]->submit(request, error)); CHECK(p.spin([&] { return p.controllers[0]->state().document.revision == 2; }));
    for (int i = 0; i < 40; ++i) p.step(); CHECK(p.native[1].baselines == 0 && p.native[1].commits == 0 && p.native[1].imports == 1);
    p.native[1].ready = true; CHECK(p.revision(2));
    CHECK(p.native[1].baselines == 1 && p.native[1].commits == 2 && p.native[1].imports == 2);
    CHECK(p.controllers[1]->state().document == p.controllers[0]->state().document);
    p.release(0, tokens);
    tokens = p.grant(0); p.native[1].fail_apply = true;
    CHECK(p.controllers[0]->submit(p.edit(0, object_b, position, Vec3{30, 0, 0}, tokens), error));
    CHECK(p.spin([&] { return p.controllers[1]->view().phase == es::Phase::Frozen; }));
    CHECK(p.controllers[1]->state().document.revision == 2 && p.controllers[0]->state().document.revision == 3);
    CHECK(!p.controllers[1]->accept_offer(error));
    const auto previous_guest = p.controllers[1]->view().local;
    p.native[1].fail_apply = false; p.controllers[0]->on_join(1);
    CHECK(p.spin([&] { return p.controllers[1]->view().phase == es::Phase::Offered; }));
    auto old_capability = p.controllers[1]->view().local;
    CHECK(old_capability.generation > 1); CHECK(p.controllers[1]->accept_offer(error)); CHECK(p.revision(3));
    CHECK(p.controllers[1]->view().phase == es::Phase::Ready && p.native[1].baselines == 2);
    // Delayed old-incarnation traffic must not freeze the replacement either way.
    net::Writer old_leave; auto ctx = p.controllers[0]->view().context; CHECK(w::write({ctx, w::Leave{3}}, old_leave, error));
    const auto host = p.controllers[0]->view().local;
    CHECK(p.sessions[1].send_reliable_to(0, 53, addressed(previous_guest, host, old_leave)));
    CHECK(p.sessions[0].send_reliable_to(1, 53, addressed(host, previous_guest, old_leave)));
    for (int i = 0; i < 30; ++i) p.step();
    CHECK(p.controllers[1]->view().phase == es::Phase::Ready && p.controllers[0]->view().members.at(1).phase == es::Phase::Ready);
}

static void test_import_and_input_bounds()
{
    Party p(2); p.native[1].fail_import = true; p.start(); std::string error;
    CHECK(p.controllers[1]->accept_offer(error));
    CHECK(p.spin([&] { return p.controllers[1]->view().phase == es::Phase::Frozen; }));
    CHECK(p.native[1].imports == 1 && p.native[1].baselines == 0 && p.native[1].commits == 0);
    CHECK(p.controllers[0]->view().phase == es::Phase::Ready);
    es::Config small; small.inbound_packets = 2; small.packets_per_tick = 1; small.reliable_window = 1;
    Party bounded(2, small); bounded.start(); bounded.accept();
    auto ctx = bounded.controllers[0]->view().context; net::Writer packet; CHECK(w::write({ctx, w::Leave{0}}, packet, error));
    packet = addressed(bounded.controllers[1]->view().local, bounded.controllers[0]->view().local, packet);
    auto errors = bounded.native[0].errors;
    for (int i = 0; i < 3; ++i) { net::Reader r(packet.data.data(), packet.size()); bounded.controllers[0]->on_message(1, r); CHECK(r.left() == 0); }
    CHECK(bounded.native[0].errors == errors); // No native/error callback during dispatch.
    bounded.step(); CHECK(bounded.native[0].errors > errors);
    CHECK(bounded.controllers[0]->view().phase == es::Phase::Ready);
    CHECK(bounded.controllers[0]->view().members.at(1).phase == es::Phase::Frozen);
}

static void test_rehost_authorship_and_document_hash()
{
    Party p(2); p.start(); p.accept(); std::string error;
    auto token = p.grant(1); CHECK(p.controllers[1]->submit(p.edit(1, object_a, position, Vec3{20, 0, 0}, token), error)); CHECK(p.revision(1)); p.release(1, token);
    token = p.grant(0); auto owner = p.controllers[0]->view().local;
    CHECK(p.controllers[0]->submit(p.edit(0, object_b, position, Vec3{30, 0, 0}, token), error)); CHECK(p.revision(2)); p.release(0, token);
    auto old_guest = p.controllers[1]->view().local; auto saved = p.controllers[0]->state();
    std::string a, b; CHECK(es::document_hash(saved.document, a, error));
    auto private_state = saved; private_state.decisions.clear(); private_state.decision_order.clear();
    CHECK(es::document_hash(private_state.document, b, error) && a == b);
    saved.document.epoch = 0x42; p.controllers[0]->end(); for (int i = 0; i < 10; ++i) p.step();
    p.start(saved); p.accept();
    CHECK(p.controllers[0]->view().local.generation > owner.generation);
    CHECK(p.controllers[1]->view().local.generation > old_guest.generation);
    token = p.grant(1); Request undo; undo.intent = Intent::Undo; undo.leases = token; undo.base_revision = 2;
    const auto prior_decisions = p.native[1].decisions;
    CHECK(p.controllers[1]->submit(undo, error));
    CHECK(p.spin([&] { return p.native[1].decisions > prior_decisions; })); // Rejected original ownership remains untouched.
    CHECK(p.controllers[0]->state().document.revision == 2);
    p.release(1, token);
    auto resumed = p.controllers[0]->state(); resumed.document.epoch = 0x24;
    p.controllers[0]->end(); for (int i = 0; i < 10; ++i) p.step(); p.start(resumed, owner); p.accept();
    CHECK(p.controllers[0]->view().local == owner);
}

static void preview_packet(Party& p,unsigned to,unsigned from,const w::Message& message,std::optional<Peer> envelope={})
{
    net::Writer packet;std::string error;CHECK(w::write(message,packet,error));
    packet=addressed(envelope.value_or(p.controllers[from]->view().local),p.controllers[to]->view().local,packet);
    net::Reader reader(packet.data.data(),packet.size());p.controllers[to]->on_message(uint8_t(from),reader);CHECK(reader.left()==0);
}

// Session still ACKs these real UDP replies; delaying only outside-frame editor
// dispatch proves transport ACKs alone cannot unlock a new lease acquisition.
static void test_release_quarantines_and_waits_for_reply()
{
    Party p(4);p.start();p.accept();std::string error;
    auto tokens=p.grant(1,{ScopeKind::Placement,object_a,{}});
    const auto old=tokens.front().token;p.hold_lease_replies[1]=true;
    auto renew=p.controllers[1]->renew_leases(tokens,3000,error);CHECK(renew);
    auto older_acquire=p.controllers[1]->request_leases({tokens.front().scope},3000,error);CHECK(older_acquire);
    CHECK(p.spin([&]{return p.held_lease_replies[1].size()==2;}));
    auto release=p.controllers[1]->release_leases(tokens,error);CHECK(release);
    CHECK(p.controllers[1]->view().local_leases.empty()); // Before any tick or ACK.
    CHECK(!p.controllers[1]->renew_leases(tokens,3000,error));
    CHECK(!p.controllers[1]->submit(p.edit(1,object_a,position,Vec3{90,0,0},tokens),error));
    CHECK(!p.controllers[1]->set_preview(object_a,{2,1},Pose{},error));
    auto acquire=p.controllers[1]->request_leases({tokens.front().scope},3000,error);CHECK(acquire);
    CHECK(p.spin([&]{return p.held_lease_replies[1].size()==3;}));
    CHECK(std::none_of(p.wire_lease_requests[1].begin(),p.wire_lease_requests[1].end(),[&](const auto& q){return q.serial==acquire;}));
    // An unrelated object remains independently editable while the release waits.
    auto disjoint=p.controllers[1]->request_leases({{ScopeKind::Placement,object_b,{}}},3000,error);CHECK(disjoint);
    CHECK(p.spin([&]{return p.held_lease_replies[1].size()==4;}));
    p.deliver_lease_reply(1,disjoint);p.step();
    CHECK(p.controllers[1]->view().local_leases.size()==1);
    CHECK(p.controllers[1]->view().local_leases.front().scope.object==object_b);
    // Neither older accepted reply can restore the quarantined token.
    p.deliver_lease_reply(1,older_acquire);p.deliver_lease_reply(1,renew);p.step();
    CHECK(p.controllers[1]->view().local_leases.size()==1);
    CHECK(p.controllers[1]->view().local_leases.front().scope.object==object_b);
    // Deferred TTL starts at emission, even when ACK dispatch took longer than TTL.
    p.origins[1]+=4000;p.step();CHECK(p.controllers[1]->view().local_leases.empty());
    p.deliver_lease_reply(1,release);p.step();
    CHECK(p.spin([&]{return p.held_lease_replies[1].size()==1;}));
    const auto granted=p.held_lease_replies[1].front().first;
    CHECK(granted.serial==acquire&&granted.result.accepted());
    CHECK(granted.result.leases.front().token!=old);
    p.deliver_lease_reply(1,acquire);p.step();
    CHECK(p.controllers[1]->view().local_leases.size()==1);
    auto renewed=p.controllers[1]->view().local_leases.front();
    CHECK(renewed.expires>p.origins[1]+uint32_t(net::now_ms()-p.begun)+2500);
    CHECK(renewed.token!=old);
    p.hold_lease_replies[1]=false;
    CHECK(p.controllers[1]->submit(p.edit(1,object_a,position,Vec3{90,0,0},{{renewed.scope,renewed.token}}),error));CHECK(p.revision(1));
    for(unsigned peer=0;peer<4;++peer)CHECK(p.controllers[peer]->view().phase==es::Phase::Ready);
}

static void test_host_release_quarantine_survives_view_refresh()
{
    Party p(2);p.start();p.accept();std::string error;
    auto tokens=p.grant(0,{ScopeKind::Placement,object_a,{}});const auto old=tokens.front().token;
    CHECK(p.controllers[0]->release_leases(tokens,error));CHECK(p.controllers[0]->view().local_leases.empty());
    CHECK(!p.controllers[0]->renew_leases(tokens,3000,error));
    CHECK(!p.controllers[0]->submit(p.edit(0,object_a,position,Vec3{7,0,0},tokens),error));
    CHECK(p.controllers[0]->request_leases({tokens.front().scope},3000,error));
    CHECK(p.spin([&]{return !p.controllers[0]->view().local_leases.empty();}));
    CHECK(p.controllers[0]->view().local_leases.front().token!=old);
    CHECK(p.controllers[0]->view().local_leases.size()==1);
    // Invalid all-or-nothing release must not implicitly republish its valid part.
    tokens={{p.controllers[0]->view().local_leases.front().scope,p.controllers[0]->view().local_leases.front().token},{{ScopeKind::Placement,object_b,{}},UINT64_C(999999)}};
    CHECK(p.controllers[0]->release_leases(tokens,error));CHECK(p.controllers[0]->view().local_leases.empty());
    for(int i=0;i<8;++i)p.step();CHECK(p.controllers[0]->view().local_leases.empty());
    CHECK(p.controllers[0]->request_leases({tokens.front().scope},3000,error));
    CHECK(p.spin([&]{return !p.controllers[0]->view().local_leases.empty();}));
    CHECK(p.controllers[0]->view().local_leases.front().token==tokens.front().token); // Explicit grant restores it safely.
    p.controllers[0]->freeze_local("Explicit native recovery required");p.step();
    CHECK(p.controllers[0]->view().phase==es::Phase::Frozen&&p.controllers[0]->view().local_leases.empty());
}

static void test_distinct_release_tokens_keep_both_quarantined()
{
    es::Config config;config.inbound_packets=1;config.reliable_window=1;config.packets_per_tick=1;Party p(2,config);p.start();p.accept();std::string error;
    auto held=p.grant(0,{ScopeKind::Placement,object_a,{}});const auto initial=held.front();
    p.after_own_decision=[&](unsigned author){if(author)return;
        CHECK(p.controllers[0]->release_leases(held,error));
        CHECK(p.controllers[0]->release_leases({{initial.scope,initial.token+UINT64_C(999999)}},error));
    };
    CHECK(p.controllers[0]->submit(p.edit(0,object_a,position,Vec3{100,0,0},held),error));
    CHECK(p.spin([&]{return p.native[0].decisions==1;}));
    // The one-packet budget left both Releases queued at this host refresh.
    CHECK(p.controllers[0]->view().local_leases.empty());
    CHECK(!p.controllers[0]->renew_leases(held,3000,error));
    p.after_own_decision={};for(int i=0;i<8;++i)p.step();
    CHECK(p.controllers[0]->view().local_leases.empty());
    CHECK(!p.grant(0,{ScopeKind::Placement,object_a,{}}).empty());
}

static void test_reordered_lease_requests_cannot_recreate_released_scope()
{
    Party p(4);p.start();p.accept();std::string error;
    auto tokens=p.grant(1,{ScopeKind::Placement,object_a,{}});const auto ctx=p.controllers[1]->view().context;
    w::LeaseRequest release{w::LeaseAction::Release,1002,0,{},tokens};
    preview_packet(p,0,1,{ctx,release});p.step();
    // These are first deliveries, not duplicates remembered in the host LRU.
    preview_packet(p,0,1,{ctx,w::LeaseRequest{w::LeaseAction::Acquire,1000,3000,{tokens.front().scope},{}}});
    preview_packet(p,0,1,{ctx,w::LeaseRequest{w::LeaseAction::Renew,1001,3000,{},tokens}});p.step();
    CHECK(p.spin([&]{return p.wire_lease_replies[1].size()>=4;}));
    for(auto serial:{1000u,1001u}){
        auto found=std::find_if(p.wire_lease_replies[1].begin(),p.wire_lease_replies[1].end(),[&](const auto& reply){return reply.serial==serial;});
        CHECK(found!=p.wire_lease_replies[1].end()&&found->result.code==Code::ExpiredOperation);
    }
    // Earlier unrelated acquisition is allowed; the serial fence is scoped.
    preview_packet(p,0,1,{ctx,w::LeaseRequest{w::LeaseAction::Acquire,999,3000,{{ScopeKind::Placement,object_b,{}}},{}}});p.step();
    CHECK(p.spin([&]{return std::any_of(p.wire_lease_replies[1].begin(),p.wire_lease_replies[1].end(),[](const auto& r){return r.serial==999;});}));
    auto disjoint=std::find_if(p.wire_lease_replies[1].begin(),p.wire_lease_replies[1].end(),[](const auto& r){return r.serial==999;});CHECK(disjoint->result.accepted());
    auto other=p.grant(2,tokens.front().scope);CHECK(other.front().token!=tokens.front().token);
    CHECK(p.controllers[2]->submit(p.edit(2,object_a,position,Vec3{40,0,0},other),error));CHECK(p.revision(1));
    p.release(2,other);
    preview_packet(p,0,1,{ctx,w::LeaseRequest{w::LeaseAction::Acquire,1003,3000,{tokens.front().scope},{}}});p.step();
    CHECK(p.spin([&]{return std::any_of(p.wire_lease_replies[1].begin(),p.wire_lease_replies[1].end(),[](const auto& r){return r.serial==1003;});}));
    auto fresh=std::find_if(p.wire_lease_replies[1].begin(),p.wire_lease_replies[1].end(),[](const auto& r){return r.serial==1003;});
    CHECK(fresh->result.accepted());auto fresh_tokens=std::vector<LeaseToken>{{fresh->result.leases.front().scope,fresh->result.leases.front().token}};
    preview_packet(p,0,1,{ctx,release});p.step(); // Retried old Release only returns its original decision.
    auto request=p.edit(1,object_a,position,Vec3{50,0,0},fresh_tokens);
    CHECK(p.controllers[1]->submit(request,error));CHECK(p.revision(2));
}

static void test_deferred_lease_bounds_and_lifecycle()
{
    Party p(2);p.start();p.accept();std::string error;
    auto tokens=p.grant(1);p.hold_lease_replies[1]=true;
    const auto release=p.controllers[1]->release_leases(tokens,error);CHECK(release);
    for(int i=0;i<63;++i)CHECK(p.controllers[1]->request_leases({Scope{}},3000,error));
    CHECK(!p.controllers[1]->request_leases({Scope{}},3000,error));
    CHECK(p.spin([&]{return p.held_lease_replies[1].size()==1;}));
    auto requests=p.wire_lease_requests[1].size();
    CHECK(p.controllers[0]->prepare_playtest(error));CHECK(p.spin([&]{return p.controllers[1]->view().phase==es::Phase::Preparing;}));
    p.deliver_lease_reply(1,release);p.step();CHECK(p.controllers[1]->view().local_leases.empty());
    CHECK(p.wire_lease_requests[1].size()==requests); // Revocation retired all deferred requests.
    CHECK(p.controllers[0]->cancel_playtest(error));CHECK(p.spin([&]{return p.controllers[1]->view().phase==es::Phase::Ready;}));
    p.hold_lease_replies[1]=false;auto fresh=p.grant(1);CHECK(fresh.front().token!=tokens.front().token);p.release(1,fresh);
    // End/rehost resets fences, while old-context replies stay harmless.
    fresh=p.grant(1);p.hold_lease_replies[1]=true;auto last=p.controllers[1]->release_leases(fresh,error);CHECK(last);
    CHECK(p.controllers[1]->request_leases({Scope{}},3000,error));CHECK(p.spin([&]{return !p.held_lease_replies[1].empty();}));
    auto next=p.controllers[0]->state();++next.document.epoch;p.controllers[0]->end();
    CHECK(p.spin([&]{return p.controllers[1]->view().phase==es::Phase::Idle;}));
    p.start(next);p.hold_lease_replies[1]=false;p.accept();p.deliver_lease_reply(1,last);p.step();CHECK(p.controllers[1]->view().local_leases.empty());
    CHECK(!p.grant(1).empty());
}

static void test_repeated_release_fences_past_cache_window()
{
    Party p(4);p.start();p.accept();std::string error;uint64_t first_release=0;std::vector<LeaseToken> first_tokens;
    for(unsigned round=0;round<128;++round){
        const auto author=round%4;auto tokens=p.grant(author,{ScopeKind::Placement,object_a,{}});
        CHECK(p.controllers[author]->submit(p.edit(author,object_a,position,Vec3{float(round+1000),0,0},tokens),error));CHECK(p.revision(round+1));
        CHECK(p.controllers[author]->renew_leases(tokens,3000,error));
        const auto released=p.controllers[author]->release_leases(tokens,error);CHECK(released);CHECK(p.controllers[author]->view().local_leases.empty());
        if(author==1&&!first_release){first_release=released;first_tokens=tokens;}
        CHECK(p.controllers[author]->request_leases({tokens.front().scope},3000,error));
        CHECK(p.spin([&]{return !p.controllers[author]->view().local_leases.empty();}));
        const auto fresh=p.controllers[author]->view().local_leases.front();CHECK(fresh.token!=tokens.front().token);
        p.release(author,{{fresh.scope,fresh.token}});
    }
    auto fresh=p.grant(1,{ScopeKind::Placement,object_a,{}});
    preview_packet(p,0,1,{p.controllers[1]->view().context,w::LeaseRequest{w::LeaseAction::Release,first_release,0,{},first_tokens}});p.step();
    CHECK(p.controllers[1]->submit(p.edit(1,object_a,position,Vec3{2000,0,0},fresh),error));CHECK(p.revision(129));
    for(unsigned peer=0;peer<4;++peer)CHECK(p.controllers[peer]->view().phase==es::Phase::Ready&&p.native[peer].errors==0);
}

static void test_transient_preview_authority_coalescing_and_lifecycle()
{
    Party p(4);p.start();p.accept();std::string error;
    auto leases=p.grant(1,{ScopeKind::Placement,object_a,{}});
    w::Presence presence;presence.selected=object_a;presence.act={2,1};presence.editing=true;
    CHECK(p.controllers[1]->set_presence(presence,error));
    CHECK(p.spin([&]{for(unsigned i=0;i<4;++i){auto found=p.controllers[i]->view().members.find(1);if(found==p.controllers[i]->view().members.end()||!(found->second.presence.ownership&w::kOwnSelection))return false;}return true;}));
    const auto document=p.controllers[0]->state().document;
    std::array<int,4> persists{};for(unsigned i=0;i<4;++i)persists[i]=p.native[i].persists;
    const auto pending=p.sessions[1].pending_to(0);
    for(unsigned i=1;i<=1000;++i)CHECK(p.controllers[1]->set_preview(object_a,{2,1},Pose{{float(i),2,3},{0,0,0,1},2},error));
    CHECK(p.sessions[1].pending_to(0)==pending);
    CHECK(p.spin([&]{for(unsigned i=0;i<4;++i){auto& member=p.controllers[i]->view().members.at(1);if(!member.preview||member.preview->value.pose.position.x!=1000)return false;}return true;}));
    const auto accepted=p.controllers[0]->view().members.at(1).preview->value;
    const auto expires=p.controllers[0]->view().members.at(1).preview->expires;
    for(unsigned i=0;i<4;++i){CHECK(p.controllers[i]->state().document==document);CHECK(p.native[i].document==document&&p.native[i].persists==persists[i]);}
    auto ctx=p.controllers[0]->view().context;
    auto stale=accepted;--stale.serial;stale.pose.position.x=-1;preview_packet(p,0,1,{ctx,stale});
    stale=accepted;stale.pose.position.x=-2;preview_packet(p,0,1,{ctx,stale});p.step();
    CHECK(p.controllers[0]->view().members.at(1).preview->value==accepted&&p.controllers[0]->view().members.at(1).preview->expires==expires);
    auto invalid=accepted;++invalid.serial;invalid.pose.position.x=-3;++invalid.lease.token;
    preview_packet(p,0,1,{ctx,invalid});p.step();CHECK(p.controllers[0]->view().members.at(1).preview->value==accepted);
    invalid=accepted;++invalid.serial;invalid.act={999,1};preview_packet(p,0,1,{ctx,invalid});p.step();CHECK(p.controllers[0]->view().members.at(1).preview->value==accepted);
    auto wrong=ctx;++wrong.epoch;invalid=accepted;++invalid.serial;preview_packet(p,0,1,{wrong,invalid});
    wrong=ctx;++wrong.revision;preview_packet(p,0,1,{wrong,invalid});
    auto old_cap=p.controllers[1]->view().local;++old_cap.generation;preview_packet(p,0,1,{ctx,invalid},old_cap);
    preview_packet(p,0,2,{ctx,invalid});p.step();CHECK(p.controllers[0]->view().members.at(1).preview->value==accepted);
    CHECK(p.controllers[0]->view().phase==es::Phase::Ready);
    p.release(1,leases);
    CHECK(p.spin([&]{for(unsigned i=0;i<4;++i)if(p.controllers[i]->view().members.at(1).preview)return false;return true;}));
    leases=p.grant(1,{ScopeKind::Placement,object_a,{}});
    preview_packet(p,0,1,{ctx,invalid});p.step();CHECK(!p.controllers[0]->view().members.at(1).preview);
    presence.editing=true;CHECK(p.controllers[1]->set_presence(presence,error));
    CHECK(p.spin([&]{return p.controllers[0]->view().members.at(1).presence.editing;}));
    CHECK(p.controllers[1]->set_preview(object_a,{2,1},Pose{{7,8,9},{0,0,0,1},1},error));
    CHECK(p.spin([&]{return p.controllers[0]->view().members.at(1).preview.has_value();}));
    CHECK(p.spin([&]{for(unsigned i=0;i<4;++i)if(p.controllers[i]->view().members.at(1).preview)return false;return true;},2000));
    CHECK(p.controllers[0]->state().document==document);
    CHECK(p.controllers[1]->set_preview(object_a,{2,1},Pose{{8,8,9},{0,0,0,1},1},error));
    CHECK(p.spin([&]{return p.controllers[2]->view().members.at(1).preview.has_value();}));
    p.controllers[1]->clear_preview();CHECK(p.spin([&]{for(unsigned i=0;i<4;++i)if(p.controllers[i]->view().members.at(1).preview)return false;return true;}));
    // A host clear with the same author serial wins over an active datagram
    // already queued this tick; reordering cannot resurrect that exact hint.
    auto latest=accepted;latest.serial=1004;latest.lease=leases.front();
    auto tombstone=latest;tombstone.active=false;tombstone.object={};tombstone.act={};tombstone.pose={};tombstone.lease={};
    preview_packet(p,2,0,{ctx,latest});preview_packet(p,2,0,{ctx,tombstone});p.step();CHECK(!p.controllers[2]->view().members.at(1).preview);
    preview_packet(p,2,0,{ctx,latest});p.step();CHECK(!p.controllers[2]->view().members.at(1).preview);
    CHECK(p.controllers[1]->set_preview(object_a,{2,1},Pose{{9,8,9},{0,0,0,1},1},error));
    CHECK(p.spin([&]{return p.controllers[0]->view().members.at(1).preview.has_value();}));
    presence.pending=true;presence.editing=false;CHECK(p.controllers[1]->set_presence(presence,error));
    CHECK(p.spin([&]{return p.controllers[0]->view().members.at(1).presence.pending&&!p.controllers[0]->view().members.at(1).preview;}));
    CHECK(p.controllers[1]->submit(p.edit(1,object_a,position,Vec3{30,40,50},leases),error));CHECK(p.revision(1));
    for(unsigned i=0;i<4;++i)CHECK(!p.controllers[i]->view().members.at(1).preview);
    invalid=accepted;invalid.serial=999999;invalid.lease=leases.front();preview_packet(p,0,1,{ctx,invalid});p.step();
    CHECK(!p.controllers[0]->view().members.at(1).preview); // Old revision, even with a live token.
    p.release(1,leases);
    leases=p.grant(1,{ScopeKind::Placement,object_a,{}});presence.pending=false;presence.editing=true;CHECK(p.controllers[1]->set_presence(presence,error));
    CHECK(p.spin([&]{return p.controllers[2]->view().members.at(1).presence.editing;}));
    p.tick_enabled[2]=false;CHECK(p.controllers[1]->set_preview(object_a,{2,1},Pose{{77,78,79},{0,0,0,1},1},error));
    CHECK(p.spin([&]{return p.controllers[0]->view().members.at(1).preview.has_value();}));
    const auto delayed_relay=p.controllers[0]->view().members.at(1).preview->value;
    CHECK(p.controllers[0]->prepare_playtest(error));CHECK(p.spin([&]{return p.controllers[3]->view().phase==es::Phase::Preparing;}));
    for(unsigned n=0;n<10;++n)p.step();p.tick_enabled[2]=true;
    CHECK(p.spin([&]{return p.controllers[2]->view().phase==es::Phase::Preparing;}));
    CHECK(!p.controllers[1]->set_preview(object_a,{2,1},Pose{},error));
    CHECK(p.controllers[0]->cancel_playtest(error));CHECK(p.spin([&]{for(unsigned i=0;i<4;++i)if(p.controllers[i]->view().phase!=es::Phase::Ready)return false;return true;}));
    CHECK(p.spin([&]{return p.controllers[2]->view().members.at(1).phase==es::Phase::Ready;}));
    leases=p.grant(1,{ScopeKind::Placement,object_a,{}});presence.pending=false;presence.editing=true;CHECK(p.controllers[1]->set_presence(presence,error));
    CHECK(p.spin([&]{return p.controllers[0]->view().members.at(1).presence.editing&&p.controllers[2]->view().members.at(1).presence.editing;}));
    preview_packet(p,2,0,{p.controllers[0]->view().context,delayed_relay});p.step();CHECK(!p.controllers[2]->view().members.at(1).preview);
    CHECK(p.controllers[1]->set_preview(object_a,{2,1},Pose{},error));
    CHECK(p.spin([&]{return p.controllers[2]->view().members.at(1).preview.has_value();}));
    p.sessions[1].leave("preview owner disconnect");CHECK(p.spin([&]{return !p.controllers[0]->view().members.contains(1)&&!p.controllers[2]->view().members.contains(1);}));
}
static void test_guest_end_preparing_reoffers_without_freezing_party()
{
    Party p(2);p.start();p.accept();std::string error;
    CHECK(p.controllers[0]->prepare_playtest(error));CHECK(p.spin([&]{return p.controllers[1]->view().phase==es::Phase::Preparing;}));
    const auto old=p.controllers[1]->view().local;p.controllers[1]->end();
    CHECK(p.spin([&]{return p.controllers[0]->view().phase==es::Phase::Ready&&p.controllers[1]->view().phase==es::Phase::Offered;}));
    CHECK(p.sessions[0].connected()&&p.sessions[1].connected());CHECK(p.controllers[0]->view().members.at(1).phase==es::Phase::Offered);
    CHECK(p.controllers[1]->view().local.generation>old.generation);
    auto lease=p.grant(0);CHECK(p.controllers[0]->submit(p.edit(0,object_a,position,Vec3{17,18,19},lease),error));
    CHECK(p.spin([&]{return p.controllers[0]->state().document.revision==1;}));p.release(0,lease);
    p.accept();CHECK(p.controllers[1]->state().document==p.controllers[0]->state().document);
    CHECK(p.controllers[0]->prepare_playtest(error));CHECK(p.spin([&]{return p.controllers[1]->view().phase==es::Phase::Preparing;}));
}

static void test_large_join_inventory_is_bounded_pinned_and_nonblocking()
{
    es::Config config;config.packets_per_tick=1;Party p(4,config);p.start();std::string error;
    CHECK(p.controllers[1]->accept_offer(error));CHECK(p.controllers[3]->accept_offer(error));
    CHECK(p.spin([&]{return p.controllers[0]->view().members.at(1).phase==es::Phase::Ready&&p.controllers[0]->view().members.at(3).phase==es::Phase::Ready;}));
    auto lease=p.grant(3,{ScopeKind::Placement,object_b,{}});
    auto& inventory=p.join_claims[2];inventory.base_hash=std::string(64,'a');inventory.base_size=1024;
    for(unsigned i=0;i<2048;++i)inventory.records.push_back({{i+1,9,1},1,std::string(64,'b')});
    std::vector<uint8_t> encoded;CHECK(w::encode_join(inventory,encoded,error));CHECK(encoded.size()>w::kPacketBytes&&encoded.size()<=w::kJoinBytes);
    const auto offered=p.controllers[2]->view().context;const auto capability=p.controllers[2]->view().local;
    p.small_join_resources=true;CHECK(p.controllers[2]->accept_offer(error));CHECK(p.native[2].imports==0);
    CHECK(p.controllers[3]->submit(p.edit(3,object_b,position,Vec3{19,20,21},lease),error));
    CHECK(p.spin([&]{return p.controllers[0]->state().document.revision==1;}));CHECK(p.received_joins.empty());
    CHECK(p.controllers[0]->update_resources(resource(1),1,error,false));
    CHECK(p.spin([&]{return p.controllers[2]->view().phase==es::Phase::Ready&&p.controllers[0]->view().members.at(2).phase==es::Phase::Ready;},7000));
    CHECK(p.received_joins.size()==1&&p.received_joins.front()==inventory);
    CHECK(p.pinned_join_resources.front()==resource(1));CHECK(p.native[2].imported_payloads.front()==resource(1,96));
    CHECK(p.native[1].imported_payloads.front()==resource(0));CHECK(p.native[3].imported_payloads.front()==resource(0));
    CHECK(p.controllers[2]->state().document==p.controllers[0]->state().document);
    // Baseline scheduling must retain the completed inventory seal. Delayed
    // duplicate Begin/End cannot invoke the callback twice or freeze the peer.
    w::TransferBegin begin{w::TransferKind::Join,1,uint32_t(encoded.size()),upd::sha256_hex(encoded)};
    preview_packet(p,0,2,{offered,begin},capability);preview_packet(p,0,2,{offered,w::TransferEnd{1}},capability);p.step();
    CHECK(p.controllers[0]->view().members.at(2).phase==es::Phase::Ready&&p.received_joins.size()==1);
    p.controllers[2]->end();CHECK(p.spin([&]{return p.controllers[2]->view().phase==es::Phase::Offered;}));
    p.join_claims[2]={};CHECK(p.controllers[2]->accept_offer(error));
    CHECK(p.spin([&]{return p.controllers[2]->view().phase==es::Phase::Ready;}));
    CHECK(p.native[2].imported_payloads.back()==resource(1));CHECK(p.received_joins.size()==1); // Full host cache was never replaced.
}
static void test_join_inventory_failures_preserve_party_and_draft()
{
    Party p(2);p.start();std::string error;const auto offered=p.controllers[1]->view().context;
    p.fail_join_prepare=true;CHECK(!p.controllers[1]->accept_offer(error));CHECK(p.controllers[1]->view().phase==es::Phase::Offered);
    CHECK(p.native[1].imports==0&&p.sessions[1].connected());p.fail_join_prepare=false;
    p.join_claims[1].base_hash=std::string(64,'c');p.join_claims[1].base_size=1;p.fail_join_resources=true;
    CHECK(p.controllers[1]->accept_offer(error));CHECK(p.spin([&]{return p.controllers[1]->view().phase==es::Phase::Frozen;}));
    CHECK(p.controllers[0]->view().phase==es::Phase::Ready&&p.controllers[0]->state().document==seed().document);
    CHECK(p.native[1].imports==0&&p.sessions[0].connected()&&p.sessions[1].connected());
    auto stale=offered;++stale.epoch;w::Join empty;preview_packet(p,0,1,{stale,empty});p.step();
    CHECK(p.native[1].imports==0&&p.received_joins.size()==1);
}

int main()
{
    CHECK(net::net_init()); test_release_quarantines_and_waits_for_reply(); test_host_release_quarantine_survives_view_refresh(); test_distinct_release_tokens_keep_both_quarantined(); test_reordered_lease_requests_cannot_recreate_released_scope(); test_deferred_lease_bounds_and_lifecycle(); test_repeated_release_fences_past_cache_window(); test_large_join_inventory_is_bounded_pinned_and_nonblocking(); test_join_inventory_failures_preserve_party_and_draft(); test_transient_preview_authority_coalescing_and_lifecycle(); test_guest_end_preparing_reoffers_without_freezing_party(); test_two_player_and_real_udp(); test_host_durable_coverage(); test_four_players_and_controls(); test_large_operation_transfer(); test_saved_snapshot_waits_for_native_gesture(); test_delete_revokes_lease_before_native_decision(0); test_delete_revokes_lease_before_native_decision(1); test_accepted_retry_keeps_new_gesture_ownership(0); test_accepted_retry_keeps_new_gesture_ownership(1); test_queued_begin_and_new_roster(); test_native_gesture_transaction_holds(); test_same_controller_host_to_guest(); test_delta_resources_keep_complete_late_join_cache(); test_failed_delta_staging_publishes_nothing(); test_close_waits_for_accepted_structural_commit(); test_settled_waits_for_join_but_excludes_failed_peer(); test_same_revision_saved_needs_explicit_applied_ack(); test_local_freeze_notifies_party_once(); test_deferred_baseline_and_fail_closed(); test_import_and_input_bounds(); test_rehost_authorship_and_document_hash(); test_real_udp_packet_adversity(2); test_real_udp_packet_adversity(4);
    std::printf("editor_session UDP/fake-native: %d checks, %d failures\n", checks, failures);
    return failures ? 1 : 0;
}
