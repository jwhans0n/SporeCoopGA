// Exact-build native Adventure Editor adapter. Call mutations outside Present.
#pragma once

#include <array>
#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "net/editor_document.h"
#include "editor_native_scale.h"
#include "editor_native_cast.h"
#include "editor_native_authoring.h"
#include "sporekit.h"

namespace editor_native {

inline constexpr uint32_t kPlacementLimit = 1024;

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

struct Gesture {
    Snapshot before, after;
    uint64_t generation = 0;
    std::optional<editor::ObjectId> clone_source;
};

struct GestureScope {
    editor::ProjectId project{};
    uint64_t epoch = 0;
    editor::Peer author;
    bool operator==(const GestureScope&) const = default;
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
    // Native validation cancelled the current entry, not the user's history.
    // Called outside Present after the finalizer and actual frame advance.
    std::function<bool(const CancellationTicket&, std::string&)> cancelled;
    std::function<void(Gesture)> gesture;
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
    std::function<bool(uint32_t)> structure_gate; // Existing native class ID.
    std::function<bool(uint32_t)> class_gate;
    std::function<bool(editor::ObjectId)> settings_gate;
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
State state();
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
// Reject unsupported authored changes after private normalization of known poses.
bool supported_changes(const Gesture&, std::string& error);
bool validate(const Snapshot&, const editor::Document&, std::string& error);
bool restore(const Snapshot&, std::string& error);
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
