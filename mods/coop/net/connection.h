// One logical connection over UDP: seq/ack/ackbits, reliable-ordered + unreliable messages.
#pragma once

#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <vector>

#include "buffer.h"
#include "udp.h"

namespace net {

inline bool seq_greater(uint16_t a, uint16_t b)
{
    return ((a > b) && (a - b <= 32768)) || ((a < b) && (b - a > 32768));
}

// which of the 128 packets before the latest one arrived: bit k = sequence latest - (k + 1)
struct AckBits {
    uint64_t lo = 0, hi = 0;
    void clear() { lo = hi = 0; }
    void shift(unsigned s) // a newer packet arrived s sequence numbers later
    {
        if (s >= 128) {
            lo = hi = 0;
        } else if (s >= 64) {
            hi = s == 64 ? lo : lo << (s - 64);
            lo = 0;
        } else if (s > 0) {
            hi = (hi << s) | (lo >> (64 - s));
            lo <<= s;
        }
    }
    void set(unsigned k) { (k < 64 ? lo : hi) |= 1ull << (k & 63); }
    bool test(unsigned k) const { return k < 128 && (((k < 64 ? lo : hi) >> (k & 63)) & 1); }
};
constexpr unsigned kAckWindow = 128;
// reliable messages the receiver buffers ahead of a missing one; the sender never sends a message
// this far past the oldest unacknowledged one, so the buffer can never overflow (it used to drop the
// message of a packet it still acknowledged: lost for good, the world transfer stalled forever)
constexpr unsigned kRelWindow = 2048;

struct ConnStats {
    uint32_t packets_sent = 0, packets_recv = 0, resends = 0, acked = 0;
    // Actual UDP payload bytes, including headers and retransmitted messages.
    uint64_t bytes_sent = 0, bytes_recv = 0;
};

// Logical retained transport containers, excluding allocator/deque/map overhead.
struct ConnectionAccounting {
    size_t connections = 0, sent_packets = 0, sent_id_capacity = 0;
    size_t reliable_out = 0, reliable_in = 0, unreliable_out = 0, unreliable_slots_capacity = 0;
    size_t payload_size = 0, payload_capacity = 0;
    bool operator==(const ConnectionAccounting&) const = default;
};

class Connection {
public:
    using Deliver = std::function<void(uint8_t type, Reader& payload)>;

    Endpoint remote;
    // connection id in every packet header (protocol 6): the guest picks a new one for every join,
    // so a rejoin from the same address and port (a NAT that keeps the mapping, a proxy) is a new
    // connection and late packets of the old one are told apart instead of corrupting the new one
    uint32_t conn_id = 0;
    float rtt_ms = 100.f;
    uint32_t last_recv_ms = 0;
    uint32_t created_ms = 0;
    ConnStats stats;

    void send_reliable(uint8_t type, const Writer& w);
    // first: ahead of what is queued (the player state must not wait behind a big snapshot)
    void send_unreliable(uint8_t type, const Writer& w, bool first = false);
    size_t pending_reliable() const { return rel_out_.size(); }

    // Builds the next datagram if anything is due (messages, acks, keepalive).
    bool build_packet(uint32_t now, std::vector<uint8_t>& out);
    // Parses a datagram, updates acks, delivers messages in order. false = invalid packet.
    bool on_packet(const uint8_t* data, size_t n, uint32_t now, const Deliver& deliver);

    static bool looks_valid(const uint8_t* data, size_t n);
    static uint32_t peek_conn(const uint8_t* data, size_t n); // conn id of a valid packet

private:
    struct OutMsg {
        uint8_t type;
        uint16_t rel_id;
        std::vector<uint8_t> payload;
        uint32_t last_sent = 0;
        int sends = 0;
    };
    struct SentPkt {
        uint16_t seq;
        uint32_t time;
        std::vector<uint16_t> rel_ids;
        bool acked = false;
    };

    void ack_packet(SentPkt& p, uint32_t now);

    uint16_t local_seq_ = 0;
    uint16_t remote_seq_ = 0;
    AckBits recv_bits_;
    bool have_remote_ = false;
    bool ack_pending_ = false;
    uint32_t last_send_ms_ = 0;

    std::deque<SentPkt> sent_;
    uint16_t next_rel_id_ = 0;
    std::deque<OutMsg> rel_out_;
    uint16_t next_rel_expected_ = 0;
    std::map<uint16_t, std::pair<uint8_t, std::vector<uint8_t>>> rel_in_;
    std::vector<std::pair<uint8_t, std::vector<uint8_t>>> unrel_out_;

public:
    // unreliable messages not sent by the end of a flush are stale by the next one
    void drop_unreliable() { unrel_out_.clear(); }
    size_t unreliable_queued() const { return unrel_out_.size(); }
    ConnectionAccounting retained_accounting() const {
        ConnectionAccounting a; a.connections = 1; a.sent_packets = sent_.size();
        a.reliable_out = rel_out_.size(); a.reliable_in = rel_in_.size();
        a.unreliable_out = unrel_out_.size(); a.unreliable_slots_capacity = unrel_out_.capacity();
        for (const auto& packet : sent_) a.sent_id_capacity += packet.rel_ids.capacity() * sizeof(uint16_t);
        for (const auto& message : rel_out_) { a.payload_size += message.payload.size(); a.payload_capacity += message.payload.capacity(); }
        for (const auto& [serial, message] : rel_in_) { (void)serial; a.payload_size += message.second.size(); a.payload_capacity += message.second.capacity(); }
        for (const auto& message : unrel_out_) { a.payload_size += message.second.size(); a.payload_capacity += message.second.capacity(); }
        return a;
    }


private:
};

} // namespace net
