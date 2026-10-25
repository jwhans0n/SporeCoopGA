#include "adventure_pause.h"

#include <algorithm>
#include <map>
#include <utility>

namespace adventure_pause {
namespace {
enum Packet : uint8_t { Intent = 164, Snapshot = 165 };
struct State { uint64_t serial = 0; bool paused = false; };
Ctx C;
uint64_t started_run, local_serial, revision;
bool local, sent, dirty, authoritative_hold, notified_hold, host_hold;
std::map<uint8_t, State> states;

uint64_t run() { return C.run ? C.run() : 0; }
bool running() { return C.sess && C.sess->connected() && C.running && C.running() && run(); }
bool host() { return C.sess && C.sess->role() == net::Role::Host; }
void put64(net::Writer& w, uint64_t n) { w.u32(uint32_t(n)); w.u32(uint32_t(n >> 32)); }
uint64_t get64(net::Reader& r) { uint64_t n = r.u32(); return n | (uint64_t(r.u32()) << 32); }
std::vector<uint8_t> members()
{
    auto ids = C.members ? C.members() : std::vector<uint8_t>{};
    const auto players = C.sess->players();
    ids.erase(std::remove_if(ids.begin(), ids.end(), [&](uint8_t id) {
        return (C.eligible && !C.eligible(id)) ||
            std::none_of(players.begin(), players.end(), [&](const auto& p) { return p.id == id; });
    }), ids.end());
    std::sort(ids.begin(), ids.end());
    ids.erase(std::unique(ids.begin(), ids.end()), ids.end());
    return ids;
}
bool member(uint8_t id)
{
    auto ids = members();
    return std::find(ids.begin(), ids.end(), id) != ids.end();
}
bool unanimous()
{
    const auto ids = members();
    return !ids.empty() && std::all_of(ids.begin(), ids.end(), [](uint8_t id) {
        auto it = states.find(id);
        return it != states.end() && it->second.serial && it->second.paused;
    });
}
void changed()
{
    // held() is queried for every native creature Update. Membership work and
    // its small vectors belong only here, after intent/roster changes or tick.
    if (host()) host_hold = active() && unanimous();
    const bool value = held();
    if (notified_hold != value) {
        notified_hold = value;
        if (C.changed) C.changed();
    }
}
void refresh_members()
{
    const auto ids = members();
    for (auto it = states.begin(); it != states.end();) {
        if (std::find(ids.begin(), ids.end(), it->first) == ids.end()) { it = states.erase(it); dirty = true; }
        else ++it;
    }
    for (uint8_t id : ids) if (!states.count(id)) { states[id] = {}; dirty = true; }
}
void publish()
{
    net::Writer w; w.u8(Snapshot); put64(w, started_run); put64(w, ++revision);
    w.u8(uint8_t(states.size()));
    for (const auto& [id, state] : states) { w.u8(id); put64(w, state.serial); w.u8(state.paused); }
    C.sess->broadcast_reliable(net::MSG_ADVENTURE, w);
    dirty = false;
}
void start_run()
{
    if (started_run == run()) return;
    reset(); started_run = run(); dirty = true;
    if (host()) refresh_members();
    local_intent(C.local_intent());
    // Native pause flags may predate the first ordinary frame of this run.
    C.changed();
}
} // namespace

bool init(Ctx context)
{
    C = std::move(context); reset();
    return C.sess && C.run && C.running && C.members && C.local_intent && C.changed;
}
void reset()
{
    const bool previous = notified_hold;
    started_run = local_serial = revision = 0;
    local = sent = dirty = authoritative_hold = notified_hold = host_hold = false;
    states.clear();
    if (previous && C.changed) C.changed();
}
bool active() { return running() && started_run == run(); }
bool local_requested() { return active() && local; }
bool held()
{
    if (!active() || !local) return false;
    if (host()) return host_hold;
    auto it = states.find(C.sess->my_id());
    // An old all-paused snapshot must not acknowledge a new menu visit.
    return authoritative_hold && it != states.end() && it->second.serial == local_serial && it->second.paused;
}
void local_intent(bool value)
{
    if (!active() || (local_serial && local == value)) return;
    local = value; ++local_serial; sent = false;
    if (host()) { states[C.sess->my_id()] = {local_serial, local}; dirty = true; }
    changed();
}
void tick()
{
    if (!running()) { if (started_run) reset(); return; }
    if (started_run != run()) start_run();
    else local_intent(C.local_intent());
    if (host()) {
        refresh_members();
        if (dirty) publish();
    } else if (!sent) {
        net::Writer w; w.u8(Intent); put64(w, started_run); put64(w, local_serial); w.u8(local);
        C.sess->send_reliable_to(0, net::MSG_ADVENTURE, w); sent = true;
    }
    changed();
}
void on_message(uint8_t from, net::Reader& r)
{
    uint8_t kind = r.u8(); uint64_t packet_run = get64(r), serial = get64(r);
    if (!r.ok() || !running() || packet_run != run() || !serial) return;
    if (host()) {
        if (kind != Intent || !from || !member(from)) return;
        uint8_t value = r.u8();
        if (!r.ok() || r.left() || value > 1) return;
        // Keepalive can commit Running and ACK this Intent before the first
        // native frame. Initialize only after validating current run and sender.
        start_run();
        if (serial <= states[from].serial) return;
        states[from] = {serial, value != 0}; dirty = true;
    } else {
        if (kind != Snapshot || from || (started_run == run() && serial <= revision)) return;
        uint8_t count = r.u8(); const auto roster = C.members();
        if (!count || count > net::kMaxPlayers) return;
        std::map<uint8_t, State> next;
        for (uint8_t i = 0; i < count; ++i) {
            uint8_t id = r.u8(); uint64_t selection = get64(r); uint8_t value = r.u8();
            if (!r.ok() || value > 1 || (!selection && value) || id >= net::kMaxPlayers ||
                std::find(roster.begin(), roster.end(), id) == roster.end() || next.count(id)) return;
            next[id] = {selection, value != 0};
        }
        if (!r.ok() || r.left()) return;
        auto own = next.find(C.sess->my_id());
        if (!next.count(0) || own == next.end()) return;
        start_run();
        if (own->second.serial > local_serial ||
            (own->second.serial == local_serial && own->second.paused != local)) return;
        states = std::move(next); revision = serial;
        // The host owns membership as well as readiness. A reliable pause
        // snapshot can be observed before the corresponding peer-leave event.
        authoritative_hold = std::all_of(states.begin(), states.end(), [](const auto& entry) {
            return entry.second.serial && entry.second.paused;
        });
    }
    changed();
}
std::string status()
{
    size_t count = 0;
    for (const auto& [id, state] : states) { (void)id; if (state.serial && state.paused) ++count; }
    return "party pause: run=" + std::to_string(started_run) + " local=" + std::to_string(local) +
        " paused=" + std::to_string(count) + "/" + std::to_string(states.size()) +
        " held=" + std::to_string(held()) + " revision=" + std::to_string(revision) + "\n";
}
} // namespace adventure_pause
