// Exact-build native Adventure Editor adapter. Call mutations outside Present.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <memory>
#include <optional>
#include <string>
#include <vector>

#include "net/editor_document.h"
#include "net/editor_complexity.h"
#include "editor_native_scale.h"
#include "editor_native_cast.h"
#include "editor_native_authoring.h"
#include "sporekit.h"

namespace editor_native {

inline constexpr uint32_t kPlacementLimit = 4096;
struct ProjectionCacheStats {size_t bytes=0,entries=0;uint64_t hits=0,misses=0;};
ProjectionCacheStats projection_cache_stats();
// Inclusive game-thread CPU spans. These local diagnostics never enter a draft
// or packet; nested Read/Write/projection work is intentionally counted twice.
struct WorkSpan {uint64_t calls=0,ticks=0;};
enum class WorkStep : size_t {IntegrityWrite,IntegrityClosureWrite,BaseRead,ProducerWrite,AfterRead,FixedpointWrite,InverseWrite,Count};
struct WorkStepSpan {WorkSpan work;uint64_t begin=0,end=0,frame=0;};
struct WorkStats {
    uint64_t frequency=0;
    WorkSpan reads,writes,projections,resolutions,supported;
    WorkSpan snapshot_copies; // CPU-only owned value construction, no Native calls.
    // Last completed QPC interval/frame correlates cumulative call-site work
    // with the existing operation recorder; it grants no continuation authority.
    std::array<WorkStepSpan,size_t(WorkStep::Count)> steps;
    // Before, AFTER bytes, AFTER typed projection, canonical-before proof,
    // opaque forward proof, and later delivery. Inclusive local CPU spans.
    std::array<WorkSpan,6> staged;
    uint64_t parsed_hits=0,parsed_misses=0,pose_captures=0,pose_fallbacks=0;
    size_t parsed_entries=0,parsed_bytes=0;
    // Retained owners include borrowed entries no longer indexed in the cache;
    // ordinary uncached local Read owners remain the existing fallback path.
    size_t parsed_owners=0,parsed_owner_bytes=0,parsed_peak_owners=0,parsed_peak_bytes=0;
};
WorkStats work_stats();
// Main-thread passive call-stack state. No Native getters or ownership proof.
bool service_busy();

struct Binding {
    editor::ObjectId object;
    uint32_t marker = 0;
    bool operator==(const Binding&) const = default;
};

struct Snapshot {
    struct Metadata {
        std::array<std::u16string,3> text; // native name, description and tags
        uint32_t genre = 0;
        bool operator==(const Metadata&) const = default;
    };
    struct Dirty { uint32_t saved_revision = 0; bool dirty = false, imported = false; };
    // Owned native serialization, including fields absent from the projection.
    std::vector<uint8_t> bytes;
    editor::ResourceKey source;
    editor::ResourceKey captain;
    bool captain_locked = false;
    editor::Document document;
    // Dense marker indices in bytes after native Read, never live pool slots.
    std::vector<Binding> bindings;
    uint32_t act = 0;
    // Local rollback metadata only. Never serialize this into a network artifact.
    std::optional<Dirty> local_dirty;
    std::optional<Metadata> metadata;
};

struct State {
    bool available = false, editing = false, ready = false, playtesting = false, gesture = false;
    bool terraforming = false;
    uint32_t entry = 0, act = 0;
    uint64_t generation = 0;
    uint32_t history_size = 0, history_cursor = 0;
    std::vector<editor::ObjectId> selected;
    bool world_visible = true; // Native editor layout, excluding fullscreen pickers.
};

// Local values only, never persisted or sent to another player. The Runtime
// pins these to one exact project/epoch/playtest round. No native pointer is
// retained across Test, including the camera itself.
struct LocalView {
    std::array<float, 3> target{};
    std::array<float, 4> rotation{};
    std::array<float, 3> orbit{};
    // Native camera settings used by its final transform/viewer consumer. They
    // must match on return; the adapter never overwrites camera configuration.
    std::array<float, 4> settings{};
    bool operator==(const LocalView&) const = default;
};
// Opaque values only. Match freshly resolved owners; never dereference retained
// addresses after a creation editor has replaced the active game mode.
struct SceneOwner {
    uintptr_t mode=0,data=0,resource=0;
    editor::ResourceKey source;
    bool operator==(const SceneOwner&) const = default;
};
struct SceneResumeState {
    uint64_t generation=0;
    uint32_t initialization=0;
    bool available=false,owner=false,ready=false,enter_pending=false,transitioning=false,world_visible=false;
    bool settled() const {return available&&owner&&ready&&initialization==4&&!enter_pending&&!transitioning&&world_visible;}
};

struct GestureScope {
    editor::ProjectId project{};
    uint64_t epoch = 0;
    editor::Peer author;
    bool operator==(const GestureScope&) const = default;
};
struct CaptureTicket {
    GestureScope scope;
    SceneOwner owner;
    uint64_t generation=0,gesture=0;
    uint32_t act=0;
    bool operator==(const CaptureTicket&) const = default;
};
enum class CaptureState {Wait,Ready,Retired};
struct PoseSupportProof;
struct Gesture {
    Snapshot before, after;
    uint64_t generation = 0;
    std::optional<editor::ObjectId> clone_source;
    std::optional<CaptureTicket> capture;
    // Opaque, owned values sealed only by the native producer after both the
    // genuine typed projection and exact opaque comparison have completed.
    std::shared_ptr<const PoseSupportProof> capture_proof;
};
struct CancellationTicket {
    GestureScope scope;
    uint64_t generation = 0, gesture = 0, serial = 0;
    bool operator==(const CancellationTicket&) const = default;
};

struct Ctx {
    // active gates only this shared document, never unrelated native editing.
    std::function<bool()> active;
    // Cached logical identity, captured before an owned native gesture starts.
    std::function<GestureScope()> gesture_scope;
    // Read-only proof identity across submitted operations and owned test
    // transitions. This grants no input/lease/native mutation permission;
    // authoring still uses gesture_scope and its existing input holds.
    std::function<GestureScope()> projection_scope;
    // Pure cached cohort comparison for an ALREADY captured local capability.
    // Never call native getters from here; this grants no mutation permission.
    std::function<bool(const editor::ComplexityCapability&)> complexity_current;
    // The runtime owns the creation session nonce and authorizes only its
    // current, not-yet-materialized return. Called synchronously outside frame.
    std::function<bool(const GestureScope&,uint64_t serial)> creation_return_current;
    // Native validation cancelled the current entry, not the user's history.
    // Called outside Present after the finalizer and actual frame advance.
    std::function<bool(const CancellationTicket&, std::string&)> cancelled;
    std::function<void(Gesture)> gesture;
    // Optional ordinary-pose staging. Runtime holds local input/detaches the
    // local scene; these callbacks grant read identity, never new input scope.
    // False+empty begin opts out without publishing a hold. Each Native phase
    // requires a strictly later game frame. No parsed owner crosses a drain.
    std::function<bool(const CaptureTicket&,std::string&)> capture_begin;
    std::function<CaptureState(const CaptureTicket&)> capture_current;
    std::function<bool(const CaptureTicket&,const Snapshot&,std::string&)> capture_before;
    // Called only after Native retires its staged owners/state, so an exact
    // original-owner restoration can use the ordinary readiness contract.
    std::function<void(const CaptureTicket&,const std::string&)> capture_failed;
    // Native release boundary before its after-snapshot serialization. The
    // runtime stamps this immediately so expensive native Commit work is timed.
    std::function<void()> gesture_released;
    std::function<void(bool)> gesture_capture; // Deferred native snapshot begin/end.
    std::function<void(bool)> history; // false Undo, true Redo.
    // Cached chronological availability; native toolbar queries perform no work.
    std::function<bool(bool)> history_available;
    std::function<void(const std::string&)> failure;
    // A blocked legacy control is a notice, never a failure/freeze.
    std::function<void(const std::string&)> notice;
    // Default true for new/unshared editors; Runtime scopes legacy recovery.
    std::function<bool()> relative_scale_tracked;
    std::function<bool()> extended_poses_tracked;
    std::function<bool()> cast_tracked;
    std::function<bool()> authoring_tracked;
    // Native mutation input must be held until this exact selection has leases.
    std::function<bool(const std::vector<editor::ObjectId>&)> gate;
    std::function<std::optional<editor::Revision>(editor::ObjectId)> object_revision;
    // Whole canonical head for restoring an independently owned full artifact.
    // Placement revisions alone cannot detect a disjoint terrain commit.
    std::function<std::optional<editor::Revision>()> canonical_revision;
    std::function<bool(uint32_t)> structure_gate; // Existing native class ID.
    std::function<bool(uint32_t)> class_gate;
    std::function<bool(editor::ObjectId)> settings_gate;
    // New terrain strokes take no exclusive lease. Existing records and
    // planet controls have independent checked scopes supplied before mutation.
    std::function<bool(const std::vector<editor::ObjectId>&,const std::vector<uint32_t>&)> terrain_gate;
    std::function<bool()> navigation_allowed;
    std::function<void(int)> act_action;
    std::function<bool(uint32_t)> remove_class_gate;
    std::function<bool(const Snapshot&,std::string&)> validate_dependencies;
    std::function<bool()> transaction_pending;
    // Stable batch owner even while a submitted local operation settles.
    std::function<GestureScope()> import_scope;
};

bool init(const SkHost*, SkMod*, Ctx);
void shutdown();
void tick(); // Queues native UI fences outside Present.
// Outside-frame Runtime tail only, after canonical work. Advances at most one
// staged capture phase, and only on a strictly later actual game frame.
void service_capture();
State state();
bool terraform_navigation_allowed(); // Ordinary local Build/Terraform browsing.
bool extended_complexity_available();
// Scene-independent, read-only evidence. False still fills a valid unavailable
// value (zero tuning words plus reason) for authenticated peer publication.
bool complexity_capability(editor::ComplexityCapability&,std::string& error);
// Diagnostic only: one natural ratio query, no Measure call or admission proof.
enum class ComplexityObservationState {Disarmed,Pending,Ready,Expired};
struct ComplexityObservation {
    uint64_t serial=0,frame=0,qpc=0,generation=0;
    uint32_t markers=0;
    std::array<uint32_t,12> counts{};
    std::array<uint32_t,13> tuning{};
    float weighted=0,native_marker=0,extended_marker=0,original=0,effective=0;
    bool extended=false;
};
uint64_t sample_complexity(const GestureScope&,editor::Revision,std::string& error);
ComplexityObservationState complexity_observation(const GestureScope&,editor::Revision,ComplexityObservation&,std::string& error);
editor::ResourceKey avatar();
bool avatar_locked();
// Local browsing only. Does not create a shared authored transaction.
bool select_act(uint32_t, std::string& error);
// Binding ownership survives native vector reorders and snapshot reconstruction.
void bind(const std::vector<Binding>&);
// Stable logical acts follow their native ordering across roster transactions.
void bind_acts(const editor::Document&);
// 0 add after current, 1 delete current, 2 move left, 3 move right.
// Builds an owned native gesture without retaining any native UI payload.
bool edit_acts(int action, const editor::Document&, std::string& error);
// Normal local native selection, outside Present; never changes authored bytes.
bool select_placement(editor::ObjectId, std::string& error);
bool capture(Snapshot&, std::string& error);
// Freshly serialize the ready live scene and compare it with an already
// validated owned image. Different requests the ordinary full capture path;
// Error fences an expired identity or invalid expected image. No native owner
// or parsed pointer survives this synchronous call.
enum class RecentMatches {Match,Different,Error};
RecentMatches recent_matches(const Snapshot&,const editor::Document&,uint64_t expected_generation,
                             Snapshot& out,std::string& error);
// Initial unsaved baseline: export the active native planet properties into an
// owned resource copy, as native Save does, without publishing a creation.
bool capture_draft(Snapshot&, std::string& error);
// Decode an owned legacy snapshot privately. Does not open/modify the live editor.
bool expand_snapshot(const Snapshot&,bool relative_scale,Snapshot&,std::string& error);
// Private recovery copies only: retain class/history revisions and references.
bool canonicalize_cast_state(editor::State&,std::string& error);
uint32_t creation_group(); // Native configured group, not a mod-owned namespace.
bool validate_entry(const std::vector<uint8_t>&, uint32_t act, std::string& error);
// Outside Present, from the actual active native editor camera. These fail
// closed when that camera or its exact current context is unavailable.
bool capture_view(LocalView&, std::string& error);
bool restore_view(const LocalView&, uint64_t expected_generation, std::string& error);
// Read identity and caller policy remain pinned across the existing camera
// callbacks. The copied callback must inspect only owned Runtime state.
bool restore_view_fenced(const LocalView&,uint64_t expected_generation,SceneOwner owner,GestureScope scope,
                         std::function<bool()> current,std::string& error);
bool restore_fenced(const Snapshot&,uint64_t expected_generation,SceneOwner owner,GestureScope scope,
                    std::function<bool()> current,std::string& error);
bool capture_scene_owner(SceneOwner&,std::string& error);
// An already-owned Save/cancel may rotate its key and rebuild this same scene.
// Readiness is separate; no restore is authorized until the caller checks it.
bool saved_scene_current(SceneOwner,uint64_t expected_generation);
SceneResumeState creation_scene_state(const SceneOwner&);
bool resume_creation_scene_camera(const GestureScope&,uint64_t serial,uint64_t expected_generation,const SceneOwner&,std::string& error);
// One participant-local creation only. This opaque token owns exact primitive
// materialization values and entry identity, never native objects. Retained
// address identities are compared only against freshly verified native owners.
// Capture before leaving; resume once after the same native scene has settled.
// A reopened adventure establishes fresh terrain provenance instead.
struct CreationTerrainProof;
using CreationTerrainTicket=std::shared_ptr<const CreationTerrainProof>;
bool capture_creation_terrain(const GestureScope&,uint64_t serial,const SceneOwner&,const Snapshot& entry,
                              CreationTerrainTicket&,std::string& error);
bool resume_creation_terrain(const CreationTerrainTicket&,const GestureScope&,uint64_t serial,const SceneOwner&,
                             const Snapshot& entry,uint64_t returned_generation,std::string& error);
// Explicit value-proof receipts for native Save/export and recovery. These
// fences never grant mutation leases or permit an input gesture.
enum class TerrainTransitionKind {Save,Export,Recovery,RecoveryBootstrap,ExportRebuild};
struct TerrainTransitionKey {
    GestureScope scope;uint64_t serial=0,generation=0;SceneOwner owner;
    bool operator==(const TerrainTransitionKey&)const=default;
};
struct TerrainTransitionReceipt {
    TerrainTransitionKind kind=TerrainTransitionKind::Save;
    TerrainTransitionKey before,after;
    uint64_t save_generation=0,save_serial=0;
    bool operator==(const TerrainTransitionReceipt&)const=default;
};
using TerrainTransitionCurrent=std::function<bool()>;
struct TerrainTransitionProof;
using TerrainTransitionTicket=std::shared_ptr<const TerrainTransitionProof>;
bool capture_terrain_transition(const TerrainTransitionKey&,const Snapshot&,const TerrainTransitionCurrent&,
                                TerrainTransitionTicket&,std::string& error);
bool refresh_terrain_transition(const TerrainTransitionTicket&,const TerrainTransitionKey&,const Snapshot& expected,
                                const TerrainTransitionCurrent&,TerrainTransitionTicket&,Snapshot& observed,std::string& error);
bool normalize_saved_terrain(const TerrainTransitionTicket&,const TerrainTransitionReceipt&,const Snapshot& expected,
                             Snapshot& saved,const TerrainTransitionCurrent&,std::string& error);
bool restore_saved_terrain(const TerrainTransitionTicket&,const TerrainTransitionReceipt&,const Snapshot& saved,
                           const TerrainTransitionCurrent&,TerrainTransitionTicket& prepared,std::string& error);
bool resume_terrain_transition(const TerrainTransitionTicket&,const TerrainTransitionReceipt&,const Snapshot& expected,
                               const TerrainTransitionCurrent&,std::string& error,Snapshot* observed=nullptr);
bool restore_terrain_transition(const TerrainTransitionTicket&,const TerrainTransitionReceipt&,const Snapshot& expected,
                                const TerrainTransitionCurrent&,TerrainTransitionTicket& prepared,std::string& error);
// Reject unsupported authored changes after private normalization of known poses.
bool supported_changes(const Gesture&, std::string& error);
// Bound synchronous validation work. Any retained projection proof owns only
// immutable bytes/values and rechecks generation, scope and asset resolution.
struct ValidationScope {
    ValidationScope();
    ~ValidationScope();
    ValidationScope(const ValidationScope&)=delete;
    ValidationScope& operator=(const ValidationScope&)=delete;
};
bool validate(const Snapshot&, const editor::Document&, std::string& error);
// A verified native Save grounds marker XYZ along their existing radial ray.
// Normalize only that derived radial displacement in an owned readback copy;
// every authored direction, elevation, rotation, property and recipe must match.
bool normalize_saved_grounding(Snapshot&,const editor::Document&,std::string& error);
bool restore(const Snapshot&, std::string& error);
// Resource-only canonical merge. Does not inspect or mutate the active scene,
// selection, native history, camera, or current native placement mappings.
// Structural additions require their owned native artifact; all retained fields
// are merged into base so unrelated accepted changes cannot be overwritten.
bool prepare(const Snapshot& base, const editor::Document&, const editor::Change&,
             const Snapshot* artifact, Snapshot& out, std::string& error);
// Experimental two-frame private preparation. Calls and cancellation belong to
// the game thread outside Present. The value ticket owns no Native pointer;
// at most one private AFTER resource is retained inside this adapter.
struct PosePrepareTicket {uint64_t serial=0;bool operator==(const PosePrepareTicket&)const=default;};
enum class PosePrepareResult {Fallback,Pending,Ready,Error};
PosePrepareResult begin_pose_prepare(const Snapshot&,const editor::Document&,const editor::Change&,
                                    const std::function<bool()>& current,PosePrepareTicket&,std::string&);
PosePrepareResult finish_pose_prepare(PosePrepareTicket,Snapshot&,std::string&);
void cancel_pose_prepare(PosePrepareTicket);
bool apply_prepared_checked(const Snapshot&,const editor::Change&,uint64_t generation,const SceneOwner&,
                            const std::function<bool()>& current,std::string&);
// Reviewed native authoring helpers may mutate this independently owned
// ScenarioResource synchronously. Never retain the pointer or call scene APIs.
// The result is reprojected with base's explicit schemas and stable bindings.
bool edit_owned_snapshot(const Snapshot&, const std::function<bool(void*,std::string&)>&,
                         Snapshot& out, std::string& error);
// Ordered canonical Crew references, including duplicate slots. Legacy drafts
// without Crew support return an empty list; malformed advertised rows reject.
bool crew_assets(const editor::Document&,std::vector<editor::ResourceKey>&,std::string& error);
struct CrewAssetRemap {
    uint32_t slot=0;
    editor::ResourceKey expected,replacement;
};
// Sanctioned immutable-version rewrite in an owned snapshot only. Each checked
// slot changes its primary key and local hints; all other row bytes, roster
// order/capacity, draft revisions and unrelated authored values are preserved.
bool remap_crew_assets(const Snapshot&,const std::vector<CrewAssetRemap>&,Snapshot&,std::string& error);
// Materialize precisely the already validated canonical bytes. A pose-only
// patch updates existing actors without serializing the whole adventure again.
bool apply_prepared(const Snapshot&, const editor::Change&, std::string& error);
// Retire native-only history after the canonical draft has settled/restored.
bool rebaseline(std::string& error);
// Preserves opaque fields and the local camera. Unsupported structural changes
// fail before changing the live resource.
bool apply(const editor::Document&, std::string& error);
bool apply(const editor::Document&, const editor::Change&, const Snapshot* artifact, std::string& error);
bool enter(const editor::ResourceKey&, std::string& error);
// Outside Present. Reads only the selected in-progress native transform.
bool selection_pose(editor::ObjectId& object, editor::ObjectId& act, editor::Pose& pose);
struct LivePreview {
    editor::Peer author;
    editor::ObjectId object, act, class_id;
    editor::Pose pose;
    bool creating = false;
    std::optional<editor::Pose> destination;
};
bool selection_preview(LivePreview&);
// Render components only, outside Present. Never writes authored marker bytes.
void update_previews(const std::vector<LivePreview>&);
std::string visual_status(); // Read-only native render positions, outside frame.
std::string authoring_status(); // Bounded read-only act/captain diagnostics.
// Outside frame, after the current scoped ownership request was denied.
void deny_input(const std::vector<editor::Scope>&);
std::string status();

} // namespace editor_native
