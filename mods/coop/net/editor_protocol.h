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
constexpr size_t kResourceBytes = 128u << 20, kJoinBytes = 256u << 10;
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
struct ContentClaim {
    ResourceKey key; uint32_t size = 0; std::string hash;
    bool operator==(const ContentClaim&) const = default;
};
struct Join {
    Revision durable_revision = 0;
    std::string base_hash = {};
    uint32_t base_size = 0;
    std::vector<ContentClaim> records = {}; // unique (group,type,instance) order
    bool operator==(const Join&) const = default;
};
bool encode_join(const Join&, std::vector<uint8_t>&, std::string&);
bool decode_join(const std::vector<uint8_t>&, Join&, std::string&);
enum class TransferKind : uint8_t { Baseline, Request, Decision, Resources, Join };
struct TransferBegin { TransferKind kind{}; uint64_t serial = 0; uint32_t total = 0; std::string hash; };
struct TransferChunk { uint64_t serial = 0; uint32_t offset = 0; std::vector<uint8_t> bytes; };
struct TransferEnd { uint64_t serial = 0; };
enum class LeaseAction : uint8_t { Acquire, Renew, Release };
struct LeaseRequest { LeaseAction action{}; uint64_t serial = 0; Time ttl = 0; std::vector<Scope> scopes; std::vector<LeaseToken> tokens; };
struct LeaseReply { uint64_t serial = 0; LeaseResult result; };
enum class PresencePhase : uint8_t { Ready, Syncing, Preparing, Playing, Returning, Frozen, Disconnected, Offered };
inline constexpr uint8_t kOwnSelection = 1, kOwnDocument = 2;
struct Presence {
    ObjectId selected; ObjectId act; bool editing = false; Peer author;
    bool pending = false;
    // Host-derived hints, never capabilities. Guests send zero/Ready.
    uint8_t ownership = 0;
    PresencePhase phase = PresencePhase::Ready;
    LeaseToken preview_lease = {}; // Host-derived current selection capability identity.
    bool operator==(const Presence&) const = default;
};
// Noncanonical hint for an already committed placement. The host verifies the
// exact live lease before relaying. A released lease cannot resurrect a ghost.
struct TransformPreview {
    Peer author; uint64_t serial = 0; bool active = true;
    ObjectId object, act; Pose pose; LeaseToken lease;
    bool operator==(const TransformPreview&) const = default;
};
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
// Native toolbar requests and one host-ordered local-copy export barrier.
// These are lifecycle commands, never authored document operations.
enum class CommandKind : uint8_t {
    RequestPlaytest, ReplyPlaytest, RequestExport, HoldExport, ExportHeld,
    BeginExport, FinishExport, EndExport, ExitEditor
};
struct Command {
    CommandKind kind{};
    uint64_t serial = 0;
    Revision revision = 0;
    Peer actor;
    bool accepted = false;
    bool operator==(const Command&) const = default;
};
using Payload = std::variant<Offer, Join, TransferBegin, TransferChunk, TransferEnd,
    LeaseRequest, LeaseReply, Request, Decision, Presence, Coverage, Saved, Leave, Control, SavedApplied, TransformPreview, Command>;
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
    // Logical retained vector storage, not an allocator or process-byte claim.
    size_t retained_data_size() const { return data_.size(); }
    size_t retained_data_capacity() const { return data_.capacity(); }
    size_t retained_chunk_capacity() const { return chunks_.capacity(); }
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
