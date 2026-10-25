// Win32 callback ABI and lifecycle fixture. These fake native callbacks model
// native-owned controller construction/refcounts, not a successful game UI run.
#include <array>
#include <bit>
#include <cstdio>
#include <cstring>
#include <map>
#include <set>
#include <string>

#include "adventure_social.h"
#include "sporekit_game.hpp"

namespace {
using namespace skg;
constexpr const char* kHash = "dc04aee5a3debc3f1ad4c1a937460e99a29b9bd3bc285008be83615dd5e59a37";
const char* hash = kHash;
bool active = true, init_result = true, tick_result = true, malformed_controller;
uint64_t run = 7;
int act = 2, failures, starts, ticks, ends, controller_refs = 1, constructions;
uint64_t frame;
float expected_delta = 0.125f;
int native_eligibility_result = 5, eligibility_calls;
bool fail_hook;
using Eligibility = int(SK_CDECL*)(void*, void*);
Eligibility eligibility_hook;
std::array<uint8_t, 0xE80> avatar{}, npc{}, supporter{}, unrelated{};
std::array<uint8_t, 0x60C> avatar_behavior{}, npc_behavior{}, supporter_behavior{}, unrelated_behavior{};
std::array<uint8_t, 0x230> controller{};
std::array<uint8_t, 0x30> npc_order{}, supporter_order{}, unrelated_order{};
std::set<void*> listed, mirrors;
std::map<void*, void*> actor_behavior, orders, order_targets, states;
void* partner = npc.data();
void check(bool value, const char* message)
{
    if (!value) { ++failures; std::printf("FAIL: %s\n", message); }
}
void common(void* actor, uint32_t lo, uint32_t hi, uint32_t a, uint32_t b, void* state)
{
    check(actor == npc.data() || actor == supporter.data(), "only engaged bound native actors receive social callbacks");
    check(!lo && !hi && !a && !b && state && (uintptr_t(state) & 3) == 0,
          "native callback words and aligned POD storage survive");
}
bool SK_CDECL native_start(void* actor, uint32_t lo, uint32_t hi, uint32_t a, uint32_t b, void* state, uint32_t node)
{
    common(actor, lo, hi, a, b, state); check(!node, "init seventh stack word is present"); ++starts;
    field<uint32_t>(state, 0) = 0; field<uint32_t>(state, 4) = 0;
    field<uint8_t>(state, 8) = actor == npc.data(); field<int>(state, 12) = -1;
    states[actor] = state;
    if (actor == npc.data()) {
        ++constructions;
        controller.fill(0);
        field<uint32_t>(controller.data(), 8) = malformed_controller ? 0xDEADBEEF : 0x14FCF96E;
        field<void*>(controller.data(), 0x1A4) = npc.data();
        field<void*>(controller.data(), 0x1A8) = avatar.data();
    }
    field<void*>(actor_behavior.at(actor), 0x600) = controller.data(); ++controller_refs;
    orders.erase(static_cast<uint8_t*>(actor_behavior.at(actor)) + 8);
    return init_result;
}
bool SK_CDECL native_tick(void* actor, uint32_t lo, uint32_t hi, uint32_t a, uint32_t b,
                          void* state, uint32_t node, float delta)
{
    common(actor, lo, hi, a, b, state); ++ticks;
    check(!node && states.at(actor) == state && field<int>(state, 12) == -1,
          "tick uses the same native-initialized callback state");
    check(std::bit_cast<uint32_t>(delta) == std::bit_cast<uint32_t>(expected_delta), "milliseconds convert to native float seconds");
    ++field<uint32_t>(state, 4);
    return tick_result;
}
void SK_CDECL native_end(void* actor, uint32_t lo, uint32_t hi, uint32_t a, uint32_t b, void* state)
{
    common(actor, lo, hi, a, b, state); ++ends;
    check(states.at(actor) == state && field<void*>(actor_behavior.at(actor), 0x600) == controller.data(),
          "exit sees its original state and native-owned controller before release");
    field<void*>(actor_behavior.at(actor), 0x600) = nullptr; --controller_refs; states.erase(actor);
}
uint8_t* SK_THISCALL native_order(void* self, uint32_t low, uint32_t high)
{
    check(low == 0x10000 && !high, "only explicit native social orders are queried");
    auto found = orders.find(self); return found == orders.end() ? nullptr : static_cast<uint8_t*>(found->second);
}
void* SK_CDECL native_order_target(void* order) { return order_targets.at(order); }
void* SK_THISCALL native_target(void* self)
{
    check(self == avatar.data(), "the actual local captain supplies the social partner"); return partner;
}
int SK_CDECL native_eligibility(void* first, void* second)
{
    ++eligibility_calls;
    check(first && second, "eligibility preserves both caller arguments");
    return native_eligibility_result;
}
int hook_create(SkMod*, void* original, void* replacement, void** trampoline)
{
    check(original == reinterpret_cast<void*>(&native_eligibility), "only verified ability eligibility is hooked");
    if (fail_hook) return 1;
    *trampoline = original; eligibility_hook = reinterpret_cast<Eligibility>(replacement); return 0;
}
std::map<std::string, uintptr_t> symbols;
uintptr_t symbol(const char* name) { auto found = symbols.find(name); return found == symbols.end() ? 0 : found->second; }
const char* exe_hash() { return hash; }
uint64_t frame_index() { return ++frame; }
adventure_social::Ctx context()
{
    return {[] { return active; }, [] { return run; }, [] { return act; }, []() -> void* { return avatar.data(); },
        [](void* actor) { return mirrors.count(actor) != 0; }, [](void* actor) { return listed.count(actor) != 0; }};
}
void pending(void* actor, void* order, int kind, void* recipient)
{
    orders[static_cast<uint8_t*>(actor_behavior.at(actor)) + 8] = order;
    field<int>(order, 12) = kind; order_targets[order] = recipient;
}
void begin()
{
    pending(npc.data(), npc_order.data(), 1, avatar.data());
    adventure_social::blocked_update(npc.data(), 125);
}
void balanced(const char* message) { check(controller_refs == 1 && states.empty(), message); }
} // namespace

int main()
{
    listed = {avatar.data(), npc.data(), supporter.data(), unrelated.data()};
    mirrors = {npc.data(), supporter.data(), unrelated.data()};
    actor_behavior = {{avatar.data(), avatar_behavior.data()}, {npc.data(), npc_behavior.data()},
        {supporter.data(), supporter_behavior.data()}, {unrelated.data(), unrelated_behavior.data()}};
    unsigned incarnation = 30;
    for (auto [actor, data] : actor_behavior) { field<void*>(actor, 0xB4C) = data; field<uint32_t>(actor, 0x24) = incarnation++; }
    symbols = {
        {"SporeCoop::Adventure::SocialProposeStart", reinterpret_cast<uintptr_t>(&native_start)},
        {"SporeCoop::Adventure::SocialProposeTick", reinterpret_cast<uintptr_t>(&native_tick)},
        {"SporeCoop::Adventure::SocialProposeEnd", reinterpret_cast<uintptr_t>(&native_end)},
        {"SporeCoop::Adventure::GetBehaviorOrder", reinterpret_cast<uintptr_t>(&native_order)},
        {"SporeCoop::Adventure::SocialOrderTarget", reinterpret_cast<uintptr_t>(&native_order_target)},
        {"SporeCoop::Adventure::CreatureTarget", reinterpret_cast<uintptr_t>(&native_target)},
        {"SporeCoop::Adventure::SocialAbilityEligibility", reinterpret_cast<uintptr_t>(&native_eligibility)},
    };
    SkHost host{}; host.sym = symbol; host.exe_sha256 = exe_hash; host.frame_index = frame_index; host.hook_create = hook_create;
    hash = "unverified";
    check(!adventure_social::init(&host, nullptr, context()), "unverified executable cannot initialize");
    hash = kHash; auto saved = symbols["SporeCoop::Adventure::SocialProposeTick"];
    symbols.erase("SporeCoop::Adventure::SocialProposeTick");
    check(!adventure_social::init(&host, nullptr, context()), "missing callback capability fails closed");
    symbols["SporeCoop::Adventure::SocialProposeTick"] = saved;
    fail_hook = true;
    check(!adventure_social::init(&host, nullptr, context()), "failed eligibility hook leaves native social unavailable");
    fail_hook = false;
    check(adventure_social::init(&host, nullptr, context()), "exact native capabilities initialize");

    adventure_social::blocked_update(npc.data(), 125);
    pending(unrelated.data(), unrelated_order.data(), 1, avatar.data());
    adventure_social::blocked_update(unrelated.data(), 125);
    check(!starts && !ticks, "no order and unrelated NPC cannot start a social game");
    pending(npc.data(), npc_order.data(), 2, avatar.data());
    adventure_social::blocked_update(npc.data(), 125);
    check(!starts, "performer must have exact native propose order type one");
    begin();
    check(starts == 1 && ticks == 1 && controller_refs == 2, "native init constructs and owns its social controller");
    pending(supporter.data(), supporter_order.data(), 2, npc.data());
    adventure_social::blocked_update(supporter.data(), 125);
    check(starts == 2 && ticks == 2 && constructions == 1 && controller_refs == 3,
          "explicit native supporter joins the same controller without a second UI game");
    for (int i = 0; i < 10000; ++i) adventure_social::blocked_update(npc.data(), 125);
    check(starts == 2 && ticks == 10002 && controller_refs == 3, "repeated eight-word callbacks preserve state stack and reference balance");
    tick_result = false; adventure_social::blocked_update(supporter.data(), 125);
    check(ends == 1 && controller_refs == 2, "false callback performs native exit exactly once");
    tick_result = true; partner = nullptr; adventure_social::blocked_update(npc.data(), 125);
    balanced("target cancellation detaches remaining native controller ownership");
    unsigned before = ends; adventure_social::blocked_update(npc.data(), 125);
    check(ends == int(before), "cancelled callbacks never exit twice");
    partner = npc.data(); begin(); active = false; adventure_social::blocked_update(npc.data(), 125);
    balanced("host solo inactive or frozen context cancels callback ownership"); active = true;
    begin(); ++act; adventure_social::blocked_update(npc.data(), 125); balanced("act change runs native exit before stale state is forgotten");
    begin(); ++run; adventure_social::blocked_update(npc.data(), 125); balanced("run change runs native exit exactly once");
    begin(); mirrors.erase(npc.data()); adventure_social::blocked_update(npc.data(), 125);
    balanced("binding removal cancels while native object is still listed"); mirrors.insert(npc.data());
    begin(); field<bool>(npc.data(), 0xB5E) = true; mirrors.erase(npc.data()); adventure_social::blocked_update(npc.data(), 125);
    balanced("native exit still closes presentation after actor death"); field<bool>(npc.data(), 0xB5E) = false; mirrors.insert(npc.data());
    init_result = false; begin(); balanced("partial native-init failure releases its constructed controller"); init_result = true;
    malformed_controller = true; begin(); balanced("unexpected native controller identity fails closed with native cleanup"); malformed_controller = false;
    field<uint32_t>(npc_behavior.data(), 0x1D8) = 0x341BD4BE; before = starts; begin();
    check(starts == int(before), "preexisting native BT owns its own callback lifecycle"); field<uint32_t>(npc_behavior.data(), 0x1D8) = 0;
    // A live native UI run reproduced state five: its performer has our owned
    // callback instead of a native Propose BT leaf, so the native ability tray
    // would silently reject the real captain's response. Only the exact ready
    // primary can relax this specific native result.
    begin();
    field<uint32_t>(avatar_behavior.data(), 0x1D8) = 0x941BD4BF;
    auto eligible = [&](void* first = avatar.data(), void* second = npc.data()) { return eligibility_hook(first, second); };
    check(eligible() == 5, "performer before response state retains native input block");
    field<uint32_t>(states.at(npc.data()), 0) = 6;
    check(eligible() == 5, "controller must explicitly permit a response");
    field<bool>(controller.data(), 0x1E4) = true;
    check(eligible() == 0, "native response is permitted for exact ready owned performer");
    check(field<uint32_t>(npc_behavior.data(), 0x1D8) == 0,
          "eligibility never fabricates native BT state");
    for (int value : {0, 1, 4, 6, -9}) {
        native_eligibility_result = value;
        check(eligible() == value, "all other native eligibility results remain unchanged");
    }
    native_eligibility_result = 5;
    active = false; check(eligible() == 5, "inactive or downed/frozen scope cannot permit input"); active = true;
    ++run; check(eligible() == 5, "old run cannot permit input"); --run;
    ++act; check(eligible() == 5, "old act cannot permit input"); --act;
    mirrors.erase(npc.data()); check(eligible() == 5, "stale mirror binding cannot permit input"); mirrors.insert(npc.data());
    ++field<uint32_t>(npc.data(), 0x24); check(eligible() == 5, "changed native incarnation cannot permit input"); --field<uint32_t>(npc.data(), 0x24);
    check(eligible(unrelated.data(), npc.data()) == 5 && eligible(avatar.data(), unrelated.data()) == 5,
          "other captains, NPCs and unowned callbacks retain native input restrictions");
    partner = unrelated.data(); check(eligible() == 5, "captain must still target this performer"); partner = npc.data();
    field<uint32_t>(avatar_behavior.data(), 0x1D8) = 0;
    check(eligible() == 5, "captain must own native SocialRespond"); field<uint32_t>(avatar_behavior.data(), 0x1D8) = 0x941BD4BF;
    field<bool>(controller.data(), 0x1C) = true;
    check(eligible() == 5, "completed controller cannot permit another response"); field<bool>(controller.data(), 0x1C) = false;
    field<void*>(controller.data(), 0x1A8) = unrelated.data();
    check(eligible() == 5, "controller must belong to the actual captain"); field<void*>(controller.data(), 0x1A8) = avatar.data();
    field<bool>(avatar.data(), 0xB5E) = true;
    check(eligible() == 5, "dead native captain cannot permit input"); field<bool>(avatar.data(), 0xB5E) = false;
    for (int i = 0; i < 10000; ++i) check(eligible() == 0, "repeated two-argument cdecl response queries preserve stack balance");
    check(adventure_social::counters().input_queries_allowed == 10001 && eligibility_calls > 10000,
          "input override counts only authorized queries after native evaluation");
    adventure_social::reset(); balanced("eligibility fixture releases native callback ownership");
    begin(); adventure_social::reset(); balanced("explicit reset releases all live native callback ownership");
    check(adventure_social::available(), "reset leaves verified callback capability available");
    auto counts = adventure_social::counters();
    check(!counts.starts && !counts.ticks && !counts.ends && !counts.rejected && !counts.input_queries_allowed, "reset clears diagnostics");
    std::printf("adventure native social callbacks: %d failure(s)\n", failures);
    return failures ? 1 : 0;
}
