// Coop session: host (server for up to 3 guests) or client. Game-agnostic.
#pragma once

#include <functional>
#include <memory>
#include <string>
#include <vector>

#include "connection.h"
#include "protocol.h"
#include "transport.h"

namespace net {

enum class Role { None, Host, Client };

// BUG-0204: how Session starts the thread that looks a host name up. The default is a detached
// std::thread; coop.dll sets one that keeps the DLL loaded until the thread is done (hot reload).
using SpawnFn = bool (*)(void (*fn)(void*), void* arg);
extern SpawnFn g_spawn_thread;

struct Peer {
    uint8_t id = 0xFF; // player id (host side: guest id; client side: 0 = host)
    std::string name;
    bool accepted = false;
    bool bye = false; // said goodbye: dropped in update()
    uint32_t caps = 0;      // its HELLO / WELCOME caps (BUG-0219); 0 = an older SporeCoop
    std::string version;    // CAP_VERSION text ("" = unknown: an older SporeCoop)
    bool has_token = false; // host: its HELLO carried a rejoin token (BUG-0235)
    uint64_t token = 0;
    Connection conn;
    // Extra send-only pumps share this update's existing bounded allowance.
    unsigned send_packets = 0, send_burst = 0;
};

class Session {
public:
    struct Callbacks {
        std::function<void(const std::string&)> log;
        std::function<void()> connected;                       // client: welcome received
        std::function<void(const std::string&)> disconnected;  // client: lost / rejected / host left
        std::function<void(const PlayerInfo&)> player_join;
        std::function<void(uint8_t id, uint8_t reason)> player_leave;
        std::function<void(const PlayerState&)> player_state;
        std::function<void(uint8_t id, const std::string& text)> chat;
        std::function<void(uint8_t type, uint8_t from, Reader& payload)> custom; // other msgs
        // a player (host: a guest; guest: the host, id 0) runs an older SporeCoop without the HELLO
        // extension: long chat lines reach them cut, messages above one packet not at all (BUG-0219)
        std::function<void(uint8_t id, const std::string& name)> old_peer;
    } cb;

    uint32_t build = 0;      // exe build id; mismatch = reject
    std::string version;     // ours for HELLO / WELCOME (CAP_VERSION): "coop X, SporeKit Y, <game build> <exe hash>"
    // BUG-0141: names are unique in a session (the host appends " 2", " 3" to a taken name; the
    // guest takes its name from the roster). Space tables keyed by name mixed up two players with
    // the same (default) name. false = the old behaviour (tests: coop.corefix unique_names 0)
    bool unique_names = true;
    // a new player with the name of an entry silent for 2 s replaces it (a rejoin after a crash)
    // instead of getting "name 2". false = rename (tests: coop.fix core.rejoin_name off, BUG-0209)
    bool rejoin_by_name = true;
    // BUG-0235: the rejoin token this game sends in its HELLO (0 = none). The same game after a crash
    // sends the same token (coop.dll keeps it in a file until a clean leave); the host replaces a
    // silent entry only on an equal token (net::g_fix.rejoin_token), never by the name alone
    uint64_t rejoin_token = 0;
    bool send_caps = true; // not a lab switch: test seam, false = HELLO / WELCOME without the extension (0.7.2 shape)
    // host: its world id (save CRC in Cell / Creature / Tribe / Civ, else 0), sent in WELCOME; coop.cpp
    // sets it every frame (host_world.h). Guest: welcome_world() is what the host sent (0 = unknown
    // or an older host), until leave(). The field stays where it is in Welcome (BUG-0231)
    uint32_t world = 0;
    uint32_t welcome_world() const { return welcome_world_; }
    uint32_t timeout_ms = 30000; // the game can block for a long time while saving/loading
    std::string lobby_info;  // host: shown to players looking for games on the network

    bool host(uint16_t port, const std::string& name, std::string& err);
    bool join(const std::string& addr, const std::string& name, std::string& err);
    void leave(const char* why = "left");
    void update(); // call every frame: receive, timeouts, send
    // Main-thread send only, with a fresh monotonic timestamp. Never receives
    // or invokes callbacks, and cannot flush a replaced transport. Shares the
    // most recent update's packet allowance rather than starting another one.
    bool flush_pending(uint64_t expected_transport);

    void send_state(PlayerState st);              // own state
    void send_chat(const std::string& text);
    void broadcast_reliable(uint8_t type, const Writer& w, int except_id = -1);
    bool send_reliable_to(uint8_t id, uint8_t type, const Writer& w); // host: guest id, client: 0
    // player event (reliable): client -> host, host relays to the other guests
    void send_event(uint8_t type, const Writer& w);
    size_t pending_to(uint8_t id) const;
    bool send_unreliable_to(uint8_t id, uint8_t type, const Writer& w);
    // the connection to a player (host: a guest, guest: the host, id 0) for the support statistics
    // (diag_stats.cpp), nullptr when there is none; valid until the next update()
    Connection* conn_to(uint8_t id);

    // Process-local transport identity. Never sent on the wire or reused after
    // a successful Host/Join or an active Leave, including failed restarts.
    uint64_t transport_generation() const { return transport_generation_; }
    ConnectionAccounting retained_accounting() const {
        ConnectionAccounting a;
        for (const auto& peer : peers_) {
            const auto p = peer->conn.retained_accounting();
            a.connections += p.connections; a.sent_packets += p.sent_packets; a.sent_id_capacity += p.sent_id_capacity;
            a.reliable_out += p.reliable_out; a.reliable_in += p.reliable_in; a.unreliable_out += p.unreliable_out;
            a.unreliable_slots_capacity += p.unreliable_slots_capacity; a.payload_size += p.payload_size; a.payload_capacity += p.payload_capacity;
        }
        return a;
    }
    Role role() const { return role_; }
    uint8_t my_id() const { return my_id_; }
    bool connected() const { return role_ == Role::Host || (role_ == Role::Client && welcomed_); }
    std::string name_of(uint8_t id) const;
    std::vector<PlayerInfo> players() const; // including self
    std::string status() const;
    uint16_t port() const { return tr_->local_port(); }
    // round trip time to a player in ms (host: any guest, guest: the host), -1 if unknown
    int rtt_of(uint8_t id) const;
    // the link to a player (host: any guest, guest: the host) for the support trace: no address
    struct Link {
        int rtt_ms = -1;
        uint32_t sent = 0, resends = 0, silent_ms = 0, pending = 0;
    };
    bool link_of(uint8_t id, Link& out) const;
    std::string host_address() const; // guest: "ip:port" of the host
    bool resolving() const { return lookup_ != nullptr; } // guest: the host name is being looked up
    struct Lookup; // a name lookup on a worker thread (BUG-0204)
    static constexpr uint32_t kLookupTimeoutMs = 20000;
    size_t peer_count() const { return peers_.size(); } // host: accepted and half-open peers

    // test seam (tests/fuzz_decoders.cpp): another network and clock; nullptr = UDP and now_ms()
    void set_transport(Transport* t) { tr_ = t ? t : &udp_; }
    uint32_t (*clock)() = nullptr;

private:
    void handle_packet(Peer& p, const uint8_t* data, size_t n, uint32_t now);
    void handle_msg(Peer& p, uint8_t type, Reader& r);
    void flush(uint32_t now);
    Peer* find_peer(const Endpoint& e);
    void drop_peer(size_t idx, uint8_t reason);
    void send_chat_to(Peer& p, const Chat& c);
    uint8_t alloc_id() const;
    bool retired(uint32_t conn) const;
    void start_hello(const Endpoint& ep, uint32_t now);
    bool poll_lookup(uint32_t now); // false = the session ended or still waits

    std::shared_ptr<Lookup> lookup_;
    uint16_t lookup_port_ = 0;
    uint32_t lookup_started_ = 0;

    uint64_t transport_generation_ = 0;
    Role role_ = Role::None;
    uint32_t now() const { return clock ? clock() : now_ms(); }
    UdpTransport udp_;
    Transport* tr_ = &udp_;
    std::string my_name_;
    uint32_t lan_id_ = 0; // BUG-0210: DiscoverReply::host_id of the game hosted now
    uint8_t my_id_ = 0;
    bool welcomed_ = false;
    uint32_t welcome_world_ = 0; // guest: Welcome::world of the host
    uint32_t join_started_ = 0;
    std::vector<std::shared_ptr<Peer>> peers_;
    std::vector<PlayerInfo> roster_; // client: known players (incl. host)
    std::vector<uint32_t> retired_;  // host: conn ids of dropped peers (their late packets are ignored)
    // Packet delivery retains a shared Peer owner and fences callbacks by
    // transport generation, including an immediate Leave/Host/Join callback.

};

} // namespace net
