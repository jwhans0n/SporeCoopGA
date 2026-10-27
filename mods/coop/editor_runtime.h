// Live authoring coordination. Native work and persistence run outside Present.
#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <vector>

#include "net/session.h"
#include "sporekit.h"

namespace coop_editor {

struct PlayerView {
    uint8_t id = 255;
    std::string name, selection, status;
    bool host = false, local = false, ready = false;
    bool pending = false, owns_selection = false;
    uint64_t applied_revision = 0, durable_revision = 0;
};

struct TransformHint {
    std::string label;
    net::Vec3 position;
    net::Quat rotation;
    float scale = 1;
};

struct View {
    bool visible = false, active = false, offered = false, host = false;
    bool can_begin = false, can_undo = false, can_redo = false;
    bool can_save = false, can_playtest = false, can_recover = false;
    bool waiting = false, ready = false, frozen = false, natively_saved = false;
    bool confirming = false, playtesting = false, can_return = false, can_cancel = false;
    bool native_editing = false, join_prompt = false, test_request = false, retry = false, exporting = false;
    uint64_t revision = 0, durable_revision = 0, saved_revision = 0;
    std::string title, status, selection;
    std::string requester;
    std::string prompt_identity;
    std::vector<PlayerView> players;
    std::vector<TransformHint> previews;
};

struct Ctx {
    bool automatic_follow = true;
    net::Session* session = nullptr;
    std::function<void(const std::string&)> feed;
    std::function<bool()> adventure_busy;
    std::function<void()> open_panel, close_panel;
    std::function<void()> leave_party;
};

bool init(const SkHost*, SkMod*, Ctx);
void tick(double now); // Scheduling and immutable view reads are safe inside Present.
void on_message(uint8_t from, net::Reader&);
void on_adventure_message(uint8_t from, net::Reader&); // Ordered behind editor ownership.
void on_join(uint8_t);
void on_leave(uint8_t);
void session_ended();
void mode_changed(uint32_t from, uint32_t to);
void shutdown();
bool active();
bool authoring(); // Suppresses legacy gameplay replication only while editing.
bool playtest_owned();
View view();
// Immutable cached overlay data. No native work or resource creation.
const std::vector<TransformHint>& transform_hints();

// Actions enqueue work; native functions are never called by a render callback.
void begin();
void undo();
void redo();
void save();
void playtest();
void cancel_playtest(); // Host-only cancellation of this preparation round.
void set_ready(bool);
void recover();
void end();
void approve_playtest(bool);
void resolve_join(int choice); // 0 Save, 1 Discard, 2 Cancel.
std::string debug(const std::vector<std::string>& args);

} // namespace coop_editor
