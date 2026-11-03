// Portable editor transport/controller. Call tick outside the native render frame.
#pragma once

#include "net/editor_document.h"
#include "net/editor_protocol.h"
#include "net/session.h"

#include <memory>
#include <optional>
#include <utility>

namespace editor_session {

enum class Phase : uint8_t { Idle, Offered, Joining, Syncing, Ready, Preparing, Playing, Returning, Frozen, Disconnected };
inline constexpr editor::Time kPreviewLifetime = 1000, kPreviewInterval = 50;
struct PreviewView { editor::wire::TransformPreview value; editor::Time expires = 0; };
struct Member {
    editor::Peer peer;
    std::string name;
    Phase phase = Phase::Offered;
    editor::wire::Presence presence;
    editor::wire::Coverage coverage;
    bool ready = false;
    std::optional<PreviewView> preview;
    editor::Revision applied_revision = 0;
    std::optional<editor::ComplexityCapability> complexity;
    uint64_t complexity_serial=0;
};
struct View {
    Phase phase = Phase::Idle;
    std::string name, error;
    editor::Peer local;
    editor::wire::Context context;
    uint64_t playtest_round = 0;
    uint8_t playtest_participants = 0;
    editor::Revision playtest_revision = 0;
    editor::wire::PlaytestMode playtest_mode=editor::wire::PlaytestMode::Together;
    editor::Peer playtest_tester;
    editor::ObjectId playtest_act;
    std::map<uint8_t, Member> members;
    std::vector<editor::Lease> local_leases;
    editor::Revision locally_durable = 0;
    std::optional<editor::wire::Saved> saved;
    bool complexity_ready=false;
    std::string complexity_error;
};
struct SavedCoverage {
    editor::Revision revision = 0;
    uint64_t serial = 0;
    size_t applied = 0, expected = 0;
    bool current = false;
};
// Passive outside-frame diagnostics. Sizes/capacities describe retained logical
// containers; shared buffer references may alias and estimates are not heap/RSS.
struct Accounting {
    net::ConnectionAccounting transport;
    size_t history = 0, decisions = 0, decision_order = 0, state_estimate = 0;
    size_t coordinator_history = 0, coordinator_decisions = 0, coordinator_estimate = 0;
    size_t replica_history = 0, replica_decisions = 0, replica_estimate = 0;
    size_t prepared_history = 0, prepared_decisions = 0, prepared_estimate = 0;
    size_t tombstones = 0, retired_sequences = 0, truncated_authors = 0, leases = 0;
    size_t channels = 0, hashes = 0, completed_transfers = 0, lease_decisions = 0, released_scope_fences = 0;
    size_t local_leases = 0, lease_reply_capacity = 0, completed_hash_capacity = 0, revision_hash_capacity = 0;
    size_t input_count = 0, input_bytes = 0, local_count = 0, local_bytes = 0;
    size_t queued_packets = 0, queued_transfers = 0, output_bytes = 0;
    size_t transfer_reference_size = 0, transfer_reference_capacity = 0;
    size_t pending_leases = 0, release_fences = 0, lease_retirements = 0, lease_stamps = 0;
    size_t deferred_count = 0, deferred_bytes = 0, held_count = 0, held_bytes = 0;
    size_t resource_size = 0, resource_capacity = 0, baseline_size = 0, baseline_capacity = 0;
    size_t staged_size = 0, staged_capacity = 0;
    size_t receiving = 0, receiver_data_size = 0, receiver_data_capacity = 0, receiver_chunk_capacity = 0;
};
struct Config {
    uint8_t message_type = net::MSG_EDITOR;
    size_t inbound_packets = 512, inbound_bytes = 1u << 20;
    size_t outbound_packets = 256, outbound_bytes = 4u << 20;
    size_t transfer_bytes = 224u << 20, catchup_decisions = 256;
    size_t reliable_window = 24, packets_per_tick = 16;
};
enum class TracePoint : uint8_t { HostRequestReceived, HostDecisionQueued, DecisionDecoded };
enum class Durability : uint8_t { Pending, Durable, Rejected, Uncertain };
enum class NativePreparation : uint8_t { Fallback, Pending, Ready, Error };
struct Callbacks {
    editor::Hooks host_hooks;
    // Optional outside-frame numeric diagnostics. Does not affect wire or policy.
    std::function<void(TracePoint, const editor::ProjectId&, const editor::OperationId&, editor::Revision)> trace;
    // Outside-frame cache inventory. Claims must describe independently hashed
    // owned bytes. Empty inventory preserves the full verified baseline path.
    std::function<bool(editor::wire::Join&, std::string&)> prepare_join;
    // Immutable full payload is pinned to the baseline chosen by the host. This
    // can omit verified cache hits; no host resource cache is replaced by it.
    std::function<bool(const std::vector<uint8_t>&, const editor::wire::Join&,
                       std::vector<uint8_t>&, std::string&)> resources_for_join;
    // Returning true means the owned import/application is complete. Failure
    // freezes this replica until a newly accepted baseline; no callbacks run in
    // on_message or Session's packet-dispatch callbacks.
    // Stage immutable target-revision bytes; do not change the currently rendered
    // scene until apply_baseline/apply_committed consumes that revision.
    std::function<bool(const std::vector<uint8_t>&, const editor::wire::Context&, std::string&)> apply_resources;
    std::function<bool(const editor::State&)> baseline_ready;
    // Native history gestures and asynchronous saves must settle before another
    // semantic edit is applied. Transport/leases/presence continue while held.
    std::function<bool()> authoring_ready;
    // Playtest preparation may need an explicit local gesture rollback even
    // when detached canonical edits can proceed. False defers a guest Prepare
    // or declines a host Prepare before lease revocation; Decisions use only
    // authoring_ready. Exceptions also defer/decline. Missing keeps old behavior.
    std::function<bool()> prepare_ready;
    // Installing a host's saved native checkpoint may require checked local
    // gesture restoration. False/throw defers that Saved notice in FIFO order;
    // the adapter must not wait for a Decision ordered after the notice.
    // Decisions continue to use authoring_ready, not this installation gate.
    std::function<bool()> saved_ready;
    // A missing revision means the local native scene is away or rebuilding.
    // Default adapters materialize synchronously with canonical acceptance.
    std::function<std::optional<editor::Revision>()> rendered_revision;
    // A lifecycle barrier rejects new mutations instead of queuing them until
    // after the pinned export revision. Native gesture settlement stays separate.
    std::function<bool()> authoring_held;
    std::function<void(uint8_t, const editor::wire::Command&)> command;
    // A guest's immutable dependencies arrive before its ordered cast request.
    // This stages owned bytes only; native registration belongs to host apply.
    std::function<bool(editor::OperationId,const std::vector<uint8_t>&,std::string&)> assets;
    std::function<bool(const editor::State&, std::string&)> apply_baseline;
    editor::Hooks::Native apply_committed, rollback_committed;
    // Optional experiment: private Native proof only. Ready never means that
    // the live scene or semantic head was applied. The original apply callback
    // runs afterward, before any persistence worker can start.
    std::function<bool()> native_prepare_enabled;
    std::function<NativePreparation(const editor::Document&,const editor::Document&,const editor::Change&,std::string&)> begin_native_prepare;
    std::function<NativePreparation(std::string&)> poll_native_prepare;
    std::function<void()> cancel_native_prepare;
    std::function<bool(const editor::State&, const editor::Change&, std::string&)> persist_guest;
    // Owned-byte work only. Native application has completed, but the semantic
    // state and decision remain unaccepted until poll_persist reports Durable.
    // Both callbacks run outside Present. Completion must be fenced to the
    // exact project/epoch/revision supplied to begin_persist.
    std::function<bool(const editor::State&,const editor::Change&,std::string&)> begin_persist;
    std::function<Durability(std::string&)> poll_persist;
    // Teardown only: join the owned-byte worker and return its known outcome
    // before controller/context destruction. Never used by the ordinary tick.
    std::function<Durability(std::string&)> settle_persist;
    std::function<void(const editor::Decision&)> decision;
    std::function<void(const std::vector<editor::Scope>&)> lease_denied;
    std::function<void(editor::Peer, const editor::wire::Presence&)> presence;
    std::function<void(editor::Peer, const editor::wire::Coverage&)> coverage;
    std::function<void(const editor::wire::Saved&)> saved;
    std::function<bool(const editor::wire::Control&, std::string&)> control;
    // Return/cancel may wait for native playtest teardown and asynchronous editor
    // loading. False defers without failure, retaining the group authoring hold.
    std::function<bool(const editor::wire::Control&)> control_ready;
    std::function<void(const std::string&)> error;
};

// Hashes only the canonical Document, never peer-private decision caches/history.
bool document_hash(const editor::Document&, std::string& hash, std::string& error);

// Owned publication bytes and their computed digests. Only the pure builder
// can create a valid value; publication never accepts caller-supplied hashes.
class PreparedResources {
public:
    PreparedResources(const PreparedResources&) = default;
    PreparedResources(PreparedResources&&) noexcept = default;
    PreparedResources& operator=(const PreparedResources&) = default;
    PreparedResources& operator=(PreparedResources&&) noexcept = default;
private:
    using Buffer = std::shared_ptr<const std::vector<uint8_t>>;
    Buffer full_, outgoing_;
    std::string full_hash_, outgoing_hash_;
    bool broadcast_;
    PreparedResources(Buffer full, Buffer outgoing, std::string full_hash,
                      std::string outgoing_hash, bool broadcast)
        : full_(std::move(full)), outgoing_(std::move(outgoing)),
          full_hash_(std::move(full_hash)), outgoing_hash_(std::move(outgoing_hash)), broadcast_(broadcast) {}
    friend std::optional<PreparedResources> prepare_resources(std::vector<uint8_t>,std::vector<uint8_t>,bool,std::string&);
    friend class Controller;
};
// Pure owned-byte work, safe on a worker. Empty broadcast bytes reuse full bytes.
std::optional<PreparedResources> prepare_resources(std::vector<uint8_t> full,
    std::vector<uint8_t> broadcast_bytes, bool broadcast, std::string& error);

class Controller {
public:
    Controller(net::Session&, Callbacks = {}, Config = {});
    ~Controller();
    Controller(const Controller&) = delete;
    Controller& operator=(const Controller&) = delete;
    bool start(editor::State, std::vector<uint8_t> resources, const std::string& name, std::string& error,
               std::optional<editor::Peer> resume_host = {});
    // Cache the complete late-join payload. A nonempty broadcast_bytes payload
    // supplies a smaller immutable delta for peers that already have the baseline.
    bool update_resources(std::vector<uint8_t> full, editor::Revision target, std::string& error,
                          bool broadcast = true, std::vector<uint8_t> broadcast_bytes = {});
    // Outside-frame publication of immutable worker-prepared bytes. Rechecks
    // the live context, target and queue bounds without hashing/copying payloads.
    bool update_resources(PreparedResources, editor::Revision target, std::string& error);
    bool accept_offer(std::string& error);
    void end(); // Authoring session only; leaves the party transport connected.
    // Host recovery retires the old authoring epoch while its guests keep the
    // recoverable draft frozen. It sends the existing authenticated Leave{3},
    // never an ordinary Leave{0} that would reopen private native input.
    void end_for_recovery();
    // Outside-frame failure notification; keeps the last document for recovery
    // and tells the party this editor replica cannot acknowledge further work.
    void freeze_local(const std::string& reason);
    void on_message(uint8_t from, net::Reader&);
    void on_join(uint8_t id);
    void on_leave(uint8_t id);
    void tick(editor::Time now);
    // Outside-frame worker completion only. Never drains inbound/local/control
    // queues; unsuccessful outcomes wait for tick before native rollback.
    bool poll_persistence(editor::Time now);
    bool submit(editor::Request, std::string& error);
    bool upload_assets(editor::OperationId,const std::vector<uint8_t>&,std::string& error);
    // Release quarantines local tokens immediately. Overlapping acquisition is
    // bounded and deferred until the authenticated release decision arrives;
    // lease deadlines begin when that deferred request is actually emitted.
    uint64_t request_leases(const std::vector<editor::Scope>&, editor::Time ttl, std::string& error);
    uint64_t renew_leases(const std::vector<editor::LeaseToken>&, editor::Time ttl, std::string& error);
    uint64_t release_leases(const std::vector<editor::LeaseToken>&, std::string& error);
    bool set_presence(editor::wire::Presence, std::string& error);
    // Outside frame. The local value survives context resets; authenticated
    // cohort claims/acknowledgements never do. Defaults do not require equality.
    bool set_complexity_capability(const editor::ComplexityCapability&,std::string& error);
    // Outside-frame only. Host commands can target a peer or all active peers;
    // guest commands always target the host. Native callbacks run during tick.
    bool send_command(editor::wire::Command, std::string& error, uint8_t to = 255);
    // Latest-only unreliable hint. Does not apply native edits or persist state.
    bool set_preview(editor::ObjectId, editor::ObjectId act, editor::Pose, std::string& error,
                     editor::ObjectId class_id={},bool creating=false,std::optional<editor::Pose> destination={});
    void clear_preview();
    bool publish_saved(editor::wire::Saved, std::string& error);
    // Guest only, after native root application and durable opaque checkpoint
    // completion. Deferring a Saved callback must also defer this acknowledgement.
    bool acknowledge_saved(const editor::wire::Saved&, std::string& error);
    bool prepare_playtest(std::string& error,editor::wire::PlaytestMode mode=editor::wire::PlaytestMode::Together,editor::Peer tester={},editor::ObjectId start_act={});
    bool ready_playtest(bool, std::string& error);
    bool cancel_playtest(std::string& error);
    bool return_editor(std::string& error);
    // Host close barrier: continue ticking while false. Waits queued output,
    // reliable transport ACKs and current canonical applied/durable coverage
    // from active participants; ignores unaccepted requests and offered/frozen
    // peers. Same-revision Saves also require acknowledge_saved from every active
    // participant; transport ACKs alone never satisfy native save completion.
    bool settled() const;
    Accounting accounting() const; // No native work, encoding, I/O or cache mutation.
    SavedCoverage saved_coverage() const; // Exact native SavedApplied serials, not transport ACKs.
    const editor::State& state() const;
    const View& view() const;

private:
    void end_session(bool recovery);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};

} // namespace editor_session
