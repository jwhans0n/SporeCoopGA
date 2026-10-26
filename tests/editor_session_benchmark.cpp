// Opt-in transport benchmark. Real UDP, deliberately fake native callbacks.
// No game, renderer, profile, installed asset or native save is accessed.
#include "editor_session.h"
#include "editor_project.h"

#include <algorithm>
#include <array>
#include <chrono>
#include <cmath>
#include <fstream>
#include <functional>
#include <iomanip>
#include <iostream>
#include <map>
#include <memory>
#include <set>
#include <sstream>
#include <stdexcept>
#include <thread>

using namespace editor;
namespace es = editor_session;
namespace w = editor::wire;
using Clock = std::chrono::steady_clock;
static const auto process_start = Clock::now();
static double milliseconds() { return std::chrono::duration<double, std::milli>(Clock::now() - process_start).count(); }
static void require(bool yes, const std::string& why) { if (!yes) throw std::runtime_error(why); }
static std::string quoted(const std::string& value) {
    std::string out = "\"";
    for (char c : value) { if (c == '"' || c == '\\') out += '\\'; if (c == '\n') out += "\\n"; else if (c == '\r') out += "\\r"; else out += c; }
    return out + '"';
}
struct Profile {
    const char* name;
    double rtt_ms = 0;
    unsigned loss_per_mille = 0, reorder_per_mille = 0, duplicate_per_mille = 0;
    double rate_bytes_second = 0, budget_ms = 0;
};
struct Traffic {
    uint64_t datagrams = 0, bytes = 0, reliable_messages = 0, retransmitted_messages = 0, retransmitted_message_bytes = 0;
    uint64_t dropped = 0, duplicates = 0, reordered = 0, forwarded_bytes = 0;
};
static std::string traffic_json(const Traffic& t) {
    std::ostringstream o; o << "{\"source_datagrams\":" << t.datagrams << ",\"source_udp_payload_bytes\":" << t.bytes
      << ",\"reliable_message_sends\":" << t.reliable_messages << ",\"retransmitted_messages\":" << t.retransmitted_messages
      << ",\"retransmitted_message_bytes\":" << t.retransmitted_message_bytes << ",\"dropped_datagrams\":" << t.dropped
      << ",\"injected_duplicate_datagrams\":" << t.duplicates << ",\"reordered_deliveries\":" << t.reordered
      << ",\"forwarded_udp_payload_bytes\":" << t.forwarded_bytes << '}'; return o.str();
}
struct Trace {
    double submitted = 0, first_wire = -1, host_delivery = -1, host_apply = -1, decision_wire = -1, confirmed = -1;
    uint8_t author = 0;
};
using Traces = std::map<OperationId, Trace>;

class Proxy {
    struct Packet { net::Endpoint to; std::vector<uint8_t> bytes; unsigned direction; uint64_t ordinal; };
    net::UdpSocket socket_;
    net::Endpoint host_, client_;
    Profile profile_{"warmup"};
    Traces& traces_;
    uint8_t guest_slot_;
    std::multimap<double, Packet> pending_;
    std::array<double, 2> next_departure_{};
    std::array<uint64_t, 2> ordinals_{}, last_delivered_{};
    std::array<std::set<std::pair<uint32_t, uint16_t>>, 2> seen_;
    uint32_t random_;
    unsigned random(unsigned n) { random_ ^= random_ << 13; random_ ^= random_ >> 17; random_ ^= random_ << 5; return random_ % n; }
    void messages(const std::vector<uint8_t>& bytes, unsigned direction, bool delivered, double now) {
        if (!net::Connection::looks_valid(bytes.data(), bytes.size())) return;
        net::Reader r(bytes.data(), bytes.size()); r.u32(); r.u16(); const auto conn = r.u32(); r.skip(20); const auto count = r.u8();
        for (unsigned i = 0; i < count && r.ok(); ++i) {
            const auto type = r.u8(), flags = r.u8(); const auto size = r.u16(); const auto rel = flags & 1 ? r.u16() : 0;
            if (r.left() < size) return;
            if (!delivered && (flags & 1)) {
                auto& t = traffic[direction]; ++t.reliable_messages;
                if (!seen_[direction].insert({conn, uint16_t(rel)}).second) { ++t.retransmitted_messages; t.retransmitted_message_bytes += size + 6; }
            }
            if (type == 53 && size > 9) {
                net::Reader payload(r.cur(), size); Peer sender{payload.u8(), payload.u32()}; payload.u32();
                w::Message m; std::string error;
                if (w::read(payload, m, sender, direction == 0, error)) {
                    if (auto* q = std::get_if<Request>(&m.payload)) {
                        if (auto it = traces_.find(q->operation); it != traces_.end()) {
                            auto& stamp = delivered ? it->second.host_delivery : it->second.first_wire;
                            if (stamp < 0) stamp = now;
                        }
                    } else if (auto* d = std::get_if<Decision>(&m.payload); d && d->accepted() && !delivered) {
                        if (auto it = traces_.find(d->change->operation); it != traces_.end() && it->second.author == guest_slot_ && it->second.decision_wire < 0) it->second.decision_wire = now;
                    }
                }
            }
            r.skip(size);
        }
    }
public:
    std::array<Traffic, 2> traffic; // 0 host->guest, 1 guest->host, before injected duplication.
    explicit Proxy(uint16_t host_port, Traces& traces, uint32_t seed, uint8_t slot) : traces_(traces), guest_slot_(slot), random_(seed) {
        require(socket_.open(0), "proxy socket"); require(net::Endpoint::parse("127.0.0.1", host_port, host_), "proxy target");
    }
    uint16_t port() const { return socket_.local_port(); }
    bool empty() const { return pending_.empty(); }
    void configure(Profile p) { profile_ = p; next_departure_.fill(milliseconds()); }
    void reset_stats() { traffic = {}; }
    void pump() {
        double now = milliseconds();
        for (auto it = pending_.begin(); it != pending_.end() && it->first <= now;) {
            auto p = std::move(it->second); it = pending_.erase(it);
            auto& t = traffic[p.direction];
            if (p.ordinal < last_delivered_[p.direction]) ++t.reordered;
            last_delivered_[p.direction] = std::max(last_delivered_[p.direction], p.ordinal);
            require(socket_.send_to(p.to, p.bytes.data(), p.bytes.size()), "proxy send"); t.forwarded_bytes += p.bytes.size();
            messages(p.bytes, p.direction, true, now);
        }
        for (unsigned budget = 512; budget; --budget) {
            std::array<uint8_t, 2048> raw{}; net::Endpoint from; const int n = socket_.recv_from(from, raw.data(), raw.size());
            require(n >= 0, "proxy receive"); if (!n) break;
            const unsigned direction = from == host_ ? 0 : 1; if (direction) client_ = from;
            auto to = direction ? host_ : client_; require(to.port != 0, "unbound proxy peer");
            std::vector<uint8_t> bytes(raw.begin(), raw.begin() + n); auto& t = traffic[direction]; ++t.datagrams; t.bytes += n;
            messages(bytes, direction, false, now); const auto ordinal = ++ordinals_[direction];
            double depart = now;
            if (profile_.rate_bytes_second) { depart = std::max(now, next_departure_[direction]) + double(n) * 1000 / profile_.rate_bytes_second; next_departure_[direction] = depart; }
            if (random(1000) < profile_.loss_per_mille) { ++t.dropped; continue; }
            double arrival = depart + profile_.rtt_ms / 2;
            if (random(1000) < profile_.reorder_per_mille) arrival += profile_.rtt_ms * .75 + 5;
            require(pending_.size() < 4096, "bounded proxy queue overflow"); pending_.emplace(arrival, Packet{to, bytes, direction, ordinal});
            if (random(1000) < profile_.duplicate_per_mille) { ++t.duplicates; pending_.emplace(arrival + 2, Packet{to, std::move(bytes), direction, ordinal}); }
        }
    }
};

static State initial_state() {
    State s; auto& d = s.document; d.project[0] = 0xAC; d.epoch = 0x1001; d.next_id = 1000;
    d.classes.insert({1, 1}); d.acts.insert({2, 1});
    for (uint64_t id = 3; id < 515; ++id) { Placement p; p.object = {id, 1}; p.class_id = {1, 1}; p.act_id = {2, 1}; d.placements[p.object] = p; }
    return s;
}
static std::vector<uint8_t> asset(size_t size, uint8_t tag = 0xA5) { return std::vector<uint8_t>(size, tag); }
class Harness {
public:
    using ResourceValidator = std::function<bool(unsigned, const std::vector<uint8_t>&, std::string&)>;
    unsigned players;
    std::array<net::Session, 4> sessions;
    std::array<std::unique_ptr<es::Controller>, 4> controllers;
    std::array<std::unique_ptr<Proxy>, 4> proxies;
    std::array<Document, 4> native;
    std::array<unsigned, 4> applies{}, imports{};
    Traces traces;
    std::vector<std::string> errors;
    ResourceValidator resource_validator;
    explicit Harness(unsigned count, size_t full_bytes, es::Config config = {}, std::vector<uint8_t> payload = {}, ResourceValidator validator = {}) : players(count), resource_validator(std::move(validator)) {
        for (unsigned p = 0; p < players; ++p) {
            sessions[p].build = 0xDC04AEE5; sessions[p].timeout_ms = 60000; native[p] = initial_state().document;
            es::Callbacks c;
            c.host_hooks.apply = [&, p](const Document& before, const Document& after, const Change& change, std::string&) {
                require(native[p] == before, "fake host before-state divergence"); native[p] = after; ++applies[p];
                if (auto it = traces.find(change.operation); it != traces.end()) it->second.host_apply = milliseconds(); return true;
            };
            c.host_hooks.rollback = [&, p](const Document& before, const Document&, const Change&, std::string&) { native[p] = before; return true; };
            c.host_hooks.persist = [](const State&, const Change&, std::string&) { return true; };
            c.apply_resources = [&, p](const auto& bytes, const auto&, std::string& error) { if (resource_validator && !resource_validator(p, bytes, error)) return false; ++imports[p]; return true; };
            c.apply_baseline = [&, p](const State& s, std::string&) { native[p] = s.document; return true; };
            c.apply_committed = [&, p](const Document& before, const Document& after, const Change&, std::string&) { require(native[p] == before, "fake guest before-state divergence"); native[p] = after; ++applies[p]; return true; };
            c.rollback_committed = c.host_hooks.rollback; c.persist_guest = [](const State&, const Change&, std::string&) { return true; };
            c.decision = [&, p](const Decision& decision) {
                if (!decision.accepted()) { errors.push_back(decision.reason); return; }
                auto it = traces.find(decision.change->operation);
                if (it != traces.end() && it->second.author == p) { require(it->second.confirmed < 0, "duplicate confirmation callback"); it->second.confirmed = milliseconds(); }
            };
            c.error = [&](const std::string& e) { errors.push_back(e); };
            controllers[p] = std::make_unique<es::Controller>(sessions[p], c, config);
            sessions[p].cb.custom = [&, p](uint8_t type, uint8_t from, net::Reader& r) { if (type == 53) controllers[p]->on_message(from, r); };
            sessions[p].cb.player_join = [&, p](const net::PlayerInfo& q) { controllers[p]->on_join(q.id); };
            sessions[p].cb.player_leave = [&, p](uint8_t id, uint8_t) { controllers[p]->on_leave(id); };
        }
        std::string error; require(sessions[0].host(0, "bench host", error), error);
        for (unsigned p = 1; p < players; ++p) { proxies[p] = std::make_unique<Proxy>(sessions[0].port(), traces, 0xBEEF1234u + p * 101, uint8_t(p)); require(sessions[p].join("127.0.0.1:" + std::to_string(proxies[p]->port()), "bench " + std::to_string(p), error), error); }
        wait([&] { return sessions[0].players().size() == players && std::all_of(sessions.begin() + 1, sessions.begin() + players, [](auto& s) { return s.connected(); }); }, 10000, "session join");
        for (unsigned p = 0; p < players; ++p) require(sessions[p].my_id() == p, "unexpected benchmark slot assignment");
        require(controllers[0]->start(initial_state(), payload.empty() ? asset(full_bytes) : std::move(payload), "portable latency fixture", error), error);
        wait([&] { for (unsigned p = 1; p < players; ++p) if (controllers[p]->view().phase != es::Phase::Offered) return false; return true; }, 10000, "editor offer");
        for (unsigned p = 1; p < players; ++p) require(controllers[p]->accept_offer(error), error);
        wait([&] { for (unsigned p = 1; p < players; ++p) if (controllers[p]->view().phase != es::Phase::Ready || controllers[0]->view().members.at(p).phase != es::Phase::Ready) return false; return true; }, 15000, "baseline");
    }
    void step() {
        for (unsigned p = 1; p < players; ++p) proxies[p]->pump();
        for (unsigned p = 0; p < players; ++p) sessions[p].update();
        for (unsigned p = 1; p < players; ++p) proxies[p]->pump();
        const auto now = uint64_t(milliseconds()); for (unsigned p = 0; p < players; ++p) controllers[p]->tick(now);
        std::this_thread::sleep_for(std::chrono::milliseconds(1));
    }
    void wait(const std::function<bool()>& predicate, double timeout, const std::string& stage) {
        const auto until = milliseconds() + timeout;
        while (milliseconds() < until) { step(); if (!errors.empty()) throw std::runtime_error(errors.front()); if (predicate()) return; }
        std::ostringstream diagnostic; diagnostic << "benchmark timeout during " << stage;
        for (unsigned p = 1; p < players; ++p) diagnostic << "; peer " << p << " phase=" << unsigned(controllers[p]->view().phase) << " leases=" << controllers[p]->view().local_leases.size() << " pending-host=" << sessions[0].pending_to(p) << " pending-guest=" << sessions[p].pending_to(0) << " proxy-empty=" << proxies[p]->empty();
        throw std::runtime_error(diagnostic.str());
    }
    void settle() { wait([&] { for (unsigned p = 1; p < players; ++p) if (sessions[0].pending_to(p) || sessions[p].pending_to(0)) return false; return true; }, 5000, "settle reliable ACKs"); }
    void configure(Profile profile) {
        settle(); for (unsigned p = 1; p < players; ++p) proxies[p]->configure(profile);
        auto until = milliseconds() + 2500; while (milliseconds() < until) step(); settle();
    }
    void leases(unsigned samples) {
        std::string error;
        // Each acquire/reply stays below the single-message UDP payload bound.
        for (unsigned first = 0; first < samples; first += 5) {
            for (unsigned p = 1; p < players; ++p) {
                std::vector<Scope> scopes; for (unsigned j = first; j < std::min(first + 5, samples); ++j) scopes.push_back({ScopeKind::Placement, {100 + (p - 1) * samples + j, 1}, {}});
                require(controllers[p]->request_leases(scopes, 30000, error) != 0, "acquire lease batch: " + error);
            }
            wait([&] { for (unsigned p = 1; p < players; ++p) if (controllers[p]->view().local_leases.size() != std::min(first + 5, samples)) return false; return true; }, 10000, "lease batch " + std::to_string(first));
        }
        settle();
    }
    void submit(unsigned author, unsigned index, unsigned samples) {
        auto& controller = *controllers[author]; const auto& d = controller.state().document;
        Request r; r.operation = {controller.view().local, index + 1, d.epoch}; r.base_revision = d.revision; r.summary = "Measured placement gesture";
        const ObjectId object{100 + (author - 1) * samples + index, 1};
        Mutation m; m.target = object; m.field = {Field::Position, 0}; m.expected = 0; m.value = Vec3{float(index + 1), float(author), 0}; r.mutations.push_back(m);
        for (const auto& l : controller.view().local_leases) if (l.scope.object == object) r.leases.push_back({l.scope, l.token});
        require(r.leases.size() == 1, "measured gesture ownership missing");
        traces[r.operation] = Trace{milliseconds(), -1, -1, -1, -1, -1, uint8_t(author)};
        std::string error; require(controller.submit(std::move(r), error), error);
    }
};

static double percentile(std::vector<double> values, double p) {
    if (values.empty()) return 0; std::sort(values.begin(), values.end()); return values[size_t(std::ceil(values.size() * p)) - 1];
}
static std::string distribution(const std::vector<double>& values) {
    std::ostringstream o; o << std::fixed << std::setprecision(3) << "{\"samples\":" << values.size() << ",\"p50_ms\":" << percentile(values, .5)
       << ",\"p95_ms\":" << percentile(values, .95) << ",\"max_ms\":" << percentile(values, 1) << '}'; return o.str();
}
static std::string run(unsigned players, Profile profile, bool contend, bool use_delta, size_t binary_root_bytes = 0) {
    std::cout << "starting " << profile.name << " " << players << " peers\n" << std::flush;
    constexpr unsigned samples = 30; size_t full_bytes = 1u << 20, delta_bytes = 256u << 10;
    std::vector<uint8_t> initial_payload, refreshed_payload, encoded_delta, expected_root;
    std::array<editor_project::Resources, 4> native_resources;
    Harness::ResourceValidator validator; double encode_ms = 0;
    if (binary_root_bytes) {
        std::string error; editor_project::Resources resources;
        resources.root = {42, 0x366A930D, 0x408A0000};
        uint32_t marker = 0; for (const auto& [object, unused] : initial_state().document.placements) { (void)unused; resources.bindings.push_back({object, marker++}); }
        auto base = asset(binary_root_bytes); uint32_t rng = 0x45C0FFEE;
        for (auto& b : base) { rng ^= rng << 13; rng ^= rng >> 17; rng ^= rng << 5; b = uint8_t(rng); }
        resources.base_root = std::make_shared<const std::vector<uint8_t>>(base);
        require(net::adventure_content::make_bundle(resources.root, {{resources.root, base}}, resources.bundle, error), error);
        require(editor_project::encode_resources(resources, initial_payload, error), error);
        expected_root = base; for (size_t pos = 1024; pos < expected_root.size(); pos += 8192) expected_root[pos] ^= 0x97;
        expected_root.insert(expected_root.begin() + 777, {6, 7, 8});
        require(net::adventure_content::make_bundle(resources.root, {{resources.root, expected_root}}, resources.bundle, error), error);
        const auto encoding_started = milliseconds();
        require(editor_project::encode_resources(resources, refreshed_payload, error), error);
        require(editor_project::encode_delta_resources(resources, encoded_delta, error), error);
        encode_ms = milliseconds() - encoding_started; full_bytes = refreshed_payload.size(); delta_bytes = encoded_delta.size();
        // Keep the initial base owned independently of the temporary encoder.
        for (auto& r : native_resources) r.root = resources.root;
        auto initial_base = resources.base_root;
        validator = [&, initial_base](unsigned peer, const std::vector<uint8_t>& bytes, std::string& e) {
            editor_project::Resources imported;
            if (!editor_project::decode_resources(bytes, imported, e, native_resources[peer].base_root)) return false;
            const auto& wanted = native_resources[peer].base_root ? expected_root : *initial_base;
            for (const auto& record : imported.bundle.records) if (record.key == imported.root) {
                if (record.data != wanted) { e = "Binary delta benchmark reconstructed different opaque root bytes"; return false; }
                native_resources[peer] = std::move(imported); return true;
            }
            e = "Binary delta benchmark root missing"; return false;
        };
    }
    Harness h(players, full_bytes, {}, std::move(initial_payload), std::move(validator)); h.configure(profile); h.leases(samples);
    for (unsigned p = 1; p < players; ++p) h.proxies[p]->reset_stats();
    const auto began = milliseconds(); std::string error;
    if (contend) require(h.controllers[0]->update_resources(binary_root_bytes ? std::move(refreshed_payload) : asset(full_bytes, 0xB6), 0, error, true,
        use_delta ? (binary_root_bytes ? std::move(encoded_delta) : asset(delta_bytes, 0xC7)) : std::vector<uint8_t>{}), error);
    for (unsigned i = 0; i < samples * (players - 1); ++i) {
        const auto due = began + i * 25.; while (milliseconds() < due) h.step();
        h.submit(1 + i % (players - 1), i / (players - 1), samples); h.step();
    }
    h.wait([&] { for (const auto& [op, trace] : h.traces) { (void)op; if (trace.confirmed < 0) return false; } return true; }, 20000, "gesture confirmations"); h.settle();
    const auto ended = milliseconds(); std::vector<double> latency, queue, native_wait, decision_queue, confirmation_delivery;
    for (const auto& [op, t] : h.traces) {
        (void)op; require(t.first_wire >= 0 && t.host_delivery >= 0 && t.host_apply >= 0 && t.decision_wire >= 0, "missing measured wire trace");
        latency.push_back(t.confirmed - t.submitted); queue.push_back(t.first_wire - t.submitted);
        native_wait.push_back(t.host_apply - t.host_delivery); decision_queue.push_back(t.decision_wire - t.host_apply);
        confirmation_delivery.push_back(t.confirmed - t.decision_wire);
    }
    bool converged = true; for (unsigned p = 0; p < players; ++p) converged &= h.controllers[p]->state().document == h.controllers[0]->state().document && h.applies[p] == h.traces.size();
    require(converged, "native fake/document convergence or exactly-once count failed");
    std::ostringstream out; out << std::fixed << std::setprecision(3) << "{\"profile\":" << quoted(profile.name) << ",\"players\":" << players << ",\"placements\":512,\"samples_per_guest\":" << samples
        << ",\"native_callbacks\":\"fake and memory-only\",\"full_resource_bytes\":" << full_bytes << ",\"broadcast_resource_bytes\":" << (contend ? (use_delta ? delta_bytes : full_bytes) : 0)
        << ",\"binary_root_base_bytes\":" << binary_root_bytes << ",\"binary_root_full_and_delta_encode_ms\":" << encode_ms
        << ",\"binary_root_reconstruction_verified\":" << (binary_root_bytes ? "true" : "false")
        << ",\"duration_ms\":" << ended - began << ",\"modeled_rtt_ms\":" << profile.rtt_ms << ",\"configured_loss_percent\":" << profile.loss_per_mille / 10.
        << ",\"configured_reorder_percent\":" << profile.reorder_per_mille / 10. << ",\"configured_duplicate_percent\":" << profile.duplicate_per_mille / 10.
        << ",\"per_direction_per_peer_bits_second\":" << profile.rate_bytes_second * 8 << ",\"request_to_own_confirmation\":" << distribution(latency)
        << ",\"request_queue_to_first_udp\":" << distribution(queue) << ",\"host_delivery_to_fake_apply\":" << distribution(native_wait)
        << ",\"host_fake_apply_to_first_decision_udp_for_author\":" << distribution(decision_queue)
        << ",\"first_decision_udp_to_own_confirmation\":" << distribution(confirmation_delivery)
        << ",\"p95_budget_ms\":" << profile.budget_ms << ",\"budget_pass\":" << (profile.budget_ms ? (percentile(latency, .95) <= profile.budget_ms ? "true" : "false") : "null")
        << ",\"converged_exactly_once\":true,\"peers\":[";
    for (unsigned p = 1; p < players; ++p) {
        if (p != 1) out << ','; std::vector<double> own; for (const auto& [op, t] : h.traces) { (void)op; if (t.author == p) own.push_back(t.confirmed - t.submitted); }
        out << "{\"slot\":" << p << ",\"confirmation\":" << distribution(own) << ",\"host_to_guest\":" << traffic_json(h.proxies[p]->traffic[0]) << ",\"guest_to_host\":" << traffic_json(h.proxies[p]->traffic[1]) << '}';
    }
    out << "]}"; std::cout << profile.name << " " << players << " peers: p95=" << percentile(latency, .95) << "ms budget=" << profile.budget_ms << "ms\n" << std::flush; return out.str();
}
static std::string quota_audit() {
    es::Config config; config.transfer_bytes = 512u << 10;
    Harness h(2, 256u << 10, config); h.settle(); std::string error;
    require(h.controllers[0]->update_resources(asset(256u << 10, 0x17), 0, error), "queue quota audit old full: " + error);
    const bool blocked = !h.controllers[0]->update_resources(asset(256u << 10, 0x18), 0, error, true, asset(1024));
    const auto rejection = error; require(blocked, "queued full transfer must count toward aggregate budget");
    h.wait([&] { return h.imports[1] == 2; }, 10000, "quota audit old full drain"); h.settle();
    require(h.controllers[0]->update_resources(asset(256u << 10, 0x18), 0, error, true, asset(1024)), "idle full cache must not double-count transfer budget: " + error);
    return "{\"scaled_real_udp_check\":true,\"transfer_budget_bytes\":524288,\"old_queued_full_bytes\":262144,\"new_full_bytes\":262144,\"delta_bytes\":1024,\"blocked_while_old_full_queued\":true,\"accepted_after_old_full_drained\":true,\"rejection\":" + quoted(rejection) +
        ",\"default_224mib_implication\":\"Idle cached full resources do not count. If an old 128MiB full baseline is still queued, the next full plus delta must fit the remaining 96MiB. Admission resumes when the old transfer drains.\"}";
}
int main(int argc, char** argv) {
    try {
        require(net::net_init(), "network initialization"); std::vector<std::string> cases;
        const Profile lan{"lan", 10, 0, 0, 0, 0, 200};
        const Profile wan{"wan_100ms_1pct", 100, 10, 20, 2, 0, 500};
        const Profile stress{"wan_100ms_10pct_stress", 100, 100, 100, 10, 0, 0};
        const Profile constrained{"wan_asset_2mbit_delta", 100, 10, 20, 2, 250000, 750};
        auto full = constrained; full.name = "wan_asset_2mbit_full_comparison";
        const std::string mode = argc > 2 ? argv[2] : "all";
        require(mode == "all" || mode == "assets" || mode == "quota" || mode == "compressed", "mode must be all, assets, compressed or quota");
        if (mode == "compressed") for (unsigned players : {2u, 4u}) {
            auto compressed = constrained; compressed.name = "wan_asset_2mbit_binary_delta_256k"; cases.push_back(run(players, compressed, true, true, 256u << 10));
            compressed.name = "wan_asset_2mbit_binary_delta_1m"; cases.push_back(run(players, compressed, true, true, 1u << 20));
        }
        else if (mode != "quota") for (unsigned players : {2u, 4u}) {
            if (mode == "all") { cases.push_back(run(players, lan, false, true)); cases.push_back(run(players, wan, false, true)); cases.push_back(run(players, stress, false, true)); }
            cases.push_back(run(players, constrained, true, true)); cases.push_back(run(players, full, true, false));
        }
        const auto quota = quota_audit();
        std::ofstream output(argc > 1 ? argv[1] : "out/editor-core/editor-latency.json"); require(bool(output), "benchmark output path");
        output << "{\"scope\":\"Portable real-UDP Controller benchmark; FAKE NATIVE, not game FPS or native acceptance\",\"platform\":\"Linux host C++20 -O2, real loopback UDP, 1ms controller polling\","
          "\"method\":\"One run per case, deterministic per-peer PRNG seed 0xBEEF1234+slot*101; nearest-rank p95; 30 independent leased placement gestures per guest at 25ms aggregate intervals; warm baseline/leases excluded; source UDP payload bytes include Connection headers, ACKs and retransmissions, excluding IP/UDP headers. Retransmission counts are repeated reliable IDs per connection/direction observed BEFORE injected proxy duplication. 2Mbps is symmetric per-peer serialization, not aggregate host uplink. All asset-contention commands are pure transforms while a same-revision resource refresh is queued ahead of canonical decisions. Includes ACK traffic through reliable queue drain; normal keepalives may remain in flight. Fake persistence is memory-only, so results do not include disk/native latency.\","
          "\"resource_sizes_are_synthetic\":true,\"warmup_ms_per_profile\":2500,\"cases\":[";
        for (size_t i = 0; i < cases.size(); ++i) { if (i) output << ','; output << cases[i]; }
        output << "],\"resource_staging_quota_audit\":" << quota << "}\n"; output.flush(); require(bool(output), "benchmark output write"); return 0;
    } catch (const std::exception& e) { std::cerr << "BENCHMARK FAILED: " << e.what() << '\n'; return 1; }
}
