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
struct Ctx {
    net::Session* sess = nullptr;
    std::function<uint64_t()> run;
    std::function<bool()> running;
    std::function<std::vector<uint8_t>()> members;
    std::function<bool(uint8_t, const Scene&)> validate_talk;
    std::function<bool(const Scene&)> present;
    std::function<void(const Scene&)> release;
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
bool local_waiting();
bool ready(uint8_t player);
std::string status();
Scene scene();
} // namespace adventure_flow
