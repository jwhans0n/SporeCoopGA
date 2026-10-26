// Portable authoring projection and host-ordered transactions. No native layouts.
#pragma once

#include <array>
#include <compare>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <functional>
#include <map>
#include <optional>
#include <set>
#include <string>
#include <variant>
#include <vector>

namespace editor {

using ProjectId = std::array<uint8_t, 16>;
using Revision = uint64_t;
using Time = uint64_t; // Monotonic milliseconds supplied by the host.

struct ObjectId {
    // Persistent logical identity, never a vector index/native pointer. Undo may
    // restore this same identity; monotonically newer field revisions fence stale
    // edits and every reacquired lease receives a fresh token.
    uint64_t id = 0;
    uint32_t generation = 0;
    auto operator<=>(const ObjectId&) const = default;
};
struct Peer {
    uint8_t slot = 255;
    uint32_t generation = 0;
    auto operator<=>(const Peer&) const = default;
};
struct OperationId {
    Peer author;
    uint64_t sequence = 0;
    uint64_t epoch = 0;
    auto operator<=>(const OperationId&) const = default;
};
struct Vec3 { float x = 0, y = 0, z = 0; auto operator<=>(const Vec3&) const = default; };
struct Rotation { float x = 0, y = 0, z = 0, w = 1; auto operator<=>(const Rotation&) const = default; };
struct Pose {
    Vec3 position;
    Rotation rotation;
    float scale = 1;
    bool operator==(const Pose&) const = default;
};
struct ResourceKey {
    uint32_t instance = 0, type = 0, group = 0;
    auto operator<=>(const ResourceKey&) const = default;
};
using Value = std::variant<Vec3, Rotation, float, bool, int64_t, std::string, ObjectId, ResourceKey>;
enum class ValueKind : uint8_t { Position, Rotation, Number, Boolean, Integer, Text, Reference, Resource };
enum class Field : uint8_t { Position, Rotation, Scale, Property };
struct FieldKey {
    Field field = Field::Position;
    uint32_t property = 0;
    auto operator<=>(const FieldKey&) const = default;
};
// Only properties explicitly registered by a verified native adapter are editable.
struct PropertySpec {
    ValueKind kind = ValueKind::Boolean;
    double minimum = -1000000, maximum = 1000000;
    uint32_t max_text = 1024;
    bool optional = true, nullable = false;
    auto operator<=>(const PropertySpec&) const = default;
};
struct Placement {
    ObjectId object, class_id, act_id;
    bool protected_object = false;
    Vec3 position;
    Rotation rotation;
    float scale = 1;
    std::map<uint32_t, Value> properties;
    Revision revision = 0;
    std::map<FieldKey, Revision> field_revisions;
    bool operator==(const Placement&) const = default;
};
struct Reference {
    ObjectId owner, target; // Read-only external goal/avatar references.
    auto operator<=>(const Reference&) const = default;
};
struct Document {
    ProjectId project{};
    uint64_t epoch = 0;
    Revision revision = 0;
    uint64_t next_id = 1;
    std::set<ObjectId> classes, acts;
    std::map<uint32_t, PropertySpec> schema;
    std::map<ObjectId, Placement> placements;
    std::map<ObjectId, Revision> tombstones;
    std::vector<Reference> external_references;
    // This is only a supported semantic projection. Opaque native data stays owned
    // by the native adapter/store and is never reconstructed from this structure.
    bool operator==(const Document&) const = default;
};

enum class CheckKind : uint8_t { Object, Field, Absent };
struct Check {
    ObjectId object;
    CheckKind kind = CheckKind::Object;
    FieldKey field;
    Revision expected = 0;
    bool operator==(const Check&) const = default;
};
enum class ScopeKind : uint8_t { Document, Class, Act, Placement, Field };
struct Scope {
    ScopeKind kind = ScopeKind::Document;
    ObjectId object;
    FieldKey field;
    auto operator<=>(const Scope&) const = default;
};
struct Lease {
    Scope scope;
    Peer owner;
    uint64_t token = 0;
    Time expires = 0;
    bool operator==(const Lease&) const = default;
};
struct LeaseToken { Scope scope; uint64_t token = 0; bool operator==(const LeaseToken&) const = default; };

enum class MutationKind : uint8_t { Create, Clone, Delete, Set };
struct Mutation {
    MutationKind kind = MutationKind::Set;
    ObjectId target; // Clone: source. Create: zero; host allocates the new ID.
    Placement initial; // Create only; identity/version fields must be zero.
    FieldKey field;
    std::optional<Value> value; // Set: null removes an optional property.
    Revision expected = 0; // Set: field revision. Delete/clone: object revision.
    std::optional<Pose> clone_pose; // Clone only: final Ctrl-drag pose, one gesture.
    bool operator==(const Mutation&) const = default;
};
enum class Intent : uint8_t { Forward, Undo, Redo };
struct Request {
    ProjectId project{};
    uint64_t epoch = 0;
    OperationId operation;
    Revision base_revision = 0;
    Intent intent = Intent::Forward;
    std::string summary;
    std::vector<Mutation> mutations;
    std::vector<Check> dependencies;
    std::vector<LeaseToken> leases;
    bool operator==(const Request&) const = default;
};

struct ObjectEdit {
    ObjectId object;
    std::optional<Placement> before, after;
    Revision expected = 0;
    bool operator==(const ObjectEdit&) const = default;
};
struct FieldEdit {
    ObjectId object;
    FieldKey field;
    std::optional<Value> before, after;
    Revision expected = 0;
    Revision restored_revision = 0, restored_object_revision = 0;
    bool operator==(const FieldEdit&) const = default;
};
struct Patch {
    std::vector<ObjectEdit> objects;
    std::vector<FieldEdit> fields;
    std::vector<Check> dependencies;
    bool operator==(const Patch&) const = default;
};
struct Change {
    OperationId operation, gesture;
    Intent intent = Intent::Forward;
    Revision revision = 0;
    std::string summary;
    Patch patch, inverse;
    std::vector<ObjectId> allocated;
    std::map<ObjectId, ObjectId> clone_sources; // Forward Clone: new ID -> source.
    bool operator==(const Change&) const = default;
};
enum class Code : uint8_t {
    Accepted, Invalid, StaleProject, StaleEpoch, StalePeer, StaleRevision,
    MissingObject, Conflict, Unsupported, Limit, LeaseRequired, LeaseExpired,
    Referenced, Protected, NothingToUndo, NothingToRedo, HistoryUnavailable,
    ExpiredOperation, NativeRejected, NativeFailed, PersistenceFailed, Frozen, Busy
};
struct Decision {
    Code code = Code::Invalid;
    std::string reason;
    Revision revision = 0;
    std::optional<Change> change;
    bool accepted() const { return code == Code::Accepted; }
    bool operator==(const Decision&) const = default;
};
struct Gesture {
    OperationId operation;
    Revision original_revision = 0;
    Revision transition_revision = 0; // Most recent successful Undo/Redo/forward.
    std::string summary;
    bool applied = true, redo_available = false;
    Patch undo, redo;
    bool operator==(const Gesture&) const = default;
};
// Durable state. Leases and connected-peer capabilities are deliberately excluded.
struct State {
    Document document;
    std::deque<Gesture> history;
    std::map<OperationId, Decision> decisions;
    std::deque<OperationId> decision_order;
    std::map<Peer, uint64_t> retired_sequences;
    std::set<Peer> history_truncated;
    bool operator==(const State&) const = default;
};
struct Limits {
    size_t placements = 4096, classes = 1024, acts = 8, properties = 32;
    size_t mutations = 64, dependencies = 256, leases = 256, tombstones = 16384;
    size_t history = 128, history_bytes = 8u << 20;
    size_t decisions = 1024, decision_bytes = 8u << 20;
    size_t document_bytes = 32u << 20, summary_bytes = 256, text_bytes = 1024;
    Time max_lease_ms = 30000;
};
struct Hooks {
    // No callbacks run for rejected/duplicate requests. apply may partially fail;
    // rollback must restore before, otherwise the coordinator freezes. Persistence
    // receives the complete candidate state before any accepted decision is exposed.
    using Native = std::function<bool(const Document&, const Document&, const Change&, std::string&)>;
    Native validate, apply, rollback;
    // false guarantees no complete durable commit remains. A thrown persistence
    // hook is ambiguous and freezes the coordinator until checkpoint recovery.
    std::function<bool(const State&, const Change&, std::string&)> persist;
};
struct LeaseResult {
    Code code = Code::Invalid;
    std::string reason;
    std::vector<Lease> leases;
    bool accepted() const { return code == Code::Accepted; }
};

bool valid(const Document&, const Limits&, std::string& error);
size_t bytes(const Document&);
size_t bytes(const Patch&);
size_t bytes(const State&);
std::optional<Value> value(const Placement&, FieldKey);
Revision field_revision(const Placement&, FieldKey);
// Replica path: canonical ordered host acceptance only. It recomputes semantic
// before/after/inverse data and all per-author cursor transitions. Unknown native
// fields remain the adapter's responsibility. Rejection leaves state unchanged.
bool apply_committed(State&, const Decision&, std::string& error, const Hooks& = {}, Limits = {});

class Coordinator {
public:
    explicit Coordinator(Limits limits = {});
    // Import/recovery requires an explicit new epoch. Existing durable decisions
    // and original-gesture history may be retained; all transient peers/leases reset.
    bool reset(State state, uint64_t epoch, std::string& error);
    // Peer generations are author capabilities. A replacement generation cannot
    // undo its predecessor's gestures without an explicit external identity policy.
    bool connect(Peer peer);
    void disconnect(Peer peer);
    LeaseResult acquire(Peer, const std::vector<Scope>&, Time now, Time ttl);
    LeaseResult renew(Peer, const std::vector<LeaseToken>&, Time now, Time ttl);
    bool release(Peer, const std::vector<LeaseToken>&);
    Decision submit(const Request&, Time now, const Hooks& hooks = {});
    const State& state() const { return state_; }
    const std::vector<Lease>& leases() const { return leases_; }
    bool frozen() const { return frozen_; }
    bool applying() const { return busy_; }
    const Limits& limits() const { return limits_; }

private:
    Limits limits_;
    State state_;
    std::array<uint32_t, 4> generations_{};
    std::array<bool, 4> connected_{};
    std::vector<Lease> leases_;
    uint64_t next_token_ = 1;
    Time last_time_ = 0;
    bool ready_ = false, frozen_ = false, busy_ = false;
    bool peer(Peer) const;
    bool clock(Time);
    bool scope_valid(const Scope&) const;
    bool overlaps(const Scope&, const Scope&) const;
    bool covers(const Scope&, const Scope&) const;
    Code leased(const Request&, const Scope&, Time) const;
    void remember(State&, OperationId, const Decision&) const;
    void trim(State&) const;
};

} // namespace editor
