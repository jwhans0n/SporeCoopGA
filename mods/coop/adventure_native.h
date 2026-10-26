// Native Galactic Adventures adapter. Every engine entry is resolved by verified symbol.
#pragma once

#include "adventure.h"
#include <array>

namespace adventure_native {

struct Ctx {
    net::Session* sess = nullptr;
    std::function<void(const std::string&)> feed;
    std::function<void*(uint8_t)> captain_of;
    std::function<bool(uint8_t, net::PlayerState&)> remote_state;
    std::function<void()> invalidate_ready;
    std::function<void(uint8_t, uint64_t, const net::Vec3&, const net::Quat&)> teleport;
    std::function<void()> reset_movement;
    std::function<void()> reset_pause_movement;
};

// Installed only after the editor controller's explicit party confirmations.
// Logical adventure identity may differ from this process's native draft key.
struct EditorPreview {
    std::array<uint8_t,16> project{};
    uint64_t epoch=0,round=0,revision=0;
    dbpf::Key adventure,authored_avatar;
    std::function<bool()> current; // exact project/epoch/round/revision ownership
    std::function<bool(std::string&)> launch;
    std::function<void()> return_editor; // queues a host-authoritative group return
};
// Outside Present: pins an owned native snapshot and its local resource identity.
bool set_editor_preview(EditorPreview, std::string& error);
bool editor_preview();
void clear_editor_preview();

bool init(const SkHost* host, SkMod* mod, Ctx context);
adventure::Ctx coordinator_context();
void tick(double now);
void on_message(uint8_t from, net::Reader& message);
void reset();
void shutdown();
bool update_filter(void* creature);
// Host-authoritative unanimous menu/user pause, independent of Intro/Talk holds.
bool party_paused();
void blocked_update(void* creature, int milliseconds);
void creature_update(void* creature, bool before);
bool damage_filter(void* victim, float& amount, int type, void* attacker);
std::string status();

} // namespace adventure_native
