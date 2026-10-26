// Exact-build native Adventure Editor adapter. Call mutations outside Present.
#pragma once

#include <cstdint>
#include <functional>
#include <map>
#include <optional>
#include <string>
#include <vector>

#include "net/editor_document.h"
#include "editor_native_scale.h"
#include "sporekit.h"

namespace editor_native {

inline constexpr uint32_t kPlacementLimit = 1024;

struct Binding {
    editor::ObjectId object;
    uint32_t marker = 0;
    bool operator==(const Binding&) const = default;
};

struct Snapshot {
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
};

struct State {
    bool available = false, editing = false, ready = false, playtesting = false, gesture = false;
    uint32_t entry = 0, act = 0;
    uint64_t generation = 0;
    uint32_t history_size = 0, history_cursor = 0;
    std::vector<editor::ObjectId> selected;
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
    // Native mutation input must be held until this exact selection has leases.
    std::function<bool(const std::vector<editor::ObjectId>&)> gate;
    std::function<bool(uint32_t)> structure_gate; // Existing native class ID.
};

bool init(const SkHost*, SkMod*, Ctx);
void shutdown();
void tick(); // Queues native UI fences outside Present.
State state();
editor::ResourceKey avatar();
bool avatar_locked();
// Binding ownership survives native vector reorders and snapshot reconstruction.
void bind(const std::vector<Binding>&);
// Normal local native selection, outside Present; never changes authored bytes.
bool select_placement(editor::ObjectId, std::string& error);
bool capture(Snapshot&, std::string& error);
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
std::string status();

} // namespace editor_native
