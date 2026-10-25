#pragma once

#include <functional>
#include <string>
#include <vector>

#include "net/session.h"
#include "sporekit.h"

namespace adventure_flow {
enum class Kind : uint8_t { None, Intro, Talk };
struct Scene {
    uint64_t run = 0, serial = 0;
    Kind kind = Kind::None;
    uint8_t act = 0, goal = 0;
    uint32_t marker = 0, incarnation = 0;
};
struct Confirmation {
    uint8_t player = 0;
    std::string name;
    bool local = false, confirmed = false, pending = false;
};
struct ConfirmationView {
    Scene scene;
    std::vector<Confirmation> players;
};
struct Ctx {
    net::Session* sess = nullptr;
    std::function<uint64_t()> run;
    std::function<bool()> running;
    std::function<std::vector<uint8_t>()> members;
    std::function<bool(uint8_t, const Scene&)> validate_talk;
    std::function<bool(const Scene&)> present;
    std::function<void(const Scene&)> release;
    // Native caption advancement may finish after release() returns. This
    // callback reads adapter state only; it must not call held() recursively.
    std::function<bool()> settling;
    std::function<void(bool)> waiting;
};
bool init(const SkHost*, Ctx);
void tick();
void on_message(uint8_t, net::Reader&);
void reset();
void confirm_intro();
bool request_talk(Scene);
bool finish_talk();
bool held();
// Holds native dialog resources, excluding an unpresented future Talk whose
// prerequisite authoritative act may still need to be applied locally.
bool native_scene_held();
// True between receipt of a guest Release and the native release callback's
// return, before/while the adapter establishes asynchronous settling ownership.
bool release_pending();
bool local_waiting();
bool ready(uint8_t player);
std::string status();
Scene scene();
// Read-only native letterbox HUD state; confirmed means acknowledged by the host.
bool confirmation_view(ConfirmationView&);
} // namespace adventure_flow
