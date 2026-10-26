// Bounded portable editor messages and canonical durable-state serialization.
#pragma once
#include "editor_document.h"
#include "buffer.h"
#include <string>
#include <variant>

namespace editor::wire {
constexpr uint32_t kSchema = 1;
constexpr size_t kPacketBytes = 1100, kChunkBytes = 900;
constexpr size_t kStateBytes = 64u << 20, kOperationBytes = 8u << 20;
constexpr size_t kResourceBytes = 128u << 20;
constexpr size_t kQueueBytes = kResourceBytes + kOperationBytes;

bool encode_state(const State&, std::vector<uint8_t>&, std::string&);
bool decode_state(const std::vector<uint8_t>&, State&, std::string&);
bool encode_request(const Request&, std::vector<uint8_t>&, std::string&);
bool decode_request(const std::vector<uint8_t>&, Request&, std::string&);
bool encode_decision(const Decision&, std::vector<uint8_t>&, std::string&);
bool decode_decision(const std::vector<uint8_t>&, Decision&, std::string&);

struct Context {
    ProjectId project{};
    uint64_t epoch = 0;
    Revision revision = 0;
    bool operator==(const Context&) const = default;
};
struct Offer { uint32_t schema = kSchema; std::string name; Peer assigned; Peer host{0,1}; };
struct Join { Revision durable_revision = 0; };
enum class TransferKind : uint8_t { Baseline, Request, Decision, Resources };
struct TransferBegin { TransferKind kind{}; uint64_t serial = 0; uint32_t total = 0; std::string hash; };
struct TransferChunk { uint64_t serial = 0; uint32_t offset = 0; std::vector<uint8_t> bytes; };
struct TransferEnd { uint64_t serial = 0; };
enum class LeaseAction : uint8_t { Acquire, Renew, Release };
struct LeaseRequest { LeaseAction action{}; uint64_t serial = 0; Time ttl = 0; std::vector<Scope> scopes; std::vector<LeaseToken> tokens; };
struct LeaseReply { uint64_t serial = 0; LeaseResult result; };
struct Presence { ObjectId selected; ObjectId act; bool editing = false; Peer author; };
struct Coverage { Revision durable_revision = 0; std::string state_hash; };
struct Saved { Revision saved_revision = 0; ResourceKey resource; uint64_t serial = 0; bool operator==(const Saved&) const = default; };
struct SavedApplied { Saved saved; };
struct Leave { uint8_t reason = 0; };
enum class ControlKind : uint8_t {
    HostPreparePlaytest, GuestReadyPlaytest, HostBeginPlaytest,
    HostReturnEditor, HostCancelPlaytest, GuestEditorReturned, HostEditorResumed
};
// serial is a host-assigned playtest round, independent of document revision.
struct Control { ControlKind kind{}; uint64_t serial = 0; Revision revision = 0; bool ready = false; uint8_t ready_mask = 0, participant_mask = 0; };
using Payload = std::variant<Offer, Join, TransferBegin, TransferChunk, TransferEnd,
    LeaseRequest, LeaseReply, Request, Decision, Presence, Coverage, Saved, Leave, Control, SavedApplied>;
struct Message { Context context; Payload payload; };

// from_host is a transport role fact. sender is the connected Session peer,
// never a claimed player identity from an authoring payload.
bool write(const Message&, net::Writer&, std::string& error);
bool read(net::Reader&, Message&, Peer sender, bool from_host, std::string& error);

// One bounded incoming transfer per peer. Duplicate/out-of-order chunks are
// accepted only when identical; the receiver exposes nothing before full hash.
class Receiver {
public:
    bool begin(const Context&, const TransferBegin&, Peer, bool from_host, std::string&);
    bool chunk(const Context&, const TransferChunk&, std::string&);
    bool finish(const Context&, const TransferEnd&, std::vector<uint8_t>&, std::string&);
    void reset();
    bool active() const { return serial_ != 0; }
    TransferKind kind() const { return kind_; }
    Peer sender() const { return sender_; }
private:
    Context context_;
    Peer sender_;
    TransferKind kind_{};
    uint64_t serial_ = 0;
    std::string hash_;
    std::vector<uint8_t> data_, chunks_;
    size_t received_ = 0;
};

// Paces one immutable encoded transfer without constructing an unbounded packet
// queue. Call next only while Session::pending_to(peer) has room.
class Sender {
public:
    bool begin(Context, TransferKind, uint64_t serial, std::vector<uint8_t>, std::string&);
    bool next(Message&);
    void reset();
    bool active() const { return serial_ != 0; }
private:
    Context context_;
    TransferKind kind_{};
    uint64_t serial_ = 0;
    std::vector<uint8_t> data_;
    std::string hash_;
    size_t offset_ = 0;
    uint8_t phase_ = 0;
};
} // namespace editor::wire
