#include "session.h"

#include <algorithm>
#include <cstdarg>
#include <cstdio>

namespace net {

static void logf(const Session::Callbacks& cb, const char* fmt, ...)
{
    if (!cb.log)
        return;
    char b[512];
    va_list ap;
    va_start(ap, fmt);
    vsnprintf(b, sizeof b, fmt, ap);
    va_end(ap);
    cb.log(b);
}

// a connection id that differs from the previous ones of this process (and of other processes)
static uint32_t new_conn_id(uint32_t salt)
{
    static uint64_t counter = 0;
    uint64_t x = ((uint64_t)now_ms() << 32) ^ salt ^ (++counter * 0x9E3779B97F4A7C15ull) ^ (uint64_t)(uintptr_t)&counter;
    x ^= x >> 30;
    x *= 0xBF58476D1CE4E5B9ull;
    x ^= x >> 27;
    x *= 0x94D049BB133111EBull;
    x ^= x >> 31;
    return (uint32_t)x ? (uint32_t)x : 1u;
}

bool Session::host(uint16_t port, const std::string& name, std::string& err)
{
    leave("restart");
    if (!sock_.open(port, &err))
        return false;
    role_ = Role::Host;
    my_id_ = 0;
    my_name_ = name;
    welcomed_ = true;
    logf(cb, "hosting on UDP port %u", sock_.local_port());
    return true;
}

bool Session::join(const std::string& addr, const std::string& name, std::string& err)
{
    leave("restart");
    Endpoint ep;
    if (!Endpoint::parse(addr, kDefaultPort, ep)) {
        err = "cannot resolve " + addr;
        return false;
    }
    if (!sock_.open(0, &err))
        return false;
    role_ = Role::Client;
    my_name_ = name;
    welcomed_ = false;
    join_started_ = now_ms();
    auto p = std::make_unique<Peer>();
    p->id = 0;
    p->conn.remote = ep;
    p->conn.conn_id = new_conn_id(sock_.local_port());
    p->conn.created_ms = p->conn.last_recv_ms = join_started_;
    Hello h;
    h.build = build;
    h.name = name;
    Writer w;
    h.write(w);
    p->conn.send_reliable(MSG_HELLO, w);
    peers_.push_back(std::move(p));
    logf(cb, "joining %s ...", ep.str().c_str());
    return true;
}

void Session::leave(const char* why)
{
    if (role_ == Role::None)
        return;
    uint32_t now = now_ms();
    for (auto& p : peers_) {
        // unreliable, in three packets: a reliable BYE waited behind any earlier lost message (and
        // was sent once), so over a lossy link the other side often never saw it
        Writer w;
        w.u8(0);
        std::vector<uint8_t> pkt;
        for (int i = 0; i < 3; i++) {
            p->conn.send_unreliable(MSG_BYE, w, true);
            if (p->conn.build_packet(now + 1000, pkt))
                if (sock_.send_to(p->conn.remote, pkt.data(), pkt.size())) p->conn.stats.bytes_sent += pkt.size();
        }
    }
    peers_.clear();
    roster_.clear();
    retired_.clear();
    sock_.close();
    role_ = Role::None;
    welcomed_ = false;
    logf(cb, "session closed (%s)", why);
}

Peer* Session::find_peer(const Endpoint& e)
{
    for (auto& p : peers_)
        if (p->conn.remote == e)
            return p.get();
    return nullptr;
}

bool Session::retired(uint32_t conn) const
{
    return std::find(retired_.begin(), retired_.end(), conn) != retired_.end();
}

uint8_t Session::alloc_id() const
{
    for (uint8_t id = 1; id < kMaxPlayers; id++) {
        bool used = false;
        for (auto& p : peers_)
            if (p->accepted && p->id == id)
                used = true;
        if (!used)
            return id;
    }
    return 0xFF;
}

std::string Session::name_of(uint8_t id) const
{
    if (id == my_id_ && role_ != Role::None)
        return my_name_;
    if (role_ == Role::Host) {
        for (auto& p : peers_)
            if (p->accepted && p->id == id)
                return p->name;
    } else {
        for (auto& r : roster_)
            if (r.id == id)
                return r.name;
    }
    return "player" + std::to_string(id);
}

std::vector<PlayerInfo> Session::players() const
{
    std::vector<PlayerInfo> v;
    if (role_ == Role::Host) {
        v.push_back({0, my_name_});
        for (auto& p : peers_)
            if (p->accepted)
                v.push_back({p->id, p->name});
    } else if (role_ == Role::Client) {
        v = roster_;
        bool self = false;
        for (auto& r : v)
            if (r.id == my_id_)
                self = true;
        if (!self && welcomed_)
            v.push_back({my_id_, my_name_});
    }
    return v;
}

int Session::rtt_of(uint8_t id) const
{
    for (auto& p : peers_)
        if (p->id == id && (p->accepted || role_ == Role::Client))
            return (int)p->conn.rtt_ms;
    return -1;
}

std::string Session::host_address() const
{
    return role_ == Role::Client && !peers_.empty() ? peers_[0]->conn.remote.str() : std::string();
}

std::string Session::status() const
{
    char b[384];
    std::string s;
    if (role_ == Role::None)
        return "offline\n";
    if (role_ == Role::Host)
        snprintf(b, sizeof b, "host port=%u players=%d/%d\n", sock_.local_port(),
                 (int)players().size(), kMaxPlayers);
    else
        snprintf(b, sizeof b, "client id=%u %s\n", my_id_, welcomed_ ? "connected" : "connecting");
    s += b;
    uint32_t now = now_ms();
    for (auto& p : peers_) {
        snprintf(b, sizeof b, "  [%u] %-16s %s rtt=%.0fms last=%ums sent=%u recv=%u acked=%u resend=%u pending=%u bytes_sent=%llu bytes_recv=%llu\n",
                 p->id, p->name.c_str(), p->conn.remote.str().c_str(), p->conn.rtt_ms,
                 now - p->conn.last_recv_ms, p->conn.stats.packets_sent, p->conn.stats.packets_recv,
                 p->conn.stats.acked, p->conn.stats.resends, (unsigned)p->conn.pending_reliable(),
                 (unsigned long long)p->conn.stats.bytes_sent, (unsigned long long)p->conn.stats.bytes_recv);
        s += b;
    }
    return s;
}

void Session::broadcast_reliable(uint8_t type, const Writer& w, int except_id)
{
    for (auto& p : peers_)
        if (p->accepted && (int)p->id != except_id)
            p->conn.send_reliable(type, w);
}

bool Session::send_reliable_to(uint8_t id, uint8_t type, const Writer& w)
{
    for (auto& p : peers_)
        if ((p->accepted || role_ == Role::Client) && p->id == id) {
            p->conn.send_reliable(type, w);
            return true;
        }
    return false;
}

void Session::send_event(uint8_t type, const Writer& w)
{
    for (auto& p : peers_)
        if (p->accepted || role_ == Role::Client)
            p->conn.send_reliable(type, w);
}

bool Session::send_unreliable_to(uint8_t id, uint8_t type, const Writer& w)
{
    for (auto& p : peers_)
        if ((p->accepted || role_ == Role::Client) && p->id == id) {
            p->conn.send_unreliable(type, w);
            return true;
        }
    return false;
}

size_t Session::pending_to(uint8_t id) const
{
    for (auto& p : peers_)
        if (p->id == id)
            return p->conn.pending_reliable();
    return 0;
}

void Session::send_state(PlayerState st)
{
    if (!connected())
        return;
    st.id = my_id_;
    st.time_ms = now_ms();
    Writer w;
    st.write(w);
    for (auto& p : peers_)
        if (p->accepted || role_ == Role::Client)
            p->conn.send_unreliable(MSG_PLAYER_STATE, w, true);
}

void Session::send_chat(const std::string& text)
{
    if (!connected())
        return;
    Chat c;
    c.id = my_id_;
    c.text = text;
    Writer w;
    c.write(w);
    for (auto& p : peers_)
        if (p->accepted || role_ == Role::Client)
            p->conn.send_reliable(MSG_CHAT, w);
    if (cb.chat)
        cb.chat(my_id_, text);
}

void Session::drop_peer(size_t idx, uint8_t reason)
{
    auto& p = peers_[idx];
    bool was = p->accepted;
    uint8_t id = p->id;
    logf(cb, "player %u (%s) disconnected (reason %u)", id, p->name.c_str(), reason);
    retired_.push_back(p->conn.conn_id);
    if (retired_.size() > 64)
        retired_.erase(retired_.begin());
    peers_.erase(peers_.begin() + idx);
    if (was) {
        PlayerLeave pl{id, reason};
        Writer w;
        pl.write(w);
        broadcast_reliable(MSG_PLAYER_LEAVE, w);
        if (cb.player_leave)
            cb.player_leave(id, reason);
    }
}

void Session::handle_msg(Peer& p, uint8_t type, Reader& r)
{
    if (role_ == Role::Host) {
        if (type == MSG_HELLO) {
            if (p.accepted)
                return;
            Hello h;
            if (!h.read(r))
                return;
            Reject rej;
            if (h.proto != kProtoVersion) {
                rej.reason = REJ_VERSION;
                rej.text = "protocol version mismatch";
            } else if (build && h.build && h.build != build) {
                rej.reason = REJ_BUILD;
                rej.text = "different SporeApp.exe build";
            } else if (alloc_id() == 0xFF) {
                rej.reason = REJ_FULL;
                rej.text = "session is full";
            }
            if (rej.reason) {
                Writer w;
                rej.write(w);
                p.conn.send_reliable(MSG_REJECT, w);
                logf(cb, "rejected %s: %s", p.conn.remote.str().c_str(), rej.text.c_str());
                return;
            }
            p.id = alloc_id();
            p.name = h.name.empty() ? "guest" + std::to_string(p.id) : h.name;
            p.accepted = true;
            Welcome wl;
            wl.your_id = p.id;
            wl.world = world;
            wl.players = players();
            Writer w;
            wl.write(w);
            p.conn.send_reliable(MSG_WELCOME, w);
            PlayerJoin pj;
            pj.p = {p.id, p.name};
            Writer wj;
            pj.write(wj);
            broadcast_reliable(MSG_PLAYER_JOIN, wj, p.id);
            logf(cb, "player %u '%s' joined from %s", p.id, p.name.c_str(), p.conn.remote.str().c_str());
            if (cb.player_join)
                cb.player_join(pj.p);
            return;
        }
        if (!p.accepted)
            return;
        switch (type) {
        case MSG_PLAYER_STATE: {
            PlayerState st;
            if (!st.read(r))
                return;
            st.id = p.id; // never trust the id from the wire
            if (cb.player_state)
                cb.player_state(st);
            Writer w;
            st.write(w);
            for (auto& o : peers_)
                if (o->accepted && o->id != p.id)
                    o->conn.send_unreliable(MSG_PLAYER_STATE, w);
            break;
        }
        case MSG_CHAT: {
            Chat c;
            if (!c.read(r))
                return;
            c.id = p.id;
            if (cb.chat)
                cb.chat(c.id, c.text);
            Writer w;
            c.write(w);
            broadcast_reliable(MSG_CHAT, w, p.id);
            break;
        }
        case MSG_PING: {
            Writer w;
            w.u32(r.u32());
            p.conn.send_unreliable(MSG_PONG, w);
            break;
        }
        case MSG_BYE:
            p.bye = true; // dropped in update()
            break;
        case MSG_ANIM: {
            AnimEvent ev;
            if (!ev.read(r))
                return;
            ev.id = p.id;
            Writer w;
            ev.write(w);
            broadcast_reliable(MSG_ANIM, w, p.id);
            Reader rr(w.data.data(), w.size());
            if (cb.custom)
                cb.custom(type, p.id, rr);
            break;
        }
        default:
            if (cb.custom)
                cb.custom(type, p.id, r);
        }
        return;
    }

    // ---- client ----
    switch (type) {
    case MSG_WELCOME: {
        Welcome wl;
        if (!wl.read(r))
            return;
        my_id_ = wl.your_id;
        welcomed_ = true;
        p.accepted = true;
        roster_ = wl.players;
        for (auto& pl : roster_)
            if (pl.id == 0)
                p.name = pl.name;
        logf(cb, "joined as player %u (host '%s', %d players)", my_id_, p.name.c_str(), (int)roster_.size());
        if (cb.connected)
            cb.connected();
        for (auto& pl : roster_)
            if (pl.id != my_id_ && cb.player_join)
                cb.player_join(pl);
        break;
    }
    case MSG_REJECT: {
        Reject rej;
        rej.read(r);
        std::string why = "rejected: " + rej.text;
        logf(cb, "%s", why.c_str());
        leave("rejected");
        if (cb.disconnected)
            cb.disconnected(why);
        break;
    }
    case MSG_PLAYER_JOIN: {
        PlayerJoin pj;
        if (!pj.read(r))
            return;
        roster_.push_back(pj.p);
        logf(cb, "player %u '%s' joined", pj.p.id, pj.p.name.c_str());
        if (cb.player_join)
            cb.player_join(pj.p);
        break;
    }
    case MSG_PLAYER_LEAVE: {
        PlayerLeave pl;
        if (!pl.read(r))
            return;
        roster_.erase(std::remove_if(roster_.begin(), roster_.end(),
                                     [&](const PlayerInfo& x) { return x.id == pl.id; }),
                      roster_.end());
        if (cb.player_leave)
            cb.player_leave(pl.id, pl.reason);
        break;
    }
    case MSG_PLAYER_STATE: {
        PlayerState st;
        if (st.read(r) && st.id != my_id_ && cb.player_state)
            cb.player_state(st);
        break;
    }
    case MSG_CHAT: {
        Chat c;
        if (c.read(r) && cb.chat)
            cb.chat(c.id, c.text);
        break;
    }
    case MSG_BYE:
        logf(cb, "host closed the session");
        leave("host left");
        if (cb.disconnected)
            cb.disconnected("host closed the session");
        break;
    default:
        if (cb.custom)
            cb.custom(type, 0, r);
    }
}

void Session::handle_packet(Peer& p, const uint8_t* data, size_t n, uint32_t now)
{
    p.conn.stats.bytes_recv += n;
    p.conn.on_packet(data, n, now, [&](uint8_t type, Reader& r) {
        if (role_ != Role::None)
            handle_msg(p, type, r);
    });
}

void Session::flush(uint32_t now)
{
    std::vector<uint8_t> pkt;
    for (auto& p : peers_) {
        // the reliable bulk (world transfer) is paced by the ack window: all of it per flush on a short
        // round trip (LAN), a third on the internet; unreliable messages are stale by the next frame,
        // so they all go out (up to 64 more packets): a cell pool snapshot is one ~900-byte chunk per
        // 14 cells and used to push the player state queued after it past the limit
        int burst = p->conn.rtt_ms < 40.f ? (int)kAckWindow : (int)kAckWindow / 3;
        for (int i = 0; (i < burst || (i < burst + 64 && p->conn.unreliable_queued() > 0)) && p->conn.build_packet(now, pkt); i++)
            if (sock_.send_to(p->conn.remote, pkt.data(), pkt.size())) p->conn.stats.bytes_sent += pkt.size();
        p->conn.drop_unreliable();
    }
}

void Session::update()
{
    if (role_ == Role::None)
        return;
    uint32_t now = now_ms();
    uint8_t buf[2048];
    for (int i = 0; i < 256; i++) {
        Endpoint from;
        int n = sock_.recv_from(from, buf, sizeof buf);
        if (n <= 0)
            break;
        if (n >= 6 && role_ == Role::Host && *(const uint32_t*)buf == kDiscoverQuery) {
            DiscoverReply rep;
            rep.port = sock_.local_port();
            rep.name = my_name_;
            rep.players = (uint8_t)players().size();
            rep.max_players = kMaxPlayers;
            rep.info = lobby_info;
            Writer w;
            rep.write(w);
            sock_.send_to(from, w.data.data(), w.size());
            continue;
        }
        if (!Connection::looks_valid(buf, n))
            continue;
        Peer* p = find_peer(from);
        uint32_t conn = Connection::peek_conn(buf, n);
        if (role_ == Role::Host && (!p || p->conn.conn_id != conn)) {
            if (retired(conn))
                continue; // a late packet of a connection that is gone
            if (p) {
                // the same address and port with a new connection id: the guest left and joined
                // again (its goodbye got lost) through a NAT / proxy that kept the mapping; the old
                // peer goes (otherwise it swallowed the new HELLO and never timed out)
                for (size_t k = 0; k < peers_.size(); k++)
                    if (peers_[k].get() == p) {
                        logf(cb, "a new connection from %s replaces player %u", from.str().c_str(), p->id);
                        drop_peer(k, 0);
                        break;
                    }
                p = nullptr;
            }
        }
        if (!p) {
            if (role_ != Role::Host || peers_.size() >= 16)
                continue;
            auto np = std::make_unique<Peer>();
            np->conn.remote = from;
            np->conn.conn_id = conn;
            np->conn.created_ms = now;
            peers_.push_back(std::move(np));
            p = peers_.back().get();
        }
        handle_packet(*p, buf, n, now);
        if (role_ == Role::None)
            return; // left during handling
    }

    // timeouts
    if (role_ == Role::Client) {
        if (!welcomed_ && now - join_started_ > 10000) {
            leave("join timeout");
            if (cb.disconnected)
                cb.disconnected("no answer from host (timeout)");
            return;
        }
        if (welcomed_ && !peers_.empty() && now - peers_[0]->conn.last_recv_ms > timeout_ms) {
            leave("timeout");
            if (cb.disconnected)
                cb.disconnected("connection to host lost (timeout)");
            return;
        }
    } else {
        for (size_t i = 0; i < peers_.size();) {
            auto& p = peers_[i];
            bool dead = p->bye || (p->accepted ? now - p->conn.last_recv_ms > timeout_ms
                                               : now - p->conn.created_ms > 10000);
            if (dead)
                drop_peer(i, p->bye ? 0 : 1);
            else
                i++;
        }
    }
    flush(now);
}

} // namespace net
