#pragma once

#include <functional>
#include <string>
#include <vector>

#include "net/session.h"

// Native menu pause counters are local intent. Only the host's unanimous,
// run-scoped snapshot turns that intent into a shared simulation hold.
namespace adventure_pause {
struct Ctx {
    net::Session* sess = nullptr;
    std::function<uint64_t()> run;
    std::function<bool()> running;
    // Immutable launch roster; host live membership also checks Session and
    // eligible(). Guests validate the authoritative snapshot against this roster,
    // without requiring a matching locally observed peer-leave notification.
    std::function<std::vector<uint8_t>()> members;
    std::function<bool(uint8_t)> eligible;
    std::function<bool()> local_intent;
    // Queue native flag recalculation; never call held() recursively here.
    std::function<void()> changed;
};
bool init(Ctx);
void reset();
void tick();
void local_intent(bool);
void on_message(uint8_t from, net::Reader&);
bool active();
bool held();
bool local_requested();
std::string status();
} // namespace adventure_pause
